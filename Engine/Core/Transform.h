#pragma once
#include "Component.h"
#include <DirectXMath.h>
#include <vector>

namespace VibeEngine {

class Transform : public Component {
public:
    Transform();

    void SetPosition(const DirectX::XMFLOAT3& pos)    { m_Position = pos; }
    void SetRotation(const DirectX::XMFLOAT3& euler)  { m_Rotation = euler; }
    void SetScale   (const DirectX::XMFLOAT3& scale)  { m_Scale = scale; }

    const DirectX::XMFLOAT3& GetLocalPosition() const { return m_Position; }
    const DirectX::XMFLOAT3& GetLocalRotation() const { return m_Rotation; }
    const DirectX::XMFLOAT3& GetLocalScale()    const { return m_Scale; }

    void Translate(const DirectX::XMFLOAT3& delta);
    void Rotate(float yawDeg, float pitchDeg, float rollDeg);

    DirectX::XMMATRIX GetLocalMatrix() const;
    DirectX::XMMATRIX GetWorldMatrix() const;

    void       SetParent(Transform* parent);
    Transform* GetParent() const { return m_Parent; }
    const std::vector<Transform*>& GetChildren() const { return m_Children; }

private:
    DirectX::XMFLOAT3 m_Position = { 0.f, 0.f, 0.f };
    DirectX::XMFLOAT3 m_Rotation = { 0.f, 0.f, 0.f }; // Euler degrees: pitch, yaw, roll
    DirectX::XMFLOAT3 m_Scale    = { 1.f, 1.f, 1.f };

    Transform*              m_Parent   = nullptr;
    std::vector<Transform*> m_Children;
};

} // namespace VibeEngine
