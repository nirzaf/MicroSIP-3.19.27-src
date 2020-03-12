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

#include "stdafx.h"
#include "BaseDialog.h"
#include "resource.h"
#include "global.h"

CBaseDialog::CBaseDialog(UINT nIDTemplate, CWnd* pParent /*=NULL*/)
	: CDialog(nIDTemplate, pParent),
	m_szMinimum(0, 0)
{
	mainWnd = NULL;
}


BEGIN_MESSAGE_MAP(CBaseDialog, CDialog)
	//{{AFX_MSG_MAP(CBaseDialog)
	ON_WM_GETMINMAXINFO()
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CBaseDialog::WinHelp(DWORD dwData, UINT nCmd)
{
	OpenHelp();
}

void CBaseDialog::OpenHelp()
{
	OpenURL(_T(_GLOBAL_MENU_HELP));
}

BOOL CBaseDialog::PreTranslateMessage(MSG* pMsg)
{
	BOOL catched = FALSE;
	int postCommand = 0;
	if (!mainWnd) {
		mainWnd = (CBaseDialog *)AfxGetApp()->GetMainWnd();
	}
	if (
		(pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_F10)
		||
		(pMsg->message == WM_SYSKEYUP && pMsg->wParam == VK_MENU)
		) {
		if (mainWnd == this || mainWnd == this->GetParent()) {
			bool controlState = GetKeyState(VK_CONTROL) >> 7;
			if (!controlState) {
				CWnd *menuButton = mainWnd->GetDlgItem(IDC_MAIN_MENU);
				if (mainWnd->GetFocus() == menuButton) {
					if (pMsg->wParam == VK_F10) {
						mainWnd->TabFocusSet();
					}
				}
				else {
					menuButton->SetFocus();
				}
				catched = TRUE;
			}
		}
	}
	else
		if (pMsg->message == WM_KEYDOWN) {
			bool controlState = GetKeyState(VK_CONTROL) >> 7;
			bool altState = GetKeyState(VK_MENU) >> 7;
				if (controlState && !altState) {
					if (pMsg->wParam == VK_TAB) {
						if (mainWnd == this || mainWnd == this->GetParent()) {
							bool shiftState = GetKeyState(VK_SHIFT) >> 7;
							if (!shiftState) {
								mainWnd->GotoTab(-1);
							}
							else {
								mainWnd->GotoTab(-2);
							}
							catched = TRUE;
						}
					}
					if (pMsg->wParam == 'M') {
						postCommand = ID_ACCOUNT_EDIT_RANGE;
					}
					if (pMsg->wParam == 'L') {
						postCommand = ID_ACCOUNT_EDIT_LOCAL;
					}
					if (pMsg->wParam == 'P') {
						postCommand = ID_SETTINGS;
					}
					if (pMsg->wParam == 'S') {
						postCommand = ID_SHORTCUTS;
					}
					if (pMsg->wParam == 'W') {
						postCommand = ID_MENU_WEBSITE;
					}
					if (pMsg->wParam == 'Q') {
						postCommand = ID_EXIT;
					}
				}
				else {
					if (pMsg->wParam == VK_F2) {
						//answer a call
						msip_call_answer();
						catched = TRUE;
					}
					if (pMsg->wParam == VK_F4) {
						//hangup
						pjsua_call_hangup_all();
						catched = TRUE;
					}
					if (pMsg->wParam == VK_ESCAPE) {
						if (mainWnd == this || mainWnd == this->GetParent()) {
							CWnd *menuButton = mainWnd->GetDlgItem(IDC_MAIN_MENU);
							if (mainWnd->GetFocus() == menuButton) {
								mainWnd->TabFocusSet();
								catched = TRUE;
							}
						}
					}
				}
		}
	if (mainWnd && postCommand) {
		mainWnd->PostMessage(WM_COMMAND, postCommand, 0);
		catched = TRUE;
	}
	if (!catched) {
		return CDialog::PreTranslateMessage(pMsg);
	}
	else {
		return TRUE;
	}
}

void CBaseDialog::AutoMove(int iID, double dXMovePct, double dYMovePct, double dXSizePct, double dYSizePct)
{
	ASSERT((dXMovePct + dXSizePct) <= 100.0);   // can't use more than 100% of the resize for the child
	ASSERT((dYMovePct + dYSizePct) <= 100.0);   // can't use more than 100% of the resize for the child
	SMovingChild s;
	GetDlgItem(iID, &s.m_hWnd);
	ASSERT(s.m_hWnd != NULL);
	s.m_dXMoveFrac = dXMovePct / 100.0;
	s.m_dYMoveFrac = dYMovePct / 100.0;
	s.m_dXSizeFrac = dXSizePct / 100.0;
	s.m_dYSizeFrac = dYSizePct / 100.0;
	::GetWindowRect(s.m_hWnd, &s.m_rcInitial);
	ScreenToClient(s.m_rcInitial);
	m_MovingChildren.push_back(s);
}

void CBaseDialog::AutoMove(HWND hWnd, double dXMovePct, double dYMovePct, double dXSizePct, double dYSizePct)
{
	ASSERT((dXMovePct + dXSizePct) <= 100.0);   // can't use more than 100% of the resize for the child
	ASSERT((dYMovePct + dYSizePct) <= 100.0);   // can't use more than 100% of the resize for the child
	SMovingChild s;
	s.m_hWnd = hWnd;
	ASSERT(s.m_hWnd != NULL);
	s.m_dXMoveFrac = dXMovePct / 100.0;
	s.m_dYMoveFrac = dYMovePct / 100.0;
	s.m_dXSizeFrac = dXSizePct / 100.0;
	s.m_dYSizeFrac = dYSizePct / 100.0;
	::GetWindowRect(s.m_hWnd, &s.m_rcInitial);
	ScreenToClient(s.m_rcInitial);
	m_MovingChildren.push_back(s);
}

void CBaseDialog::AutoUnmove(HWND hWnd)
{
	ASSERT(hWnd != NULL);
	for (MovingChildren::iterator p = m_MovingChildren.begin(); p != m_MovingChildren.end(); ++p)
	{
		if (p->m_hWnd == hWnd)
		{
			m_MovingChildren.erase(p);
			break;
		}
	}
}

BOOL CBaseDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	// use the initial dialog size as the default minimum
	if ((m_szMinimum.cx == 0) && (m_szMinimum.cy == 0))
	{
		CRect rcWindow;
		GetWindowRect(rcWindow);
		m_szMinimum = rcWindow.Size();
	}

	// keep the initial size of the client area as a baseline for moving/sizing controls
	CRect rcClient;
	GetClientRect(rcClient);
	m_szInitial = rcClient.Size();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CBaseDialog::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI)
{
	CDialog::OnGetMinMaxInfo(lpMMI);

	if (lpMMI->ptMinTrackSize.x < m_szMinimum.cx)
		lpMMI->ptMinTrackSize.x = m_szMinimum.cx;
	if (lpMMI->ptMinTrackSize.y < m_szMinimum.cy)
		lpMMI->ptMinTrackSize.y = m_szMinimum.cy;
}

void CBaseDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	int iXDelta = cx - m_szInitial.cx;
	int iYDelta = cy - m_szInitial.cy;
	HDWP hDefer = NULL;
	for (MovingChildren::iterator p = m_MovingChildren.begin(); p != m_MovingChildren.end(); ++p)
	{
		if (p->m_hWnd != NULL)
		{
			CRect rcNew(p->m_rcInitial);
			rcNew.OffsetRect(int(iXDelta * p->m_dXMoveFrac), int(iYDelta * p->m_dYMoveFrac));
			rcNew.right += int(iXDelta * p->m_dXSizeFrac);
			rcNew.bottom += int(iYDelta * p->m_dYSizeFrac);
			if (hDefer == NULL)
				hDefer = BeginDeferWindowPos(m_MovingChildren.size());
			UINT uFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER;
			if ((p->m_dXSizeFrac != 0.0) || (p->m_dYSizeFrac != 0.0))
				uFlags |= SWP_NOCOPYBITS;
			DeferWindowPos(hDefer, p->m_hWnd, NULL, rcNew.left, rcNew.top, rcNew.Width(), rcNew.Height(), uFlags);
		}
	}
	if (hDefer != NULL)
		EndDeferWindowPos(hDefer);

}
