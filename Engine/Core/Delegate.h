#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace VibeEngine {

// ============================================================================
// DelegateHandle
// Opaque subscription token returned by Event::AddListener() / operator+=.
// Pass to Event::RemoveListener() / operator-= to unsubscribe.
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
//   float r = d.Invoke(4);               // or: d(4)
//
//   d.BindMember(myObj, &MyClass::Foo);  // member function
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

    void Unbind()        { m_Fn = nullptr; }
    bool IsBound() const { return m_Fn != nullptr; }

    Ret Invoke(Args... args) const { return m_Fn(std::forward<Args>(args)...); }
    Ret operator()(Args... args) const { return Invoke(std::forward<Args>(args)...); }

private:
    Fn m_Fn;
};


// ============================================================================
// Event<Args...>
// Multicast delegate — void return, unlimited listeners. (Unity: UnityEvent<T>)
// Each listener gets a DelegateHandle for later removal.
//
// Usage:
//   Event<int, float> OnDamage;
//
//   DelegateHandle h = OnDamage.AddListener(           // lambda
//       [](int amount, float dir) { ... });
//
//   DelegateHandle h2 = OnDamage.AddListener(          // member function
//       myObj, &MyClass::TakeDamage);
//
//   OnDamage.Invoke(10, 1.f);                          // fire all listeners
//
//   OnDamage.RemoveListener(h);                        // unsubscribe by handle
//   OnDamage -= h2;                                    // same, operator form
//
//   OnDamage.RemoveAllListeners();                     // remove all
// ============================================================================
template<typename... Args>
class Event {
public:
    using Fn = std::function<void(Args...)>;

    // Subscribe (lambda / free function / std::function)
    DelegateHandle AddListener(Fn fn) {
        uint32_t id = ++m_Counter;
        m_Listeners[id] = std::move(fn);
        return DelegateHandle{ id };
    }
    DelegateHandle operator+=(Fn fn) { return AddListener(std::move(fn)); }

    // Subscribe a member function directly
    template<typename T>
    DelegateHandle AddListener(T* obj, void (T::*fn)(Args...)) {
        return AddListener([obj, fn](Args... args) {
            (obj->*fn)(std::forward<Args>(args)...);
        });
    }

    // Unsubscribe by handle
    void RemoveListener(DelegateHandle h) {
        if (h.IsValid()) m_Listeners.erase(h.id);
    }
    void operator-=(DelegateHandle h) { RemoveListener(h); }

    // Fire all listeners (snapshot-safe: Remove inside a listener is allowed)
    void Invoke(Args... args) const {
        auto snapshot = m_Listeners;
        for (auto& [id, fn] : snapshot)
            fn(args...);
    }
    void operator()(Args... args) const { Invoke(std::forward<Args>(args)...); }

    void RemoveAllListeners()   { m_Listeners.clear(); }
    bool HasListeners()   const { return !m_Listeners.empty(); }
    int  ListenerCount()  const { return static_cast<int>(m_Listeners.size()); }

private:
    std::unordered_map<uint32_t, Fn> m_Listeners;
    uint32_t m_Counter = 0;
};

} // namespace VibeEngine
