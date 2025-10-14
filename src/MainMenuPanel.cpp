#include "MainMenuPanel.h"
#include "PhotoQuickSorterFrame.h"
#include "../FolderLocations.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>

MainMenuPanel::MainMenuPanel(wxWindow* parent)
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
    wxStaticText* baseFolderNameLabel = new wxStaticText(this, wxID_ANY, "Base Folder path:");
    m_baseFolderText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton* baseFolderNameBrowseBtn = new wxButton(this, ID_BROWSE_BASE, "Browse");

    wxStaticText* folder1NameLabel = new wxStaticText(this, wxID_ANY, "Folder 1 path:");
    m_folder1Text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton* folder1NameBrowseBtn = new wxButton(this, ID_BROWSE_1, "Browse");

    wxStaticText* folder2NameLabel = new wxStaticText(this, wxID_ANY, "Folder 2 path:");
    m_folder2Text = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    wxButton* folder2NameBrowseBtn = new wxButton(this, ID_BROWSE_2, "Browse");

    wxString imagePath = projectRoot.GetFullPath() + "/assets/Nice_Nature.jpeg";
    wxButton* toSortPanelBtn = new wxButton(this, ID_TO_SORT_PANEL, "Start sorting!");

    // --- Layout setup ---
    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);

    auto makeRow = [&](wxStaticText* label, wxTextCtrl* text, wxButton* button) {
        wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(label, 0, wxALL, 5);
        hbox->Add(text, 1, wxALL, 5);
        hbox->Add(button, 0, wxALL, 5);
        vbox->Add(hbox, 0, wxEXPAND | wxALL, 5);
    };

    makeRow(baseFolderNameLabel, m_baseFolderText, baseFolderNameBrowseBtn);
    makeRow(folder1NameLabel, m_folder1Text, folder1NameBrowseBtn);
    makeRow(folder2NameLabel, m_folder2Text, folder2NameBrowseBtn);

    // Image
    if (wxFileExists(imagePath)) {
        wxImage::AddHandler(new wxJPEGHandler()); // always forgot this
        wxImage logoImage(imagePath, wxBITMAP_TYPE_JPEG);
        int maxHeight = 200;
        if (logoImage.GetHeight() > maxHeight) {
            double scale = (double)maxHeight / logoImage.GetHeight();
            logoImage = logoImage.Scale(logoImage.GetWidth() * scale, maxHeight, wxIMAGE_QUALITY_HIGH);
        }
        wxStaticBitmap* logo = new wxStaticBitmap(this, wxID_ANY, wxBitmap(logoImage));
        vbox->Add(logo, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    } else {
        vbox->Add(new wxStaticText(this, wxID_ANY, "Image not found"), 0, wxALIGN_CENTER | wxBOTTOM, 10);
    }

    vbox->Add(toSortPanelBtn, 0, wxEXPAND | wxALL, 5);
    SetSizer(vbox);

    // --- Events ---
    baseFolderNameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    folder1NameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    folder2NameBrowseBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnBrowseFolder, this);
    toSortPanelBtn->Bind(wxEVT_BUTTON, &MainMenuPanel::OnStartSorting, this);
}

void MainMenuPanel::OnBrowseFolder(wxCommandEvent& event) {
    wxDirDialog dlg(this, "Select a folder", "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        wxString path = dlg.GetPath();
        int id = event.GetId();

        auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent());
        if (!frame) return;

        if (id == ID_BROWSE_BASE) {
            m_baseFolderText->SetValue(path);
            frame->folderLocations.baseFolder = path;
        } else if (id == ID_BROWSE_1) {
            m_folder1Text->SetValue(path);
            frame->folderLocations.folder1 = path;
        } else if (id == ID_BROWSE_2) {
            m_folder2Text->SetValue(path);
            frame->folderLocations.folder2 = path;
        }
    }
}

void MainMenuPanel::OnStartSorting(wxCommandEvent& event) {
    if (auto* frame = dynamic_cast<PhotoQuickSorterFrame*>(GetParent()))
        frame->ShowSortPhotosPanel();
}
