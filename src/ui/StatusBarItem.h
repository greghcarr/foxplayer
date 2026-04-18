#pragma once

#include <functional>
#include <memory>

namespace FoxPlayer
{

// macOS menu-bar status item.
// Left-click shows the app window. Right-click shows "Show App" / "Quit FoxPlayer".
class StatusBarItem
{
public:
    enum class State { Stopped, Playing, Paused };

    StatusBarItem();
    ~StatusBarItem();

    void setState(State state);

    std::function<void()> onShowApp;
    std::function<void()> onQuit;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    StatusBarItem(const StatusBarItem&)            = delete;
    StatusBarItem& operator=(const StatusBarItem&) = delete;
};

} // namespace FoxPlayer
