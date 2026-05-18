#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VibeEngine {

// ---------------------------------------------------------------------------
// ICommand — base interface for undoable operations.
// ---------------------------------------------------------------------------
struct ICommand {
    std::string label;           // shown in "Undo <label>" / "Redo <label>"
    virtual ~ICommand() = default;
    virtual void Execute() = 0;  // (re)apply the change
    virtual void Undo()    = 0;  // revert the change
};

// ---------------------------------------------------------------------------
// UndoStack — linear undo / redo history.
//
// Push(cmd)            — call Execute() then push; clears the redo tail.
// PushPreExecuted(cmd) — push without calling Execute() (change already applied).
// Undo() / Redo()      — walk backwards / forwards through the stack.
// Clear()              — flush everything (call on scene restart).
// ---------------------------------------------------------------------------
class UndoStack {
public:
    static constexpr int kMaxDepth = 50;

    // Execute the command and record it for future Undo.
    void Push(std::unique_ptr<ICommand> cmd)
    {
        cmd->Execute();
        PushPreExecuted(std::move(cmd));
    }

    // Record a command whose effect has already been applied (e.g. after
    // the user finishes dragging a gizmo).
    void PushPreExecuted(std::unique_ptr<ICommand> cmd)
    {
        // Discard any redo tail beyond the current position.
        if (m_Top + 1 < static_cast<int>(m_Stack.size()))
            m_Stack.erase(m_Stack.begin() + m_Top + 1, m_Stack.end());

        m_Stack.push_back(std::move(cmd));

        // Keep history within the depth limit.
        if (static_cast<int>(m_Stack.size()) > kMaxDepth)
            m_Stack.erase(m_Stack.begin());
        else
            ++m_Top;
    }

    void Undo()
    {
        if (CanUndo()) m_Stack[m_Top--]->Undo();
    }

    void Redo()
    {
        if (CanRedo()) m_Stack[++m_Top]->Execute();
    }

    bool CanUndo() const { return m_Top >= 0; }
    bool CanRedo() const { return m_Top + 1 < static_cast<int>(m_Stack.size()); }

    const std::string& GetUndoLabel() const
    {
        static const std::string kEmpty;
        return CanUndo() ? m_Stack[m_Top]->label : kEmpty;
    }
    const std::string& GetRedoLabel() const
    {
        static const std::string kEmpty;
        return CanRedo() ? m_Stack[m_Top + 1]->label : kEmpty;
    }

    void Clear()
    {
        m_Stack.clear();
        m_Top = -1;
    }

private:
    std::vector<std::unique_ptr<ICommand>> m_Stack;
    int m_Top = -1;
};

} // namespace VibeEngine
