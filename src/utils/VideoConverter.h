#pragma once
#include <wx/string.h>
#include <functional>

class VideoConverter {
public:
    // Returns "ffmpeg" if found in PATH, a full path if found at a known install
    // location, or an empty string if FFmpeg is not available.
    static wxString FindFFmpegPath();

    // Converts inputPath to MP4 (H.264 + AAC) using FFmpeg.
    // On success: returns true, sets outputPath to the new .mp4 file.
    // On failure: returns false, sets errorMsg; any partial output is deleted.
    // Output is placed in the same directory as input; name collisions are
    // resolved by appending _1, _2, etc. before the extension.
    //
    // tickCallback is called every ~100 ms while FFmpeg runs. Return true to
    // continue, false to cancel (the FFmpeg process will be killed).
    static bool ConvertToMp4(const wxString& ffmpegPath,
                             const wxString& inputPath,
                             wxString&       outputPath,
                             wxString&       errorMsg,
                             std::function<bool()> tickCallback = nullptr);
};
