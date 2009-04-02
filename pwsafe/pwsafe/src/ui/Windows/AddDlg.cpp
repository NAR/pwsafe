/*
* Copyright (c) 2003-2009 Rony Shapiro <ronys@users.sourceforge.net>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/
/// \file AddDlg.cpp
//-----------------------------------------------------------------------------

#include "stdafx.h"
#include "PasswordSafe.h"

#include "ThisMfcApp.h"
#include "DboxMain.h"
#include "AddDlg.h"
#include "PwFont.h"
#include "ExpDTDlg.h"

#include "corelib/PWCharPool.h"
#include "corelib/PWSprefs.h"
#include "corelib/PWSAuxParse.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static TCHAR PSSWDCHAR = TCHAR('*');

CString CAddDlg::CS_SHOW;
CString CAddDlg::CS_HIDE;

//-----------------------------------------------------------------------------
CAddDlg::CAddDlg(CWnd* pParent)
  : CPWDialog(CAddDlg::IDD, pParent),
    m_password(_T("")), m_username(_T("")), m_title(_T("")),
    m_group(_T("")), m_URL(_T("")), m_autotype(_T("")), m_runcommand(_T("")),
    m_notes(_T("")),  m_notesww(_T("")),
    m_tttXTime(time_t(0)), m_tttCPMTime(time_t(0)), m_XTimeInt(0),
    m_isPwHidden(false), m_OverridePolicy(FALSE), m_bWordWrap(FALSE),
    m_last_TFC(0)
{
  m_pDbx = static_cast<DboxMain *>(pParent);
  m_isExpanded = PWSprefs::GetInstance()->
    GetPref(PWSprefs::DisplayExpandedAddEditDlg);
  m_SavePWHistory = PWSprefs::GetInstance()->
    GetPref(PWSprefs::SavePasswordHistory);
  m_MaxPWHistory = PWSprefs::GetInstance()->
    GetPref(PWSprefs::NumPWHistoryDefault);
  m_locXTime.LoadString(IDS_NEVER);

  if (CS_SHOW.IsEmpty()) {
#if defined(POCKET_PC)
    CS_SHOW.LoadString(IDS_SHOWPASSWORDTXT1);
    CS_HIDE.LoadString(IDS_HIDEPASSWORDTXT1);
#else
    CS_SHOW.LoadString(IDS_SHOWPASSWORDTXT2);
    CS_HIDE.LoadString(IDS_HIDEPASSWORDTXT2);
#endif
  }
  m_pwp.Empty();

  PWSprefs *prefs = PWSprefs::GetInstance();
  m_bWordWrap = prefs->GetPref(PWSprefs::NotesWordWrap) ? TRUE : FALSE;

  std::vector<st_context_menu> vmenu_items(1);

  st_context_menu st_cm;
  stringT cs_menu_string;

  LoadAString(cs_menu_string, IDS_WORD_WRAP);
  st_cm.menu_string = cs_menu_string;
  st_cm.message_number = WM_EDIT_WORDWRAP;
  st_cm.flags = m_bWordWrap == TRUE ? MF_CHECKED : MF_UNCHECKED;
  vmenu_items[0] = st_cm;

  m_pex_notes = new CEditExtn(vmenu_items);
  m_pex_notesww = new CEditExtn(vmenu_items);
}

CAddDlg::~CAddDlg()
{
  delete m_pex_notes;
  delete m_pex_notesww;
}

BOOL CAddDlg::OnInitDialog() 
{
  CPWDialog::OnInitDialog();

  ApplyPasswordFont(GetDlgItem(IDC_PASSWORD));
  ApplyPasswordFont(GetDlgItem(IDC_PASSWORD2));

  ((CEdit*)GetDlgItem(IDC_PASSWORD2))->SetPasswordChar(PSSWDCHAR);

  PWSprefs *prefs = PWSprefs::GetInstance();
  if (prefs->GetPref(PWSprefs::ShowPWDefault)) {
    ShowPassword();
  } else {
    HidePassword();
  }

  GetDlgItem(IDC_NOTES)->EnableWindow(m_bWordWrap == TRUE ? FALSE : TRUE);
  GetDlgItem(IDC_NOTESWW)->EnableWindow(m_bWordWrap == TRUE ? TRUE : FALSE);
  GetDlgItem(IDC_NOTES)->ShowWindow(m_bWordWrap == TRUE ? SW_HIDE : SW_SHOW);
  GetDlgItem(IDC_NOTESWW)->ShowWindow(m_bWordWrap == TRUE ? SW_SHOW : SW_HIDE);

  UpdateData(FALSE);

  ResizeDialog();

  CSpinButtonCtrl* pspin = (CSpinButtonCtrl *)GetDlgItem(IDC_PWHSPIN);

  pspin->SetBuddy(GetDlgItem(IDC_MAXPWHISTORY));
  pspin->SetRange(1, 255);
  pspin->SetBase(10);
  pspin->SetPos(m_MaxPWHistory);  // Default suggestion of max. to keep!

  // Populate the group combo box
  if (m_ex_group.GetCount() == 0) {
      std::vector<stringT> aryGroups;
      app.m_core.GetUniqueGroups(aryGroups);
      for (size_t igrp = 0; igrp < aryGroups.size(); igrp++) {
        m_ex_group.AddString(aryGroups[igrp].c_str());
      }
  }

  // Populate the Text Fields combobox
  m_txtFieldsList_combo.AddString(CString(MAKEINTRESOURCE(IDS_URL)));
  m_txtFieldsList_combo.AddString(CString(MAKEINTRESOURCE(IDS_AUTOTYPE)));
  m_txtFieldsList_combo.AddString(CString(MAKEINTRESOURCE(IDS_RUNCMND)));
  m_txtFieldsList_combo.SetCurSel(0);

  time(&m_tttCPMTime);

  m_ex_group.ChangeColour();
  return TRUE;
}

void CAddDlg::DoDataExchange(CDataExchange* pDX)
{
   CPWDialog::DoDataExchange(pDX);
   m_ex_password.DoDDX(pDX, m_password);
   m_ex_password2.DoDDX(pDX, m_password2);
   DDX_Text(pDX, IDC_NOTES, (CString&)m_notes);
   DDX_Text(pDX, IDC_NOTESWW, (CString&)m_notesww);
   DDX_Text(pDX, IDC_USERNAME, (CString&)m_username);
   DDX_Text(pDX, IDC_TITLE, (CString&)m_title);
   DDX_Text(pDX, IDC_XTIME, (CString&)m_locXTime);
   DDX_Check(pDX, IDC_SAVE_PWHIST, m_SavePWHistory);

   DDX_CBString(pDX, IDC_GROUP, (CString&)m_group);
   DDX_Control(pDX, IDC_MORE, m_moreLessBtn);
   DDX_Text(pDX, IDC_MAXPWHISTORY, m_MaxPWHistory);
   DDV_MinMaxInt(pDX, m_MaxPWHistory, 1, 255);

   DDX_Control(pDX, IDC_GROUP, m_ex_group);
   DDX_Control(pDX, IDC_PASSWORD, m_ex_password);
   DDX_Control(pDX, IDC_PASSWORD2, m_ex_password2);
   DDX_Control(pDX, IDC_NOTES, *m_pex_notes);
   DDX_Control(pDX, IDC_NOTESWW, *m_pex_notesww);
   DDX_Control(pDX, IDC_USERNAME, m_ex_username);
   DDX_Control(pDX, IDC_TITLE, m_ex_title);

   GetDlgItem(IDC_MAXPWHISTORY)->EnableWindow(m_SavePWHistory);
   DDX_Check(pDX, IDC_OVERRIDE_POLICY, m_OverridePolicy);
   DDX_Control(pDX, IDC_TXT_FLDS_COMBO, m_txtFieldsList_combo);
   DDX_Control(pDX, IDC_TXT_FLD, m_TextFieldEdit);
   // If we're moving data from controls to members, set
   // member corresponding to current combobox
   if (pDX->m_bSaveAndValidate == TRUE) {
     OnCbnSelchangeTxtFldsCombo(); // does a bit extra work, but who cares...
   }
}

BEGIN_MESSAGE_MAP(CAddDlg, CPWDialog)
  ON_BN_CLICKED(ID_HELP, OnHelp)
  ON_BN_CLICKED(IDC_SHOWPASSWORD, OnShowpassword)
  ON_BN_CLICKED(IDC_RANDOM, OnRandom)
  ON_BN_CLICKED(IDC_MORE, OnBnClickedMore)
  ON_BN_CLICKED(IDOK, OnBnClickedOk)
  ON_BN_CLICKED(IDC_XTIME_CLEAR, OnBnClickedClearXTime)
  ON_BN_CLICKED(IDC_XTIME_SET, OnBnClickedSetXTime)
  ON_BN_CLICKED(IDC_SAVE_PWHIST, OnCheckedSavePasswordHistory)
  ON_BN_CLICKED(IDC_OVERRIDE_POLICY, OnBnClickedOverridePolicy)
  ON_MESSAGE(WM_EDIT_WORDWRAP, OnWordWrap)
  ON_CBN_SELCHANGE(IDC_TXT_FLDS_COMBO, &CAddDlg::OnCbnSelchangeTxtFldsCombo)
END_MESSAGE_MAP()

void CAddDlg::OnCancel() 
{
  if (AfxMessageBox(IDS_AREYOUSURE, 
                         MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2) == IDYES)
  CPWDialog::OnCancel();
}

void CAddDlg::OnShowpassword() 
{
  UpdateData(TRUE);

  if (m_isPwHidden)
    ShowPassword();
  else
    HidePassword();

  UpdateData(FALSE);
}

void CAddDlg::ShowPassword()
{
  m_isPwHidden = false;
  GetDlgItem(IDC_SHOWPASSWORD)->SetWindowText(CS_HIDE);

  m_ex_password.SetSecure(false);
  // Remove password character so that the password is displayed
  m_ex_password.SetPasswordChar(0);
  m_ex_password.Invalidate();

  // Don't need verification as the user can see the password entered
  m_ex_password2.EnableWindow(FALSE);
  m_ex_password2.Invalidate();
  m_password2.Empty();
}

void CAddDlg::HidePassword()
{
  m_isPwHidden = true;
  GetDlgItem(IDC_SHOWPASSWORD)->SetWindowText(CS_SHOW);
  m_ex_password.SetSecure(true);
  // Set password character so that the password is not displayed
  m_ex_password.SetPasswordChar(PSSWDCHAR);
  m_ex_password.Invalidate();
  // Need verification as the user can not see the password entered
  m_ex_password2.EnableWindow(TRUE);
  m_password2 = m_password;
  m_ex_password2.SetSecureText(m_password2);
  m_ex_password2.Invalidate();
}

void CAddDlg::OnOK() 
{
  if (UpdateData(TRUE) == FALSE)
    return;

  m_group.EmptyIfOnlyWhiteSpace();
  m_title.EmptyIfOnlyWhiteSpace();
  m_username.EmptyIfOnlyWhiteSpace();
  if (m_password.IsOnlyWhiteSpace()) {
    m_password.Empty();
    if (m_isPwHidden)
      m_password2.Empty();
  }
  if (m_bWordWrap == TRUE)
    m_notes = m_notesww;
  m_notes.EmptyIfOnlyWhiteSpace();
  m_URL.EmptyIfOnlyWhiteSpace();
  m_autotype.EmptyIfOnlyWhiteSpace();

  UpdateData(FALSE);

  //Check that data is valid
  if (m_title.IsEmpty()) {
    AfxMessageBox(IDS_MUSTHAVETITLE);
    ((CEdit*)GetDlgItem(IDC_TITLE))->SetFocus();
    return;
  }

  if (m_password.IsEmpty()) {
    AfxMessageBox(IDS_MUSTHAVEPASSWORD);
    ((CEdit*)GetDlgItem(IDC_PASSWORD))->SetFocus();
    return;
  }

  if (!m_group.IsEmpty() && m_group[0] == '.') {
    AfxMessageBox(IDS_DOTINVALID);
    ((CEdit*)GetDlgItem(IDC_GROUP))->SetFocus();
    return;
  }

  if (m_isPwHidden && (m_password.Compare(m_password2) != 0)) {
    AfxMessageBox(IDS_PASSWORDSNOTMATCH);
    UpdateData(FALSE);
    ((CEdit*)GetDlgItem(IDC_PASSWORD))->SetFocus();
    return;
  }

  // If there is a matching entry in our list, tell the user to try again.
  if (m_pDbx->Find(m_group, m_title, m_username) != m_pDbx->End()) {
    CSecString temp;
    if (m_group.IsEmpty())
      temp.Format(IDS_ENTRYEXISTS2, m_title, m_username);
    else
      temp.Format(IDS_ENTRYEXISTS, m_group, m_title, m_username);
    AfxMessageBox(temp);
    ((CEdit*)GetDlgItem(IDC_TITLE))->SetSel(MAKEWORD(-1, 0));
    ((CEdit*)GetDlgItem(IDC_TITLE))->SetFocus();
    return;
  }

  bool brc, b_msg_issued;
  brc = m_pDbx->CheckNewPassword(m_group, m_title, m_username, m_password,
                              false, CItemData::ET_ALIAS,
                              m_base_uuid, m_ibasedata, b_msg_issued);

  if (!brc && m_ibasedata != 0) {
    if (!b_msg_issued)
      AfxMessageBox(IDS_MUSTHAVETARGET, MB_OK);
    UpdateData(FALSE);
    ((CEdit*)GetDlgItem(IDC_PASSWORD))->SetFocus();
    return;
  }

  if (m_runcommand.GetLength() > 0) {
    //Check Run Command parses - don't substitute
    stringT errmsg;
    size_t st_column;
    bool bAutoType(false);
    StringX sxAutotype(_T(""));
    PWSAuxParse::GetExpandedString(m_runcommand, _T(""), NULL, 
       bAutoType, sxAutotype, errmsg, st_column);
    if (errmsg.length() > 0) {
      CString cs_title(MAKEINTRESOURCE(IDS_RUNCOMMAND_ERROR));
      CString cs_temp(MAKEINTRESOURCE(IDS_RUN_IGNOREORFIX));
      CString cs_errmsg;
      cs_errmsg.Format(IDS_RUN_ERRORMSG, (int)st_column, errmsg.c_str());
      cs_errmsg += cs_temp;
      int rc = MessageBox(cs_errmsg, cs_title, 
                          MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
      if (rc == IDNO) {
        UpdateData(FALSE);
        ((CEdit*)GetDlgItem(IDC_RUNCMD))->SetFocus();
        return;
      }
    }
  }
  //End check

  CPWDialog::OnOK();
}

void CAddDlg::OnHelp() 
{
#if defined(POCKET_PC)
  CreateProcess( _T("PegHelp.exe"), _T("pws_ce_help.html#adddata"), NULL, NULL, FALSE, 0, NULL, NULL, NULL, NULL );
#else
  CString cs_HelpTopic;
  cs_HelpTopic = app.GetHelpFileName() + _T("::/html/entering_pwd.html");
  HtmlHelp(DWORD_PTR((LPCTSTR)cs_HelpTopic), HH_DISPLAY_TOPIC);
#endif
}

void CAddDlg::OnRandom() 
{
  DboxMain* pParent = static_cast<DboxMain*>(GetParent());

  UpdateData(TRUE);
  StringX passwd;
  pParent->MakeRandomPassword(passwd, m_pwp);
  m_password = passwd.c_str();
  if (m_isPwHidden) {
    m_password2 = m_password;
  }
  UpdateData(FALSE);
}


//-----------------------------------------------------------------------------

void CAddDlg::OnBnClickedMore()
{
  m_isExpanded = !m_isExpanded;
  PWSprefs::GetInstance()->
    SetPref(PWSprefs::DisplayExpandedAddEditDlg, m_isExpanded);
  ResizeDialog();
}

void CAddDlg::OnBnClickedOk()
{
  OnOK();
}

void CAddDlg::ResizeDialog()
{
  int TopHideableControl = IDC_TOP_HIDEABLE;
  int BottomHideableControl = IDC_BOTTOM_HIDEABLE;
  int controls[] = {
    IDC_TXT_FLDS_COMBO,
    IDC_TXT_FLD,
    IDC_SAVE_PWHIST,
    IDC_XTIME,
    IDC_XTIME_RECUR,
    IDC_STATIC_XTIME,
    IDC_XTIME_CLEAR,
    IDC_XTIME_SET,
    IDC_STATIC_DTEXPGROUP,
    IDC_MAXPWHISTORY,
    IDC_STATIC_OLDPW1,
    IDC_PWHSPIN,
  };

  for(unsigned n = 0; n < sizeof(controls)/sizeof(controls[0]); n++) {
    CWnd* pWind = (CWnd *)GetDlgItem(controls[n]);
    pWind->ShowWindow(m_isExpanded);
  }

  RECT curDialogRect;

  this->GetWindowRect(&curDialogRect);

  RECT newDialogRect=curDialogRect;

  RECT curLowestCtlRect;
  CWnd* pLowestCtl;
  int newHeight;
  CString cs_text;
  if (m_isExpanded) {
    // from less to more
    pLowestCtl = (CWnd *)GetDlgItem(BottomHideableControl);

    pLowestCtl->GetWindowRect(&curLowestCtlRect);

    newHeight =  curLowestCtlRect.bottom + 15 - newDialogRect.top;
    cs_text.LoadString(IDS_LESS);
    m_moreLessBtn.SetWindowText(cs_text);
  } else {
    // from more to less
    pLowestCtl = (CWnd *)GetDlgItem(TopHideableControl);
    pLowestCtl->GetWindowRect(&curLowestCtlRect);

    newHeight =  curLowestCtlRect.top + 5 - newDialogRect.top;

    cs_text.LoadString(IDS_MORE);
    m_moreLessBtn.SetWindowText(cs_text);
  }

  this->SetWindowPos(NULL, 0, 0,
                     newDialogRect.right - newDialogRect.left,
                     newHeight, SWP_NOMOVE );
}

void CAddDlg::OnBnClickedClearXTime()
{
  m_locXTime.LoadString(IDS_NEVER);
  GetDlgItem(IDC_XTIME)->SetWindowText((CString)m_locXTime);
  GetDlgItem(IDC_XTIME_RECUR)->SetWindowText(_T(""));
  m_tttXTime = (time_t)0;
  m_XTimeInt = 0;
}

void CAddDlg::OnBnClickedSetXTime()
{
  CExpDTDlg dlg_expDT(m_tttCPMTime,
                      m_tttXTime,
                      m_XTimeInt,
                      this);

  app.DisableAccelerator();
  INT_PTR rc = dlg_expDT.DoModal();
  app.EnableAccelerator();

  if (rc == IDOK) {
    CString cs_text;
    m_locXTime = dlg_expDT.m_locXTime;
    m_tttXTime = dlg_expDT.m_tttXTime;
    m_XTimeInt = dlg_expDT.m_XTimeInt;
    if (m_XTimeInt != 0) // recurring expiration
      cs_text.Format(IDS_IN_N_DAYS, m_XTimeInt);
    GetDlgItem(IDC_XTIME)->SetWindowText(m_locXTime);
    GetDlgItem(IDC_XTIME_RECUR)->SetWindowText(cs_text);
  } // rc == IDOK
}

void CAddDlg::OnCheckedSavePasswordHistory()
{
  m_SavePWHistory = ((CButton*)GetDlgItem(IDC_SAVE_PWHIST))->GetCheck();

  GetDlgItem(IDC_MAXPWHISTORY)->EnableWindow(m_SavePWHistory);
}

void CAddDlg::OnBnClickedOverridePolicy()
{
  UpdateData(TRUE);
  if (m_OverridePolicy == TRUE) {
    DboxMain* pParent = static_cast<DboxMain*>(GetParent());
    pParent->SetPasswordPolicy(m_pwp);
  } else
    m_pwp.Empty();
}

void CAddDlg::SelectAllNotes()
{
  // Here from PreTranslateMessage iff User pressed Ctrl+A
  // in Notes control
  ((CEdit *)GetDlgItem(m_bWordWrap == TRUE ? IDC_NOTESWW : IDC_NOTES))->
            SetSel(0, -1, TRUE);
}

LRESULT CAddDlg::OnWordWrap(WPARAM, LPARAM)
{
  m_bWordWrap = m_bWordWrap == TRUE ? FALSE : TRUE;
  // Get value of notes from dialog.
  UpdateData(TRUE);
  if (m_bWordWrap == FALSE)
    m_notes = m_notesww;
  else
    m_notesww = m_notes;
  // Update dalog
  UpdateData(FALSE);

  GetDlgItem(IDC_NOTES)->EnableWindow(m_bWordWrap == TRUE ? FALSE : TRUE);
  GetDlgItem(IDC_NOTESWW)->EnableWindow(m_bWordWrap == TRUE ? TRUE : FALSE);
  GetDlgItem(IDC_NOTES)->ShowWindow(m_bWordWrap == TRUE ? SW_HIDE : SW_SHOW);
  GetDlgItem(IDC_NOTESWW)->ShowWindow(m_bWordWrap == TRUE ? SW_SHOW : SW_HIDE);

  ((CEdit*)GetDlgItem(IDC_NOTES))->Invalidate();
  ((CEdit*)GetDlgItem(IDC_NOTESWW))->Invalidate();

  m_pex_notes->UpdateState(WM_EDIT_WORDWRAP, m_bWordWrap);
  m_pex_notesww->UpdateState(WM_EDIT_WORDWRAP, m_bWordWrap);
  return 0;
}

BOOL CAddDlg::PreTranslateMessage(MSG* pMsg)
{
  // if user hit Ctrl+A in Notes control, then SelectAllNotes
  if (pMsg->message == WM_KEYDOWN && pMsg->wParam == 'A' &&
      (GetKeyState(VK_CONTROL) & 0x8000) &&
      GetDlgItem(IDC_NOTES)->m_hWnd == ::GetFocus()) {
    SelectAllNotes();
    return TRUE;
  }
  return CPWDialog::PreTranslateMessage(pMsg);
}

void CAddDlg::OnCbnSelchangeTxtFldsCombo()
{
  // get previous, possibly changed, text where it belongs
  CString oldText;
  m_TextFieldEdit.GetWindowText(oldText);
  switch (m_last_TFC) {
  case 0: // URL
    m_URL = LPCTSTR(oldText);
    break;
  case 1: // AutoType
    m_autotype = LPCTSTR(oldText);
    break;
  case 2: // RunCmd
    m_runcommand = LPCTSTR(oldText);
    break;
  default:
    ASSERT(0);
  }
  // now update text field to reflect new combobox value
  m_last_TFC = m_txtFieldsList_combo.GetCurSel();
  switch (m_last_TFC) {
  case 0: // URL
    m_TextFieldEdit.SetWindowText(m_URL);
    break;
  case 1: // AutoType
    m_TextFieldEdit.SetWindowText(m_autotype);
    break;
  case 2: // RunCmd
    m_TextFieldEdit.SetWindowText(m_runcommand);
    break;
  default:
    ASSERT(0);
  }
}
