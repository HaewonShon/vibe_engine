#pragma once
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace VibeEngine {

// ============================================================================
// GameplayTag
//
// Hierarchical dot-delimited tag, e.g. "Enemy.Skill.Attack1".
// Hierarchy rules:
//   "Enemy.Skill.Attack1".MatchesTag("Enemy")         → true  (child matches parent)
//   "Enemy.Skill.Attack1".MatchesTag("Enemy.Skill")   → true
//   "Enemy.Skill.Attack1".MatchesTag("Enemy.Skill.Attack1") → true
//   "Enemy".MatchesTag("Enemy.Skill")                 → false (parent does NOT match child)
//
// Typical usage:
//   auto tag = GameplayTag::FromString("Enemy.Skill.Attack1");
//   go->AddTag(tag);
//   if (go->HasTag(GameplayTag::FromString("Enemy"))) { ... }
// ============================================================================
struct GameplayTag {
    // ---- Construction -------------------------------------------------------
    GameplayTag() = default;

    // Preferred factory — validates that the string contains only
    // alphanumeric characters and dots, with no leading/trailing/consecutive dots.
    static GameplayTag FromString(std::string_view str);

    // Null tag
    static GameplayTag Invalid() { return {}; }

    // ---- Queries ------------------------------------------------------------
    bool IsValid() const { return !m_TagName.empty(); }

    // Returns true if this tag is equal to OR a descendant of 'parent'.
    //   "Enemy.Skill.Attack1".MatchesTag("Enemy")  → true
    //   "Enemy".MatchesTag("Enemy")                → true
    //   "Enemy".MatchesTag("Enemy.Skill")          → false
    bool MatchesTag(const GameplayTag& parent) const;

    // Exact string equality only.
    bool MatchesTagExact(const GameplayTag& other) const { return m_TagName == other.m_TagName; }

    // "Enemy.Skill.Attack1" → "Enemy.Skill"
    // "Enemy"              → invalid tag
    GameplayTag RequestParent() const;

    // "Enemy.Skill.Attack1" → 3,  "Enemy" → 1,  invalid → 0
    int Depth() const;

    const std::string& ToString() const { return m_TagName; }

    // ---- Operators ----------------------------------------------------------
    bool operator==(const GameplayTag& o) const { return m_TagName == o.m_TagName; }
    bool operator!=(const GameplayTag& o) const { return !(*this == o); }
    bool operator< (const GameplayTag& o) const { return m_TagName <  o.m_TagName; }

private:
    std::string m_TagName; // canonical dot-delimited string
};

// Hash support for unordered containers
struct GameplayTagHash {
    size_t operator()(const GameplayTag& t) const {
        return std::hash<std::string>{}(t.ToString());
    }
};


// ============================================================================
// GameplayTagContainer
//
// Holds a set of GameplayTags and provides hierarchical query methods.
//
//   GameplayTagContainer tags;
//   tags.AddTag(GameplayTag::FromString("Enemy.Skill.Attack1"));
//   tags.AddTag(GameplayTag::FromString("Status.Stunned"));
//
//   tags.HasTag(GameplayTag::FromString("Enemy"))          → true
//   tags.HasTagExact(GameplayTag::FromString("Enemy"))     → false
//   tags.HasTagExact(GameplayTag::FromString("Enemy.Skill.Attack1")) → true
// ============================================================================
class GameplayTagContainer {
public:
    // ---- Mutation -----------------------------------------------------------
    void AddTag(const GameplayTag& tag);
    void RemoveTag(const GameplayTag& tag);
    void Reset() { m_Tags.clear(); }

    // ---- Queries ------------------------------------------------------------

    // True if any tag in this container matches (equals or is a descendant of) 'tag'.
    //   container = { "Enemy.Skill.Attack1", "Status.Stunned" }
    //   HasTag("Enemy")        → true   ("Enemy.Skill.Attack1" is a descendant)
    //   HasTag("Enemy.Skill")  → true
    bool HasTag(const GameplayTag& tag) const;

    // True if this container holds the exact tag (no hierarchy).
    bool HasTagExact(const GameplayTag& tag) const;

    // True if this container has at least one tag that matches any tag in 'other'.
    bool HasAnyTags(const GameplayTagContainer& other) const;

    // True if this container has a matching tag for EVERY tag in 'other'.
    bool HasAllTags(const GameplayTagContainer& other) const;

    // Fills 'out' with tags in this container that match 'tag'.
    std::vector<GameplayTag> GetMatchingTags(const GameplayTag& tag) const;

    // ---- State --------------------------------------------------------------
    bool IsEmpty() const { return m_Tags.empty(); }
    int  Count()   const { return static_cast<int>(m_Tags.size()); }

    // Raw access — use for iteration / debug
    const std::unordered_set<GameplayTag, GameplayTagHash>& GetTags() const { return m_Tags; }

    // Range-for support
    auto begin() const { return m_Tags.begin(); }
    auto end()   const { return m_Tags.end(); }

private:
    std::unordered_set<GameplayTag, GameplayTagHash> m_Tags;
};

} // namespace VibeEngine
