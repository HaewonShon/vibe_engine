#include "Mesh.h"
#include "DX12Helpers.h"
#include <stdexcept>
#include <cmath>

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
    const std::vector<uint32_t>& indices)
{
    UINT64 vbSize = vertices.size()  * sizeof(Vertex);
    UINT64 ibSize = indices.size()   * sizeof(uint32_t);

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
    m_IBView.Format         = DXGI_FORMAT_R32_UINT;

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
    std::vector<uint32_t> indices = { 0, 1, 2 };

    Mesh mesh;
    mesh.Create(device, cmdList, verts, indices);
    return mesh;
}

Mesh Mesh::CreateCube(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // 24 vertices: 4 per face
    // UV: BL=(0,1), BR=(1,1), TR=(1,0), TL=(0,0)
    // Vertex color = white {1,1,1,1} so material albedo drives the final colour
    // without any per-vertex tinting or gradient seams on flat surfaces.
    std::vector<Vertex> verts = {
        // Front  (z=+0.5)  normal  0, 0,+1
        { { -0.5f, -0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f,  1.f } },
        { {  0.5f, -0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f,  1.f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f,  1.f } },
        { { -0.5f,  0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f,  1.f } },
        // Back   (z=-0.5)  normal  0, 0,-1
        { {  0.5f, -0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f, -0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f,  0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f, -1.f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f, -1.f } },
        // Left   (x=-0.5)  normal -1, 0, 0
        { { -0.5f, -0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 1.f }, { -1.f, 0.f, 0.f } },
        { { -0.5f, -0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f }, { -1.f, 0.f, 0.f } },
        { { -0.5f,  0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 0.f }, { -1.f, 0.f, 0.f } },
        { { -0.5f,  0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f }, { -1.f, 0.f, 0.f } },
        // Right  (x=+0.5)  normal +1, 0, 0
        { {  0.5f, -0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 1.f }, {  1.f, 0.f, 0.f } },
        { {  0.5f, -0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f }, {  1.f, 0.f, 0.f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 0.f }, {  1.f, 0.f, 0.f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f }, {  1.f, 0.f, 0.f } },
        // Top    (y=+0.5)  normal  0,+1, 0
        { { -0.5f,  0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 1.f }, { 0.f,  1.f, 0.f } },
        { {  0.5f,  0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f }, { 0.f,  1.f, 0.f } },
        { {  0.5f,  0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 0.f }, { 0.f,  1.f, 0.f } },
        { { -0.5f,  0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f }, { 0.f,  1.f, 0.f } },
        // Bottom (y=-0.5)  normal  0,-1, 0
        { { -0.5f, -0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 1.f }, { 0.f, -1.f, 0.f } },
        { {  0.5f, -0.5f, -0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f }, { 0.f, -1.f, 0.f } },
        { {  0.5f, -0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 1.f, 0.f }, { 0.f, -1.f, 0.f } },
        { { -0.5f, -0.5f,  0.5f }, { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f }, { 0.f, -1.f, 0.f } },
    };

    // 6 faces x 2 triangles x 3 indices = 36
    std::vector<uint32_t> indices = {
         0,  1,  2,   0,  2,  3,  // front
         4,  5,  6,   4,  6,  7,  // back
         8,  9, 10,   8, 10, 11,  // left
        12, 13, 14,  12, 14, 15,  // right
        16, 17, 18,  16, 18, 19,  // top
        20, 21, 22,  20, 22, 23,  // bottom
    };

    ComputeTangents(verts, indices);

    Mesh mesh;
    mesh.Create(device, cmdList, verts, indices);
    return mesh;
}

Mesh Mesh::CreatePlane(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // 2x2 quad in XZ at y=0, normal (0,1,0) pointing up
    std::vector<Vertex> verts = {
        { { -1.f, 0.f, -1.f }, { 0.8f, 0.8f, 0.8f, 1.f }, { 0.f, 1.f }, { 0.f, 1.f, 0.f } },
        { {  1.f, 0.f, -1.f }, { 0.8f, 0.8f, 0.8f, 1.f }, { 1.f, 1.f }, { 0.f, 1.f, 0.f } },
        { {  1.f, 0.f,  1.f }, { 0.8f, 0.8f, 0.8f, 1.f }, { 1.f, 0.f }, { 0.f, 1.f, 0.f } },
        { { -1.f, 0.f,  1.f }, { 0.8f, 0.8f, 0.8f, 1.f }, { 0.f, 0.f }, { 0.f, 1.f, 0.f } },
    };
    std::vector<uint32_t> indices = { 0, 1, 2,  0, 2, 3 };

    ComputeTangents(verts, indices);

    Mesh mesh;
    mesh.Create(device, cmdList, verts, indices);
    return mesh;
}

Mesh Mesh::CreateGrid(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, int divisions)
{
    if (divisions < 1) divisions = 1;

    // (divisions+1) x (divisions+1) vertices covering [-0.5, 0.5] in X and Z,
    // y = 0, normal (0, 1, 0).  UV goes 0-1 across the whole plane.
    const int vcount = (divisions + 1) * (divisions + 1);
    std::vector<Vertex> verts;
    verts.reserve(static_cast<size_t>(vcount));

    for (int row = 0; row <= divisions; ++row) {
        for (int col = 0; col <= divisions; ++col) {
            float u = static_cast<float>(col) / static_cast<float>(divisions);
            float v = static_cast<float>(row) / static_cast<float>(divisions);
            Vertex vert;
            vert.Position = { u - 0.5f, 0.f, v - 0.5f };
            vert.Color    = { 1.f, 1.f, 1.f, 1.f };
            vert.TexCoord = { u, v };
            vert.Normal   = { 0.f, 1.f, 0.f };
            verts.push_back(vert);
        }
    }

    // Each grid cell = 2 triangles; alternate diagonal direction per cell to
    // cancel out perspective seams across the grid.
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(divisions * divisions * 6));

    for (int row = 0; row < divisions; ++row) {
        for (int col = 0; col < divisions; ++col) {
            uint32_t tl = static_cast<uint32_t>(row       * (divisions + 1) + col);
            uint32_t tr = static_cast<uint32_t>(row       * (divisions + 1) + col + 1);
            uint32_t bl = static_cast<uint32_t>((row + 1) * (divisions + 1) + col);
            uint32_t br = static_cast<uint32_t>((row + 1) * (divisions + 1) + col + 1);

            // Alternate diagonal: even cells /, odd cells backslash
            if ((row + col) % 2 == 0) {
                indices.insert(indices.end(), { tl, tr, br,  tl, br, bl });
            } else {
                indices.insert(indices.end(), { tl, tr, bl,  tr, br, bl });
            }
        }
    }

    ComputeTangents(verts, indices);

    Mesh mesh;
    mesh.Create(device, cmdList, verts, indices);
    return mesh;
}

// ---------------------------------------------------------------------------
// ComputeTangents — Lengyel's method (per-triangle accumulation + Gram-Schmidt).
// Requires that Position, TexCoord, and Normal are already set on every vertex.
// Tangent/Bitangent fields are zeroed first and overwritten in-place.
// ---------------------------------------------------------------------------
void Mesh::ComputeTangents(std::vector<Vertex>&         vertices,
                           const std::vector<uint32_t>& indices)
{
    using namespace DirectX;

    // Zero out existing tangents/bitangents
    for (auto& v : vertices) {
        v.Tangent   = { 0.f, 0.f, 0.f };
        v.Bitangent = { 0.f, 0.f, 0.f };
    }

    // Accumulate per-triangle contributions
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex& v0 = vertices[indices[i]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        XMVECTOR p0 = XMLoadFloat3(&v0.Position);
        XMVECTOR p1 = XMLoadFloat3(&v1.Position);
        XMVECTOR p2 = XMLoadFloat3(&v2.Position);

        XMFLOAT2 uv0 = v0.TexCoord;
        XMFLOAT2 uv1 = v1.TexCoord;
        XMFLOAT2 uv2 = v2.TexCoord;

        float dp1x = p1.m128_f32[0] - p0.m128_f32[0];
        float dp1y = p1.m128_f32[1] - p0.m128_f32[1];
        float dp1z = p1.m128_f32[2] - p0.m128_f32[2];
        float dp2x = p2.m128_f32[0] - p0.m128_f32[0];
        float dp2y = p2.m128_f32[1] - p0.m128_f32[1];
        float dp2z = p2.m128_f32[2] - p0.m128_f32[2];

        float du1 = uv1.x - uv0.x;
        float dv1 = uv1.y - uv0.y;
        float du2 = uv2.x - uv0.x;
        float dv2 = uv2.y - uv0.y;

        float det = du1 * dv2 - du2 * dv1;
        float r   = (std::abs(det) > 1e-8f) ? 1.f / det : 0.f;

        XMFLOAT3 T = {
            r * (dv2 * dp1x - dv1 * dp2x),
            r * (dv2 * dp1y - dv1 * dp2y),
            r * (dv2 * dp1z - dv1 * dp2z)
        };
        XMFLOAT3 B = {
            r * (du1 * dp2x - du2 * dp1x),
            r * (du1 * dp2y - du2 * dp1y),
            r * (du1 * dp2z - du2 * dp1z)
        };

        // Accumulate into all 3 vertices
        for (int k = 0; k < 3; ++k) {
            Vertex& v = vertices[indices[i + static_cast<size_t>(k)]];
            v.Tangent.x   += T.x;  v.Tangent.y   += T.y;  v.Tangent.z   += T.z;
            v.Bitangent.x += B.x;  v.Bitangent.y += B.y;  v.Bitangent.z += B.z;
        }
    }

    // Gram-Schmidt orthogonalize and normalize per vertex
    for (auto& v : vertices) {
        XMVECTOR N = XMVector3Normalize(XMLoadFloat3(&v.Normal));
        XMVECTOR T = XMLoadFloat3(&v.Tangent);
        XMVECTOR B = XMLoadFloat3(&v.Bitangent);

        // Gram-Schmidt: T' = normalize(T - dot(T,N)*N)
        T = XMVector3Normalize(T - XMVector3Dot(T, N) * N);

        // Ensure right-handed TBN; flip B if needed
        XMVECTOR crossNT = XMVector3Cross(N, T);
        float    handedness = XMVectorGetX(XMVector3Dot(crossNT, B)) < 0.f ? -1.f : 1.f;
        B = crossNT * handedness;

        XMStoreFloat3(&v.Tangent,   T);
        XMStoreFloat3(&v.Bitangent, B);
    }
}

} // namespace VibeEngine
