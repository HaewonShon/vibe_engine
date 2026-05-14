#include "Mesh.h"
#include "DX12Helpers.h"
#include <stdexcept>

namespace VibeEngine {

static ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size)
{
    auto hp   = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = BufferDesc(size);
    ComPtr<ID3D12Resource> buf;
    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&buf));
    if (FAILED(hr)) throw std::runtime_error("CreateUploadBuffer failed");
    return buf;
}

static ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, UINT64 size)
{
    auto hp   = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = BufferDesc(size);
    ComPtr<ID3D12Resource> buf;
    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&buf));
    if (FAILED(hr)) throw std::runtime_error("CreateDefaultBuffer failed");
    return buf;
}

static void UploadData(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* defaultBuf,
    ID3D12Resource* uploadBuf,
    const void* data, UINT64 size,
    D3D12_RESOURCE_STATES finalState)
{
    // Copy data into upload heap
    void* mapped = nullptr;
    D3D12_RANGE range = { 0, 0 };
    uploadBuf->Map(0, &range, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    uploadBuf->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(defaultBuf, 0, uploadBuf, 0, size);

    auto barrier = TransitionBarrier(defaultBuf, D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    cmdList->ResourceBarrier(1, &barrier);
}

bool Mesh::Create(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<Vertex>&   vertices,
    const std::vector<uint16_t>& indices)
{
    UINT64 vbSize = vertices.size()  * sizeof(Vertex);
    UINT64 ibSize = indices.size()   * sizeof(uint16_t);

    m_VertexBuffer = CreateDefaultBuffer(device, vbSize);
    m_IndexBuffer  = CreateDefaultBuffer(device, ibSize);
    m_VBUpload     = CreateUploadBuffer(device, vbSize);
    m_IBUpload     = CreateUploadBuffer(device, ibSize);

    UploadData(cmdList, m_VertexBuffer.Get(), m_VBUpload.Get(),
               vertices.data(), vbSize,
               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    UploadData(cmdList, m_IndexBuffer.Get(), m_IBUpload.Get(),
               indices.data(), ibSize,
               D3D12_RESOURCE_STATE_INDEX_BUFFER);

    m_VBView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
    m_VBView.SizeInBytes    = static_cast<UINT>(vbSize);
    m_VBView.StrideInBytes  = sizeof(Vertex);

    m_IBView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
    m_IBView.SizeInBytes    = static_cast<UINT>(ibSize);
    m_IBView.Format         = DXGI_FORMAT_R16_UINT;

    m_IndexCount = static_cast<UINT>(indices.size());
    return true;
}

void Mesh::ReleaseUploadBuffers()
{
    m_VBUpload.Reset();
    m_IBUpload.Reset();
}

Mesh Mesh::CreateTriangle(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    std::vector<Vertex> verts = {
        { {  0.0f,  0.5f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f } },
        { {  0.5f, -0.5f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f } },
        { { -0.5f, -0.5f, 0.0f }, { 0.2f, 0.2f, 1.0f, 1.0f } },
    };
    std::vector<uint16_t> indices = { 0, 1, 2 };

    Mesh mesh;
    mesh.Create(device, cmdList, verts, indices);
    return mesh;
}

Mesh Mesh::CreateCube(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // 24 vertices: 4 per face
    // UV: BL=(0,1), BR=(1,1), TR=(1,0), TL=(0,0)
    std::vector<Vertex> verts = {
        // Front  (z=+0.5) -- red,     normal  0, 0,+1
        { { -0.5f, -0.5f,  0.5f }, { 0.9f, 0.2f, 0.2f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f,  1.f } },
        { {  0.5f, -0.5f,  0.5f }, { 0.9f, 0.2f, 0.2f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f,  1.f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.4f, 0.4f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f,  1.f } },
        { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.4f, 0.4f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f,  1.f } },
        // Back   (z=-0.5) -- blue,    normal  0, 0,-1
        { {  0.5f, -0.5f, -0.5f }, { 0.2f, 0.2f, 0.9f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f, -0.5f, -0.5f }, { 0.2f, 0.2f, 0.9f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.4f, 0.4f, 1.0f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f, -1.f } },
        { {  0.5f,  0.5f, -0.5f }, { 0.4f, 0.4f, 1.0f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f, -1.f } },
        // Left   (x=-0.5) -- green,   normal -1, 0, 0
        { { -0.5f, -0.5f, -0.5f }, { 0.2f, 0.9f, 0.2f, 1.f }, { 0.f, 1.f }, { -1.f, 0.f, 0.f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.2f, 0.9f, 0.2f, 1.f }, { 1.f, 1.f }, { -1.f, 0.f, 0.f } },
        { { -0.5f,  0.5f,  0.5f }, { 0.4f, 1.0f, 0.4f, 1.f }, { 1.f, 0.f }, { -1.f, 0.f, 0.f } },
        { { -0.5f,  0.5f, -0.5f }, { 0.4f, 1.0f, 0.4f, 1.f }, { 0.f, 0.f }, { -1.f, 0.f, 0.f } },
        // Right  (x=+0.5) -- yellow,  normal +1, 0, 0
        { {  0.5f, -0.5f,  0.5f }, { 0.9f, 0.9f, 0.1f, 1.f }, { 0.f, 1.f }, {  1.f, 0.f, 0.f } },
        { {  0.5f, -0.5f, -0.5f }, { 0.9f, 0.9f, 0.1f, 1.f }, { 1.f, 1.f }, {  1.f, 0.f, 0.f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 0.3f, 1.f }, { 1.f, 0.f }, {  1.f, 0.f, 0.f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 0.3f, 1.f }, { 0.f, 0.f }, {  1.f, 0.f, 0.f } },
        // Top    (y=+0.5) -- magenta,  normal  0,+1, 0
        { { -0.5f,  0.5f,  0.5f }, { 0.9f, 0.2f, 0.9f, 1.f }, { 0.f, 1.f }, { 0.f,  1.f, 0.f } },
        { {  0.5f,  0.5f,  0.5f }, { 0.9f, 0.2f, 0.9f, 1.f }, { 1.f, 1.f }, { 0.f,  1.f, 0.f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.4f, 1.0f, 1.f }, { 1.f, 0.f }, { 0.f,  1.f, 0.f } },
        { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.4f, 1.0f, 1.f }, { 0.f, 0.f }, { 0.f,  1.f, 0.f } },
        // Bottom (y=-0.5) -- cyan,     normal  0,-1, 0
        { { -0.5f, -0.5f, -0.5f }, { 0.2f, 0.9f, 0.9f, 1.f }, { 0.f, 1.f }, { 0.f, -1.f, 0.f } },
        { {  0.5f, -0.5f, -0.5f }, { 0.2f, 0.9f, 0.9f, 1.f }, { 1.f, 1.f }, { 0.f, -1.f, 0.f } },
        { {  0.5f, -0.5f,  0.5f }, { 0.4f, 1.0f, 1.0f, 1.f }, { 1.f, 0.f }, { 0.f, -1.f, 0.f } },
        { { -0.5f, -0.5f,  0.5f }, { 0.4f, 1.0f, 1.0f, 1.f }, { 0.f, 0.f }, { 0.f, -1.f, 0.f } },
    };

    // 6 faces x 2 triangles x 3 indices = 36
    std::vector<uint16_t> indices = {
         0,  1,  2,   0,  2,  3,  // front
         4,  5,  6,   4,  6,  7,  // back
         8,  9, 10,   8, 10, 11,  // left
        12, 13, 14,  12, 14, 15,  // right
        16, 17, 18,  16, 18, 19,  // top
        20, 21, 22,  20, 22, 23,  // bottom
    };

    Mesh mesh;
    mesh.Create(device, cmdList, verts, indices);
    return mesh;
}

} // namespace VibeEngine
