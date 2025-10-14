#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

// class declr
class PhotoQuickSorterApp : public wxApp {
public:
    bool OnInit() override;
};

class PhotoQuickSorterFrame : public wxFrame {
public:
    PhotoQuickSorterFrame(const wxString& title);
private:
    void ShowMainMenuPanel();
    void ShowSortPhotosPanel();

    void OnSortPhotos(wxCommandEvent& event);
    void OnBrowseFolder(wxCommandEvent& event);
    wxDECLARE_EVENT_TABLE();
    
    wxTextCtrl* m_baseFolderText;
    wxTextCtrl* m_folder1Text;
    wxTextCtrl* m_folder2Text;
    wxPanel* m_mainMenuPanel;
    wxPanel* m_sortPhotosPanel;


};

enum {
    ID_SORT_BUTTON = wxID_HIGHEST + 1,
    ID_BROWSE_BASE,
    ID_BROWSE_1,
    ID_BROWSE_2,
    ID_TO_SORT_PANEL
};

wxBEGIN_EVENT_TABLE(PhotoQuickSorterFrame, wxFrame)
    EVT_BUTTON(ID_SORT_BUTTON, PhotoQuickSorterFrame::OnSortPhotos)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(PhotoQuickSorterApp);

bool PhotoQuickSorterApp::OnInit() {
    PhotoQuickSorterFrame* frame = new PhotoQuickSorterFrame("Photo Quick Sorter");
    frame->Show(true);
    return true;
}

// constructor
PhotoQuickSorterFrame::PhotoQuickSorterFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(500, 500)) 
{
    // project root and exe root def
    wxFileName exeFile(wxStandardPaths::Get().GetExecutablePath());
    wxFileName projectRoot = exeFile;
    projectRoot.Normalize();          // resolves to full absolute path
    projectRoot.SetFullName("");      // removes the "PhotoQuickSorter.exe" part
    projectRoot.RemoveLastDir();  // removes "Debug"
    projectRoot.RemoveLastDir();  // removes "build"

    // panels - containers
    m_mainMenuPanel = new wxPanel(this);
    m_sortPhotosPanel = new wxPanel(this);

    // --- Controls ---
    wxStaticText* baseFolderNameLabel = new wxStaticText(m_mainMenuPanel, wxID_ANY, "Base Folder path:");
    wxTextCtrl* baseFolderNameTextArea = new wxTextCtrl(
        m_mainMenuPanel, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1), wxTE_PROCESS_ENTER);
    wxButton* baseFolderNameBrowseBtn = new wxButton(m_mainMenuPanel, ID_BROWSE_BASE, "Browse");
    wxStaticText* folder1NameLabel = new wxStaticText(m_mainMenuPanel, wxID_ANY, "Folder 1 path:");
    wxTextCtrl* folder1NameTextArea = new wxTextCtrl(
        m_mainMenuPanel, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1), wxTE_PROCESS_ENTER);
    wxButton* folder1NameBrowseBtn = new wxButton(m_mainMenuPanel, ID_BROWSE_1, "Browse");
    wxStaticText* folder2NameLabel = new wxStaticText(m_mainMenuPanel, wxID_ANY, "Folder 2 path:");
    wxTextCtrl* folder2NameTextArea = new wxTextCtrl(
        m_mainMenuPanel, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1), wxTE_PROCESS_ENTER);
    wxButton* folder2NameBrowseBtn = new wxButton(m_mainMenuPanel, ID_BROWSE_2, "Browse");
    // instr image
    wxImage::AddHandler(new wxJPEGHandler());
    // future proof: use wxInitAllImageHandlers(); in OnInit()
    wxString imagePath = projectRoot.GetFullPath() + "/assets/Nice_Nature.jpeg";
    // wxLogMessage("Image path: %s", imagePath);
    wxButton* toSortPanelBtn = new wxButton(m_mainMenuPanel, ID_TO_SORT_PANEL, "Start sorting!");
    


    // --- Layout: horizontal box for text + button ---
    wxBoxSizer* hbox0 = new wxBoxSizer(wxHORIZONTAL);
    hbox0->Add(baseFolderNameLabel, 0, wxALL, 5);
    hbox0->Add(baseFolderNameTextArea, 1, wxALL, 5);
    hbox0->Add(baseFolderNameBrowseBtn, 0, wxALL, 5);
    wxBoxSizer* hbox1 = new wxBoxSizer(wxHORIZONTAL);
    hbox1->Add(folder1NameLabel, 0, wxALL, 5);
    hbox1->Add(folder1NameTextArea, 1, wxALL, 5);
    hbox1->Add(folder1NameBrowseBtn, 0, wxALL, 5);
    wxBoxSizer* hbox2 = new wxBoxSizer(wxHORIZONTAL);
    hbox2->Add(folder2NameLabel, 0, wxALL, 5);
    hbox2->Add(folder2NameTextArea, 1, wxALL, 5);
    hbox2->Add(folder2NameBrowseBtn, 0, wxALL, 5);

    // --- Layout: vertical box for all folder widgets ---
    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
    vbox->Add(hbox0, 0, wxEXPAND | wxALL, 5);
    vbox->Add(hbox1, 0, wxEXPAND | wxALL, 5);
    vbox->Add(hbox2, 0, wxEXPAND | wxALL, 5);
    if (wxFileExists(imagePath)) {
        wxImage logoImage(imagePath, wxBITMAP_TYPE_JPEG);

        int maxHeight = 200;

        int originalWidth = logoImage.GetWidth();
        int originalHeight = logoImage.GetHeight();
        if (originalHeight > maxHeight) {
            double scale = (double)maxHeight / (double)originalHeight;
            int newWidth = (int)(originalWidth * scale);
            logoImage = logoImage.Scale(newWidth, maxHeight, wxIMAGE_QUALITY_HIGH);
        }

        wxBitmap logoBitmap(logoImage);
        wxStaticBitmap* logo = new wxStaticBitmap(m_mainMenuPanel, wxID_ANY, logoBitmap);
        vbox->Add(logo, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    } else {
        wxStaticText* noImageText = new wxStaticText(m_mainMenuPanel, wxID_ANY, "Image not found");
        vbox->Add(noImageText, 0, wxALIGN_CENTER | wxBOTTOM, 10);
    }
    vbox->Add(toSortPanelBtn, 1, wxEXPAND | wxALL, 5);

    m_mainMenuPanel->SetSizer(vbox);
    // top level sizer for frame
    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_mainMenuPanel, 1, wxEXPAND);
    frameSizer->Add(m_sortPhotosPanel, 1, wxEXPAND);

    // hide all other panels initially
    m_sortPhotosPanel->Hide();

    SetSizer(frameSizer);
    Layout();

    // tempo
    wxStaticText* sortLabel = new wxStaticText(m_sortPhotosPanel, wxID_ANY, "Sorting Screen", wxDefaultPosition, wxDefaultSize);
    wxBoxSizer* sortSizer = new wxBoxSizer(wxVERTICAL);
    sortSizer->Add(sortLabel, 1, wxALIGN_CENTER | wxALL, 10);
    m_sortPhotosPanel->SetSizer(sortSizer);




    // --- Event binding ---
    toSortPanelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        ShowSortPhotosPanel();
    });


    baseFolderNameBrowseBtn->Bind(wxEVT_BUTTON, &PhotoQuickSorterFrame::OnBrowseFolder, this);
    folder1NameBrowseBtn->Bind(wxEVT_BUTTON, &PhotoQuickSorterFrame::OnBrowseFolder, this);
    folder2NameBrowseBtn->Bind(wxEVT_BUTTON, &PhotoQuickSorterFrame::OnBrowseFolder, this);

    this->m_baseFolderText = baseFolderNameTextArea;
    this->m_folder1Text = folder1NameTextArea;
    this->m_folder2Text = folder2NameTextArea;

}

// event handlers
void PhotoQuickSorterFrame::OnSortPhotos(wxCommandEvent& event) {
    wxMessageBox("Sorting photos (placeholder)!", "Info", wxOK | wxICON_INFORMATION);
}

void PhotoQuickSorterFrame::OnBrowseFolder(wxCommandEvent& event) {
    wxDirDialog dlg(this, "Select a folder", "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        wxString selectedPath = dlg.GetPath();
        int id = event.GetId();

        if (id == ID_BROWSE_BASE) {
            m_baseFolderText->SetValue(selectedPath);
        } else if (id == ID_BROWSE_1) {
            m_folder1Text->SetValue(selectedPath);
        } else if (id == ID_BROWSE_2) {
            m_folder2Text->SetValue(selectedPath);
        }
    }
}

// panel switches
void PhotoQuickSorterFrame::ShowMainMenuPanel() {
    m_sortPhotosPanel->Hide();
    m_mainMenuPanel->Show();
    Layout();
}

void PhotoQuickSorterFrame::ShowSortPhotosPanel() {
    m_mainMenuPanel->Hide();
    m_sortPhotosPanel->Show();
    Layout();
}