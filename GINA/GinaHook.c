/*
 Copyright (c) 2008, Guillaume Seguin (guillaume@paralint.com)
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 SUCH DAMAGE.
*/

#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <winwlx.h>

#include "Ginahook.h"
#include "GinaDlg.h"
#include "global.h"
#include "trace.h"
#include "debug.h"
#include "randpasswd.h"
#include "SecurityHelper.h"
#include "Settings.h"
#include "Serialize.h"

#include "loggedout_dlg.h"
#include "resource.h"

//
// Location of the real MSGINA.
//

#define REALGINA_PATH      TEXT("MSGINA.DLL")
#define GINASTUB_VERSION   (WLX_VERSION_1_4)

//Hooked instance of MSGINA
HINSTANCE hMsginaDll;

//
// Winlogon function dispatch table.
//
PVOID g_pWinlogon = NULL;
static DWORD g_dwVersion = WLX_VERSION_1_4;

static MyGinaContext gAucunContext = {0};
MyGinaContext *pgAucunContext = &gAucunContext;

//
// Pointers to the real MSGINA functions.
//

static PFWLXNEGOTIATE            pfWlxNegotiate;
static PFWLXINITIALIZE           pfWlxInitialize;
static PFWLXDISPLAYSASNOTICE     pfWlxDisplaySASNotice;
static PFWLXLOGGEDOUTSAS         pfWlxLoggedOutSAS;
static PFWLXACTIVATEUSERSHELL    pfWlxActivateUserShell;
static PFWLXLOGGEDONSAS          pfWlxLoggedOnSAS;
static PFWLXDISPLAYLOCKEDNOTICE  pfWlxDisplayLockedNotice;
static PFWLXWKSTALOCKEDSAS       pfWlxWkstaLockedSAS;
static PFWLXISLOCKOK             pfWlxIsLockOk;
static PFWLXISLOGOFFOK           pfWlxIsLogoffOk;
static PFWLXLOGOFF               pfWlxLogoff;
static PFWLXSHUTDOWN             pfWlxShutdown;

//
// New for version 1.1
//
static PFWLXSTARTAPPLICATION     pfWlxStartApplication  = NULL;
static PFWLXSCREENSAVERNOTIFY    pfWlxScreenSaverNotify = NULL;

//
// New for version 1.2 - No new GINA interface was added, except
//                       a new function in the dispatch table.
//

//
// New for version 1.3
//
static PFWLXNETWORKPROVIDERLOAD   pfWlxNetworkProviderLoad  = NULL;
static PFWLXDISPLAYSTATUSMESSAGE  pfWlxDisplayStatusMessage = NULL;
static PFWLXGETSTATUSMESSAGE      pfWlxGetStatusMessage     = NULL;
static PFWLXREMOVESTATUSMESSAGE   pfWlxRemoveStatusMessage  = NULL;

//
// New for version 1.4
//
static PWLXGETCONSOLESWITCHCREDENTIALS pfWlxGetConsoleSwitchCredentials = NULL;
static PWLXRECONNECTNOTIFY pfWlxReconnectNotify  = NULL;
static PWLXDISCONNECTNOTIFY pfWlxDisconnectNotify = NULL;

PVOID GetHookedContext(PVOID pWlxContext)
{
    return ((MyGinaContext*)pWlxContext)->mHookedContext;
}

//
// Hook into the real MSGINA.
//
BOOL MyInitialize(HINSTANCE hMsginaDll, DWORD dwWlxVersion)
{
    //
    // Get pointers to all of the WLX functions in the real MSGINA.
    //
    pfWlxInitialize = (PFWLXINITIALIZE) GetProcAddress(hMsginaDll, "WlxInitialize");

    if (!pfWlxInitialize)
    {
        return FALSE;
    }

    pfWlxDisplaySASNotice = (PFWLXDISPLAYSASNOTICE) GetProcAddress(hMsginaDll, "WlxDisplaySASNotice");

    if (!pfWlxDisplaySASNotice)
    {
        return FALSE;
    }

    pfWlxLoggedOutSAS = (PFWLXLOGGEDOUTSAS) GetProcAddress(hMsginaDll, "WlxLoggedOutSAS");

    if (!pfWlxLoggedOutSAS)
    {
        return FALSE;
    }

    pfWlxActivateUserShell = (PFWLXACTIVATEUSERSHELL) GetProcAddress(hMsginaDll, "WlxActivateUserShell");

    if (!pfWlxActivateUserShell)
    {
        return FALSE;
    }

    pfWlxLoggedOnSAS = (PFWLXLOGGEDONSAS) GetProcAddress(hMsginaDll, "WlxLoggedOnSAS");

    if (!pfWlxLoggedOnSAS)
    {
        return FALSE;
    }

    pfWlxDisplayLockedNotice = (PFWLXDISPLAYLOCKEDNOTICE) GetProcAddress(hMsginaDll, "WlxDisplayLockedNotice");

    if (!pfWlxDisplayLockedNotice)
    {
        return FALSE;
    }

    pfWlxIsLockOk = (PFWLXISLOCKOK) GetProcAddress(hMsginaDll, "WlxIsLockOk");

    if (!pfWlxIsLockOk)
    {
        return FALSE;
    }

    pfWlxWkstaLockedSAS = (PFWLXWKSTALOCKEDSAS) GetProcAddress(hMsginaDll, "WlxWkstaLockedSAS");

    if (!pfWlxWkstaLockedSAS)
    {
        return FALSE;
    }

    pfWlxIsLogoffOk = (PFWLXISLOGOFFOK) GetProcAddress(hMsginaDll, "WlxIsLogoffOk");

    if (!pfWlxIsLogoffOk)
    {
        return FALSE;
    }

    pfWlxLogoff = (PFWLXLOGOFF) GetProcAddress(hMsginaDll, "WlxLogoff");

    if (!pfWlxLogoff)
    {
        return FALSE;
    }

    pfWlxShutdown = (PFWLXSHUTDOWN) GetProcAddress(hMsginaDll, "WlxShutdown");

    if (!pfWlxShutdown)
    {
        return FALSE;
    }

    //
    // Load functions for version 1.1 as necessary.
    //
    if (dwWlxVersion > WLX_VERSION_1_0)
    {
        pfWlxStartApplication = (PFWLXSTARTAPPLICATION) GetProcAddress(hMsginaDll, "WlxStartApplication");

        if (!pfWlxStartApplication)
        {
            return FALSE;
        }

        pfWlxScreenSaverNotify = (PFWLXSCREENSAVERNOTIFY) GetProcAddress(hMsginaDll, "WlxScreenSaverNotify");

        if (!pfWlxScreenSaverNotify)
        {
            return FALSE;
        }
    }

    //
    // Load functions for version 1.3 as necessary.
    //
    if (dwWlxVersion > WLX_VERSION_1_2)
    {
        pfWlxNetworkProviderLoad = (PFWLXNETWORKPROVIDERLOAD)GetProcAddress(hMsginaDll, "WlxNetworkProviderLoad");

        if (!pfWlxNetworkProviderLoad)
        {
            return FALSE;
        }

        pfWlxDisplayStatusMessage = (PFWLXDISPLAYSTATUSMESSAGE)GetProcAddress(hMsginaDll, "WlxDisplayStatusMessage");

        if (!pfWlxDisplayStatusMessage)
        {
            return FALSE;
        }

        pfWlxGetStatusMessage =(PFWLXGETSTATUSMESSAGE)GetProcAddress(hMsginaDll, "WlxGetStatusMessage");
        if (!pfWlxGetStatusMessage)
        {
            return FALSE;
        }

        pfWlxRemoveStatusMessage =
            (PFWLXREMOVESTATUSMESSAGE)
            GetProcAddress(hMsginaDll, "WlxRemoveStatusMessage");
        if (!pfWlxRemoveStatusMessage)
        {
            return FALSE;
        }
    }

    //
    // Load functions for version 1.3 as necessary.
    //
    if (dwWlxVersion > WLX_VERSION_1_3)
    {
        pfWlxGetConsoleSwitchCredentials = (PWLXGETCONSOLESWITCHCREDENTIALS) GetProcAddress(hMsginaDll, "WlxGetConsoleSwitchCredentials");
        if (!pfWlxGetConsoleSwitchCredentials) return FALSE;

        pfWlxReconnectNotify = (PWLXRECONNECTNOTIFY) GetProcAddress(hMsginaDll, "WlxReconnectNotify");
        if (!pfWlxReconnectNotify) return FALSE;

        pfWlxDisconnectNotify = (PWLXDISCONNECTNOTIFY) GetProcAddress(hMsginaDll, "WlxDisconnectNotify");
        if (!pfWlxDisconnectNotify) return FALSE;

    }

    //
    // Everything loaded OK.
    //
    return TRUE;
}


BOOL WINAPI WlxNegotiate(DWORD dwWinlogonVersion, DWORD *pdwDllVersion)
{
    DWORD dwWlxVersion = GINASTUB_VERSION;

    TRACE(eDEBUG, L"WlxNegotiate\n");
    //
    // Load MSGINA.DLL.
    //
    if (!(hMsginaDll = LoadLibrary(REALGINA_PATH)))
    {
        return FALSE;
    }

    //
    // Get pointers to WlxNegotiate function in the real MSGINA.
    //
    pfWlxNegotiate = (PFWLXNEGOTIATE) GetProcAddress(hMsginaDll, "WlxNegotiate");
    if (!pfWlxNegotiate)
    {
        return FALSE;
    }

    //
    // Handle older version of Winlogon.
    //
    if (dwWinlogonVersion < dwWlxVersion)
    {
        dwWlxVersion = dwWinlogonVersion;
    }

    //
    // Negotiate with MSGINA for version that we can support.
    //
    if (!pfWlxNegotiate(dwWlxVersion, &dwWlxVersion))
    {
        return FALSE;
    }

    //
    // Load the rest of the WLX functions from the real MSGINA.
    //
    if (!MyInitialize(hMsginaDll, dwWlxVersion))
    {
        return FALSE;
    }

    //
    // Inform Winlogon which version to use.
    //
    *pdwDllVersion = g_dwVersion = dwWlxVersion;

    return TRUE;
}


BOOL WINAPI WlxInitialize(LPWSTR lpWinsta, HANDLE hWlx, PVOID pvReserved, PVOID pWinlogonFunctions, PVOID * pWlxContext)
{
    BOOL result;

    TRACE(eDEBUG, L"WlxInitialize\n");
    //
    // Save pointer to dispatch table.
    //
    // Note that g_pWinlogon will need to be properly casted to the
    // appropriate version when used to call function in the dispatch
    // table.
    //
    // For example, assuming we are at WLX_VERSION_1_3, we would call
    // WlxSasNotify() as follows:
    //
    // ((PWLX_DISPATCH_VERSION_1_3) g_pWinlogon)->WlxSasNotify(hWlx, MY_SAS);
    //
    g_pWinlogon = pWinlogonFunctions;

    //
    // Now hook the WlxDialogBoxParam() dispatch function.
    //
    HookWlxDialogBoxParam(g_pWinlogon, g_dwVersion);

    *pWlxContext = &gAucunContext;
    gAucunContext.Winlogon = hWlx;
    result = pfWlxInitialize(lpWinsta, hWlx, pvReserved, pWinlogonFunctions, &gAucunContext.mHookedContext);

    if (result == TRUE)
    {
        gAucunContext.mLSA = 0; //safety
        RegisterLogonProcess(LOGON_PROCESS_NAME, &gAucunContext.mLSA);

		//The first winlogon process will set the password for the other processes
        SerializeEnter();

        if (!*gEncryptedRandomSelfservePassword)
		  {
				//TODO Encrypt password to memory
            GenerateRandomUnicodePassword(gEncryptedRandomSelfservePassword, gEncryptedRandomSelfservePassword_len);
				TRACE(eINFO, L"Random password is %s\n", gEncryptedRandomSelfservePassword);
        }

        SerializeLeave();

        {
            wchar_t username[255];
            GetSelfServeSetting(L"Username", username, sizeof username / sizeof *username);
            SetSelfservePassword(username);
        }
    }

    return result;
}

VOID WINAPI WlxDisplaySASNotice(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxDisplaySASNotice\n");

    //If we just finished a selfserve request (and pressed CTRL-ALT-DEL) we skip this notice.
    //If we did show the notice, the user would have to press CTRL-ALT-DEL a second time
    //It makes the mSelfServeLogon flag live a little longer than the actual self serve logon
    if (((MyGinaContext*)pWlxContext)->mSelfServeLogon)
    {
        ((MyGinaContext*)pWlxContext)->mSelfServeLogon = FALSE;
        //Inform Winlogon that CTRL-ALT_DELETE was pressed
        ((PWLX_DISPATCH_VERSION_1_0) g_pWinlogon)->WlxSasNotify(gAucunContext.Winlogon, WLX_SAS_TYPE_CTRL_ALT_DEL);
    }
    else
    {
        pfWlxDisplaySASNotice(GetHookedContext(pWlxContext));
    }
}


int WINAPI WlxLoggedOutSAS(PVOID pWlxContext, DWORD dwSasType, PLUID pAuthenticationId, PSID pLogonSid, PDWORD pdwOptions, PHANDLE phToken, PWLX_MPR_NOTIFY_INFO pMprNotifyInfo, PVOID * pProfile)
{
    int result;

    TRACE(eDEBUG, L"WlxLoggedOutSAS\n");

    result =  pfWlxLoggedOutSAS(GetHookedContext(pWlxContext), dwSasType, pAuthenticationId, pLogonSid, pdwOptions, phToken, pMprNotifyInfo, pProfile);

    if (result == WLX_SAS_ACTION_LOGON)
    {
        //A user might have gotten hold of the selfserve username's password.
        //Whatever the reason for logging in with the selfserve account, when
        //you do, you get the selfservice shell.
        wchar_t username[255];

        if (GetSelfServeSetting(L"Username", username, sizeof username / sizeof *username) == S_OK)
        {
            PSID selfservesid = 0;
            if (GetSIDFromUsername(username, &selfservesid))
            {
                PSID tokensid = 0;
                if (GetSIDFromToken(*phToken, &tokensid))
                {
                    //mSelfServeLogon will be true if user logging in with selfserve account
                    ((MyGinaContext*)pWlxContext)->mSelfServeLogon = EqualSid(selfservesid, tokensid);

                    HeapFree(GetProcessHeap(), 0, tokensid);
                }
                HeapFree(GetProcessHeap(), 0, selfservesid);
            }
        }

        ((MyGinaContext*)pWlxContext)->mCurrentUser = *phToken;
    }
    else
    {
        TRACE(eERROR, L"Logon failed or cancelled.\n");
    }

    return result;
}


BOOL WINAPI WlxActivateUserShell(PVOID pWlxContext, PWSTR pszDesktopName, PWSTR pszMprLogonScript, PVOID pEnvironment)
{
    BOOL result = FALSE;

    TRACE(eDEBUG, L"WlxActivateUserShell\n");
    if (((MyGinaContext*)pWlxContext)->mSelfServeLogon)
    {
        wchar_t shell[MAX_PATH];

        if (GetSelfServeSetting(L"Shell", shell, sizeof shell / sizeof *shell) == S_OK)
        {
            TRACE(eERROR, L"Switching to selfservice account\n");
            //TODO : Deny Administrator group in token (not sure it will work)
            //TODO : Allow for command line parameters (could be in the registry, with placemarks ?)
            result = CreateProcessAsUserOnDesktop(((MyGinaContext*)pWlxContext)->mCurrentUser, shell, pszDesktopName, pEnvironment);
        }

        if (*gUsername)
        {
            HKEY reg;

            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", 0, KEY_WRITE, &reg) == ERROR_SUCCESS)
            {
                RegSetValueEx(reg, L"DefaultUserName", 0, REG_SZ, (BYTE*)gUsername, sizeof(wchar_t)*(wcslen(gUsername)+1));
                RegCloseKey(reg);
                *gUsername = 0;
            }
        }
    }

    if (!result)
        result = pfWlxActivateUserShell(GetHookedContext(pWlxContext), pszDesktopName, pszMprLogonScript, pEnvironment);

    return result;
}


int WINAPI WlxLoggedOnSAS(PVOID pWlxContext, DWORD dwSasType, PVOID pReserved)
{
    int result;

    TRACE(eDEBUG, L"WlxLoggedOnSAS, type %d\n", dwSasType);

    //If you press CTRL-ALT-DEL in the selfservice shell, the user is logged off
    if (((MyGinaContext*)pWlxContext)->mSelfServeLogon)
    {
        TRACEMORE(eDEBUG, L" received in a selfservice context\n");

        result = WLX_SAS_ACTION_LOGOFF;
    }
    else
    {
        TRACEMORE(eDEBUG, L"\n");
        result = pfWlxLoggedOnSAS(GetHookedContext(pWlxContext), dwSasType, pReserved);
    }

    return result;
}


VOID WINAPI WlxDisplayLockedNotice(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxDisplayLockedNotice\n");
    pfWlxDisplayLockedNotice(GetHookedContext(pWlxContext));
}


BOOL WINAPI WlxIsLockOk(PVOID pWlxContext)
{
    BOOL result = TRUE;

    TRACE(eDEBUG, L"WlxIsLockOk\n");

    //Lock is not OK in self serve mode. Else let MSGINA decide
    if (((MyGinaContext*)pWlxContext)->mSelfServeLogon)
    {
        //Lock is not allowed, we just log off
        result = FALSE;
        ((PWLX_DISPATCH_VERSION_1_0) g_pWinlogon)->WlxSasNotify(gAucunContext.Winlogon, WLX_SAS_TYPE_CTRL_ALT_DEL);
        ((MyGinaContext*)pWlxContext)->mSelfServeLogon = FALSE;
    }
    else
    {
        result = pfWlxIsLockOk(GetHookedContext(pWlxContext));
    }
    return result;
}


int WINAPI WlxWkstaLockedSAS(PVOID pWlxContext, DWORD dwSasType)
{
    int result;

    TRACE(eDEBUG, L"WlxWkstaLockedSAS\n");

    result = pfWlxWkstaLockedSAS(GetHookedContext(pWlxContext), dwSasType);

    if (result == WLX_SAS_ACTION_LOGOFF)
    {
        TRACE(eERROR, L"Proceding with a force logoff (comming from AUCUN).\n");

        result = WLX_SAS_ACTION_FORCE_LOGOFF;
    }

    return result;
}


BOOL WINAPI WlxIsLogoffOk(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxIsLogoffOk\n");
    return pfWlxIsLogoffOk(GetHookedContext(pWlxContext));
}


VOID WINAPI WlxLogoff(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxLogoff\n");
    pfWlxLogoff(GetHookedContext(pWlxContext));

    ((MyGinaContext*)pWlxContext)->mCurrentUser = 0;
}


VOID WINAPI WlxShutdown(PVOID pWlxContext, DWORD ShutdownType)
{
    TRACE(eDEBUG, L"WlxShutdown\n");
    pfWlxShutdown(GetHookedContext(pWlxContext), ShutdownType);
    LsaDeregisterLogonProcess(((MyGinaContext*)pWlxContext)->mLSA);
    FreeLibrary(hMsginaDll);
}


//
// New for version 1.1
//
BOOL WINAPI WlxScreenSaverNotify(PVOID  pWlxContext, BOOL * pSecure)
{
    TRACE(eDEBUG, L"WlxScreenSaverNotify\n");
    return pfWlxScreenSaverNotify(GetHookedContext(pWlxContext), pSecure);
}

BOOL WINAPI WlxStartApplication(PVOID pWlxContext, PWSTR pszDesktopName, PVOID pEnvironment, PWSTR pszCmdLine)
{
    TRACE(eDEBUG, L"WlxStartApplication\n");
    return pfWlxStartApplication(GetHookedContext(pWlxContext), pszDesktopName, pEnvironment, pszCmdLine);
}


//
// New for version 1.3
//

BOOL WINAPI WlxNetworkProviderLoad(PVOID pWlxContext, PWLX_MPR_NOTIFY_INFO pNprNotifyInfo)
{
    TRACE(eDEBUG, L"WlxNetworkProviderLoad\n");
    return pfWlxNetworkProviderLoad(GetHookedContext(pWlxContext), pNprNotifyInfo);
}


BOOL WINAPI WlxDisplayStatusMessage(PVOID pWlxContext, HDESK hDesktop, DWORD dwOptions, PWSTR pTitle, PWSTR pMessage)
{
    TRACE(eDEBUG, L"WlxDisplayStatusMessage\n");
    return pfWlxDisplayStatusMessage(GetHookedContext(pWlxContext), hDesktop, dwOptions, pTitle, pMessage);
}


BOOL WINAPI WlxGetStatusMessage(PVOID pWlxContext, DWORD * pdwOptions, PWSTR pMessage, DWORD dwBufferSize)
{
    TRACE(eDEBUG, L"WlxGetStatusMessage\n");
    return pfWlxGetStatusMessage(GetHookedContext(pWlxContext), pdwOptions, pMessage, dwBufferSize);
}


BOOL WINAPI WlxRemoveStatusMessage(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxRemoveStatusMessage\n");
    return pfWlxRemoveStatusMessage(GetHookedContext(pWlxContext));
}


//
// New for 1.4
//
BOOL WINAPI WlxGetConsoleSwitchCredentials(PVOID pWlxContext, PVOID pCredInfo)
{
    TRACE(eDEBUG, L"WlxGetConsoleSwitchCredentials\n");
    return pfWlxGetConsoleSwitchCredentials(GetHookedContext(pWlxContext), pCredInfo);
}

VOID WINAPI WlxReconnectNotify(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxReconnectNotify\n");
    pfWlxReconnectNotify(GetHookedContext(pWlxContext));
}

VOID WINAPI WlxDisconnectNotify(PVOID pWlxContext)
{
    TRACE(eDEBUG, L"WlxDisconnectNotify\n");
    pfWlxDisconnectNotify(GetHookedContext(pWlxContext));
}