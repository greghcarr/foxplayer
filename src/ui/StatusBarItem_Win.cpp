// Windows stub for StatusBarItem. The macOS implementation creates an
// NSStatusItem in the menu bar; the Windows equivalent will be a
// Shell_NotifyIcon system-tray entry with a context menu. For now the stub
// keeps the public surface intact so the rest of the app builds; the tray
// icon simply isn't shown.

#include "StatusBarItem.h"

namespace Stylus
{

struct StatusBarItem::Impl {};

StatusBarItem::StatusBarItem()  : impl_(std::make_unique<Impl>()) {}
StatusBarItem::~StatusBarItem() = default;

void StatusBarItem::setState(State /*state*/) {}

} // namespace Stylus
