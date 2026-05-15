#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

struct Vertex {
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT4 Color;
    DirectX::XMFLOAT2 TexCoord;
    DirectX::XMFLOAT3 Normal;
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
