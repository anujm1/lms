/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2010-2019 Intel Corporation
 */
#include "PartialFWUpdateService.h"

#include <ace/OS_NS_stdio.h>
#include <ace/OS_NS_string.h>
#include <ace/OS_NS_dirent.h>
#include <ace/Dirent_Selector.h>
#include "EventManagment.h"
#include "Tools.h"

#include "UNSEventsDefinition.h"
#include "DataStorageGenerator.h"
#include "SIOWSManClient.h"

#include "PFWUpdateDllWrapperFactory.h"



#include "MKHIErrorException.h"
#include "GetFWVersionCommand.h"
#include "GetPlatformTypeCommand.h"
#include "GetImageTypeCommand.h"
#include "FWUpdateCommand.h"
#include <FuncEntryExit.h>

#include <iostream>
#include <memory>

const ACE_TString PARTIAL_FW_UPDATE_BEGIN_MESSAGE_INIT(ACE_TEXT("Partial FW update begin - "));
//#define PARTIAL_FW_UPDATE_PROGRESS_INIT			L"Partial FW update progress - "
#define PARTIAL_FW_UPDATE_END_INIT				L"Partial FW update "
//#define ME_In_RECOVERY_MODE_MSG					L"Intel� ME is in full recovery mode"
const ACE_TString MISSING_IMAGE_FILE_MSG(ACE_TEXT("Partial FW update failed due missing image file"));

class PartialFWUpdateEventsFilter: public EventsFilter
{
public:
	PartialFWUpdateEventsFilter(unsigned int maxEventID = MAX_EVENT_NUM);
	~PartialFWUpdateEventsFilter();
	virtual bool toSubscribe(const GMS_AlertIndication *alert) const;
	bool addEvent(unsigned int eventID);
	static bool defaultInitialization(std::shared_ptr<PartialFWUpdateEventsFilter> & filter);
private:
	ACE_Array<unsigned int> eventsIDList_;
	unsigned int arrayMaxElement_;
};


PartialFWUpdateEventsFilter::PartialFWUpdateEventsFilter(unsigned int maxEventID):eventsIDList_(maxEventID+1, 0, NULL), arrayMaxElement_(maxEventID)
{}

PartialFWUpdateEventsFilter::~PartialFWUpdateEventsFilter() {}

bool PartialFWUpdateEventsFilter::addEvent(unsigned int eventID)
{
	if (eventID > arrayMaxElement_)
		return false;
	eventsIDList_[eventID] = 1;
	return true;
}

bool PartialFWUpdateEventsFilter::toSubscribe(const GMS_AlertIndication *alert) const
{
	if (alert->id <= arrayMaxElement_)
	{
		if (eventsIDList_[alert->id] == 1)
		{
			return true;
		}
	}
	return false;
}

bool PartialFWUpdateEventsFilter::defaultInitialization(std::shared_ptr<PartialFWUpdateEventsFilter> & filter)
{
	if (
		filter->addEvent(EVENT_PORT_FORWARDING_SERVICE_AVAILABLE)
		) //if
		return true;
	UNS_DEBUG(L"filter initialization failed", L"\n");
	return false;
}



extern "C" FILE* gLogFileHandle = NULL;


void FlowLog(const wchar_t * pref, const wchar_t * func) 
{
	std::wstringstream ss;
	ss << pref << func;
	auto l = ss.str();
	UNS_DEBUG(L"%W", L"\n", l.c_str());
}

void FuncEntry(const wchar_t * func) 
{
	FlowLog(L"PFU : --> ", func);
}

void FuncExit(const wchar_t * func) 
{
	FlowLog(L"PFU : <-- ", func);
}

void FuncExitWithStatus(const wchar_t * func, uint64_t status) 
{
	std::wstringstream ss;
	ss << L"PFU : <-- " << func << L" Status: " << status;
	auto l = ss.str();
	UNS_DEBUG(L"%W", L"\n", l.c_str());
}




PartialFWUpdateService::PartialFWUpdateService() : 
	filter_(std::make_shared<PartialFWUpdateEventsFilter>()), langID(PRIMARYLANGID(GetSystemDefaultLCID())), mode(INITIAL_MODE), schedPFUAfterResume_(false)
{
	PartialFWUpdateEventsFilter::defaultInitialization(filter_);

}

std::shared_ptr<EventsFilter> PartialFWUpdateService::getFilter()
{
	return filter_;
}


int PartialFWUpdateService::init (int argc, ACE_TCHAR *argv[])
{
	int retVal = EventHandler::init(argc, argv);
	if (retVal != 0)
	{
		UNS_DEBUG(L"EventHandler::init failed. retVal: %d", L"\n", retVal);
		return retVal;
	}

	initSubService(argc,argv);
	m_PfuRequiredButNoPfw = false;

	startSubService();

	//send message to the service to start it self after init - we want to start after the handlers have started
	startPFWUpMessage();
	initTimestamp = ACE_OS::gettimeofday();
	return 0;
}

const ACE_TString PartialFWUpdateService::name()
{
	return GMS_PARTIALFWUPDATESERVICE;
}


int PartialFWUpdateService::resume() 
{
	GmsSubService::resume();
	UNS_DEBUG(L"PartialFWUpdateService::resume(). Service resumed, PFWUp flow will be scheduled after PFWS is up, to see if HW changed during hiberboot.",L"\n");
	schedPFUAfterResume_ = true;
	return 0;
}

//Starting PFWUp flow by sending a message to self (in order to verify the order of actions).
void PartialFWUpdateService::startPFWUpMessage()
{
	MessageBlockPtr mbPtr(new ACE_Message_Block(), deleteMessageBlockPtr);
	mbPtr->data_block(new StartPFWUP());
	mbPtr->msg_type(MB_PFWU_START_EVENT);
	mbPtr->msg_priority(3);//higher than normal messages
	this->putq(mbPtr->duplicate()); 
}

ACE_FACTORY_DEFINE (PARTIALFWUPDATESERVICE , PartialFWUpdateService)

/* Business logic */
int PartialFWUpdateService::handle_event (MessageBlockPtr mbPtr )
{	
	int type=mbPtr->msg_type();
	GMS_AlertIndication * pGMS_AlertIndication = nullptr;
	StartPFWUP * pStartPFWUP = nullptr;
	FuncEntryExit<decltype(type)> fee(L"handle_event", type);
	switch (type)
	{
	case MB_PFWU_EVENT:
		UNS_DEBUG(L"PartialFWUpdateService: MB_PFWU_EVENT", L"\n");
		if (!getAllowFlashUpdate())
		{
			UNS_DEBUG(L"Feature disabled in registry", L"\n");
			publishPartialFWUpgrade_failed(LANGUAGE_MODULE, L"- feature disabled in registry", 8719);
			return 1;
		}

		pStartPFWUP = dynamic_cast<StartPFWUP*>(mbPtr->data_block());
		if (pStartPFWUP != nullptr)
		{
			return partialFWUpdate(pStartPFWUP->value, MANUAL_MODE, true);
		}
		else
		{
			ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("Invalid data block.\n")), -1);
		}
		break;
	case MB_PFWU_START_EVENT:
		UNS_DEBUG(L"PartialFWUpdateService: MB_PFWU_START_EVENT", L"\n");
		if (getAllowFlashUpdate())
		{
			partialFWUpdate();
		}
		else
		{
			UNS_DEBUG(L"Feature disabled in registry", L"\n");
		}
		return 1;
		break;
	case MB_PUBLISH_EVENT:
		pGMS_AlertIndication = dynamic_cast<GMS_AlertIndication*>(mbPtr->data_block());
		if (pGMS_AlertIndication != nullptr)
		{
			return handlePublishEvent(*pGMS_AlertIndication);
		}
		else
		{
			ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("Invalid data block.\n")), -1);
		}
	}

	ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("Invalid Message.\n")), -1);
}




int PartialFWUpdateService::handlePublishEvent(const GMS_AlertIndication & alert)
{

	switch (alert.category)
	{

	case CATEGORY_UNS:
		switch (alert.id)
		{
		case EVENT_PORT_FORWARDING_SERVICE_AVAILABLE:
		{
			UNS_DEBUG(L"%s got EVENT_PORT_FORWARDING_SERVICE_AVAILABLE. schedPFUAfterResume_: %d, m_PfuRequiredButNoPfw: %d", L"\n", name().c_str(), schedPFUAfterResume_, m_PfuRequiredButNoPfw);

			if (schedPFUAfterResume_)
			{
				schedPFUAfterResume_ = false;

				//Starting PFWUp flow. There is no need to do this in sleep and in hibernate, but there is need 
				// in hiberboot (Win 8 and up).
				//In hibernate the PFWU is done in the init, and here it will be duplicate. So if more than 5 seconds passed from the init, there may be hiberboot, and do PFWU. 
				// Otherwise, we are right after the init and do nothing
				ACE_Time_Value now = ACE_OS::gettimeofday();
				if ((now.sec() - initTimestamp.sec()) > 5)
					startPFWUpMessage();
			}

			if (m_PfuRequiredButNoPfw)
			{
				m_PfuRequiredButNoPfw = false;
				startPFWUpMessage();
			}

			return 1;
			break;
		}
		default:
			ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("Invalid Message id.\n")), -1);
			break;

		}
		break;

	default:
		ACE_ERROR_RETURN((LM_ERROR, ACE_TEXT("Invalid Message category.\n")), -1);
		break;
	}
}

// Registry handlers
bool getBoolRegistryKey(DATA_NAME storageName , bool defaultValue)
{

	DWORD value;
	if (!(DSinstance().GetDataValue(storageName, value, false)))
	{
		return defaultValue;
	}
	if (value == 0)
	{
		return false;
	}
	else if (value == 1)
	{
		return true;
	}
	else //default behaviour -  true
		return defaultValue;
}

bool PartialFWUpdateService::getAllowFlashUpdate()
{
	return getBoolRegistryKey(ALLOW_FLASH_UPDATE, true);
}

bool PartialFWUpdateService::getPartialFWUpdateImagePath(std::wstring& value)
{	
	return DSinstance().GetDataValue(PARTIAL_FWU_IMAGE_PATH, value, false);
}

// Publish events
void PartialFWUpdateService::publishPartialFWUpgrade_begin(PARTIAL_FWU_MODULE module)
{

	FuncEntryExit<decltype(module)> fee(L"publishPartialFWUpgrade_begin", module);
	ACE_TString moduleStr;
	switch (module)
	{
	case LANGUAGE_MODULE:
		moduleStr = ACE_TEXT("Language update");
		break;
	case WLAN_MODULE:
		moduleStr = ACE_TEXT("WLAN uCode update");
		break;
	default:
		return;
	}
	sendAlertIndicationMessage(CATEGORY_PARTIAL_FW_UPDATE, EVENT_PARTIAL_FWU_BEGIN, PARTIAL_FW_UPDATE_BEGIN_MESSAGE_INIT);
}

void PartialFWUpdateService::publishPartialFWUpgrade_failed(PARTIAL_FWU_MODULE module, const std::wstring& returnValue, int error)
{
	FuncEntryExit<decltype(error)> fee(L"publishPartialFWUpgrade_failed", error);
	unsigned long eventID = EVENT_PARTIAL_FWU_END_FAILURE_LANG;			
	if(module == WLAN_MODULE)
		eventID = EVENT_PARTIAL_FWU_END_FAILURE_WLAN;
	std::wstringstream strStream, errorStream;
	strStream << returnValue <<L".";

	errorStream << error;
	sendAlertIndicationMessage(CATEGORY_PARTIAL_FW_UPDATE, eventID,
		ACE_TEXT_WCHAR_TO_TCHAR(strStream.str().c_str()), ACE_TEXT_WCHAR_TO_TCHAR(errorStream.str().c_str()));

}

void PartialFWUpdateService::publishPartialFWUpgrade_end(PARTIAL_FWU_MODULE module, int returnValue)
{	
	std::wstring state;
	unsigned long eventID;
	std::wstringstream strStream;
	std::wstringstream tmpbuffer_s;

	FuncEntryExit<decltype(returnValue)> fee(L"publishPartialFWUpgrade_end", returnValue);

	if(returnValue == 0)
	{
		state = L"success";
		if(module == WLAN_MODULE)
			eventID = EVENT_PARTIAL_FWU_END_SUCCESS_WLAN;
		else
			eventID = EVENT_PARTIAL_FWU_END_SUCCESS_LANG;
	}
	else
	{
		state = L"failed with error ";
		if(module == WLAN_MODULE)
			eventID = EVENT_PARTIAL_FWU_END_FAILURE_WLAN;
		else
			eventID = EVENT_PARTIAL_FWU_END_FAILURE_LANG;
		tmpbuffer_s<<returnValue;
	}		
	strStream << PARTIAL_FW_UPDATE_END_INIT << state;

	ACE_TString arg;
	if ((eventID == EVENT_PARTIAL_FWU_END_FAILURE_LANG) || (eventID == EVENT_PARTIAL_FWU_END_FAILURE_WLAN))
		arg = ACE_TEXT_WCHAR_TO_TCHAR(tmpbuffer_s.str().c_str());

	sendAlertIndicationMessage(CATEGORY_PARTIAL_FW_UPDATE, eventID,
		ACE_TEXT_WCHAR_TO_TCHAR(strStream.str().c_str()), arg);
}

void PartialFWUpdateService::publishMissingImageFile(PARTIAL_FWU_MODULE module)
{		
	FuncEntryExit<decltype(module)> fee(L"publishMissingImageFile", module);

	unsigned long eventID;
	if(module == WLAN_MODULE)
		eventID = EVENT_PARTIAL_FWU_MISSING_IMAGE_WLAN;      
	else
		eventID = EVENT_PARTIAL_FWU_MISSING_IMAGE_LANG;

	sendAlertIndicationMessage(CATEGORY_PARTIAL_FW_UPDATE, eventID, MISSING_IMAGE_FILE_MSG);
}

bool PartialFWUpdateService::checkImageFileExist(std::wstring &imagePath)
{
	bool retVal = false;
	std::wstring lmsPath;
	std::wstring path;
	std::wstring value;

	FuncEntryExit<decltype(retVal)>(L"checkImageFileExist", retVal);
	
	if (GetServiceDirectory(L"LMS", lmsPath) != true)
	{
		UNS_DEBUG("PartialFWUpdateService::checkImageFileExist Failed getting LMS path", L"\n");
		return retVal;
	}
	lmsPath = lmsPath.substr(0, lmsPath.length() - 7); // 7 is length of "LMS.exe", we need the directory, not the file.


	if (getPartialFWUpdateImagePath(value)) //check if we have an explicit image path in registry
	{
		if (value.find(L":\\") == 1) //Absolute path
		{
			path = value;
		}
		else if (value.find(L".\\") == 0) //Relative path beginning with ".\"
		{
			path = lmsPath + value.substr(2, value.size() - 2);	//concatenate the relative path, substr is used to eliminate the ".\"

		}
		else 
		{
			path = lmsPath + value;
		}
	}
	else //no path in registry, use the executable path
	{
		std::wstring fileName;
		if (!getImageFileNameByFwVersion(fileName))
			return false; //TODO - another event log message?

		path = lmsPath + fileName;
	}

	UNS_DEBUG(L"checkImageFileExist path: %W", L"\n", path);

	if(checkFileExist(path))
	{
		imagePath = path;
		retVal = true;
	}
	return retVal;
}


const wchar_t* templateOfPfu = L"PFU.BIN";

static int
selector(const ACE_DIRENT *d)
{
	return ACE_OS::strnstr(d->d_name, templateOfPfu, ACE_OS::strlen(templateOfPfu)) > 0;
}

bool PartialFWUpdateService::LoadFwUpdateLibDll()
{
	try
	{
		if (!pfwuWrapper) // Initialize only once
		{
			using namespace Intel::MEI_Client::MKHI_Client;
			using namespace Intel::MEI_Client;

			GetFWVersionCommand  getFWVersionCommand;
			GET_FW_VER_RESPONSE version = getFWVersionCommand.getResponse();

			pfwuWrapper = PFWUpdateDllWrapperFactory::Create(version.FTMajor);
		}
		return true;
	}
	catch (std::exception e)
	{
		UNS_DEBUG(L"LoadFwUpdateLibDll failed. Error: %C", L"\n", e.what());
	}
	catch (...) {}
	return false;
}

//get the image file name according to FW version, return false at failure
bool PartialFWUpdateService::getImageFileNameByFwVersion(std::wstring& fileName)
{
	try
	{
		using namespace Intel::MEI_Client::MKHI_Client;
		using namespace Intel::MEI_Client;
		
		bool isProduction = false;

		GetFWVersionCommand getFWVersionCommand;
		GET_FW_VER_RESPONSE fwVersion = getFWVersionCommand.getResponse();

		if (fwVersion.FTMajor >= 12) //ME12 and later - use PlatformType
		{
			GetPlatformTypeCommand getPlatformTypeCommand;
			MKHI_PLATFORM_TYPE platformType = getPlatformTypeCommand.getResponse();

			isProduction = platformType.Fields.ProdSigned;
		}
		else //ME11 and before - use ImageType
		{
			GetImageTypeCommand getImageTypeCommand;
			MKHI_IMAGE_TYPE imageType = getImageTypeCommand.getResponse();

			isProduction = imageType.ImageSignData;
		}

		//Get the path of the LMS folder
		std::wstring lmsPath;
		if (GetServiceDirectory(L"LMS", lmsPath) != true)
		{
			UNS_DEBUG("PartialFWUpdateService::getImageFileNameByFwVersion Failed getting LMS path", L"\n");
			return false;
		}
		lmsPath = lmsPath.substr(0, lmsPath.length() - 7); // 7 is length of "LMS.exe", we need the directory, not the file.


														   //Get existing *PFU.BIN files in LMS folder
		ACE_Dirent_Selector lmsDir;
		std::vector<ACE_TString> existingPfuFiles;
		// Pass in function that'll specify the selection criteria.
		int status = lmsDir.open(lmsPath.c_str(), selector, ACE_SCANDIR_COMPARATOR(ACE_OS::alphasort));
		if (status == -1)
		{
			UNS_DEBUG("PartialFWUpdateService::getImageFileNameByFwVersion Failed opening: %s", L"\n", lmsPath.c_str());
			return false;
		}

		for (int n = 0; n < lmsDir.length(); ++n)
		{
			ACE_TString temp(lmsDir[n]->d_name);
			existingPfuFiles.push_back(temp);
		}

		status = lmsDir.close();
		if (status == -1)
		{
			UNS_DEBUG("PartialFWUpdateService::getImageFileNameByFwVersion Failed closing: %s", L"\n", lmsPath.c_str());
			return false;
		}


		//Look for bin file that match current FW version.
		//If none exists - try decreasing the minor version. (i.e. FW 11.7 uses bin file of 11.6)
		bool found = false;
		ACE_TCHAR requiredName[16];
		ACE_TCHAR requiredBestName[16];
		swprintf_s(requiredBestName, 16, L"%02d%02d_%2s_PFU.BIN", fwVersion.FTMajor, fwVersion.FTMinor, isProduction ? L"PD" : L"PP");
		int minorInt = fwVersion.FTMinor + 1; //Will be decreased at the begining of the loop
		
		do {
			minorInt--;
			if (minorInt < 0) //No matching bin file was found
			{
				UNS_DEBUG(L"PartialFWUpdateService::getImageFileNameByFwVersion Could not find matching bin file. Required bin file: %s. Returning false", L"\n", requiredBestName);
				return false;
			}
			swprintf_s(requiredName, 16, L"%02d%02d_%2s_PFU.BIN", fwVersion.FTMajor, minorInt, isProduction ? L"PD" : L"PP");
			UNS_DEBUG("PartialFWUpdateService::getImageFileNameByFwVersion Look for bin file: %s", L"\n", requiredName);
			
			for each (ACE_TString existingPfuFile in existingPfuFiles)
			{
				if (ACE_OS::strcmp(requiredName, existingPfuFile.c_str()) == 0)
				{
					UNS_DEBUG("PartialFWUpdateService::getImageFileNameByFwVersion Found: %s", L"\n", requiredName);
					found = true;
					break;
				}
			}
			
		} while (!found);
			
		fileName = requiredName;
		return true;
	}
#ifdef _DEBUG
	catch (Intel::MEI_Client::MKHI_Client::MKHIErrorException& e)
	{	
		UNS_DEBUG(L"PartialFWUpdateService::getImageFileNameByFwVersion failed: %C",L"\n",e.what());
	}
	catch (Intel::MEI_Client::MEIClientException& e)
	{	
		UNS_DEBUG(L"PartialFWUpdateService::getImageFileNameByFwVersion failed: %C",L"\n",e.what());
	}
	catch (std::exception& e)
	{
		UNS_DEBUG(L"Exception in PartialFWUpdateService::getImageFileNameByFwVersion: %C",L"\n", e.what());
	}

#else
	catch(...){}
#endif
	return false;

}

bool PartialFWUpdateService::isMESKU()
{
	bool res = false;
	FuncEntryExit<decltype(res)>(L"isMESKU", res);
	//Lock lock(FWUpdate_Client::FWUpdateCommand::getInternalSemaphore());
	try
	{				
		using namespace Intel::MEI_Client;	
		//NOTE: we can't convert this call to MKHI command, since calling this command check also that
		//FWUpdate Client isn't caught by an external application. Otherwise the program will get stuck
		// at the Lock constructor in the beginning of invokePartialFWUpdateFlow if the handle is caught
		MKHI_Client::GetPlatformTypeCommand getPlatformTypeCommand;
		MKHI_Client::MKHI_PLATFORM_TYPE Platform = getPlatformTypeCommand.getResponse();

		UNS_DEBUG(L"FW ImageType=%d",L"\n",Platform.Fields.ImageType);
		res = (Platform.Fields.ImageType == MKHI_Client::ME_FULL_8MB);		
	}
#ifdef _DEBUG
	catch (Intel::MEI_Client::MKHI_Client::MKHIErrorException& e)
	{	
		UNS_DEBUG(L"GetPlatformTypeCommand failed %C",L"\n",e.what());
	}
	catch (Intel::MEI_Client::MEIClientException& e)
	{	
		UNS_DEBUG(L"GetPlatformTypeCommand %C",L"\n",e.what());
	}
	catch (std::exception& e)
	{
		UNS_DEBUG(L"\nException in GetPlatformTypeCommand %C",L"\n", e.what());
	}
#else
	catch (std::exception& e)
	{
		UNS_DEBUG(L"\nException in GetPlatformTypeCommand %C",L"\n", e.what());
	}
#endif
	return res;
}

bool PartialFWUpdateService::updateLanguageChangeCode(UINT32 languageID, LANGUAGE_FLOW_MODE mode)
{
	bool res = false;
	FuncEntryExit<decltype(res)>(L"updateLanguageChangeCode", res);
	DWORD lcid;
	bool defaultLangSet = (languageID == DEFAULT_LANG_ID);

	if (defaultLangSet)
	{
		languageID = getUCLanguageID();	
	}

	SIOWSManClient client;
	UINT32 currentLang = 0;
	
	if (!client.GetSpriteLanguage((unsigned short*)&currentLang))
	{
		publishPartialFWUpgrade_failed(LANGUAGE_MODULE,L"- Failed to get FW status", 8725);
		return res;
	}
	UNS_DEBUG(L"Current language %d",L"\n", currentLang);

	UINT32 expectedLang = 0;
	if(!client.GetExpectedLanguage((unsigned short*)&expectedLang))
	{
		UNS_DEBUG(L"Failed to get expected language",L"\n");
		publishPartialFWUpgrade_failed(LANGUAGE_MODULE,L"- Failed to get FW status", 8725);
		return res;
	} 

	if (expectedLang != languageID)
	{
		if (!client.SetExpectedLanguage((unsigned short)languageID))
		{
			UNS_DEBUG(L"failed to set expected language %d",L"\n", languageID);
			publishPartialFWUpgrade_failed(LANGUAGE_MODULE,L"- Failed to set FW status", 8725);
			return res;
		}
	}

	if (currentLang == languageID)
	{
		UNS_DEBUG(L"Current language is the requested one",L"\n");
		if (mode==MANUAL_MODE)
		{
			if(!defaultLangSet && !DSinstance().GetDataValue(LastLanguageUpdate, lcid))
			{
				lcid = getWindowsLanguageID((unsigned short)languageID);
				DSinstance().SetDataValue(LastLanguageUpdate, lcid, true);
			}
			publishPartialFWUpgrade_end(LANGUAGE_MODULE,0);
		}
		res = true;
		return res;
	}

	res = invokePartialFWUpdateFlow(LANGUAGE_MODULE, LOCL_ID);	

	if (res && (mode == MANUAL_MODE))
	{
		if (defaultLangSet)
		{
			DSinstance().DeleteDataVal(LastLanguageUpdate);
		}
		else
		{
			lcid = getWindowsLanguageID((unsigned short)languageID);
			DSinstance().SetDataValue(LastLanguageUpdate, lcid, true);
		}
	}
	
	return res;
}

bool PartialFWUpdateService::invokePartialFWUpdateFlow(PARTIAL_FWU_MODULE module, UINT32 partialID)
{
	bool res = false;
	FuncEntryExit<decltype(res)>(L"invokePartialFWUpdateFlow", res);

	UNS_DEBUG(L"Partition: 0x%X",L"\n", partialID);
	publishPartialFWUpgrade_begin(module);
	std::wstring imagePath;
	if (!checkImageFileExist(imagePath))
	{
		UNS_DEBUG(L"File is missing: %s",L"\n", imagePath.c_str());
		publishMissingImageFile(module);
		return res;
	}
	
	UNS_DEBUG(L"Start",L"\n");
	
	uint32_t retcode = pfwuWrapper->performPFWU(partialID, imagePath);
	publishPartialFWUpgrade_end(module, retcode);
	return retcode == 0;
}

bool PartialFWUpdateService::partialFWUpdate(int _langID, int _mode, bool _toPublishFailure)
{
	bool res = false;
	langID = _langID;
	mode = _mode;
	FuncEntryExit<decltype(res)>(L"partialFWUpdate", res);


	if (!m_mainService->GetPortForwardingStarted()) {
		UNS_DEBUG(L"%s: Error - Port Forwarding did not start yet, aborting partialFWUpdate operation. (Will perform it when gets event of EVENT_PORT_FORWARDING_SERVICE_AVAILABLE", L"\n", name().c_str());
		m_PfuRequiredButNoPfw = true;
		return res;
	}

	PARTIAL_FWU_MODULE module = (PARTIAL_FWU_MODULE)(!_mode);

	if (!isMESKU())
	{
		UNS_DEBUG(L"Not ME SKU",L"\n");
		if(_toPublishFailure)
			publishPartialFWUpgrade_failed(module, L"- Not ME SKU", 1);
		return res;
	}

	if (!LoadFwUpdateLibDll())
	{
		UNS_DEBUG(L"Failed to load FwUpdateLib_Dll",L"\n");
		if(_toPublishFailure)
			publishPartialFWUpgrade_failed(module, L"- Failed to load FwUpdateLib_Dll", 2);
		return res;
	}


	/* COMMENT OUT AS W/A START */
	int ret = pfwuWrapper->waitForFwInitDone();
	if (ret != 0)
	{
		publishPartialFWUpgrade_end(module, ret);
		return res;
	}
	/* COMMENT OUT AS W/A END */

	UNS_DEBUG(L"After Init phase",L"\n");

	if (mode == MANUAL_MODE)
	{
		UNS_DEBUG(L"Manual Mode",L"\n");
		res = updateLanguageChangeCode(langID, MANUAL_MODE);
		return res;
	}

	//mode == INITIAL_MODE:
	
	// TODO: do nothing with failure?
	if (!SetExpectedWithLocalOSLanguage())
	{
		UNS_DEBUG(L"updateLanguageChangeCode - cause LANG PFWU - failed to set expected language", L"\n");
	}

	bool isLoclPfuNeeded = false, isWcodPfuNeeded = false;
	uint32_t requiredLanguage = DEFAULT_LANG_ID;
	ret = pfwuWrapper->isPfwuRequired(isLoclPfuNeeded, isWcodPfuNeeded, requiredLanguage);
	if (ret != 0)
	{
		publishPartialFWUpgrade_end(WLAN_MODULE, ret);
		return res;
	}

	if (isWcodPfuNeeded)
	{
		res &= invokePartialFWUpdateFlow(WLAN_MODULE, WOCD_ID);
	}
	if (isLoclPfuNeeded) 
	{
		res &= updateLanguageChangeCode(requiredLanguage, INITIAL_MODE);
	}
	return res;
}

bool SetExpectedWithLocalOSLanguage()
{
	bool res = false;
	FuncEntryExit<decltype(res)>(L"SetExpectedWithLocalOSLanguage", res);

	unsigned long valSz;
	bool retVal = DSinstance().GetDataValue(LastLanguageUpdate,valSz);


	UNS_DEBUG(L"vS: %d rV: %d",L"\n", valSz, retVal ? 1 : 0);

	if(!retVal || valSz == 0) 	
	{			
		unsigned short lang = (unsigned short)getUCLanguageID();
		SIOWSManClient wsman;
		UINT32 expectedLang = 0;

		if (!wsman.GetExpectedLanguage((unsigned short*)&expectedLang))
		{
			UNS_DEBUG(L"GetExpectedLanguage failure - lang %d",L"\n",expectedLang);
			return res;
		}
		UNS_DEBUG(L"eL: %d",L"\n", expectedLang);

		if (lang != expectedLang)
		{
			if (wsman.SetExpectedLanguage(lang))
			{
				UNS_DEBUG(L"SetExpectedLanguage success - set lang %d",L"\n",lang);
			}
			else  
			{
				UNS_DEBUG(L"SetExpectedLanguage failure - set lang %d",L"\n",lang);
				return res;
			}
		}
	}
	res = true;
	return res;
}

unsigned int getUCLanguageID()
{
	SIOWSManClient::LanguageId lang = SIOWSManClient::English;  //default language
	FuncEntryExit<decltype(lang)> fee(L"getUCLanguageID", lang);

	LCID lcid =  GetSystemDefaultLCID();
	int languageId = PRIMARYLANGID(lcid);
	int sublanguageId = SUBLANGID(lcid);

	
	switch(languageId)
	{
	case LANG_ENGLISH:
		lang = SIOWSManClient::English;
		break;
	case LANG_FRENCH :
		lang = SIOWSManClient::French;
		break;
	case LANG_GERMAN :
		lang = SIOWSManClient::German;
		break;
	case LANG_CHINESE :
		switch (sublanguageId)
		{
		case SUBLANG_CHINESE_SIMPLIFIED:
		case SUBLANG_CHINESE_SINGAPORE:
			lang = SIOWSManClient::Chinese_Simplified;
			break;
		case SUBLANG_CHINESE_TRADITIONAL:
		case SUBLANG_CHINESE_HONGKONG:
		case SUBLANG_CHINESE_MACAU:
			lang = SIOWSManClient::Chinese_Traditional;
			break;					
		}
		break;
	case LANG_JAPANESE :
		lang = SIOWSManClient::Gapanese;
		break;
	case LANG_RUSSIAN :
		lang = SIOWSManClient::Russian;
		break;
	case LANG_ITALIAN :
		lang = SIOWSManClient::Italian;
		break;
	case LANG_SPANISH :
		lang = SIOWSManClient::Spanish;
		break;
	case LANG_PORTUGUESE:
		switch (sublanguageId)
		{
		case SUBLANG_PORTUGUESE_BRAZILIAN:
			lang = SIOWSManClient::Portuguese_Brazil;
			break;
		case SUBLANG_PORTUGUESE:
		default:
			lang = SIOWSManClient::Portuguese_Portugal;
			break;
		}
		break;
	case  LANG_KOREAN :
		lang = SIOWSManClient::Korean;
		break;
	case LANG_ARABIC:
		lang = SIOWSManClient::Arabic;
		break;
	case LANG_CZECH:
		lang = SIOWSManClient::Czech;
		break;
	case LANG_DANISH:
		lang = SIOWSManClient::Danish;
		break;
	case LANG_GREEK:
		lang = SIOWSManClient::Greek;
		break;
	case LANG_FINNISH:
		lang = SIOWSManClient::Finnish;
		break;
	case LANG_HEBREW:
		lang = SIOWSManClient::Hebrew;
		break;
	case LANG_HUNGARIAN:
		lang = SIOWSManClient::Hungarian;
		break;
	case LANG_DUTCH:
		lang = SIOWSManClient::Dutch;
		break;
	case LANG_NORWEGIAN:
		lang = SIOWSManClient::Norwegian;
		break;
	case LANG_POLISH:
		lang = SIOWSManClient::Polish;
		break;
	case LANG_SLOVAK:
		lang = SIOWSManClient::Slovak;
		break;
	case LANG_SLOVENIAN:
		lang = SIOWSManClient::Slovenian;
		break;
	case LANG_SWEDISH:
		lang = SIOWSManClient::Swedish;
		break;
	case LANG_THAI:
		lang = SIOWSManClient::Thai;
		break;
	case LANG_TURKISH:
		lang = SIOWSManClient::Turkish;
		break;
	default:
		break;
	}

	return lang;
}

unsigned int getWindowsLanguageID(const UINT16 languageID)
{
	DWORD windowsLangID = 0;
	DWORD lang = 0;
	DWORD sublanguageId = 0;
	switch(languageID)
	{
	case SIOWSManClient::English :
		lang = LANG_ENGLISH;
		break;
	case SIOWSManClient::French :
		lang = LANG_FRENCH;
		break;
	case SIOWSManClient::German :
		lang = LANG_GERMAN;
		break;
	case SIOWSManClient::Chinese_Simplified :
		lang = LANG_CHINESE;
		sublanguageId = SUBLANG_CHINESE_SIMPLIFIED;
		break;
	case SIOWSManClient::Chinese_Traditional:
		lang = LANG_CHINESE;
		sublanguageId = SUBLANG_CHINESE_TRADITIONAL;
		break;
	case SIOWSManClient::Gapanese :
		lang = LANG_JAPANESE;
		break;
	case SIOWSManClient::Russian :
		lang = LANG_RUSSIAN;
		break;
	case SIOWSManClient::Italian :
		lang = LANG_ITALIAN;
		break;
	case SIOWSManClient::Spanish :
		lang = LANG_SPANISH;
		break;
	case SIOWSManClient::Portuguese_Brazil :
		lang = LANG_PORTUGUESE;
		sublanguageId = SUBLANG_PORTUGUESE_BRAZILIAN;
		break;
	case SIOWSManClient::Portuguese_Portugal:
		lang = LANG_PORTUGUESE;
		sublanguageId = SUBLANG_PORTUGUESE;
		break;					
	case  SIOWSManClient::Korean :
		lang = LANG_KOREAN;
		break;
	case SIOWSManClient::Arabic :
		lang = LANG_ARABIC;
		break;
	case SIOWSManClient::Czech :
		lang = LANG_CZECH;
		break;
	case SIOWSManClient::Danish :
		lang = LANG_DANISH;
		break;
	case SIOWSManClient::Greek :
		lang = LANG_GREEK;
		break;
	case SIOWSManClient::Finnish :
		lang = LANG_FINNISH;
		break;
	case SIOWSManClient::Hebrew :
		lang = LANG_HEBREW;
		break;
	case SIOWSManClient::Hungarian :
		lang = LANG_HUNGARIAN;
		break;
	case SIOWSManClient::Dutch :
		lang = LANG_DUTCH;
		break;
	case SIOWSManClient::Norwegian :
		lang = LANG_NORWEGIAN;
		break;
	case SIOWSManClient::Polish :
		lang = LANG_POLISH;
		break;
	case SIOWSManClient::Slovak :
		lang = LANG_SLOVAK;
		break;
	case SIOWSManClient::Slovenian :
		lang = LANG_SLOVENIAN;
		break;
	case SIOWSManClient::Swedish :
		lang = LANG_SWEDISH;
		break;
	case SIOWSManClient::Thai :
		lang = LANG_THAI;
		break;
	case SIOWSManClient::Turkish :
		lang = LANG_TURKISH;
		break;
	default:
		break;
	}
	windowsLangID = sublanguageId*0x100 + lang;
	return windowsLangID;

}
