/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2009-2015 Intel Corporation
 */
/*++

@file: AT_Device.cpp

--*/

// AT_Device.cpp : Implementation of CAT_Device

#include "stdafx.h"
#include "atlsafe.h"
#include "AT_Device.h"
#include "AT_Device_BE.h"
#include "UNSRegistry.h"
#include "DataStorageGenerator.h"
// CAT_Device


STDMETHODIMP CAT_Device::GetATDeviceInfo(SHORT* pState, BSTR* bstrInfo)
{
#ifdef _DEBUG
	if (GetFromRegistry(L"DebugData", L"ATState", pState) == true)
	{
		return S_OK;
	}
#endif

	if (CheckCredentials(GetATDeviceInfo_F) != S_OK)
		return E_ACCESSDENIED;

	Intel::LMS::LMS_ERROR err = Intel::LMS::AT_Device_BE(GetGmsPortForwardingStarted()).GetATDeviceInfo(*pState);
	if (err == Intel::LMS::ERROR_NOT_AVAILABLE_NOW)
		return E_NOT_VALID_STATE; 
	if (err != Intel::LMS::ERROR_OK)
		return E_FAIL;

	char ValueStr[512];
	memset(ValueStr, 0, 512);
	CComBSTR bstr(ValueStr);
	*bstrInfo = bstr.Detach();
	return S_OK;
}

STDMETHODIMP CAT_Device::GetAuditLogs(BSTR* bstrAuditLogs)
{
#ifdef _DEBUG
	//return S_OK;
#endif	
	if (CheckCredentials(GetAuditLogs_F) != S_OK)
		return E_ACCESSDENIED;

	std::string AuditLogs;
	
	Intel::LMS::LMS_ERROR err = Intel::LMS::AT_Device_BE(GetGmsPortForwardingStarted()).GetAuditLogs(AuditLogs);
	if (err == Intel::LMS::ERROR_NOT_AVAILABLE_NOW)
		return E_NOT_VALID_STATE;
	if (err != Intel::LMS::ERROR_OK)
		return E_FAIL;
	CComBSTR bstr(AuditLogs.c_str());
	*bstrAuditLogs = bstr.Detach();
	return S_OK;
}