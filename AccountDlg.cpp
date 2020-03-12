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
#include "AccountDlg.h"
#include "mainDlg.h"
#include "langpack.h"
#include "atlrx.h"

#include <ws2tcpip.h>

AccountDlg::AccountDlg(CWnd* pParent /*=NULL*/)
: CDialog(AccountDlg::IDD, pParent)
{
	accountId = -1;
	Create (IDD, pParent);

}

AccountDlg::~AccountDlg(void)
{
}

int AccountDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (langPack.rtl) {
		ModifyStyleEx(0,WS_EX_LAYOUTRTL);
	}
	return 0;
}

BOOL AccountDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	TranslateDialog(this->m_hWnd);

	CString str;

	str.Format(_T("<a>%s</a>"),Translate(_T("display password")));
	GetDlgItem(IDC_SYSLINK_DISPLAY_PASSWORD)->SetWindowText(str);

	CComboBox *combobox;

	combobox= (CComboBox*)GetDlgItem(IDC_TRANSPORT);
	str.Format(_T("%s (UDP & TCP)"), Translate(_T("Auto")));
	combobox->AddString(str);
	combobox->AddString(_T("UDP"));
	combobox->AddString(_T("TCP"));
	combobox->AddString(_T("TLS"));
	combobox->SetCurSel(0);

	combobox= (CComboBox*)GetDlgItem(IDC_SRTP);
	combobox->AddString(Translate(_T("Disabled")));
	str.Format(_T("%s SRTP (RTP/AVP)"), Translate(_T("Optional")));
	combobox->AddString(str);
	str.Format(_T("%s SRTP (RTP/SAVP)"), Translate(_T("Mandatory")));
	combobox->AddString(str);
	combobox->SetCurSel(0);

	CEdit* edit;

	combobox= (CComboBox*)GetDlgItem(IDC_PUBLIC_ADDR);
	combobox->AddString(Translate(_T("Auto")));
	char buf[256]={0};
	if ( gethostname(buf, 256) == 0) {
		struct addrinfo* l_addrInfo = NULL;
		struct addrinfo l_addrInfoHints;
		ZeroMemory(&l_addrInfoHints, sizeof(addrinfo));
		l_addrInfoHints.ai_socktype = SOCK_STREAM;
		l_addrInfoHints.ai_family = PF_INET;
		if ( getaddrinfo(buf,NULL, &l_addrInfoHints,&l_addrInfo) == 0 ) {
			if (l_addrInfo) {
				struct addrinfo* l_addrInfoCurrent = l_addrInfo;
				for (l_addrInfoCurrent = l_addrInfo; l_addrInfoCurrent; l_addrInfoCurrent=l_addrInfoCurrent->ai_next) {
					struct sockaddr_in *ipv4 = (struct sockaddr_in *)l_addrInfoCurrent->ai_addr;
					char * ip = inet_ntoa(ipv4->sin_addr);
					combobox->AddString(CString(ip));
				}
			}
		}
	}
	combobox->SetCurSel(0);
	if (accountSettings.enableSTUN && !accountSettings.stun.IsEmpty()) {
		combobox->EnableWindow(FALSE);
	}

	return TRUE;
}

void AccountDlg::OnDestroy()
{
	mainDlg->accountDlg = NULL;
	CDialog::OnDestroy();
}

void AccountDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	delete this;
}

BEGIN_MESSAGE_MAP(AccountDlg, CDialog)
	ON_WM_CREATE()
	ON_WM_SYSCOMMAND()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDCANCEL, &AccountDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDOK, &AccountDlg::OnBnClickedOk)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_SIP_SERVER, &AccountDlg::OnNMClickSyslinkSipServer)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_SIP_PROXY, &AccountDlg::OnNMClickSyslinkSipProxy)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_USERNAME, &AccountDlg::OnNMClickSyslinkUsername)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_DOMAIN, &AccountDlg::OnNMClickSyslinkDomain)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_AUTHID, &AccountDlg::OnNMClickSyslinkAuthID)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_PASSWORD, &AccountDlg::OnNMClickSyslinkPassword)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_NAME, &AccountDlg::OnNMClickSyslinkName)
	ON_NOTIFY(NM_CLICK, IDC_ACCOUNT_HELP_DIALING_PREFIX, &AccountDlg::OnNMClickSyslinkDialingPrefix)
	ON_NOTIFY(NM_CLICK, IDC_ACCOUNT_HELP_DIAL_PLAN, &AccountDlg::OnNMClickSyslinkDialPlan)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_VOICEMAIL, &AccountDlg::OnNMClickSyslinkVoicemail)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_ENCRYPTION, &AccountDlg::OnNMClickSyslinkEncryption)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_TRANSPORT, &AccountDlg::OnNMClickSyslinkTransport)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_PUBLIC_ADDRESS, &AccountDlg::OnNMClickSyslinkPublicAddress)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_PUBLISH_PRESENCE, &AccountDlg::OnNMClickSyslinkPublishPresence)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_ICE, &AccountDlg::OnNMClickSyslinkIce)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_REWRITE, &AccountDlg::OnNMClickSyslinkRewrite)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_SESSION_TIMER, &AccountDlg::OnNMClickSyslinkSessionTimer)
	ON_NOTIFY(NM_CLICK, IDC_SYSLINK_DISPLAY_PASSWORD, &AccountDlg::OnNMClickSyslinkDisplayPasswod)
	ON_NOTIFY(NM_RETURN, IDC_SYSLINK_DISPLAY_PASSWORD, &AccountDlg::OnNMClickSyslinkDisplayPasswod)
	ON_BN_CLICKED(IDC_ACCOUNT_REMOVE, &AccountDlg::OnBnClickedDelete)

	
END_MESSAGE_MAP()

void AccountDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	__super::OnSysCommand(nID, lParam);
}

void AccountDlg::OnClose() 
{
	DestroyWindow();
}

void AccountDlg::OnBnClickedCancel()
{
	OnClose();
}

void AccountDlg::Load(int id)
{
	CEdit* edit;
	CComboBox *combobox;
	CString str;

	int show = id ? SW_SHOW : SW_HIDE;
	GetDlgItem(IDC_ACCOUNT_REQUIRED_USERNAME)->ShowWindow(show);
	GetDlgItem(IDC_ACCOUNT_REQUIRED_DOMAIN)->ShowWindow(show);
	GetDlgItem(IDC_EDIT_SERVER)->EnableWindow(id);
	GetDlgItem(IDC_PUBLISH)->EnableWindow(id);
	GetDlgItem(IDC_ACCOUNT_REGISTER_REFRESH)->EnableWindow(id);
	GetDlgItem(IDC_ACCOUNT_KEEP_ALIVE)->EnableWindow(id);
	GetDlgItem(IDC_REWRITE)->EnableWindow(id);

	accountId = id;
	if (accountSettings.AccountLoad(id,&m_Account)) {
		accountId = id;
		if (accountId  && accountSettings.accountId == accountId && !accountSettings.account.rememberPassword) {
			m_Account.username = accountSettings.account.username;
			m_Account.password = accountSettings.account.password;
			m_Account.rememberPassword = false;
		}
	} else {
		accountId = -1;
	}

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_LABEL);
	edit->SetWindowText(m_Account.label);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_SERVER);
	edit->SetWindowText(m_Account.server);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_PROXY);
	edit->SetWindowText(m_Account.proxy);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_DOMAIN);
	edit->SetWindowText(m_Account.domain);


	edit = (CEdit*)GetDlgItem(IDC_EDIT_AUTHID);
	edit->SetWindowText(m_Account.authID);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_USERNAME);
	edit->SetWindowText(m_Account.username);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_PASSWORD);
	edit->SetWindowText(m_Account.password);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_DISPLAYNAME);
	edit->SetWindowText(m_Account.displayName);

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_DIALING_PREFIX);
	edit->SetWindowText(m_Account.dialingPrefix);

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_DIAL_PLAN);
	edit->SetWindowText(m_Account.dialPlan);

	((CButton*)GetDlgItem(IDC_ACCOUNT_HIDE_CID))->SetCheck(m_Account.hideCID);

	edit = (CEdit*)GetDlgItem(IDC_EDIT_VOICEMAIL);
	edit->SetWindowText(m_Account.voicemailNumber);

int i;

	combobox= (CComboBox*)GetDlgItem(IDC_TRANSPORT);
	if (m_Account.transport==_T("udp")) {
		i=1;
	} else if (m_Account.transport==_T("tcp")) {
		i=2;
	} else if (m_Account.transport==_T("tls")) {
		i=3;
	} else {
		i=0;
	}
	if (i>0) {
		combobox->SetCurSel(i);
	}

	combobox= (CComboBox*)GetDlgItem(IDC_SRTP);
	if (m_Account.srtp==_T("optional")) {
		i=1;
	} else if (m_Account.srtp==_T("mandatory")) {
		i=2;
	}
	else {
		i=0;
	}
	if (i>0) {
		combobox->SetCurSel(i);
	}

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_REGISTER_REFRESH);
	str.Format(_T("%d"), m_Account.registerRefresh);
	edit->SetWindowText(str);

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_KEEP_ALIVE);
	str.Format(_T("%d"), m_Account.keepAlive);
	edit->SetWindowText(str);

	combobox= (CComboBox*)GetDlgItem(IDC_PUBLIC_ADDR);
	if (combobox->IsWindowEnabled()) {
		if (m_Account.publicAddr.GetLength()) {
			combobox->SetWindowText(m_Account.publicAddr);
		}
	}

	((CButton*)GetDlgItem(IDC_PUBLISH))->SetCheck(m_Account.publish);

	((CButton*)GetDlgItem(IDC_REWRITE))->SetCheck(m_Account.allowRewrite);

	((CButton*)GetDlgItem(IDC_ICE))->SetCheck(m_Account.ice);

	((CButton*)GetDlgItem(IDC_SESSION_TIMER))->SetCheck(m_Account.disableSessionTimer);
	if (accountId>0 && (!m_Account.username.IsEmpty() || accountId>1)) {
		GetDlgItem(IDC_ACCOUNT_REMOVE)->ShowWindow(SW_SHOW);
	} else {
		GetDlgItem(IDC_ACCOUNT_REMOVE)->ShowWindow(SW_HIDE);
	}
}

void AccountDlg::OnBnClickedOk()
{
	CEdit* edit;
	CString str;
	CComboBox *combobox;
	int i;

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_LABEL);
	edit->GetWindowText(str);
	m_Account.label=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_SERVER);
	edit->GetWindowText(str);
	m_Account.server=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_PROXY);
	edit->GetWindowText(str);
	m_Account.proxy=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_DOMAIN);
	edit->GetWindowText(str);
	m_Account.domain=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_AUTHID);
	edit->GetWindowText(str);
	m_Account.authID=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_USERNAME);
	edit->GetWindowText(str);
	m_Account.username=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_PASSWORD);
	edit->GetWindowText(str);
	m_Account.password=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_DISPLAYNAME);
	edit->GetWindowText(str);
	m_Account.displayName=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_DIALING_PREFIX);
	edit->GetWindowText(str);
	m_Account.dialingPrefix=str.Trim();

	edit = (CEdit*)GetDlgItem(IDC_ACCOUNT_DIAL_PLAN);
	edit->GetWindowText(str);
	m_Account.dialPlan = str.Trim();

	m_Account.hideCID = ((CButton*)GetDlgItem(IDC_ACCOUNT_HIDE_CID))->GetCheck();

	edit = (CEdit*)GetDlgItem(IDC_EDIT_VOICEMAIL);
	edit->GetWindowText(str);
	m_Account.voicemailNumber=str.Trim();

	combobox= (CComboBox*)GetDlgItem(IDC_TRANSPORT);
	i = combobox->GetCurSel();
	switch (i) {
		case 1:
			m_Account.transport=_T("udp");
			break;
		case 2:
			m_Account.transport=_T("tcp");
			break;
		case 3:
			m_Account.transport=_T("tls");
			break;
		default:
			m_Account.transport=_T("");
	}

	m_Account.rememberPassword = 1;

	combobox= (CComboBox*)GetDlgItem(IDC_SRTP);
	i = combobox->GetCurSel();
	switch (i) {
		case 1:
			m_Account.srtp=_T("optional");
			break;
		case 2:
			m_Account.srtp=_T("mandatory");
			break;
		default:
			m_Account.srtp=_T("");
	}

	GetDlgItem(IDC_ACCOUNT_REGISTER_REFRESH)->GetWindowText(str);
	m_Account.registerRefresh = _wtoi(str);
	if (m_Account.registerRefresh <= 0) {
		m_Account.registerRefresh = PJSUA_REG_INTERVAL;
	}

	GetDlgItem(IDC_ACCOUNT_KEEP_ALIVE)->GetWindowText(str);
	m_Account.keepAlive = _wtoi(str);
	if (m_Account.keepAlive < 0) {
		m_Account.keepAlive = 15;
	}

	m_Account.publish = ((CButton*)GetDlgItem(IDC_PUBLISH))->GetCheck();

	m_Account.allowRewrite = ((CButton*)GetDlgItem(IDC_REWRITE))->GetCheck();

	combobox= (CComboBox*)GetDlgItem(IDC_PUBLIC_ADDR);
	if (combobox->IsWindowEnabled()) {
		i = combobox->GetCurSel();
		combobox->GetWindowText(m_Account.publicAddr);
		if (m_Account.publicAddr == Translate(_T("Auto")))
		{
			m_Account.publicAddr = _T("");
		}
	}

	m_Account.ice = ((CButton*)GetDlgItem(IDC_ICE))->GetCheck();

	m_Account.disableSessionTimer = ((CButton*)GetDlgItem(IDC_SESSION_TIMER))->GetCheck();

	if (
		m_Account.domain.IsEmpty() ||
		m_Account.username.IsEmpty()) {
		if (accountId != 0) {
			CString str;
			str.Append(Translate(_T("Please fill out at least the required fields marked with *.")));
			str.AppendFormat(_T(" %s"), Translate(_T("Ask your SIP provider how to configure the account correctly.")));
			AfxMessageBox(str);
			return;
		}
	}


	this->ShowWindow(SW_HIDE);
	mainDlg->accountDlg = NULL;

	if (accountId == -1) { // find id for new account
		Account dummy;
		int i = 1;
		while (true) {
			if (!accountSettings.AccountLoad(i, &dummy)) {
				break;
			}
			i++;
		}
		accountId = i;
	}

	accountSettings.AccountSave(accountId, &m_Account);

	if (accountId) {
		mainDlg->PJAccountDelete(true);

		accountSettings.accountId = accountId;
		accountSettings.account = m_Account;
		accountSettings.AccountLoad(accountSettings.accountId, &accountSettings.account);
		if (!m_Account.rememberPassword) {
			accountSettings.account.username = m_Account.username;
			accountSettings.account.password = m_Account.password;
			accountSettings.account.rememberPassword = false;
		}
		mainDlg->OnAccountChanged();
		accountSettings.SettingsSave();
		mainDlg->PJAccountAdd();
	} else {
		// local account
		mainDlg->PJAccountDeleteLocal();
		accountSettings.AccountLoad(0, &accountSettings.accountLocal);
		mainDlg->PJAccountAddLocal();
	}
	OnClose();
}

void AccountDlg::OnNMClickSyslinkSipServer(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("sipServer"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkSipProxy(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("sipProxy"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkUsername(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("username"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkDomain(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("domain"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkAuthID(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("login"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkPassword(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("password"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkName(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("name"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkDialingPrefix(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("dialingPrefix"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkDialPlan(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("dialPlan"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkVoicemail(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("voicemail"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkEncryption(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("encryption"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkTransport(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("transport"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkPublicAddress(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("publicAddress"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkPublishPresence(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("publishPresence"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkIce(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("ice"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkRewrite(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("allowRewrite"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkSessionTimer(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("sessionTimers"));
	*pResult = 0;
}

void AccountDlg::OnNMClickSyslinkDisplayPasswod(NMHDR *pNMHDR, LRESULT *pResult)
{
	GetDlgItem(IDC_SYSLINK_DISPLAY_PASSWORD)->ShowWindow(SW_HIDE);
	CEdit* edit = (CEdit*)GetDlgItem(IDC_EDIT_PASSWORD);
	edit->SetPasswordChar(0);
	edit->Invalidate();
	edit->SetFocus();
	int nLength = edit->GetWindowTextLength();
	edit->SetSel(nLength,nLength);
	*pResult = 0;
}

void AccountDlg::OnBnClickedDelete()
{
	if (accountId>0 && AfxMessageBox(Translate(_T("Are you sure you want to remove?")), MB_YESNO)==IDYES) {
		this->ShowWindow(SW_HIDE);
		mainDlg->accountDlg = NULL;

		Account account;
		int i = accountId;
		while (true) {
			if (!accountSettings.AccountLoad(i+1,&account)) {
				break;
			}
			accountSettings.AccountSave(i,&account);
			i++;
		}
		accountSettings.AccountDelete(i);
		if (accountId && accountId == accountSettings.accountId) {
			mainDlg->PJAccountDelete(true);
			if (i>1) {
				accountSettings.accountId = 1;
				accountSettings.AccountLoad(accountSettings.accountId,&accountSettings.account);
				mainDlg->OnAccountChanged();
				mainDlg->InitUI();
				mainDlg->PJAccountAdd();
			} else {
				accountSettings.accountId = 0;
				mainDlg->OnAccountChanged();
				mainDlg->InitUI();
			}
			accountSettings.SettingsSave();
		}
		OnClose();
	}
}

