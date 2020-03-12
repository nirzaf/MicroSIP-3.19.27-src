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

#define THIS_FILENAME "mainDlg.cpp"

#include "mainDlg.h"

#include "microsip.h"

#include "Mmsystem.h"
#include "settings.h"
#include "global.h"
#include "ModelessMessageBox.h"
#include "utf.h"
#include "json.h"
#include "XMLFile.h"
#include "Markup.h"
#include "langpack.h"
#include "jumplist.h"
#include "atlenc.h"

#include <io.h>
#include <afxmt.h>
#include <afxinet.h>
#include <ws2tcpip.h>
#include <Dbt.h>
#include <Strsafe.h>
#include <locale.h> 
#include <Wtsapi32.h>
#include <afxsock.h>
#include "atlrx.h"

using namespace MSIP;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CmainDlg *mainDlg;

static UINT WM_SHELLHOOKMESSAGE;

static UINT BASED_CODE indicators[] =
{
	IDS_STATUSBAR,
	IDS_STATUSBAR2,
};

static bool timerContactBlinkState = false;

CCriticalSection gethostbyaddrThreadCS;
static CString gethostbyaddrThreadResult;
static DWORD WINAPI gethostbyaddrThread(LPVOID lpParam)
{
	CString *addr = (CString *)lpParam;
	CString res = *addr;
	delete addr;
	struct hostent *he = NULL;
	struct in_addr inaddr;
	inaddr.S_un.S_addr = inet_addr(CStringA(res));
	if (inaddr.S_un.S_addr != INADDR_NONE && inaddr.S_un.S_addr != INADDR_ANY) {
		he = gethostbyaddr((char *)&inaddr, 4, AF_INET);
		if (he) {
			res = he->h_name;
		}
	}
	gethostbyaddrThreadCS.Lock();
	gethostbyaddrThreadResult = res;
	gethostbyaddrThreadCS.Unlock();
	return 0;
}

static void on_reg_state2(pjsua_acc_id acc_id, pjsua_reg_info *info)
{
	if (!IsWindow(mainDlg->m_hWnd)) {
		return;
	}
	CString *str = NULL;
	PostMessage(mainDlg->m_hWnd, UM_ON_REG_STATE2, (WPARAM)info->cbparam->code, (LPARAM)str);
}

LRESULT CmainDlg::onRegState2(WPARAM wParam, LPARAM lParam)
{
	int code = wParam;

	if (code == 200) {
		if (accountSettings.usersDirectory.Find(_T("%s")) != -1 || accountSettings.usersDirectory.Find(_T("{")) != -1) {
			UsersDirectoryLoad();
		}
	}

	UpdateWindowText(_T(""), IDI_DEFAULT, true);

	return 0;
}

/* Callback from timer when the maximum call duration has been
 * exceeded.
 */
static void call_timeout_callback(pj_timer_heap_t *timer_heap,
	struct pj_timer_entry *entry)
{
	pjsua_call_id call_id = entry->id;
	pjsua_msg_data msg_data_;
	pjsip_generic_string_hdr warn;
	pj_str_t hname = pj_str("Warning");
	pj_str_t hvalue = pj_str("399 localhost \"Call duration exceeded\"");

	PJ_UNUSED_ARG(timer_heap);

	if (call_id == PJSUA_INVALID_ID) {
		PJ_LOG(1, (THIS_FILENAME, "Invalid call ID in timer callback"));
		return;
	}

	/* Add warning header */
	pjsua_msg_data_init(&msg_data_);
	pjsip_generic_string_hdr_init2(&warn, &hname, &hvalue);
	pj_list_push_back(&msg_data_.hdr_list, &warn);

	/* Call duration has been exceeded; disconnect the call */
	PJ_LOG(3, (THIS_FILENAME, "Duration (%d seconds) has been exceeded "
		"for call %d, disconnecting the call",
		accountSettings.autoHangUpTime, call_id));
	entry->id = PJSUA_INVALID_ID;
	pjsua_call_hangup(call_id, 200, NULL, &msg_data_);
}

static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
	if (!IsWindow(mainDlg->m_hWnd)) {
		return;
	}
	pjsua_call_info *call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_info->id);
	// reset user_data after call transfer
	if (user_data) {
		user_data->CS.Lock();
		bool callIdMissmatch = user_data->call_id != PJSUA_INVALID_ID && user_data->call_id != call_info->id;
		bool hidden = user_data->hidden;
		user_data->CS.Unlock();
		if (callIdMissmatch) {
			pjsua_call_set_user_data(call_info->id, NULL);
			user_data = NULL;
		}
		else {
			if (hidden) {
				if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
					delete user_data;
				}
				return;
			}
		}
	}
	if (!user_data) {
		user_data = new call_user_data(call_info->id);
		pjsua_call_set_user_data(call_info->id, user_data);
	}

	user_data->CS.Lock();

	switch (call_info->state) {
	case PJSIP_INV_STATE_CALLING:
		msip_call_unhold(call_info);
		break;
	case PJSIP_INV_STATE_CONNECTING:
		msip_call_unhold(call_info);
		break;
	case PJSIP_INV_STATE_CONFIRMED:
		if (accountSettings.autoRecording) {
			msip_call_recording_start(user_data, call_info);
		}
		if (accountSettings.autoHangUpTime > 0) {
			/* Schedule timer to hangup call after the specified duration */
			pj_time_val delay;
			user_data->auto_hangup_timer.id = call_info->id;
			user_data->auto_hangup_timer.cb = &call_timeout_callback;
			delay.sec = accountSettings.autoHangUpTime;
			delay.msec = 0;
			pjsua_schedule_timer(&user_data->auto_hangup_timer, &delay);
		}
		break;
	case PJSIP_INV_STATE_DISCONNECTED:
		msip_call_recording_stop(user_data);
		/* Cancel duration timer, if any */
		if (user_data->auto_hangup_timer.id != PJSUA_INVALID_ID) {
			pjsua_cancel_timer(&user_data->auto_hangup_timer);
			user_data->auto_hangup_timer.id = PJSUA_INVALID_ID;
		}
		call_deinit_tonegen(call_info->id);
		msip_conference_leave(call_info);
		pjsua_call_set_user_data(call_info->id, NULL);
		break;
	}

	user_data->CS.Unlock();

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_STATE, (WPARAM)call_info, (LPARAM)user_data);
}

LRESULT CmainDlg::onCallState(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info *call_info = (pjsua_call_info *)wParam;
	call_user_data *user_data = (call_user_data *)lParam;

	CString number = PjToStr(&call_info->remote_info, TRUE);
	SIPURI sipuri;
	ParseSIPURI(number, &sipuri);

	user_data->CS.Lock();

	CString *str = new CString();
	CString adder;

	if (call_info->state != PJSIP_INV_STATE_DISCONNECTED && call_info->state != PJSIP_INV_STATE_CONNECTING && call_info->remote_contact.slen > 0) {
		SIPURI contactURI;
		ParseSIPURI(PjToStr(&call_info->remote_contact, TRUE), &contactURI);
		CString contactDomain = RemovePort(contactURI.domain);
		struct hostent *he = NULL;
		if (IsIP(contactDomain)) {
			HANDLE hThread;
			CString *addr = new CString(contactDomain);
			if (addr) {
				hThread = CreateThread(NULL, 0, gethostbyaddrThread, addr, 0, NULL);
				if (WaitForSingleObject(hThread, 500) == 0) {
					gethostbyaddrThreadCS.Lock();
					contactDomain = gethostbyaddrThreadResult;
					gethostbyaddrThreadCS.Unlock();
				}
			}
		}
		adder.AppendFormat(_T("%s, "), contactDomain);
	}

	unsigned cnt = 0;
	unsigned cnt_srtp = 0;

	switch (call_info->state) {
	case PJSIP_INV_STATE_CALLING:
		*str = Translate(_T("Calling"));
		str->AppendFormat(_T(" %s "), !sipuri.user.IsEmpty() ? sipuri.user : sipuri.domain);
		str->Append(_T("..."));
		break;
	case PJSIP_INV_STATE_INCOMING:
		str->SetString(Translate(_T("Incoming Call")));
		break;
	case PJSIP_INV_STATE_EARLY:
		str->SetString(Translate(PjToStr(&call_info->last_status_text).GetBuffer()));
		break;
	case PJSIP_INV_STATE_CONNECTING:
		str->Format(_T("%s..."), Translate(_T("Connecting")));
		break;
	case PJSIP_INV_STATE_CONFIRMED:
		str->SetString(Translate(_T("Connected")));
		for (unsigned i = 0; i < call_info->media_cnt; i++) {
			if (call_info->media[i].dir != PJMEDIA_DIR_NONE &&
				(call_info->media[i].type == PJMEDIA_TYPE_AUDIO || call_info->media[i].type == PJMEDIA_TYPE_VIDEO)) {
				cnt++;
				pjsua_call_info call_info_stub;
				if (pjsua_var.state == PJSUA_STATE_RUNNING && pjsua_call_get_info(call_info->id, &call_info_stub) == PJ_SUCCESS) {
					pjsua_stream_info psi;
					if (pjsua_call_get_stream_info(call_info->id, call_info->media[i].index, &psi) == PJ_SUCCESS) {
						if (call_info->media[i].type == PJMEDIA_TYPE_AUDIO) {
							adder.AppendFormat(_T("%s@%dkHz %dkbit/s%s, "),
								PjToStr(&psi.info.aud.fmt.encoding_name), psi.info.aud.fmt.clock_rate / 1000,
								psi.info.aud.param->info.avg_bps / 1000,
								psi.info.aud.fmt.channel_cnt == 2 ? _T(" Stereo") : _T("")
							);
						}
						else {
							adder.AppendFormat(_T("%s %dkbit/s, "),
								PjToStr(&psi.info.vid.codec_info.encoding_name),
								psi.info.vid.codec_param->enc_fmt.det.vid.max_bps / 1000
							);
						}
					}
					pjmedia_transport_info t;
					if (pjsua_call_get_med_transport_info(call_info->id, call_info->media[i].index, &t) == PJ_SUCCESS) {
						for (unsigned j = 0; j < t.specific_info_cnt; j++) {
							if (t.spc_info[j].buffer[0]) {
								switch (t.spc_info[j].type) {
								case PJMEDIA_TRANSPORT_TYPE_SRTP:
									adder.Append(_T("SRTP, "));
									cnt_srtp++;
									break;
								case PJMEDIA_TRANSPORT_TYPE_ICE:
									adder.Append(_T("ICE, "));
									break;
								}
							}
						}
					}
				}
			}
		}
		if (cnt_srtp && cnt == cnt_srtp) {
			user_data->srtp = MSIP_SRTP;
		}
		else {
			user_data->srtp = MSIP_SRTP_DISABLED;
		}
		break;
	case PJSIP_INV_STATE_DISCONNECTED:
		//--
		if (call_info->last_status == 200) {
			str->SetString(Translate(_T("Call Ended")));
		}
		else {
			CString rab = PjToStr(&call_info->last_status_text);
			if (rab.Find(_T("(PJ_ERESOLVE)")) != -1) {
				rab = _T("Cannot get IP address of the called host.");
			}
			if (call_info->last_status == 487) {
				str->SetString(Translate(_T("Cancel")));
			}
			else
				if (call_info->acc_id && call_info->last_status >= 500 && call_info->last_status < 600) {
					str->Format(_T("Server Failure: "));
					str->AppendFormat(_T("%d %s"), call_info->last_status, Translate(rab.GetBuffer()));
				}
				else {
					str->SetString(rab);
				}
		}
		break;
	}

	if (!str->IsEmpty() && !adder.IsEmpty()) {
		str->AppendFormat(_T(" (%s)"), adder.Left(adder.GetLength() - 2));
	}

	if (call_info->state == PJSIP_INV_STATE_CALLING) {
		//--
		if (!accountSettings.cmdOutgoingCall.IsEmpty()) {
			CString params = sipuri.user;
			RunCmd(accountSettings.cmdOutgoingCall, params);
		}
		//--
	}

	if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
		PostMessage(WM_TIMER, IDT_TIMER_CALL, NULL);
		SetTimer(IDT_TIMER_CALL, 1000, NULL);
		if (call_info->role == PJSIP_ROLE_UAS) {
			//--
			if (!accountSettings.cmdCallAnswer.IsEmpty()) {
				CString params = sipuri.user;
				RunCmd(accountSettings.cmdCallAnswer, params);
			}
			//--
		}
		//--
		if (!accountSettings.cmdCallStart.IsEmpty()) {
			CString params = sipuri.user;
			RunCmd(accountSettings.cmdCallStart, params);
		}
		//--
		if (!user_data->commands.IsEmpty()) {
			SetTimer((UINT_PTR)call_info->id, 1000, (TIMERPROC)DTMFQueueTimerHandler);
		}
	}

	if (!accountSettings.singleMode) {
		if (call_info->state != PJSIP_INV_STATE_CONFIRMED) {
			if (call_info->state != PJSIP_INV_STATE_DISCONNECTED) {
				UpdateWindowText(*str, call_info->role == PJSIP_ROLE_UAS ? IDI_CALL_IN : IDI_CALL_OUT);
			}
			else {
				PostMessage(UM_UPDATEWINDOWTEXT, 0, 0);
			}
		}
	}

	if (call_info->role == PJSIP_ROLE_UAC) {
		if (call_info->last_status == 180 && !call_info->media_cnt) {
			if (toneCalls.IsEmpty()) {
				PostMessage(WM_TIMER, IDT_TIMER_TONE, NULL);
				SetTimer(IDT_TIMER_TONE, 4500, NULL);
				toneCalls.AddTail(call_info->id);
			}
			else if (toneCalls.Find(call_info->id) == NULL) {
				toneCalls.AddTail(call_info->id);
			}
		}
		else {
			POSITION position = toneCalls.Find(call_info->id);
			if (position != NULL) {
				toneCalls.RemoveAt(position);
				if (toneCalls.IsEmpty()) {
					KillTimer(IDT_TIMER_TONE);
					PostMessage(UM_ON_PLAYER_STOP, 0, 0);
				}
			}
		}
	}

	bool doNotShowMessagesWindow =
		call_info->state == PJSIP_INV_STATE_INCOMING ||
		call_info->state == PJSIP_INV_STATE_EARLY ||
		call_info->state == PJSIP_INV_STATE_DISCONNECTED ||
		accountSettings.singleMode ||
		(accountSettings.silent && !mainDlg->IsWindowVisible());

	if (user_data->autoAnswer) {
		if (!accountSettings.bringToFrontOnIncoming) {
			doNotShowMessagesWindow = true;
		}
	}

	MessagesContact* messagesContact = messagesDlg->AddTab(number, _T(""),
		(!accountSettings.singleMode &&
		(call_info->state == PJSIP_INV_STATE_CONFIRMED
			|| call_info->state == PJSIP_INV_STATE_CONNECTING)
			)
		||
		(accountSettings.singleMode
			&&
			(
			(call_info->role == PJSIP_ROLE_UAC && call_info->state != PJSIP_INV_STATE_DISCONNECTED)
				||
				(call_info->role == PJSIP_ROLE_UAS &&
				(call_info->state == PJSIP_INV_STATE_CONFIRMED
					|| call_info->state == PJSIP_INV_STATE_CONNECTING)
					)
				))
		? TRUE : FALSE,
		call_info, user_data, doNotShowMessagesWindow, call_info->state == PJSIP_INV_STATE_DISCONNECTED
	);

	if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
		if (!accountSettings.singleMode && accountSettings.AC) {
			messagesDlg->OnMergeAll();
		}
	}

	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		CString number = sipuri.user;
		if (call_info->last_status == 200) {
			onPlayerPlay(MSIP_SOUND_HANGUP, 0);
		}
		else {
			if (accountSettings.singleMode) {
				if (call_info->last_status == 487 || (call_info->role == PJSIP_ROLE_UAS && (call_info->last_status == 486 || call_info->last_status == 600 || call_info->last_status == 603))) {
					//don't show
				}
				else {
					if (messagesContact) {
						BaloonPopup(*str, messagesContact->name, NIIF_INFO);
					}
					else {
						BaloonPopup(*str, number, NIIF_INFO);
					}
				}
			}
		}
		if (call_info->last_status == 486 || call_info->last_status == 600 || call_info->last_status == 603) {
			//--
			if (!accountSettings.cmdCallBusy.IsEmpty()) {
				CString params = number;
				RunCmd(accountSettings.cmdCallBusy, params);
			}
			//--
		}
		else {
			//--
			if (!accountSettings.cmdCallEnd.IsEmpty()) {
				CString params = number;
				RunCmd(accountSettings.cmdCallEnd, params);
			}
			//--
		}
		if (call_info->role == PJSIP_ROLE_UAS && call_info->last_status == 487) {
			//-- missed call
			missed = true;
		}
	}

	if (messagesContact) {
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			messagesContact->mediaStatus = PJSUA_CALL_MEDIA_ERROR;
		}
		if (call_info->role == PJSIP_ROLE_UAS) {
			if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
				pageCalls->Add(call_info->call_id, messagesContact->number + messagesContact->numberParameters, messagesContact->name, MSIP_CALL_IN);
			}
			else if (call_info->state == PJSIP_INV_STATE_INCOMING || call_info->state == PJSIP_INV_STATE_EARLY) {
				pageCalls->Add(call_info->call_id, messagesContact->number + messagesContact->numberParameters, messagesContact->name, MSIP_CALL_MISS);
			}
		}
		else {
			if (call_info->state == PJSIP_INV_STATE_CALLING) {
				pageCalls->Add(call_info->call_id, messagesContact->number + messagesContact->numberParameters, messagesContact->name, MSIP_CALL_OUT);
			}
		}
	}
	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		pageCalls->SetDuration(call_info->call_id, msip_get_duration(&call_info->connect_duration));
		CString number = sipuri.user;
		CString name = pageContacts->GetNameByNumber(GetSIPURI(sipuri.user, true));
		if (name.IsEmpty()) {
			name = !sipuri.name.IsEmpty() ? sipuri.name : (!sipuri.user.IsEmpty() ? sipuri.user : sipuri.domain);
		}
		pageCalls->Add(call_info->call_id, number, name, MSIP_CALL_MISS);
		pageCalls->SetInfo(call_info->call_id, *str);
	}

	if (accountSettings.singleMode) {
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			pjsua_call_id current_call_id = CurrentCallId();
			if (current_call_id == call_info->id || current_call_id == -1) {
				pageDialer->Clear(false);
				pageDialer->UpdateCallButton(FALSE, 0);
			}
			PostMessage(UM_UPDATEWINDOWTEXT, 0, 0);
		}
		else {
			if (call_info->state != PJSIP_INV_STATE_CONFIRMED) {
				UpdateWindowText(*str, call_info->role == PJSIP_ROLE_UAS ? IDI_CALL_IN : IDI_CALL_OUT);
			}
			int tabN = 0;
			GotoTab(tabN);
			messagesDlg->OnChangeTab(call_info, user_data);
		}
	}
	else {
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			pageDialer->PostMessage(WM_COMMAND, MAKELPARAM(IDC_CLEAR, 0), 0);
		}
	}

	if (messagesContact && !str->IsEmpty()) {
		messagesDlg->AddMessage(messagesContact, *str, MSIP_MESSAGE_TYPE_SYSTEM,
			call_info->state == PJSIP_INV_STATE_INCOMING || call_info->state == PJSIP_INV_STATE_EARLY
		);
	}

	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		messagesDlg->OnEndCall(call_info);
		if (pjsua_var.state != PJSUA_STATE_RUNNING || !pjsua_call_get_count()) {
#ifdef _GLOBAL_VIDEO
			if (previewWin) {
				previewWin->PostMessage(WM_CLOSE, NULL, NULL);
			}
#endif
		}
	}

	if (call_info->role == PJSIP_ROLE_UAS) {
		if (call_info->state != PJSIP_INV_STATE_INCOMING && call_info->state != PJSIP_INV_STATE_EARLY) {
			int count = ringinDlgs.GetCount();
			if (!count) {
				if (call_info->state != PJSIP_INV_STATE_DISCONNECTED || (call_info->state == PJSIP_INV_STATE_DISCONNECTED && call_info->connect_duration.sec == 0 && call_info->connect_duration.msec == 0)) {
					PlayerStop();
				}
			}
			else {
				for (int i = 0; i < count; i++) {
					RinginDlg *ringinDlg = ringinDlgs.GetAt(i);
					if (call_info->id == ringinDlg->call_id) {
						if (count == 1) {
							PlayerStop();
						}
						ringinDlgs.RemoveAt(i);
						ringinDlg->DestroyWindow();
						break;
					}
				}
			}
		}
	}

	if (call_info->state != PJSIP_INV_STATE_INCOMING &&
		call_info->state != PJSIP_INV_STATE_EARLY
		) {
		if (call_info->state != PJSIP_INV_STATE_DISCONNECTED) {
			if (messagesContact) {
				pageDialer->SetName(messagesContact->name);
			}
		}
		else {
			pageDialer->SetName();
		}
	}

	user_data->CS.Unlock();

	// --delete user data
	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		if (user_data) {
			delete user_data;
		}
	}
	// --
	delete call_info;
	delete str;

	bool hasCalls = call_get_count_noincoming();
	if (pageDialer->IsChild(&pageDialer->m_ButtonRec)) {
		pageDialer->m_ButtonRec.EnableWindow(hasCalls);
	}
	return 0;
}

static void on_call_media_state(pjsua_call_id call_id)
{
	pjsua_call_info *call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_info->id);

	if (call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE
		|| call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD
		) {
		msip_conference_join(call_info);
		pjsua_conf_connect(call_info->conf_slot, 0);
		pjsua_conf_connect(0, call_info->conf_slot);
		//--
		user_data->CS.Lock();
		user_data->holdFrom = -1;
		if (user_data->recorder_id != PJSUA_INVALID_ID) {
			pjsua_conf_port_id rec_conf_port_id = pjsua_recorder_get_conf_port(user_data->recorder_id);
			pjsua_conf_connect(call_info->conf_slot, rec_conf_port_id);
		}
		user_data->CS.Unlock();

		//--
		::SetTimer(mainDlg->pageDialer->m_hWnd, IDT_TIMER_VU_METER, 100, NULL);
		//--
	}
	else {
		msip_conference_leave(call_info, true);
		pjsua_conf_disconnect(call_info->conf_slot, 0);
		pjsua_conf_disconnect(0, call_info->conf_slot);
		call_deinit_tonegen(call_id);
		//--
		user_data->CS.Lock();
		user_data->holdFrom = msip_get_duration(&call_info->connect_duration);
		user_data->CS.Unlock();
		//--
	}

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_MEDIA_STATE, (WPARAM)call_info, (LPARAM)user_data);
}

LRESULT CmainDlg::onCallMediaState(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info *call_info = (pjsua_call_info *)wParam;
	call_user_data *user_data = (call_user_data *)lParam;

	messagesDlg->UpdateHoldButton(call_info);

	CString message;
	CString number = PjToStr(&call_info->remote_info, TRUE);

	MessagesContact* messagesContact = messagesDlg->AddTab(number, _T(""), FALSE, call_info, user_data, TRUE, TRUE);

	if (messagesContact) {
		if (call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
			message = _T("Call on Remote Hold");
		}
		if (call_info->media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD) {
			message = _T("Call on Local Hold");
		}
		if (call_info->media_status == PJSUA_CALL_MEDIA_NONE) {
			message = _T("Call on Hold");
		}
		if (messagesContact->mediaStatus != PJSUA_CALL_MEDIA_ERROR && messagesContact->mediaStatus != call_info->media_status && call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE) {
			message = _T("Call is Active");
		}
		if (!message.IsEmpty()) {
			messagesDlg->AddMessage(messagesContact, Translate(message.GetBuffer()), MSIP_MESSAGE_TYPE_SYSTEM, TRUE);
		}
		messagesContact->mediaStatus = call_info->media_status;
		pageDialer->SetName();
	}

	if (call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE
		|| call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD
		) {
		onRefreshLevels(0, 0);
	}

	delete call_info;

	return 0;
}

static void on_call_media_event(pjsua_call_id call_id,
	unsigned med_idx,
	pjmedia_event *event)
{
	char event_name[5];

	PJ_LOG(5, (THIS_FILENAME, "Event %s",
		pjmedia_fourcc_name(event->type, event_name)));

	//#if PJSUA_HAS_VIDEO
		//if (event->type == PJMEDIA_EVENT_FMT_CHANGED) {
		//	pjsua_call_info ci;
		//	pjsua_call_get_info(call_id, &ci);
		//	if ((ci.media[med_idx].type == PJMEDIA_TYPE_VIDEO) &&
		//		(ci.media[med_idx].dir & PJMEDIA_DIR_DECODING)) {
		//		pjsua_vid_win_id wid;
		//		pjmedia_rect_size size;
		//		pjsua_vid_win_info win_info;

		//		wid = ci.media[med_idx].stream.vid.win_in;
		//		pjsua_vid_win_get_info(wid, &win_info);

		//		size = event->data.fmt_changed.new_fmt.det.vid.size;
		//		if (size.w != win_info.size.w || size.h != win_info.size.h) {
		//			pjsua_vid_win_set_size(wid, &size);
		//			/* Re-arrange video windows */
		//			arrange_window(PJSUA_INVALID_ID);
		//		}
		//	}
		//}
	//#else
	//	PJ_UNUSED_ARG(call_id);
	//	PJ_UNUSED_ARG(med_idx);
	//	PJ_UNUSED_ARG(event);
	//#endif
}

static void on_incoming_call(pjsua_acc_id acc, pjsua_call_id call_id,
	pjsip_rx_data *rdata)
{
	pjsua_call_info call_info;
	pjsua_call_get_info(call_id, &call_info);

	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_info.id);
	if (!user_data) {
		user_data = new call_user_data(call_info.id);
		pjsua_call_set_user_data(call_info.id, user_data);
	}

	user_data->CS.Lock();

	SIPURI sipuri;
	ParseSIPURI(PjToStr(&call_info.remote_info, TRUE), &sipuri);
	user_data->name = sipuri.name;
	if (accountSettings.forceCodec) {
		pjsua_call *call;
		pjsip_dialog *dlg;
		pj_status_t status;
		status = acquire_call("on_incoming_call()", call_id, &call, &dlg);
		if (status == PJ_SUCCESS) {
			pjmedia_sdp_neg_set_prefer_remote_codec_order(call->inv->neg, PJ_FALSE);
			pjsip_dlg_dec_lock(dlg);
		}
	}
	//--
	if (!accountSettings.cmdIncomingCall.IsEmpty()) {
		CString params = sipuri.user;
		RunCmd(accountSettings.cmdIncomingCall, params);
	}
	//--
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	unsigned calls_count_cmp = 0;
	if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
		for (unsigned i = 0; i < calls_count; ++i) {
			pjsua_call_info call_info_curr;
			if (pjsua_call_get_info(call_ids[i], &call_info_curr) == PJ_SUCCESS) {
				SIPURI sipuri_curr;
				ParseSIPURI(PjToStr(&call_info_curr.remote_info, TRUE), &sipuri_curr);
				if (call_info_curr.id != call_info.id &&
					sipuri.user + _T("@") + sipuri.domain == sipuri_curr.user + _T("@") + sipuri_curr.domain
					) {
					// 486 Busy Here
					msip_call_busy(call_info.id);
					goto return_on_incoming_call;
				}
				if (call_info_curr.state != PJSIP_INV_STATE_DISCONNECTED) {
					calls_count_cmp++;
				}
			}
		}
	}

	if ((!accountSettings.callWaiting && calls_count_cmp > 1) || (accountSettings.maxConcurrentCalls > 0 && calls_count_cmp > accountSettings.maxConcurrentCalls)) {
		// 486 Busy Here
		msip_call_busy(call_info.id);
		goto return_on_incoming_call;
	}

	if (IsWindow(mainDlg->m_hWnd)) {
		pjsip_generic_string_hdr *hsr;
		// -- diversion
		const pj_str_t headerDiversion = { "Diversion",9 };
		hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerDiversion, NULL);
		if (hsr) {
			CString str = PjToStr(&hsr->hvalue, true);
			SIPURI sipuriDiversion;
			ParseSIPURI(str, &sipuriDiversion);
			user_data->diversion = !sipuriDiversion.user.IsEmpty() ? sipuriDiversion.user : sipuriDiversion.domain;
		}
		// -- end diversion
		// -- caller id
		const pj_str_t headerCallerID = { "P-Asserted-Identity",19 };
		hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerCallerID, NULL);
		if (!hsr) {
			const pj_str_t headerCallerID = { "Remote-Party-Id",15 };
			hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerCallerID, NULL);
		}
		if (hsr) {
			user_data->callerID = PjToStr(&hsr->hvalue, true);
		}
		// -- end caller id
		if (!mainDlg->callIdIncomingIgnore.IsEmpty() && mainDlg->callIdIncomingIgnore == PjToStr(&call_info.call_id)) {
			pjsua_call_answer(call_id, 487, NULL, NULL);
			goto return_on_incoming_call;
		}

		bool reject = false;
		CString reason;
		if (accountSettings.denyIncoming == _T("all")) {
			reject = true;
		}
		else if (accountSettings.denyIncoming == _T("button")) {
			reject = accountSettings.DND;
			reason = _T("Do Not Disturb");
		}
		else if (accountSettings.denyIncoming == _T("user")) {
			SIPURI sipuri_curr;
			ParseSIPURI(PjToStr(&call_info.local_info, TRUE), &sipuri_curr);
			if (sipuri_curr.user != get_account_username()) {
				reject = true;
			}
		}
		else if (accountSettings.denyIncoming == _T("domain")) {
			SIPURI sipuri_curr;
			ParseSIPURI(PjToStr(&call_info.local_info, TRUE), &sipuri_curr);
			if (accountSettings.accountId) {
				if (sipuri_curr.domain != get_account_domain()) {
					reject = true;
				}
			}
		}
		else if (accountSettings.denyIncoming == _T("rdomain")) {
			if (accountSettings.accountId) {
				if (sipuri.domain != get_account_domain()) {
					reject = true;
				}
			}
		}
		else if (accountSettings.denyIncoming == _T("address")) {
			SIPURI sipuri_curr;
			ParseSIPURI(PjToStr(&call_info.local_info, TRUE), &sipuri_curr);
			if (sipuri_curr.user != get_account_username() || (get_account_domain() != _T("") && sipuri_curr.domain != get_account_domain())) {
				reject = true;
			}
		}
		if (reject) {
			if (reason.IsEmpty()) {
				reason = _T("Denied");
			}
			msip_call_busy(call_info.id, reason);
			goto return_on_incoming_call;
		}

		accountSettings.lastCallNumber = sipuri.user;
		accountSettings.lastCallHasVideo = false;

		bool autoAnswer = false;
		int autoAnswerDelay = accountSettings.autoAnswerDelay;
		bool noRingDialog = false;
		if (accountSettings.autoAnswer == _T("all")) {
			autoAnswer = true;
		}
		else if (accountSettings.autoAnswer == _T("button")) {
			autoAnswer = accountSettings.AA;
		}
		else if (accountSettings.autoAnswer == _T("header")) {
			//--
			pjsip_generic_string_hdr *hsr = NULL;
			const pj_str_t header = pj_str("X-AUTOANSWER");
			hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &header, NULL);
			if (hsr) {
				CString autoAnswerValue = PjToStr(&hsr->hvalue, TRUE);
				autoAnswerValue.MakeLower();
				if (autoAnswerValue == _T("true") || autoAnswerValue == _T("1")) {
					autoAnswer = true;
				}
			}
			//--
			if (!autoAnswer) {
				pjsip_generic_string_hdr *hsr = NULL;
				const pj_str_t header = pj_str("Call-Info");
				hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &header, NULL);
				if (hsr) {
					CString callInfoValue = PjToStr(&hsr->hvalue, TRUE);
					callInfoValue.MakeLower();
					if (callInfoValue.Find(_T("auto answer")) != -1) {
						autoAnswer = true;
					}
					else {
						CAtlRegExp<> regex;
						REParseError parseStatus = regex.Parse(_T("answer-after={[0-9]+}"), true);
						if (parseStatus == REPARSE_ERROR_OK) {
							CAtlREMatchContext<> mc;
							if (regex.Match(callInfoValue, &mc) && mc.m_uNumGroups == 1) {
								const CAtlREMatchContext<>::RECHAR* szStart = 0;
								const CAtlREMatchContext<>::RECHAR* szEnd = 0;
								mc.GetMatch(0, &szStart, &szEnd);
								ptrdiff_t nLength = szEnd - szStart;
								CStringA text(szStart, nLength);
								autoAnswerDelay = atoi(text);
								autoAnswer = true;
							}
						}
					}
				}
			}
		}
		if (autoAnswer && autoAnswerDelay > 0) {
			autoAnswer = false;
			if (mainDlg->autoAnswerCallId == PJSUA_INVALID_ID) {
				mainDlg->autoAnswerCallId = call_id;
				mainDlg->SetTimer(IDT_TIMER_AUTOANSWER, autoAnswerDelay * 1000, NULL);
			}
		}
		BOOL playBeep = FALSE;
		if (autoAnswer
			&& calls_count <= 1
			) {
			mainDlg->AutoAnswer(call_id);
		}
		else {
			if (accountSettings.hidden) {
				mainDlg->PostMessage(UM_CALL_HANGUP, (WPARAM)call_id, NULL);
			}
			else {
				if (!noRingDialog) {
					//--
					const pj_str_t headerUserAgent = { "User-Agent",10 };
					pjsip_generic_string_hdr *hsr = NULL;
					hsr = (pjsip_generic_string_hdr*)pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &headerUserAgent, NULL);
					if (hsr) {
						user_data->userAgent = PjToStr(&hsr->hvalue, true);
					}
					//--
					mainDlg->PostMessage(UM_CREATE_RINGING, (WPARAM)call_id, NULL);
				}
				pjsua_call_answer(call_id, 180, NULL, NULL);
				if (call_get_count_noincoming()) {
					playBeep = TRUE;
				}
				else {
					if (!accountSettings.ringtone.GetLength()) {
						mainDlg->PostMessage(UM_ON_PLAYER_PLAY, MSIP_SOUND_RINGTONE, 0);
					}
					else {
						mainDlg->PostMessage(UM_ON_PLAYER_PLAY, MSIP_SOUND_CUSTOM, (LPARAM)&accountSettings.ringtone);
					}
				}
				//--
				if (!accountSettings.cmdCallRing.IsEmpty()) {
					CString params = sipuri.user;
					RunCmd(accountSettings.cmdCallRing, params);
				}
				//--
			}
		}
		if (accountSettings.localDTMF && playBeep) {
			mainDlg->PostMessage(UM_ON_PLAYER_PLAY, MSIP_SOUND_RINGIN2, 0);
		}
	}
return_on_incoming_call:
	user_data->CS.Unlock();
}

static void on_nat_detect(const pj_stun_nat_detect_result *res)
{
	if (res->status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILENAME, "NAT detection failed", res->status);
	}
	else {
		if (res->nat_type == PJ_STUN_NAT_TYPE_SYMMETRIC) {
			if (IsWindow(mainDlg->m_hWnd)) {
				CString message;
				pjsua_acc_config acc_cfg;
				pj_pool_t *pool;
				pool = pjsua_pool_create("acc_cfg-pjsua", 1000, 1000);
				if (pool) {
					pjsua_acc_id ids[PJSUA_MAX_ACC];
					unsigned count = PJSUA_MAX_ACC;
					if (pjsua_enum_accs(ids, &count) == PJ_SUCCESS) {
						for (unsigned i = 0; i < count; i++) {
							if (pjsua_acc_get_config(ids[i], pool, &acc_cfg) == PJ_SUCCESS) {
								acc_cfg.sip_stun_use = PJSUA_STUN_USE_DISABLED;
								acc_cfg.media_stun_use = PJSUA_STUN_USE_DISABLED;
								if (pjsua_acc_modify(ids[i], &acc_cfg) == PJ_SUCCESS) {
									message = _T("STUN was automatically disabled.");
									message.Append(_T(" For more info visit MicroSIP website, help page."));

								}
							}
						}
					}
					pj_pool_release(pool);
				}
				mainDlg->BaloonPopup(_T("Symmetric NAT detected!"), message);
			}
		}
		PJ_LOG(3, (THIS_FILENAME, "NAT detected as %s", res->nat_type_name));
	}
}

static void on_buddy_state(pjsua_buddy_id buddy_id)
{
	if (IsWindow(mainDlg->m_hWnd)) {
		pjsua_buddy_info buddy_info;
		if (pjsua_buddy_get_info(buddy_id, &buddy_info) == PJ_SUCCESS) {
			Contact *contact = (Contact *)pjsua_buddy_get_user_data(buddy_id);
			if (contact) {
				int image;
				bool ringing = false;
				switch (buddy_info.status)
				{
				case PJSUA_BUDDY_STATUS_OFFLINE:
					image = MSIP_CONTACT_ICON_OFFLINE;
					break;
				case PJSUA_BUDDY_STATUS_ONLINE:
					if (PJRPID_ACTIVITY_UNKNOWN && !buddy_info.rpid.activity) {
						image = MSIP_CONTACT_ICON_ON_THE_PHONE;
						if (PjToStr(&buddy_info.status_text).Left(4) == _T("Ring")) {
							ringing = true;
						}
					}
					else
					if (buddy_info.rpid.activity == PJRPID_ACTIVITY_AWAY)
					{
						image = MSIP_CONTACT_ICON_AWAY;
					}
					else if (buddy_info.rpid.activity == PJRPID_ACTIVITY_BUSY)
					{
						image = MSIP_CONTACT_ICON_BUSY;

					}
					else
					{
						image = MSIP_CONTACT_ICON_ONLINE;
					}
					break;
				default:
					image = MSIP_CONTACT_ICON_UNKNOWN;
					break;
				}
				contact->image = image;
				contact->ringing = ringing;
				if (contact->presenceNote.IsEmpty()) {
					contact->presenceNote = PjToStr(&buddy_info.status_text);
				}
				else if (pj_strcmp2(&buddy_info.status_text, "?") != 0) {
					contact->presenceNote = PjToStr(&buddy_info.status_text);
				}
				mainDlg->PostMessage(UM_ON_BUDDY_STATE, (WPARAM)contact);
			}
		}
	}
}

LRESULT CmainDlg::onBuddyState(WPARAM wParam, LPARAM lParam)
{
	Contact *contact = (Contact *)wParam;
	CListCtrl *list = (CListCtrl*)pageContacts->GetDlgItem(IDC_CONTACTS);
	int n = list->GetItemCount();
	bool found = false;
	for (int i = 0; i < n; i++) {
		if (contact == (Contact *)list->GetItemData(i)) {
			list->SetItem(i, 0, LVIF_IMAGE, 0, contact->image, 0, 0, 0);
			list->SetItemText(i, 2, Translate(contact->presenceNote.GetBuffer()));
			found = true;
		}
	}
	if (found && contact->ringing) {
		OnTimerContactBlink();
		SetTimer(IDT_TIMER_CONTACTS_BLINK, 500, NULL);
	}
	return 0;
}

static void on_pager2(pjsua_call_id call_id, const pj_str_t *from, const pj_str_t *to, const pj_str_t *contact, const pj_str_t *mime_type, const pj_str_t *body, pjsip_rx_data *rdata, pjsua_acc_id acc_id)
{
	if (pj_strcmp2(mime_type, "text/plain") != 0) {
		return;
	}
	if (IsWindow(mainDlg->m_hWnd)) {
		CString *number = new CString();
		CString *message = new CString();
		number->SetString(PjToStr(from, TRUE));
		message->SetString(PjToStr(body, TRUE));
		message->Trim();
		//-- fix wrong domain
		SIPURI sipuri;
		ParseSIPURI(*number, &sipuri);
		if (accountSettings.accountId) {
			if (IsIP(sipuri.domain)) {
				sipuri.domain = get_account_domain();
			}
			if (!sipuri.user.IsEmpty()) {
				number->Format(_T("%s@%s"), sipuri.user, sipuri.domain);
			}
			else {
				number->SetString(sipuri.domain);
			}
		}
		//--
		mainDlg->PostMessage(UM_ON_PAGER, (WPARAM)number, (LPARAM)message);
	}
}

static void on_pager_status2(pjsua_call_id call_id, const pj_str_t *to, const pj_str_t *body, void *user_data, pjsip_status_code status, const pj_str_t *reason, pjsip_tx_data *tdata, pjsip_rx_data *rdata, pjsua_acc_id acc_id)
{
	if (status != 200) {
		if (IsWindow(mainDlg->m_hWnd)) {
			CString *number = new CString();
			CString *message = new CString();
			number->SetString(PjToStr(to, TRUE));
			message->SetString(PjToStr(reason, TRUE));
			message->Trim();
			//-- fix wrong domain
			SIPURI sipuri;
			ParseSIPURI(*number, &sipuri);
			if (accountSettings.accountId) {
				if (IsIP(sipuri.domain)) {
					sipuri.domain = get_account_domain();
				}
				if (!sipuri.user.IsEmpty()) {
					number->Format(_T("%s@%s"), sipuri.user, sipuri.domain);
				}
				else {
					number->SetString(sipuri.domain);
				}
			}
			//--
			mainDlg->PostMessage(UM_ON_PAGER_STATUS, (WPARAM)number, (LPARAM)message);
		}
	}
}

static void on_call_transfer_status(pjsua_call_id call_id,
	int status_code,
	const pj_str_t *status_text,
	pj_bool_t final,
	pj_bool_t *p_cont)
{
	pjsua_call_info *call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_info->id);

	CString *str = new CString();
	str->Format(_T("%s: %s"),
		Translate(_T("Call Transfer")),
		PjToStr(status_text, TRUE)
	);
	if (final) {
		str->AppendFormat(_T(" [%s]"), Translate(_T("Final")));
	}

	if (status_code / 100 == 2) {
		*p_cont = PJ_FALSE;
	}

	call_info->last_status = (pjsip_status_code)status_code;

	call_info->call_id.ptr = (char *)user_data;
	call_info->call_id.slen = 0;

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_TRANSFER_STATUS, (WPARAM)call_info, (LPARAM)str);
}

LRESULT CmainDlg::onCallTransferStatus(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_info *call_info = (pjsua_call_info *)wParam;
	call_user_data *user_data = (call_user_data *)call_info->call_id.ptr;
	CString *str = (CString *)lParam;


	MessagesContact* messagesContact = NULL;
	CString number = PjToStr(&call_info->remote_info, TRUE);
	messagesContact = mainDlg->messagesDlg->AddTab(number, _T(""), FALSE, call_info, user_data, TRUE, TRUE);
	if (messagesContact) {
		mainDlg->messagesDlg->AddMessage(messagesContact, *str);
	}
	if (call_info->last_status / 100 == 2) {
		if (messagesContact) {
			messagesDlg->AddMessage(messagesContact, Translate(_T("Call transfered successfully, disconnecting call")));
		}
		msip_call_hangup_fast(call_info->id);
	}
	delete call_info;
	delete str;
	return 0;
}

static void on_call_transfer_request2(pjsua_call_id call_id, const pj_str_t *dst, pjsip_status_code *code, pjsua_call_setting *opt)
{
	SIPURI sipuri;
	ParseSIPURI(PjToStr(dst, TRUE), &sipuri);
	pj_bool_t cont;
	CString number = sipuri.user;
	if (number.IsEmpty()) {
		number = sipuri.domain;
	}
	else if (!accountSettings.accountId || sipuri.domain != get_account_domain()) {
		number.Append(_T("@") + sipuri.domain);
	}
	pj_str_t status_text = StrToPjStr(number);
	on_call_transfer_status(call_id,
		0,
		&status_text,
		PJ_FALSE,
		&cont);
	//--
	if (!code) {
		// if our function call
		return;
	}
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS || call_info.state != PJSIP_INV_STATE_CONFIRMED) {
		*code = PJSIP_SC_DECLINE;
	}
	if (*code != PJSIP_SC_DECLINE) {
		// deny transfer if we already have a call with same dest address
		pjsua_call_id call_ids[PJSUA_MAX_CALLS];
		unsigned calls_count = PJSUA_MAX_CALLS;
		unsigned calls_count_cmp = 0;
		if (pjsua_enum_calls(call_ids, &calls_count) == PJ_SUCCESS) {
			for (unsigned i = 0; i < calls_count; ++i) {
				pjsua_call_info call_info_curr;
				if (pjsua_call_get_info(call_ids[i], &call_info_curr) == PJ_SUCCESS) {
					SIPURI sipuri_curr;
					ParseSIPURI(PjToStr(&call_info_curr.remote_info, TRUE), &sipuri_curr);
					if (sipuri.user + _T("@") + sipuri.domain == sipuri_curr.user + _T("@") + sipuri_curr.domain
						) {
						*code = PJSIP_SC_DECLINE;
						break;
					}
				}
			}
		}
	}
}

static void on_call_replace_request2(pjsua_call_id call_id, pjsip_rx_data *rdata, int *st_code, pj_str_t *st_text, pjsua_call_setting *opt)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
		if (!call_info.rem_vid_cnt) {
			opt->vid_cnt = 0;
		}
	}
	else {
		opt->vid_cnt = 0;
	}
}

static void on_call_replaced(pjsua_call_id old_call_id, pjsua_call_id new_call_id)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(new_call_id, &call_info) == PJ_SUCCESS) {
		on_call_transfer_request2(old_call_id, &call_info.remote_info, NULL, NULL);
	}
}

static void on_mwi_info(pjsua_acc_id acc_id, pjsua_mwi_info *mwi_info)
{
	if (mwi_info->rdata->msg_info.ctype) {
		const pjsip_ctype_hdr *ctype = mwi_info->rdata->msg_info.ctype;
		if (pj_strcmp2(&ctype->media.type, "application") != 0 || pj_strcmp2(&ctype->media.subtype, "simple-message-summary") != 0) {
			return;
		}
	}
	if (!mwi_info->rdata->msg_info.msg->body || !mwi_info->rdata->msg_info.msg->body->len) {
		return;
	}
	pjsip_msg_body *body = mwi_info->rdata->msg_info.msg->body;
	pj_scanner scanner;

	pj_scan_init(&scanner, (char*)body->data, body->len, PJ_SCAN_AUTOSKIP_WS, 0);
	bool hasMail = false;
	while (!pj_scan_is_eof(&scanner)) {
		pj_str_t key;
		pj_scan_get_until_chr(&scanner, ":", &key);
		pj_strtrim(&key);
		if (key.slen && !pj_scan_is_eof(&scanner)) {
			scanner.curptr++;
			pj_str_t value;
			pj_scan_get_until_chr(&scanner, "\r\n", &value);
			pj_strtrim(&value);
			if (pj_stricmp2(&key, "Messages-Waiting") == 0) {
				hasMail = pj_stricmp2(&value, "yes") == 0;
				break;
			}
		}
	}
	pj_scan_fini(&scanner);
	PostMessage(mainDlg->m_hWnd, UM_ON_MWI_INFO, (WPARAM)hasMail, 0);
}

LRESULT CmainDlg::onMWIInfo(WPARAM wParam, LPARAM lParam)
{
	bool hasMail = (bool)wParam;
	pageDialer->UpdateVoicemailButton(hasMail);
	return 0;
}

static void on_dtmf_digit(pjsua_call_id call_id, int digit)
{
	char signal[2];
	signal[0] = digit;
	signal[1] = 0;
	call_play_digit(-1, signal);
}

static void on_call_tsx_state(pjsua_call_id call_id, pjsip_transaction *tsx, pjsip_event *e)
{
	const pjsip_method info_method = {
		PJSIP_OTHER_METHOD,
		{ "INFO", 4 }
	};
	if (pjsip_method_cmp(&tsx->method, &info_method) == 0) {
		/*
		* Handle INFO method.
		*/
		if (tsx->role == PJSIP_ROLE_UAS && tsx->state == PJSIP_TSX_STATE_TRYING) {
			if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
				pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;
				pjsip_msg_body *body = rdata->msg_info.msg->body;
				int code = 0;
				if (body && body->len
					&& pj_strcmp2(&body->content_type.type, "application") == 0
					&& pj_strcmp2(&body->content_type.subtype, "dtmf-relay") == 0) {
					code = 400;
					pj_scanner scanner;
					pj_scan_init(&scanner, (char*)body->data, body->len, PJ_SCAN_AUTOSKIP_WS, 0);
					char digit;
					int duration = 250;
					while (!pj_scan_is_eof(&scanner)) {
						pj_str_t key;
						pj_scan_get_until_chr(&scanner, "=", &key);
						pj_strtrim(&key);
						if (key.slen && !pj_scan_is_eof(&scanner)) {
							scanner.curptr++;
							pj_str_t value;
							pj_scan_get_until_chr(&scanner, "\r\n", &value);
							pj_strtrim(&value);
							if (pj_stricmp2(&key, "Signal") == 0) {
								if (value.slen == 1) {
									digit = *value.ptr;
									code = 200;
								}
							}
							else if (pj_stricmp2(&key, "Duration") == 0) {
								int res = 0;
								for (int i = 0; i < (unsigned)value.slen; ++i) {
									res = res * 10 + (value.ptr[i] - '0');
									res = res;
								}
								if (res >= 100 || res <= 5000) {
									duration = res;
								}
							}
						}
					}
					pj_scan_fini(&scanner);
					if (code == 200) {
						on_dtmf_digit(-1, digit);
					}
				}
				else if (!body || !body->len) {
					/* 200/OK */
					code = 200;
				}
				if (code) {
					/* Answer incoming INFO */
					pjsip_tx_data *tdata;
					if (pjsip_endpt_create_response(tsx->endpt, rdata,
						code, NULL, &tdata) == PJ_SUCCESS
						) {
						pjsip_tsx_send_msg(tsx, tdata);
					}
				}
			}
		}
	}
	else {
		if (tsx->state == PJSIP_TSX_STATE_COMPLETED) {
			// display declined REFER status
			const pjsip_method refer_method = {
				PJSIP_OTHER_METHOD,
				{ "REFER", 5 }
			};
			if (pjsip_method_cmp(&tsx->method, &refer_method) == 0 && tsx->status_code / 100 != 2) {
				pj_bool_t cont;
				on_call_transfer_status(call_id,
					tsx->status_code,
					&tsx->status_text,
					PJ_FALSE,
					&cont);
			}
		}
	}
}

CmainDlg::~CmainDlg(void)
{
}

void CmainDlg::OnDestroy()
{
	if (mmNotificationClient) {
		delete mmNotificationClient;
	}
	WTSUnRegisterSessionNotification(m_hWnd);

	PJDestroy();

	accountSettings.SettingsSave();

	RemoveJumpList();
	if (tnd.hWnd) {
		Shell_NotifyIcon(NIM_DELETE, &tnd);
	}
	UnloadLangPackModule();

	CBaseDialog::OnDestroy();
}

void CmainDlg::PostNcDestroy()
{
	CBaseDialog::PostNcDestroy();
	delete this;
}

void CmainDlg::DoDataExchange(CDataExchange* pDX)
{
	CBaseDialog::DoDataExchange(pDX);
	//	DDX_Control(pDX, IDD_MAIN, *mainDlg);
	DDX_Control(pDX, IDC_MAIN_MENU, m_ButtonMenu);
}

BEGIN_MESSAGE_MAP(CmainDlg, CBaseDialog)
	ON_WM_CREATE()
	ON_WM_SYSCOMMAND()
	ON_WM_QUERYENDSESSION()
	ON_WM_TIMER()
	ON_WM_MOVE()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_CONTEXTMENU()
	ON_WM_DEVICECHANGE()
	ON_WM_WTSSESSION_CHANGE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_MAIN_MENU, OnBnClickedMenu)
	ON_MESSAGE(UM_UPDATEWINDOWTEXT, OnUpdateWindowText)
	ON_MESSAGE(UM_NOTIFYICON, onTrayNotify)
	ON_MESSAGE(UM_CREATE_RINGING, onCreateRingingDlg)
	ON_MESSAGE(UM_REFRESH_LEVELS, onRefreshLevels)
	ON_MESSAGE(UM_ON_REG_STATE2, onRegState2)
	ON_MESSAGE(UM_ON_CALL_STATE, onCallState)
	ON_MESSAGE(UM_ON_MWI_INFO, onMWIInfo)
	ON_MESSAGE(UM_ON_CALL_MEDIA_STATE, onCallMediaState)
	ON_MESSAGE(UM_ON_CALL_TRANSFER_STATUS, onCallTransferStatus)
	ON_MESSAGE(UM_ON_PLAYER_PLAY, onPlayerPlay)
	ON_MESSAGE(UM_ON_PLAYER_STOP, onPlayerStop)
	ON_MESSAGE(UM_ON_PAGER, onPager)
	ON_MESSAGE(UM_ON_PAGER_STATUS, onPagerStatus)
	ON_MESSAGE(UM_ON_BUDDY_STATE, onBuddyState)
	ON_MESSAGE(UM_USERS_DIRECTORY, onUsersDirectoryLoaded)
	ON_MESSAGE(WM_POWERBROADCAST, onPowerBroadcast)
	ON_MESSAGE(WM_COPYDATA, onCopyData)
	ON_MESSAGE(UM_CALL_ANSWER, onCallAnswer)
	ON_MESSAGE(UM_CALL_HANGUP, onCallHangup)
	ON_MESSAGE(UM_TAB_ICON_UPDATE, onTabIconUpdate)
	ON_MESSAGE(UM_SET_PANE_TEXT, onSetPaneText)
	ON_MESSAGE(UM_ON_ACCOUNT, OnAccount)
	ON_COMMAND(ID_ACCOUNT_ADD, OnMenuAccountAdd)
	ON_COMMAND_RANGE(ID_ACCOUNT_CHANGE_RANGE, ID_ACCOUNT_CHANGE_RANGE + 99, OnMenuAccountChange)
	ON_COMMAND_RANGE(ID_ACCOUNT_EDIT_RANGE, ID_ACCOUNT_EDIT_RANGE + 99, OnMenuAccountEdit)
	ON_COMMAND(ID_ACCOUNT_EDIT_LOCAL, OnMenuAccountLocalEdit)
	ON_COMMAND_RANGE(ID_CUSTOM_RANGE, ID_CUSTOM_RANGE + 99, OnMenuCustomRange)
	ON_MESSAGE(UM_UPDATE_CHECKER_LOADED, OnUpdateCheckerLoaded)
	ON_COMMAND(ID_SETTINGS, OnMenuSettings)
	ON_COMMAND(ID_SHORTCUTS, OnMenuShortcuts)
	ON_COMMAND(ID_ALWAYS_ON_TOP, OnMenuAlwaysOnTop)
	ON_COMMAND(ID_LOG, OnMenuLog)
	ON_COMMAND(ID_EXIT, OnMenuExit)
	ON_NOTIFY(TCN_SELCHANGE, IDC_MAIN_TAB, &CmainDlg::OnTcnSelchangeTab)
	ON_NOTIFY(TCN_SELCHANGING, IDC_MAIN_TAB, &CmainDlg::OnTcnSelchangingTab)
	ON_COMMAND(ID_MENU_WEBSITE, OnMenuWebsite)
	ON_COMMAND(ID_MENU_HELP, OnMenuHelp)
	ON_COMMAND(ID_MENU_ADDL, OnMenuAddl)
END_MESSAGE_MAP()


BOOL CmainDlg::PreTranslateMessage(MSG* pMsg)
{
	BOOL catched = FALSE;
	if (accountSettings.enableMediaButtons) {
		if (pMsg->message == WM_SHELLHOOKMESSAGE) {
			onShellHookMessage(pMsg->wParam, pMsg->lParam);
		}
	}
	if (!catched) {
		return CBaseDialog::PreTranslateMessage(pMsg);
	}
	else {
		return TRUE;
	}
}

// CmainDlg message handlers

void CmainDlg::OnBnClickedOk()
{
}

void CmainDlg::OnBnClickedMenu()
{
	m_ButtonMenu.ModifyStyle(BS_DEFPUSHBUTTON, BS_PUSHBUTTON);
	MainPopupMenu(true);
	TabFocusSet();
}

CmainDlg::CmainDlg(CWnd* pParent /*=NULL*/)
	: CBaseDialog(CmainDlg::IDD, pParent)
{
#ifdef _DEBUG
	if (AllocConsole()) {
		HANDLE console = NULL;
		console = GetStdHandle(STD_OUTPUT_HANDLE);
		freopen("CONOUT$", "wt", stdout);
	}
#endif

	this->m_hWnd = NULL;
	mmNotificationClient = NULL;

	mainDlg = this;
	widthAdd = 0;
	heightAdd = 0;

	m_tabPrev = -1;
	newMessages = false;
	missed = false;

	usersDirectoryLoaded = false;

	CString audioCodecsCaptions = _T("opus/48000/2;Opus 24 kHz;\
PCMA/8000/1;G.711 A-law;\
PCMU/8000/1;G.711 u-law;\
G722/16000/1;G.722 16 kHz;\
G7221/16000/1;G.722.1 16 kHz;\
G7221/32000/1;G.722.1 32 kHz;\
G723/8000/1;G.723 8 kHz;\
G729/8000/1;G.729 8 kHz;\
GSM/8000/1;GSM 8 kHz;\
GSM-EFR/8000/1;GSM-EFR 8 kHz;\
AMR/8000/1;AMR 8 kHz;\
AMR-WB/16000/1;AMR-WB 16 kHz;\
iLBC/8000/1;iLBC 8 kHz;\
speex/32000/1;Speex 32 kHz;\
speex/16000/1;Speex 16 kHz;\
speex/8000/1;Speex 8 kHz;\
SILK/24000/1;SILK 24 kHz;\
SILK/16000/1;SILK 16 kHz;\
SILK/12000/1;SILK 12 kHz;\
SILK/8000/1;SILK 8 kHz;\
L16/8000/1;LPCM 8 kHz;\
L16/8000/2;LPCM 8 kHz Stereo;\
L16/16000/1;LPCM 16 kHz;\
L16/16000/2;LPCM 16 kHz Stereo;\
L16/44100/1;LPCM 44 kHz;\
L16/44100/2;LPCM 44 kHz Stereo;\
L16/48000/1;LPCM 48 kHz;\
L16/48000/2;LPCM 48 kHz Stereo");
	int pos = 0;
	CString resToken = audioCodecsCaptions.Tokenize(_T(";"), pos);
	while (!resToken.IsEmpty()) {
		audioCodecList.AddTail(resToken);
		resToken = audioCodecsCaptions.Tokenize(_T(";"), pos);
	}

	wchar_t szBuf[STR_SZ];
	wchar_t szLocale[STR_SZ];
	::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SENGLANGUAGE, szBuf, STR_SZ);
	_tcscpy(szLocale, szBuf);
	::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SENGCOUNTRY, szBuf, STR_SZ);
	if (_tcsclen(szBuf) != 0) {
		_tcscat(szLocale, _T("_"));
		_tcscat(szLocale, szBuf);
	}
	::GetLocaleInfo(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, szBuf, STR_SZ);
	if (_tcsclen(szBuf) != 0) {
		_tcscat(szLocale, _T("."));
		_tcscat(szLocale, szBuf);
	}
	_tsetlocale(LC_ALL, szLocale); // e.g. szLocale = "English_United States.1252"

	LoadLangPackModule();

	Create(IDD, pParent);
}

int CmainDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{

	bool setpos = false;
	if (accountSettings.noResize) {
		lpCreateStruct->style &= ~(WS_MAXIMIZEBOX | WS_THICKFRAME);
		::SetWindowLong(m_hWnd, GWL_STYLE, lpCreateStruct->style);
		CRect rectStub;
		GetClientRect(&rectStub);
		AdjustWindowRectEx(&rectStub, lpCreateStruct->style, FALSE, lpCreateStruct->dwExStyle);
		lpCreateStruct->cx = rectStub.Width();
		lpCreateStruct->cy = rectStub.Height();
		setpos = true;
	}

	ShortcutsLoad();
	shortcutsEnabled = accountSettings.enableShortcuts;
	shortcutsBottom = accountSettings.shortcutsBottom;
	if (accountSettings.enableShortcuts) {
		if (shortcutsBottom) {
			if (shortcuts.GetCount()) {
				heightAdd += 10 + shortcuts.GetCount() * 25;
			}
		}
		else {
			widthAdd += 140;
		}
	}
	int heightFix = 0;
	if (setpos || widthAdd || heightAdd || heightFix) {
		SetWindowPos(NULL, 0, 0, lpCreateStruct->cx + widthAdd, lpCreateStruct->cy + heightAdd + heightFix, SWP_NOMOVE | SWP_NOZORDER);
	}

	if (langPack.rtl) {
		ModifyStyleEx(0, WS_EX_LAYOUTRTL);
	}

	return CBaseDialog::OnCreate(lpCreateStruct);
}

BOOL CmainDlg::OnInitDialog()
{
	CBaseDialog::OnInitDialog();

	if (lstrcmp(theApp.m_lpCmdLine, _T("/hidden")) == 0) {
		accountSettings.hidden = TRUE;
		theApp.m_lpCmdLine = NULL;
	}

	WTSRegisterSessionNotification(m_hWnd, NOTIFY_FOR_THIS_SESSION);
	mmNotificationClient = new CMMNotificationClient();

	pj_ready = false;

	autoAnswerCallId = PJSUA_INVALID_ID;

	settingsDlg = NULL;
	shortcutsDlg = NULL;
	messagesDlg = new MessagesDlg(this);
	transferDlg = NULL;
	accountDlg = NULL;

	m_lastInputTime = 0;
	m_idleCounter = 0;
	m_PresenceStatus = PJRPID_ACTIVITY_UNKNOWN;

#ifdef _GLOBAL_VIDEO
	previewWin = NULL;
#endif

	if (!accountSettings.hidden) {

		SetupJumpList();

		m_hIcon = theApp.LoadIcon(IDR_MAINFRAME);
		iconSmall = (HICON)LoadImage(
			AfxGetInstanceHandle(),
			MAKEINTRESOURCE(IDR_MAINFRAME),
			IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
		PostMessage(WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

		TranslateDialog(this->m_hWnd);

		SetIcon(m_hIcon, TRUE);			// Set big icon
		SetIcon(m_hIcon, FALSE);		// Set small icon

		// TODO: Add extra initialization here

		// add tray icon
		ShowTrayIcon();
	}
	else {
		tnd.hWnd = NULL;
	}

	m_bar.Create(this);
	m_bar.SetIndicators(indicators, sizeof(indicators) / sizeof(indicators[0]));
	m_bar.SetPaneInfo(IDS_STATUSBAR, IDS_STATUSBAR, SBPS_STRETCH, 0);
	m_bar.SetPaneInfo(IDS_STATUSBAR2, IDS_STATUSBAR2, SBPS_NOBORDERS, 0);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, IDS_STATUSBAR);

	AutoMove(m_bar.m_hWnd, 0, 100, 100, 0);

	//--set window pos
	CRect screenRect;
	GetScreenRect(&screenRect);
	CRect clientRect;
	GetClientRect(&clientRect);
	CRect rect;
	GetWindowRect(&rect);

	int mx;
	int my;
	int mW = accountSettings.mainW > 0 ? accountSettings.mainW : rect.Width();
	int mH = accountSettings.mainH > 0 ? accountSettings.mainH : rect.Height();
	// coors not specified, first run
	if (!accountSettings.mainX && !accountSettings.mainY) {
		CRect primaryScreenRect;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &primaryScreenRect, 0);
		mx = primaryScreenRect.Width() - mW - widthAdd;
		my = primaryScreenRect.Height() - mH;
	}
	else {
		int maxLeft = screenRect.right - mW;
		if (accountSettings.mainX > maxLeft) {
			mx = maxLeft;
		}
		else {
			mx = accountSettings.mainX < screenRect.left ? screenRect.left : accountSettings.mainX;
		}
		int maxTop = screenRect.bottom - mH;
		if (accountSettings.mainY > maxTop) {
			my = maxTop;
		}
		else {
			my = accountSettings.mainY < screenRect.top ? screenRect.top : accountSettings.mainY;
		}
	}

	//--set messages window pos/size
	messagesDlg->GetWindowRect(&rect);
	int messagesX;
	int messagesY;
	int messagesW = accountSettings.messagesW > 0 ? accountSettings.messagesW : 550;
	int messagesH = accountSettings.messagesH > 0 ? accountSettings.messagesH : mH;
	// coors not specified, first run
	if (!accountSettings.messagesX && !accountSettings.messagesY) {
		accountSettings.messagesX = mx - messagesW;
		accountSettings.messagesY = my;
	}
	int maxLeft = screenRect.right - messagesW;
	if (accountSettings.messagesX > maxLeft) {
		messagesX = maxLeft;
	}
	else {
		messagesX = accountSettings.messagesX < screenRect.left ? screenRect.left : accountSettings.messagesX;
	}
	int maxTop = screenRect.bottom - messagesH;
	if (accountSettings.messagesY > maxTop) {
		messagesY = maxTop;
	}
	else {
		messagesY = accountSettings.messagesY < screenRect.top ? screenRect.top : accountSettings.messagesY;
	}

	messagesDlg->SetWindowPos(NULL, messagesX, messagesY, messagesW, messagesH, SWP_NOZORDER);

	SetWindowPos(accountSettings.alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost, mx, my, mW, mH, NULL);

	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	CRect tabRect;
	tab->GetWindowRect(&tabRect);
	ScreenToClient(&tabRect);
	TC_ITEM tabItem;
	CRect lineRect;
	lineRect.bottom = 3;
	MapDialogRect(&lineRect);
	tabRect.top += lineRect.bottom;
	tabRect.bottom += lineRect.bottom;
	tab->SetWindowPos(NULL, tabRect.left, tabRect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	tabItem.mask = TCIF_TEXT | TCIF_PARAM;

	m_ButtonMenu.SetIcon(LoadImageIcon(IDI_DROPDOWN));

	if (widthAdd) {
		CRect pageRect;
		m_ButtonMenu.GetWindowRect(pageRect);
		ScreenToClient(pageRect);
		m_ButtonMenu.SetWindowPos(NULL, pageRect.left + widthAdd, pageRect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
		//--
		tabRect.right += widthAdd;
		tab->SetWindowPos(NULL, 0, 0, tabRect.Width(), tabRect.Height(), SWP_NOZORDER | SWP_NOMOVE);
	}

	AutoMove(tab->m_hWnd, 0, 0, 100, 0);
	AutoMove(m_ButtonMenu.m_hWnd, 100, 0, 0, 0);

	BYTE offset = tabRect.bottom - 1;
	CRect pageRect;

	pageDialer = new Dialer(this);
	tabItem.pszText = Translate(_T("Phone"));
	tabItem.iImage = 0;
	tabItem.lParam = (LPARAM)pageDialer;
	tab->InsertItem(99, &tabItem);
	pageDialer->GetWindowRect(pageRect);
	int pageWidth = pageRect.Width() + (clientRect.Width() - pageRect.Width()) / 3;
	int offsetX = (clientRect.Width() - pageWidth) / 2;
	pageDialer->SetWindowPos(NULL, offsetX, offset, pageWidth, pageRect.Height(), SWP_NOZORDER);
	AutoMove(pageDialer->m_hWnd, 40, 40, 20, 20);

		pageCalls = new Calls(this);
		pageCalls->OnCreated();
		tabItem.pszText = Translate(_T("Logs"));
		tabItem.iImage = 1;
		tabItem.lParam = (LPARAM)pageCalls;
		tab->InsertItem(99, &tabItem);
		pageCalls->GetWindowRect(pageRect);
		pageCalls->SetWindowPos(NULL, 0, offset, pageRect.Width() + widthAdd, pageRect.Height() + heightAdd, SWP_NOZORDER);
		AutoMove(pageCalls->m_hWnd, 0, 0, 100, 100);

		pageContacts = new Contacts(this);
		pageContacts->OnCreated();
		tabItem.pszText = Translate(_T("Contacts"));
		tabItem.iImage = 2;
		tabItem.lParam = (LPARAM)pageContacts;
		tab->InsertItem(99, &tabItem);
		pageContacts->GetWindowRect(pageRect);
		pageContacts->SetWindowPos(NULL, 0, offset, pageRect.Width() + widthAdd, pageRect.Height() + heightAdd, SWP_NOZORDER);
		AutoMove(pageContacts->m_hWnd, 0, 0, 100, 100);


	tab->SetCurSel(accountSettings.activeTab);

	BOOL minimized = !lstrcmp(theApp.m_lpCmdLine, _T("/minimized"));
	if (minimized) {
		theApp.m_lpCmdLine = _T("");
	}
	if (accountSettings.silent) {
		minimized = true;
	}

	m_startMinimized = (!firstRun && minimized) || accountSettings.hidden;

	InitUI();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CmainDlg::InitUI()
{
	onMWIInfo(0, 0); // voicemail button
	SetPaneText2();
	SetWindowText(_T(_GLOBAL_NAME_NICE));
	UpdateWindowText();
	pageDialer->SetName();
}

void CmainDlg::ShowTrayIcon()
{
	// add tray icon
	CString str;
	str.Format(_T("%s %s"), _T(_GLOBAL_NAME_NICE), _T(_GLOBAL_VERSION));
	tnd.cbSize = sizeof(NOTIFYICONDATA);
	tnd.hWnd = this->GetSafeHwnd();
	tnd.uID = IDR_MAINFRAME;
	tnd.uCallbackMessage = UM_NOTIFYICON;
	tnd.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	iconMissed = LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_MISSED));
	iconInactive = LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_INACTIVE));
	tnd.hIcon = iconInactive;
	lstrcpyn(tnd.szTip, (LPCTSTR)str, sizeof(tnd.szTip));
	DWORD dwMessage = NIM_ADD;
	Shell_NotifyIcon(dwMessage, &tnd);
}

void CmainDlg::OnCreated()
{
	if (!m_startMinimized) {
		ShowWindow(SW_SHOW);
	}
	TabFocusSet();

	//-- PJSIP create
	while (!pj_ready) {
		PJCreate();
		if (pj_ready) {
			break;
		}
		if (AfxMessageBox(_T("Unable to initialize network sockets."), MB_RETRYCANCEL | MB_ICONEXCLAMATION) != IDRETRY) {
			DestroyWindow();
			return;
		}
	}
	if (lstrlen(theApp.m_lpCmdLine)) {
		CommandLine(theApp.m_lpCmdLine);
		theApp.m_lpCmdLine = NULL;
	}
	PJAccountAdd();
	//--
	WM_SHELLHOOKMESSAGE = RegisterWindowMessage(_T("SHELLHOOK"));
	if (WM_SHELLHOOKMESSAGE) {
		RegisterShellHookWindow(m_hWnd);
	}
}

void CmainDlg::BaloonPopup(CString title, CString message, DWORD flags)
{
	if (tnd.hWnd) {
		lstrcpyn(tnd.szInfo, message, sizeof(tnd.szInfo));
		lstrcpyn(tnd.szInfoTitle, title, sizeof(tnd.szInfoTitle));
		tnd.uFlags = tnd.uFlags | NIF_INFO;
		tnd.dwInfoFlags = flags;
		DWORD dwMessage = NIM_MODIFY;
		Shell_NotifyIcon(dwMessage, &tnd);
	}
}

void CmainDlg::OnMenuAccountAdd()
{
	if (!accountSettings.hidden) {
		if (!accountDlg) {
			accountDlg = new AccountDlg(this);
		}
		else {
			accountDlg->SetForegroundWindow();
		}
		if (accountDlg) {
			accountDlg->Load(-1);
		}
	}
}
void CmainDlg::OnMenuAccountChange(UINT nID)
{
	if (accountSettings.accountId) {
		PJAccountDelete(true);
	}
	int idNew = nID - ID_ACCOUNT_CHANGE_RANGE + 1;
	if (accountSettings.accountId != idNew) {
		accountSettings.accountId = idNew;
		accountSettings.AccountLoad(accountSettings.accountId, &accountSettings.account);
	}
	else {
		accountSettings.accountId = 0;
		InitUI();
	}
	OnAccountChanged();
	accountSettings.SettingsSave();
	mainDlg->PJAccountAdd();
}

void CmainDlg::OnMenuAccountEdit(UINT nID)
{
	if (!accountDlg) {
		accountDlg = new AccountDlg(this);
	}
	else {
		accountDlg->SetForegroundWindow();
	}
	if (accountDlg) {
		int id = accountSettings.accountId > 0 ? accountSettings.accountId : nID - ID_ACCOUNT_EDIT_RANGE + 1;
		accountDlg->Load(id ? id : -1);
	}
}

void CmainDlg::OnMenuAccountLocalEdit()
{
	if (MACRO_ENABLE_LOCAL_ACCOUNT) {
		if (!accountDlg) {
			accountDlg = new AccountDlg(this);
		}
		else {
			accountDlg->SetForegroundWindow();
		}
		if (accountDlg) {
			accountDlg->Load(0);
		}
	}
}

void CmainDlg::OnMenuCustomRange(UINT nID)
{
}

void CmainDlg::OnMenuSettings()
{
	if (!accountSettings.hidden) {
		if (!settingsDlg) {
			bool showDlg = true;
			if (showDlg) {
				settingsDlg = new SettingsDlg(this);
			}
		}
		else {
			settingsDlg->SetForegroundWindow();
		}
	}
}

void CmainDlg::OnMenuShortcuts()
{
	if (!shortcutsDlg)
	{
		shortcutsDlg = new ShortcutsDlg(this);
	}
	else {
		shortcutsDlg->SetForegroundWindow();
	}
}

void CmainDlg::OnMenuAlwaysOnTop()
{
	accountSettings.alwaysOnTop = 1 - accountSettings.alwaysOnTop;
	AccountSettingsPendingSave();
	SetWindowPos(accountSettings.alwaysOnTop ? &this->wndTopMost : &this->wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void CmainDlg::OnMenuLog()
{
	ShellExecute(NULL, NULL, accountSettings.logFile, NULL, NULL, SW_SHOWNORMAL);
}

void CmainDlg::OnMenuExit()
{
	this->DestroyWindow();
}

LRESULT CmainDlg::onTrayNotify(WPARAM wParam, LPARAM lParam)
{
	UINT uMsg = (UINT)lParam;
	switch (uMsg)
	{
	case NIN_BALLOONUSERCLICK:
		onTrayNotify(NULL, WM_LBUTTONUP);
		break;
	case WM_LBUTTONUP:
		if (this->IsWindowVisible() && !IsIconic())
		{
			if (wParam) {
				ShowWindow(SW_HIDE);
			}
			else {
				//set up a generic keyboard event
				INPUT keyInput;
				keyInput.type = INPUT_KEYBOARD;
				keyInput.ki.wScan = 0; //hardware scan code for key
				keyInput.ki.time = 0;
				keyInput.ki.dwExtraInfo = 0;

				//set focus to the hWnd (sending Alt allows to bypass limitation)
				keyInput.ki.wVk = VK_MENU;
				keyInput.ki.dwFlags = 0;   //0 for key press
				SendInput(1, &keyInput, sizeof(INPUT));

				SetForegroundWindow(); //sets the focus 

				keyInput.ki.wVk = VK_MENU;
				keyInput.ki.dwFlags = KEYEVENTF_KEYUP;  //for key release
				SendInput(1, &keyInput, sizeof(INPUT));
			}
		}
		else
		{
			bool blockRestore = false;
			if (accountSettings.hidden) {
				blockRestore = true;
			}
			if (!blockRestore) {
				if (IsIconic()) {
					ShowWindow(SW_RESTORE);
				}
				else {
					ShowWindow(SW_SHOW);
				}
				SetForegroundWindow();
				if (missed) {
					GotoTabLParam((LPARAM)pageCalls);
					missed = false;
					UpdateWindowText();
				}
				// -- show ringing dialogs
				int count = ringinDlgs.GetCount();
				for (int i = 0; i < count; i++) {
					RinginDlg *ringinDlg = ringinDlgs.GetAt(i);
					ringinDlg->ShowWindow(SW_SHOWNORMAL);
				}
				// -- show messages dialog
				if (accountSettings.silent && ((!accountSettings.singleMode && call_get_count_noincoming()) || newMessages)) {
					newMessages = false;
					messagesDlg->ShowWindow(SW_SHOW);
				}
				//--
				TabFocusSet();
			}
		}
		break;
	case WM_RBUTTONUP:
		MainPopupMenu();
		break;
	}
	return TRUE;
}

void CmainDlg::MainPopupMenu(bool isMenuButton)
{
	CString str;
	CPoint point;
	if (isMenuButton) {
		CWnd *menuButton = mainDlg->GetDlgItem(IDC_MAIN_MENU);
		CRect rect;
		menuButton->GetWindowRect(rect);
		point = rect.TopLeft();
	}
	else {
		GetCursorPos(&point);
	}
	CMenu menu;
	menu.CreatePopupMenu();
	CMenu* tracker = &menu;

		// -- add
		tracker->AppendMenu(MF_STRING, ID_ACCOUNT_ADD, Translate(_T("Add Account...")));
		//-- edit
		CMenu editMenu;
		editMenu.CreatePopupMenu();
		bool checked = false;
		Account acc;
		int i = 0;
		while (true) {
			if (!accountSettings.AccountLoad(i + 1, &acc)) {
				break;
			}
			if (!acc.label.IsEmpty()) {
				str = acc.label;
			}
			else {
				str.Format(_T("%s@%s"), acc.username, acc.domain);
			}
				tracker->InsertMenu(ID_ACCOUNT_ADD, (accountSettings.accountId == i + 1 ? MF_CHECKED : 0), ID_ACCOUNT_CHANGE_RANGE + i, str);
				editMenu.AppendMenu(MF_STRING, ID_ACCOUNT_EDIT_RANGE + i, str);
				if (!checked) {
					checked = accountSettings.accountId == i + 1;
				}
				i++;
		}
		if (i == 1) {
			MENUITEMINFO menuItemInfo;
			menuItemInfo.cbSize = sizeof(MENUITEMINFO);
			menuItemInfo.fMask = MIIM_STRING;
			menuItemInfo.dwTypeData = Translate(_T("Make Active"));
			tracker->SetMenuItemInfo(ID_ACCOUNT_CHANGE_RANGE, &menuItemInfo);
		}
		str = Translate(_T("Edit Account"));
		str.Append(_T("\tCtrl+M"));
		if (i == 1) {
				tracker->InsertMenu(ID_ACCOUNT_ADD, 0, ID_ACCOUNT_EDIT_RANGE, str);
		}
		else if (i > 1) {
			tracker->InsertMenu(ID_ACCOUNT_ADD, MF_SEPARATOR);
			if (checked) {
				tracker->InsertMenu(ID_ACCOUNT_ADD, 0, ID_ACCOUNT_EDIT_RANGE, str);
			}
			else {
				tracker->InsertMenu(ID_ACCOUNT_ADD, MF_POPUP, (UINT_PTR)editMenu.m_hMenu, Translate(_T("Edit Account")));
			}
		}

		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			str = Translate(_T("Edit Local Account"));
			str.Append(_T("\tCtrl+L"));
			tracker->AppendMenu(MF_STRING, ID_ACCOUNT_EDIT_LOCAL, str);
		}

	str = Translate(_T("Settings"));
	str.Append(_T("\tCtrl+P"));
	tracker->AppendMenu(MF_STRING, ID_SETTINGS, str);
	tracker->AppendMenu(MF_SEPARATOR);
	str = Translate(_T("Shortcuts"));
	str.Append(_T("\tCtrl+S"));
	tracker->AppendMenu(MF_STRING, ID_SHORTCUTS, str);

	bool separator = false;
	if (!separator) {
		tracker->AppendMenu(MF_SEPARATOR);
		separator = true;
	}
	tracker->AppendMenu(MF_STRING | (accountSettings.alwaysOnTop ? MF_CHECKED : 0), ID_ALWAYS_ON_TOP, Translate(_T("Always on Top")));
			if (!separator) {
				tracker->AppendMenu(MF_SEPARATOR);
				separator = true;
			}
			tracker->AppendMenu(MF_STRING | (!accountSettings.enableLog ? MF_DISABLED | MF_GRAYED : 0), ID_LOG, Translate(_T("View Log File")));

	separator = false;

	if (!separator) {
		tracker->AppendMenu(MF_SEPARATOR);
		separator = true;
	}
	str = Translate(_T("Visit Website"));
	str.Append(_T("\tCtrl+W"));
	tracker->AppendMenu(MF_STRING, ID_MENU_WEBSITE, str);

	if (!separator) {
		tracker->AppendMenu(MF_SEPARATOR);
		separator = true;
	}
	str = Translate(_T("Help"));
	str.AppendFormat(_T("\tver. %s"),_T(_GLOBAL_VERSION));
	tracker->AppendMenu(MF_STRING, ID_MENU_HELP, str);

	tracker->AppendMenu(MF_SEPARATOR);
	str = Translate(_T("Exit"));
	str.Append(_T("\tCtrl+Q"));
	tracker->AppendMenu(MF_STRING, ID_EXIT, str);

	MENUITEMINFO menuItemInfo;
	menuItemInfo.cbSize = sizeof(MENUITEMINFO);
	menuItemInfo.fMask = MIIM_FTYPE;
	tracker->GetMenuItemInfo(0, &menuItemInfo, TRUE);
	if (menuItemInfo.fType == MFT_SEPARATOR) {
		tracker->RemoveMenu(0, MF_BYPOSITION);
	}

	SetForegroundWindow();
	tracker->TrackPopupMenu(0, point.x, point.y, this);
	PostMessage(WM_NULL, 0, 0);
}

LRESULT CmainDlg::onCreateRingingDlg(WPARAM wParam, LPARAM lParam)
{
	pjsua_call_id call_id = wParam;
	pjsua_call_info call_info;

	if (pjsua_var.state != PJSUA_STATE_RUNNING || pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS) {
		int count = ringinDlgs.GetCount();
		if (!count) {
			PlayerStop();
		}
		return  0;
	}

	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_info.id);

	user_data->CS.Lock();

	RinginDlg* ringinDlg = new RinginDlg(this);

	ringinDlg->remoteHasVideo = call_info.rem_vid_cnt;
	if (call_info.rem_vid_cnt) {
		((CButton*)ringinDlg->GetDlgItem(IDC_VIDEO))->EnableWindow(TRUE);
	}
	ringinDlg->SetCallId(call_info.id);
	SIPURI sipuri;
	CStringW rab;
	CString str;
	CString info;

	if (user_data && !user_data->callerID.IsEmpty()) {
		info = user_data->callerID;
	} else {
		info = PjToStr(&call_info.remote_info, TRUE);
	}

	ParseSIPURI(info, &sipuri);

	CString name = pageContacts->GetNameByNumber(GetSIPURI(sipuri.user, true));
	if (name.IsEmpty()) {
		name = !sipuri.name.IsEmpty() ? sipuri.name : (!sipuri.user.IsEmpty() ? sipuri.user : sipuri.domain);
	}
	ringinDlg->GetDlgItem(IDC_CALLER_NAME)->SetWindowText(name);

	str.Empty();

	info = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;
	if (!sipuri.name.IsEmpty() && sipuri.name != name) {
		info = sipuri.name + _T(" <") + info + _T(">");
	}
	str.AppendFormat(_T("%s\r\n"), info);
	if (user_data && !user_data->userAgent.IsEmpty()) {
		str.AppendFormat(_T("%s\r\n"), user_data->userAgent);
	}
	str.Append(_T("\r\n"));
	info = PjToStr(&call_info.local_info, TRUE);
	ParseSIPURI(info, &sipuri);
	info = (!sipuri.user.IsEmpty() ? sipuri.user + _T("@") : _T("")) + sipuri.domain;
	str.AppendFormat(_T("%s: %s\r\n"), Translate(_T("To")), info);

	if (user_data && !user_data->diversion.IsEmpty()) {
		str.AppendFormat(_T("%s: %s\r\n"), Translate(_T("Diversion")), user_data->diversion);
	}
	if (str != name) {
		ringinDlg->GetDlgItem(IDC_CALLER_ADDR)->SetWindowText(str);
	}
	else {
		ringinDlg->GetDlgItem(IDC_CALLER_ADDR)->EnableWindow(FALSE);
	}
	ringinDlgs.Add(ringinDlg);
	if (!accountSettings.bringToFrontOnIncoming) {
		if (GetForegroundWindow()->GetTopLevelParent() != this) {
			BaloonPopup(Translate(_T("Incoming Call")), name, NIIF_INFO);
		}
	}

	user_data->CS.Unlock();

	return 0;
}

LRESULT CmainDlg::onRefreshLevels(WPARAM wParam, LPARAM lParam)
{
	pageDialer->OnHScroll(0, 0, NULL);
	return 0;
}

LRESULT CmainDlg::onPager(WPARAM wParam, LPARAM lParam)
{
	CString *number = (CString *)wParam;
	CString *message = (CString *)lParam;
	bool doNotShowMessagesWindow = accountSettings.silent && !mainDlg->IsWindowVisible();
	if (doNotShowMessagesWindow) {
		newMessages = true;
	}
	MessagesContact* messagesContact = messagesDlg->AddTab(*number,
		CString(), FALSE, NULL, NULL,
		doNotShowMessagesWindow
	);
	if (messagesContact) {
		messagesDlg->AddMessage(messagesContact, *message, MSIP_MESSAGE_TYPE_REMOTE);
		onPlayerPlay(MSIP_SOUND_MESSAGE_IN, 0);
	}
	delete number;
	delete message;
	return 0;
}

LRESULT CmainDlg::onPagerStatus(WPARAM wParam, LPARAM lParam)
{
	CString *number = (CString *)wParam;
	CString *message = (CString *)lParam;
	bool doNotShowMessagesWindow = accountSettings.silent && !mainDlg->IsWindowVisible();
	MessagesContact* messagesContact = mainDlg->messagesDlg->AddTab(*number,
		CString(), FALSE, NULL, NULL,
		doNotShowMessagesWindow);
	if (messagesContact) {
		mainDlg->messagesDlg->AddMessage(messagesContact, *message);
	}
	delete number;
	delete message;
	return 0;
}

LRESULT CmainDlg::onPowerBroadcast(WPARAM wParam, LPARAM lParam)
{
	if (wParam == PBT_APMRESUMEAUTOMATIC) {
		PJAccountAdd();
	}
	else if (wParam == PBT_APMSUSPEND) {
		PJAccountDelete();
	}
	return TRUE;
}

LRESULT CmainDlg::OnAccount(WPARAM wParam, LPARAM lParam)
{
	if (!accountDlg) {
		accountDlg = new AccountDlg(this);
	}
	else {
		accountDlg->SetForegroundWindow();
	}
	if (accountDlg) {
		accountDlg->Load(accountSettings.accountId ? accountSettings.accountId : -1);
		if (wParam && accountDlg) {
			CEdit *edit = (CEdit*)accountDlg->GetDlgItem(IDC_EDIT_PASSWORD);
			if (edit) {
				edit->SetFocus();
				int nLength = edit->GetWindowTextLength();
				edit->SetSel(nLength, nLength);
			}
		}
	}
	return 0;
}

void CmainDlg::OnTimerProgress()
{
}

void CmainDlg::OnTimerCall()
{
	pjsua_call_id call_id;
	int duration = messagesDlg->GetCallDuration(&call_id);
	if (duration != -1) {
		CString str;
		unsigned icon = IDI_ACTIVE;
		if (call_id != PJSUA_INVALID_ID) {
			int holdFrom = -1;
			if (pjsua_var.state == PJSUA_STATE_RUNNING) {
				call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_id);
				if (user_data) {
					user_data->CS.Lock();
					holdFrom = user_data->holdFrom;
					user_data->CS.Unlock();
				}
			}
			if (holdFrom != -1) {
				icon = IDI_HOLD;
				str.Format(_T("%s %s / %s"), Translate(_T("Hold")), GetDuration(duration - holdFrom, true), GetDuration(duration, true));
			}
			else {
				str.Format(_T("%s %s"), Translate(_T("Connected")), GetDuration(duration, true));
			}
		}
		else {
			str.Format(_T("%s (%d)"), Translate(_T("Connected")), duration);
		}
		if (call_id != PJSUA_INVALID_ID) {
			call_user_data *user_data;
			if (pjsua_var.state == PJSUA_STATE_RUNNING) {
				user_data = (call_user_data *)pjsua_call_get_user_data(call_id);
			}
			else {
				user_data = NULL;
			}
			if (user_data) {
				user_data->CS.Lock();
				if (user_data->srtp == MSIP_SRTP) {
					icon = IDI_ACTIVE_SECURE;
				}
				user_data->CS.Unlock();
			}
		}
		UpdateWindowText(str, icon);
	}
	else {
		KillTimer(IDT_TIMER_CALL);
	}
}

void CmainDlg::OnTimerContactBlink()
{
	CListCtrl *list = (CListCtrl*)pageContacts->GetDlgItem(IDC_CONTACTS);
	int n = list->GetItemCount();
	bool ringing = false;
	for (int i = 0; i < n; i++) {
		Contact *contact = (Contact *)list->GetItemData(i);
		if (contact->ringing) {
			list->SetItem(i, 0, LVIF_IMAGE, 0, timerContactBlinkState ? MSIP_CONTACT_ICON_BLANK : contact->image, 0, 0, 0);
			ringing = true;
		}
		else {
			list->SetItem(i, 0, LVIF_IMAGE, 0, contact->image, 0, 0, 0);
		}
	}
	if (!ringing) {
		KillTimer(IDT_TIMER_CONTACTS_BLINK);
		timerContactBlinkState = false;
	}
	else {
		timerContactBlinkState = !timerContactBlinkState;
	}
}

void CmainDlg::OnTimer(UINT_PTR TimerVal)
{
	if (TimerVal == IDT_TIMER_AUTOANSWER) {
		KillTimer(IDT_TIMER_AUTOANSWER);
		if (pjsua_var.state == PJSUA_STATE_RUNNING) {
			AutoAnswer(autoAnswerCallId);
		}
		autoAnswerCallId = PJSUA_INVALID_ID;
	}
	else if (TimerVal == IDT_TIMER_SWITCH_DEVICES) {
		KillTimer(IDT_TIMER_SWITCH_DEVICES);
		if (pjsua_var.state == PJSUA_STATE_RUNNING) {
			PJ_LOG(3, (THIS_FILENAME, "Execute refresh devices"));
			bool snd_is_active = pjsua_snd_is_active();
			bool is_ring;
			if (snd_is_active) {
				int in, out;
				if (pjsua_get_snd_dev(&in, &out) == PJ_SUCCESS) {
					is_ring = (out == msip_audio_ring);
				}
				else {
					is_ring = false;
				}
				pjsua_set_null_snd_dev();
			}
			pjmedia_aud_dev_refresh();
			UpdateSoundDevicesIds();
			if (snd_is_active) {
				msip_set_sound_device(is_ring ? msip_audio_ring : msip_audio_output, true);
			}
#ifdef _GLOBAL_VIDEO
			pjmedia_vid_subsys *vid_subsys = pjmedia_get_vid_subsys();
			if (vid_subsys->init_count) {
				pjmedia_vid_dev_refresh();
			}
#endif
		}
	}
	else if (TimerVal == IDT_TIMER_SAVE) {
		KillTimer(IDT_TIMER_SAVE);
		accountSettings.SettingsSave();
	}
	else if (TimerVal == IDT_TIMER_DIRECTORY) {
		UsersDirectoryLoad();
	}
	else if (TimerVal == IDT_TIMER_CONTACTS_BLINK) {
		OnTimerContactBlink();
	}
	else if (TimerVal == IDT_TIMER_PROGRESS) {
		OnTimerProgress();
	}
	else if (TimerVal == IDT_TIMER_CALL) {
		OnTimerCall();
	}
	else
			if (TimerVal == IDT_TIMER_IDLE) {
				if (pjsua_var.state == PJSUA_STATE_RUNNING && m_PresenceStatus != PJRPID_ACTIVITY_BUSY) {
					//--
					LASTINPUTINFO lii;
					lii.cbSize = sizeof(LASTINPUTINFO);
					if (GetLastInputInfo(&lii)) {
						if (lii.dwTime != m_lastInputTime) {
							m_lastInputTime = lii.dwTime;
							m_idleCounter = 0;
							if (m_PresenceStatus == PJRPID_ACTIVITY_AWAY) {
								PublishStatus();
							}
						}
						else {
							m_idleCounter++;
							if (m_idleCounter == 120) {
								PublishStatus(false);
							}
						}
					}
					//--
				}
			}
			else
				if (TimerVal = IDT_TIMER_TONE) {
					onPlayerPlay(MSIP_SOUND_RINGING, 0);
				}
}

void CmainDlg::PJCreate()
{
	player_eof_data = NULL;

	pageContacts->isSubscribed = FALSE;
	if (accountSettings.audioCodecs.IsEmpty())
	{
		accountSettings.audioCodecs = _T(_GLOBAL_CODECS_ENABLED);
	}

	// check updates
	if (accountSettings.updatesInterval != _T("never"))
	{
		CTime t = CTime::GetCurrentTime();
		time_t time = t.GetTime();
		int days;
		if (accountSettings.updatesInterval == _T("daily"))
		{
			days = 1;
		}
		else if (accountSettings.updatesInterval == _T("monthly"))
		{
			days = 30;
		}
		else if (accountSettings.updatesInterval == _T("quarterly"))
		{
			days = 90;
		}
		else
		{
			days = 7;
		}
		if (accountSettings.updatesInterval == _T("always") || accountSettings.checkUpdatesTime + days * 86400 < time) {
			CheckUpdates();
			accountSettings.checkUpdatesTime = time;
			accountSettings.SettingsSave();
		}
	}

	// pj create
	pj_status_t status;
	pjsua_config         ua_cfg;
	pjsua_media_config   media_cfg;
	pjsua_transport_config cfg;

	// Must create pjsua before anything else!
	status = pjsua_create();
	if (status != PJ_SUCCESS) {
		return;
	}

	// Initialize configs with default settings.
	pjsua_config_default(&ua_cfg);
	pjsua_media_config_default(&media_cfg);

	CString userAgent;
	if (accountSettings.userAgent.IsEmpty()) {
		userAgent.Format(_T("%s/%s"), _T(_GLOBAL_NAME_NICE), _T(_GLOBAL_VERSION));
		ua_cfg.user_agent = StrToPjStr(userAgent);
	}
	else {
		ua_cfg.user_agent = StrToPjStr(accountSettings.userAgent);
	}

	ua_cfg.cb.on_reg_state2 = &on_reg_state2;
	ua_cfg.cb.on_call_state = &on_call_state;
	ua_cfg.cb.on_dtmf_digit = &on_dtmf_digit;
	ua_cfg.cb.on_call_tsx_state = &on_call_tsx_state;

	ua_cfg.cb.on_call_media_state = &on_call_media_state;
	ua_cfg.cb.on_call_media_event = &on_call_media_event;
	ua_cfg.cb.on_incoming_call = &on_incoming_call;
	ua_cfg.cb.on_nat_detect = &on_nat_detect;
	ua_cfg.cb.on_buddy_state = &on_buddy_state;
	ua_cfg.cb.on_pager2 = &on_pager2;
	ua_cfg.cb.on_pager_status2 = &on_pager_status2;
	ua_cfg.cb.on_call_transfer_request2 = &on_call_transfer_request2;
	ua_cfg.cb.on_call_transfer_status = &on_call_transfer_status;

	ua_cfg.cb.on_call_replace_request2 = &on_call_replace_request2;
	ua_cfg.cb.on_call_replaced = &on_call_replaced;

	ua_cfg.cb.on_mwi_info = &on_mwi_info;
	
	ua_cfg.srtp_secure_signaling = 0;

	/*
	TODO: accountSettings.account: public_addr
	*/

	if (accountSettings.enableSTUN && !accountSettings.stun.IsEmpty()) {
		int pos = 0;
		int i = 0;
		while (i < 8) {
			CString resToken = accountSettings.stun.Tokenize(_T(";,"), pos);
			if (pos == -1) {
				break;
			}
			resToken.Trim();
			if (!resToken.IsEmpty()) {
				ua_cfg.stun_srv[i] = StrToPjStr(resToken);
				i++;
			}
		}
		ua_cfg.stun_srv_cnt = i;
	}

	media_cfg.enable_ice = PJ_FALSE;
	media_cfg.no_vad = accountSettings.vad ? PJ_FALSE : PJ_TRUE;
	media_cfg.ec_tail_len = accountSettings.ec ? PJSUA_DEFAULT_EC_TAIL_LEN : 0;

	int maxClockRate = 8000;
	int maxChannelCount = 1;
	int curPos = 0;
	CString resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
	while (!resToken.IsEmpty()) {
		int pos = 0;
		bool isOpus = resToken.Tokenize(_T("/"), pos) == _T("opus");
		int clockRate;
		if (isOpus) {
			clockRate = 24000;
		}
		else {
			clockRate = _wtoi(resToken.Tokenize(_T("/"), pos));
		}
		if (clockRate > maxClockRate) {
			maxClockRate = clockRate;
		}
		if (!accountSettings.ec && !isOpus) {
			BYTE channelCount = resToken.Tokenize(_T("/"), pos) == _T("2") ? 2 : 1;
			if (channelCount > maxChannelCount) {
				maxChannelCount = channelCount;
			}
		}
		resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
	}
	media_cfg.clock_rate = maxClockRate;
	media_cfg.channel_count = maxChannelCount;

	if (accountSettings.dnsSrv && !accountSettings.dnsSrvNs.IsEmpty()) {
		int pos = 0;
		int i = 0;
		while (i < 4) {
			CString resToken = accountSettings.dnsSrvNs.Tokenize(_T(";,"), pos);
			if (pos == -1) {
				break;
			}
			resToken.Trim();
			if (!resToken.IsEmpty()) {
				ua_cfg.nameserver[i] = StrToPjStr(resToken);
				i++;
			}
		}
		ua_cfg.nameserver_count = i;
	}

	// Initialize pjsua
	if (accountSettings.enableLog) {
		pjsua_logging_config log_cfg;
		pjsua_logging_config_default(&log_cfg);
		log_cfg.decor |= PJ_LOG_HAS_CR;
		char *buf = WideCharToPjStr(accountSettings.logFile);
		log_cfg.log_filename = pj_str(buf);
		status = pjsua_init(&ua_cfg, &log_cfg, &media_cfg);
		delete buf;
	}
	else {
		status = pjsua_init(&ua_cfg, NULL, &media_cfg);
	}

	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return;
	}

	// Start pjsua
	status = pjsua_start();

	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return;
	}

	if (!accountSettings.rport) {
		pjsip_cfg()->endpt.disable_rport = PJ_TRUE;
	}

	pj_ready = true;

	// Set snd devices
	UpdateSoundDevicesIds();

	//Set aud codecs prio
	PJ_LOG(3, (THIS_FILENAME, "Set audio codecs"));
	if (accountSettings.audioCodecs.GetLength())
	{
		// add unknown new codecs to the list
		unsigned count = PJMEDIA_CODEC_MGR_MAX_CODECS;
		pjsua_codec_info codec_info[PJMEDIA_CODEC_MGR_MAX_CODECS];
		if (pjsua_enum_codecs(codec_info, &count) == PJ_SUCCESS) {
			for (unsigned i = 0; i < count; i++) {
				pjsua_codec_set_priority(&codec_info[i].codec_id, PJMEDIA_CODEC_PRIO_DISABLED);
				CString rab = PjToStr(&codec_info[i].codec_id);
				if (!audioCodecList.Find(rab)) {
					audioCodecList.AddTail(rab);
					rab.Append(_T("~"));
					audioCodecList.AddTail(rab);
				}
			}
		}
		// remove unsupported codecs from list
		POSITION pos = audioCodecList.GetHeadPosition();
		while (pos) {
			POSITION posKey = pos;
			CString key = audioCodecList.GetNext(pos);
			POSITION posValue = pos;
			CString value = audioCodecList.GetNext(pos);
			pj_str_t codec_id = StrToPjStr(key);
			pjmedia_codec_param param;
			if (pjsua_codec_get_param(&codec_id, &param) != PJ_SUCCESS) {
				audioCodecList.RemoveAt(posKey);
				audioCodecList.RemoveAt(posValue);
			}
		};

		int curPos = 0;
		int i = PJMEDIA_CODEC_PRIO_NORMAL;
		CString resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
		while (!resToken.IsEmpty()) {
			pj_str_t codec_id = StrToPjStr(resToken);
			/*
			if (pj_strcmp2(&codec_id,"opus/48000/2") == 0) {
				pjmedia_codec_param param;
				pjsua_codec_get_param(&codec_id, &param);
				//param.info.clock_rate = 16000;
				param.info.avg_bps = 16000;
				param.info.max_bps = param.info.avg_bps * 2;
				//param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].name=pj_str("maxplaybackrate");
				//param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].val = pj_str("8000");
				//param.setting.dec_fmtp.cnt++;
				param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].name=pj_str("sprop-maxcapturerate");
				param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].val = pj_str("8000");
				param.setting.dec_fmtp.cnt++;
				pjsua_codec_set_param(&codec_id, &param);
			}
			*/
			pjsua_codec_set_priority(&codec_id, i);
			resToken = accountSettings.audioCodecs.Tokenize(_T(" "), curPos);
			i--;
		}
	}

#ifdef _GLOBAL_VIDEO
	//Set vid codecs prio
	PJ_LOG(3, (THIS_FILENAME, "Set video codecs"));
	if (accountSettings.videoCodec.GetLength())
	{
		pj_str_t codec_id = StrToPjStr(accountSettings.videoCodec);
		pjsua_vid_codec_set_priority(&codec_id, 255);
	}
	int bitrate;
	if (!accountSettings.videoH264) {
		pjsua_vid_codec_set_priority(&pj_str("H264/99"), 0);
	}
	else
	{
		const pj_str_t codec_id = { "H264/99", 7 };
		pjmedia_vid_codec_param param;
		pjsua_vid_codec_get_param(&codec_id, &param);
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
		}
		pjsua_vid_codec_set_param(&codec_id, &param);
	}
	if (!accountSettings.videoH263) {
		pjsua_vid_codec_set_priority(&pj_str("H263-1998/98"), 0);
	}
	else {
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			const pj_str_t codec_id = { "H263-1998/98", 12 };
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
	if (!accountSettings.videoVP8) {
		pjsua_vid_codec_set_priority(&pj_str("VP8/100"), 0);
	}
	else {
		if (accountSettings.videoBitrate) {
			bitrate = 1000 * accountSettings.videoBitrate;
			const pj_str_t codec_id = { "VP8/100", 7 };
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
#endif

	// Create transport
	PJ_LOG(3, (THIS_FILENAME, "Create transport"));
	transport_udp_local = -1;
	transport_udp = -1;
	transport_tcp = -1;
	transport_tls = -1;

	pjsua_transport_config_default(&cfg);

	if (accountSettings.sourcePort) {
		cfg.port = accountSettings.sourcePort;
		status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		if (status != PJ_SUCCESS) {
			cfg.port = 0;
			pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		}
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			if (accountSettings.sourcePort == 5060) {
				transport_udp_local = transport_udp;
			}
			else {
				cfg.port = 5060;
				status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp_local);
				if (status != PJ_SUCCESS) {
					transport_udp_local = transport_udp;
				}
			}
		}
	}
	else {
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			cfg.port = 5060;
			status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp_local);
			if (status != PJ_SUCCESS) {
				transport_udp_local = -1;
			}
		}
		cfg.port = 0;
		pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		if (transport_udp_local == -1) {
			transport_udp_local = transport_udp;
		}
	}

		cfg.port = MACRO_ENABLE_LOCAL_ACCOUNT ? 5060 : 0;
		status = pjsua_transport_create(PJSIP_TRANSPORT_TCP, &cfg, &transport_tcp);
		if (status != PJ_SUCCESS && cfg.port) {
			cfg.port = 0;
			pjsua_transport_create(PJSIP_TRANSPORT_TCP, &cfg, &transport_tcp);
		}
		cfg.port = MACRO_ENABLE_LOCAL_ACCOUNT ? 5061 : 0;
		status = pjsua_transport_create(PJSIP_TRANSPORT_TLS, &cfg, &transport_tls);
		if (status != PJ_SUCCESS && cfg.port) {
			cfg.port = 0;
			pjsua_transport_create(PJSIP_TRANSPORT_TLS, &cfg, &transport_tls);
		}

	if (accountSettings.usersDirectory.Find(_T("%s")) == -1 && accountSettings.usersDirectory.Find(_T("{")) == -1) {
		UsersDirectoryLoad();
	}

	SetTimer(IDT_TIMER_IDLE, 5000, NULL);

	account = PJSUA_INVALID_ID;
	account_local = PJSUA_INVALID_ID;

	PJAccountAddLocal();

}

void CmainDlg::UpdateSoundDevicesIds()
{
	msip_audio_input = -1;
	msip_audio_output = -2;
	msip_audio_ring = -2;

	unsigned count = PJMEDIA_AUD_MAX_DEVS;
	pjmedia_aud_dev_info aud_dev_info[PJMEDIA_AUD_MAX_DEVS];
	pjsua_enum_aud_devs(aud_dev_info, &count);
	for (unsigned i = 0; i < count; i++)
	{
		CString audDevName = PjStrToWideChar(aud_dev_info[i].name);
		if (aud_dev_info[i].input_count && !accountSettings.audioInputDevice.Compare(audDevName)) {
			msip_audio_input = i;
		}
		if (aud_dev_info[i].output_count) {
			if (!accountSettings.audioOutputDevice.Compare(audDevName)) {
				msip_audio_output = i;
			}
			if (!accountSettings.audioRingDevice.Compare(audDevName)) {
				msip_audio_ring = i;
			}
		}
	}
}

void CmainDlg::PJDestroy()
{
	KillTimer(IDT_TIMER_IDLE);
	KillTimer(IDT_TIMER_CALL);

	usersDirectoryLoaded = false;
	if (pj_ready) {
		if (pageContacts) {
			pageContacts->PresenceUnsubsribe();
		}
		call_deinit_tonegen(-1);

		toneCalls.RemoveAll();

		if (IsWindow(m_hWnd)) {
			KillTimer(IDT_TIMER_TONE);
		}

		PlayerStop();

		if (player_eof_data) {
			pj_pool_release(player_eof_data->pool);
			player_eof_data = NULL;
		}

		if (accountSettings.accountId) {
			PJAccountDelete();
		}

		pj_ready = false;

		//if (transport_udp_local!=PJSUA_INVALID_ID && transport_udp_local!=transport_udp) {
		//	pjsua_transport_close(transport_udp_local,PJ_TRUE);
		//}
		if (transport_udp != PJSUA_INVALID_ID) {
			//pjsua_transport_close(transport_udp,PJ_TRUE);
		}
		//if (transport_tcp!=PJSUA_INVALID_ID) {
		//	pjsua_transport_close(transport_tcp,PJ_TRUE);
		//}
		//if (transport_tls!=PJSUA_INVALID_ID) {
		//	pjsua_transport_close(transport_tls,PJ_TRUE);
		//}
		pjsua_destroy();
	}
}

void CmainDlg::PJAccountConfig(pjsua_acc_config *acc_cfg, Account *account)
{
	bool isLocal = (account == &accountSettings.accountLocal);
	pjsua_acc_config_default(acc_cfg);
	// global
	acc_cfg->ka_interval = account->keepAlive;
#ifdef _GLOBAL_VIDEO
	acc_cfg->vid_in_auto_show = PJ_TRUE;
	acc_cfg->vid_out_auto_transmit = PJ_TRUE;
	acc_cfg->vid_cap_dev = VideoCaptureDeviceId();
	acc_cfg->vid_wnd_flags = PJMEDIA_VID_DEV_WND_BORDER | PJMEDIA_VID_DEV_WND_RESIZABLE;
#endif

	if (accountSettings.rtpPortMin > 0) {
		acc_cfg->rtp_cfg.port = accountSettings.rtpPortMin;
		if (accountSettings.rtpPortMax > accountSettings.rtpPortMin) {
			acc_cfg->rtp_cfg.port_range = accountSettings.rtpPortMax - accountSettings.rtpPortMin;
		}
	}
	// account
	if (account->disableSessionTimer) {
		acc_cfg->use_timer = PJSUA_SIP_TIMER_INACTIVE;
	}

	acc_cfg->reg_timeout = account->registerRefresh;

	if (account->srtp == _T("optional")) {
		acc_cfg->use_srtp = PJMEDIA_SRTP_OPTIONAL;
	}
	else if (account->srtp == _T("mandatory")) {
		acc_cfg->use_srtp = PJMEDIA_SRTP_MANDATORY;
	}
	else {
		acc_cfg->use_srtp = PJMEDIA_SRTP_DISABLED;
	}
	if (!accountSettings.enableSTUN || accountSettings.stun.IsEmpty()) {
		acc_cfg->rtp_cfg.public_addr = StrToPjStr(account->publicAddr);
	}
	acc_cfg->ice_cfg_use = PJSUA_ICE_CONFIG_USE_CUSTOM;
	acc_cfg->ice_cfg.enable_ice = account->ice ? PJ_TRUE : PJ_FALSE;
	acc_cfg->allow_via_rewrite = account->allowRewrite ? PJ_TRUE : PJ_FALSE;
	acc_cfg->allow_sdp_nat_rewrite = acc_cfg->allow_via_rewrite;
	acc_cfg->allow_contact_rewrite = acc_cfg->allow_via_rewrite ? 2 : PJ_FALSE;
	acc_cfg->publish_enabled = account->publish ? PJ_TRUE : PJ_FALSE;

	if (!account->voicemailNumber.IsEmpty()) {
		acc_cfg->mwi_enabled = PJ_TRUE;
	}

	transport = MSIP_TRANSPORT_AUTO;
	if (account->transport == _T("udp") && transport_udp != -1) {
		acc_cfg->transport_id = transport_udp;
	}
	else if (account->transport == _T("tcp") && transport_tcp != -1) {
		transport = MSIP_TRANSPORT_TCP;
	}
	else if (account->transport == _T("tls") && transport_tls != -1) {
		transport = MSIP_TRANSPORT_TLS;
	}

	acc_cfg->cred_count = 1;
	acc_cfg->cred_info[0].username = StrToPjStr(!account->authID.IsEmpty() ? account->authID : (isLocal ? account->username : get_account_username()));
	acc_cfg->cred_info[0].realm = pj_str("*");
	acc_cfg->cred_info[0].scheme = pj_str("Digest");
	acc_cfg->cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	acc_cfg->cred_info[0].data = StrToPjStr((isLocal ? account->password : get_account_password()));

	CStringList proxies;
	get_account_proxy(account, proxies);
	acc_cfg->proxy_cnt = proxies.GetCount();
	POSITION pos = proxies.GetHeadPosition();
	int i = 0;
	while (pos) {
		CString proxy = proxies.GetNext(pos);
		proxy.Format(_T("sip:%s"), proxy);
		if (account->port > 0) {
			proxy.AppendFormat(_T(":%d"), account->port);
		}
		AddTransportSuffix(proxy);
		acc_cfg->proxy[i] = StrToPjStr(proxy);
		i++;
	}
}

/**
 * Add account is not exists.
 */
void CmainDlg::PJAccountAdd()
{
	if (pjsua_acc_is_valid(account)) {
		return;
	}
	CString str;

	if (!accountSettings.accountId) {
		return;
	}
	if (accountSettings.account.username.IsEmpty()) {
		if (!accountSettings.silent) {
			OnAccount(0, 0);
		}
		return;
	}
	PJAccountAddRaw();
}

void CmainDlg::PJAccountAddRaw()
{
	CString str;
	CString title = _T(_GLOBAL_NAME_NICE);
	CString titleAdder;
	CString usernameLocal;
	usernameLocal = accountSettings.account.username;
	if (!accountSettings.account.label.IsEmpty())
	{
		titleAdder = accountSettings.account.label;
	}
	else if (!accountSettings.account.displayName.IsEmpty())
	{
		titleAdder = accountSettings.account.displayName;
	}
	else if (!usernameLocal.IsEmpty())
	{
		titleAdder = usernameLocal;
	}
	if (!titleAdder.IsEmpty()) {
		title.AppendFormat(_T(" - %s"), titleAdder);
	}
	SetPaneText2(accountSettings.account.username);
	SetWindowText(title);
	pageDialer->SetName();

	pjsua_acc_config acc_cfg;
	PJAccountConfig(&acc_cfg, &accountSettings.account);

	if (!pjsua_call_get_count()) {
		str.Format(_T("%s..."), Translate(_T("Connecting")));
		UpdateWindowText(str);
	}

	//-- port knocker
	if (!accountSettings.portKnockerPorts.IsEmpty()) {
		CString host;
		CString ip;
		if (!accountSettings.portKnockerHost.IsEmpty()) {
			host = accountSettings.portKnockerHost;
		}
		else {
			host = RemovePort(get_account_server());
		}
		if (!host.IsEmpty()) {
			AfxSocketInit();
			if (IsIP(host)) {
				ip = host;
			}
			else {
				hostent *he = gethostbyname(CStringA(host));
				if (he) {
					ip = inet_ntoa(*((struct in_addr *) he->h_addr_list[0]));
				}
			}
			CSocket udpSocket;
			if (!ip.IsEmpty() && udpSocket.Create(0, SOCK_DGRAM, 0)) {
				int pos = 0;
				CString strPort = accountSettings.portKnockerPorts.Tokenize(_T(","), pos);
				while (pos != -1) {
					strPort.Trim();
					if (!strPort.IsEmpty()) {
						int port = StrToInt(strPort);
						if (port > 0 && port <= 65535) {
							char buf[6] = "knock";
							udpSocket.SendToEx(buf, sizeof(buf), port, ip);
						}
					}
					strPort = accountSettings.portKnockerPorts.Tokenize(_T(","), pos);
				}
				udpSocket.Close();
			}
		}
	}
	//--
	pj_status_t status = PJ_SUCCESS;
	//--
	CString localURI;
	if (!accountSettings.account.displayName.IsEmpty()) {
		localURI = _T("\"") + accountSettings.account.displayName + _T("\" ");
	}
	localURI += GetSIPURI(get_account_username());
	acc_cfg.id = StrToPjStr(localURI);
	//--
	if (get_account_server().IsEmpty()) {
		acc_cfg.register_on_acc_add = PJ_FALSE;
	}
	else {
		CString regURI;
		regURI.Format(_T("sip:%s"), get_account_server());
		AddTransportSuffix(regURI);
		acc_cfg.reg_uri = StrToPjStr(regURI);
	}
	//--
	status = pjsua_acc_add(&acc_cfg, PJ_TRUE, &account);

	if (account == PJSUA_INVALID_ID) {
		ShowErrorMessage(status);
		UpdateWindowText(_T(""), IDI_DEFAULT, true);
	}
	PublishStatus(true, acc_cfg.register_on_acc_add);

}

void CmainDlg::PJAccountAddLocal()
{
	if (MACRO_ENABLE_LOCAL_ACCOUNT) {
		pj_status_t status;
		pjsua_acc_config acc_cfg;
		PJAccountConfig(&acc_cfg, &accountSettings.accountLocal);

		CString localURI;
		if (!accountSettings.accountLocal.displayName.IsEmpty()) {
			localURI = _T("\"") + accountSettings.accountLocal.displayName + _T("\" ");
		}
		CString domain;
		if (!accountSettings.accountLocal.domain.IsEmpty()) {
			domain = accountSettings.accountLocal.domain;
		}
		else {
			pjsua_transport_data *t = &pjsua_var.tpdata[0];
			domain = PjToStr(&t->local_name.host);
		}
		if (!accountSettings.accountLocal.username.IsEmpty()) {
			localURI.AppendFormat(_T("<sip:%s@%s>"), accountSettings.accountLocal.username, domain);
		}
		else {
			localURI.AppendFormat(_T("<sip:%s>"), domain);
		}

		acc_cfg.id = StrToPjStr(localURI);
		acc_cfg.priority--;
		pjsua_acc_add(&acc_cfg, PJ_TRUE, &account_local);
		acc_cfg.priority++;
	}
}

/**
 * Delete account if exists.
 */
void CmainDlg::PJAccountDelete(bool deep)
{
	if (pageContacts) {
		pageContacts->PresenceUnsubsribe();
	}
	if (pjsua_acc_is_valid(account)) {
		pjsua_acc_del(account);
		account = PJSUA_INVALID_ID;
	}
}

void CmainDlg::PJAccountDeleteLocal()
{
	if (pjsua_acc_is_valid(account_local)) {
		pjsua_acc_del(account_local);
		account_local = PJSUA_INVALID_ID;
	}
}

void CmainDlg::OnTcnSelchangeTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	int nTab = tab->GetCurSel();
	TC_ITEM tci;
	tci.mask = TCIF_PARAM;
	tab->GetItem(nTab, &tci);
	if (tci.lParam > 0) {
		CWnd* pWnd = (CWnd *)tci.lParam;
		if (m_tabPrev != -1) {
			tab->GetItem(m_tabPrev, &tci);
			if (tci.lParam > 0) {
				((CWnd *)tci.lParam)->ShowWindow(SW_HIDE);
			}
		}
		pWnd->ShowWindow(SW_SHOW);
		if (IsWindowVisible()) {
			pWnd->SetFocus();
		}
		if (nTab != accountSettings.activeTab) {
			accountSettings.activeTab = nTab;
			AccountSettingsPendingSave();
		}
		if (pWnd == pageCalls && missed) {
			missed = false;
			UpdateWindowText();
		}
	}
	else {
	}
	*pResult = 0;
}

void CmainDlg::OnTcnSelchangingTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	m_tabPrev = tab->GetCurSel();
	*pResult = FALSE;
}

LRESULT CmainDlg::OnUpdateWindowText(WPARAM wParam, LPARAM lParam)
{
	UpdateWindowText(_T("-"));
	return TRUE;
}

void CmainDlg::TabFocusSet()
{
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	int nTab = tab->GetCurSel();
	TC_ITEM tci;
	tci.mask = TCIF_PARAM;
	tab->GetItem(nTab, &tci);
	if (tci.lParam > 0) {
		CWnd* pWnd = (CWnd *)tci.lParam;
		pWnd->SetFocus();
	}
}

void CmainDlg::UpdateWindowText(CString text, int icon, bool afterRegister)
{
	if (text.IsEmpty() && pjsua_var.state == PJSUA_STATE_RUNNING && pjsua_call_get_count()) {
		return;
	}
	CString str;
	bool showAccountDlg = false;
	bool noReg = false;
	if (text.IsEmpty() || text == _T("-")) {
		if (pjsua_var.state == PJSUA_STATE_RUNNING && pjsua_acc_is_valid(account)) {
			pjsua_acc_info info;
			pjsua_acc_get_info(account, &info);
			str = PjToStr(&info.status_text);
			if (str != _T("Default status message")) {
				if (!info.has_registration) {
					icon = IDI_DEFAULT;
					str = Translate(_T("Idle"));
					noReg = true;
				} else if (str == _T("OK")) {
					if (m_PresenceStatus == PJRPID_ACTIVITY_BUSY) {
						icon = IDI_BUSY;
						str = Translate(_T("Do Not Disturb"));
					}
					else {
						if (m_PresenceStatus == PJRPID_ACTIVITY_AWAY) {
							icon = IDI_AWAY;
							str = Translate(_T("Away"));
						}
						else {
							if (transport == MSIP_TRANSPORT_TLS) {
								icon = IDI_SECURE;
							}
							else {
								icon = IDI_ONLINE;
							}
							str = Translate(_T("Online"));
						}
						if (!accountSettings.singleMode && accountSettings.AC) {
							str.AppendFormat(_T(" (%s)"), Translate(_T("Auto Conference")));
						} else if (accountSettings.autoAnswer == _T("button") && accountSettings.AA) {
							str.AppendFormat(_T(" (%s)"), Translate(_T("Auto Answer")));
						}
					}
					pageContacts->PresenceSubsribe();
					if (!dialNumberDelayed.IsEmpty())
					{
						DialNumber(dialNumberDelayed);
						dialNumberDelayed = _T("");
					}
				}
				else if (str == _T("In Progress")) {
					str.Format(_T("%s..."), Translate(_T("Connecting")));
				}
				else if (info.status == 401) {
					onTrayNotify(NULL, WM_LBUTTONUP);
					if (afterRegister) {
						showAccountDlg = true;
					}
					icon = IDI_OFFLINE;
					str = Translate(_T("Incorrect Password"));
				}
				else {
					str = Translate(str.GetBuffer());
				}
				str = str.GetBuffer();
			}
			else {
				str.Format(_T("%s: %d"), Translate(_T("Response Code")), info.status);
			}
		}
		else {
			if (afterRegister) {
				showAccountDlg = true;
			}
			str = _T(_GLOBAL_NAME_NICE);
			icon = IDI_DEFAULT;
		}
	}
	else
	{
		str = text;
	}

	CString* pPaneText = new CString();
	*pPaneText = str;
	PostMessage(UM_SET_PANE_TEXT, NULL, (LPARAM)pPaneText);

	if (icon != -1) {
		HICON hIcon = (HICON)LoadImage(
			AfxGetInstanceHandle(),
			MAKEINTRESOURCE(icon),
			IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
		m_bar.GetStatusBarCtrl().SetIcon(0, hIcon);
		iconStatusbar = icon;

		//--
		tnd.uFlags = tnd.uFlags & ~NIF_INFO;
		if ((pjsua_var.state == PJSUA_STATE_RUNNING && !pjsua_acc_is_valid(account) && MACRO_ENABLE_LOCAL_ACCOUNT) || ((icon != IDI_DEFAULT || noReg) && icon != IDI_OFFLINE)) {
			if (missed) {
				if (tnd.hIcon != iconMissed) {
					tnd.uFlags = tnd.uFlags & ~NIF_INFO;
					tnd.hIcon = iconMissed;
					Shell_NotifyIcon(NIM_MODIFY, &tnd);
				}
			}
			else {

				if (tnd.hIcon != iconSmall) {
					tnd.hIcon = iconSmall;
					Shell_NotifyIcon(NIM_MODIFY, &tnd);
				}
			}
		}
		else {
			if (tnd.hIcon != iconInactive) {
				tnd.hIcon = iconInactive;
				Shell_NotifyIcon(NIM_MODIFY, &tnd);
			}
		}
		//--
	}
	if (showAccountDlg) {
		PostMessage(UM_ON_ACCOUNT, 1);
	}
}

void CmainDlg::PublishStatus(bool online, bool init)
{
	if (pjsua_var.state != PJSUA_STATE_RUNNING) {
		return;
	}
	bool busy = (accountSettings.denyIncoming == _T("button") && accountSettings.DND);
	pjrpid_activity presenceStatusNew;
	pj_str_t note = pj_str("");
	if (m_PresenceStatus == PJRPID_ACTIVITY_BUSY) {
		if (!busy) {
			presenceStatusNew = PJRPID_ACTIVITY_UNKNOWN;
			note = pj_str("Idle");
		}
	}
	else {
		if (busy) {
			presenceStatusNew = PJRPID_ACTIVITY_BUSY;
			note = pj_str("Busy");
		}
		else {
			presenceStatusNew = online ? PJRPID_ACTIVITY_UNKNOWN : PJRPID_ACTIVITY_AWAY;
			note = online ? pj_str("Idle") : pj_str("Away");
		}
	}
	if (note.slen) {
		pjsua_acc_id ids[PJSUA_MAX_ACC];
		unsigned count = PJSUA_MAX_ACC;
		if (pjsua_enum_accs(ids, &count) == PJ_SUCCESS) {
			pjrpid_element pr;
			pr.type = PJRPID_ELEMENT_TYPE_PERSON;
			pr.id = pj_str(NULL);
			pr.note = pj_str(NULL);
			pr.note = note;
			pr.activity = presenceStatusNew;
			for (unsigned i = 0; i < count; i++) {
				pjsua_acc_set_online_status2(ids[i], PJ_TRUE, &pr);
			}
		}
		m_PresenceStatus = presenceStatusNew;
	}
	if (!init) {
		UpdateWindowText();
	}
}

LRESULT CmainDlg::onCopyData(WPARAM wParam, LPARAM lParam)
{
	LRESULT res = TRUE;
	if (pj_ready) {
		COPYDATASTRUCT *s = (COPYDATASTRUCT*)lParam;
		if (s && s->dwData == 1) {
			CString params = (LPCTSTR)s->lpData;
			res = CommandLine(params);
		}
	}
	return res;
}

bool CmainDlg::CommandLine(CString params) {
	bool activate = false;
	params.Trim();
	if (params.GetAt(0) == '"' && params.GetAt(params.GetLength() - 1) == '"') {
		params = params.Mid(1, params.GetLength() - 2);
	}
	if (!params.IsEmpty()) {
		if (params.Find(_T("msip:")) == 0) {
			CString cmd = params.Mid(5);
			if (cmd == _T("minimize")) {
				ShowWindow(SW_HIDE);
			}
			else if (cmd == _T("answer")) {
				msip_call_answer();
			}
			else if (cmd == _T("hangupall")) {
				call_hangup_all_noincoming();
			}
			else if (cmd == _T("hold")) {
				messagesDlg->OnBnClickedHold();
			}
			else if (cmd.Find(_T("transfer_")) == 0) {
				messagesDlg->CallAction(MSIP_ACTION_TRANSFER, cmd.Mid(9));
			}
			else if (cmd == _T("micmute")) {
				pageDialer->OnBnClickedMuteInput();
			}
			else if (cmd == _T("speakmute")) {
				pageDialer->OnBnClickedMuteOutput();
			}
			else if (cmd == _T("micup")) {
				pageDialer->OnBnClickedPlusInput();
			}
			else if (cmd == _T("micdown")) {
				pageDialer->OnBnClickedMinusInput();
			}
			else if (cmd == _T("speakup")) {
				pageDialer->OnBnClickedPlusOutput();
			}
			else if (cmd == _T("speakdown")) {
				pageDialer->OnBnClickedMinusOutput();
			}
			else if (!cmd.IsEmpty()) {
				DialNumberFromCommandLine(cmd);
			}
			return activate;
		}
		if (params == _T("/answer")) {
			msip_call_answer();
		}
		else if (params == _T("/hangupall")) {
			call_hangup_all_noincoming();
		}
		else if (params.Find(_T("/dtmf:")) == 0) {
			CString value = params.Mid(6);
			if (!value.IsEmpty()) {
				mainDlg->pageDialer->DTMF(value);
			}
		}
		else if (params.Find(_T("/transfer:")) == 0) {
			CString value = params.Mid(10);
			if (!value.IsEmpty()) {
				messagesDlg->CallAction(MSIP_ACTION_TRANSFER, value);
			}
		}
		else {
			DialNumberFromCommandLine(params);
		}
	}
	return activate;
}

bool CmainDlg::GotoTabLParam(LPARAM lParam) {
	CTabCtrl* tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	for (int i = 0; i < tab->GetItemCount(); i++) {
		TC_ITEM tci;
		tci.mask = TCIF_PARAM;
		tab->GetItem(i, &tci);
		if (tci.lParam == lParam) {
			return GotoTab(i, tab);
		}
	}
	return false;
}

bool CmainDlg::GotoTab(int i, CTabCtrl* tab) {
	if (!tab) {
		tab = (CTabCtrl*)GetDlgItem(IDC_MAIN_TAB);
	}
	int nTab = tab->GetCurSel();
	if (i < 0) {
		int max = tab->GetItemCount() - 1;
		if (i == -1) {
			i = nTab < max ? nTab + 1 : 0;
		}
		else {
			i = nTab == 0 ? max : nTab - 1;
		}
	}
	if (nTab != i) {
		LRESULT pResult;
		OnTcnSelchangingTab(NULL, &pResult);
		tab->SetCurSel(i);
		OnTcnSelchangeTab(NULL, &pResult);
		return true;
	}
	return false;
}


void CmainDlg::DialNumberFromCommandLine(CString number) {
	GotoTab(0);
	onTrayNotify(NULL, WM_LBUTTONUP);
	pjsua_acc_info info;
	if (number.Mid(0, 4).CompareNoCase(_T("tel:")) == 0 || number.Mid(0, 4).CompareNoCase(_T("sip:")) == 0) {
		number = number.Mid(4);
	}
	else if (number.Mid(0, 7).CompareNoCase(_T("callto:")) == 0) {
		number = number.Mid(7);
	}
	if (accountSettings.accountId > 0) {
		if (pjsua_acc_is_valid(account) &&
			(get_account_server().IsEmpty() ||
			(pjsua_acc_get_info(account, &info) == PJ_SUCCESS && info.status == 200)
				)
			) {
			DialNumber(number);
		}
		else {
			dialNumberDelayed = number;
		}
	}
	else {
		if (pjsua_acc_is_valid(account_local)) {
			DialNumber(number);
		}
		else if (accountSettings.enableLocalAccount) {
			dialNumberDelayed = number;
		}
	}
}

void CmainDlg::DialNumber(CString params)
{
	CString number;
	CString message;
	int i = params.Find(_T(" "));
	if (i != -1) {
		number = params.Mid(0, i);
		message = params.Mid(i + 1);
		message.Trim();
	}
	else {
		number = params;
	}
	number.Trim();
	if (!number.IsEmpty()) {
		if (message.IsEmpty()) {
			pageDialer->DialedAdd(number);
			MakeCall(number, false, true);
		}
		else {
			messagesDlg->SendInstantMessage(NULL, message, number);
		}
	}
}

bool CmainDlg::MakeCall(CString number, bool hasVideo, bool fromCommandLine)
{
	if (accountSettings.singleMode && call_get_count_noincoming()) {
		GotoTab(0);
	}
	else if (!pjsua_acc_is_valid(account) && !pjsua_acc_is_valid(account_local)) {
		Account dummy;
		pj_status_t status = accountSettings.AccountLoad(1, &dummy) ? PJSIP_EAUTHACCDISABLED : PJSIP_EAUTHACCNOTFOUND;
		ShowErrorMessage(status);
	}
	else
	{
		CString commands;
		if (MessagesOpen(number, _T(""), &commands, true)) {
			MessagesContact* messagesContact = messagesDlg->GetMessageContact();
			messagesContact->fromCommandLine = fromCommandLine;
			messagesDlg->Call(hasVideo, commands);
			return true;
		}
	}
	return false;
}

bool CmainDlg::MessagesOpen(CString number, CString name, CString *commands, bool forCall)
{
	CString numberFormated = FormatNumber(number, commands);
	pj_status_t pj_status = msip_verify_sip_url(StrToPj(numberFormated));
	if (pj_status == PJ_SUCCESS) {
		if (name.IsEmpty()) {
			name = pageContacts->GetNameByNumber(numberFormated);
			if (name.IsEmpty()) {
				name = number;
			}
		}
		bool doNotShowMessagesWindow = false;
		if (forCall) {
			doNotShowMessagesWindow = accountSettings.singleMode ||
				(accountSettings.silent && !mainDlg->IsWindowVisible());
		}
		if (messagesDlg->AddTab(numberFormated, name, TRUE, NULL, NULL, doNotShowMessagesWindow)) {
			return true;
		}
	}
	else {
		ShowErrorMessage(pj_status);
	}
	return false;
}

void CmainDlg::AutoAnswer(pjsua_call_id call_id)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS || (call_info.state != PJSIP_INV_STATE_INCOMING && call_info.state != PJSIP_INV_STATE_EARLY)) {
		return;
	}
	call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_id);
	if (user_data) {
		user_data->CS.Lock();
		user_data->autoAnswer = true;
		user_data->CS.Unlock();
	}
	mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)call_id, (LPARAM)call_info.rem_vid_cnt);
	if (accountSettings.localDTMF && !accountSettings.hidden) {
		mainDlg->PostMessage(UM_ON_PLAYER_PLAY, MSIP_SOUND_RINGIN2, 0);
	}
}

pjsua_call_id CmainDlg::CurrentCallId()
{
	MessagesContact* messagesContact = messagesDlg->GetMessageContact();
	if (messagesContact) {
		return messagesContact->callId;
	}
	return -1;
}

void CmainDlg::ShortcutAction(Shortcut *shortcut)
{
	pjsua_call_id current_call_id;
	CString params;
	if (shortcut->type == MSIP_SHORTCUT_CALL) {
		mainDlg->MakeCall(shortcut->number);
	}
	else if (shortcut->type == MSIP_SHORTCUT_VIDEOCALL) {
#ifdef _GLOBAL_VIDEO
		mainDlg->MakeCall(shortcut->number, true);
#else
		mainDlg->MakeCall(shortcut->number);
#endif
	}
	else if (shortcut->type == MSIP_SHORTCUT_MESSAGE) {
		mainDlg->MessagesOpen(shortcut->number);
	}
	else if (shortcut->type == MSIP_SHORTCUT_DTMF) {
		mainDlg->pageDialer->DTMF(shortcut->number);
	}
	else if (shortcut->type == MSIP_SHORTCUT_TRANSFER) {
		current_call_id = CurrentCallId();
		if (current_call_id != -1) {
			pj_str_t pj_uri = StrToPjStr(GetSIPURI(shortcut->number, true));
			call_user_data *user_data;
			user_data = (call_user_data *)pjsua_call_get_user_data(current_call_id);
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
				pjsua_call_xfer(current_call_id, &pj_uri, NULL);
			}
		}
	}
	else if (shortcut->type == MSIP_SHORTCUT_CONFERENCE) {
		messagesDlg->CallAction(MSIP_ACTION_INVITE, shortcut->number);
	}
	else if (shortcut->type == MSIP_SHORTCUT_RUNBATCH) {
		AfxMessageBox(_T(_GLOBAL_BUSINESS_FEATURE));
			}
	else if (shortcut->type == MSIP_SHORTCUT_CALL_URL || shortcut->type == MSIP_SHORTCUT_POP_URL) {
		AfxMessageBox(_T(_GLOBAL_BUSINESS_FEATURE));
	}
}

LRESULT CmainDlg::onPlayerPlay(WPARAM wParam, LPARAM lParam)
{
	CString filename;
	BOOL noLoop;
	BOOL inCall;
	if (wParam == MSIP_SOUND_CUSTOM) {
		filename = *(CString*)lParam;
		noLoop = FALSE;
		inCall = FALSE;
	} else if (wParam == MSIP_SOUND_CUSTOM_NOLOOP) {
		filename = *(CString*)lParam;
		noLoop = TRUE;
		inCall = FALSE;
	}
	else {
		filename = accountSettings.pathExe + _T("\\");
		switch (wParam) {
		case MSIP_SOUND_MESSAGE_IN:
			filename.Append(_T("msgin.wav"));
			noLoop = TRUE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_MESSAGE_OUT:
			filename.Append(_T("msgout.wav"));
			noLoop = TRUE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_HANGUP:
			filename.Append(_T("hangup.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		case MSIP_SOUND_RINGTONE:
			filename.Append(_T("ringtone.wav"));
			noLoop = FALSE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_RINGIN2:
			filename.Append(_T("ringing2.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		case MSIP_SOUND_RINGING:
			filename.Append(_T("ringing.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		default:
			noLoop = TRUE;
			inCall = FALSE;
		}
	}
	PlayerPlay(filename, noLoop, inCall);
	return 0;
}

LRESULT CmainDlg::onPlayerStop(WPARAM wParam, LPARAM lParam)
{
	PlayerStop();
	return 0;
}

static PJ_DEF(pj_status_t) on_pjsua_wav_file_end_callback(pjmedia_port* media_port, void* args)
{
	mainDlg->PostMessage(UM_ON_PLAYER_STOP, 0, 0);
	return -1;//Here it is important to return value other than PJ_SUCCESS
}

void CmainDlg::PlayerPlay(CString filename, bool noLoop, bool inCall)
{
	PlayerStop();
	if (!filename.IsEmpty()) {
		pj_str_t file = StrToPjStr(filename);
		pjsua_player_id player_id;
		if (pjsua_var.state == PJSUA_STATE_RUNNING && pjsua_player_create(&file, noLoop ? PJMEDIA_FILE_NO_LOOP : 0, &player_id) == PJ_SUCCESS) {
			pjmedia_port *player_media_port;
			if (pjsua_player_get_port(player_id, &player_media_port) == PJ_SUCCESS) {
				if (!player_eof_data) {
					pj_pool_t *pool = pjsua_pool_create("microsip_eof_data", 512, 512);
					player_eof_data = PJ_POOL_ZALLOC_T(pool, struct player_eof_data);
					player_eof_data->pool = pool;
				}
				player_eof_data->player_id = player_id;
				if (noLoop) {
					pjmedia_wav_player_set_eof_cb(player_media_port, player_eof_data, &on_pjsua_wav_file_end_callback);
				}
				if (
					(!tone_gen && pjsua_conf_get_active_ports() <= 2)
					||
					(tone_gen && pjsua_conf_get_active_ports() <= 3)
					) {
					msip_set_sound_device(inCall ? msip_audio_output : msip_audio_ring);
				}
				pjsua_conf_port_id conf_port_id = pjsua_player_get_conf_port(player_id);
				if (inCall) {
					pjsua_conf_adjust_rx_level(conf_port_id, 0.4);
				}
				else {
					pjsua_conf_adjust_rx_level(conf_port_id, (float)accountSettings.volumeRing / 100);
				}
				pjsua_conf_connect(conf_port_id, 0);
			}
		}
	}
}

void CmainDlg::PlayerStop()
{
	if (player_eof_data && player_eof_data->player_id != PJSUA_INVALID_ID) {
		if (pjsua_var.state != PJSUA_STATE_NULL) {
			pjsua_conf_disconnect(pjsua_player_get_conf_port(player_eof_data->player_id), 0);
			pjsua_player_destroy(player_eof_data->player_id);
			player_eof_data->player_id = PJSUA_INVALID_ID;
		}
		else {
			player_eof_data->player_id = PJSUA_INVALID_ID;
		}
	}
}

bool CmainDlg::CommandCallAnswer() {
	if (ringinDlgs.GetCount()) {
		RinginDlg *ringinDlg = ringinDlgs.GetAt(0);
		mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)ringinDlg->call_id, (LPARAM)0);
		return true;
	}
	return false;
}

bool CmainDlg::CommandCallReject()
{
	if (ringinDlgs.GetCount()) {
		RinginDlg *ringinDlg = ringinDlgs.GetAt(ringinDlgs.GetCount()-1);
		ringinDlg->OnBnClickedDecline();
		return true;
	}
	return false;
}

LRESULT CmainDlg::onShellHookMessage(WPARAM wParam, LPARAM lParam)
{
	if (wParam == HSHELL_APPCOMMAND) {
		int nCmd = GET_APPCOMMAND_LPARAM(lParam);
		if (nCmd == APPCOMMAND_MEDIA_PLAY ||
			nCmd == APPCOMMAND_MEDIA_PLAY_PAUSE ||
			nCmd == APPCOMMAND_MEDIA_STOP) {
			if (ringinDlgs.GetCount()) {
				RinginDlg *ringinDlg = ringinDlgs.GetAt(0);
				if (nCmd == APPCOMMAND_MEDIA_STOP) {
					ringinDlg->OnBnClickedDecline();
				}
				else {
					ringinDlg->CallAccept(ringinDlg->remoteHasVideo);
				}
			}
			else {
				if (nCmd == APPCOMMAND_MEDIA_STOP) {
					messagesDlg->OnBnClickedEnd();
				}
				else {
					CButton* callButton = (CButton*)pageDialer->GetDlgItem(IDC_CALL);
					if (callButton->IsWindowVisible() && callButton->IsWindowEnabled()) {
						pageDialer->OnBnClickedCall();
					}
					else {
						messagesDlg->OnBnClickedHold();
					}
				}
			}
		}
		else if (nCmd == APPCOMMAND_MEDIA_PAUSE) {
			messagesDlg->OnBnClickedHold();
		}
	}
	return 0;
}

LRESULT CmainDlg::onCallAnswer(WPARAM wParam, LPARAM lParam)
{
	if (pjsua_var.state == PJSUA_STATE_RUNNING) {
		pjsua_call_id call_id = wParam;
		if (lParam < 0) {
			pjsua_call_answer(call_id, -lParam, NULL, NULL);
			return 0;
		}
		pjsua_call_info call_info;
		if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
			if (call_info.role == PJSIP_ROLE_UAS && (call_info.state == PJSIP_INV_STATE_INCOMING || call_info.state == PJSIP_INV_STATE_EARLY)) {
				if (accountSettings.singleMode) {
					call_hangup_all_noincoming();
				}
				msip_set_sound_device(msip_audio_output);
#ifdef _GLOBAL_VIDEO
				if (lParam > 0) {
					createPreviewWin();
				}
#endif
				pjsua_call_setting call_setting;
				pjsua_call_setting_default(&call_setting);
				call_setting.vid_cnt = lParam > 0 ? 1 : 0;
				if (pjsua_call_answer2(call_id, &call_setting, 200, NULL, NULL) == PJ_SUCCESS) {
					callIdIncomingIgnore = PjToStr(&call_info.call_id);
				}
				PlayerStop();
				bool restore = true;
				if (accountSettings.silent) {
					restore = false;
				}

				call_user_data *user_data = (call_user_data *)pjsua_call_get_user_data(call_id);
				if (user_data) {
					user_data->CS.Lock();
					if (user_data->autoAnswer) {
						if (!accountSettings.bringToFrontOnIncoming) {
							restore = false;
							if (GetForegroundWindow()->GetTopLevelParent() != this) {
								SIPURI sipuri;
								ParseSIPURI(PjToStr(&call_info.remote_info, TRUE), &sipuri);
								BaloonPopup(Translate(_T("Auto Answer")), !sipuri.name.IsEmpty() ? sipuri.name : sipuri.user, NIIF_INFO);
							}
						}
					}
					user_data->CS.Unlock();
				}

				if (restore) {
					onTrayNotify(NULL, WM_LBUTTONUP);
				}
			}
		}
	}
	return 0;
}

LRESULT CmainDlg::onCallHangup(WPARAM wParam, LPARAM lParam)
{
	if (pjsua_var.state == PJSUA_STATE_RUNNING) {
		pjsua_call_id call_id = wParam;
		msip_call_hangup_fast(call_id);
	}
	return 0;
}

LRESULT CmainDlg::onTabIconUpdate(WPARAM wParam, LPARAM lParam)
{
	if (messagesDlg) {
		pjsua_call_id call_id = wParam;
		for (int i = 0; i < messagesDlg->tab->GetItemCount(); i++) {
			MessagesContact* messagesContact = messagesDlg->GetMessageContact(i);
			if (messagesContact->callId == call_id) {
				pjsua_call_info call_info;
				if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
					messagesDlg->UpdateTabIcon(messagesContact, i, &call_info);
				}
				break;
			}
		}
	}
	return 0;
}

LRESULT CmainDlg::onSetPaneText(WPARAM wParam, LPARAM lParam)
{
	CString* pString = (CString*)lParam;
	ASSERT(pString != NULL);
	m_bar.SetPaneText(0, *pString);
	delete pString;
	return 0;
}

void CmainDlg::SetPaneText2(CString str)
{
	if (str.IsEmpty()) {
		m_bar.SetPaneInfo(IDS_STATUSBAR2, IDS_STATUSBAR2, SBPS_NOBORDERS, 0);
	}
	else {
		CSize size = m_bar.GetDC()->GetTextExtent(str);
		int width = size.cx;
		m_bar.SetPaneInfo(IDS_STATUSBAR2, IDS_STATUSBAR2, SBPS_NORMAL, width);
	}
	m_bar.SetPaneText(IDS_STATUSBAR2, str);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, IDS_STATUSBAR);
}


BOOL CmainDlg::CopyStringToClipboard(IN const CString & str)
{
	// Open the clipboard
	if (!OpenClipboard())
		return FALSE;

	// Empty the clipboard
	if (!EmptyClipboard())
	{
		CloseClipboard();
		return FALSE;
	}

	// Number of bytes to copy (consider +1 for end-of-string, and
	// properly scale byte size to sizeof(TCHAR))
	SIZE_T textCopySize = (str.GetLength() + 1) * sizeof(TCHAR);

	// Allocate a global memory object for the text
	HGLOBAL hTextCopy = GlobalAlloc(GMEM_MOVEABLE, textCopySize);
	if (hTextCopy == NULL)
	{
		CloseClipboard();
		return FALSE;
	}

	// Lock the handle, and copy source text to the buffer
	TCHAR * textCopy = reinterpret_cast<TCHAR *>(GlobalLock(
		hTextCopy));
	ASSERT(textCopy != NULL);
	StringCbCopy(textCopy, textCopySize, str.GetString());
	GlobalUnlock(hTextCopy);
	textCopy = NULL; // avoid dangling references

	// Place the handle on the clipboard
#if defined( _UNICODE )
	UINT textFormat = CF_UNICODETEXT;  // Unicode text
#else
	UINT textFormat = CF_TEXT;         // ANSI text
#endif // defined( _UNICODE )

	if (SetClipboardData(textFormat, hTextCopy) == NULL)
	{
		// Failed
		CloseClipboard();
		return FALSE;
	}

	// Release the clipboard
	CloseClipboard();

	// All right
	return TRUE;
}

void CmainDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (nID == SC_CLOSE) {
		ShowWindow(SW_HIDE);
	}
	else {
		__super::OnSysCommand(nID, lParam);
	}
}

BOOL CmainDlg::OnQueryEndSession()
{
	return TRUE;
}

void CmainDlg::OnClose()
{
	DestroyWindow();
}

void CmainDlg::OnContextMenu(CWnd *pWnd, CPoint point)
{
	CPoint local = point;
	ScreenToClient(&local);
	CRect rect;
	GetClientRect(&rect);
	int height = 16;
	CDC *pDC = GetDC();
	if (pDC) {
		int dpiY = GetDeviceCaps(pDC->m_hDC, LOGPIXELSY);
		height = MulDiv(height, dpiY, 96);
		ReleaseDC(pDC);
	}

	if (rect.Height() - local.y <= height) {
		MainPopupMenu();
	}
	else {
		DefWindowProc(WM_CONTEXTMENU, NULL, MAKELPARAM(point.x, point.y));
	}
}

BOOL CmainDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	if (nEventType == DBT_DEVNODES_CHANGED) {
		if (pj_ready) {
			if (dwData == 1) {
				PJ_LOG(3, (THIS_FILENAME, "OnDeviceStateChanged event, schedule refresh devices"));
			}
			else {
				PJ_LOG(3, (THIS_FILENAME, "WM_DEVICECHANGE received, schedule refresh devices"));
			}
			KillTimer(IDT_TIMER_SWITCH_DEVICES);
			SetTimer(IDT_TIMER_SWITCH_DEVICES, 1500, NULL);
		}
	}
	return FALSE;
}

void CmainDlg::OnSessionChange(UINT nSessionState, UINT nId)
{
	if (nSessionState == WTS_REMOTE_CONNECT || nSessionState == WTS_CONSOLE_CONNECT) {
		if (pj_ready) {
			PJ_LOG(3, (THIS_FILENAME, "WM_WTSSESSION_CHANGE received, schedule refresh devices"));
			KillTimer(IDT_TIMER_SWITCH_DEVICES);
			SetTimer(IDT_TIMER_SWITCH_DEVICES, 1500, NULL);
		}
	}
}

void CmainDlg::OnMove(int x, int y)
{
	if (IsWindowVisible() && !IsZoomed() && !IsIconic()) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.mainX = cRect.left;
		accountSettings.mainY = cRect.top;
		AccountSettingsPendingSave();
	}
}

void CmainDlg::OnSize(UINT type, int w, int h)
{
	CBaseDialog::OnSize(type, w, h);
	if (this->IsWindowVisible() && type == SIZE_RESTORED) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.mainW = cRect.Width();
		accountSettings.mainH = cRect.Height();
		AccountSettingsPendingSave();
	}
}

void CmainDlg::SetupJumpList()
{
	JumpList jl(_T(_GLOBAL_NAME_NICE));
	jl.AddTasks();
}

void CmainDlg::RemoveJumpList()
{
	JumpList jl(_T(_GLOBAL_NAME_NICE));
	jl.DeleteJumpList();
}

void CmainDlg::OnMenuWebsite()
{
	OpenURL(_T(_GLOBAL_MENU_WEBSITE));
}

void CmainDlg::OnMenuHelp()
{
	OpenHelp();
}

void CmainDlg::OnMenuAddl()
{
}

LRESULT CmainDlg::onUsersDirectoryLoaded(WPARAM wParam, LPARAM lParam)
{
	PJ_LOG(3, (THIS_FILENAME, "Users directory loaded"));
	URLGetAsyncData *response = (URLGetAsyncData *)wParam;
	int expires = 0;
	if (response->statusCode == 200 && !response->body.IsEmpty()) {
		if (!response->body.IsEmpty()) {
			BOOL ok = FALSE;
			bool changed = false;
			// JSON
			Json::Value root;
			Json::Reader reader;
			bool parsedSuccess = reader.parse((LPCSTR)response->body,
				root,
				false);
			if (parsedSuccess) {
				try {
					if (root.isArray()) {
						pageContacts->SetCanditates();
						for (Json::Value::ArrayIndex i = 0; i != root.size(); i++) {
							CString number;
							CString name;
							CString info;
							CString hint;
							char presence = -1;
							if (root[i]["number"].isString()) {
								number = Utf8DecodeUni(root[i]["number"].asCString());
							}
							else if (root[i]["phone"].isString()) {
								number = Utf8DecodeUni(root[i]["phone"].asCString());
							}
							if (root[i]["name"].isString()) {
								name = Utf8DecodeUni(root[i]["name"].asCString());
							}
							if (root[i]["info"].isString()) {
								info = Utf8DecodeUni(root[i]["info"].asCString());
							}
							if (root[i]["hint"].isString()) {
								hint = Utf8DecodeUni(root[i]["hint"].asCString());
							}
							if (root[i]["presence"].isInt()) {
								presence = root[i]["presence"].asInt() ? 1 : 0;
							}
							if (!number.IsEmpty()) {
								if (pageContacts->ContactAdd(number, name, info, hint, presence, 1, FALSE, TRUE) && !changed) {
									changed = true;
								}
								if (!ok) {
									ok = TRUE;
								}
							}
						}
					}
				}
				catch (std::exception const& e) {
				}
			}
			else {
				// XML
				CMarkup xml;
				BOOL bResult = xml.SetDoc(Utf8DecodeUni(response->body));
				if (bResult) {
					pageContacts->SetCanditates();
					if (xml.FindElem(_T("YealinkIPPhoneBook"))) {
						while (xml.FindChildElem(_T("Menu"))) {
							xml.IntoElem();
							//int i = 0;
							while (xml.FindChildElem(_T("Unit"))) {
								xml.IntoElem();
								CString number = xml.GetAttrib(_T("Phone1"));
								CString name = xml.GetAttrib(_T("Name"));
								char presence = 0;
								if (!number.IsEmpty()) {
									if (pageContacts->ContactAdd(number, name, _T(""), _T(""), presence, 1, FALSE, TRUE) && !changed) {
										changed = true;
									}
									if (!ok) {
										ok = TRUE;
									}
								}
								xml.OutOfElem();
							}
							xml.OutOfElem();
						}
					}
					else {
						while (xml.FindChildElem(_T("entry")) || xml.FindChildElem(_T("DirectoryEntry"))) {
							xml.IntoElem();
							CString number;
							CString name;
							CString rab;
							CString info;
							CString hint;
							if (xml.FindChildElem(_T("extension")) || xml.FindChildElem(_T("Telephone"))) {
								number = xml.GetChildData();
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("name")) || xml.FindChildElem(_T("Name"))) {
								name = xml.GetChildData();
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("presence"))) {
								rab = xml.GetChildData();
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("info"))) {
								info = xml.GetChildData();
								xml.ResetChildPos();
							}
							if (xml.FindChildElem(_T("hint"))) {
								hint = xml.GetChildData();
								xml.ResetChildPos();
							}
							char presence = !(rab.IsEmpty() || rab == _T("0")
								|| rab.CompareNoCase(_T("no")) == 0
								|| rab.CompareNoCase(_T("false")) == 0
								|| rab.CompareNoCase(_T("null")) == 0);
							if (!number.IsEmpty()) {
								if (pageContacts->ContactAdd(number, name, info, hint, presence, 1, FALSE, TRUE) && !changed) {
									changed = true;
								}
								if (!ok) {
									ok = TRUE;
								}
							}
							xml.OutOfElem();
						}
					}
				}
			}
			if (ok) {
				usersDirectoryLoaded = true;
				if (pageContacts->DeleteCanditates() || changed) {
					pageContacts->ContactsSave();
				}
			}
		}
		response->headers.MakeLower();
		CString search = _T("\r\ncache-control:");
		int n = response->headers.Find(search);
		if (n > 0) {
			n = n + search.GetLength();
			int l = response->headers.Find(_T("\r\n"), n);
			if (l > 0) {
				response->headers = response->headers.Mid(n, l - n);
				search = _T("max-age=");
				n = response->headers.Find(search);
				if (n != -1) {
					response->headers = response->headers.Mid(n + search.GetLength());
					expires = atoi(CStringA(response->headers));
				}
			}
		}
	}
	if (expires <= 0) {
		expires = 3600;
	}
	else if (expires < 60) {
		expires = 60;
	}
	else if (expires > 86400) {
		expires = 86400;
	}
	SetTimer(IDT_TIMER_DIRECTORY, 1000 * expires, NULL);

	PJ_LOG(3, (THIS_FILENAME, "End UsersDirectoryLoad"));
	delete response;
	return 0;
}

void CmainDlg::UsersDirectoryLoad()
{
	KillTimer(IDT_TIMER_DIRECTORY);
	if (!accountSettings.usersDirectory.IsEmpty()) {
		PJ_LOG(3, (THIS_FILENAME, "Users directory load"));
		CString url;
		url.Format(accountSettings.usersDirectory, accountSettings.account.username, accountSettings.account.password, get_account_server());
		url = msip_url_mask(url);
		PJ_LOG(3, (THIS_FILENAME, "Begin UsersDirectoryLoad"));
		URLGetAsync(url, m_hWnd, UM_USERS_DIRECTORY
			, false
		);

	}
}

void CmainDlg::AccountSettingsPendingSave()
{
	KillTimer(IDT_TIMER_SAVE);
	SetTimer(IDT_TIMER_SAVE, 5000, NULL);
}

void CmainDlg::OnAccountChanged()
{
	pageDialer->RebuildButtons();
}

void CmainDlg::CheckUpdates()
{
	CString url;
	url = _T("http://update.microsip.org/softphone-update.txt");
	url.AppendFormat(_T("?version=%s&client=%s"), _T(_GLOBAL_VERSION), CString(urlencode(Utf8EncodeUcs2(_T(_GLOBAL_NAME_NICE)))));
#ifndef _GLOBAL_VIDEO
	url.Append(_T("&lite=1"));
#endif
	URLGetAsync(url, m_hWnd, UM_UPDATE_CHECKER_LOADED);
}

LRESULT CmainDlg::OnUpdateCheckerLoaded(WPARAM wParam, LPARAM lParam)
{
	URLGetAsyncData *response = (URLGetAsyncData *)wParam;
	if (response->statusCode == 200) {
		if (!response->body.IsEmpty() && response->body.Left(4) == "http") {
			int pos = response->body.Find("\n");
			if (pos > 0) {
				CStringA url = response->body.Left(pos);
				url.Trim();
				int pos1 = response->body.Find("\n", pos + 1);
				if (pos1 > pos) {
					CStringA version = response->body.Mid(pos, pos1 - pos);
					version.Trim();
					CString info = Utf8DecodeUni(response->body.Mid(pos1 + 1));
					info.Trim();
					CStringA our = _GLOBAL_VERSION;
					int count = version.Replace(".", ".");
					if (count < 4) {
						int i = count;
						while (i < 3) {
							version.Append(".0");
							i++;
						}
						count = our.Replace(".", ".");
						i = count;
						while (i < 3) {
							our.Append(".0");
							i++;
						}
						unsigned long ia = inet_addr(version.GetBuffer());
						if (ia != -1 && htonl(ia) > htonl(inet_addr(our.GetBuffer()))) {
							CString caption;
							caption.Format(_T("%s %s"), _T(_GLOBAL_NAME), Translate(_T("Update Available")));
							CString message = Translate(_T("Do you want to update now?"));
							if (!info.IsEmpty()) {
								message.AppendFormat(_T("\r\n\r\n%s"), info);
							}
							if (::MessageBox(this->m_hWnd, message, caption, MB_YESNO | MB_ICONQUESTION) == IDYES) {
								OpenURL(Utf8DecodeUni(url));
							}
						}
					}
				}
			}
		}
	}
	delete response;
	return 0;
}

#ifdef _GLOBAL_VIDEO
int CmainDlg::VideoCaptureDeviceId(CString name)
{
	unsigned count = PJMEDIA_VID_DEV_MAX_DEVS;
	pjmedia_vid_dev_info vid_dev_info[PJMEDIA_VID_DEV_MAX_DEVS];
	pjsua_vid_enum_devs(vid_dev_info, &count);
	for (unsigned i = 0; i < count; i++) {
		if (vid_dev_info[i].fmt_cnt && (vid_dev_info[i].dir == PJMEDIA_DIR_ENCODING || vid_dev_info[i].dir == PJMEDIA_DIR_ENCODING_DECODING)) {
			CString vidDevName = PjStrToWideChar(vid_dev_info[i].name);
			if ((!name.IsEmpty() && name == vidDevName)
				||
				(name.IsEmpty() && accountSettings.videoCaptureDevice == vidDevName)) {
				return vid_dev_info[i].id;
			}
		}
	}
	return PJMEDIA_VID_DEFAULT_CAPTURE_DEV;
}

void CmainDlg::createPreviewWin()
{
	if (!previewWin) {
		previewWin = new Preview(this);
	}
	previewWin->Start(VideoCaptureDeviceId());
}
#endif

