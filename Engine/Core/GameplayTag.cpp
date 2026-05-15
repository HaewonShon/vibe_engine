#include "GameplayTag.h"
#include "Log.h"
#include <algorithm>
#include <sstream>

namespace VibeEngine {

// ============================================================================
// GameplayTag
// ============================================================================

// Basic validation: only [A-Za-z0-9_] and dots, no leading/trailing/consecutive dots.
static bool ValidateTagString(std::string_view str)
{
    if (str.empty()) return false;
    if (str.front() == '.' || str.back() == '.') return false;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok) return false;
        if (c == '.' && i + 1 < str.size() && str[i + 1] == '.') return false; // consecutive dots
    }
    return true;
}

GameplayTag GameplayTag::FromString(std::string_view str)
{
    if (!ValidateTagString(str)) {
        LOG_WARN("GameplayTag::FromString — invalid tag string: \"%.*s\"",
                 static_cast<int>(str.size()), str.data());
        return Invalid();
    }
    GameplayTag t;
    t.m_TagName = std::string(str);
    return t;
}

bool GameplayTag::MatchesTag(const GameplayTag& parent) const
{
    if (!IsValid() || !parent.IsValid()) return false;

    const std::string& a = parent.m_TagName;  // the shorter "prefix" we test against
    const std::string& b = m_TagName;         // this tag (the potential child/descendant)

    if (a == b) return true;

    // b must start with a followed by '.' to be a proper descendant
    if (b.size() > a.size() &&
        b[a.size()] == '.' &&
        b.compare(0, a.size(), a) == 0)
        return true;

    return false;
}

GameplayTag GameplayTag::RequestParent() const
{
    if (!IsValid()) return Invalid();
    auto pos = m_TagName.rfind('.');
    if (pos == std::string::npos) return Invalid(); // already root
    GameplayTag parent;
    parent.m_TagName = m_TagName.substr(0, pos);
    return parent;
}

int GameplayTag::Depth() const
{
    if (!IsValid()) return 0;
    return 1 + static_cast<int>(std::count(m_TagName.begin(), m_TagName.end(), '.'));
}


// ============================================================================
// GameplayTagContainer
// ============================================================================

void GameplayTagContainer::AddTag(const GameplayTag& tag)
{
    if (tag.IsValid()) m_Tags.insert(tag);
}

void GameplayTagContainer::RemoveTag(const GameplayTag& tag)
{
    m_Tags.erase(tag);
}

bool GameplayTagContainer::HasTag(const GameplayTag& tag) const
{
    // True if any stored tag is a descendant of (or equal to) the query tag.
    for (const auto& t : m_Tags)
        if (t.MatchesTag(tag)) return true;
    return false;
}

bool GameplayTagContainer::HasTagExact(const GameplayTag& tag) const
{
    return m_Tags.count(tag) > 0;
}

bool GameplayTagContainer::HasAnyTags(const GameplayTagContainer& other) const
{
    for (const auto& otherTag : other.m_Tags)
        if (HasTag(otherTag)) return true;
    return false;
}

bool GameplayTagContainer::HasAllTags(const GameplayTagContainer& other) const
{
    for (const auto& otherTag : other.m_Tags)
        if (!HasTag(otherTag)) return false;
    return true;
}

std::vector<GameplayTag> GameplayTagContainer::GetMatchingTags(const GameplayTag& tag) const
{
    std::vector<GameplayTag> result;
    for (const auto& t : m_Tags)
        if (t.MatchesTag(tag)) result.push_back(t);
    return result;
}

} // namespace VibeEngine
