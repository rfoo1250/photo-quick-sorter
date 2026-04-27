#include "utils/logging.h"
#include "utils/MediaUtils.h"
#include "utils/VideoConverter.h"
#include "panels/MainMenuPanel.h"
#include "ui/PhotoQuickSorterFrame.h"
#include "ui/Theme.h"
#include "ui/StyledButton.h"
#include "core/FolderLocations.h"
#include <wx/progdlg.h>
#include <wx/collpane.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/filefn.h>
#include <algorithm>

namespace {

wxBitmap LoadMenuKeycap(const wxString& filename) {
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.Normalize(); exe.SetFullName("");
    // Installed: assets/ next to the exe
    wxString path = exe.GetFullPath() + "assets/single-keys-blank/200dpi/" + filename;
    if (!wxFileExists(path)) {
        // Dev: CMake puts exe in build/{Config}/, project root is 2 levels up
        wxFileName dev = exe; dev.RemoveLastDir(); dev.RemoveLastDir();
        path = dev.GetFullPath() + "assets/single-keys-blank/200dpi/" + filename;
    }
    if (!wxFileExists(path)) return wxNullBitmap;
    wxImage img(path, wxBITMAP_TYPE_PNG);
    if (!img.IsOk()) return wxNullBitmap;
    img = img.Scale(24, 24, wxIMAGE_QUALITY_HIGH);
    return wxBitmap(img);
}

// yesNo=true → Yes(default)/No buttons; false → OK(default) only
// Returns true if user clicked Yes/OK
bool ShowIconDialog(wxWindow* parent, const wxString& title, const wxString& msg, bool yesNo = false) {
    wxDialog dlg(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);

    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* text = new wxStaticText(&dlg, wxID_ANY, msg,
                                          wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    text->Wrap(360);
    vSizer->Add(text, 0, wxALIGN_CENTER | wxALL, 20);

    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    wxBitmap icEnter = LoadMenuKeycap("enter.png");

    if (yesNo) {
        // wxDialog only auto-closes for wxID_OK (affirmative) and wxID_CANCEL (escape)
        wxButton* yesBtn = new wxButton(&dlg, wxID_OK,     "Yes");
        wxButton* noBtn  = new wxButton(&dlg, wxID_CANCEL, "No");
        if (icEnter.IsOk()) { yesBtn->SetBitmap(icEnter); yesBtn->SetBitmapPosition(wxLEFT); }
        yesBtn->SetDefault();
        btnRow->Add(yesBtn, 0, wxALL, 8);
        btnRow->Add(noBtn,  0, wxALL, 8);
    } else {
        wxButton* okBtn = new wxButton(&dlg, wxID_OK, "OK");
        if (icEnter.IsOk()) { okBtn->SetBitmap(icEnter); okBtn->SetBitmapPosition(wxLEFT); }
        okBtn->SetDefault();
        btnRow->Add(okBtn, 0, wxALL, 8);
    }

    vSizer->Add(btnRow, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    dlg.SetSizerAndFit(vSizer);
    dlg.Centre();

    return dlg.ShowModal() == wxID_OK;
}

wxString FriendlyCodecName(const wxString& codec)
{
    if (codec == "hevc")        return "HEVC/H.265";
    if (codec == "vp9")         return "VP9";
    if (codec == "av1")         return "AV1";
    if (codec == "vp8")         return "VP8";
    if (codec == "mpeg4")       return "MPEG-4";
    if (codec == "mpeg2video")  return "MPEG-2";
    if (codec == "wmv3")        return "WMV3";
    if (codec == "theora")      return "Theora";
    return codec.Upper();
}

} // namespace

MainMenuPanel::MainMenuPanel(wxWindow *parent)
    : wxPanel(parent)
{
    SetBackgroundColour(Theme::BgPanel);

    // Project root for image path
    wxFileName exeFile(wxStandardPaths::Get().GetExecutablePath());
    wxFileName projectRoot = exeFile;
    projectRoot.Normalize();
    projectRoot.SetFullName("");
    projectRoot.RemoveLastDir();
    projectRoot.RemoveLastDir();

    // --- Controls ---
    wxStaticText *baseFolderNameLabel = new wxStaticText(this, wxID_ANY, "Base Folder path:");
    baseFolderNameLabel->SetFont(Theme::FontBody());
    m_baseFolderText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton *baseFolderNameBrowseBtn = new wxButton(this, ID_BROWSE_BASE, "Browse");

    wxStaticText *folder1NameLabel = new wxStaticText(this, wxID_ANY, "Folder 1 path:");
    folder1NameLabel->SetFont(Theme::FontBody());
    m_folder1Text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton *folder1NameBrowseBtn = new wxButton(this, ID_BROWSE_1, "Browse");

    wxStaticText *folder2NameLabel = new wxStaticText(this, wxID_ANY, "Folder 2 path:");
    folder2NameLabel->SetFont(Theme::FontBody());
    m_folder2Text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton *folder2NameBrowseBtn = new wxButton(this, ID_BROWSE_2, "Browse");
    // image test
    // wxString imagePath = projectRoot.GetFullPath() + "/assets/Nice_Nature.jpeg";

    auto *toSortPanelBtn = new StyledButton(this, ID_TO_SORT_PANEL, "Start sorting!",
                                             Theme::Accent, Theme::AccentText);
    toSortPanelBtn->SetFont(Theme::MakeFont(11, wxFONTWEIGHT_SEMIBOLD));
    {
        wxBitmap ic = LoadMenuKeycap("enter.png");
        if (ic.IsOk()) {
            wxImage img = ic.ConvertToImage();
            img = img.Scale(20, 20, wxIMAGE_QUALITY_HIGH);
            toSortPanelBtn->SetBitmap(wxBitmap(img));
        }
    }

    // --- Layout setup ---
    wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);

    auto makeRow = [&](wxStaticText *label, wxTextCtrl *text, wxButton *button)
    {
        wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, Theme::PadSmall);
        hbox->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxALL, Theme::PadSmall);
        hbox->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxALL, Theme::PadSmall);
        vbox->Add(hbox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, Theme::PadMedium);
    };

    makeRow(baseFolderNameLabel, m_baseFolderText, baseFolderNameBrowseBtn);
    makeRow(folder1NameLabel, m_folder1Text, folder1NameBrowseBtn);
    makeRow(folder2NameLabel, m_folder2Text, folder2NameBrowseBtn);

    // Image test (kept for reference)
    // if (wxFileExists(imagePath))
    // {
    //     wxImage::AddHandler(new wxJPEGHandler()); // always forgot this
    //     wxImage logoImage(imagePath, wxBITMAP_TYPE_JPEG);
    //     int maxHeight = 200;
    //     if (logoImage.GetHeight() > maxHeight)
    //     {
    //         double scale = (double)maxHeight / logoImage.GetHeight();
    //         logoImage = logoImage.Scale(logoImage.GetWidth() * scale, maxHeight, wxIMAGE_QUALITY_HIGH);
    //     }
    //     wxStaticBitmap *logo = new wxStaticBitmap(this, wxID_ANY, wxBitmap(logoImage));
    //     vbox->Add(logo, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    // }
    // else
    // {
    //     vbox->Add(new wxStaticText(this, wxID_ANY, "Image not found"), 0, wxALIGN_CENTER | wxBOTTOM, 10);
    // }

    // --- Base folder preview ---
    m_thumbnailGrid = new ThumbnailGrid(this, "Select a Base Folder to preview media.");
    vbox->Add(m_thumbnailGrid, 1, wxEXPAND | wxALL, Theme::PadMedium);
    vbox->Add(toSortPanelBtn, 0, wxEXPAND | wxALL, Theme::PadMedium);
    SetSizer(vbox);

    // --- Events ---
    baseFolderNameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    folder1NameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    folder2NameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    toSortPanelBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnStartSorting, this);
    m_baseFolderText->Bind(wxEVT_KILL_FOCUS, &MainMenuPanel::OnBaseFolderFocusLost, this);

    // Enter key from any focused child fires Start sorting
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_RETURN) {
            wxCommandEvent cmd(wxEVT_BUTTON, ID_TO_SORT_PANEL);
            cmd.SetEventObject(this);
            OnStartSorting(cmd);
        } else {
            e.Skip();
        }
    });

}

void MainMenuPanel::OnBrowseFolder(wxCommandEvent &event)
{
    int id = event.GetId();

    wxString startDir;
    if (id == ID_BROWSE_BASE) {
        // Start the dialog at Pictures when the field is empty — a convenient
        // default without pre-filling the text box (which would trigger scans).
        wxString cur = m_baseFolderText->GetValue().Trim(true).Trim(false);
        if (cur.IsEmpty()) {
            wxString pics = wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Pictures);
            if (wxDir::Exists(pics)) startDir = pics;
        } else {
            startDir = cur;
        }
    } else {
        // For folder 1 & 2, start at the parent of the base folder if one is set
        wxString base = m_baseFolderText->GetValue().Trim(true).Trim(false);
        if (!base.IsEmpty()) {
            wxFileName fn(base);
            fn.Normalize();
            startDir = fn.GetPath();
        }
    }

    wxDirDialog dlg(this, "Select a folder", startDir, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK)
    {
        wxString path = dlg.GetPath();
        auto *frame = dynamic_cast<PhotoQuickSorterFrame *>(GetParent());
        if (!frame)
            return;

        if (id == ID_BROWSE_BASE)
        {
            m_baseFolderText->SetValue(path);
            frame->folderLocations.baseFolder = path;
            LOG_DEBUG("Base folder: %s", frame->folderLocations.baseFolder);
            RefreshPreview(path);
        }
        else if (id == ID_BROWSE_1)
        {
            m_folder1Text->SetValue(path);
            frame->folderLocations.folder1 = path;
            LOG_DEBUG("Folder 1: %s", frame->folderLocations.folder1);
        }
        else if (id == ID_BROWSE_2)
        {
            m_folder2Text->SetValue(path);
            frame->folderLocations.folder2 = path;
            LOG_DEBUG("Folder 2: %s", frame->folderLocations.folder2);
        }
    }
}

void MainMenuPanel::RefreshPreview()
{
    if (!m_baseFolderText) return;
    RefreshPreview(m_baseFolderText->GetValue().Trim(true).Trim(false));
}

void MainMenuPanel::OnBaseFolderFocusLost(wxFocusEvent& event)
{
    event.Skip(); // always skip focus events
    RefreshPreview();
}

void MainMenuPanel::RefreshPreview(const wxString& folderPath)
{
    if (!m_thumbnailGrid) return;

    if (folderPath.IsEmpty() || !wxDir::Exists(folderPath)) {
        m_thumbnailGrid->SetEmptyHint("Select a Base Folder to preview media.");
        m_thumbnailGrid->SetImages({});
        Layout();
        return;
    }

    // Convert non-mp4 videos before scanning so thumbnails show the final state
    RunVideoConversionIfNeeded(folderPath);

    // Collect all supported media files (images + videos)
    std::vector<wxString> files;
    wxDir dir(folderPath);
    if (dir.IsOpened()) {
        wxArrayString specs = wxSplit(MediaUtils::GetMediaFileSpec(), ';');
        for (const auto& spec : specs) {
            wxString fn;
            bool has = dir.GetFirst(&fn, spec, wxDIR_FILES);
            while (has) {
                files.push_back(folderPath + wxFILE_SEP_PATH + fn);
                has = dir.GetNext(&fn);
            }
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    if (files.empty()) {
        m_thumbnailGrid->SetEmptyHint("This folder is empty - no supported media files found.");
        m_thumbnailGrid->SetImages({});
        Layout();
        return;
    }

    m_thumbnailGrid->SetImages(files);
    Layout();
}

void MainMenuPanel::RunVideoConversionIfNeeded(const wxString& folderPath)
{
    static const int ID_CONVERT = wxID_HIGHEST + 300;
    static const int ID_SKIP    = wxID_HIGHEST + 301;

    struct ConvertEntry { wxString path, label; bool isMp4; };
    std::vector<ConvertEntry> toConvert;

    // --- 1. Collect non-MP4 video files (no FFmpeg needed) ---
    {
        wxDir dir(folderPath);
        if (!dir.IsOpened()) return;
        static const wxString specs[] = {
            "*.mov", "*.avi", "*.mkv", "*.wmv",
            "*.m4v", "*.flv", "*.webm", "*.mpg", "*.mpeg"
        };
        for (const auto& spec : specs) {
            wxString fn;
            bool has = dir.GetFirst(&fn, spec, wxDIR_FILES);
            while (has) {
                toConvert.push_back({ folderPath + wxFILE_SEP_PATH + fn, fn, false });
                has = dir.GetNext(&fn);
            }
        }
        std::sort(toConvert.begin(), toConvert.end(),
                  [](const ConvertEntry& a, const ConvertEntry& b){ return a.path < b.path; });
    }

    // --- 2. Find FFmpeg ---
    const wxString ffmpegPath = VideoConverter::FindFFmpegPath();

    // --- 3. If FFmpeg/ffprobe available, probe .mp4 files for incompatible codecs ---
    if (!ffmpegPath.IsEmpty()) {
        const wxString ffprobePath = VideoConverter::GetFfprobePath(ffmpegPath);
        if (!ffprobePath.IsEmpty()) {
            wxBusyCursor busy;
            wxDir dir(folderPath);
            if (dir.IsOpened()) {
                wxString fn;
                bool has = dir.GetFirst(&fn, "*.mp4", wxDIR_FILES);
                while (has) {
                    const wxString fullPath = folderPath + wxFILE_SEP_PATH + fn;
                    wxString videoCodec, audioCodec;
                    if (VideoConverter::ProbeCodecs(ffprobePath, fullPath, videoCodec, audioCodec) &&
                        VideoConverter::NeedsReencoding(videoCodec, audioCodec)) {
                        const wxString label = fn + " (" + FriendlyCodecName(videoCodec) + " encoding)";
                        toConvert.push_back({ fullPath, label, true });
                    }
                    has = dir.GetNext(&fn);
                }
            }
        }
    }

    if (toConvert.empty()) return;

    // --- 4. FFmpeg required but not found ---
    if (ffmpegPath.IsEmpty()) {
        wxMessageBox(
            wxString::Format(
                "Found %zu video file(s) that need conversion, but FFmpeg was not found.\n\n"
                "Install FFmpeg and add it to your PATH to enable automatic conversion.\n"
                "Sorting will continue with the original files.",
                toConvert.size()),
            "FFmpeg Not Found",
            wxOK | wxICON_INFORMATION, this);
        return;
    }

    // --- 5. Confirmation dialog ---
    wxString fileListText;
    for (const auto& e : toConvert)
        fileListText += "- " + e.label + "\n";

    wxDialog confirmDlg(this, wxID_ANY, "Convert Videos to MP4",
                        wxDefaultPosition, wxDefaultSize,
                        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* msgText = new wxStaticText(&confirmDlg, wxID_ANY,
        wxString::Format(
            "%zu video file(s) will be converted to MP4 (H.264 + AAC).\n"
            "Originals will be deleted after successful conversion.",
            toConvert.size()));
    msgText->Wrap(420);
    vSizer->Add(msgText, 0, wxALL, 16);

    wxTextCtrl* fileList = new wxTextCtrl(&confirmDlg, wxID_ANY,
        fileListText, wxDefaultPosition, wxSize(420, 120),
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    vSizer->Add(fileList, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    wxButton* convertBtn = new wxButton(&confirmDlg, ID_CONVERT,
                                        "Convert and delete originals");
    wxButton* skipBtn    = new wxButton(&confirmDlg, ID_SKIP, "Skip");

    wxBitmap icEnter = LoadMenuKeycap("enter.png");
    wxBitmap icEsc   = LoadMenuKeycap("esc.png");
    if (icEnter.IsOk()) { convertBtn->SetBitmap(icEnter); convertBtn->SetBitmapPosition(wxLEFT); }
    if (icEsc.IsOk())   { skipBtn->SetBitmap(icEsc);      skipBtn->SetBitmapPosition(wxLEFT);    }
    convertBtn->SetDefault();

    convertBtn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { confirmDlg.EndModal(ID_CONVERT); });
    skipBtn->Bind(wxEVT_BUTTON,    [&](wxCommandEvent&) { confirmDlg.EndModal(ID_SKIP);    });

    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->Add(convertBtn, 0, wxALL, 8);
    btnRow->Add(skipBtn,    0, wxALL, 8);
    vSizer->Add(btnRow, 0, wxALIGN_CENTER | wxBOTTOM, 12);

    confirmDlg.SetSizerAndFit(vSizer);
    confirmDlg.Centre();

    if (confirmDlg.ShowModal() != ID_CONVERT) return;

    // --- 6. Conversion loop ---
    wxProgressDialog progress(
        "Converting Videos", "Preparing...",
        (int)toConvert.size(), this,
        wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_AUTO_HIDE);

    struct FailureInfo { wxString name, detail; };
    std::vector<FailureInfo> failures;
    bool userCancelled = false;

    for (size_t i = 0; i < toConvert.size(); ++i) {
        const ConvertEntry& entry    = toConvert[i];
        const wxString&     srcPath  = entry.path;
        const wxString      shortName = wxFileName(srcPath).GetFullName();
        const wxString      label    = wxString::Format("Converting %s (%zu/%zu)...",
                                                         shortName, i + 1, toConvert.size());

        if (!progress.Update((int)i, label)) { userCancelled = true; break; }

        wxString outputPath, errorMsg;
        bool convCancelled = false;

        bool ok = VideoConverter::ConvertToMp4(ffmpegPath, srcPath, outputPath, errorMsg,
            [&]() -> bool {
                bool cont = progress.Update((int)i, label);
                if (!cont) convCancelled = true;
                return cont;
            });

        if (convCancelled) { userCancelled = true; break; }

        if (ok) {
            if (entry.isMp4) {
                // In-place re-encode: output went to a collision-free temp name.
                // Delete the original then rename the temp back to the original path.
                if (!wxRemoveFile(srcPath)) {
                    LOG_WARN("RunVideoConversionIfNeeded: delete original failed: %s", srcPath);
                    failures.push_back({shortName,
                        "Re-encoded successfully but the original file could not be deleted.\n"
                        "The re-encoded copy is at: " + outputPath});
                } else if (!wxRenameFile(outputPath, srcPath)) {
                    LOG_WARN("RunVideoConversionIfNeeded: rename failed: %s -> %s",
                             outputPath, srcPath);
                    failures.push_back({shortName,
                        "Re-encoded successfully but the file could not be renamed.\n"
                        "The re-encoded copy is at: " + outputPath});
                }
            } else {
                // Non-MP4: output already has the correct .mp4 name; just remove original.
                if (!wxRemoveFile(srcPath)) {
                    LOG_WARN("RunVideoConversionIfNeeded: delete original failed: %s", srcPath);
                    failures.push_back({shortName,
                        "Converted successfully but the original file could not be deleted."});
                }
            }
        } else if (errorMsg != "Cancelled.") {
            LOG_ERROR("RunVideoConversionIfNeeded: %s failed: %s", shortName, errorMsg);
            failures.push_back({shortName, errorMsg});
        }
    }

    progress.Update((int)toConvert.size(), "Done.");

    // --- 7. Report any issues ---
    if (!failures.empty() || userCancelled) {
        wxDialog errDlg(this, wxID_ANY, "Conversion Issues",
                        wxDefaultPosition, wxDefaultSize,
                        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

        wxBoxSizer* vSizer = new wxBoxSizer(wxVERTICAL);

        wxString mainMsg;
        if (userCancelled)
            mainMsg = "Conversion was cancelled.\n"
                      "Already-converted originals have been deleted.";
        if (!failures.empty()) {
            if (!mainMsg.IsEmpty()) mainMsg += "\n";
            mainMsg += wxString::Format("%zu file(s) could not be converted.", failures.size());
        }
        mainMsg += "\n\nSorting will continue with the remaining files.";

        wxStaticText* msgText = new wxStaticText(&errDlg, wxID_ANY, mainMsg);
        msgText->Wrap(400);
        vSizer->Add(msgText, 0, wxALL, 16);

        if (!failures.empty()) {
            wxString fileListText;
            for (const auto& f : failures)
                fileListText += "- " + f.name + "\n";

            wxTextCtrl* fileListCtrl = new wxTextCtrl(
                &errDlg, wxID_ANY, fileListText,
                wxDefaultPosition, wxSize(400, 80),
                wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
            vSizer->Add(fileListCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

            wxString detailsText;
            for (const auto& f : failures)
                detailsText += "--- " + f.name + " ---\n" + f.detail + "\n\n";

            wxCollapsiblePane* pane = new wxCollapsiblePane(
                &errDlg, wxID_ANY, "Details",
                wxDefaultPosition, wxDefaultSize,
                wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);

            wxBoxSizer* paneSizer = new wxBoxSizer(wxVERTICAL);
            wxTextCtrl* detailCtrl = new wxTextCtrl(
                pane->GetPane(), wxID_ANY, detailsText,
                wxDefaultPosition, wxSize(400, 150),
                wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
            paneSizer->Add(detailCtrl, 1, wxEXPAND);
            pane->GetPane()->SetSizer(paneSizer);

            pane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED,
                [&errDlg](wxCollapsiblePaneEvent&) {
                    errDlg.Fit();
                    errDlg.Layout();
                });

            vSizer->Add(pane, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
        }

        wxButton* okBtn = new wxButton(&errDlg, wxID_OK, "OK");
        wxBitmap icEnter = LoadMenuKeycap("enter.png");
        if (icEnter.IsOk()) { okBtn->SetBitmap(icEnter); okBtn->SetBitmapPosition(wxLEFT); }
        okBtn->SetDefault();
        vSizer->Add(okBtn, 0, wxALIGN_CENTER | wxBOTTOM, 12);

        errDlg.SetSizerAndFit(vSizer);
        errDlg.Centre();
        errDlg.ShowModal();
    }

    // No repo rebuild needed — RefreshPreview's own scan runs immediately after
    // this call and will pick up the new .mp4 files.
}

void MainMenuPanel::OnStartSorting(wxCommandEvent &event)
{
    if (!IsShown()) return;

    LOG_DEBUG("Start sorting button pressed");

    auto *frame = dynamic_cast<PhotoQuickSorterFrame *>(GetParent());
    if (!frame) {
        LOG_ERROR("OnStartSorting: Parent frame not found");
        return;
    }

    // Read text boxes as the source of truth
    frame->folderLocations.baseFolder = m_baseFolderText->GetValue().Trim(true).Trim(false);
    frame->folderLocations.folder1    = m_folder1Text->GetValue().Trim(true).Trim(false);
    frame->folderLocations.folder2    = m_folder2Text->GetValue().Trim(true).Trim(false);

    const wxString baseFolder = frame->folderLocations.baseFolder;
    LOG_DEBUG("Base folder path: '%s'", baseFolder);

    // 1) Base folder empty check
    if (baseFolder.IsEmpty()) {
        LOG_WARN("OnStartSorting: Base folder path is empty");
        wxMessageBox(
            "Please select a Base Folder before starting sorting.",
            "Base Folder Required",
            wxOK | wxICON_WARNING,
            this);
        return;
    }

    // 2) Base folder existence check
    if (!wxDir::Exists(baseFolder)) {
        LOG_ERROR("OnStartSorting: Base folder does not exist: %s", baseFolder);
        wxMessageBox(
            "The selected Base Folder does not exist. Please choose a valid folder.",
            "Folder Not Found",
            wxOK | wxICON_ERROR,
            this);
        return;
    }

    // 3a) Duplicate path check: warn for any pair that shares the same location
    const wxString& f1 = frame->folderLocations.folder1;
    const wxString& f2 = frame->folderLocations.folder2;
    {
        wxArrayString conflicts;
        if (!baseFolder.IsEmpty() && !f1.IsEmpty() && baseFolder == f1)
            conflicts.Add("Base Folder and Folder 1 are the same location.");
        if (!baseFolder.IsEmpty() && !f2.IsEmpty() && baseFolder == f2)
            conflicts.Add("Base Folder and Folder 2 are the same location.");
        if (!f1.IsEmpty() && !f2.IsEmpty() && f1 == f2)
            conflicts.Add("Folder 1 and Folder 2 are the same location.");

        if (!conflicts.IsEmpty()) {
            wxString details;
            for (const auto& line : conflicts)
                details += "- " + line + "\n";
            bool proceed = ShowIconDialog(this,
                "Duplicate Folder Paths",
                wxString::Format(
                    "%s\nImages may overwrite each other or be sorted into their source folder.\n\nContinue anyway?",
                    details),
                true);
            if (!proceed) return;
        }
    }

    // 3b) Empty destination warning: ask before proceeding
    if (f1.IsEmpty() || f2.IsEmpty()) {
        wxString which;
        if (f1.IsEmpty() && f2.IsEmpty())
            which = "Folder 1 and Folder 2 are both";
        else if (f1.IsEmpty())
            which = "Folder 1 is";
        else
            which = "Folder 2 is";
        bool proceed = ShowIconDialog(this,
            "Empty Folder Path",
            wxString::Format(
                "%s not set.\n\nImages sorted to that destination will stay in the base folder.\n\nContinue anyway?",
                which),
            true);
        if (!proceed) return;
    }

    // 3) Folder1 / Folder2: auto-create if path provided but missing
    const wxString folders[2] = { frame->folderLocations.folder1, frame->folderLocations.folder2 };
    const char*    labels[2]  = { "Folder 1", "Folder 2" };
    for (int i = 0; i < 2; ++i) {
        const wxString& path = folders[i];
        if (!path.IsEmpty() && !wxDir::Exists(path)) {
            if (wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
                LOG_INFO("OnStartSorting: Created %s: %s", labels[i], path);
            } else {
                LOG_ERROR("OnStartSorting: Failed to create %s: %s", labels[i], path);
                wxMessageBox(
                    wxString::Format("Could not create %s:\n%s", labels[i], path),
                    "Folder Creation Failed",
                    wxOK | wxICON_ERROR,
                    this);
                return;
            }
        }
    }

    // 4) Scan base folder for images
    LOG_DEBUG("Building image repository from base folder...");
    frame->imageRepo.Clear();
    if (!frame->imageRepo.BuildFromFolder(baseFolder)) {
        LOG_ERROR("OnStartSorting: Failed to scan folder: %s", baseFolder);
        wxMessageBox(
            "Failed to scan the Base Folder for images.",
            "Scan Failed",
            wxOK | wxICON_ERROR,
            this);
        return;
    }

    const size_t imageCount = frame->imageRepo.GetCount();
    LOG_DEBUG("Image repository built. Found %zu image(s).", imageCount);

    if (imageCount == 0) {
        LOG_WARN("OnStartSorting: No supported media files found in: %s", baseFolder);
        wxMessageBox(
            "No supported media files were found in the selected Base Folder.",
            "No Media Found",
            wxOK | wxICON_INFORMATION,
            this);
        return;
    }

    // 5) All checks passed
    LOG_DEBUG("OnStartSorting: Validation successful. Switching to SortPhotosPanel.");
    frame->ShowSortPhotosPanel();
}


