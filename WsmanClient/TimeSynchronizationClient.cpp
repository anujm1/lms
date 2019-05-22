/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2011-2019 Intel Corporation
 */
/*++

@file: TimeSynchronizationClient.cpp

--*/

#include "TimeSynchronizationClient.h"
#include "UNSDebug.h"
#include "WsmanClientCatch.h"

using namespace Intel::Manageability::Cim::Typed;
using namespace std;

const std::string TimeSynchronizationClient::DEFAULT_USER = "$$uns";
const std::string TimeSynchronizationClient::DEFAULT_PASS = "$$uns";

TimeSynchronizationClient::TimeSynchronizationClient() : BaseWSManClient(DEFAULT_USER, DEFAULT_PASS), m_TimeSyncState(DEFAULT_TRUE)
{
	m_isInit = false;
}

TimeSynchronizationClient::TimeSynchronizationClient(const std::string &User, const std::string &Password) : BaseWSManClient(User, Password), m_TimeSyncState(DEFAULT_TRUE)
{
	m_isInit = false;	
}


TimeSynchronizationClient::~TimeSynchronizationClient()
{
	m_isInit = false;
}

bool TimeSynchronizationClient::Init()
{
	if (m_isInit) return true;
	try 
	{
		if (!m_endpoint)
			SetEndpoint(false);
		//Lock WsMan to prevent reentry
		std::lock_guard<std::mutex> lock(WsManSemaphore());
		m_service.WsmanClient(m_client.get());
		m_service.Get();
		m_isInit = true;
	}
	CATCH_exception("TimeSynchronizationClient::Init")
	return m_isInit;
}

bool TimeSynchronizationClient::UpdateTimeSyncState()
{
	bool ret = false;
	m_TimeSyncState = TIMESYNC_DISABLED; //These is FALSE value if the LocalTimeSyncEnabled FW property.
	try{
		if (m_service.LocalTimeSyncEnabledExists())
		{
			unsigned int state = m_service.LocalTimeSyncEnabled();
			if (state == TIMESYNC_DISABLED || state == CONFIGURED_TRUE || state == DEFAULT_TRUE)
			{
				m_TimeSyncState = (ENABLED_STATE)state;
				ret = true;
			}
		}
	}
	CATCH_exception("TimeSynchronizationClient::UpdateTimeSyncState")
	return ret;
}

bool TimeSynchronizationClient::GetLocalTimeSyncEnabledState(bool & state)
{
	if (!Init())
	{
		DbgPrint("GetLocalTimeSyncEnabledState Init return false !\n");
		return false;
	}
	//recheck the update state (in case it was changed since the last init.
	if (!UpdateTimeSyncState())
	{
		DbgPrint("Error: Failed to Get the time sync state in in TimeSynchronizationClient::GetLocalTimeSyncEnabledState\n");
		return false;
	}

	state = ((m_TimeSyncState == CONFIGURED_TRUE || m_TimeSyncState == DEFAULT_TRUE) ? true : false);

	return true;
}


bool TimeSynchronizationClient::GetAMTTime(unsigned int & time)
{	
	if (!Init())
	{
		DbgPrint("GetAMTTime Init return false !\n");
		return false;
	}

	try{
	int ret = m_service.GetLowAccuracyTimeSynch(m_time);
	if (ret != 0)
	{
		DbgPrint("Error: Failed while calling GetLowAccuracyTimeSynch\n");
		return false;
	}
	if (!m_time.Ta0Exists())
	{
		DbgPrint("Error: Time object doesn't exist\n");
		return false;
	}
	time = m_time.Ta0();
	}
	CATCH_exception_return("TimeSynchronizationClient::GetAMTTime")
	return true;
}

bool TimeSynchronizationClient::SetAMTTime(unsigned int time)
{
	if (!Init())
		return false;
	
	AMT_TimeSynchronizationService::SetHighAccuracyTimeSynch_INPUT input;
	unsigned int currTime = 0;
	if (!GetAMTTime(currTime))
	{
		return false;
	}
	input.Ta0(currTime);
	input.Tm1(time);
	input.Tm2(time);
	try{
		int ret = m_service.SetHighAccuracyTimeSynch(input);
		if (ret != 0)
		{
			DbgPrint("Error: failed while calling SetHighAccuracyTimeSynch\n");
			return false;
		}
	}
	CATCH_exception_return("TimeSynchronizationClient::SetAMTTime")
	return true;
}

