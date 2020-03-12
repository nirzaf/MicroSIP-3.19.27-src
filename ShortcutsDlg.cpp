/* 
 * Copyright (C) 2011-2020 MicroSIP (http://www.microsip.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include "StdAfx.h"
#include "ShortcutsDlg.h"
#include "mainDlg.h"
#include "settings.h"
#include "langpack.h"

static CString defaultActionItems[] = {
	MSIP_SHORTCUT_CALL,
#ifdef _GLOBAL_VIDEO
	MSIP_SHORTCUT_VIDEOCALL,
#endif
	MSIP_SHORTCUT_MESSAGE,
	MSIP_SHORTCUT_DTMF,
	MSIP_SHORTCUT_TRANSFER,
	MSIP_SHORTCUT_CONFERENCE,
	MSIP_SHORTCUT_RUNBATCH,
	MSIP_SHORTCUT_CALL_URL,
	MSIP_SHORTCUT_POP_URL,
};
static CString defaultActionValues[] = {
	_T("Call"),
#ifdef _GLOBAL_VIDEO
	_T("Video Call"),
#endif
	_T("Message"),
	_T("DTMF"),
	_T("Call Transfer"),
	_T("Conference"),
	_T("Run Batch"),
	_T("Call URL"),
	_T("Pop URL"),
};

ShortcutsDlg::ShortcutsDlg(CWnd* pParent /*=NULL*/)
: CDialog(ShortcutsDlg::IDD, pParent)
{
	Create (IDD, pParent);
}

ShortcutsDlg::~ShortcutsDlg(void)
{
}

int ShortcutsDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (langPack.rtl) {
		ModifyStyleEx(0,WS_EX_LAYOUTRTL);
	}
	return 0;
}

BOOL ShortcutsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	TranslateDialog(this->m_hWnd);

	((CButton*)GetDlgItem(IDC_SHORTCUTS_ENABLE))->SetCheck(accountSettings.enableShortcuts);
	((CButton*)GetDlgItem(IDC_SHORTCUTS_BOTTOM))->SetCheck(accountSettings.shortcutsBottom);

	CComboBox *combobox;
	for (int i=0; i<_GLOBAL_SHORTCUTS_QTY; i++) {
		Shortcut shortcut;
		if (i < shortcuts.GetCount()) {
			shortcut = shortcuts.GetAt(i);
			GetDlgItem(IDC_SHORTCUTS_EDIT_SHORTCUT1_LABEL + i * 3)->SetWindowText(shortcut.label);
			GetDlgItem(IDC_SHORTCUTS_EDIT_SHORTCUT1_NUMBER + i * 3)->SetWindowText(shortcut.number);
		}
		combobox = (CComboBox*)GetDlgItem(IDC_SHORTCUTS_COMBO_SHORTCUT1_TYPE + i * 3);
		int n = sizeof(defaultActionItems) / sizeof(defaultActionItems[0]);
		bool found = false;
		for (int j = 0; j < n; j++) {
			CString str = defaultActionValues[j];
			if (defaultActionItems[j] == _T("runBatch")
				|| defaultActionItems[j] == _T("callURL")
				|| defaultActionItems[j] == _T("popURL")) {
				str.Append(_T("*"));
			}
			combobox->AddString(str);
			if (shortcut.type == defaultActionItems[j]) {
				combobox->SetCurSel(j);
				found = true;
			}
		}
		if (!found) {
			combobox->SetCurSel(0);
		}
	}
	return TRUE;
}

void ShortcutsDlg::OnDestroy()
{
	mainDlg->shortcutsDlg = NULL;
	CDialog::OnDestroy();
}

void ShortcutsDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	delete this;
}

BEGIN_MESSAGE_MAP(ShortcutsDlg, CDialog)
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDCANCEL, &ShortcutsDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDOK, &ShortcutsDlg::OnBnClickedOk)
END_MESSAGE_MAP()


void ShortcutsDlg::OnClose() 
{
	DestroyWindow();
}

void ShortcutsDlg::OnBnClickedCancel()
{
	OnClose();
}

void ShortcutsDlg::OnBnClickedOk()
{
	this->ShowWindow(SW_HIDE);
	shortcuts.RemoveAll();
	for (int i=0; i<_GLOBAL_SHORTCUTS_QTY; i++) {
		Shortcut shortcut;
		GetDlgItem(IDC_SHORTCUTS_EDIT_SHORTCUT1_LABEL+i*3)->GetWindowText(shortcut.label);
		GetDlgItem(IDC_SHORTCUTS_EDIT_SHORTCUT1_NUMBER+i*3)->GetWindowText(shortcut.number);
		int n = ((CComboBox*)GetDlgItem(IDC_SHORTCUTS_COMBO_SHORTCUT1_TYPE + i * 3))->GetCurSel();
		if (n >=0 && !shortcut.label.IsEmpty() && !shortcut.number.IsEmpty()) {
			shortcut.type = defaultActionItems[n];
			shortcuts.Add(shortcut);
		}
	}
	ShortcutsSave();
	mainDlg->pageDialer->RebuildShortcuts();

	bool enabled = ((CButton*)GetDlgItem(IDC_SHORTCUTS_ENABLE))->GetCheck();
	bool bottom = ((CButton*)GetDlgItem(IDC_SHORTCUTS_BOTTOM))->GetCheck();

	if (mainDlg->shortcutsEnabled!=enabled 
		|| 
		mainDlg->shortcutsBottom!=bottom 
		) {
			AfxMessageBox(Translate(_T("You need to restart application for the changes to take effect.")));
			accountSettings.enableShortcuts=enabled;
			accountSettings.shortcutsBottom = bottom;
			accountSettings.SettingsSave();
	}
	OnClose();
}

