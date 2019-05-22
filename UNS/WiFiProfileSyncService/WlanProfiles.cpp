/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2018 Intel Corporation
 */
#include "global.h"
#include "WlanDefs.h"
#include "WlanProfiles.h"

wlanps::WlanProfiles::WlanProfiles() : m_hwlan(nullptr), m_lastConnSSID(L"")
{
}
	
int wlanps::WlanProfiles::Init(HANDLE hwlan)
{
	m_hwlan = hwlan;
	return 0;
}

int wlanps::WlanProfiles::GetProfileData(PINTEL_PROFILE_DATA profileData, unsigned long *pProfileFlags)
{
	LPWSTR pProfileXml = nullptr;
	unsigned long  flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
	unsigned long  dwGrantedAccess = 0;
	unsigned long  dwResult = ERROR_SUCCESS;
	
	UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": profile: [%W]", L"\n", profileData->profile);

	// Get Wlan Profile
	dwResult = WlanGetProfile(m_hwlan, &(profileData->ifGuid), profileData->profile, nullptr, &pProfileXml, &flags, &dwGrantedAccess);
	
	if (dwResult != ERROR_SUCCESS)
	{
		UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": WlanGetProfile Failed %d", L"\n", dwResult);

		if (pProfileXml != nullptr)
		{
			WlanFreeMemory(pProfileXml);
			pProfileXml = nullptr;
		}

		// Get Profile failed
		return dwResult;
	}

	if (!isLegalProfileName(profileData->profile))
	{
		UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: Illegal profile name %W -> Return ERROR", L"\n", profileData->profile);

		return -1;
	}

	// Set additional profile data
	if (pProfileXml != nullptr)
	{
		// Authentication
		wcsncpy_s(profileData->auth, xmlRead(pProfileXml, L"authentication").c_str(), sizeof(profileData->auth) / sizeof(profileData->auth[0]));

		// Encription
		wcsncpy_s(profileData->encr, xmlRead(pProfileXml, L"encryption").c_str(), sizeof(profileData->encr) / sizeof(profileData->encr[0]));

		// Key
		wcsncpy_s(profileData->keyMaterial,	xmlRead(pProfileXml, L"keyMaterial").c_str(), sizeof(profileData->keyMaterial) / sizeof(profileData->keyMaterial[0]));

		UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": \n  auth: %W\n  encr: %W\n  key:  %W\n Flags: %d", L"\n",
			profileData->auth, profileData->encr, profileData->keyMaterial, flags);

		// Set Profile Flags
		*pProfileFlags = flags;

		WlanFreeMemory(pProfileXml);
		pProfileXml = nullptr;
	}

	return dwResult;
}

std::wstring wlanps::WlanProfiles::xmlRead(const std::wstring &strXML, std::wstring strKey)
{
	std::wstring strVal;
	std::wstring start_tag, end_tag;

	start_tag = L"<" + strKey + L">";
	size_t start_tag_len = start_tag.length();

	end_tag = L"</" + strKey + L">";
	size_t start_pos = strXML.find(start_tag, 0);
	size_t end_pos = strXML.find(end_tag, start_pos);

	if (start_pos != strXML.npos &&  end_pos != strXML.npos)
	{
		strVal = strXML.substr(start_pos + start_tag_len, end_pos - (start_pos + start_tag_len));
	}
	return strVal;
}

bool wlanps::WlanProfiles::isLegalProfileName(const std::wstring &profileName)
{
	bool isLegal = false;

	std::wstring startOfTag = L"<";
	std::wstring endOfTag = L">";
	std::wstring ampersand = L"&";

	size_t startOfTagPosition = profileName.find(startOfTag, 0);
	size_t endOfTagPosition = profileName.find(endOfTag, 0);
	size_t ampersandPosition = profileName.find(ampersand, 0);

	if ((startOfTagPosition == std::wstring::npos) &&  
		(endOfTagPosition == std::wstring::npos) &&
		(ampersandPosition == std::wstring::npos))
	{
		isLegal = true;
	}
	else
	{
		UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: Bad charachters in profile name", L"\n");
	}
	return isLegal;
}

int wlanps::WlanProfiles::GetProfiles(PINTEL_PROFILE_DATA profiles[], int* numOsUserProfiles, const authenticationSet_t &supportedAuthentication, const encriptionSet_t &supportedEncription)
{
	// initialize variables
	unsigned long dwResult = 0;
	unsigned int i, j;
	unsigned int numUserProfiles = 0;

	// WlanEnumInterfaces
	PWLAN_INTERFACE_INFO_LIST pIfList = nullptr;
	PWLAN_PROFILE_INFO_LIST   pProfileList = nullptr;

	PWLAN_INTERFACE_INFO      pIfInfo = nullptr;
	PWLAN_PROFILE_INFO        pProfile = nullptr;

	if (nullptr == m_hwlan)
	{
		UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": handle is not opened", L"\n");
		return 1;
	}

	dwResult = WlanEnumInterfaces(m_hwlan, nullptr, &pIfList);
	if (dwResult != ERROR_SUCCESS)
	{
		UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": WlanEnumInterfaces failed with error: %d", L"\n", dwResult);
		return 1;
	}
	else
	{
		if (pIfList != nullptr)
		{
			UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": WLAN_INTERFACE_INFO_LIST Entries: %d Index: %d", L"\n",
				pIfList->dwNumberOfItems, pIfList->dwIndex);

			// Loop over Wlan Interfaces
			for (i = 0; (i < (int)pIfList->dwNumberOfItems) ; i++)
			{
				pIfInfo = (WLAN_INTERFACE_INFO *)&pIfList->InterfaceInfo[i];

				UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": [%d] Interface Description: %W  Interface State: %d, dwNumberOfItems = %d", L"\n",
					i, pIfInfo->strInterfaceDescription, pIfInfo->isState, pIfList->dwNumberOfItems);

				dwResult = WlanGetProfileList(m_hwlan, &pIfInfo->InterfaceGuid, nullptr, &pProfileList);

				if (dwResult != ERROR_SUCCESS)
				{
					UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": WlanGetProfileList error: %u", L"\n", dwResult);
					continue;
				}

				UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": WlanGetProfileList num of items: %d", L"\n", pProfileList->dwNumberOfItems);

				// Loop over all OS profiles or until MAX_OS_USER_PROFILES User Profiles were found 
				for (j = 0; (j < pProfileList->dwNumberOfItems) && (numUserProfiles < MAX_OS_USER_PROFILES); j++)
				{
					std::wstring auth = L"";
					std::wstring enc  = L"";
					std::wstring key  = L"";
					std::wstring ssid = L"";

					pProfile = (WLAN_PROFILE_INFO *)&pProfileList->ProfileInfo[j];

					// WlanGetProfile
					LPWSTR pProfileXml = nullptr;
					unsigned long  dwFlags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
					unsigned long  dwGrantedAccess = 0;
					unsigned long  dwResult = WlanGetProfile(m_hwlan, &pIfInfo->InterfaceGuid, pProfile->strProfileName, nullptr, &pProfileXml, &dwFlags, &dwGrantedAccess);

					if (dwResult != ERROR_SUCCESS)
					{
						UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: WlanGetProfile failed with %d", L"\n", dwResult);
						continue;
					}

					if (pProfile->dwFlags & WLAN_PROFILE_GROUP_POLICY)
					{
						UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: %W is a Group Policy Profile", L"\n", pProfile->strProfileName);
						continue; // skip
					}

					if (!isLegalProfileName(pProfile->strProfileName))
					{
						UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: illegal profile name %W", L"\n", pProfile->strProfileName);
						continue;
					}

					if (pProfileXml != nullptr)
					{
						auth = xmlRead(pProfileXml, L"authentication");
						enc = xmlRead(pProfileXml, L"encryption");
						key = xmlRead(pProfileXml, L"keyMaterial");
						std::wstring ssidElement = xmlRead(pProfileXml, L"SSID");
						ssid = xmlRead(ssidElement, L"name");

						WlanFreeMemory(pProfileXml);
						pProfileXml = nullptr;
					}

					if (!isSupportedEncription(enc, supportedEncription ))
					{
						UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: encription unsupported by LMS-PS %W for Profile %W", L"\n",
							enc.c_str(), pProfile->strProfileName);
						continue;
					}

					if (!isSupportedAuthentication(auth, supportedAuthentication) )
					{
						UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: authentication unsupported by LMS-PS %W for Profile %W", L"\n",
							auth.c_str(), pProfile->strProfileName);
						continue;
					}

					//allocate new user profile data
					PINTEL_PROFILE_DATA prof = (PINTEL_PROFILE_DATA)malloc(sizeof(INTEL_PROFILE_DATA));

					if (prof != nullptr)
					{
						SecureZeroMemory(prof, sizeof(INTEL_PROFILE_DATA));

						//Set the profile data
						wcsncpy_s(prof->profile, pProfile->strProfileName, _countof(prof->profile));
						memcpy_s(&prof->ifGuid, sizeof(prof->ifGuid), &pIfInfo->InterfaceGuid, sizeof(prof->ifGuid));
						wcsncpy_s(prof->auth, auth.c_str(), auth.length());
						wcsncpy_s(prof->encr, enc.c_str(), enc.length());
						wcsncpy_s(prof->keyMaterial, key.c_str(), key.length());
						wcsncpy_s(prof->SSID, ssid.c_str(), _countof(prof->SSID));

						//update the list
						profiles[numUserProfiles] = prof;
						numUserProfiles++;
					}
					else
					{
						UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__"[%03l]: Failed to allocate memory for new Profile -> Get out of the loop", L"\n");
						break;
					}
				}
			}
		}
	}

	UNS_DEBUG(L"[ProfileSync] " __FUNCTIONW__": numUserProfiles %d", L"\n", numUserProfiles);

	// Return the number of OS User Profiles
	*numOsUserProfiles = numUserProfiles;

	if (pProfileList != nullptr)
	{
		WlanFreeMemory(pProfileList);
		pProfileList = nullptr;
	}

	if (pIfList != nullptr)
	{
		WlanFreeMemory(pIfList);
		pIfList = nullptr;
	}

	return 0;
}

bool wlanps::WlanProfiles::isSupportedEncription(const std::wstring &enc, const encriptionSet_t &supportedEnc)
{
	return (supportedEnc.find(enc) != supportedEnc.end());
}

bool wlanps::WlanProfiles::isSupportedAuthentication(const std::wstring &auth, const authenticationSet_t &supportedAuth)
{
	return (supportedAuth.find(auth) != supportedAuth.end());
}