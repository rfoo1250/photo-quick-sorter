#pragma once
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/string.h>

namespace AssetPaths {

// Resolve a file inside assets/, e.g. "single-keys-blank/200dpi/j.png".
//
// Starts at the executable's own directory and walks up, so every build
// layout resolves without tweaking anything:
//   installed         — {exe_dir}/assets/...
//   MSVC / Xcode      — build/{Config}/              (2 up)
//   Ninja / Makefiles — build/                       (1 up)
//   macOS .app        — Foo.app/Contents/MacOS/      (4 up)
// A packaged .app keeps its copy in Contents/Resources, checked first.
// Returns an empty string when the file cannot be found anywhere.
inline wxString Find(const wxString& relative)
{
#ifdef __WXOSX__
    const wxString bundled = wxStandardPaths::Get().GetResourcesDir()
                           + wxFILE_SEP_PATH + "assets" + wxFILE_SEP_PATH + relative;
    if (wxFileExists(bundled)) return bundled;
#endif

    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.MakeAbsolute();

    wxFileName dir(exe.GetPath(), "");
    for (int up = 0; up < 6; ++up) {
        const wxString candidate = dir.GetPathWithSep() + "assets"
                                 + wxFILE_SEP_PATH + relative;
        if (wxFileExists(candidate)) return candidate;
        if (dir.GetDirCount() == 0) break;
        dir.RemoveLastDir();
    }
    return wxEmptyString;
}

} // namespace AssetPaths
