/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2009-2018 Intel Corporation
 */
/*++

@file: FWUpdate_Commands.cpp

--*/

#include "FWUpdate_Commands.h"
#include "UNSDebug.h"
#include "GetFWUpdateStateCommand.h"

UINT32 
FWUpdate_Commands::GetFWUpdateVersion(Intel::MEI_Client::MKHI_Client::GET_FW_VER_RESPONSE& Version)
{
	using namespace Intel::MEI_Client;
	unsigned int rc = AMT_STATUS_INTERNAL_ERROR;
	try {
		MKHI_Client::GetFWVersionCommand command;
		Version = command.getResponse();
		rc = 0;
	}
#ifdef _DEBUGPRINT 
	catch (MEIClientException& e)
#else
	catch (MEIClientException&)
#endif
	{
		DbgPrint("GetFWVersionCommand failed %s\n",e.what());
	}


#ifdef _DEBUGPRINT 
	catch (std::exception& e)
#else
	catch(std::exception&)
#endif
	{
		DbgPrint("\nException in FWUGetVersionCommand %s\n", e.what());
	}
	return rc;
}

UINT32 
FWUpdate_Commands::GetFWCapabilities(Intel::MEI_Client::MKHI_Client::MEFWCAPS_SKU_MKHI& capabilities)
{
	using namespace Intel::MEI_Client;
	unsigned int rc = AMT_STATUS_INTERNAL_ERROR;
	try {
		MKHI_Client::GetFWCapsCommand command(MKHI_Client::FEATURES_CAPABLE);
		capabilities = command.getResponse();
		rc = 0;
	}
#ifdef _DEBUGPRINT 
	catch (MEIClientException& e)
#else
	catch (MEIClientException&)
#endif
	{
		DbgPrint("GetFWCapsCommand failed %s\n",e.what());
	}

#ifdef _DEBUGPRINT 
	catch (std::exception& e)
#else
	catch (std::exception&)
#endif	
	{
		DbgPrint("\nException in GetFWCapsCommand %s\n", e.what());
	}
return rc;
}

UINT32 
FWUpdate_Commands::GetFWFeaturesState(Intel::MEI_Client::MKHI_Client::MEFWCAPS_SKU_MKHI& features)
{
	using namespace Intel::MEI_Client;
	unsigned int rc = AMT_STATUS_INTERNAL_ERROR;
	try {
		MKHI_Client::GetFWCapsCommand command(MKHI_Client::FEATURES_ENABLED);
		features = command.getResponse();
		rc = 0;
	}
#ifdef _DEBUGPRINT 
	catch (MEIClientException& e)
#else
	catch (MEIClientException&)
#endif
	{
		DbgPrint("GetFWCapsCommand failed %s\n",e.what());
	}
	
#ifdef _DEBUGPRINT 
	catch (std::exception& e)
#else
	catch (std::exception&)
#endif
	{
		DbgPrint("\nException in GetFWCapsCommand %s\n", e.what());
	}
	return rc;
}

UINT32 
FWUpdate_Commands::GetFWPlatformType(Intel::MEI_Client::MKHI_Client::MKHI_PLATFORM_TYPE& platform)
{
	using namespace Intel::MEI_Client;
	unsigned int rc = AMT_STATUS_INTERNAL_ERROR;
	try {
		MKHI_Client::GetPlatformTypeCommand command;
		platform = command.getResponse();
		rc = 0;
	}
#ifdef _DEBUGPRINT 
	catch (MEIClientException& e)
#else
	catch (MEIClientException&)
#endif
	{
	DbgPrint("GetPlatformTypeCommand failed %s\n",e.what());
	}
#ifdef _DEBUGPRINT 
	catch (std::exception& e)
#else 
	catch (std::exception&)
#endif
	{
	DbgPrint("\nException in GetPlatformTypeCommand %s\n", e.what());
	}
	return rc;
}

unsigned int FWUpdate_Commands::GetFWUpdateStateCommand(bool* enabled)
{
	unsigned int rc = AMT_STATUS_INTERNAL_ERROR;
	try {
		Intel::MEI_Client::MKHI_Client::GetFWUpdateStateCommand command;
		Intel::MEI_Client::MKHI_Client::FW_UPDATE_STATE response = command.getResponse();
		*enabled = (response.Data == Intel::MEI_Client::MKHI_Client::FW_UPDATE_ENABLED) ||
				   (response.Data == Intel::MEI_Client::MKHI_Client::FW_UPDATE_PASSWORD_PROTECTED);
		rc = 0;
	}
#ifdef _DEBUGPRINT
	catch (Intel::MEI_Client::MEIClientException& e)
#else
	catch (Intel::MEI_Client::MEIClientException&)
#endif
	{
		DbgPrint("GetFWUpdateStateCommand failed %s\n", e.what());
	}

	catch (Intel::MEI_Client::MKHI_Client::MKHIErrorException& e)
	{
		DbgPrint("GetFWUpdateStateCommand failed ret=%d\n", e.getErr());
		rc = e.getErr();
	}
#ifdef _DEBUGPRINT
	catch (std::exception& e)
#else
	catch (std::exception&)
#endif
	{
		DbgPrint("\nException in GetFWUpdateStateCommand %s\n", e.what());
	}
	return rc;
}