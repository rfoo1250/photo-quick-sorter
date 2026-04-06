#pragma once
#include <wx/string.h>
#include <wx/filename.h>

namespace MediaUtils {

inline bool IsVideoFile(const wxString& path)
{
    wxString ext = wxFileName(path).GetExt().Lower();
    return ext == "mp4"  || ext == "mov"  || ext == "avi"  || ext == "mkv"
        || ext == "wmv"  || ext == "m4v"  || ext == "flv"  || ext == "webm"
        || ext == "mpg"  || ext == "mpeg";
}

inline bool IsImageFile(const wxString& path)
{
    wxString ext = wxFileName(path).GetExt().Lower();
    return ext == "jpg" || ext == "jpeg" || ext == "png"
        || ext == "webp" || ext == "heic";
}

// Semicolon-separated glob filespec covering all supported images and videos.
inline wxString GetMediaFileSpec()
{
    return "*.jpg;*.jpeg;*.png;*.webp;*.heic;"
           "*.mp4;*.mov;*.avi;*.mkv;*.wmv;*.m4v;*.flv;*.webm;*.mpg;*.mpeg";
}

} // namespace MediaUtils
