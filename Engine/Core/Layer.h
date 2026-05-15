#pragma once
#include <cstdint>

namespace VibeEngine {

// ============================================================================
// Layer
//
// Integer layer indices 0-31, mirroring Unity's layer model.
// Predefined layers occupy 0-5; slots 6-31 are free for user-defined layers.
//
//   go->SetLayer(Layer::Enemy);
//   go->GetLayer();
//
// ============================================================================
namespace Layer {

    constexpr int Default       = 0;
    constexpr int UI            = 1;
    constexpr int Player        = 2;
    constexpr int Enemy         = 3;
    constexpr int Environment   = 4;
    constexpr int IgnoreRaycast = 5;
    // 6 - 31: user-defined

    constexpr int Count = 32;   // total number of available layers

    inline const char* GetName(int layer)
    {
        switch (layer) {
            case Default:       return "Default";
            case UI:            return "UI";
            case Player:        return "Player";
            case Enemy:         return "Enemy";
            case Environment:   return "Environment";
            case IgnoreRaycast: return "IgnoreRaycast";
            default:            return "UserLayer";
        }
    }

} // namespace Layer


// ============================================================================
// LayerMask
//
// 32-bit bitmask where bit N represents Layer N.
//
//   LayerMask mask = LayerMask::From(Layer::Enemy);
//   mask.Add(Layer::Player);                        // Enemy | Player
//
//   LayerMask::Everything()                         // all bits set
//   LayerMask::Nothing()                            // no bits set
//
//   mask.Contains(Layer::Enemy)                     // true
//   mask.Contains(Layer::UI)                        // false
//
//   // Combine with |
//   LayerMask combined = LayerMask::From(Layer::Enemy) | LayerMask::From(Layer::Player);
//
// ============================================================================
struct LayerMask {
    uint32_t value = 0;

    // ---- Construction -------------------------------------------------------
    LayerMask() = default;
    explicit LayerMask(uint32_t v) : value(v) {}

    static LayerMask Everything()     { return LayerMask{ ~0u }; }
    static LayerMask Nothing()        { return LayerMask{  0u }; }
    static LayerMask From(int layer)  { return LayerMask{ 1u << layer }; }

    // ---- Mutation -----------------------------------------------------------
    LayerMask& Add   (int layer) { value |=  (1u << layer); return *this; }
    LayerMask& Remove(int layer) { value &= ~(1u << layer); return *this; }

    // ---- Query --------------------------------------------------------------
    bool Contains(int layer) const { return (value & (1u << layer)) != 0; }
    bool IsEmpty()           const { return value == 0; }

    // ---- Operators ----------------------------------------------------------
    LayerMask operator| (LayerMask o) const { return LayerMask{ value | o.value }; }
    LayerMask operator& (LayerMask o) const { return LayerMask{ value & o.value }; }
    LayerMask operator~ ()            const { return LayerMask{ ~value }; }
    bool      operator==(LayerMask o) const { return value == o.value; }
    bool      operator!=(LayerMask o) const { return value != o.value; }
};

} // namespace VibeEngine
