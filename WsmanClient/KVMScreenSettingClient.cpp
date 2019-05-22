/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2009-2019 Intel Corporation
 */
/*++

@file: KVMScreenSettingClient.cpp

--*/
//#define _MOCK //- for debugging

#include "KVMScreenSettingClient.h"
#include "UNSDebug.h"
#include "WsmanClientCatch.h"



using namespace std;


using namespace Intel::Manageability::Cim::Typed;

const string KVMScreenSettingClient::DEFAULT_USER = "$$uns";
const string KVMScreenSettingClient::DEFAULT_PASS = "$$uns";

KVMScreenSettingClient::KVMScreenSettingClient() : BaseWSManClient( DEFAULT_USER, DEFAULT_PASS)
{
	m_isInit = false;
	
}

KVMScreenSettingClient::KVMScreenSettingClient(const std::string &User, const std::string &Password) : BaseWSManClient( User, Password)
{
	m_isInit = false;
	
}

KVMScreenSettingClient::~KVMScreenSettingClient()
{
}

bool KVMScreenSettingClient::updateScreenSettings(const ExtendedDisplayParameters &displaySettings, short numOfDisplays)
{
	try 
	{
		if (!Init())
			return false;

		std::lock_guard<std::mutex> lock(WsManSemaphore()); 		//Lock WsMan to prevent reentry
		short i = 0;
		
		vector<bool> isActive(numOfDisplays);
		vector<int> positionX(numOfDisplays);
		vector<int> positionY(numOfDisplays);
		vector<unsigned int> resolutionX(numOfDisplays);
		vector<unsigned int> resolutionY(numOfDisplays);
	
		for (i = 0 ; i < numOfDisplays ; i++)
		{
			DbgPrint("adding %d, isActive:%d,UpperLeftX:%d,UpperLeftY:%d,ResolutionX:%d,ResolutionY:%d, pipe:%d \n",
				i,displaySettings.screenSettings[i].isActive,displaySettings.screenSettings[i].UpperLeftX,
				displaySettings.screenSettings[i].UpperLeftY,displaySettings.screenSettings[i].ResolutionX,
				displaySettings.screenSettings[i].ResolutionY,displaySettings.screenSettings[i].Pipe);
			isActive[i]=displaySettings.screenSettings[i].isActive;
			positionX[i]=displaySettings.screenSettings[i].UpperLeftX;
			positionY[i]=displaySettings.screenSettings[i].UpperLeftY;
			resolutionX[i]=displaySettings.screenSettings[i].ResolutionX;
			resolutionY[i]=displaySettings.screenSettings[i].ResolutionY;

			if (i == 0)
				m_service.PrimaryIndex(displaySettings.screenSettings[i].Pipe);
			if (i == 1)
				m_service.SecondaryIndex(displaySettings.screenSettings[i].Pipe);
			if (i == 2)
				m_service.TertiaryIndex(displaySettings.screenSettings[i].Pipe);
			if (i == 3)
				m_service.QuadraryIndex(displaySettings.screenSettings[i].Pipe);
		}	
		m_service.IsActive(isActive);
		m_service.UpperLeftX(positionX);
		m_service.UpperLeftY(positionY);		
		m_service.ResolutionX(resolutionX);
		m_service.ResolutionY(resolutionY);
		m_service.Put();
		//KVMScreenSettingClientException e("no primary display");	
	}
	CATCH_exception_return("KVMScreenSettingClient::updateScreenSettings")
	return true;
}

bool  KVMScreenSettingClient::getScreenSettings (ExtendedDisplayParameters &displaySettings, unsigned char & primary, short numOfDisplays)
{
	try 
	{
		if (!Init())
			return false;

		primary = m_service.PrimaryIndex();
		const vector<bool> isActive = m_service.IsActive();
		const vector<int> UpperLeftX = m_service.UpperLeftX();
		const vector<int> UpperLeftY = m_service.UpperLeftY();
		const vector<unsigned int> ResolutionX = m_service.ResolutionX();
		const vector<unsigned int> ResolutionY = m_service.ResolutionY();

		for (short i=0; i<numOfDisplays; i++)
		{
			displaySettings.screenSettings[i].isActive = isActive[i];
			displaySettings.screenSettings[i].ResolutionX = ResolutionX[i];
			displaySettings.screenSettings[i].ResolutionY = ResolutionY[i];
			displaySettings.screenSettings[i].UpperLeftX = UpperLeftX[i];
			displaySettings.screenSettings[i].UpperLeftY= UpperLeftY[i];
		}
		return true;
	}
	CATCH_exception_return("KVMScreenSettingClient::getScreenSettings")
}

bool KVMScreenSettingClient::Init(bool forceGet)
{
	DbgPrint("\nKVMScreenSettingClient::Init\n");
	if (!forceGet && m_isInit) return true;
	m_isInit = false;
	

	try 
	{
		if (!m_endpoint)
			SetEndpoint(false);
		//Lock WsMan to prevent reentry
		std::lock_guard<std::mutex> lock(WsManSemaphore());
		m_service.WsmanClient(m_client.get());
		m_service.Get();
		m_isInit = true;
		DbgPrint("\nKVMScreenSettingClient::Initialized\n");
	}
	CATCH_exception("KVMScreenSettingClient::Init")
	return m_isInit;	
}