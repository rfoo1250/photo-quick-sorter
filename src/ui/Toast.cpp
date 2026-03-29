#include "ui/Toast.h"
#include <wx/utils.h>

static const int TOAST_MARGIN_BOTTOM = 24;
static const int TOAST_PADDING       = 14;
static const int ANIM_STEPS          = 12;
static const int ANIM_STEP_MS        = 8;

Toast::Toast(wxWindow* parent)
    : wxFrame(parent, wxID_ANY, wxEmptyString,
              wxDefaultPosition, wxDefaultSize,
              wxNO_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxFRAME_FLOAT_ON_PARENT)
{
    SetBackgroundColour(wxColour(45, 45, 45));

    m_label = new wxStaticText(this, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_label->SetForegroundColour(*wxWHITE);

    wxFont f = m_label->GetFont();
    f.SetPointSize(f.GetPointSize() + 1);
    m_label->SetFont(f);

    wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
    s->Add(m_label, 1, wxALIGN_CENTER | wxALL, TOAST_PADDING);
    SetSizerAndFit(s);
}

void Toast::Animate(int fromY, int toY, bool hideAfter)
{
    const int x = GetPosition().x;
    for (int i = 1; i <= ANIM_STEPS; ++i) {
        int y = fromY + (int)((toY - fromY) * (float)i / ANIM_STEPS);
        Move(x, y);
        Refresh();
        Update();
        wxMilliSleep(ANIM_STEP_MS);
        wxSafeYield(nullptr, true); // paints UI, blocks user input during animation
    }
    if (hideAfter)
        wxFrame::Show(false);
}

void Toast::ShowMessage(const wxString& message)
{
    if (IsShown())
        wxFrame::Show(false);

    m_label->SetLabel(message);
    Fit(); // resize to fit new text

    wxRect pr  = GetParent()->GetScreenRect();
    wxSize ts  = GetSize();
    int x      = pr.x + (pr.width  - ts.x) / 2;
    int startY = pr.y + pr.height;                          // below parent, off-screen
    int endY   = pr.y + pr.height - ts.y - TOAST_MARGIN_BOTTOM;

    Move(x, startY);
    wxFrame::Show(true);
    Raise();

    Animate(startY, endY, false);
}

void Toast::Dismiss()
{
    if (!IsShown()) return;

    wxRect pr  = GetParent()->GetScreenRect();
    int startY = GetPosition().y;
    int endY   = pr.y + pr.height + 10; // back below parent

    Animate(startY, endY, true);
}
