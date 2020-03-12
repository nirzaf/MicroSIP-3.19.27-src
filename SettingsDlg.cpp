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
#include "SettingsDlg.h"
#include "mainDlg.h"
#include "settings.h"
#include "Preview.h"
#include "langpack.h"
#include <afxshellmanager.h>

static CString defaultActionItems[] = {
_T(""),
_T("call"),
#ifdef _GLOBAL_VIDEO
_T("video"),
#endif
_T("message"),
};
static CString defaultActionValues[] = {
_T("Default"),
_T("Call"),
#ifdef _GLOBAL_VIDEO
_T("Video Call"),
#endif
_T("Message"),
};

SettingsDlg::SettingsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(SettingsDlg::IDD, pParent)
{
	Create(IDD, pParent);
}

SettingsDlg::~SettingsDlg(void)
{
}

int SettingsDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (langPack.rtl) {
		ModifyStyleEx(0, WS_EX_LAYOUTRTL);
	}
	return 0;
}

BOOL SettingsDlg::OnInitDialog()
{
	CComboBox *combobox;
	CComboBox *combobox2;
	unsigned count;
	int i;
	int n;
	bool found;
	CString str;

	CDialog::OnInitDialog();

	TranslateDialog(this->m_hWnd);

	GetDlgItem(IDC_SETTINGS_RINGTONE)->SetWindowText(accountSettings.ringtone);
	((CSliderCtrl*)GetDlgItem(IDC_SETTINGS_VOLUME_RING))->SetRange(0, 100);
	((CSliderCtrl*)GetDlgItem(IDC_SETTINGS_VOLUME_RING))->SetPos(accountSettings.volumeRing);
	GetDlgItem(IDC_SETTINGS_RECORDING)->SetWindowText(accountSettings.recordingPath);
	if (accountSettings.recordingFormat == _T("wav")) {
		CheckRadioButton(IDC_SETTINGS_RECORDING_MP3, IDC_SETTINGS_RECORDING_WAV, IDC_SETTINGS_RECORDING_WAV);
	}
	else {
		CheckRadioButton(IDC_SETTINGS_RECORDING_MP3, IDC_SETTINGS_RECORDING_WAV, IDC_SETTINGS_RECORDING_MP3);
	}
	((CButton*)GetDlgItem(IDC_SETTINGS_RECORDING_CHECKBOX))->SetCheck(accountSettings.autoRecording);
	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_MICROPHONE);
	combobox->AddString(Translate(_T("Default")));
	combobox->SetCurSel(0);

	pjmedia_aud_dev_info aud_dev_info[PJMEDIA_AUD_MAX_DEVS];
	count = PJMEDIA_AUD_MAX_DEVS;
	pjsua_enum_aud_devs(aud_dev_info, &count);
	for (unsigned i = 0; i < count; i++)
	{
		if (aud_dev_info[i].input_count) {
			CString audDevName = PjStrToWideChar(aud_dev_info[i].name);
			combobox->AddString(audDevName);
			if (!accountSettings.audioInputDevice.Compare(audDevName))
			{
				combobox->SetCurSel(combobox->GetCount() - 1);
			}
		}
	}
	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_SPEAKERS);
	combobox->AddString(Translate(_T("Default")));
	combobox->SetCurSel(0);
	combobox2 = (CComboBox*)GetDlgItem(IDC_SETTINGS_RING);
	combobox2->AddString(Translate(_T("Default")));
	combobox2->SetCurSel(0);
	for (unsigned i = 0; i < count; i++)
	{
		if (aud_dev_info[i].output_count) {
			CString audDevName = PjStrToWideChar(aud_dev_info[i].name);
			combobox->AddString(audDevName);
			combobox2->AddString(audDevName);
			if (!accountSettings.audioOutputDevice.Compare(audDevName))
			{
				combobox->SetCurSel(combobox->GetCount() - 1);
			}
			if (!accountSettings.audioRingDevice.Compare(audDevName))
			{
				combobox2->SetCurSel(combobox->GetCount() - 1);
			}
		}
	}
	((CButton*)GetDlgItem(IDC_SETTINGS_MIC_AMPLIF))->SetCheck(accountSettings.micAmplification);
	((CButton*)GetDlgItem(IDC_SETTINGS_SW_ADJUST))->SetCheck(accountSettings.swLevelAdjustment);

	pjsua_codec_info codec_info[PJMEDIA_CODEC_MGR_MAX_CODECS];
	CListBox *listbox;
	listbox = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS_ALL);
	CListBox *listbox2;
	listbox2 = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS);

	CList<CString> disabledCodecsList;
	count = PJMEDIA_CODEC_MGR_MAX_CODECS;
	pjsua_enum_codecs(codec_info, &count);
	for (unsigned i = 0; i < count; i++)
	{
			POSITION pos = mainDlg->audioCodecList.Find(
				PjToStr(&codec_info[i].codec_id)
			);
			CString key = mainDlg->audioCodecList.GetNext(pos);
			CString value = mainDlg->audioCodecList.GetNext(pos);
			if (codec_info[i].priority
				&& (!accountSettings.audioCodecs.IsEmpty() || StrStr(_T(_GLOBAL_CODECS_ENABLED), key))
				) {
				listbox2->AddString(value);
			}
			else {
				disabledCodecsList.AddTail(key);
			}
	}
	POSITION pos = mainDlg->audioCodecList.GetHeadPosition();
	while (pos) {
		CString key = mainDlg->audioCodecList.GetNext(pos);
		CString value = mainDlg->audioCodecList.GetNext(pos);
		if (disabledCodecsList.Find(key)) {
			listbox->AddString(value);
		}
	}

	((CButton*)GetDlgItem(IDC_SETTINGS_VAD))->SetCheck(accountSettings.vad);
	((CButton*)GetDlgItem(IDC_SETTINGS_EC))->SetCheck(accountSettings.ec);
	((CButton*)GetDlgItem(IDC_SETTINGS_FORCE_CODEC))->SetCheck(accountSettings.forceCodec);

#ifdef _GLOBAL_VIDEO
	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_VID_CAP_DEV);
	combobox->AddString(Translate(_T("Default")));
	combobox->SetCurSel(0);

	count = PJMEDIA_VID_DEV_MAX_DEVS;
	pjmedia_vid_dev_info vid_dev_info[PJMEDIA_VID_DEV_MAX_DEVS];
	pjsua_vid_enum_devs(vid_dev_info, &count);
	for (unsigned i = 0; i < count; i++)
	{
		if (vid_dev_info[i].fmt_cnt && (vid_dev_info[i].dir == PJMEDIA_DIR_ENCODING || vid_dev_info[i].dir == PJMEDIA_DIR_ENCODING_DECODING))
		{
			CString vidDevName = PjStrToWideChar(vid_dev_info[i].name);
			combobox->AddString(vidDevName);
			if (!accountSettings.videoCaptureDevice.Compare(vidDevName))
			{
				combobox->SetCurSel(combobox->GetCount() - 1);
			}
		}
	}

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_VIDEO_CODEC);
	combobox->AddString(Translate(_T("Default")));
	combobox->SetCurSel(0);
	count = PJMEDIA_CODEC_MGR_MAX_CODECS;
	pjsua_vid_enum_codecs(codec_info, &count);
	for (unsigned i = 0; i < count; i++)
	{
		combobox->AddString(PjToStr(&codec_info[i].codec_id));
		if (!accountSettings.videoCodec.Compare(PjToStr(&codec_info[i].codec_id)))
		{
			combobox->SetCurSel(combobox->GetCount() - 1);
		}
	}

	((CButton*)GetDlgItem(IDC_SETTINGS_VIDEO_H264))->SetCheck(accountSettings.videoH264);
	((CButton*)GetDlgItem(IDC_SETTINGS_VIDEO_H263))->SetCheck(accountSettings.videoH263);
	((CButton*)GetDlgItem(IDC_SETTINGS_VIDEO_VP8))->SetCheck(accountSettings.videoVP8);
	if (!accountSettings.videoBitrate) {
		const pj_str_t codec_id = { "H264", 4 };
		pjmedia_vid_codec_param param;
		pjsua_vid_codec_get_param(&codec_id, &param);
		accountSettings.videoBitrate = param.enc_fmt.det.vid.max_bps / 1000;
	}
	str.Format(_T("%d"), accountSettings.videoBitrate);
	GetDlgItem(IDC_SETTINGS_VIDEO_BITRATE)->SetWindowText(str);

#endif

	((CButton*)GetDlgItem(IDC_SETTINGS_RPORT))->SetCheck(accountSettings.rport);
	str.Format(_T("%d"), accountSettings.sourcePort);
	GetDlgItem(IDC_SETTINGS_SOURCE_PORT)->SetWindowText(str);
	str.Format(_T("%d"), accountSettings.rtpPortMin);
	GetDlgItem(IDC_SETTINGS_RTP_PORT_MIN)->SetWindowText(str);
	str.Format(_T("%d"), accountSettings.rtpPortMax);
	GetDlgItem(IDC_SETTINGS_RTP_PORT_MAX)->SetWindowText(str);

	GetDlgItem(IDC_SETTINGS_DNS_SRV_NS)->SetWindowText(accountSettings.dnsSrvNs);
	((CButton*)GetDlgItem(IDC_SETTINGS_DNS_SRV_CHECKBOX))->SetCheck(accountSettings.dnsSrv);

	GetDlgItem(IDC_SETTINGS_STUN)->SetWindowText(accountSettings.stun);
	((CButton*)GetDlgItem(IDC_SETTINGS_STUN_CHECKBOX))->SetCheck(accountSettings.enableSTUN);

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_DTMF_METHOD);
	combobox->AddString(Translate(_T("Auto")));
	combobox->AddString(Translate(_T("In-band")));
	combobox->AddString(Translate(_T("RFC2833")));
	combobox->AddString(Translate(_T("SIP-INFO")));
	combobox->SetCurSel(accountSettings.DTMFMethod);

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_AUTO_ANSWER);
	combobox->AddString(Translate(_T("No")));
	autoAnswerValues.Add(_T(""));
	combobox->AddString(Translate(_T("Control Button")));
	autoAnswerValues.Add(_T("button"));
	combobox->AddString(Translate(_T("SIP Header")));
	autoAnswerValues.Add(_T("header"));
	combobox->AddString(Translate(_T("All Calls")));
	autoAnswerValues.Add(_T("all"));
	combobox->SetCurSel(0);
	for (i = 0; i < autoAnswerValues.GetCount(); i++) {
		if (accountSettings.autoAnswer == autoAnswerValues.GetAt(i)) {
			combobox->SetCurSel(i);
			break;
		}
	}

	str.Format(_T("%d"), accountSettings.autoAnswerDelay);
	GetDlgItem(IDC_SETTINGS_AUTO_ANSWER_DELAY)->SetWindowText(str);

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_DENY_INCOMING);
	combobox->AddString(Translate(_T("No")));
	denyIncomingValues.Add(_T(""));
	combobox->AddString(Translate(_T("Control Button")));
	denyIncomingValues.Add(_T("button"));
	combobox->AddString(Translate(_T("Different User")));
	denyIncomingValues.Add(_T("user"));
	combobox->AddString(Translate(_T("Different Domain")));
	denyIncomingValues.Add(_T("domain"));
	combobox->AddString(Translate(_T("Different User or Domain")));
	denyIncomingValues.Add(_T("userdomain"));
	combobox->AddString(Translate(_T("Different Remote Domain")));
	denyIncomingValues.Add(_T("remotedomain"));
	combobox->AddString(Translate(_T("All Calls")));
	denyIncomingValues.Add(_T("all"));
	combobox->SetCurSel(0);
	for (i = 0; i < denyIncomingValues.GetCount(); i++) {
		if (accountSettings.denyIncoming == denyIncomingValues.GetAt(i)) {
			combobox->SetCurSel(i);
			break;
		}
	}

	GetDlgItem(IDC_SETTINGS_DIRECTORY)->SetWindowText(accountSettings.usersDirectory);

	combobox= (CComboBox*)GetDlgItem(IDC_SETTINGS_DEFAULT_ACTION);
	n = sizeof(defaultActionItems)/sizeof(defaultActionItems[0]);
	found = false;
	for (int i=0;i<n;i++) {
		combobox->AddString(Translate(defaultActionValues[i].GetBuffer()));
		if (accountSettings.defaultAction==defaultActionItems[i]) {
			combobox->SetCurSel(i);
			found = true;
		}
	}
	if (!found)  {
		combobox->SetCurSel(0);
	}


	((CButton*)GetDlgItem(IDC_SETTINGS_MEDIA_BUTTONS))->SetCheck(accountSettings.enableMediaButtons);
	((CButton*)GetDlgItem(IDC_SETTINGS_LOCAL_DTMF))->SetCheck(accountSettings.localDTMF);
	((CButton*)GetDlgItem(IDC_SETTINGS_SINGLE_MODE))->SetCheck(accountSettings.singleMode);
	((CButton*)GetDlgItem(IDC_SETTINGS_ENABLE_LOG))->SetCheck(accountSettings.enableLog);
	((CButton*)GetDlgItem(IDC_SETTINGS_BRING_TO_FRONT))->SetCheck(accountSettings.bringToFrontOnIncoming);
	((CButton*)GetDlgItem(IDC_SETTINGS_ANSWER_BOX_RANDOM))->SetCheck(accountSettings.randomAnswerBox);
	((CButton*)GetDlgItem(IDC_SETTINGS_CALL_WAITING))->SetCheck(accountSettings.callWaiting);
	((CButton*)GetDlgItem(IDC_SETTINGS_ENABLE_LOCAL))->SetCheck(accountSettings.enableLocalAccount);

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_UPDATES_INTERVAL);
	combobox->AddString(Translate(_T("Daily")));
	combobox->AddString(Translate(_T("Weekly")));
	combobox->AddString(Translate(_T("Monthly")));
	combobox->AddString(Translate(_T("Quarterly")));
	combobox->AddString(Translate(_T("Never")));
	if (accountSettings.updatesInterval == _T("daily"))
	{
		i = 0;
	}
	else if (accountSettings.updatesInterval == _T("monthly"))
	{
		i = 2;
	}
	else if (accountSettings.updatesInterval == _T("quarterly"))
	{
		i = 3;
	}
	else if (accountSettings.updatesInterval == _T("never"))
	{
		i = 4;
	}
	else
	{
		i = 1;
	}
	combobox->SetCurSel(i);

	return TRUE;
}

void SettingsDlg::OnDestroy()
{
	mainDlg->settingsDlg = NULL;
	CDialog::OnDestroy();
}

void SettingsDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	delete this;
}

BEGIN_MESSAGE_MAP(SettingsDlg, CDialog)
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDCANCEL, &SettingsDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDOK, &SettingsDlg::OnBnClickedOk)
	ON_MESSAGE(UM_UPDATE_SETTINGS, &SettingsDlg::OnUpdateSettings)
	ON_WM_VKEYTOITEM()
	ON_NOTIFY(UDN_DELTAPOS, IDC_SETTINGS_SPIN_MODIFY, &SettingsDlg::OnDeltaposSpinModify)
	ON_NOTIFY(UDN_DELTAPOS, IDC_SETTINGS_SPIN_ORDER, &SettingsDlg::OnDeltaposSpinOrder)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_RINGTONE, &SettingsDlg::OnNMClickSyslinkRingtone)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_MIC_AMPLIF, &SettingsDlg::OnNMClickSyslinkMicAmplif)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_SW_ADJUST, &SettingsDlg::OnNMClickSyslinkSwAdjust)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_DTMF_METHOD, &SettingsDlg::OnNMClickSyslinkDTMFMethod)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_AUTO_ANSWER, &SettingsDlg::OnNMClickSyslinkAutoAnswer)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_DENY_INCOMING, &SettingsDlg::OnNMClickSyslinkDenyIncoming)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_DIRECTORY, &SettingsDlg::OnNMClickSyslinkDirectory)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_DNS_SRV, &SettingsDlg::OnNMClickSyslinkDnsSrv)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_STUN_SERVER, &SettingsDlg::OnNMClickSyslinkStunServer)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_MEDIA_BUTTONS, &SettingsDlg::OnNMClickSyslinkMediaButtons)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_LOCAL_DTMF, &SettingsDlg::OnNMClickSyslinkLocalDTMF)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_SINGLE_MODE, &SettingsDlg::OnNMClickSyslinkSingleMode)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_VAD, &SettingsDlg::OnNMClickSyslinkVAD)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_EC, &SettingsDlg::OnNMClickSyslinkEC)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_FORCE_CODEC, &SettingsDlg::OnNMClickSyslinkForceCodec)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_VIDEO, &SettingsDlg::OnNMClickSyslinkVideo)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_PORTS, &SettingsDlg::OnNMClickSyslinkPorts)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_AUDIO_CODECS, &SettingsDlg::OnNMClickSyslinkAudioCodecs)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_ENABLE_LOG, &SettingsDlg::OnNMClickSyslinkEnableLog)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_BRING_TO_FRONT, &SettingsDlg::OnNMClickSyslinkBringToFrontOnIncoming)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_ANSWER_BOX_RANDOM, &SettingsDlg::OnNMClickSyslinkRandomAnswerBox)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_DISABLE_LOCAL, &SettingsDlg::OnNMClickSyslinkEnableLocal)
	ON_NOTIFY(NM_CLICK, IDC_SETTINGS_HELP_CRASH_REPORT, &SettingsDlg::OnNMClickSyslinkCrashReport)
#ifdef _GLOBAL_VIDEO
	ON_BN_CLICKED(IDC_SETTINGS_PREVIEW, &SettingsDlg::OnBnClickedPreview)
#endif
	ON_BN_CLICKED(IDC_SETTINGS_BROWSE, &SettingsDlg::OnBnClickedBrowse)
	ON_EN_CHANGE(IDC_SETTINGS_RINGTONE, &SettingsDlg::OnChangeRingtone)
	ON_BN_CLICKED(IDC_SETTINGS_DEFAULT, &SettingsDlg::OnBnClickedDefault)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_SETTINGS_RECORDING_BROWSE, &SettingsDlg::OnBnClickedRecordingBrowse)
	ON_EN_CHANGE(IDC_SETTINGS_RECORDING, &SettingsDlg::OnEnChangeRecording)
	ON_BN_CLICKED(IDC_SETTINGS_RECORDING_DEFAULT, &SettingsDlg::OnBnClickedRecordingDefault)
	ON_BN_CLICKED(IDC_SETTINGS_DNS_SRV_CHECKBOX, &SettingsDlg::OnBnClickedDnsSrv)
	ON_BN_CLICKED(IDC_SETTINGS_STUN_CHECKBOX, &SettingsDlg::OnBnClickedStun)
END_MESSAGE_MAP()

void SettingsDlg::OnClose()
{
	DestroyWindow();
}

void SettingsDlg::OnBnClickedCancel()
{
	mainDlg->PlayerStop();
	OnClose();
}

void SettingsDlg::OnBnClickedOk()
{
	this->ShowWindow(SW_HIDE);
	mainDlg->PJDestroy();
	PostMessage(UM_UPDATE_SETTINGS, 0, 0);
}

LRESULT SettingsDlg::OnUpdateSettings(WPARAM wParam, LPARAM lParam)
{
	CString str; 

	CComboBox *combobox;
	int i;
	GetDlgItem(IDC_SETTINGS_MICROPHONE)->GetWindowText(accountSettings.audioInputDevice);
	if (accountSettings.audioInputDevice == Translate(_T("Default")))
	{
		accountSettings.audioInputDevice = _T("");
	}

	GetDlgItem(IDC_SETTINGS_SPEAKERS)->GetWindowText(accountSettings.audioOutputDevice);
	if (accountSettings.audioOutputDevice == Translate(_T("Default")))
	{
		accountSettings.audioOutputDevice = _T("");
	}

	GetDlgItem(IDC_SETTINGS_RING)->GetWindowText(accountSettings.audioRingDevice);
	if (accountSettings.audioRingDevice == Translate(_T("Default")))
	{
		accountSettings.audioRingDevice = _T("");
	}
	accountSettings.micAmplification = ((CButton*)GetDlgItem(IDC_SETTINGS_MIC_AMPLIF))->GetCheck();
	accountSettings.swLevelAdjustment = ((CButton*)GetDlgItem(IDC_SETTINGS_SW_ADJUST))->GetCheck();

	accountSettings.vad = ((CButton*)GetDlgItem(IDC_SETTINGS_VAD))->GetCheck();
	accountSettings.ec = ((CButton*)GetDlgItem(IDC_SETTINGS_EC))->GetCheck();
	accountSettings.forceCodec = ((CButton*)GetDlgItem(IDC_SETTINGS_FORCE_CODEC))->GetCheck();


	bool hasStereo = false;
	accountSettings.audioCodecs = _T("");
	CListBox *listbox2;
	listbox2 = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS);
	for (unsigned i = 0; i < listbox2->GetCount(); i++)
	{
		CString value;
		listbox2->GetText(i, value);
		POSITION pos = mainDlg->audioCodecList.Find(value);
		if (pos) {
			mainDlg->audioCodecList.GetPrev(pos);
			CString key = mainDlg->audioCodecList.GetPrev(pos);
			accountSettings.audioCodecs += key + _T(" ");
			if (!hasStereo && key.Right(2) == _T("/2") && key.Left(4) != _T("opus")) {
				hasStereo = true;
			}
		}
	}
	accountSettings.audioCodecs.Trim();
	if (hasStereo && accountSettings.ec) {
		AfxMessageBox(_T("Echo Canceler enabled. Stereo will be converted to Mono."));
	}

#ifdef _GLOBAL_VIDEO
	GetDlgItem(IDC_SETTINGS_VID_CAP_DEV)->GetWindowText(accountSettings.videoCaptureDevice);
	if (accountSettings.videoCaptureDevice == Translate(_T("Default")))
	{
		accountSettings.videoCaptureDevice = _T("");
	}

	GetDlgItem(IDC_SETTINGS_VIDEO_CODEC)->GetWindowText(accountSettings.videoCodec);
	if (accountSettings.videoCodec == Translate(_T("Default")))
	{
		accountSettings.videoCodec = _T("");
	}
	accountSettings.videoH264 = ((CButton*)GetDlgItem(IDC_SETTINGS_VIDEO_H264))->GetCheck();
	accountSettings.videoH263 = ((CButton*)GetDlgItem(IDC_SETTINGS_VIDEO_H263))->GetCheck();
	accountSettings.videoVP8 = ((CButton*)GetDlgItem(IDC_SETTINGS_VIDEO_VP8))->GetCheck();
	GetDlgItem(IDC_SETTINGS_VIDEO_BITRATE)->GetWindowText(str);
	accountSettings.videoBitrate = _wtoi(str);
#endif

	accountSettings.rport = ((CButton*)GetDlgItem(IDC_SETTINGS_RPORT))->GetCheck();
	GetDlgItem(IDC_SETTINGS_SOURCE_PORT)->GetWindowText(str);
	accountSettings.sourcePort = _wtoi(str);
	GetDlgItem(IDC_SETTINGS_RTP_PORT_MIN)->GetWindowText(str);
	accountSettings.rtpPortMin = _wtoi(str);
	GetDlgItem(IDC_SETTINGS_RTP_PORT_MAX)->GetWindowText(str);
	accountSettings.rtpPortMax = _wtoi(str);

	GetDlgItem(IDC_SETTINGS_DNS_SRV_NS)->GetWindowText(accountSettings.dnsSrvNs);
	accountSettings.dnsSrvNs.Trim();
	if (!accountSettings.dnsSrvNs.IsEmpty()) {
		accountSettings.dnsSrv = ((CButton*)GetDlgItem(IDC_SETTINGS_DNS_SRV_CHECKBOX))->GetCheck();
	}
	else {
		accountSettings.dnsSrv = false;
	}

	GetDlgItem(IDC_SETTINGS_STUN)->GetWindowText(accountSettings.stun);
	accountSettings.stun.Trim();
	if (!accountSettings.stun.IsEmpty()) {
		accountSettings.enableSTUN = ((CButton*)GetDlgItem(IDC_SETTINGS_STUN_CHECKBOX))->GetCheck();
	}
	else {
		accountSettings.enableSTUN = false;
	}

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_DTMF_METHOD);
	accountSettings.DTMFMethod = combobox->GetCurSel();

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_AUTO_ANSWER);
	accountSettings.autoAnswer = autoAnswerValues.GetAt(combobox->GetCurSel());
	GetDlgItem(IDC_SETTINGS_AUTO_ANSWER_DELAY)->GetWindowText(str);
	accountSettings.autoAnswerDelay = _wtoi(str);

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_DENY_INCOMING);
	accountSettings.denyIncoming = denyIncomingValues.GetAt(combobox->GetCurSel());

	GetDlgItem(IDC_SETTINGS_DIRECTORY)->GetWindowText(accountSettings.usersDirectory);
	accountSettings.usersDirectory.Trim();
	combobox= (CComboBox*)GetDlgItem(IDC_SETTINGS_DEFAULT_ACTION);
	accountSettings.defaultAction = defaultActionItems[combobox->GetCurSel()];

	accountSettings.enableMediaButtons = ((CButton*)GetDlgItem(IDC_SETTINGS_MEDIA_BUTTONS))->GetCheck();
	accountSettings.localDTMF = ((CButton*)GetDlgItem(IDC_SETTINGS_LOCAL_DTMF))->GetCheck();
	accountSettings.singleMode = ((CButton*)GetDlgItem(IDC_SETTINGS_SINGLE_MODE))->GetCheck();
	accountSettings.enableLog = ((CButton*)GetDlgItem(IDC_SETTINGS_ENABLE_LOG))->GetCheck();
	accountSettings.bringToFrontOnIncoming = ((CButton*)GetDlgItem(IDC_SETTINGS_BRING_TO_FRONT))->GetCheck();
	accountSettings.randomAnswerBox = ((CButton*)GetDlgItem(IDC_SETTINGS_ANSWER_BOX_RANDOM))->GetCheck();
	accountSettings.callWaiting = ((CButton*)GetDlgItem(IDC_SETTINGS_CALL_WAITING))->GetCheck();
	GetDlgItem(IDC_SETTINGS_RINGTONE)->GetWindowText(accountSettings.ringtone);
	accountSettings.volumeRing = ((CSliderCtrl*)GetDlgItem(IDC_SETTINGS_VOLUME_RING))->GetPos();
	GetDlgItem(IDC_SETTINGS_RECORDING)->GetWindowText(accountSettings.recordingPath);
	accountSettings.recordingPath.Trim();
	accountSettings.recordingFormat = IsDlgButtonChecked(IDC_SETTINGS_RECORDING_MP3) ? _T("mp3") : _T("wav");
	accountSettings.autoRecording = ((CButton*)GetDlgItem(IDC_SETTINGS_RECORDING_CHECKBOX))->GetCheck();
	accountSettings.enableLocalAccount = ((CButton*)GetDlgItem(IDC_SETTINGS_ENABLE_LOCAL))->GetCheck();

	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_UPDATES_INTERVAL);
	i = combobox->GetCurSel();
	switch (i) {
	case 0:
		accountSettings.updatesInterval = _T("daily");
		break;
	case 2:
		accountSettings.updatesInterval = _T("monthly");
		break;
	case 3:
		accountSettings.updatesInterval = _T("quarterly");
		break;
	case 4:
		accountSettings.updatesInterval = _T("never");
		break;
	default:
		accountSettings.updatesInterval = _T("");
	}

	accountSettings.SettingsSave();

	if (accountSettings.singleMode) {
		mainDlg->messagesDlg->OnClose();
	}

	mainDlg->pageDialer->RebuildButtons();
	mainDlg->PJCreate();
	mainDlg->PJAccountAdd();

	OnClose();
	return 0;
}

void SettingsDlg::OnBnClickedBrowse()
{
	CFileDialog dlgFile(TRUE, _T("wav"), 0, OFN_NOCHANGEDIR | OFN_HIDEREADONLY, _T("WAV Files (*.wav)|*.wav|"));
	if (dlgFile.DoModal() == IDOK) {
		CString cwd;
		LPTSTR ptr = cwd.GetBuffer(MAX_PATH);
		::GetCurrentDirectory(MAX_PATH, ptr);
		cwd.ReleaseBuffer();
		if (cwd.MakeLower() + _T("\\") + dlgFile.GetFileName().MakeLower() == dlgFile.GetPathName().MakeLower()) {
			GetDlgItem(IDC_SETTINGS_RINGTONE)->SetWindowText(dlgFile.GetFileName());
		}
		else {
			GetDlgItem(IDC_SETTINGS_RINGTONE)->SetWindowText(dlgFile.GetPathName());
		}
	}
}

void SettingsDlg::OnChangeRingtone()
{
	CString str;
	GetDlgItem(IDC_SETTINGS_RINGTONE)->GetWindowText(str);
	GetDlgItem(IDC_SETTINGS_DEFAULT)->EnableWindow(str.GetLength() > 0);

}

void SettingsDlg::OnBnClickedDefault()
{
	GetDlgItem(IDC_SETTINGS_RINGTONE)->SetWindowText(_T(""));
}

void SettingsDlg::OnHScroll(UINT nSBCode, UINT, CScrollBar* sender)
{
	if (sender == GetDlgItem(IDC_SETTINGS_VOLUME_RING)) {
		if (nSBCode == SB_ENDSCROLL) {
			int volumeRingOld = accountSettings.volumeRing;
			accountSettings.volumeRing = ((CSliderCtrl*)GetDlgItem(IDC_SETTINGS_VOLUME_RING))->GetPos();
			CString ringtone;
			GetDlgItem(IDC_SETTINGS_RINGTONE)->GetWindowText(ringtone);
			mainDlg->PlayerPlay(ringtone.IsEmpty() ? _T("ringtone.wav") : ringtone, true, false);
			accountSettings.volumeRing = volumeRingOld;
		}
	}
}

void SettingsDlg::OnBnClickedRecordingBrowse()
{
	CString strOutFolder;
	CString str;
	CShellManager* pShellManager = ((CWinAppEx*)AfxGetApp())->GetShellManager();
	GetDlgItem(IDC_SETTINGS_RECORDING)->GetWindowText(str);
	if (str.IsEmpty() || PathIsRelative(str)) {
		TCHAR currentDir[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, currentDir);
		strOutFolder = currentDir;
		if (!str.IsEmpty()) {
			strOutFolder.AppendFormat(_T("\\%s"), str);
		}
	}
	else {
		strOutFolder = str;
	}
	if (pShellManager->BrowseForFolder(strOutFolder,this, strOutFolder))
	{
		GetDlgItem(IDC_SETTINGS_RECORDING)->SetWindowText(strOutFolder);
	}
}

void SettingsDlg::OnEnChangeRecording()
{
	CString str;
	GetDlgItem(IDC_SETTINGS_RECORDING)->GetWindowText(str);
	GetDlgItem(IDC_SETTINGS_RECORDING_DEFAULT)->EnableWindow(str.GetLength() > 0);
}

void SettingsDlg::OnBnClickedRecordingDefault()
{
	GetDlgItem(IDC_SETTINGS_RECORDING)->SetWindowText(_T(""));
}

int SettingsDlg::OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex)
{
	CListBox *listbox = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS_ALL);
	CListBox *listbox2 = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS);
	if (pListBox == listbox && listbox->GetCurSel()!=-1) {
		if (nKey == 32) {
			//add
			NMUPDOWN NMUpDown;
			NMUpDown.iDelta = -1;
			LRESULT lResult;
			OnDeltaposSpinModify((NMHDR*)&NMUpDown, &lResult);
			return -2;
		}
	}
	if (pListBox == listbox2 && listbox2->GetCurSel() != -1) {
		if (nKey == 46) {
			//remove
			NMUPDOWN NMUpDown;
			NMUpDown.iDelta = 1;
			LRESULT lResult;
			OnDeltaposSpinModify((NMHDR*)&NMUpDown, &lResult);
			return -2;
		}
	}
	return -1;
}

void SettingsDlg::OnDeltaposSpinModify(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMUPDOWN pNMUpDown = reinterpret_cast<LPNMUPDOWN>(pNMHDR);
	CListBox *listbox;
	listbox = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS_ALL);
	CListBox *listbox2;
	listbox2 = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS);
	if (pNMUpDown->iDelta == -1) {
		//add
		int selected = listbox->GetCurSel();
		if (selected != LB_ERR)
		{
			CString str;
			listbox->GetText(selected, str);
			listbox2->AddString(str);
			listbox->DeleteString(selected);
			listbox->SetCurSel(selected < listbox->GetCount() ? selected : selected - 1);
		}
	}
	else {
		//remove
		int selected = listbox2->GetCurSel();
		if (selected != LB_ERR)
		{
			CString str;
			listbox2->GetText(selected, str);
			listbox->AddString(str);
			listbox2->DeleteString(selected);
			listbox2->SetCurSel(selected < listbox2->GetCount() ? selected : selected - 1);
		}
	}
	*pResult = 0;
}
void SettingsDlg::OnDeltaposSpinOrder(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMUPDOWN pNMUpDown = reinterpret_cast<LPNMUPDOWN>(pNMHDR);
	CListBox *listbox2;
	listbox2 = (CListBox*)GetDlgItem(IDC_SETTINGS_AUDIO_CODECS);
	int selected = listbox2->GetCurSel();
	if (selected != LB_ERR)
	{
		CString str;
		listbox2->GetText(selected, str);
		if (pNMUpDown->iDelta == -1) {
			//up
			if (selected > 0)
			{
				listbox2->DeleteString(selected);
				listbox2->InsertString(selected - 1, str);
				listbox2->SetCurSel(selected - 1);
			}
		}
		else {
			//down
			if (selected < listbox2->GetCount() - 1)
			{
				listbox2->DeleteString(selected);
				listbox2->InsertString(selected + 1, str);
				listbox2->SetCurSel(selected + 1);
			}
		}
	}
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkRingtone(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("ringtone"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkMicAmplif(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("microphoneAmplification"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkSwAdjust(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("softwareLevelAdjustment"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkDTMFMethod(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("DTMFMethod"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkAutoAnswer(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("autoAnswer"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkDenyIncoming(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("denyIncoming"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkDirectory(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("directory"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkDnsSrv(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("dnsSrv"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkStunServer(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("stunServer"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkMediaButtons(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("handleMediaButtons"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkLocalDTMF(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("soundEvents"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkSingleMode(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("singleMode"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkVAD(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("vad"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkEC(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("ec"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkForceCodec(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("forceCodec"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkVideo(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("video"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkPorts(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("ports"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkAudioCodecs(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("audioCodecs"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkEnableLog(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("log"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkBringToFrontOnIncoming(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("bringToFrontOnIncoming"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkRandomAnswerBox(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("randomAnswerBox"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkEnableLocal(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("enableLocal"));
	*pResult = 0;
}

void SettingsDlg::OnNMClickSyslinkCrashReport(NMHDR *pNMHDR, LRESULT *pResult)
{
	OpenHelp(_T("crashReport"));
	*pResult = 0;
}

#ifdef _GLOBAL_VIDEO
void SettingsDlg::OnBnClickedPreview()
{
	CComboBox *combobox;
	combobox = (CComboBox*)GetDlgItem(IDC_SETTINGS_VID_CAP_DEV);
	CString name;
	combobox->GetWindowText(name);
	if (!mainDlg->previewWin) {
		mainDlg->previewWin = new Preview(mainDlg);
	}
	mainDlg->previewWin->Start(mainDlg->VideoCaptureDeviceId(name));
}
#endif

void SettingsDlg::OnBnClickedDnsSrv()
{
	if (((CButton*)GetDlgItem(IDC_SETTINGS_DNS_SRV_CHECKBOX))->GetCheck()) {
		CString str;
		GetDlgItem(IDC_SETTINGS_DNS_SRV_NS)->GetWindowText(str);
		if (str.IsEmpty()) {
			GetDlgItem(IDC_SETTINGS_DNS_SRV_NS)->SetWindowText(_T("8.8.8.8; 8.8.4.4"));
		}
	}
}

void SettingsDlg::OnBnClickedStun()
{
	if (((CButton*)GetDlgItem(IDC_SETTINGS_STUN_CHECKBOX))->GetCheck()) {
		CString str;
		GetDlgItem(IDC_SETTINGS_STUN)->GetWindowText(str);
		if (str.IsEmpty()) {
			GetDlgItem(IDC_SETTINGS_STUN)->SetWindowText(_T("stun.l.google.com:19302"));
		}
	}
}

