#include "OBJLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <DirectXMath.h>

using namespace DirectX;

namespace VibeEngine {

// ---- Face-vertex key --------------------------------------------------------
struct FaceVtx {
    int vi = 0, ti = 0, ni = 0; // 1-based OBJ indices (0 = absent)
    bool operator==(const FaceVtx& o) const {
        return vi == o.vi && ti == o.ti && ni == o.ni;
    }
};
struct FaceVtxHash {
    size_t operator()(const FaceVtx& f) const {
        return std::hash<int>()(f.vi)
             ^ (std::hash<int>()(f.ti) << 12)
             ^ (std::hash<int>()(f.ni) << 24);
    }
};

// Parse one face token: "v", "v/vt", "v//vn", "v/vt/vn"
static FaceVtx ParseToken(const std::string& tok)
{
    FaceVtx fv;
    size_t s1 = tok.find('/');
    if (s1 == std::string::npos) {
        fv.vi = std::stoi(tok);
        return fv;
    }
    fv.vi = std::stoi(tok.substr(0, s1));
    size_t s2 = tok.find('/', s1 + 1);
    if (s2 == std::string::npos) {
        // v/vt
        if (s1 + 1 < tok.size()) fv.ti = std::stoi(tok.substr(s1 + 1));
    } else {
        // v/vt/vn  or  v//vn
        if (s2 > s1 + 1) fv.ti = std::stoi(tok.substr(s1 + 1, s2 - s1 - 1));
        if (s2 + 1 < tok.size()) fv.ni = std::stoi(tok.substr(s2 + 1));
    }
    return fv;
}

// Resolve a negative OBJ index (relative) to positive 1-based
static int Resolve(int idx, int total) {
    return idx < 0 ? total + idx + 1 : idx;
}

// ---- Main loader ------------------------------------------------------------
Mesh OBJLoader::Load(ID3D12Device* device,
                     ID3D12GraphicsCommandList* cmdList,
                     const std::wstring& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return {};

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT2> texcoords;
    std::vector<XMFLOAT3> normals;

    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<FaceVtx, uint32_t, FaceVtxHash> cache;

    bool hasNormals = false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v") {
            XMFLOAT3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (token == "vt") {
            XMFLOAT2 t;
            ss >> t.x >> t.y;
            t.y = 1.0f - t.y; // flip V axis for DX12 (top-left UV origin)
            texcoords.push_back(t);
        } else if (token == "vn") {
            XMFLOAT3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
            hasNormals = true;
        } else if (token == "f") {
            // Collect all face vertex tokens, then fan-triangulate
            std::vector<uint32_t> faceIdx;
            std::string tok;
            while (ss >> tok) {
                FaceVtx fv = ParseToken(tok);
                // Resolve relative indices
                fv.vi = Resolve(fv.vi, static_cast<int>(positions.size()));
                fv.ti = fv.ti ? Resolve(fv.ti, static_cast<int>(texcoords.size())) : 0;
                fv.ni = fv.ni ? Resolve(fv.ni, static_cast<int>(normals.size()))   : 0;

                auto it = cache.find(fv);
                if (it != cache.end()) {
                    faceIdx.push_back(it->second);
                } else {
                    Vertex vert = {};
                    vert.Color = { 1.f, 1.f, 1.f, 1.f };
                    if (fv.vi > 0 && fv.vi <= (int)positions.size())
                        vert.Position = positions[fv.vi - 1];
                    if (fv.ti > 0 && fv.ti <= (int)texcoords.size())
                        vert.TexCoord = texcoords[fv.ti - 1];
                    if (fv.ni > 0 && fv.ni <= (int)normals.size())
                        vert.Normal = normals[fv.ni - 1];

                    uint32_t idx = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vert);
                    cache[fv] = idx;
                    faceIdx.push_back(idx);
                }
            }
            // Fan triangulation: (0,1,2), (0,2,3), (0,3,4) ...
            for (size_t i = 1; i + 1 < faceIdx.size(); ++i) {
                indices.push_back(faceIdx[0]);
                indices.push_back(faceIdx[i]);
                indices.push_back(faceIdx[i + 1]);
            }
        }
    }

    // If the OBJ had no normals, compute flat per-face normals
    if (!hasNormals && !indices.empty()) {
        // Reset all normals
        for (auto& v : vertices)
            v.Normal = { 0.f, 0.f, 0.f };

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            Vertex& v0 = vertices[indices[i]];
            Vertex& v1 = vertices[indices[i + 1]];
            Vertex& v2 = vertices[indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.Position);
            XMVECTOR p1 = XMLoadFloat3(&v1.Position);
            XMVECTOR p2 = XMLoadFloat3(&v2.Position);

            XMVECTOR n = XMVector3Normalize(
                XMVector3Cross(XMVectorSubtract(p1, p0),
                               XMVectorSubtract(p2, p0)));
            XMFLOAT3 fn;
            XMStoreFloat3(&fn, n);

            v0.Normal = v1.Normal = v2.Normal = fn;
        }
    }

    Mesh mesh;
    mesh.Create(device, cmdList, vertices, indices);
    return mesh;
}

} // namespace VibeEngine
