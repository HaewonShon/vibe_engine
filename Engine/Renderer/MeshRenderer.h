#pragma once
#include "../Core/Component.h"
#include "Mesh.h"
#include "BasicPipeline.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace VibeEngine {

class MeshRenderer : public Component {
public:
    MeshRenderer();
    ~MeshRenderer() override;

    void SetMesh    (std::shared_ptr<Mesh> mesh)    { m_Mesh = mesh; }
    void SetPipeline(BasicPipeline* pipeline)       { m_Pipeline = pipeline; }
    void SetCommandList(ID3D12GraphicsCommandList* cl) { m_CmdList = cl; }

    bool CreateConstantBuffer(ID3D12Device* device);

    void Draw(const DirectX::XMMATRIX& viewProj) const;

private:
    std::shared_ptr<Mesh> m_Mesh;
    BasicPipeline*         m_Pipeline = nullptr;
    ID3D12GraphicsCommandList* m_CmdList = nullptr;

    ComPtr<ID3D12Resource> m_ConstantBuffer;
    void*                  m_MappedCB = nullptr;
};

} // namespace VibeEngine
