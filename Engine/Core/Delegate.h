#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace VibeEngine {

// ============================================================================
// DelegateHandle
// Opaque subscription token returned by Event::Add() / operator+=.
// Pass to Event::Remove() / operator-= to unsubscribe.
// ============================================================================
struct DelegateHandle {
    uint32_t id = 0;
    bool IsValid() const { return id != 0; }
    static DelegateHandle Invalid() { return { 0 }; }
    bool operator==(const DelegateHandle& o) const { return id == o.id; }
    bool operator!=(const DelegateHandle& o) const { return id != o.id; }
};


// ============================================================================
// Delegate<Ret(Args...)>
// Single-cast delegate — stores exactly one callable at a time.
//
// Usage:
//   Delegate<float(int)> d;
//   d.Bind([](int x) { return x * 1.5f; });
//   float r = d.Invoke(4);              // or: d(4)
//
//   d.BindMember(myObj, &MyClass::Foo); // member function
//   d.Unbind();
// ============================================================================
template<typename Signature>
class Delegate;

template<typename Ret, typename... Args>
class Delegate<Ret(Args...)> {
public:
    using Fn = std::function<Ret(Args...)>;

    // Bind any callable (lambda, free function, std::function)
    void Bind(Fn fn) { m_Fn = std::move(fn); }

    // Bind a non-const member function
    template<typename T>
    void BindMember(T* obj, Ret (T::*fn)(Args...)) {
        m_Fn = [obj, fn](Args... args) -> Ret {
            return (obj->*fn)(std::forward<Args>(args)...);
        };
    }

    // Bind a const member function
    template<typename T>
    void BindMember(const T* obj, Ret (T::*fn)(Args...) const) {
        m_Fn = [obj, fn](Args... args) -> Ret {
            return (obj->*fn)(std::forward<Args>(args)...);
        };
    }

    void Unbind()         { m_Fn = nullptr; }
    bool IsBound() const  { return m_Fn != nullptr; }

    Ret Invoke(Args... args) const {
        return m_Fn(std::forward<Args>(args)...);
    }
    Ret operator()(Args... args) const {
        return Invoke(std::forward<Args>(args)...);
    }

private:
    Fn m_Fn;
};


// ============================================================================
// Event<Args...>
// Multicast delegate — void return, unlimited subscribers.
// Each subscriber gets a DelegateHandle for later removal.
//
// Usage:
//   Event<int, float> OnDamage;                      // declare
//
//   DelegateHandle h = OnDamage.Add(                 // subscribe (lambda)
//       [](int amount, float dir) { ... });
//
//   DelegateHandle h2 = OnDamage.Add(                // subscribe (member)
//       [this](int a, float d) { this->TakeDamage(a, d); });
//
//   OnDamage.Broadcast(10, 1.f);                     // fire all listeners
//
//   OnDamage.Remove(h);                              // unsubscribe by handle
//   OnDamage -= h2;                                  // same, operator form
//
//   OnDamage.Clear();                                // remove all
// ============================================================================
template<typename... Args>
class Event {
public:
    using Fn = std::function<void(Args...)>;

    // Subscribe — returns a handle for later removal
    DelegateHandle Add(Fn fn) {
        uint32_t id = ++m_Counter;
        m_Listeners[id] = std::move(fn);
        return DelegateHandle{ id };
    }
    DelegateHandle operator+=(Fn fn) { return Add(std::move(fn)); }

    // Subscribe a member function directly
    template<typename T>
    DelegateHandle AddMember(T* obj, void (T::*fn)(Args...)) {
        return Add([obj, fn](Args... args) {
            (obj->*fn)(std::forward<Args>(args)...);
        });
    }

    // Unsubscribe by handle
    void Remove(DelegateHandle h) {
        if (h.IsValid()) m_Listeners.erase(h.id);
    }
    void operator-=(DelegateHandle h) { Remove(h); }

    // Fire all listeners (copy snapshot to allow Remove inside a listener)
    void Broadcast(Args... args) const {
        auto snapshot = m_Listeners; // safe iteration under mutation
        for (auto& [id, fn] : snapshot)
            fn(args...);
    }
    void operator()(Args... args) const { Broadcast(std::forward<Args>(args)...); }

    bool HasListeners() const { return !m_Listeners.empty(); }
    int  Count()        const { return static_cast<int>(m_Listeners.size()); }
    void Clear()              { m_Listeners.clear(); }

private:
    std::unordered_map<uint32_t, Fn> m_Listeners;
    uint32_t m_Counter = 0;
};

} // namespace VibeEngine
