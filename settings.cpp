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
#include "settings.h"
#include "Crypto.h"

#include <algorithm>
#include <vector>

#include <Msi.h>
#pragma comment(lib, "Msi.lib")

AccountSettings accountSettings;
bool firstRun;
bool pj_ready;
CTime startTime;
CArray<Shortcut, Shortcut> shortcuts;

static LONGLONG FileSize(const wchar_t* name)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesEx(name, GetFileExInfoStandard, &fad))
		return -1; // error condition, could call GetLastError to find out more
	LARGE_INTEGER size;
	size.HighPart = fad.nFileSizeHigh;
	size.LowPart = fad.nFileSizeLow;
	return size.QuadPart;
}

void AccountSettings::Init()
{
	bool isPortable = false;
	CString str;
	LPTSTR ptr;
	accountId = 0;
	startTime = CTime::GetCurrentTime();
	//--
	ptr = exeFile.GetBuffer(MAX_PATH);
	GetModuleFileName(NULL, ptr, MAX_PATH);
	exeFile.ReleaseBuffer();
	//--
	pathExe = exeFile.Mid(0, exeFile.ReverseFind('\\'));
	//--
	CString fileName = PathFindFileName(exeFile);
	fileName = fileName.Mid(0, fileName.ReverseFind('.'));
	logFile.Format(_T("%s_log.txt"), fileName);
	iniFile.Format(_T("%s.ini"), fileName);
	pathRoaming = _T("");
	pathLocal = _T("");
	if (pathRoaming.IsEmpty()) {
		CString contactsFile = _T("Contacts.xml");
		CRegKey regKey;
		CString pathInstaller;
		CString rab;
		ULONG pnChars;
		rab.Format(_T("Software\\%s"), _T(_GLOBAL_NAME));
		if (regKey.Open(HKEY_CURRENT_USER, rab, KEY_READ) == ERROR_SUCCESS) {
			ptr = pathInstaller.GetBuffer(255);
			pnChars = 256;
			regKey.QueryStringValue(NULL, ptr, &pnChars);
			pathInstaller.ReleaseBuffer();
			regKey.Close();
		}
		if (pathInstaller.IsEmpty() && regKey.Open(HKEY_LOCAL_MACHINE, rab, KEY_READ) == ERROR_SUCCESS) {
			ptr = pathInstaller.GetBuffer(255);
			pnChars = 256;
			regKey.QueryStringValue(NULL, ptr, &pnChars);
			pathInstaller.ReleaseBuffer();
			regKey.Close();
		}
		if (pathInstaller.IsEmpty()) {
			DWORD i = 0;
			wchar_t lpProductBuf[39];
			while (MsiEnumProducts(i, lpProductBuf) == ERROR_SUCCESS) {
				wchar_t valueBuf[256];
				DWORD cchValueBuf = 256;
				if (MsiGetProductInfo(lpProductBuf, INSTALLPROPERTY_INSTALLEDPRODUCTNAME, valueBuf, &cchValueBuf) == ERROR_SUCCESS) {
					if (StrCmp(valueBuf, _T(_GLOBAL_NAME)) == 0) {
						cchValueBuf = 256;
						if (MsiGetProductInfo(lpProductBuf, INSTALLPROPERTY_INSTALLLOCATION, valueBuf, &cchValueBuf) == ERROR_SUCCESS) {
							pathInstaller = valueBuf;
						}
						if (!pathInstaller.IsEmpty()) {
							pathInstaller.TrimRight('\\');
						} else {
							pathInstaller = pathExe;
						}
						break;
					}
				}
				i++;
			}
		}
		CString appDataRoaming;
		ptr = appDataRoaming.GetBuffer(MAX_PATH);
		SHGetSpecialFolderPath(
			0,
			ptr,
			CSIDL_APPDATA,
			FALSE);
		appDataRoaming.ReleaseBuffer();
		appDataRoaming.AppendFormat(_T("\\%s\\"), _T(_GLOBAL_NAME));

		if (!pathInstaller.IsEmpty() && pathInstaller.CompareNoCase(pathExe) == 0) {
			// installer
			CreateDirectory(appDataRoaming, NULL);
			pathRoaming = appDataRoaming;
			CString appDataLocal;
			ptr = appDataLocal.GetBuffer(MAX_PATH);
			SHGetSpecialFolderPath(
				0,
				ptr,
				CSIDL_LOCAL_APPDATA,
				FALSE);
			appDataLocal.ReleaseBuffer();
			appDataLocal.AppendFormat(_T("\\%s\\"), _T(_GLOBAL_NAME));
			CreateDirectory(appDataLocal, NULL);
			pathLocal = appDataLocal;
			logFile = pathLocal + logFile;
		}
		else {
			// portable
			isPortable = true;
			pathRoaming = pathExe + _T("\\");
			pathLocal = pathRoaming;
			logFile = pathLocal + logFile;
		}

		iniFile = pathRoaming + iniFile;

		if (!::PathFileExists(iniFile) || FileSize(iniFile) == 0) {
			firstRun = true;
			// create UTF16-LE BOM(FFFE)
			WORD wBOM = 0xFEFF;
			CString pszSectionB = _T("[Settings]");
			CFile file;
			CFileException fileException;
			if (file.Open(iniFile, CFile::modeCreate | CFile::modeReadWrite, &fileException)) {
				file.Write(&wBOM, sizeof(wBOM));
				file.Write(pszSectionB.GetBuffer(), pszSectionB.GetLength() * sizeof(wchar_t));
				file.Close();
			}
		}
		else {
			firstRun = false;
			CFile file;
			CFileException fileException;
			if (file.Open(iniFile, CFile::modeReadWrite, &fileException)) {
				WORD wBOM;
				if (sizeof(WORD) == file.Read(&wBOM, sizeof(WORD))) {
					if (wBOM != 0xFEFF) {
						// convert to UTF16-LE BOM
						file.SeekToBegin();
						CStringA data;
						char buf[256];
						int count;
						while (true) {
							count = file.Read(buf, sizeof(buf));
							data.Append(buf, count);
							if (count < sizeof(buf)) {
								break;
							}
						}
						file.SetLength(0);
						wBOM = 0xFEFF;
						file.Write(&wBOM, sizeof(wBOM));
						CString res = AnsiToWideChar(data.GetBuffer());
						file.Write(res.GetBuffer(), data.GetLength() * sizeof(wchar_t));
					}
				}
				file.Close();
			}
		}
	}
	//--

	CString section;
	section = _T("Settings");

	// load user settings

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("singleMode"), _T("1"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	singleMode = _wtoi(str);

	ptr = ringtone.GetBuffer(255);
	GetPrivateProfileString(section, _T("ringingSound"), NULL, ptr, 256, iniFile);
	ringtone.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("volumeRing"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	volumeRing = str.IsEmpty() ? 100 : _wtoi(str);

	ptr = audioRingDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioRingDevice"), NULL, ptr, 256, iniFile);
	audioRingDevice.ReleaseBuffer();
	ptr = audioOutputDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioOutputDevice"), NULL, ptr, 256, iniFile);
	audioOutputDevice.ReleaseBuffer();
	ptr = audioInputDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioInputDevice"), NULL, ptr, 256, iniFile);
	audioInputDevice.ReleaseBuffer();


	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("micAmplification"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	micAmplification = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("swLevelAdjustment"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	swLevelAdjustment = _wtoi(str);

	ptr = audioCodecs.GetBuffer(255);
	GetPrivateProfileString(section, _T("audioCodecs"), _T(_GLOBAL_CODECS_ENABLED), ptr, 256, iniFile);
	audioCodecs.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("VAD"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	vad = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("EC"), _T("1"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	ec = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("forceCodec"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	forceCodec = _wtoi(str);

#ifdef _GLOBAL_VIDEO
	ptr = videoCaptureDevice.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoCaptureDevice"), NULL, ptr, 256, iniFile);
	videoCaptureDevice.ReleaseBuffer();

	ptr = videoCodec.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoCodec"), NULL, ptr, 256, iniFile);
	videoCodec.ReleaseBuffer();
	
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoH264"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoH264 = str == "0" ? 0 : 1;
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoH263"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoH263 = str == "0" ? 0 : 1;
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoVP8"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoVP8 = str == "0" ? 0 : 1;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("videoBitrate"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	videoBitrate = _wtoi(str);
#endif

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("rport"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	rport = str == "0" ? 0 : 1;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("sourcePort"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	sourcePort = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("rtpPortMin"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	rtpPortMin = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("rtpPortMax"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	rtpPortMax = _wtoi(str);

	ptr = dnsSrvNs.GetBuffer(255);
	GetPrivateProfileString(section, _T("dnsSrvNs"), NULL, ptr, 256, iniFile);
	dnsSrvNs.ReleaseBuffer();
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("dnsSrv"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	dnsSrv = str == "1" ? 1 : 0;

	ptr = stun.GetBuffer(255);
	GetPrivateProfileString(section, _T("STUN"), NULL, ptr, 256, iniFile);
	stun.ReleaseBuffer();
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableSTUN"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableSTUN = str == "1" ? 1 : 0;

	if (isPortable) {
		str = _T("Recordings");
	}
	else {
		ptr = str.GetBuffer(MAX_PATH);
		SHGetSpecialFolderPath(
			0,
			ptr,
			CSIDL_DESKTOPDIRECTORY,
			FALSE);
		str.ReleaseBuffer();
		str.Append(_T("\\Recordings"));
	}
	ptr = recordingPath.GetBuffer(255);
	GetPrivateProfileString(section, _T("recordingPath"), str, ptr, 256, iniFile);
	recordingPath.ReleaseBuffer();

	ptr = recordingFormat.GetBuffer(255);
	GetPrivateProfileString(section, _T("recordingFormat"), NULL, ptr, 256, iniFile);
	recordingFormat.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("autoRecording"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	autoRecording = str == "1" ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("DTMFMethod"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	DTMFMethod = _wtoi(str);

	ptr = autoAnswer.GetBuffer(255);
	GetPrivateProfileString(section, _T("autoAnswer"), _T(_GLOBAL_SETT_AA_DEFAULT), ptr, 256, iniFile);
	autoAnswer.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("autoAnswerDelay"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	autoAnswerDelay = _wtoi(str);

	ptr = denyIncoming.GetBuffer(255);
	GetPrivateProfileString(section, _T("denyIncoming"), _T(_GLOBAL_SETT_DENYINC_DEFAULT), ptr, 256, iniFile);
	denyIncoming.ReleaseBuffer();

	//--
	ptr = usersDirectory.GetBuffer(255);
	GetPrivateProfileString(section, _T("usersDirectory"), NULL, ptr, 256, iniFile);
	usersDirectory.ReleaseBuffer();

	ptr = defaultAction.GetBuffer(255);
	GetPrivateProfileString(section, _T("defaultAction"), NULL, ptr, 256, iniFile);
	defaultAction.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableMediaButtons"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableMediaButtons = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("localDTMF"), _T("1"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	localDTMF = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableLog"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableLog = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("bringToFrontOnIncoming"), _T("1"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	bringToFrontOnIncoming = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableLocalAccount"), NULL, ptr, 256, iniFile);

	str.ReleaseBuffer();
	enableLocalAccount = str == _T("1") ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("randomAnswerBox"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	randomAnswerBox = _wtoi(str);

	crashReport = 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("callWaiting"), _T("1"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	callWaiting = _wtoi(str);

	ptr = updatesInterval.GetBuffer(255);
	GetPrivateProfileString(section, _T("updatesInterval"), NULL, ptr, 256, iniFile);
	updatesInterval.ReleaseBuffer();
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("checkUpdatesTime"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	checkUpdatesTime = _wtoi(str);

	// load ini settings

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("noResize"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	noResize = str == _T("1") ? 1 : 0;

	ptr = userAgent.GetBuffer(255);
	GetPrivateProfileString(section, _T("userAgent"), NULL, ptr, 256, iniFile);
	userAgent.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("autoHangUpTime"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	autoHangUpTime = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("maxConcurrentCalls"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	maxConcurrentCalls = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("noIgnoreCall"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	noIgnoreCall = str == "1" ? 1 : 0;

	ptr = cmdOutgoingCall.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdOutgoingCall"), NULL, ptr, 256, iniFile);
	cmdOutgoingCall.ReleaseBuffer();

	ptr = cmdIncomingCall.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdIncomingCall"), NULL, ptr, 256, iniFile);
	cmdIncomingCall.ReleaseBuffer();

	ptr = cmdCallRing.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallRing"), NULL, ptr, 256, iniFile);
	cmdCallRing.ReleaseBuffer();

	ptr = cmdCallAnswer.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallAnswer"), NULL, ptr, 256, iniFile);
	cmdCallAnswer.ReleaseBuffer();

	ptr = cmdCallBusy.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallBusy"), NULL, ptr, 256, iniFile);
	cmdCallBusy.ReleaseBuffer();

	ptr = cmdCallStart.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallStart"), NULL, ptr, 256, iniFile);
	cmdCallStart.ReleaseBuffer();

	ptr = cmdCallEnd.GetBuffer(255);
	GetPrivateProfileString(section, _T("cmdCallEnd"), NULL, ptr, 256, iniFile);
	cmdCallEnd.ReleaseBuffer();

	hidden = 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("silent"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	silent = str == "1" ? 1 : 0;

	ptr = portKnockerHost.GetBuffer(255);
	GetPrivateProfileString(section, _T("portKnockerHost"), NULL, ptr, 256, iniFile);
	portKnockerHost.ReleaseBuffer();

	ptr = portKnockerPorts.GetBuffer(255);
	GetPrivateProfileString(section, _T("portKnockerPorts"), NULL, ptr, 256, iniFile);
	portKnockerPorts.ReleaseBuffer();

	// load system settings

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainX"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainX = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainY"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainY = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainW"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainW = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("mainH"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	mainH = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesX"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesX = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesY"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesY = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesW"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesW = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("messagesH"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	messagesH = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("ringinX"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	ringinX = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("ringinY"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	ringinY = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("callsWidth0"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	callsWidth0 = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("callsWidth1"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	callsWidth1 = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("callsWidth2"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	callsWidth2 = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("callsWidth3"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	callsWidth3 = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("callsWidth4"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	callsWidth4 = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("contactsWidth0"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	contactsWidth0 = _wtoi(str);
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("contactsWidth1"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	contactsWidth1 = _wtoi(str);
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("contactsWidth2"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	contactsWidth2 = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("volumeOutput"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	volumeOutput = str.IsEmpty() ? 100 : _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("volumeInput"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	volumeInput = str.IsEmpty() ? 100 : _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("activeTab"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	activeTab = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("AA"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	AA = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("AC"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	AC = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("DND"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	DND = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("alwaysOnTop"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	alwaysOnTop = _wtoi(str);
	
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("enableShortcuts"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	enableShortcuts = _wtoi(str);

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("shortcutsBottom"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	shortcutsBottom = _wtoi(str);

	ptr = lastCallNumber.GetBuffer(255);
	GetPrivateProfileString(section, _T("lastCallNumber"), NULL, ptr, 256, iniFile);
	lastCallNumber.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("lastCallHasVideo"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	lastCallHasVideo = (str == _T("1"));

	//--
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("accountId"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	if (str.IsEmpty()) {
		if (AccountLoad(-2, &account)) {
			accountId = 1;
			WritePrivateProfileString(section, _T("accountId"), _T("1"), iniFile);
		}
	}
	else {
		accountId = _wtoi(str);
		if (!accountId && !enableLocalAccount) {
			accountId = 1;
		}
		if (accountId > 0) {
			if (!AccountLoad(accountId, &account)) {
				accountId = 0;
			}
		}
	}
	AccountLoad(0, &accountLocal);
}

AccountSettings::AccountSettings()
{
	Init();
}

void AccountSettings::AccountDelete(int id)
{
	CString section;
	section.Format(_T("Account%d"), id);
	//!!WritePrivateProfileStruct(section, NULL, NULL, 0, iniFile);
	WritePrivateProfileString(section, NULL, NULL, iniFile);
}

bool AccountSettings::AccountLoad(int id, Account *account)
{
	CString str;
	CString rab;
	LPTSTR ptr;

	CString section;
	if (id == -2) {
		section = _T("Settings");
}
	else {
		section.Format(_T("Account%d"), id);
	}

	ptr = account->label.GetBuffer(255);
	GetPrivateProfileString(section, _T("label"), id ? NULL : _T("Local"), ptr, 256, iniFile);
	account->label.ReleaseBuffer();

	ptr = account->server.GetBuffer(255);
	GetPrivateProfileString(section, _T("server"), NULL, ptr, 256, iniFile);
	account->server.ReleaseBuffer();

	ptr = account->proxy.GetBuffer(255);
	GetPrivateProfileString(section, _T("proxy"), NULL, ptr, 256, iniFile);
	account->proxy.ReleaseBuffer();

	ptr = account->domain.GetBuffer(255);
	GetPrivateProfileString(section, _T("domain"), NULL, ptr, 256, iniFile);
	account->domain.ReleaseBuffer();

	ptr = account->username.GetBuffer(255);
	GetPrivateProfileString(section, _T("username"), id ? NULL : _T("user"), ptr, 256, iniFile);
	account->username.ReleaseBuffer();

	ptr = account->password.GetBuffer(1040);
	GetPrivateProfileString(section, _T("password"), NULL, ptr, 1041, iniFile);
	account->password.ReleaseBuffer();
	if (!account->password.IsEmpty()) {
		CByteArray arPassword;
		String2Bin(account->password, &arPassword);
		MFC::CCrypto crypto;
		if (crypto.DeriveKey((LPCTSTR)_GLOBAL_KEY)) {
			try {
				if (!crypto.Decrypt(arPassword, account->password)) {
					//--decode from old format
					ptr = str.GetBuffer(255);
					GetPrivateProfileString(section, _T("passwordSize"), NULL, ptr, 256, iniFile);
					str.ReleaseBuffer();
					if (!str.IsEmpty()) {
						int size = _wtoi(str);
						arPassword.SetSize(size > 0 ? size : 16);
						GetPrivateProfileStruct(section, _T("password"), arPassword.GetData(), arPassword.GetSize(), iniFile);
						crypto.Decrypt(arPassword, account->password);
					}
					//--end decode from old format
					if (crypto.Encrypt(account->password, arPassword)) {
						WritePrivateProfileString(section, _T("password"), Bin2String(&arPassword), iniFile);
						//--delete old format addl.data
						WritePrivateProfileString(section, _T("passwordSize"), NULL, iniFile);
					}
				}
			}
			catch (CArchiveException *e) {
			}
		}
					}

	account->rememberPassword = account->username.GetLength() ? 1 : 0;


	ptr = account->authID.GetBuffer(255);
	GetPrivateProfileString(section, _T("authID"), NULL, ptr, 256, iniFile);
	account->authID.ReleaseBuffer();

	ptr = account->displayName.GetBuffer(255);
	GetPrivateProfileString(section, _T("displayName"), NULL, ptr, 256, iniFile);
	account->displayName.ReleaseBuffer();

	ptr = account->dialingPrefix.GetBuffer(255);
	GetPrivateProfileString(section, _T("dialingPrefix"), NULL, ptr, 256, iniFile);
	account->dialingPrefix.ReleaseBuffer();

	ptr = account->dialPlan.GetBuffer(255);
	GetPrivateProfileString(section, _T("dialPlan"), NULL, ptr, 256, iniFile);
	account->dialPlan.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("hideCID"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->hideCID = str == _T("1") ? 1 : 0;

	ptr = account->voicemailNumber.GetBuffer(255);
	GetPrivateProfileString(section, _T("voicemailNumber"), NULL, ptr, 256, iniFile);
	account->voicemailNumber.ReleaseBuffer();

	ptr = account->srtp.GetBuffer(255);
	GetPrivateProfileString(section, _T("SRTP"), NULL, ptr, 256, iniFile);
	account->srtp.ReleaseBuffer();

	ptr = account->transport.GetBuffer(255);
	GetPrivateProfileString(section, _T("transport"), NULL, ptr, 256, iniFile);
	account->transport.ReleaseBuffer();

	ptr = account->publicAddr.GetBuffer(255);
	GetPrivateProfileString(section, _T("publicAddr"), NULL, ptr, 256, iniFile);
	account->publicAddr.ReleaseBuffer();

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("registerRefresh"), _T("300"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->registerRefresh = _wtoi(str);
	if (account->registerRefresh <= 0) {
		account->registerRefresh = 300;
	}
	else if (account->registerRefresh <= 10) {
		account->registerRefresh = 10;
	}
	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("keepAlive"), _T("15"), ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->keepAlive = _wtoi(str);
	if (account->keepAlive <= 0) {
		account->keepAlive = 15;
	}

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("publish"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->publish = str == _T("1") ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("ICE"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->ice = str == _T("1") ? 1 : 0;

	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("allowRewrite"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->allowRewrite = str == _T("1") ? 1 : 0;


	ptr = str.GetBuffer(255);
	GetPrivateProfileString(section, _T("disableSessionTimer"), NULL, ptr, 256, iniFile);
	str.ReleaseBuffer();
	account->disableSessionTimer = str == _T("1") ? 1 : 0;

	bool sectionExists = IniSectionExists(section, iniFile);

	if (id == -2) {
		// delete old
		WritePrivateProfileString(section, _T("server"), NULL, iniFile);
		WritePrivateProfileString(section, _T("proxy"), NULL, iniFile);
		WritePrivateProfileString(section, _T("SRTP"), NULL, iniFile);
		WritePrivateProfileString(section, _T("transport"), NULL, iniFile);
		WritePrivateProfileString(section, _T("publicAddr"), NULL, iniFile);
		WritePrivateProfileString(section, _T("publish"), NULL, iniFile);
		WritePrivateProfileString(section, _T("STUN"), NULL, iniFile);
		WritePrivateProfileString(section, _T("ICE"), NULL, iniFile);
		WritePrivateProfileString(section, _T("allowRewrite"), NULL, iniFile);
		WritePrivateProfileString(section, _T("domain"), NULL, iniFile);
		WritePrivateProfileString(section, _T("authID"), NULL, iniFile);
		WritePrivateProfileString(section, _T("username"), NULL, iniFile);
		WritePrivateProfileString(section, _T("passwordSize"), NULL, iniFile);
		WritePrivateProfileString(section, _T("password"), NULL, iniFile);
		WritePrivateProfileString(section, _T("id"), NULL, iniFile);
		WritePrivateProfileString(section, _T("displayName"), NULL, iniFile);
		// save new
		//if (!account->domain.IsEmpty() && !account->username.IsEmpty()) {
		if (sectionExists && !account->domain.IsEmpty()) {
			AccountSave(1, account);
	}
				}
	//return !account->domain.IsEmpty() && !account->username.IsEmpty();

	if (id == 0) {
		return true;// local account
	}

	return  sectionExists && !account->domain.IsEmpty();
			}

void AccountSettings::AccountSave(int id, Account *account)
{
	CString str;
	CString section;
	section.Format(_T("Account%d"), id);

	WritePrivateProfileString(section, _T("label"), account->label, iniFile);

	WritePrivateProfileString(section, _T("server"), account->server, iniFile);

	WritePrivateProfileString(section, _T("proxy"), account->proxy, iniFile);

	WritePrivateProfileString(section, _T("domain"), account->domain, iniFile);

	if (!account->rememberPassword) {
		WritePrivateProfileString(section, _T("username"), _T(""), iniFile);
		WritePrivateProfileString(section, _T("password"), _T(""), iniFile);
	}
	else {
		WritePrivateProfileString(section, _T("username"), account->username, iniFile);
		MFC::CCrypto crypto;
		CByteArray arPassword;
		if (!account->password.IsEmpty() && crypto.DeriveKey((LPCTSTR)_GLOBAL_KEY)
			&& crypto.Encrypt(account->password, arPassword)
			) {
			str = Bin2String(&arPassword);
		}
		else {
			str = account->password;
	}
		WritePrivateProfileString(section, _T("password"), str, iniFile);
	}

	WritePrivateProfileString(section, _T("authID"), account->authID, iniFile);

	WritePrivateProfileString(section, _T("displayName"), account->displayName, iniFile);

	WritePrivateProfileString(section, _T("dialingPrefix"), account->dialingPrefix, iniFile);

	WritePrivateProfileString(section, _T("dialPlan"), account->dialPlan, iniFile);

	WritePrivateProfileString(section, _T("hideCID"), account->hideCID ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("voicemailNumber"), account->voicemailNumber, iniFile);

	WritePrivateProfileString(section, _T("transport"), account->transport, iniFile);
	WritePrivateProfileString(section, _T("publicAddr"), account->publicAddr, iniFile);
	WritePrivateProfileString(section, _T("SRTP"), account->srtp, iniFile);
	str.Format(_T("%d"), account->registerRefresh);
	WritePrivateProfileString(section, _T("registerRefresh"), str, iniFile);
	str.Format(_T("%d"), account->keepAlive);
	WritePrivateProfileString(section, _T("keepAlive"), str, iniFile);
	WritePrivateProfileString(section, _T("publish"), account->publish ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("ICE"), account->ice ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("allowRewrite"), account->allowRewrite ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("disableSessionTimer"), account->disableSessionTimer ? _T("1") : _T("0"), iniFile);
	}

void AccountSettings::SettingsSave()
{
	CString str;
	LPTSTR ptr;

	CString section;
	section = _T("Settings");

	str.Format(_T("%d"), accountId);
	WritePrivateProfileString(section, _T("accountId"), str, iniFile);

// save user settings

	WritePrivateProfileString(section, _T("singleMode"), singleMode ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("ringingSound"), ringtone, iniFile);
	str.Format(_T("%d"), volumeRing);
	WritePrivateProfileString(section, _T("volumeRing"), str, iniFile);
	WritePrivateProfileString(section, _T("audioRingDevice"), _T("\"") + audioRingDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("audioOutputDevice"), _T("\"") + audioOutputDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("audioInputDevice"), _T("\"") + audioInputDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("micAmplification"), micAmplification ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("swLevelAdjustment"), swLevelAdjustment ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("audioCodecs"), audioCodecs, iniFile);
	WritePrivateProfileString(section, _T("VAD"), vad ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("EC"), ec ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("forceCodec"), forceCodec ? _T("1") : _T("0"), iniFile);
#ifdef _GLOBAL_VIDEO
	WritePrivateProfileString(section, _T("videoCaptureDevice"), _T("\"") + videoCaptureDevice + _T("\""), iniFile);
	WritePrivateProfileString(section, _T("videoCodec"), videoCodec, iniFile);
	WritePrivateProfileString(section, _T("videoH264"), videoH264 ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("videoH263"), videoH263 ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("videoVP8"), videoVP8 ? _T("1") : _T("0"), iniFile);
	str.Format(_T("%d"), videoBitrate);
	WritePrivateProfileString(section, _T("videoBitrate"), str, iniFile);
#endif
	WritePrivateProfileString(section, _T("rport"), rport ? _T("1") : _T("0"), iniFile);
	str.Format(_T("%d"), sourcePort);
	WritePrivateProfileString(section, _T("sourcePort"), str, iniFile);
	str.Format(_T("%d"), rtpPortMin);
	WritePrivateProfileString(section, _T("rtpPortMin"), str, iniFile);
	str.Format(_T("%d"), rtpPortMax);
	WritePrivateProfileString(section, _T("rtpPortMax"), str, iniFile);
	WritePrivateProfileString(section, _T("dnsSrvNs"), dnsSrvNs, iniFile);
	WritePrivateProfileString(section, _T("dnsSrv"), dnsSrv ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("STUN"), stun, iniFile);
	WritePrivateProfileString(section, _T("enableSTUN"), enableSTUN ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("recordingPath"), recordingPath, iniFile);
	WritePrivateProfileString(section, _T("recordingFormat"), recordingFormat, iniFile);
	WritePrivateProfileString(section, _T("autoRecording"), autoRecording ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("DTMFMethod"), DTMFMethod == 1 ? _T("1") : (DTMFMethod == 2 ? _T("2") : (DTMFMethod == 3 ? _T("3") : _T("0"))), iniFile);
	WritePrivateProfileString(section, _T("autoAnswer"), autoAnswer, iniFile);
	str.Format(_T("%d"), autoAnswerDelay);
	WritePrivateProfileString(section, _T("autoAnswerDelay"), str, iniFile);
	WritePrivateProfileString(section, _T("denyIncoming"), denyIncoming, iniFile);
	WritePrivateProfileString(section, _T("usersDirectory"), usersDirectory, iniFile);
	WritePrivateProfileString(section, _T("defaultAction"), defaultAction, iniFile);
	WritePrivateProfileString(section, _T("enableMediaButtons"), enableMediaButtons ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("localDTMF"), localDTMF ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("enableLog"), enableLog ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("bringToFrontOnIncoming"), bringToFrontOnIncoming ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("enableLocalAccount"), enableLocalAccount ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("randomAnswerBox"), randomAnswerBox ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("callWaiting"), callWaiting ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("updatesInterval"), updatesInterval, iniFile);
	str.Format(_T("%d"), checkUpdatesTime);
	WritePrivateProfileString(section, _T("checkUpdatesTime"), str, iniFile);

	// save ini settings

	str.Format(_T("%d"), noResize);
	WritePrivateProfileString(section, _T("noResize"), str, iniFile);

	WritePrivateProfileString(section, _T("userAgent"), userAgent, iniFile);

	str.Format(_T("%d"), autoHangUpTime);
	WritePrivateProfileString(section, _T("autoHangUpTime"), str, iniFile);

	str.Format(_T("%d"), maxConcurrentCalls);
	WritePrivateProfileString(section, _T("maxConcurrentCalls"), str, iniFile);
	WritePrivateProfileString(section, _T("noIgnoreCall"), noIgnoreCall ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("cmdOutgoingCall"), cmdOutgoingCall, iniFile);
	WritePrivateProfileString(section, _T("cmdIncomingCall"), cmdIncomingCall, iniFile);
	WritePrivateProfileString(section, _T("cmdCallRing"), cmdCallRing, iniFile);
	WritePrivateProfileString(section, _T("cmdCallAnswer"), cmdCallAnswer, iniFile);
	WritePrivateProfileString(section, _T("cmdCallBusy"), cmdCallBusy, iniFile);
	WritePrivateProfileString(section, _T("cmdCallStart"), cmdCallStart, iniFile);
	WritePrivateProfileString(section, _T("cmdCallEnd"), cmdCallEnd, iniFile);
	WritePrivateProfileString(section, _T("silent"), silent ? _T("1") : _T("0"), iniFile);

	WritePrivateProfileString(section, _T("portKnockerHost"), portKnockerHost, iniFile);

	WritePrivateProfileString(section, _T("portKnockerPorts"), portKnockerPorts, iniFile);

	// save system settings
	
	str.Format(_T("%d"), mainX);
	WritePrivateProfileString(section, _T("mainX"), str, iniFile);

	str.Format(_T("%d"), mainY);
	WritePrivateProfileString(section, _T("mainY"), str, iniFile);

	str.Format(_T("%d"), mainW);
	WritePrivateProfileString(section, _T("mainW"), str, iniFile);

	str.Format(_T("%d"), mainH);
	WritePrivateProfileString(section, _T("mainH"), str, iniFile);

	str.Format(_T("%d"), messagesX);
	WritePrivateProfileString(section, _T("messagesX"), str, iniFile);

	str.Format(_T("%d"), messagesY);
	WritePrivateProfileString(section, _T("messagesY"), str, iniFile);

	str.Format(_T("%d"), messagesW);
	WritePrivateProfileString(section, _T("messagesW"), str, iniFile);

	str.Format(_T("%d"), messagesH);
	WritePrivateProfileString(section, _T("messagesH"), str, iniFile);

	str.Format(_T("%d"), ringinX);
	WritePrivateProfileString(section, _T("ringinX"), str, iniFile);

	str.Format(_T("%d"), ringinY);
	WritePrivateProfileString(section, _T("ringinY"), str, iniFile);

	str.Format(_T("%d"), callsWidth0);
	WritePrivateProfileString(section, _T("callsWidth0"), str, iniFile);

	str.Format(_T("%d"), callsWidth1);
	WritePrivateProfileString(section, _T("callsWidth1"), str, iniFile);

	str.Format(_T("%d"), callsWidth2);
	WritePrivateProfileString(section, _T("callsWidth2"), str, iniFile);

	str.Format(_T("%d"), callsWidth3);
	WritePrivateProfileString(section, _T("callsWidth3"), str, iniFile);

	str.Format(_T("%d"), callsWidth4);
	WritePrivateProfileString(section, _T("callsWidth4"), str, iniFile);

	str.Format(_T("%d"), contactsWidth0);
	WritePrivateProfileString(section, _T("contactsWidth0"), str, iniFile);
	str.Format(_T("%d"), contactsWidth1);
	WritePrivateProfileString(section, _T("contactsWidth1"), str, iniFile);
	str.Format(_T("%d"), contactsWidth2);
	WritePrivateProfileString(section, _T("contactsWidth2"), str, iniFile);

	str.Format(_T("%d"), volumeOutput);
	WritePrivateProfileString(section, _T("volumeOutput"), str, iniFile);

	str.Format(_T("%d"), volumeInput);
	WritePrivateProfileString(section, _T("volumeInput"), str, iniFile);

	str.Format(_T("%d"), activeTab);
	WritePrivateProfileString(section, _T("activeTab"), str, iniFile);

	WritePrivateProfileString(section, _T("AA"), AA ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("AC"), AC ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("DND"), DND ? _T("1") : _T("0"), iniFile);

	str.Format(_T("%d"), alwaysOnTop);
	WritePrivateProfileString(section, _T("alwaysOnTop"), str, iniFile);

	WritePrivateProfileString(section, _T("enableShortcuts"), enableShortcuts ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("shortcutsBottom"), shortcutsBottom ? _T("1") : _T("0"), iniFile);
	WritePrivateProfileString(section, _T("lastCallNumber"), lastCallNumber, iniFile);
	WritePrivateProfileString(section, _T("lastCallHasVideo"), lastCallHasVideo ? _T("1") : _T("0"), iniFile);
}

CString ShortcutEncode(Shortcut *pShortcut)
{
	CString data;
	data.Format(_T("%s;%s;%s"), pShortcut->label, pShortcut->number, pShortcut->type);
	return data;
}

void ShortcutDecode(CString str, Shortcut *pShortcut)
{
	pShortcut->label.Empty();
	pShortcut->number.Empty();
	pShortcut->type = MSIP_SHORTCUT_DTMF;

	CString rab;
	int begin;
	int end;
	begin = 0;
	end = str.Find(';', begin);
	if (end != -1) {
		pShortcut->label = str.Mid(begin, end - begin);
		begin = end + 1;
		end = str.Find(';', begin);
		if (end != -1) {
			pShortcut->number = str.Mid(begin, end - begin);
			begin = end + 1;
			end = str.Find(';', begin);
			if (end != -1) {
				pShortcut->type = str.Mid(begin, end - begin);
			}
			else {
				pShortcut->type = str.Mid(begin);
			}
			if (pShortcut->type == _T("0")) {
				pShortcut->type = MSIP_SHORTCUT_CALL;
			}
			else if (pShortcut->type == _T("1")) {
				pShortcut->type = MSIP_SHORTCUT_VIDEOCALL;
			}
			else if (pShortcut->type == _T("2")) {
				pShortcut->type = MSIP_SHORTCUT_MESSAGE;
			}
			else if (pShortcut->type == _T("3")) {
				pShortcut->type = MSIP_SHORTCUT_DTMF;
			}
			else if (pShortcut->type == _T("4")) {
				pShortcut->type = MSIP_SHORTCUT_TRANSFER;
			}
		}
	}
}

void ShortcutsLoad()
{
	Shortcut shortcut;
	CString key;
	CString val;
	LPTSTR ptr = val.GetBuffer(255);
	int i = 0;
	while (i < _GLOBAL_SHORTCUTS_QTY) {
		key.Format(_T("%d"), i);
		if (GetPrivateProfileString(_T("Shortcuts"), key, NULL, ptr, 256, accountSettings.iniFile)) {
			ShortcutDecode(ptr, &shortcut);
			shortcuts.Add(shortcut);
	}
		else {
			break;
		}
		i++;
	}
	if (!shortcuts.GetCount()) {
}
}

void ShortcutsSave()
{
	WritePrivateProfileSection(_T("Shortcuts"), NULL, accountSettings.iniFile);
	for (int i = 0; i < shortcuts.GetCount(); i++) {
		Shortcut shortcut = shortcuts.GetAt(i);
		CString key;
		key.Format(_T("%d"), i);
		WritePrivateProfileString(_T("Shortcuts"), key, ShortcutEncode(&shortcut), accountSettings.iniFile);
	}
}

