/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2009-2019 Intel Corporation
 */
/*++

@file: AMTFCFHWSmanClient.h

--*/

#ifndef  _AMTFCFHWSmanClient_H
#define  _AMTFCFHWSmanClient_H

#include "BaseWSManClient.h"
#include <string>
#include "AMT_SNMPEventSubscriber.h"

class WSMAN_DLL_API AMTFCFHWSmanClient : public BaseWSManClient
{
public:
	AMTFCFHWSmanClient();
	AMTFCFHWSmanClient(const std::string &User, const std::string &Password);
	virtual ~AMTFCFHWSmanClient();

	/*Actual soap actions!*/

	bool userInitiatedPolicyRuleExists(short* pExist);
	bool userInitiatedPolicyRuleForLocalMpsExists(short* pExist);
	
	bool snmpEventSubscriberExists(short* pExist);
	bool CILAFilterCollectionSubscriptionExists(short* pExist);
	bool Init();
private:
	bool CILAFilterCollectionSubscriptionExists(short* pExist,std::string filterType);

	bool m_isInit;
	static const std::string DEFAULT_USER;
	static const std::string DEFAULT_PASS;

	Intel::Manageability::Cim::Typed::AMT_SNMPEventSubscriber m_service;
};

#endif //AMTFCFHWSmanClient_H