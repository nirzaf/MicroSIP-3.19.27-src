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

#pragma once

#include "define.h"
#include "stdafx.h"
#include <afxmt.h>
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>

enum EUserWndMessages
{
	UM_FIRST_USER_MSG = (WM_USER + 0x100 + 1),

	UM_UPDATEWINDOWTEXT,
	UM_NOTIFYICON,

	UM_UPDATE_SETTINGS,

	UM_CREATE_RINGING,
	UM_CALL_ANSWER,
	UM_CALL_HANGUP,
	UM_TAB_ICON_UPDATE,
	UM_ON_ACCOUNT,
	UM_ON_REG_STATE2,
	UM_ON_CALL_STATE,
	UM_ON_CALL_TRANSFER_STATUS,
	UM_ON_MWI_INFO,
	UM_ON_CALL_MEDIA_STATE,
	UM_ON_PAGER,
	UM_ON_PAGER_STATUS,
	UM_ON_BUDDY_STATE,
	UM_ON_PLAYER_PLAY,
	UM_ON_PLAYER_STOP,
	UM_SET_PANE_TEXT,
	UM_REFRESH_LEVELS,
	UM_USERS_DIRECTORY,
	UM_ON_BALANCE_PLAIN,
	UM_ON_BALANCE_OPTIONS,
	
	IDT_TIMER_IDLE,
	IDT_TIMER_TONE,
	IDT_TIMER_BALANCE,
	IDT_TIMER_INIT_RINGIN,
	IDT_TIMER_CALL,
	IDT_TIMER_CONTACTS_BLINK,
	IDT_TIMER_DIRECTORY,
	IDT_TIMER_SAVE,
	IDT_TIMER_SWITCH_DEVICES,
	IDT_TIMER_HEADSET,
	IDT_TIMER_VU_METER,
	IDT_TIMER_AUTOANSWER,
	IDT_TIMER_PROGRESS,
	UM_CLOSETAB,
	UM_DBLCLICKTAB,
	UM_QUERYTAB,
	UM_UPDATE_CHECKER_LOADED,

};

enum {
IDS_STATUSBAR,
IDS_STATUSBAR2,
};

enum {MSIP_MESSAGE_TYPE_LOCAL, MSIP_MESSAGE_TYPE_REMOTE, MSIP_MESSAGE_TYPE_SYSTEM};
enum {MSIP_TRANSPORT_AUTO, MSIP_TRANSPORT_TCP, MSIP_TRANSPORT_TLS};
enum {MSIP_CALL_OUT, MSIP_CALL_IN, MSIP_CALL_MISS};
enum { MSIP_SOUND_CUSTOM, MSIP_SOUND_CUSTOM_NOLOOP, MSIP_SOUND_MESSAGE_IN, MSIP_SOUND_MESSAGE_OUT, MSIP_SOUND_HANGUP, MSIP_SOUND_RINGTONE, MSIP_SOUND_RINGIN2, MSIP_SOUND_RINGING };
enum msip_srtp_type { MSIP_SRTP_DISABLED, MSIP_SRTP };

#define MSIP_SHORTCUT_CALL _T("call")
#define MSIP_SHORTCUT_VIDEOCALL _T("video")
#define MSIP_SHORTCUT_MESSAGE _T("message")
#define MSIP_SHORTCUT_DTMF _T("dtmf")
#define MSIP_SHORTCUT_TRANSFER _T("transfer")
#define MSIP_SHORTCUT_CONFERENCE _T("conference")
#define MSIP_SHORTCUT_RUNBATCH _T("runBatch")
#define MSIP_SHORTCUT_CALL_URL _T("callURL")
#define MSIP_SHORTCUT_POP_URL _T("popURL")

enum {
	MSIP_CONTACT_ICON_UNKNOWN,
	MSIP_CONTACT_ICON_OFFLINE,
	MSIP_CONTACT_ICON_AWAY,
	MSIP_CONTACT_ICON_ONLINE,
	MSIP_CONTACT_ICON_ON_THE_PHONE,
	MSIP_CONTACT_ICON_BLANK,
	MSIP_CONTACT_ICON_BUSY,
	MSIP_CONTACT_ICON_DEFAULT
};

struct Account {
	CString label;
	CString server;
	CString proxy;
	CString username;
	CString domain;
	int port;
	CString authID;
	CString password;
	bool rememberPassword;
	CString displayName;
	CString dialingPrefix;
	CString dialPlan;
	bool hideCID;
	CString voicemailNumber;
	CString srtp;
	CString transport;
	CString publicAddr;
	int registerRefresh;
	int keepAlive;
	bool publish;
	bool ice;
	bool allowRewrite;
	bool disableSessionTimer;
	bool operator==(const Account& a) const {
		if (
			label == a.label
			&& server == a.server
			&& proxy == a.proxy
			&& username == a.username
			&& domain == a.domain
			&& port == a.port
			&& authID == a.authID
			&& password == a.password
			&& displayName == a.displayName
			&& dialingPrefix == a.dialingPrefix
			&& dialPlan == a.dialPlan
			&& hideCID == a.hideCID
			&& voicemailNumber == a.voicemailNumber
			&& srtp == a.srtp
			&& transport == a.transport
			&& publicAddr == a.publicAddr
			&& publish == a.publish
			&& ice == a.ice
			&& allowRewrite == a.allowRewrite
			&& disableSessionTimer == a.disableSessionTimer
			)
			return true;
		return false;
	};
	bool operator!=(const Account& a) const {
		return !(*this == a);
	}
	void operator=(const Account& a)
	{
		label = a.label;
		server = a.server;
		proxy = a.proxy;
		username = a.username;
		domain = a.domain;
		port = a.port;
		authID = a.authID;
		password = a.password;
		displayName = a.displayName;
		dialingPrefix = a.dialingPrefix;
		dialPlan = a.dialPlan;
		hideCID = a.hideCID;
		voicemailNumber = a.voicemailNumber;
		srtp = a.srtp;
		transport = a.transport;
		publicAddr = a.publicAddr;
		publish = a.publish;
		ice = a.ice;
		allowRewrite = a.allowRewrite;
		disableSessionTimer = a.disableSessionTimer;
	};
	Account() : port(0)
		, rememberPassword(false)
		, registerRefresh(0)
		, keepAlive(0)
		, publish(false)
		, ice(false)
		, allowRewrite(false)
		, disableSessionTimer(false)
	{}
};

struct Shortcut {
	CString label;
	CString number;
	CString type;
};

struct player_eof_data
{
	pj_pool_t          *pool;
	pjsua_player_id player_id;
};

struct SIPURI {
	CString name;
	CString user;
	CString domain;
	CString parameters;
};

struct Contact {
	CString number;
	CString name;
	BOOL presence;
	BOOL directory;
	time_t presenceTime;
	BOOL ringing;
	CString presenceNote;
	int image;
	BOOL candidate;
	Contact():presenceTime(0),ringing(FALSE),image(0),candidate(FALSE){}
};

struct MessagesContact {
	CString name;
	CString number;
	CString numberParameters;
	CString messages;
	CString message;
	bool hasNewMessages;
	bool fromCommandLine;
	CString lastSystemMessage;
	CTime lastSystemMessageTime;
	pjsua_call_id callId;
	int mediaStatus;
	MessagesContact():mediaStatus(PJSUA_CALL_MEDIA_ERROR)
		,hasNewMessages(false)
		,fromCommandLine(false)
	{}
};

struct Call {
	int key;
	CString id;
	CString name;
	CString number;
	int type;
	int time;
	int duration;
	CString info;
};

struct call_tonegen_data
{
   pj_pool_t          *pool;
   pjmedia_port       *tonegen;
   pjsua_conf_port_id  toneslot;
};

struct call_user_data
{
	CCriticalSection CS;
	pjsua_call_id call_id;
	call_tonegen_data *tonegen_data;
	pjsua_recorder_id recorder_id;
	pj_timer_entry auto_hangup_timer;
	msip_srtp_type srtp;
	CString name;
	CString userAgent;
	CString diversion;
	CString callerID;
	CString commands;
	bool inConference;
	bool autoAnswer;
	bool hidden;
	int holdFrom;
	call_user_data(pjsua_call_id call_id): tonegen_data(NULL)
		,recorder_id(PJSUA_INVALID_ID)
		,inConference(false)
		,autoAnswer(false)
		,hidden(false)
		,holdFrom(-1)
		,srtp(MSIP_SRTP_DISABLED)
		{
			this->call_id = call_id;
			pj_bzero(&auto_hangup_timer, sizeof(auto_hangup_timer));
			auto_hangup_timer.id = PJSUA_INVALID_ID;
		}
};

extern struct call_tonegen_data *tone_gen;
extern int transport;
extern pjsua_acc_id account;
extern pjsua_acc_id account_local;
extern pjsua_conf_port_id msip_conf_port_id;
extern pjsua_call_id msip_conf_port_call_id;

extern int msip_audio_input;
extern int msip_audio_output;
extern int msip_audio_ring;

CString GetErrorMessage(pj_status_t status);
BOOL ShowErrorMessage(pj_status_t status);
BOOL IsIP(CString host);
CString RemovePort(CString domain);
void ParseSIPURI(CString in, SIPURI* out);
CString PjToStr(const pj_str_t* str, BOOL utf = FALSE);
pj_str_t StrToPjStr(CString str);
char* StrToPj(CString str);
CString Utf8DecodeUni(CStringA str);
CStringA UnicodeToAnsi(CString str);
CString AnsiToUnicode(CStringA str);
CString AnsiToWideChar(char* str);
CStringA StringToPjString(CString str);
char *WideCharToPjStr(CString str);
CString PjStrToWideChar(char *str);
CString XMLEntityDecode(CString str);
CString XMLEntityEncode(CString str);
void OpenURL(CString url);
CString GetDuration(int sec, bool zero = false);
void AddTransportSuffix(CString &str);
CString GetSIPURI(CString str, bool isSimple = false, bool isLocal = false, CString domain = _T(""));
bool SelectSIPAccount(CString number, pjsua_acc_id &acc_id, pj_str_t &pj_uri);
bool IsPSTNNnmber(CString number);
CString FormatNumber(CString number, CString *commands = NULL);
bool IniSectionExists(CString section, CString iniFile);
CString Bin2String(CByteArray *ca);
void String2Bin(CString str, CByteArray *res);
void CommandLineToShell(CString cmd, CString &command, CString &params);
void RunCmd(CString cmdLine, CString addParams=_T(""), bool noWait = false);

namespace MSIP {
	void GetScreenRect(CRect *rect);
}

CString get_account_username();
CString get_account_password();
CString get_account_domain();
CString get_account_server();
void get_account_proxy(Account *account, CStringList &proxies);

struct call_tonegen_data *call_init_tonegen(pjsua_call_id call_id);
BOOL call_play_digit(pjsua_call_id call_id, const char *digits, int duration = 160);
void call_deinit_tonegen(pjsua_call_id call_id);
void destroyDTMFPlayerTimerHandler(
  HWND hwnd,
  UINT uMsg,
  UINT_PTR idEvent,
  DWORD dwTime);
void DTMFQueueTimerHandler(
  HWND hwnd,
  UINT uMsg,
  UINT_PTR idEvent,
  DWORD dwTime);

void msip_call_hangup_fast(pjsua_call_id call_id,pjsua_call_info *p_call_info = NULL);

unsigned call_get_count_noincoming();
void call_hangup_all_noincoming(bool onHold=false);

void OpenHelp(CString code);


typedef struct {
	HWND hWnd;
	UINT message;
	CString url;
	CString username;
	CString password;
	bool post;
	CString postData;
	DWORD statusCode;
	CString headers;
	CStringA body;
} URLGetAsyncData;
void URLGetAsync(CString url, HWND hWnd=0, UINT message=0, bool post = false, CString postData = _T(""), CString username = _T(""), CString password = _T(""));
URLGetAsyncData URLGetSync(CString url);

CStringA urldecode(CStringA str);
CStringA urlencode(CStringA str);
CStringA char2hex(char dec);

CString URLMask(CString url, SIPURI* sipuri, pjsua_acc_id acc, call_user_data *user_data = NULL);
HICON LoadImageIcon(int i);

void msip_set_sound_device(int outDev, bool forse = 0);
void msip_call_dial_dtmf(pjsua_call_id call_id, CString digits);
void msip_call_send_dtmf_info(pjsua_call_id current_call, pj_str_t digits);

void msip_call_end(pjsua_call_id call_id);
void msip_conference_join(pjsua_call_info *call_info);
void msip_conference_leave(pjsua_call_info *call_info, bool hold = false);
void msip_call_hold(pjsua_call_info *call_info);
void msip_call_unhold(pjsua_call_info *call_info = NULL);
void msip_call_answer(pjsua_call_id call_id = PJSUA_INVALID_ID);
void msip_call_busy(pjsua_call_id call_id, CString reason = _T(""));
void msip_call_recording_start(call_user_data *user_data, pjsua_call_info *call_info = NULL);
void msip_call_recording_stop(call_user_data *user_data); 
CStringA msip_md5sum(CString *str);
CString msip_url_mask(CString url);
void msip_audio_input_set_volume(int val, bool mute = false);
void msip_audio_conf_set_volume(int val, bool mute);
pj_status_t msip_verify_sip_url(const char *url);
int msip_get_duration(pj_time_val *time_val);
