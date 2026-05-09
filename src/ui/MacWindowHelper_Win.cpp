// Windows stub for the macOS-only window/dock helpers declared in
// MacWindowHelper.h. Windows has no Dock-equivalent reopen behaviour: clicking
// the taskbar icon already restores a JUCE-backed window, and there is no
// "Active Space" concept that requires native NSWindow manipulation. The
// JUCE-side calls in MainWindow::showWindow() (setVisible / toFront /
// grabKeyboardFocus) are sufficient on Windows, so these helpers do nothing.

#include "MacWindowHelper.h"

void Stylus_activateAndShowWindow(void* /*nsWindowHandle*/) {}

void Stylus_activateExistingWindow(void* /*nsWindowHandle*/) {}

void Stylus_setDockReopenCallback(std::function<void()> /*callback*/) {}
