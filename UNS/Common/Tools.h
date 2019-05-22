/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2013-2018 Intel Corporation
 */
#ifndef _TOOLS_H
#define _TOOLS_H

#pragma once
#include <string>
#include "CommonDllExport.h"

std::string COMMON_EXPORT getDateTime();

// Parse the MAC address from IP_ADAPTER_INFO Address field to human readble string
std::string COMMON_EXPORT MacAddressToString(unsigned char addr[], unsigned int addrLen);

std::wstring COMMON_EXPORT StringToWString(const std::string& s);
std::string  COMMON_EXPORT WStringToString(const std::wstring& wstr);


#ifdef WIN32
bool COMMON_EXPORT GetServiceDirectory(const std::wstring serviceName, std::wstring& serviceFilePath);

bool COMMON_EXPORT checkFileExist(std::wstring path);
#endif // WIN32

bool COMMON_EXPORT GetLocalFQDN(std::string& fqdn);

#endif //_TOOLS_H