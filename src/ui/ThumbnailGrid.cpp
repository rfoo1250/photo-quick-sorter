#include "ui/ThumbnailGrid.h"
#include <wx/filename.h>
#include <wx/filefn.h>
#include <algorithm>

static const int THUMB_PADDING = 12;

ThumbnailGrid::ThumbnailGrid(wxWindow* parent, const wxString& emptyHint)
    : wxScrolledWindow(parent, wxID_ANY,
                       wxDefaultPosition, wxDefaultSize, wxVSCROLL)
    , m_emptyHint(emptyHint)
{
    SetScrollRate(0, 20);
    Bind(wxEVT_SIZE, &ThumbnailGrid::OnSize, this);
    Rebuild(); // show initial hint
}

void ThumbnailGrid::SetImages(const std::vector<wxString>& paths)
{
    m_paths = paths;
    Rebuild();
}

void ThumbnailGrid::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (!m_paths.empty())
        Rebuild();
}

void ThumbnailGrid::Rebuild()
{
    Freeze();
    DestroyChildren();

    if (m_paths.empty()) {
        wxStaticText* lbl = new wxStaticText(this, wxID_ANY, m_emptyHint,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxALIGN_CENTER);
        wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
        s->Add(lbl, 0, wxALIGN_CENTER | wxALL, 20);
        SetSizer(s);
        FitInside();
        Thaw();
        return;
    }

    // Fill 3 columns across ~92% of the visible width
    int scrollW = GetClientSize().x;
    if (scrollW < 60) scrollW = 400; // fallback before first layout
    int thumbSize = (int)((scrollW * 0.92 - THUMB_PADDING * 4) / 3);
    if (thumbSize < 60) thumbSize = 60;

    wxFlexGridSizer* grid = new wxFlexGridSizer(0, 3, THUMB_PADDING, THUMB_PADDING);

    for (const wxString& path : m_paths) {
        wxImage img;
        if (wxFileExists(path))
            img.LoadFile(path, wxBITMAP_TYPE_ANY);

        wxBitmap bmp;
        if (img.IsOk()) {
            double sx = (double)thumbSize / img.GetWidth();
            double sy = (double)thumbSize / img.GetHeight();
            double s  = std::min(sx, sy);
            img = img.Scale((int)(img.GetWidth() * s),
                            (int)(img.GetHeight() * s),
                            wxIMAGE_QUALITY_NORMAL);
            bmp = wxBitmap(img);
        }

        wxStaticBitmap* thumb = new wxStaticBitmap(this, wxID_ANY, bmp);
        thumb->SetMinSize(wxSize(thumbSize, thumbSize));

        wxStaticText* lbl = new wxStaticText(this, wxID_ANY,
            wxFileName(path).GetFullName(),
            wxDefaultPosition, wxSize(thumbSize, -1),
            wxALIGN_CENTER | wxST_ELLIPSIZE_END);

        wxBoxSizer* cell = new wxBoxSizer(wxVERTICAL);
        cell->Add(thumb, 0, wxALIGN_CENTER | wxBOTTOM, 3);
        cell->Add(lbl,   0, wxALIGN_CENTER);

        grid->Add(cell, 0, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
    }

    SetSizer(grid);
    FitInside();
    Thaw();
}