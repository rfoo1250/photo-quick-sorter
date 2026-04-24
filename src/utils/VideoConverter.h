#pragma once
#include <wx/string.h>
#include <functional>

class VideoConverter {
public:
    // Returns "ffmpeg" if found in PATH, a full path if found at a known install
    // location, or an empty string if FFmpeg is not available.
    static wxString FindFFmpegPath();

    // Derives the ffprobe path from a known ffmpeg path.
    // If ffmpegPath is just "ffmpeg" (in PATH), tries "ffprobe" in PATH too.
    static wxString GetFfprobePath(const wxString& ffmpegPath);

    // Probes a media file and returns the first video and audio codec names
    // (e.g. "hevc", "h264", "aac", "opus"). Returns false if ffprobe fails.
    static bool ProbeCodecs(const wxString& ffprobePath,
                            const wxString& filePath,
                            wxString& videoCodec,
                            wxString& audioCodec);

    // Returns true if the given codec combination requires re-encoding for
    // broad compatibility with Windows media players (no extra codec packs).
    // Safe codecs: video = h264, audio = aac | mp3 | empty (no audio stream).
    static bool NeedsReencoding(const wxString& videoCodec,
                                const wxString& audioCodec);

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
