#pragma once
#include "Delegate.h"      // DelegateHandle
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace VibeEngine {

// ============================================================================
// EventBus  (singleton)
//
// Global type-erased publish / subscribe system.
// Decouples components: neither publisher nor subscriber needs a direct
// reference to the other — they only share the event struct definition.
//
// ---- Define an event --------------------------------------------------------
//   struct PlayerDiedEvent {
//       std::string name;
//       int         score;
//   };
//
// ---- Subscribe --------------------------------------------------------------
//   // Lambda
//   DelegateHandle h = EventBus::Get().Subscribe<PlayerDiedEvent>(
//       [](const PlayerDiedEvent& e) {
//           LOG_INFO("Player %s died with score %d", e.name.c_str(), e.score);
//       });
//
//   // Member function
//   DelegateHandle h2 = EventBus::Get().Subscribe<EnemySpawnedEvent>(
//       this, &MyComponent::OnEnemySpawned);
//
// ---- Emit -------------------------------------------------------------------
//   EventBus::Get().Emit(PlayerDiedEvent{ "Alice", 9999 });
//
// ---- Unsubscribe ------------------------------------------------------------
//   EventBus::Get().Unsubscribe(h);     // by handle
//   EventBus::Get().Clear<PlayerDiedEvent>();  // remove all for this type
//   EventBus::Get().ClearAll();         // nuke everything (scene transitions)
//
// ---- Safety -----------------------------------------------------------------
//   Emit() takes a snapshot of the handler list before iterating, so it is
//   safe to Subscribe or Unsubscribe from inside a handler.
// ============================================================================
class EventBus {
public:
    static EventBus& Get() { static EventBus inst; return inst; }

    // ---- Subscribe ----------------------------------------------------------

    // Lambda / free function / std::function
    template<typename T>
    DelegateHandle Subscribe(std::function<void(const T&)> handler)
    {
        uint32_t id = ++m_Counter;

        m_Buckets[typeid(T)].push_back({
            id,
            [h = std::move(handler)](const void* ev) {
                h(*static_cast<const T*>(ev));
            }
        });

        m_HandleToType[id] = typeid(T);
        return DelegateHandle{ id };
    }

    // Non-const member function
    template<typename T, typename Obj>
    DelegateHandle Subscribe(Obj* obj, void (Obj::*fn)(const T&))
    {
        return Subscribe<T>([obj, fn](const T& e) { (obj->*fn)(e); });
    }

    // Const member function
    template<typename T, typename Obj>
    DelegateHandle Subscribe(const Obj* obj, void (Obj::*fn)(const T&) const)
    {
        return Subscribe<T>([obj, fn](const T& e) { (obj->*fn)(e); });
    }

    // ---- Unsubscribe --------------------------------------------------------

    void Unsubscribe(DelegateHandle h)
    {
        if (!h.IsValid()) return;

        auto typeIt = m_HandleToType.find(h.id);
        if (typeIt == m_HandleToType.end()) return;

        auto bucketIt = m_Buckets.find(typeIt->second);
        if (bucketIt != m_Buckets.end()) {
            auto& vec = bucketIt->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [id = h.id](const Entry& e) { return e.id == id; }),
                vec.end());
        }

        m_HandleToType.erase(typeIt);
    }

    // ---- Emit ---------------------------------------------------------------

    template<typename T>
    void Emit(const T& event)
    {
        auto it = m_Buckets.find(typeid(T));
        if (it == m_Buckets.end()) return;

        // Snapshot — safe if a handler calls Subscribe / Unsubscribe / Emit
        auto snapshot = it->second;
        for (auto& entry : snapshot)
            entry.fn(&event);
    }

    // ---- Clear --------------------------------------------------------------

    // Remove all handlers subscribed to event type T.
    template<typename T>
    void Clear()
    {
        auto typeIdx = std::type_index(typeid(T));
        auto bucketIt = m_Buckets.find(typeIdx);
        if (bucketIt == m_Buckets.end()) return;

        for (auto& entry : bucketIt->second)
            m_HandleToType.erase(entry.id);

        bucketIt->second.clear();
    }

    // Remove ALL handlers for ALL event types.
    // Useful on scene transitions to prevent stale component references.
    void ClearAll()
    {
        m_Buckets.clear();
        m_HandleToType.clear();
    }

    // ---- Info ---------------------------------------------------------------

    // Number of active handlers for event type T.
    template<typename T>
    int SubscriberCount() const
    {
        auto it = m_Buckets.find(typeid(T));
        return it != m_Buckets.end() ? static_cast<int>(it->second.size()) : 0;
    }

    // Total number of active handlers across all event types.
    int TotalSubscriberCount() const
    {
        int total = 0;
        for (auto& [type, vec] : m_Buckets)
            total += static_cast<int>(vec.size());
        return total;
    }

private:
    EventBus() = default;

    struct Entry {
        uint32_t                     id;
        std::function<void(const void*)> fn;  // type-erased handler
    };

    std::unordered_map<std::type_index, std::vector<Entry>> m_Buckets;
    std::unordered_map<uint32_t, std::type_index>           m_HandleToType;
    uint32_t m_Counter = 0;
};

} // namespace VibeEngine
