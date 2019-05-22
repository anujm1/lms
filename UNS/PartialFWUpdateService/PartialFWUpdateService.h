/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2010-2019 Intel Corporation
 */
#ifndef __PARTIALFWUPDATESERVICE_H_
#define __PARTIALFWUPDATESERVICE_H_

#include "ace/Acceptor.h"
#include "ace/INET_Addr.h"
#include "ace/Containers.h"
#include "ace/Map_Manager.h"
#include "ace/Synch.h"
#include "ace/Reactor_Notification_Strategy.h"
#include "ace/Message_Block.h"
#include "MessageBlockPtr.h"
#include "ace/Log_Msg.h"
#include "GmsSubService.h"
#include "EventHandler.h"

#include <memory>
#include <windows.h>
#include <string>


#include "PFWUpdateDllWrapper.h"

#include "PARTIALFWUPDATESERVICE_export.h"

#define DEFAULT_LANG_ID				100


typedef	enum	_LANGUAGE_FLOW_MODE 
{
	INITIAL_MODE,
	MANUAL_MODE
} LANGUAGE_FLOW_MODE;




bool SetExpectedWithLocalOSLanguage();
unsigned int getWindowsLanguageID(const UINT16 languageID);
unsigned int getUCLanguageID();

class PartialFWUpdateEventsFilter;

class PARTIALFWUPDATESERVICE_Export PartialFWUpdateService:  public EventHandler
{
  public:
	// ********************* ACE PART ********************************
	PartialFWUpdateService();

    virtual int init (int argc, ACE_TCHAR *argv[]);

	virtual int resume ();

	virtual const ACE_TString name();
	// ******************************************************************

    virtual int handle_event (MessageBlockPtr mbPtr);

    
	virtual std::shared_ptr<EventsFilter> getFilter();
	
	// Help functions
	static bool getAllowFlashUpdate();		
	static bool isMESKU();	

	int langID;
	int mode;
	bool partialFWUpdate(int _langID = DEFAULT_LANG_ID, int _mode = INITIAL_MODE, bool _toPublishFailure = false);

private:
	std::unique_ptr<PFWUpdateDllWrapper> pfwuWrapper;

	typedef enum _PARTIAL_FWU_MODULE
	{
		LANGUAGE_MODULE,
		WLAN_MODULE,
		UNKNOWN_FWU_MODULE
	} PARTIAL_FWU_MODULE;
    bool schedPFUAfterResume_;

    int handlePublishEvent(const GMS_AlertIndication & alert);

    
    std::shared_ptr<PartialFWUpdateEventsFilter> filter_;
	ACE_Time_Value initTimestamp;
	bool m_PfuRequiredButNoPfw;

	void startPFWUpMessage();
	bool LoadFwUpdateLibDll();

	// ******************************************************************	
	bool getPartialFWUpdateImagePath(std::wstring& value);
	bool getImageFileNameByFwVersion(std::wstring& fileName);
	// ******************************************************************
	//Events handling
	void publishPartialFWUpgrade_begin(PARTIAL_FWU_MODULE module);
	void publishPartialFWUpgrade_end(PARTIAL_FWU_MODULE module,int retvalue);
	void publishPartialFWUpgrade_failed(PARTIAL_FWU_MODULE module,const std::wstring& defaultValue, int error);
	void publishMissingImageFile(PARTIAL_FWU_MODULE module);	
	bool checkImageFileExist(std::wstring &imagePath);
	bool updateLanguageChangeCode(UINT32 languageID, LANGUAGE_FLOW_MODE mode = MANUAL_MODE);
	bool invokePartialFWUpdateFlow(PARTIAL_FWU_MODULE module, UINT32 partialID);

};

#endif /* __PARTIALFWUPDATESERVICE_H_ */