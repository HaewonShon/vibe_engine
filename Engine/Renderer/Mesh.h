#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

struct Vertex {
    DirectX::XMFLOAT3 Position;   // offset  0, 12 bytes
    DirectX::XMFLOAT4 Color;      // offset 12, 16 bytes
    DirectX::XMFLOAT2 TexCoord;   // offset 28,  8 bytes
    DirectX::XMFLOAT3 Normal;     // offset 36, 12 bytes
    DirectX::XMFLOAT3 Tangent;    // offset 48, 12 bytes
    DirectX::XMFLOAT3 Bitangent;  // offset 60, 12 bytes
    // Total: 72 bytes
};

class Mesh {
public:
    bool Create(ID3D12Device* device,
                ID3D12GraphicsCommandList* cmdList,
                const std::vector<Vertex>&   vertices,
                const std::vector<uint32_t>& indices);

    void ReleaseUploadBuffers();

    static Mesh CreateTriangle(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    static Mesh CreateCube    (ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    static Mesh CreatePlane   (ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    // NxN subdivided horizontal plane in XZ, y=0, normal (0,1,0).
    // More subdivisions = smaller triangles = less perspective-seam artifact.
    static Mesh CreateGrid    (ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                               int divisions = 8);

    // Compute tangent and bitangent vectors for every triangle and accumulate
    // them per vertex (averaged, Gram-Schmidt orthogonalized). Call this before
    // uploading vertices when the mesh has UV coordinates and normals but no
    // pre-computed tangents (e.g. OBJ files, procedural geometry).
    static void ComputeTangents(std::vector<Vertex>&        vertices,
                                const std::vector<uint32_t>& indices);

    const D3D12_VERTEX_BUFFER_VIEW& GetVBView()    const { return m_VBView; }
    const D3D12_INDEX_BUFFER_VIEW&  GetIBView()    const { return m_IBView; }
    UINT                            GetIndexCount() const { return m_IndexCount; }

private:
    ComPtr<ID3D12Resource> m_VertexBuffer;
    ComPtr<ID3D12Resource> m_IndexBuffer;
    ComPtr<ID3D12Resource> m_VBUpload;
    ComPtr<ID3D12Resource> m_IBUpload;

    D3D12_VERTEX_BUFFER_VIEW m_VBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_IBView = {};
    UINT                     m_IndexCount = 0;
};

} // namespace VibeEngine
