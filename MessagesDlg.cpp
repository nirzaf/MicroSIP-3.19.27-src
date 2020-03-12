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
#include "MessagesDlg.h"
#include "microsip.h"
#include "mainDlg.h"
#include "settings.h"
#include "Transfer.h"
#include "langpack.h"

static DWORD __stdcall MEditStreamOutCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
	CString sThisWrite;
	sThisWrite.GetBufferSetLength(cb);

	CString *psBuffer = (CString *)dwCookie;

	for (int i = 0; i < cb; i++)
	{
		sThisWrite.SetAt(i, *(pbBuff + i));
	}

	*psBuffer += sThisWrite;

	*pcb = sThisWrite.GetLength();
	sThisWrite.ReleaseBuffer();
	return 0;
}

static DWORD __stdcall MEditStreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
	CString *psBuffer = (CString*)dwCookie;

	if (cb > psBuffer->GetLength()) cb = psBuffer->GetLength();

	for (int i = 0; i < cb; i++)
	{
		*(pbBuff + i) = psBuffer->GetAt(i);
	}

	*pcb = cb;
	*psBuffer = psBuffer->Mid(cb);

	return 0;
}

MessagesDlg::MessagesDlg(CWnd* pParent /*=NULL*/)
	: CBaseDialog(MessagesDlg::IDD, pParent)
{
	this->m_hWnd = NULL;
	Create(IDD, pParent);
}

MessagesDlg::~MessagesDlg(void)
{
}

int MessagesDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (langPack.rtl) {
		ModifyStyleEx(0, WS_EX_LAYOUTRTL);
	}
	return 0;
}

void MessagesDlg::DoDataExchange(CDataExchange* pDX)
{
	CBaseDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MESSAGES_TAB, tabComponent);
}

BOOL MessagesDlg::OnInitDialog()
{
	CBaseDialog::OnInitDialog();

	AutoMove(IDC_MESSAGES_TAB, 0, 0, 100, 0);
	AutoMove(IDC_LAST_CALL, 100, 0, 0, 0);
	AutoMove(IDC_CLOSE_ALL, 100, 0, 0, 0);
	AutoMove(IDC_CONFERENCE, 100, 0, 0, 0);
	AutoMove(IDC_TRANSFER, 100, 0, 0, 0);
	AutoMove(IDC_HOLD, 100, 0, 0, 0);
	AutoMove(IDC_END, 100, 0, 0, 0);
	AutoMove(IDC_LIST, 0, 0, 100, 80);
	AutoMove(IDC_MESSAGE, 0, 80, 100, 20);
	lastCall = NULL;
	tab = &tabComponent;

	HICON m_hIcon = theApp.LoadIcon(IDR_MAINFRAME);
	SetIcon(m_hIcon, FALSE);

	imageList.Create(16, 16, ILC_COLOR32 | ILC_MASK, 7, 1);

	imageList.Add(LoadImageIcon(IDI_CALL_OUT));
	imageList.Add(LoadImageIcon(IDI_CALL_IN));
	imageList.Add(LoadImageIcon(IDI_CALL_MISS));
	imageList.Add(LoadImageIcon(IDI_ACTIVE));
	imageList.Add(LoadImageIcon(IDI_ACTIVE_SECURE));
	imageList.Add(LoadImageIcon(IDI_CONFERENCE));
	imageList.Add(LoadImageIcon(IDI_CONFERENCE_SECURE));
	imageList.Add(LoadImageIcon(IDI_MESSAGE_IN));
	imageList.Add(LoadImageIcon(IDI_ON_HOLD));
	imageList.Add(LoadImageIcon(IDI_ON_REMOTE_HOLD));
	imageList.Add(LoadImageIcon(IDI_ON_REMOTE_HOLD_CONFERENCE));

	tab->SetImageList(&imageList);


	TranslateDialog(this->m_hWnd);

#ifndef _GLOBAL_VIDEO
	GetDlgItem(IDC_VIDEO_CALL)->ShowWindow(SW_HIDE);
#endif

	CRichEditCtrl* richEditList = (CRichEditCtrl*)GetDlgItem(IDC_LIST);
	richEditList->SetEventMask(richEditList->GetEventMask() | ENM_MOUSEEVENTS);
	richEditList->SetUndoLimit(0);

	CFont* font = this->GetFont();
	LOGFONT lf;
	font->GetLogFont(&lf);
	lf.lfHeight = 16;
	_tcscpy(lf.lfFaceName, _T("Arial"));
	fontList.CreateFontIndirect(&lf);
	richEditList->SetFont(&fontList);
	lf.lfHeight = 18;
	fontMessage.CreateFontIndirect(&lf);

	CRichEditCtrl* richEdit = (CRichEditCtrl*)GetDlgItem(IDC_MESSAGE);
	richEdit->SetEventMask(richEdit->GetEventMask() | ENM_KEYEVENTS);
	richEdit->SetFont(&fontMessage);

	para.cbSize = sizeof(PARAFORMAT2);
	para.dwMask = PFM_STARTINDENT | PFM_LINESPACING | PFM_SPACEBEFORE | PFM_SPACEAFTER;
	para.dxStartIndent = 100;
	para.dySpaceBefore = 100;
	para.dySpaceAfter = 0;
	para.bLineSpacingRule = 5;
	para.dyLineSpacing = 22;

	m_hIconHold = LoadImageIcon(IDI_HOLD);
	m_hIconResume = LoadImageIcon(IDI_RESUME);
	((CButton*)GetDlgItem(IDC_HOLD))->SetIcon(m_hIconHold);

	MENUITEMINFO mii;
	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_SUBMENU;

	CMenu *tracker;

	menuTransfer.LoadMenu(IDR_MENU_CALL_TRANSFER);
	tracker = menuTransfer.GetSubMenu(0);
	TranslateMenu(tracker->m_hMenu);

	menuAttendedTransfer.CreatePopupMenu();
	mii.hSubMenu = menuAttendedTransfer.m_hMenu;
	tracker->SetMenuItemInfo(ID_ATTENDED_TRANSFER, &mii);

	menuConference.LoadMenu(IDR_MENU_CALL_CONFERENCE);
	tracker = menuConference.GetSubMenu(0);
	TranslateMenu(tracker->m_hMenu);

	menuMerge.CreatePopupMenu();
	mii.hSubMenu = menuMerge.m_hMenu;
	tracker->SetMenuItemInfo(ID_MERGE, &mii);

	return TRUE;
}

void MessagesDlg::OnDestroy()
{
	mainDlg->messagesDlg = NULL;
	CBaseDialog::OnDestroy();
}


void MessagesDlg::PostNcDestroy()
{
	CBaseDialog::PostNcDestroy();
	delete this;
}

BEGIN_MESSAGE_MAP(MessagesDlg, CBaseDialog)
	ON_WM_CREATE()
	ON_WM_SYSCOMMAND()
	ON_WM_CLOSE()
	ON_WM_MOVE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_COMMAND(ID_CLOSEALLTABS, OnCloseAllTabs)
	ON_COMMAND(ID_GOTOLASTTAB, OnGoToLastTab)
	ON_COMMAND(ID_COPY, OnCopy)
	ON_COMMAND(ID_SELECT_ALL, OnSelectAll)
	ON_COMMAND_RANGE(ID_ATTENDED_TRANSFER_RANGE, ID_ATTENDED_TRANSFER_RANGE + 99, OnAttendedTransfer)
	ON_COMMAND_RANGE(ID_MERGE_RANGE, ID_MERGE_RANGE + 99, OnMerge)
	ON_COMMAND(ID_MERGE_ALL, OnMergeAll)
	ON_COMMAND(IDCANCEL, OnCancel)
	ON_BN_CLICKED(IDOK, &MessagesDlg::OnBnClickedOk)
	ON_NOTIFY(EN_MSGFILTER, IDC_MESSAGE, &MessagesDlg::OnEnMsgfilterMessage)
	ON_NOTIFY(TCN_SELCHANGE, IDC_MESSAGES_TAB, &MessagesDlg::OnTcnSelchangeTab)
	ON_NOTIFY(TCN_SELCHANGING, IDC_MESSAGES_TAB, &MessagesDlg::OnTcnSelchangingTab)
	ON_MESSAGE(WM_CONTEXTMENU, OnContextMenu)
	ON_MESSAGE(UM_CLOSETAB, &MessagesDlg::OnCloseTab)
	ON_BN_CLICKED(IDC_CALL_END, &MessagesDlg::OnBnClickedCallEnd)
	ON_BN_CLICKED(IDC_VIDEO_CALL, &MessagesDlg::OnBnClickedVideoCall)
	ON_BN_CLICKED(IDC_TRANSFER, &MessagesDlg::OnBnClickedTransfer)
	ON_BN_CLICKED(IDC_CONFERENCE, &MessagesDlg::OnBnClickedConference)
	ON_COMMAND(ID_TRANSFER, OnTransfer)
	ON_COMMAND(ID_CONFERENCE, OnConference)
	ON_COMMAND(ID_SEPARATE, OnSeparate)
	ON_COMMAND(ID_SEPARATE_ALL, OnSeparateAll)
	ON_COMMAND(ID_DISCONNECT, OnDisconnect)
	ON_BN_CLICKED(IDC_HOLD, &MessagesDlg::OnBnClickedHold)
	ON_BN_CLICKED(IDC_END, &MessagesDlg::OnBnClickedEnd)
	ON_BN_CLICKED(IDC_CLOSE_ALL, &MessagesDlg::OnBnClickedCloseAll)
	ON_BN_CLICKED(IDC_LAST_CALL, &MessagesDlg::OnBnClickedLastCall)
END_MESSAGE_MAP()

void MessagesDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (nID == SC_CLOSE) {
		OnCancel();
		return;
	}
	__super::OnSysCommand(nID, lParam);
}

LRESULT MessagesDlg::OnContextMenu(WPARAM wParam, LPARAM lParam)
{
	int x = GET_X_LPARAM(lParam);
	int y = GET_Y_LPARAM(lParam);
	POINT pt = { x, y };
	RECT rc;
	if (x != -1 || y != -1) {
		ScreenToClient(&pt);
		GetClientRect(&rc);
		if (!PtInRect(&rc, pt)) {
			x = y = -1;
		}
	}
	else {
		::ClientToScreen((HWND)wParam, &pt);
		x = 10 + pt.x;
		y = 10 + pt.y;
	}
	if (x != -1 || y != -1) {
		CMenu menu;
		menu.LoadMenu(IDR_MENU_TABS);
		CMenu* tracker = menu.GetSubMenu(0);
		TranslateMenu(tracker->m_hMenu);
		tracker->TrackPopupMenu(0, x, y, this);
		return TRUE;
	}
	return DefWindowProc(WM_CONTEXTMENU, wParam, lParam);
}

void MessagesDlg::OnClose()
{
	call_hangup_all_noincoming();
	ShowWindow(SW_HIDE);
	mainDlg->PostMessage(UM_NOTIFYICON, NULL, WM_LBUTTONUP);
}

void MessagesDlg::OnMove(int x, int y)
{
	if (IsWindowVisible() && !IsZoomed() && !IsIconic()) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.messagesX = cRect.left;
		accountSettings.messagesY = cRect.top;
		mainDlg->AccountSettingsPendingSave();
	}
}

void MessagesDlg::OnSize(UINT type, int w, int h)
{
	CBaseDialog::OnSize(type, w, h);
	if (this->IsWindowVisible() && type == SIZE_RESTORED) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.messagesW = cRect.Width();
		accountSettings.messagesH = cRect.Height();
		mainDlg->AccountSettingsPendingSave();
	}
}

void MessagesDlg::OnCancel()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (messagesContact && messagesContact->callId != -1) {
		return;
	}
	OnGoToLastTab();
	messagesContact = GetMessageContact();
	if (messagesContact && messagesContact->callId != -1) {
		return;
	}
	OnClose();
}

void MessagesDlg::OnBnClickedOk()
{
}

MessagesContact* MessagesDlg::AddTab(CString number, CString name, BOOL activate, pjsua_call_info *call_info, call_user_data *user_data, BOOL notShowWindow, BOOL ifExists)
{
	MessagesContact* messagesContact = NULL;

	SIPURI sipuri;
	ParseSIPURI(number, &sipuri);

	//-- incoming call
	if (call_info && call_info->role == PJSIP_ROLE_UAS) {
		//-- fix wrong domain
		if (accountSettings.accountId) {
			if (IsIP(RemovePort(sipuri.domain))) {
				sipuri.domain = get_account_domain();
			}
		}
		//--
	}
	//--
	if (accountSettings.accountId && RemovePort(get_account_domain()) == RemovePort(sipuri.domain)) {
		sipuri.domain = get_account_domain();
	}

	number = (sipuri.user.GetLength() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;

	LONG exists = -1;
	bool isNewCall = false;
	for (int i = 0; i < tab->GetItemCount(); i++)
	{
		messagesContact = GetMessageContact(i);
		if (messagesContact->number == number) {
			exists = i;
			if (call_info)
			{
				if (messagesContact->callId != -1) {
					if (messagesContact->callId != call_info->id) {
						if (call_info->state != PJSIP_INV_STATE_DISCONNECTED) {
							mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)call_info->id, -486);
						}
						return NULL;
					}
				}
				else {
					messagesContact->callId = call_info->id;
					isNewCall = true;
				}
			}
			break;
		}
	}
	//--name
	if (name.IsEmpty()) {
		if (exists == -1 || messagesContact->callId == -1 || isNewCall) {
			name = mainDlg->pageContacts->GetNameByNumber(GetSIPURI(number, true));
			if (name.IsEmpty()) {
				CString numberLocal = number;
				name = sipuri.name;
				if (name.IsEmpty()) {
					name = (sipuri.domain == get_account_domain() ? sipuri.user : numberLocal);
				}
			}
		}
		else {
			name = messagesContact->name;
		}
	}
	//--end name
	CString tabName = name;
	if (user_data) {
		user_data->CS.Lock();
		if (!user_data->diversion.IsEmpty()) {
			tabName.Format(_T("%s -> %s"), user_data->diversion, tabName);
		}
		user_data->CS.Unlock();
	}
	tabName.Format(_T("   %s  "), tabName);
	if (exists == -1) {
		if (ifExists) {
			return NULL;
		}
		messagesContact = new MessagesContact();
		messagesContact->callId = call_info ? call_info->id : -1;
		messagesContact->number = number;
		messagesContact->numberParameters = sipuri.parameters;
		messagesContact->name = name;
		TCITEM item;
		item.mask = TCIF_PARAM | TCIF_TEXT;
		item.pszText = tabName.GetBuffer();
		item.cchTextMax = 0;
		item.lParam = (LPARAM)messagesContact;
		exists = tab->InsertItem(tab->GetItemCount(), &item);
		if (tab->GetCurSel() == exists) {
			OnChangeTab(call_info, user_data);
		}
	}
	else {
		if (messagesContact->callId == -1) {
			messagesContact->numberParameters = sipuri.parameters;
		}
		if (messagesContact->name != name) {
			messagesContact->name = name;
			if (tab->GetCurSel() == exists) {
				SetWindowText(name);
			}
		}
		//--
		TCITEM item;
		item.mask = TCIF_TEXT;
		item.pszText = tabName.GetBuffer();
		item.cchTextMax = 0;
		tab->SetItem(exists, &item);
		//--
		if (tab->GetCurSel() == exists && call_info) {
			UpdateCallButton(messagesContact->callId != -1, call_info, user_data);
		}
	}
	//--update tab icon
	if (call_info) {
		UpdateTabIcon(messagesContact, exists, call_info, user_data);
	}
	//if (tab->GetCurSel() != exists && (activate || !IsWindowVisible()))
	if (tab->GetCurSel() != exists && activate)
	{
		LONG_PTR result;
		OnTcnSelchangingTab(NULL, &result);
		tab->SetCurSel(exists);
		OnChangeTab(call_info, user_data);
	}

	if (!IsWindowVisible()) {
		if (!notShowWindow)
		{
			if (!accountSettings.hidden) {
				ShowWindow(SW_SHOW);
				CRichEditCtrl* richEdit = (CRichEditCtrl*)GetDlgItem(IDC_MESSAGE);
				GotoDlgCtrl(richEdit);
			}
		}
	}
	return messagesContact;
}

void MessagesDlg::OnChangeTab(pjsua_call_info *p_call_info, call_user_data *user_data)
{
	//tab->HighlightItem(tab->GetCurSel(),FALSE);
	MessagesContact* messagesContact = GetMessageContact();
	pjsua_call_info call_info;
	if (messagesContact->callId != -1) {
		if (!p_call_info) {
			if (pjsua_var.state == PJSUA_STATE_RUNNING && pjsua_call_get_info(messagesContact->callId, &call_info) == PJ_SUCCESS) {
				p_call_info = &call_info;
			}
		}
	}
	if (messagesContact->hasNewMessages) {
		messagesContact->hasNewMessages = false;
		UpdateTabIcon(messagesContact, tab->GetCurSel(), p_call_info, user_data);
	}
	SetWindowText(messagesContact->name);

	if (messagesContact->callId != -1) {
		UpdateCallButton(TRUE, p_call_info, user_data);
		if (accountSettings.singleMode
			&& p_call_info && (p_call_info->role == PJSIP_ROLE_UAC ||
			(p_call_info->role == PJSIP_ROLE_UAS &&
				(p_call_info->state == PJSIP_INV_STATE_CONFIRMED
					|| p_call_info->state == PJSIP_INV_STATE_CONNECTING)
				))
			) {
			SIPURI sipuri;
			ParseSIPURI(messagesContact->number, &sipuri);
			mainDlg->pageDialer->SetNumber(!sipuri.user.IsEmpty() && sipuri.domain == get_account_domain() ? sipuri.user : messagesContact->number, 1);
		}
	}
	else {
		UpdateCallButton();
		if (accountSettings.singleMode) {
			mainDlg->pageDialer->PostMessage(WM_COMMAND, MAKELPARAM(IDC_CLEAR, 0), 0);
		}
	}

	CRichEditCtrl* richEditList = (CRichEditCtrl*)GetDlgItem(IDC_LIST);
	CString messages = messagesContact->messages;
	EDITSTREAM es;
	es.dwCookie = (DWORD_PTR)&messages;
	es.pfnCallback = MEditStreamInCallback;
	richEditList->StreamIn(SF_RTF, es);
	richEditList->PostMessage(WM_VSCROLL, SB_BOTTOM, 0);

	CRichEditCtrl* richEdit = (CRichEditCtrl*)GetDlgItem(IDC_MESSAGE);
	richEdit->SetWindowText(messagesContact->message);
	int nEnd = richEdit->GetTextLengthEx(GTL_NUMCHARS);
	richEdit->SetSel(nEnd, nEnd);
}

void MessagesDlg::OnTcnSelchangeTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	OnChangeTab();
	*pResult = 0;
}


void MessagesDlg::OnTcnSelchangingTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	CRichEditCtrl* richEdit = (CRichEditCtrl*)GetDlgItem(IDC_MESSAGE);
	CString str;
	int len = richEdit->GetWindowTextLength();
	LPTSTR ptr = str.GetBuffer(len);
	richEdit->GetWindowText(ptr, len + 1);
	str.ReleaseBuffer();

	MessagesContact* messagesContact = GetMessageContact();
	messagesContact->message = str;
	*pResult = 0;
}

LRESULT  MessagesDlg::OnCloseTab(WPARAM wParam, LPARAM lParam)
{
	int i = wParam;
	CloseTab(i);
	return TRUE;
}

BOOL MessagesDlg::CloseTab(int i, BOOL safe)
{
	int curSel = tab->GetCurSel();

	MessagesContact* messagesContact = GetMessageContact(i);
	if (messagesContact->callId != -1)
	{
		if (safe) {
			return FALSE;
		}
		msip_call_hangup_fast(messagesContact->callId);
	}
	delete messagesContact;
	tab->DeleteItem(i);
	int count = tab->GetItemCount();
	if (!count) {
		GetDlgItem(IDC_LIST)->SetWindowText(_T(""));
		GetDlgItem(IDC_MESSAGE)->SetWindowText(_T(""));
		OnClose();
	}
	else {
		tab->SetCurSel(curSel < count ? curSel : count - 1);
		OnChangeTab();
	}
	return TRUE;
}

pjsua_call_id MessagesDlg::CallMake(CString number, bool hasVideo, pj_status_t *pStatus, call_user_data *user_data)
{
	pjsua_acc_id acc_id;
	pj_str_t pj_uri;
	if (!SelectSIPAccount(number, acc_id, pj_uri)) {
		Account dummy;
		*pStatus = accountSettings.AccountLoad(1, &dummy) ? PJSIP_EAUTHACCDISABLED : PJSIP_EAUTHACCNOTFOUND;
		return PJSUA_INVALID_ID;
	}
	if (accountSettings.singleMode) {
		if (!user_data || !user_data->inConference) {
			call_hangup_all_noincoming();
		}
	}
#ifdef _GLOBAL_VIDEO
	if (hasVideo) {
		mainDlg->createPreviewWin();
	}
#endif

	msip_set_sound_device(msip_audio_output);

	pjsua_call_id call_id;

	pjsua_call_setting call_setting;
	pjsua_call_setting_default(&call_setting);
	call_setting.flag = 0;
	call_setting.vid_cnt = hasVideo ? 1 : 0;

	pjsua_msg_data msg_data;
	pjsua_msg_data_init(&msg_data);

	/* testing autoanswer
	pjsip_generic_string_hdr subject;
	pj_str_t hvalue, hname;
	//hname = pj_str("X-AUTOANSWER");
	//hvalue = pj_str("TRUE");
	hname = pj_str("Call-Info");
	hvalue = pj_str("answer-after=5");
	pjsip_generic_string_hdr_init2 (&subject, &hname, &hvalue);
	pj_list_push_back(&msg_data.hdr_list, &subject);
	//*/
	if ((acc_id == account_local && accountSettings.accountLocal.hideCID) || (acc_id == account && accountSettings.account.hideCID)) {
		pjsip_generic_string_hdr subject;
		pj_str_t hvalue, hname;
		hname = pj_str("Privacy");
		hvalue = pj_str("id");
		pjsip_generic_string_hdr_init2 (&subject, &hname, &hvalue);
		pj_list_push_back(&msg_data.hdr_list, &subject);

		pjsip_generic_string_hdr subject2;
		pj_str_t hvalue2, hname2;
		hname2 = pj_str("From");
		hvalue2 = pj_str("\"Anonymous\" <sip:anonymous@anonymous.invalid>");
		pjsip_generic_string_hdr_init2(&subject2, &hname2, &hvalue2);
		pj_list_push_back(&msg_data.hdr_list, &subject2);
	}
	pj_status_t status = pjsua_call_make_call(
		acc_id,
		&pj_uri,
		&call_setting,
		user_data,
		&msg_data,
		&call_id);
	if (pStatus) {
		*pStatus = status;
	}
	return status == PJ_SUCCESS ? call_id : PJSUA_INVALID_ID;
}

void MessagesDlg::CallStart(bool hasVideo, call_user_data *user_data)
{
	MessagesContact* messagesContact = GetMessageContact();
	if (user_data) {
		user_data->CS.Lock();
		user_data->name = messagesContact->name;
		user_data->CS.Unlock();
	}
	pj_status_t status = PJSIP_EINVALIDREQURI;
	pjsua_call_id call_id = PJSUA_INVALID_ID;
	CString numberWithParams = messagesContact->number + messagesContact->numberParameters;
	bool allow;
	allow = true;
	if (allow) {
		call_id = CallMake(numberWithParams, hasVideo, &status, user_data);
	}
	accountSettings.lastCallNumber = numberWithParams;
	accountSettings.lastCallHasVideo = hasVideo;
	if (call_id != PJSUA_INVALID_ID) {
		if (user_data) {
			user_data->CS.Lock();
			user_data->call_id = call_id;
			user_data->CS.Unlock();
		}
		messagesContact->callId = call_id;
		UpdateCallButton(TRUE);
	}
	else {
		if (status != PJ_ERESOLVE) {
			CString message = GetErrorMessage(status);
			AddMessage(messagesContact, message);
			if (accountSettings.singleMode) {
				AfxMessageBox(message);
			}
		}
	}
}

void MessagesDlg::OnBnClickedCallEnd()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (messagesContact && messagesContact->callId == -1)
	{
		CallStart();
	}
}

void MessagesDlg::OnEndCall(pjsua_call_info *call_info)
{
	for (int i = 0; i < tab->GetItemCount(); i++)
	{
		MessagesContact* messagesContact = GetMessageContact(i);
		if (messagesContact->callId == call_info->id)
		{
			lastCall = messagesContact;
			messagesContact->callId = -1;
			if (tab->GetCurSel() == i)
			{
				UpdateCallButton(FALSE, call_info);
			}
			break;
		}
	}
	OnGoToLastTab();
}

void MessagesDlg::UpdateCallButton(BOOL active, pjsua_call_info *call_info, call_user_data *user_data)
{
	GetDlgItem(IDC_CALL_END)->ShowWindow(active ? SW_HIDE : SW_SHOW);
#ifdef _GLOBAL_VIDEO
	GetDlgItem(IDC_VIDEO_CALL)->ShowWindow(active ? SW_HIDE : SW_SHOW);
#endif
	GetDlgItem(IDC_END)->ShowWindow(!active ? SW_HIDE : SW_SHOW);
	UpdateHoldButton(call_info);
	UpdateRecButton(user_data);
	if (!active) {
		if (mainDlg->transferDlg) {
			mainDlg->transferDlg->OnClose();
		}
		::SendMessage(m_hWnd, WM_CANCELMODE, 0, 0);
	}
}

void MessagesDlg::UpdateHoldButton(pjsua_call_info *call_info)
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact) {
		return;
	}
	bool hasActions = false;
	bool hasHold = false;
	bool onHold = false;
	CButton* buttonTransfer = (CButton*)GetDlgItem(IDC_TRANSFER);
	CButton* buttonConference = (CButton*)GetDlgItem(IDC_CONFERENCE);
	CButton* buttonHold = (CButton*)GetDlgItem(IDC_HOLD);
	CButton* buttonHoldDialer = (CButton*)mainDlg->pageDialer->GetDlgItem(IDC_HOLD);
	CButton* buttonTransferDialer = (CButton*)mainDlg->pageDialer->GetDlgItem(IDC_TRANSFER);
	if (messagesContact->callId != -1 && call_info) {
		if (messagesContact->callId != call_info->id) {
			return;
		}
		if (call_info->state == PJSIP_INV_STATE_EARLY ||
			call_info->state == PJSIP_INV_STATE_CONNECTING ||
			call_info->state == PJSIP_INV_STATE_CONFIRMED) {
			hasActions = true;
			if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
				hasHold = true;
				if (call_info->media_cnt > 0) {
					if (call_info->media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD
						|| call_info->media_status == PJSUA_CALL_MEDIA_NONE) {
						onHold = true;
					}
				}
			}
		}
	}
	//--
	if (hasActions) {
		buttonTransfer->ShowWindow(SW_SHOW);
		buttonConference->ShowWindow(SW_SHOW);
		buttonTransferDialer->EnableWindow(TRUE);
	}
	else {
		buttonTransfer->ShowWindow(SW_HIDE);
		buttonConference->ShowWindow(SW_HIDE);
		buttonTransferDialer->EnableWindow(FALSE);
	}
	if (mainDlg->pageDialer->IsChild(&mainDlg->pageDialer->m_ButtonConf)) {
		mainDlg->pageDialer->m_ButtonConf.EnableWindow(hasActions);
	}
	//--
	if (hasHold) {
		buttonHold->ShowWindow(SW_SHOW);
		buttonHoldDialer->EnableWindow(TRUE);
	}
	else {
		buttonHold->ShowWindow(SW_HIDE);
		buttonHoldDialer->EnableWindow(FALSE);
	}
	//--
	if (onHold) {
		buttonHold->SetCheck(TRUE);
		buttonHold->SetIcon(m_hIconResume);
		buttonHoldDialer->SetCheck(TRUE);
		buttonHoldDialer->SetIcon(mainDlg->pageDialer->m_hIconResume);
	}
	else {
		buttonHold->SetCheck(FALSE);
		buttonHold->SetIcon(m_hIconHold);
		buttonHoldDialer->SetCheck(FALSE);
		buttonHoldDialer->SetIcon(mainDlg->pageDialer->m_hIconHold);
	}
	//--
}

void MessagesDlg::UpdateRecButton(call_user_data *user_data)
{
	bool state = false;
	MessagesContact* messagesContact = GetMessageContact();
	if (messagesContact) {
		if (messagesContact->callId != -1) {
			if (!user_data) {
				user_data = (call_user_data *)pjsua_call_get_user_data(messagesContact->callId);
			}
			if (user_data) {
				user_data->CS.Lock();
				if (user_data->recorder_id != PJSUA_INVALID_ID) {
					state = true;
				}
				user_data->CS.Unlock();
			}
		}
	}
	mainDlg->pageDialer->SetRecBtnState(state);
}

bool MessagesDlg::CallCheck()
{
	if (!accountSettings.singleMode || !call_get_count_noincoming())
	{
		MessagesContact* messagesContact = GetMessageContact();
		if (!messagesContact || messagesContact->callId == -1)
		{
			return true;
		}
	}
	else {
		mainDlg->GotoTab(0);
	}
	return false;
}

void MessagesDlg::Call(BOOL hasVideo, CString commands)
{
	if (CallCheck()) {
		if (mainDlg->missed) {
			mainDlg->missed = false;
		}
		call_user_data *user_data = new call_user_data(PJSUA_INVALID_ID);
		user_data->commands = commands;
		CallStart(hasVideo, user_data);
	}
}

void MessagesDlg::AddMessage(MessagesContact* messagesContact, CString message, int type, BOOL blockForeground)
{
	CTime tm = CTime::GetCurrentTime();

	if (type == MSIP_MESSAGE_TYPE_SYSTEM) {
		if (messagesContact->lastSystemMessage == message && messagesContact->lastSystemMessageTime > tm.GetTime() - 2) {
			messagesContact->lastSystemMessageTime = tm;
			return;
		}
		messagesContact->lastSystemMessage = message;
		messagesContact->lastSystemMessageTime = tm;
	}
	else if (!messagesContact->lastSystemMessage.IsEmpty()) {
		messagesContact->lastSystemMessage.Empty();
	}

	if (IsWindowVisible() && !blockForeground) {
		SetForegroundWindow();
	}
	CRichEditCtrl richEdit;
	MessagesContact* messagesContactSelected = GetMessageContact();

	CRichEditCtrl *richEditList = (CRichEditCtrl *)GetDlgItem(IDC_LIST);
	if (messagesContactSelected != messagesContact) {
		CRect rect;
		rect.left = 0;
		rect.top = 0;
		rect.right = 300;
		rect.bottom = 300;
		richEdit.Create(ES_MULTILINE | ES_READONLY | ES_NUMBER | WS_VSCROLL, rect, this, NULL);
		richEdit.SetFont(&fontList);

		CString messages = messagesContact->messages;
		EDITSTREAM es;
		es.dwCookie = (DWORD_PTR)&messages;
		es.pfnCallback = MEditStreamInCallback;
		richEdit.StreamIn(SF_RTF, es);

		richEditList = &richEdit;
	}

	if (messagesContact->messages.IsEmpty()) {
		richEditList->SetSel(0, -1);
		richEditList->SetParaFormat(para);
	}

	COLORREF color;
	CString name;
	if (type == MSIP_MESSAGE_TYPE_LOCAL) {
		color = RGB(0, 0, 0);
		if (!accountSettings.account.displayName.IsEmpty()) {
			name = accountSettings.account.displayName;
		}
	}
	else if (type == MSIP_MESSAGE_TYPE_REMOTE) {
		color = RGB(21, 101, 206);
		name = messagesContact->name;
		int pos = name.Find(_T(" ("));
		if (pos == -1) {
			pos = name.Find(_T("@"));
		}
		if (pos != -1) {
			name = name.Mid(0, pos);
		}
	}

	int nBegin;
	CHARFORMAT cf;
	CString str;

	CString time = tm.Format(_T("%X"));

	nBegin = richEditList->GetTextLengthEx(GTL_NUMCHARS);
	richEditList->SetSel(nBegin, nBegin);
	str.Format(_T("[%s]  "), time);
	richEditList->ReplaceSel(str);
	cf.dwMask = CFM_BOLD | CFM_COLOR | CFM_SIZE;
	cf.crTextColor = RGB(131, 131, 131);
	cf.dwEffects = 0;
	cf.yHeight = 160;
	richEditList->SetSel(nBegin, -1);
	richEditList->SetSelectionCharFormat(cf);

	if (type != MSIP_MESSAGE_TYPE_SYSTEM) {
		cf.yHeight = 200;
	}
	if (name.GetLength()) {
		nBegin = richEditList->GetTextLengthEx(GTL_NUMCHARS);
		richEditList->SetSel(nBegin, nBegin);
		richEditList->ReplaceSel(name + _T(": "));
		cf.dwMask = CFM_BOLD | CFM_COLOR | CFM_SIZE;
		cf.crTextColor = color;
		cf.dwEffects = CFE_BOLD;
		richEditList->SetSel(nBegin, -1);
		richEditList->SetSelectionCharFormat(cf);
	}

	nBegin = richEditList->GetTextLengthEx(GTL_NUMCHARS);
	richEditList->SetSel(nBegin, nBegin);
	richEditList->ReplaceSel(message + _T("\r\n"));
	cf.dwMask = CFM_BOLD | CFM_COLOR | CFM_SIZE;

	cf.crTextColor = type == MSIP_MESSAGE_TYPE_SYSTEM ? RGB(131, 131, 131) : color;
	cf.dwEffects = 0;

	richEditList->SetSel(nBegin, -1);
	richEditList->SetSelectionCharFormat(cf);

	int selectedIndex = -1;
	if (messagesContactSelected == messagesContact) {
		richEditList->PostMessage(WM_VSCROLL, SB_BOTTOM, 0);
		selectedIndex = tab->GetCurSel();
	}
	else {
		if (type == MSIP_MESSAGE_TYPE_REMOTE) {
			messagesContact->hasNewMessages = true;
			UpdateTabIcon(messagesContact);
		}
		/*
		for (int i = 0; i < tab->GetItemCount(); i++) {
			if (messagesContact == GetMessageContact(i))
			{
				tab->HighlightItem(i, TRUE);
				break;
			}
		}
		*/
	}
	str = _T("");
	EDITSTREAM es;
	es.dwCookie = (DWORD_PTR)&str;
	es.pfnCallback = MEditStreamOutCallback;
	richEditList->StreamOut(SF_RTF, es);
	messagesContact->messages = str;
}

void MessagesDlg::OnEnMsgfilterMessage(NMHDR *pNMHDR, LRESULT *pResult)
{
	MSGFILTER *pMsgFilter = reinterpret_cast<MSGFILTER *>(pNMHDR);

	if (pMsgFilter->msg == WM_CHAR) {
		if (pMsgFilter->wParam == VK_RETURN) {
			CRichEditCtrl* richEdit = (CRichEditCtrl*)GetDlgItem(IDC_MESSAGE);
			CString message;
			int len = richEdit->GetWindowTextLength();
			LPTSTR ptr = message.GetBuffer(len);
			richEdit->GetWindowText(ptr, len + 1);
			message.ReleaseBuffer();
			message.Trim();
			if (message.GetLength()) {
				MessagesContact* messagesContact = GetMessageContact();
				if (SendInstantMessage(messagesContact, message)) {
					richEdit->SetWindowText(_T(""));
					GotoDlgCtrl(richEdit);
					AddMessage(messagesContact, message, MSIP_MESSAGE_TYPE_LOCAL);
					if (accountSettings.localDTMF) {
						mainDlg->onPlayerPlay(MSIP_SOUND_MESSAGE_OUT, 0);
					}
				}
			}
			*pResult = 1;
			return;
		}
	}
	*pResult = 0;
}

BOOL MessagesDlg::SendInstantMessage(MessagesContact* messagesContact, CString message, CString number)
{
	message.Trim();
	if (message.GetLength()) {
		pjsua_acc_id acc_id;
		pj_str_t pj_uri;
		pj_status_t status;
		if (SelectSIPAccount(messagesContact ? messagesContact->number + messagesContact->numberParameters : number, acc_id, pj_uri)) {
			pj_str_t pj_message = StrToPjStr(message);
			status = pjsua_im_send(acc_id, &pj_uri, NULL, &pj_message, NULL, NULL);
		}
		else {
			Account dummy;
			status = accountSettings.AccountLoad(1, &dummy) ? PJSIP_EAUTHACCDISABLED : PJSIP_EAUTHACCNOTFOUND;
		}
		if (status != PJ_SUCCESS) {
			if (messagesContact) {
				CString message = GetErrorMessage(status);
				AddMessage(messagesContact, message);
			}
		}
		else {
			return TRUE;
		}
	}
	return FALSE;
}

MessagesContact* MessagesDlg::GetMessageContact(int i)
{
	if (i == -1) {
		i = tab->GetCurSel();
	}
	if (i != -1) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		tab->GetItem(i, &item);
		return (MessagesContact*)item.lParam;
	}
	return NULL;
}

MessagesContact* MessagesDlg::GetMessageContactInCall()
{
	MessagesContact* messagesContactActive = NULL;
	for (int i = 0; i < tab->GetItemCount(); i++) {
		MessagesContact* messagesContact = GetMessageContact(i);
		if (messagesContact->callId != -1) {
			if (messagesContact->mediaStatus == PJSUA_CALL_MEDIA_ACTIVE || messagesContact->mediaStatus == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
				messagesContactActive = messagesContact;
				break;
			}
			if (messagesContact->mediaStatus == PJSUA_CALL_MEDIA_LOCAL_HOLD || messagesContact->mediaStatus == PJSUA_CALL_MEDIA_NONE) {
				messagesContactActive = messagesContact;
			}
		}
	}
	return messagesContactActive;
}

void MessagesDlg::OnBnClickedVideoCall()
{
	CallStart(true);
}

void MessagesDlg::Merge(pjsua_call_id call_id)
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	if (messagesContact->callId == call_id || !pjsua_call_is_active(call_id)) {
		return;
	}
	call_user_data *user_data;
	user_data = (call_user_data *)pjsua_call_get_user_data(messagesContact->callId);
	if (!user_data) {
		user_data = new call_user_data(messagesContact->callId);
		pjsua_call_set_user_data(messagesContact->callId, user_data);
	}
	user_data->CS.Lock();
	user_data->inConference = true;
	user_data->CS.Unlock();

	user_data = (call_user_data *)pjsua_call_get_user_data(call_id);
	if (!user_data) {
		user_data = new call_user_data(messagesContact->callId);
		pjsua_call_set_user_data(messagesContact->callId, user_data);
	}
	user_data->CS.Lock();
	user_data->inConference = true;
	user_data->CS.Unlock();

	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS) {
		return;
	}
	msip_conference_join(&call_info);
	msip_call_unhold(&call_info);
}

void MessagesDlg::Separate(pjsua_call_id call_id)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS) {
		return;
	}
	msip_conference_leave(&call_info);
	msip_call_unhold(&call_info);
}

void MessagesDlg::CallAction(int action, CString number)
{
	MessagesContact* messagesContactSelected = mainDlg->messagesDlg->GetMessageContact();
	if (!messagesContactSelected || messagesContactSelected->callId == -1) {
		return;
	}
	number.Trim();
	if (!number.IsEmpty()) {
		pj_str_t pj_uri;
		SIPURI sipuri;
		CString commands;
		if (number.Find('<') != 1 && number.Find('>') != 1) {
			CString numberFormated = FormatNumber(number, &commands);
			pj_uri = StrToPjStr(numberFormated);
			ParseSIPURI(numberFormated, &sipuri);
			number = sipuri.user;
		}
		else {
			pj_uri = StrToPjStr(GetSIPURI(number, true));
		}
		call_user_data *user_data;
		user_data = (call_user_data *)pjsua_call_get_user_data(messagesContactSelected->callId);
		if (action == MSIP_ACTION_TRANSFER) {
			bool xfer;
			if (user_data) {
				user_data->CS.Lock();
				xfer = !user_data->inConference;
				user_data->CS.Unlock();
			}
			else {
				xfer = true;
			}
			if (xfer) {

				pjsua_call_xfer(messagesContactSelected->callId, &pj_uri, NULL);
			}
		}
		if (action == MSIP_ACTION_INVITE) {
			MessagesContact *messagesContact = mainDlg->messagesDlg->AddTab(FormatNumber(number), _T(""), TRUE, NULL, NULL, TRUE);
			if (messagesContact->callId == -1) {
				pjsua_call_info call_info;
				pjsua_call_get_info(messagesContactSelected->callId, &call_info);
				msip_call_unhold(&call_info);
				if (!user_data) {
					user_data = new call_user_data(messagesContactSelected->callId);
					pjsua_call_set_user_data(messagesContactSelected->callId, user_data);
				}
				user_data->CS.Lock();
				user_data->inConference = true;
				user_data->CS.Unlock();
				call_user_data *user_data_new = new call_user_data(PJSUA_INVALID_ID);
				user_data_new->inConference = true;
				mainDlg->messagesDlg->CallStart(false, user_data_new);
				if (messagesContact->callId == -1) {
					user_data->CS.Lock();
					user_data->inConference = false;
					user_data->CS.Unlock();
					delete user_data_new;
				}
			}
		}
	}
}

void MessagesDlg::OnBnClickedHold()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	pjsua_call_info call_info;
	pjsua_call_get_info(messagesContact->callId, &call_info);
	if (call_info.state == PJSIP_INV_STATE_CONFIRMED) {
		if (call_info.media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD || call_info.media_status == PJSUA_CALL_MEDIA_NONE) {
			msip_call_unhold(&call_info);
		}
		else {
			msip_call_hold(&call_info);
		}
	}
}

void MessagesDlg::OnBnClickedTransfer()
{
	OnBnClickedActions();
}

void MessagesDlg::OnBnClickedConference()
{
	OnBnClickedActions(true);
}

void MessagesDlg::OnBnClickedActions(bool isConference)
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	CMenu *tracker;
	if (isConference) {
		tracker = menuConference.GetSubMenu(0);
	}
	else {
		tracker = menuTransfer.GetSubMenu(0);
	}
	pjsua_call_info call_info;
	if (pjsua_call_get_info(messagesContact->callId, &call_info) != PJ_SUCCESS) {
		return;
	}
	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(messagesContact->callId);
	bool inConference = false;
	if (user_data) {
		user_data->CS.Lock();
		if (user_data->inConference) {
			inConference = true;
		}
		user_data->CS.Unlock();
	}
	//-- transfer
	tracker->EnableMenuItem(ID_TRANSFER, !inConference ? 0 : MF_GRAYED);
	//-- attended transfer & merge
	while (menuAttendedTransfer.DeleteMenu(0, MF_BYPOSITION));
	while (menuMerge.DeleteMenu(0, MF_BYPOSITION));
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	int pos = 0;
	int posMerge = 0;
	pjsua_call_id mergeConferenceAddedId = PJSUA_INVALID_ID;
	if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < calls_count; ++i) {
			if (call_ids[i] != messagesContact->callId) {
				pjsua_call_info call_info_curr;
				pjsua_call_get_info(call_ids[i], &call_info_curr);
				call_user_data *user_data_curr = (call_user_data *)pjsua_call_get_user_data(call_ids[i]);
				bool inConferenceCurr = false;
				if (user_data_curr) {
					user_data_curr->CS.Lock();
					if (user_data_curr->inConference) {
						inConferenceCurr = true;
					}
					user_data_curr->CS.Unlock();
				}
				CString str;
				//--attended transfer
				if (call_info_curr.role == PJSIP_ROLE_UAS || call_info_curr.state == PJSIP_INV_STATE_CONFIRMED) {
					SIPURI sipuri_curr;
					ParseSIPURI(PjToStr(&call_info_curr.remote_info, TRUE), &sipuri_curr);
					str = !sipuri_curr.name.IsEmpty() ? sipuri_curr.name : (!sipuri_curr.user.IsEmpty() ? sipuri_curr.user : (sipuri_curr.domain));
					if (!inConference && !inConferenceCurr) {
						menuAttendedTransfer.InsertMenu(pos, MF_BYPOSITION, ID_ATTENDED_TRANSFER_RANGE + pos, str);
						MENUITEMINFO mii;
						mii.cbSize = sizeof(MENUITEMINFO);
						mii.fMask = MIIM_DATA;
						mii.dwItemData = call_ids[i];
						menuAttendedTransfer.SetMenuItemInfo(pos, &mii, TRUE);
						pos++;
					}
				}
				//--merge
				if (call_info.state == PJSIP_INV_STATE_CONFIRMED && call_info_curr.state == PJSIP_INV_STATE_CONFIRMED) {
					if (!inConference || !inConferenceCurr) {
						if (!inConferenceCurr || mergeConferenceAddedId == PJSUA_INVALID_ID) {
							MENUITEMINFO mii;
							mii.cbSize = sizeof(MENUITEMINFO);
							mii.fMask = MIIM_DATA;
							mii.dwItemData = call_ids[i];
							if (inConferenceCurr) {
								str = Translate(_T("Conference"));
								mergeConferenceAddedId = call_ids[i];
							}
							menuMerge.InsertMenu(posMerge, MF_BYPOSITION, ID_MERGE_RANGE + posMerge, str);
							menuMerge.SetMenuItemInfo(posMerge, &mii, TRUE);
							posMerge++;
						}
					}
				}
				//--
			}
		}
	}
	tracker->EnableMenuItem(ID_ATTENDED_TRANSFER, menuAttendedTransfer.GetMenuItemCount() ? 0 : MF_GRAYED);
	if (!inConference && mergeConferenceAddedId != PJSUA_INVALID_ID && menuMerge.GetMenuItemCount() > 1) {
		// only 1 conference allowed, remove all regular calls
		while (menuMerge.DeleteMenu(0, MF_BYPOSITION));
		MENUITEMINFO mii;
		mii.cbSize = sizeof(MENUITEMINFO);
		mii.fMask = MIIM_DATA;
		mii.dwItemData = mergeConferenceAddedId;
		menuMerge.InsertMenu(0, MF_BYPOSITION, ID_MERGE_RANGE, Translate(_T("Conference")));
		menuMerge.SetMenuItemInfo(0, &mii, TRUE);
	}
	tracker->EnableMenuItem(ID_MERGE, menuMerge.GetMenuItemCount() ? 0 : MF_GRAYED);
	tracker->EnableMenuItem(ID_MERGE_ALL, menuMerge.GetMenuItemCount() ? 0 : MF_GRAYED);
	//-- invite in conference
	tracker->EnableMenuItem(ID_CONFERENCE, call_info.state == PJSIP_INV_STATE_CONFIRMED && (inConference || mergeConferenceAddedId == PJSUA_INVALID_ID) ? 0 : MF_GRAYED);
	//-- separate & disconnect
	tracker->EnableMenuItem(ID_SEPARATE, inConference ? 0 : MF_GRAYED);
	tracker->EnableMenuItem(ID_SEPARATE_ALL, inConference ? 0 : MF_GRAYED);
	tracker->EnableMenuItem(ID_DISCONNECT, inConference ? 0 : MF_GRAYED);
	//--
	CPoint point;
	GetCursorPos(&point);
	tracker->TrackPopupMenu(0, point.x, point.y, this);
}

void MessagesDlg::OnTransfer()
{
	if (!mainDlg->transferDlg) {
		mainDlg->transferDlg = new Transfer(this);
		mainDlg->transferDlg->LoadFromContacts();
	}
	mainDlg->transferDlg->SetAction(MSIP_ACTION_TRANSFER);
	mainDlg->transferDlg->SetForegroundWindow();
}

void MessagesDlg::OnAttendedTransfer(UINT nID)
{
	int pos = nID - ID_ATTENDED_TRANSFER_RANGE;
	MENUITEMINFO mii;
	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_DATA;
	menuAttendedTransfer.GetMenuItemInfo(pos, &mii, TRUE);
	pjsua_call_id call_id = mii.dwItemData;

	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	if (!pjsua_call_is_active(call_id)) {
		return;
	}
	pjsua_call_xfer_replaces(messagesContact->callId, call_id, 0, 0);
}

void MessagesDlg::OnConference()
{
	if (!mainDlg->transferDlg) {
		mainDlg->transferDlg = new Transfer(this);
		mainDlg->transferDlg->LoadFromContacts();
	}
	mainDlg->transferDlg->SetAction(MSIP_ACTION_INVITE);
	mainDlg->transferDlg->SetForegroundWindow();
}

void MessagesDlg::OnMerge(UINT nID)
{
	int pos = nID - ID_MERGE_RANGE;
	MENUITEMINFO mii;
	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_DATA;
	menuMerge.GetMenuItemInfo(pos, &mii, TRUE);
	pjsua_call_id call_id = mii.dwItemData;
	Merge(call_id);
}

void MessagesDlg::OnMergeAll()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < calls_count; ++i) {
			if (call_ids[i] != messagesContact->callId) {
				Merge(call_ids[i]);
			}
		}
	}
}

void MessagesDlg::OnSeparate()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	Separate(messagesContact->callId);
}

void MessagesDlg::OnSeparateAll()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	pjsua_call_info call_info;
	pjsua_call_get_info(messagesContact->callId, &call_info);
	msip_call_hold(&call_info);
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < calls_count; ++i) {
			if (call_ids[i] != messagesContact->callId) {
				pjsua_call_info call_info;
				if (pjsua_call_get_info(call_ids[i], &call_info) == PJ_SUCCESS) {
					msip_conference_leave(&call_info);
				}
			}
		}
	}
}

void MessagesDlg::OnDisconnect()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	msip_call_hangup_fast(messagesContact->callId);
}

void MessagesDlg::OnBnClickedEnd()
{
	MessagesContact* messagesContact = GetMessageContact();
	if (!messagesContact || messagesContact->callId == -1) {
		return;
	}
	msip_call_end(messagesContact->callId);
}

void MessagesDlg::OnCloseAllTabs()
{
	int i = 0;
	while (i < tab->GetItemCount()) {
		if (CloseTab(i, TRUE)) {
			i = 0;
		}
		else {
			i++;
		}
	}
}

void MessagesDlg::OnGoToLastTab()
{
	int i = 0;
	BOOL found = FALSE;
	int lastCallIndex = -1;
	while (i < tab->GetItemCount()) {
		MessagesContact* messagesContact = GetMessageContact(i);
		if (messagesContact->callId != -1) {
			found = TRUE;
			if (tab->GetCurSel() != i) {
				LONG_PTR result;
				OnTcnSelchangingTab(NULL, &result);
				tab->SetCurSel(i);
				OnChangeTab();
				break;
			}
		}
		if (messagesContact == lastCall) {
			lastCallIndex = i;
		}
		i++;
	}
	if (found) {
		CButton* buttonHold = (CButton*)GetDlgItem(IDC_HOLD);
		if (buttonHold->IsWindowVisible() && buttonHold->GetCheck()) {
			OnBnClickedHold();
		}
	}
	if (!found && lastCallIndex != -1) {
		if (tab->GetCurSel() != lastCallIndex) {
			LONG_PTR result;
			OnTcnSelchangingTab(NULL, &result);
			tab->SetCurSel(lastCallIndex);
			OnChangeTab();
		}
	}
}

int MessagesDlg::GetCallDuration(pjsua_call_id *call_id)
{
	int duration = -1;
	pjsua_call_info call_info;
	int i = 0;
	int count = 0;
	while (i < tab->GetItemCount()) {
		MessagesContact* messagesContact = GetMessageContact(i);
		if (messagesContact->callId != -1) {
			if (pjsua_var.state == PJSUA_STATE_RUNNING && pjsua_call_get_info(messagesContact->callId, &call_info) == PJ_SUCCESS) {
				if (call_info.state == PJSIP_INV_STATE_CONFIRMED) {
					duration = msip_get_duration(&call_info.connect_duration);
					*call_id = messagesContact->callId;
					count++;
				}
			}
		}
		i++;
	}
	if (count > 1) {
		*call_id = PJSUA_INVALID_ID;
		duration = count;
	}
	return duration;
}

void MessagesDlg::OnCopy()
{
	CRichEditCtrl* richEditList = (CRichEditCtrl*)GetDlgItem(IDC_LIST);
	richEditList->Copy();
}

void MessagesDlg::OnSelectAll()
{
	CRichEditCtrl* richEditList = (CRichEditCtrl*)GetDlgItem(IDC_LIST);
	richEditList->SetSel(0, -1);
}

void MessagesDlg::OnBnClickedCloseAll()
{
	OnCloseAllTabs();
}

void MessagesDlg::OnBnClickedLastCall()
{
	OnGoToLastTab();
}

void MessagesDlg::UpdateTabIcon(MessagesContact* messagesContact, int tabIndex, pjsua_call_info *p_call_info, call_user_data *user_data)
{
	if (tabIndex == -1) {
		for (int i = 0; i < tab->GetItemCount(); i++) {
			if (messagesContact == GetMessageContact(i)) {
				tabIndex = i;
				break;
			}
		}
	}
	if (tabIndex == -1) {
		return;
	}
	int icon = -1;
	if (messagesContact->hasNewMessages) {
		icon = MSIP_TAB_ICON_MESSAGE_IN;
	}
	else if (p_call_info) {
		//-----------------
		switch (p_call_info->state) {
		case PJSIP_INV_STATE_NULL:
		case PJSIP_INV_STATE_DISCONNECTED:
			if (p_call_info->role == PJSIP_ROLE_UAS && !p_call_info->connect_duration.sec && !p_call_info->connect_duration.msec) {
				icon = MSIP_TAB_ICON_CALL_MISS;
			}
			break;
		case PJSIP_INV_STATE_CONFIRMED:
			if (p_call_info->media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD || p_call_info->media_status == PJSUA_CALL_MEDIA_NONE) {
				icon = MSIP_TAB_ICON_ON_HOLD;
			}
			else {
				if (!user_data) {
					user_data = (call_user_data *)pjsua_call_get_user_data(p_call_info->id);
				}
				if (user_data) {
					user_data->CS.Lock();
					if (p_call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
						if (user_data && user_data->inConference) {
							icon = MSIP_TAB_ICON_ON_REMOTE_HOLD_CONFERENCE;
						}
						else {
							icon = MSIP_TAB_ICON_ON_REMOTE_HOLD;
						}
					}
					else {
						if (user_data && user_data->inConference) {
							if (user_data->srtp == MSIP_SRTP) {
								icon = MSIP_TAB_ICON_CONFERENCE_SECURE;
							}
							else {
								icon = MSIP_TAB_ICON_CONFERENCE;
							}
						}
						else {
							if (user_data && user_data->srtp == MSIP_SRTP) {
								icon = MSIP_TAB_ICON_ACTIVE_SECURE;
							}
							else {
								icon = MSIP_TAB_ICON_ACTIVE;
							}
						}
					}
					user_data->CS.Unlock();
				}
				else {
					if (p_call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
						icon = MSIP_TAB_ICON_ON_REMOTE_HOLD;
					}
					else {
						icon = MSIP_TAB_ICON_ACTIVE;
					}
				}
			}
			break;
		default:
			if (p_call_info->role == PJSIP_ROLE_UAS) {
				icon = MSIP_TAB_ICON_CALL_IN;
			}
			else {
				icon = MSIP_TAB_ICON_CALL_OUT;
			}
			break;
		}
		//-----------------
	}
	TCITEM item;
	item.mask = TCIF_IMAGE;
	item.iImage = icon;
	tab->SetItem(tabIndex, &item);
}
