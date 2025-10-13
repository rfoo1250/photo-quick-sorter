#include <wx/wx.h>

class PhotoQuickSorterApp : public wxApp {
public:
    bool OnInit() override;
};

class PhotoQuickSorterFrame : public wxFrame {
public:
    PhotoQuickSorterFrame(const wxString& title);
private:
    void OnSortPhotos(wxCommandEvent& event);
    wxDECLARE_EVENT_TABLE();

    void OnBrowseFolder(wxCommandEvent& event);
    wxTextCtrl* m_textArea;

};

enum {
    ID_SORT_BUTTON = wxID_HIGHEST + 1,
    ID_BROWSE
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

PhotoQuickSorterFrame::PhotoQuickSorterFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(500, 250)) 
{
    wxPanel* panel = new wxPanel(this);

    // --- Controls ---
    wxStaticText* baseFolderNameLabel = new wxStaticText(panel, wxID_ANY, "Folder path:");
    wxTextCtrl* baseFolderNameTextArea = new wxTextCtrl(
        panel, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1), wxTE_PROCESS_ENTER);
    wxButton* baseFolderNameBrowseBtn = new wxButton(panel, ID_BROWSE, "Browse");

    // --- Layout: horizontal box for text + button ---
    wxBoxSizer* hbox = new wxBoxSizer(wxHORIZONTAL);
    hbox->Add(baseFolderNameTextArea, 1, wxALL | wxALIGN_TOP, 5);
    hbox->Add(baseFolderNameBrowseBtn, 0, wxALL, 5);

    // --- Layout: vertical box for label + (text + button) ---
    wxBoxSizer* vbox = new wxBoxSizer(wxVERTICAL);
    vbox->Add(baseFolderNameLabel, 0, wxALL, 5);
    vbox->Add(hbox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    panel->SetSizer(vbox);

    // --- Event binding ---
    baseFolderNameBrowseBtn->Bind(wxEVT_BUTTON, &PhotoQuickSorterFrame::OnBrowseFolder, this);

    this->m_textArea = baseFolderNameTextArea;
}

// event handlers
void PhotoQuickSorterFrame::OnSortPhotos(wxCommandEvent& event) {
    wxMessageBox("Sorting photos (placeholder)!", "Info", wxOK | wxICON_INFORMATION);
}

void PhotoQuickSorterFrame::OnBrowseFolder(wxCommandEvent& event) {
    wxDirDialog dlg(this, "Select a base folder", "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        wxString selectedPath = dlg.GetPath();
        m_textArea->SetValue(selectedPath);
    }
}
