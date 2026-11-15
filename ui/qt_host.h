// qt_host.h — the Qt Quick window, when this build has Qt (M4).
//
// The resident plugin and its window are one process, following the workspace's
// shared-Qt convention (ra9fael.envhosts, winshun.capture): Qt Quick needs the
// GUI thread, so the Qt event loop owns the main thread and the JSON-RPC loop
// moves to a worker thread. stdio stays owned by the protocol thread; the GUI
// thread never writes to it.
//
// `rename.exe` is what plugin.json names as runtime.program, so the window has to
// live inside it: the host launches that one exe and nothing else. Headless
// operation stays possible, which is what makes the plugin testable on a machine
// with no Qt: when Qt is absent this whole file is not compiled at all.
#pragma once

#include <string>
#include <vector>

namespace rename_host {

// Creates the Qt application, the theme object and the bridge on the calling
// thread (the GUI thread), then loads Main.qml. Returns false with the reason
// when the scene cannot be built, so the caller can report it instead of
// leaving a process with no window.
bool Start(int argc, char** argv, const std::wstring& dataDir, std::string& error);

// Shows the window for this selection. Marshalled to the GUI thread, so the
// protocol thread may call it.
void Show(const std::vector<std::wstring>& paths, bool recursive);

// Shows the window for "work in this folder": the folder's direct children are
// the rows. Same window, same presenter, same executor.
void ShowFolder(const std::wstring& folder, bool recursive);

// The host's look (docs/plugins.md, 外观): `nativeLookJson` is the compact JSON
// of `appearance.nativeLook`, empty when the host sent none. Marshalled to the
// GUI thread like Show().
void ApplyLook(const std::string& locale, bool dark, const std::string& uiSize,
               const std::string& nativeLookJson);

// Where the read-only batch journal goes. Only known after initialize, which
// runs on the protocol thread well after the window was built.
void SetDataDir(const std::wstring& dataDir);

// Runs the Qt event loop until Quit(). Returns the process exit code.
int Run();

// Ends the event loop; safe to call from the protocol thread.
void Quit();

} // namespace rename_host
