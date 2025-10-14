#pragma once
#include <wx/wx.h>

class SortPhotosPanel : public wxPanel {
public:
    explicit SortPhotosPanel(wxWindow* parent);
    void RefreshData();
private:    
    wxStaticText* m_infoLabel = nullptr; // tobe changed
};