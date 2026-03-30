#include "utils/logging.h"
#include "panels/MainMenuPanel.h"
#include "ui/PhotoQuickSorterFrame.h"
#include "core/FolderLocations.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/filefn.h>
#include <algorithm>

namespace {

wxBitmap LoadMenuKeycap(const wxString& filename) {
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.Normalize(); exe.SetFullName(""); exe.RemoveLastDir(); exe.RemoveLastDir();
    wxString path = exe.GetFullPath() + "assets/single-keys-blank/200dpi/" + filename;
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
        yesBtn->SetMinSize(wxSize(120, 45));
        noBtn->SetMinSize(wxSize(120, 45));
        if (icEnter.IsOk()) { yesBtn->SetBitmap(icEnter); yesBtn->SetBitmapPosition(wxLEFT); }
        yesBtn->SetDefault();
        btnRow->Add(yesBtn, 0, wxALL, 8);
        btnRow->Add(noBtn,  0, wxALL, 8);
    } else {
        wxButton* okBtn = new wxButton(&dlg, wxID_OK, "OK");
        okBtn->SetMinSize(wxSize(120, 45));
        if (icEnter.IsOk()) { okBtn->SetBitmap(icEnter); okBtn->SetBitmapPosition(wxLEFT); }
        okBtn->SetDefault();
        btnRow->Add(okBtn, 0, wxALL, 8);
    }

    vSizer->Add(btnRow, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    dlg.SetSizerAndFit(vSizer);
    dlg.Centre();

    return dlg.ShowModal() == wxID_OK;
}

} // namespace

MainMenuPanel::MainMenuPanel(wxWindow *parent)
    : wxPanel(parent)
{
    // Project root for image path
    wxFileName exeFile(wxStandardPaths::Get().GetExecutablePath());
    wxFileName projectRoot = exeFile;
    projectRoot.Normalize();
    projectRoot.SetFullName("");
    projectRoot.RemoveLastDir();
    projectRoot.RemoveLastDir();

    // --- Controls ---
    wxStaticText *baseFolderNameLabel = new wxStaticText(this, wxID_ANY, "Base Folder path:");
    m_baseFolderText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton *baseFolderNameBrowseBtn = new wxButton(this, ID_BROWSE_BASE, "Browse");

    wxStaticText *folder1NameLabel = new wxStaticText(this, wxID_ANY, "Folder 1 path:");
    m_folder1Text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton *folder1NameBrowseBtn = new wxButton(this, ID_BROWSE_1, "Browse");

    wxStaticText *folder2NameLabel = new wxStaticText(this, wxID_ANY, "Folder 2 path:");
    m_folder2Text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton *folder2NameBrowseBtn = new wxButton(this, ID_BROWSE_2, "Browse");
    // image test
    // wxString imagePath = projectRoot.GetFullPath() + "/assets/Nice_Nature.jpeg";

    wxButton *toSortPanelBtn = new wxButton(this, ID_TO_SORT_PANEL, "Start sorting!");

    // --- Layout setup ---
    wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);

    auto makeRow = [&](wxStaticText *label, wxTextCtrl *text, wxButton *button)
    {
        wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(label, 0, wxALL, 5);
        hbox->Add(text, 1, wxALL, 5);
        hbox->Add(button, 0, wxALL, 5);
        vbox->Add(hbox, 0, wxEXPAND | wxALL, 5);
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
    m_thumbnailGrid = new ThumbnailGrid(this, "Select a Base Folder to preview images.");
    vbox->Add(m_thumbnailGrid, 1, wxEXPAND | wxALL, 5);
    vbox->Add(toSortPanelBtn, 0, wxEXPAND | wxALL, 8);
    SetSizer(vbox);

    // --- Events ---
    baseFolderNameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    folder1NameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    folder2NameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    toSortPanelBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnStartSorting, this);
    m_baseFolderText->Bind(wxEVT_KILL_FOCUS, &MainMenuPanel::OnBaseFolderFocusLost, this);

    m_toast = new Toast(wxGetTopLevelParent(this));
}

void MainMenuPanel::OnBrowseFolder(wxCommandEvent &event)
{
    int id = event.GetId();

    // For folder 1 & 2, start at the parent of the base folder if one is set
    wxString startDir;
    if (id == ID_BROWSE_1 || id == ID_BROWSE_2) {
        wxString base = m_baseFolderText->GetValue().Trim(true).Trim(false);
        if (!base.IsEmpty()) {
            wxFileName fn(base);
            fn.Normalize();
            startDir = fn.GetPath(); // parent directory
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

void MainMenuPanel::OnBaseFolderFocusLost(wxFocusEvent& event)
{
    event.Skip(); // always skip focus events
    wxString path = m_baseFolderText->GetValue().Trim(true).Trim(false);
    RefreshPreview(path);
}

void MainMenuPanel::RefreshPreview(const wxString& folderPath)
{
    if (!m_thumbnailGrid) return;

    if (folderPath.IsEmpty() || !wxDir::Exists(folderPath)) {
        m_thumbnailGrid->SetImages({});
        Layout();
        return;
    }

    // Collect image files (same extensions as ImageRepository)
    const wxString exts[] = { "*.jpg", "*.jpeg", "*.png", "*.webp", "*.heic" };
    std::vector<wxString> files;
    wxDir dir(folderPath);
    if (dir.IsOpened()) {
        for (const auto& ext : exts) {
            wxString fn;
            bool has = dir.GetFirst(&fn, ext, wxDIR_FILES);
            while (has) {
                files.push_back(folderPath + wxFILE_SEP_PATH + fn);
                has = dir.GetNext(&fn);
            }
        }
    }
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        m_thumbnailGrid->SetImages({});
        Layout();
        return;
    }

    if (m_toast)
        m_toast->ShowMessage(wxString::Format("Loading %zu image(s)...", files.size()));

    m_thumbnailGrid->SetImages(files);
    Layout();

    if (m_toast)
        m_toast->Dismiss();
}

void MainMenuPanel::OnStartSorting(wxCommandEvent &event)
{
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

    // 3a) Conflict check: folder1 and folder2 must not be the same location
    const wxString& f1 = frame->folderLocations.folder1;
    const wxString& f2 = frame->folderLocations.folder2;
    if (!f1.IsEmpty() && !f2.IsEmpty() && f1 == f2) {
        ShowIconDialog(this,
            "Conflicting Folders",
            "Folder 1 and Folder 2 cannot be the same location.\nPlease choose different paths.");
        return;
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
        LOG_WARN("OnStartSorting: No supported images found in: %s", baseFolder);
        wxMessageBox(
            "No images were found in the selected Base Folder.",
            "No Images Found",
            wxOK | wxICON_INFORMATION,
            this);
        return;
    }

    // 5) All checks passed
    LOG_DEBUG("OnStartSorting: Validation successful. Switching to SortPhotosPanel.");
    frame->ShowSortPhotosPanel();
}


