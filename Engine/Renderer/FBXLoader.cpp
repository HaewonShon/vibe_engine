#include "FBXLoader.h"
#include "DX12Context.h"
#include "../Core/Log.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <vector>
#include <cmath>
#include <filesystem>
#include <memory>

using namespace DirectX;

namespace VibeEngine {

// ---------------------------------------------------------------------------
// ProcessMesh — convert one aiMesh into engine Vertex/index data.
// baseIndex offsets all indices so they are correct after merging into the
// global buffer.
// ---------------------------------------------------------------------------
static void ProcessMesh(const aiMesh*         mesh,
                        const aiMatrix4x4&    transform,
                        std::vector<Vertex>&   outVerts,
                        std::vector<uint32_t>& outIndices)
{
    const auto base = static_cast<uint32_t>(outVerts.size());

    // Normal matrix: upper-left 3x3 of the node transform.
    // Correct for rotation and uniform scale (the common FBX case).
    const aiMatrix3x3 m3(transform);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex v;

        aiVector3D pos = transform * mesh->mVertices[i];
        v.Position = { pos.x, pos.y, pos.z };

        v.Color = { 1.f, 1.f, 1.f, 1.f };

        if (mesh->HasTextureCoords(0)) {
            // aiProcess_FlipUVs already handles the V-flip for DX12
            v.TexCoord = { mesh->mTextureCoords[0][i].x,
                           mesh->mTextureCoords[0][i].y };
        } else {
            v.TexCoord = { 0.f, 0.f };
        }

        if (mesh->HasNormals()) {
            aiVector3D n = m3 * mesh->mNormals[i];
            float lenSq = n.x*n.x + n.y*n.y + n.z*n.z;
            if (lenSq > 1e-10f) {
                float inv = 1.f / std::sqrt(lenSq);
                n.x *= inv; n.y *= inv; n.z *= inv;
            }
            v.Normal = { n.x, n.y, n.z };
        } else {
            v.Normal = { 0.f, 1.f, 0.f };
        }

        if (mesh->HasTangentsAndBitangents()) {
            aiVector3D t = m3 * mesh->mTangents[i];
            aiVector3D b = m3 * mesh->mBitangents[i];
            float tLen = std::sqrt(t.x*t.x + t.y*t.y + t.z*t.z);
            float bLen = std::sqrt(b.x*b.x + b.y*b.y + b.z*b.z);
            if (tLen > 1e-10f) { t.x /= tLen; t.y /= tLen; t.z /= tLen; }
            if (bLen > 1e-10f) { b.x /= bLen; b.y /= bLen; b.z /= bLen; }
            v.Tangent   = { t.x, t.y, t.z };
            v.Bitangent = { b.x, b.y, b.z };
        } else {
            v.Tangent   = { 1.f, 0.f, 0.f };
            v.Bitangent = { 0.f, 0.f, 1.f };
        }

        outVerts.push_back(v);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
            outIndices.push_back(base + face.mIndices[j]);
    }
}

// ---------------------------------------------------------------------------
// ProcessNode — recursively traverse the scene node tree, accumulating the
// parent-to-world transform so each mesh is emitted in world space.
// Also tracks the material index of the largest (most-triangle) sub-mesh.
// ---------------------------------------------------------------------------
static void ProcessNode(const aiNode*          node,
                        const aiScene*         scene,
                        const aiMatrix4x4&     parentTransform,
                        std::vector<Vertex>&   outVerts,
                        std::vector<uint32_t>& outIndices,
                        unsigned int&          outDominantMatIndex,
                        unsigned int&          outDominantTriCount)
{
    aiMatrix4x4 transform = parentTransform * node->mTransformation;

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        ProcessMesh(mesh, transform, outVerts, outIndices);

        // Track which material covers the most triangles
        if (mesh->mNumFaces > outDominantTriCount) {
            outDominantTriCount  = mesh->mNumFaces;
            outDominantMatIndex  = mesh->mMaterialIndex;
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        ProcessNode(node->mChildren[i], scene, transform, outVerts, outIndices,
                    outDominantMatIndex, outDominantTriCount);
}

// ---------------------------------------------------------------------------
// ExtractMaterial — read Assimp material properties into FbxMaterialData.
// Handles Phong (diffuse color), PBR (metallic-roughness), and glTF workflows.
// Texture paths are resolved to absolute paths; embedded blobs (glTF .glb)
// are decoded directly into a Texture object via LoadFromMemory.
// ctx may be nullptr — in that case embedded textures are skipped.
// ---------------------------------------------------------------------------
static FbxMaterialData ExtractMaterial(const aiMaterial*          mat,
                                       const aiScene*             scene,
                                       const std::string&         fileDir,
                                       ID3D12Device*              device,
                                       ID3D12GraphicsCommandList* cmdList,
                                       DX12Context*               ctx)
{
    FbxMaterialData out;
    out.hasMaterial  = true;

    // ---- Albedo / diffuse color -----------------------------------------------
    aiColor4D diffuse(1.f, 1.f, 1.f, 1.f);
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
        out.albedo = { diffuse.r, diffuse.g, diffuse.b, diffuse.a };

    // PBR base color overrides Phong diffuse if present
    aiColor4D baseColor(1.f, 1.f, 1.f, 1.f);
    if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS)
        out.albedo = { baseColor.r, baseColor.g, baseColor.b, baseColor.a };

    // ---- Roughness / Metallic (PBR) -------------------------------------------
    float roughness = 0.5f;
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
        out.roughness = roughness;

    float metallic = 0.0f;
    if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
        out.metallic = metallic;

    // Phong shininess → approximate roughness when PBR data absent
    if (roughness == 0.5f) {   // not overwritten by PBR
        float shininess = 0.f;
        if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.f) {
            // shininess [0, 1000+] → roughness [1, 0] (rough approximation)
            out.roughness = 1.f - std::min(shininess / 100.f, 1.f) * 0.9f;
        }
    }

    // ---- Emissive -------------------------------------------------------------
    aiColor3D emissive(0.f, 0.f, 0.f);
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
        out.emissive = { emissive.r, emissive.g, emissive.b };

    // ---- Diffuse / base-color texture ----------------------------------------
    // Priority: embedded blob (glTF .glb) → external path (FBX / .gltf).
    // Tries aiTextureType_DIFFUSE first, then aiTextureType_BASE_COLOR.
    auto resolveTexture = [&](aiTextureType type) -> bool {
        if (mat->GetTextureCount(type) == 0) return false;
        aiString texPath;
        if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS) return false;

        std::string rawPath = texPath.C_Str();

        // Embedded texture: Assimp uses "*N" where N is mTextures[] index.
        // Common for glTF .glb files which pack all assets into one binary.
        if (!rawPath.empty() && rawPath[0] == '*') {
            const int idx = std::atoi(rawPath.c_str() + 1);
            if (idx >= 0 && idx < static_cast<int>(scene->mNumTextures)) {
                const aiTexture* emb = scene->mTextures[idx];

                // Compressed blob (mHeight == 0): decode via WIC from memory.
                if (emb->mHeight == 0 && emb->pcData && ctx) {
                    auto tex = std::make_shared<Texture>();
                    if (tex->LoadFromMemory(device, cmdList, *ctx,
                                           emb->pcData, emb->mWidth))
                    {
                        out.embeddedAlbedo = tex;
                        LOG_INFO("FBXLoader: embedded texture decoded (%u bytes).",
                                 emb->mWidth);
                        return true;
                    }
                    LOG_WARN("FBXLoader: embedded texture decode failed.");
                } else {
                    LOG_WARN("FBXLoader: embedded texture skipped "
                             "(raw pixels not supported or no ctx).");
                }
            }
            return false;
        }

        // External file — build absolute path.
        std::filesystem::path absPath(rawPath);
        if (absPath.is_relative())
            absPath = std::filesystem::path(fileDir) / absPath;

        out.diffuseTexturePath = absPath.wstring();
        return true;
    };

    if (!resolveTexture(aiTextureType_DIFFUSE))
        resolveTexture(aiTextureType_BASE_COLOR);

    // ---- Log summary ----------------------------------------------------------
    aiString matName;
    mat->Get(AI_MATKEY_NAME, matName);
    LOG_INFO("FBXLoader: material '%s'  albedo=(%.2f,%.2f,%.2f)  "
             "rough=%.2f  metal=%.2f  tex=%s",
             matName.C_Str()[0] ? matName.C_Str() : "(unnamed)",
             out.albedo.x, out.albedo.y, out.albedo.z,
             out.roughness, out.metallic,
             out.diffuseTexturePath.empty() ? "none"
                 : std::filesystem::path(out.diffuseTexturePath).filename().string().c_str());

    return out;
}

// ---------------------------------------------------------------------------
// Shared import helper
//
// Coordinate system notes:
//   FBX  — Assimp reads axis-info from the file's GlobalSettings and applies
//           its own Y-up conversion; aiProcess_FlipUVs corrects OpenGL→DX UVs.
//   glTF — always right-handed Y-up. aiProcess_MakeLeftHanded negates Z in
//           positions and normals (right→left handed). UV origin already
//           matches DirectX (top-left), so aiProcess_FlipUVs is NOT applied.
// ---------------------------------------------------------------------------
static const aiScene* ImportScene(Assimp::Importer& importer,
                                  const std::wstring& path,
                                  std::string&        narrowPathOut)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1,
                                  nullptr, 0, nullptr, nullptr);
    narrowPathOut.assign(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1,
                        narrowPathOut.data(), len, nullptr, nullptr);

    // Detect extension (lower-case) to choose import flags.
    const std::string ext = [&] {
        std::filesystem::path p(narrowPathOut);
        std::string e = p.extension().string();
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e;
    }();

    const bool isGltf = (ext == ".gltf" || ext == ".glb");

    unsigned int flags =
        aiProcess_Triangulate          |
        aiProcess_GenSmoothNormals     |
        aiProcess_JoinIdenticalVertices|
        aiProcess_CalcTangentSpace;    // compute per-vertex tangent + bitangent

    if (isGltf) {
        // glTF is right-handed; negate Z to convert to DirectX left-handed.
        // UV origin already matches DX12 (top-left) — no FlipUVs needed.
        flags |= aiProcess_MakeLeftHanded;
    } else {
        // FBX / OBJ and others typically use OpenGL UV origin (bottom-left).
        flags |= aiProcess_FlipUVs;
    }

    return importer.ReadFile(narrowPathOut, flags);
}

// ---------------------------------------------------------------------------
// FBXLoader::Load  (geometry only — backward-compatible)
// ---------------------------------------------------------------------------
Mesh FBXLoader::Load(ID3D12Device*              device,
                     ID3D12GraphicsCommandList* cmdList,
                     const std::wstring&        path)
{
    return LoadWithMaterial(device, cmdList, /*ctx=*/nullptr, path).mesh;
}

// ---------------------------------------------------------------------------
// FBXLoader::LoadWithMaterial
// ---------------------------------------------------------------------------
FbxAsset FBXLoader::LoadWithMaterial(ID3D12Device*              device,
                                     ID3D12GraphicsCommandList* cmdList,
                                     DX12Context*               ctx,
                                     const std::wstring&        path)
{
    Assimp::Importer importer;
    std::string narrowPath;
    const aiScene* scene = ImportScene(importer, path, narrowPath);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        LOG_ERROR("FBXLoader: '%s': %s", narrowPath.c_str(), importer.GetErrorString());
        return {};
    }

    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    verts.reserve(1024);
    indices.reserve(4096);

    unsigned int dominantMatIndex = 0;
    unsigned int dominantTriCount = 0;

    aiMatrix4x4 identity;
    ProcessNode(scene->mRootNode, scene, identity, verts, indices,
                dominantMatIndex, dominantTriCount);

    if (verts.empty() || indices.empty()) {
        LOG_ERROR("FBXLoader: no geometry found in '%s'", narrowPath.c_str());
        return {};
    }

    LOG_INFO("FBXLoader: '%s' — %zu verts, %zu indices  (%u materials)",
             narrowPath.c_str(), verts.size(), indices.size(), scene->mNumMaterials);

    // ---- Extract material data (colours + texture) ---------------------------
    FbxAsset asset;
    if (scene->mNumMaterials > 0) {
        const std::string fileDir =
            std::filesystem::path(narrowPath).parent_path().string();
        asset.material = ExtractMaterial(scene->mMaterials[dominantMatIndex],
                                         scene, fileDir,
                                         device, cmdList, ctx);
    }

    asset.mesh.Create(device, cmdList, verts, indices);
    return asset;
}

} // namespace VibeEngine
