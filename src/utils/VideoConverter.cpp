#include "utils/VideoConverter.h"
#include "utils/logging.h"
#include <wx/utils.h>
#include <wx/process.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/arrstr.h>
#include <wx/stdpaths.h>

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

namespace {

// Probe an ffmpeg executable by running -version. Returns true if it responds
// with exit code 0 (i.e. it is a functional ffmpeg binary).
bool ProbeFFmpeg(const wxString& exe)
{
    wxArrayString out, err;
    wxString cmd = "\"" + exe + "\" -version";
    int ret = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
    return ret == 0;
}

// Returns a collision-free output path: same directory as input, .mp4
// extension. If <stem>.mp4 already exists, tries <stem>_1.mp4, etc.
wxString BuildOutputPath(const wxString& inputPath)
{
    wxFileName fn(inputPath);
    const wxString dir  = fn.GetPath();
    const wxString stem = fn.GetName();

    wxString candidate = dir + wxFILE_SEP_PATH + stem + ".mp4";
    int suffix = 1;
    while (wxFileExists(candidate)) {
        candidate = dir + wxFILE_SEP_PATH
                  + stem + wxString::Format("_%d", suffix++) + ".mp4";
    }
    return candidate;
}

} // namespace

// ─────────────────────────────────────────────
// FindFFmpegPath
// ─────────────────────────────────────────────

wxString VideoConverter::FindFFmpegPath()
{
    static bool     s_initialized = false;
    static wxString s_cached;
    if (s_initialized) return s_cached;
    s_initialized = true;

    // 0. Bundled: {exe_dir}\bin\ffmpeg.exe (installer places it here)
    {
        wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
        exe.SetFullName(""); exe.Normalize();
        wxString bundled = exe.GetFullPath() + "bin\\ffmpeg.exe";
        if (wxFileExists(bundled) && ProbeFFmpeg(bundled)) {
            LOG_DEBUG("VideoConverter: ffmpeg found at bundled path: %s", bundled);
            s_cached = bundled;
            return s_cached;
        }
    }

    // 1. "ffmpeg" in PATH
    {
        wxArrayString out, err;
        int ret = wxExecute("ffmpeg -version", out, err,
                            wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
        if (ret == 0) {
            LOG_DEBUG("VideoConverter: ffmpeg found in PATH");
            s_cached = "ffmpeg";
            return s_cached;
        }
    }

    // 2. Common Windows install locations
    static const wxString knownPaths[] = {
        "C:\\Tools\\FFmpeg-build\\bin\\ffmpeg.exe",
        "C:\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\Program Files (x86)\\ffmpeg\\bin\\ffmpeg.exe",
        "C:\\tools\\ffmpeg\\bin\\ffmpeg.exe",
    };
    for (const auto& p : knownPaths) {
        if (wxFileExists(p) && ProbeFFmpeg(p)) {
            LOG_DEBUG("VideoConverter: ffmpeg found at %s", p);
            s_cached = p;
            return s_cached;
        }
    }

    LOG_DEBUG("VideoConverter: ffmpeg not found");
    return s_cached; // empty
}

// ─────────────────────────────────────────────
// GetFfprobePath
// ─────────────────────────────────────────────

wxString VideoConverter::GetFfprobePath(const wxString& ffmpegPath)
{
    // If ffmpeg is a full path, swap the binary name to ffprobe
    if (ffmpegPath != "ffmpeg") {
        wxFileName fn(ffmpegPath);
        fn.SetName("ffprobe");
        wxString candidate = fn.GetFullPath();
        if (wxFileExists(candidate))
            return candidate;
    }

    // ffmpeg was found in PATH — check if ffprobe is there too
    wxArrayString out, err;
    int ret = wxExecute("ffprobe -version", out, err,
                        wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
    if (ret == 0)
        return "ffprobe";

    return wxString();
}

// ─────────────────────────────────────────────
// ProbeCodecs
// ─────────────────────────────────────────────

bool VideoConverter::ProbeCodecs(const wxString& ffprobePath,
                                  const wxString& filePath,
                                  wxString& videoCodec,
                                  wxString& audioCodec)
{
    const wxString base = wxString::Format(
        "\"%s\" -v error -show_entries stream=codec_name "
        "-of default=noprint_wrappers=1:nokey=1 ", ffprobePath);

    // Video stream
    {
        wxArrayString out, err;
        wxString cmd = base + "-select_streams v:0 \"" + filePath + "\"";
        int ret = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
        if (ret != 0 || out.IsEmpty()) return false;
        videoCodec = out[0].Lower().Trim().Trim(false);
    }

    // Audio stream (no audio is valid — treat non-zero exit as "no audio")
    {
        wxArrayString out, err;
        wxString cmd = base + "-select_streams a:0 \"" + filePath + "\"";
        wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
        if (!out.IsEmpty())
            audioCodec = out[0].Lower().Trim().Trim(false);
    }

    return !videoCodec.IsEmpty();
}

// ─────────────────────────────────────────────
// NeedsReencoding
// ─────────────────────────────────────────────

bool VideoConverter::NeedsReencoding(const wxString& videoCodec,
                                      const wxString& audioCodec)
{
    if (videoCodec.IsEmpty()) return false; // can't determine — leave it alone

    // Windows Media Player supports H.264 natively; everything else may require
    // a paid codec pack (HEVC, VP9, AV1, etc.)
    const bool videoOk = (videoCodec == "h264");

    // AAC and MP3 are universally supported; empty means no audio stream
    const bool audioOk = audioCodec.IsEmpty()
                      || audioCodec == "aac"
                      || audioCodec == "mp3";

    return !videoOk || !audioOk;
}

// ─────────────────────────────────────────────
// ConvertToMp4
// ─────────────────────────────────────────────

bool VideoConverter::ConvertToMp4(const wxString& ffmpegPath,
                                   const wxString& inputPath,
                                   wxString&       outputPath,
                                   wxString&       errorMsg,
                                   std::function<bool()> tickCallback)
{
    if (!wxFileExists(inputPath)) {
        errorMsg = "Input file does not exist.";
        return false;
    }

    const wxString candidate = BuildOutputPath(inputPath);

    // -y               : overwrite output if it somehow already exists
    // -c:v libx264     : H.264 video — compatible with all MP4 players
    // -crf 18          : near-lossless quality (0=lossless, 51=worst)
    // -preset fast     : faster than default 'medium' with minimal size penalty
    // -c:a aac         : AAC audio — the standard for MP4 containers
    // -b:a 192k        : high-quality audio bitrate
    // -movflags +faststart : move moov atom to front for fast playback start
    const wxString cmd = wxString::Format(
        "\"%s\" -y -i \"%s\" -c:v libx264 -crf 18 -preset fast "
        "-c:a aac -b:a 192k -movflags +faststart \"%s\"",
        ffmpegPath, inputPath, candidate);

    LOG_DEBUG("VideoConverter: running: %s", cmd);

    // Launch async so the caller can tick the progress UI while we wait.
    // Redirect stderr so we can capture FFmpeg's output for error reporting.
    struct ProcResult { bool done = false; int code = -1; };
    ProcResult result;

    wxProcess* proc = new wxProcess();
    proc->Redirect();
    proc->Bind(wxEVT_END_PROCESS, [&result](wxProcessEvent& evt) {
        result.done = true;
        result.code = evt.GetExitCode();
    });

    long pid = wxExecute(cmd, wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, proc);
    if (pid == 0) {
        delete proc;
        errorMsg = "Failed to launch FFmpeg.";
        return false;
    }

    // Accumulate FFmpeg's stderr while polling.
    // We drain the pipe every tick to prevent the buffer from filling up,
    // which would block FFmpeg and stall the loop.
    wxString stderrAccum;
    auto drainStderr = [&]() {
        wxInputStream* err = proc->GetErrorStream();
        if (!err || !proc->IsErrorAvailable()) return;
        char buf[512];
        while (proc->IsErrorAvailable()) {
            err->Read(buf, sizeof(buf) - 1);
            size_t n = err->LastRead();
            if (n == 0) break;
            stderrAccum += wxString::FromUTF8(buf, n);
        }
    };

    bool cancelled = false;
    while (!result.done) {
        wxMilliSleep(100);
        wxYield();
        drainStderr();
        if (tickCallback && !tickCallback()) {
            wxKill(pid, wxSIGKILL);
            cancelled = true;
            for (int i = 0; i < 50 && !result.done; ++i) {
                wxMilliSleep(100);
                wxYield();
            }
            break;
        }
    }
    drainStderr(); // final drain after process exits

    if (cancelled) {
        if (wxFileExists(candidate)) wxRemoveFile(candidate);
        errorMsg = "Cancelled.";
        return false;
    }

    if (result.code != 0 || !wxFileExists(candidate)) {
        if (wxFileExists(candidate)) wxRemoveFile(candidate);
        errorMsg = stderrAccum.IsEmpty()
            ? wxString::Format("FFmpeg exited with code %d.", result.code)
            : stderrAccum;
        LOG_ERROR("VideoConverter: conversion failed for %s (exit %d)", inputPath, result.code);
        return false;
    }

    outputPath = candidate;
    LOG_INFO("VideoConverter: converted %s -> %s", inputPath, outputPath);
    return true;
}
