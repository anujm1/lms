/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2009-2019 Intel Corporation
 */
/*++

@file: Protocol.cpp

--*/

#include "global.h"
#include <string.h>

#ifdef WIN32
#include <iphlpapi.h>

#include <tchar.h>
#include <iostream>
#else
#include <errno.h>

#define INVALID_SOCKET  (SOCKET)(-1)
#define SOCKET_ERROR            (-1)
static inline int closesocket(int fd) {return close(fd);}
#define WSAEWOULDBLOCK EINPROGRESS

#if defined SHUT_RDWR && !defined SD_BOTH
#  define SD_BOTH SHUT_RDWR
#endif

//Very implementation dependent
#define SS_PORT(ssp) (((struct sockaddr_in*)(ssp))->sin_port)

#endif // WIN32

#include <algorithm>
#include <sstream>
#include <fstream>
#include "Protocol.h"
#include "Common.h"
#include "LMS_if.h"
#include "Tools.h"
#include <SetEnterpriseAccessCommand.h>
#include <GetDNSSuffixListCommand.h>

#include <FuncEntryExit.h>

#define FQDN_MAX_SIZE 256

const LMEProtocolVersionMessage Protocol::MIN_PROT_VERSION(1, 0); 
const LMEProtocolVersionMessage Protocol::MAX_PROT_VERSION(1, 0);

using std::vector;
using std::string;

//will be used for passing argument to connectionAccept_callback:
struct ConnectionAcceptCB{
	Protocol* _protocol;
	unsigned int _port;
	PortForwardRequest* _PFReq;

	ConnectionAcceptCB(Protocol* protocol, unsigned int port):_protocol(protocol), _port(port),_PFReq(NULL){}
};

#ifdef WIN32
//prototype for callback function
int CALLBACK ConditionAcceptFunc(
	LPWSABUF lpCallerId,
	LPWSABUF lpCallerData,
	LPQOS pQos,
	LPQOS lpGQOS,
	LPWSABUF lpCalleeId,
	LPWSABUF lpCalleeData,
	GROUP FAR * g,
	DWORD_PTR dwCallbackData
	);
#endif // WIN32

Protocol::Protocol() : _lme(true), _sockets_active(false), _signalPipe(), _rxSocketBuffer(0), _rxSocketBufferSize(0),
					   _eventLogWrn(nullptr), _eventLogDbg(nullptr), _eventLogParam(nullptr), _clientNotFound(false)
{
	_handshakingStatus = NOT_INITIATED;
	_pfwdService = NOT_STARTED;
	_AmtProtVersion.MajorVersion = 0;
	_AmtProtVersion.MinorVersion = 0;
#ifdef _REMOTE_SUPPORT
	_remoteAccessEnabledInAMT = false;
	_remoteAccessCurrentlyPossible = false;
#endif
	_AMTFQDN = "";
	_failureReported.clear();
	_OutHostsFileLog=true;
}

Protocol::~Protocol()
{
	_failureReported.clear();
	_lme.Disconnect(APF_DISCONNECT_BY_APPLICATION);
	DestroySockets();
}

bool Protocol::Init(InitParameters & params)
{
	bool res = false;
	FuncEntryExit<decltype(res)> fee(L"Init", res);
	
	_eventLogWrn = params.evtLogCbWrn_;
	_eventLogDbg = params.evtLogCbDbg_;
	_eventLogParam = params.evtLogParam_;

	Deinit();

	LMEConnection::InitParameters initParams(_LmeCallback, this, params.devNotifyCb_, params.evtLogParam_, params.threadMgr_, _SignalSelectCallback);

	if (!_lme.Init(initParams)) {
		_clientNotFound = _lme.IsClientNotFound();
		return res;
	}


	{
		std::lock_guard<std::mutex> l(_versionLock);

		if (_handshakingStatus == NOT_INITIATED) {
			_lme.ProtocolVersion(MAX_PROT_VERSION);
			_handshakingStatus = INITIATED;
		}
	}
#ifdef _REMOTE_SUPPORT
	if (!AdapterListInfo::Init(_AdapterCallback, this)) {
		_lme.Deinit();
		return res;
	}
#endif

	long bufSize = _lme.GetHECI().GetBufferSize() - sizeof(APF_CHANNEL_DATA_MESSAGE);
	if (bufSize > 0) {
		_rxSocketBuffer = new char[bufSize];
		_rxSocketBufferSize = bufSize;
	}
	else {
		Deinit();
		return res;
	}

#ifdef _REMOTE_SUPPORT
	_checkRemoteSupport(true);
#endif
	res = true;
	return res;
}

#ifdef WIN32
void Protocol::_TCPCleanup()
{
	DWORD pid = GetCurrentProcessId();

	PMIB_TCPTABLE_OWNER_PID  pTcpTable;
	DWORD dwSize = 0;
	DWORD dwRetVal = 0;

	int i;

	pTcpTable = (MIB_TCPTABLE_OWNER_PID *) new unsigned char[sizeof (MIB_TCPTABLE_OWNER_PID)];
	if (pTcpTable == NULL) {
		UNS_DEBUG(L"Error allocating memory", L"\n");
		return;
	}

	dwSize = sizeof(MIB_TCPTABLE_OWNER_PID);

	// Make an initial call to GetExtendedTcpTable to
	// get the necessary size into the dwSize variable
	if ((dwRetVal = GetExtendedTcpTable(pTcpTable, &dwSize, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)) ==
		ERROR_INSUFFICIENT_BUFFER) {
			delete []pTcpTable;
			pTcpTable = NULL;
			pTcpTable = (MIB_TCPTABLE_OWNER_PID *) new unsigned char[dwSize];
			if (pTcpTable == NULL) {
				UNS_DEBUG(L"Error allocating memory", L"\n");
				return;
			}
	}

	// Make a second call to GetExtendedTcpTable to get
	// the actual data we require
	if ((dwRetVal = GetExtendedTcpTable(pTcpTable, &dwSize, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)) == NO_ERROR) {
		LMS_DEBUG_VAR(L"\tNumber of entries: %d", (int) pTcpTable->dwNumEntries);
		for (i = 0; i < (int) pTcpTable->dwNumEntries; i++) {
			if ((pid == pTcpTable->table[i].dwOwningPid) && 
				(pTcpTable->table[i].dwState == MIB_TCP_STATE_FIN_WAIT2)) {
					MIB_TCPROW row;
					row.dwLocalAddr = pTcpTable->table[i].dwLocalAddr;
					row.dwLocalPort = pTcpTable->table[i].dwLocalPort;
					row.dwRemoteAddr = pTcpTable->table[i].dwRemoteAddr;
					row.dwRemotePort = pTcpTable->table[i].dwRemotePort;
					row.dwState = MIB_TCP_STATE_DELETE_TCB;
					DWORD err = SetTcpEntry(&row);
			}
		}
	} else {
		LMS_DEBUG_VAR(L"\tGetTcpTable failed with %d", dwRetVal);
		delete []pTcpTable;
		return;
	}

	delete []pTcpTable;
	pTcpTable = NULL;

	return;    

}
#else
void Protocol::_TCPCleanup()
{
// Not needed on Linux because of the of the socket reuse mechanism
}
#endif // WIN32

//void Protocol::_TCPCleanupIPv6()
//{
//	DWORD pid = GetCurrentProcessId();
//
//	// Declare and initialize variables
//	PMIB_TCP6TABLE_OWNER_PID  pTcpTable;
//	DWORD dwSize = 0;
//	DWORD dwRetVal = 0;
//
//	int i;
//
//	pTcpTable = (MIB_TCP6TABLE_OWNER_PID *) new unsigned char[sizeof (MIB_TCP6TABLE_OWNER_PID)];
//	if (pTcpTable == NULL) {
//		UNS_DEBUG("Error allocating memory", L"\n");
//		return;
//	}
//
//	dwSize = sizeof(MIB_TCP6TABLE_OWNER_PID);
//
//	// Make an initial call to GetExtendedTcpTable to
//	// get the necessary size into the dwSize variable
//	if ((dwRetVal = GetExtendedTcpTable(pTcpTable, &dwSize, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0)) ==
//		ERROR_INSUFFICIENT_BUFFER) {
//			delete []pTcpTable;
//			pTcpTable = NULL;
//			pTcpTable = (MIB_TCP6TABLE_OWNER_PID *) new unsigned char[dwSize];
//			if (pTcpTable == NULL) {
//				UNS_DEBUG("Error allocating memory", L"\n");
//				return;
//			}
//	}
//
//	// Make a second call to GetExtendedTcpTable to get
//	// the actual data we require
//	if ((dwRetVal = GetExtendedTcpTable(pTcpTable, &dwSize, TRUE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0)) == NO_ERROR) {
//		UNS_DEBUG("\tNumber of entries: %d", (int) pTcpTable->dwNumEntries, L"\n");
//		for (i = 0; i < (int) pTcpTable->dwNumEntries; i++) {
//			if ((pid == pTcpTable->table[i].dwOwningPid) && 
//				(pTcpTable->table[i].dwState == MIB_TCP_STATE_FIN_WAIT2)) {
//					MIB_TCPROW row; // Need to be MIB_TCP6ROW 
//					row.dwLocalAddr = pTcpTable->table[i].dwLocalAddr;
//					row.dwLocalPort = pTcpTable->table[i].dwLocalPort;
//					row.dwRemoteAddr = pTcpTable->table[i].dwRemoteAddr;
//					row.dwRemotePort = pTcpTable->table[i].dwRemotePort;
//					row.dwState = MIB_TCP_STATE_DELETE_TCB;
//					DWORD err = SetTcpEntry(&row); // Doesn't work for IPv^
//			}
//		}
//	} else {
//		UNS_DEBUG("\tGetTcpTable failed with %d", dwRetVal, L"\n");
//		delete []pTcpTable;
//		return;
//	}
//
//	delete []pTcpTable;
//	pTcpTable = NULL;
//
//	return;    
//
//}

void Protocol::Deinit()
{
	FuncEntryExit<void> fee(L"Deinit");
	try
	{
		_lme.Deinit();

#ifdef _REMOTE_SUPPORT

		AdapterListInfo::Deinit();
		{
			std::lock_guard<std::mutex> l(_remoteAccessLock);
			_remoteAccessEnabledInAMT = false;
			_remoteAccessCurrentlyPossible = false;
		}
#endif

		_signalPipe.close();
		{
			std::lock_guard<std::mutex> l(_channelsLock);

			ChannelMap::iterator it = _openChannels.begin();

			for (; it != _openChannels.end(); it++) {
				_closeSocket(it->second->GetSocket());	
				delete it->second;
			}

			_openChannels.clear();
		}

		{
			std::lock_guard<std::mutex> l(_portsLock);
			PortMap::iterator it = _openPorts.begin();

			for (; it != _openPorts.end(); it++) {
				if (it->second.size() > 0) {

					const vector<SOCKET> vs= it->second[0]->GetListeningSockets();

					for_each(vs.begin(), vs.end(), &Protocol::_closeSocket);

					PortForwardRequestList::iterator it2 = it->second.begin();

					for (; it2 != it->second.end(); it2++) {
						delete *it2;	
					}
					it->second.clear();
				}
			}
			_openPorts.clear();
		}

		{
			std::lock_guard<std::mutex> l(_deleteLock);
			if (_rxSocketBuffer != NULL) 
			{
				delete []_rxSocketBuffer;
				_rxSocketBuffer = NULL;
				_rxSocketBufferSize = 0;
			}

			_sockets_active = false;
			_handshakingStatus = NOT_INITIATED;
			_pfwdService = NOT_STARTED;
			_AmtProtVersion.MajorVersion = 0;
			_AmtProtVersion.MinorVersion = 0;
		}

		_TCPCleanup();
		//_TCPCleanupIPv6();
	}
#ifdef _DEBUG
	catch (std::exception& e)
	{
		UNS_DEBUG(L"Exception in Protocol::Deinit() %C",L"\n", e.what());
	}
#else
	catch(std::exception&){}
#endif
}

unsigned int GetLocalPort(SOCKET s) 
{

	sockaddr_storage addrIn; 
	socklen_t addrLen = sizeof(sockaddr_storage);

	if( getsockname(s,(sockaddr*)&addrIn, &addrLen) != 0 ) {
		return 0;
	}

	return ntohs(SS_PORT(&addrIn));
}

int Protocol::_closeSocket(SOCKET s)
{
	if (s == INVALID_SOCKET)
		return -1;
	shutdown(s, SD_BOTH);
	return closesocket(s);
}

bool Protocol::CreateSockets()
{
	int ret = _signalPipe.open();
	if (ret)
		UNS_DEBUG(L"Error: Can't open signalPipe %d", L"\n", ret);

	_sockets_active = (ret) ? false : true;
	return _sockets_active;
}

void Protocol::DestroySockets()
{
	_signalPipe.close();
	_sockets_active = false;
}

#ifdef WIN32
bool Protocol::_setSockOptions(const addrinfo &addr, SOCKET s, SOCKET_STATUS &status)
{
	int optval = 1;
	if (setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		       (char *)&optval, sizeof(optval)) == SOCKET_ERROR) {
		UNS_DEBUG(L"Error: Can't bind socket using exclusive address", L"\n");
		status = NOT_EXCLUSIVE_ADDRESS;
		return false;
	}

	if (addr.ai_socktype == SOCK_DGRAM)
		return true;

	linger l;
	l.l_onoff = 0;
	l.l_linger = 0;
	if (setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(l)) == SOCKET_ERROR) {
		status = LINGER_ERROR;
		return false;
	}
	return true;
}
#else
bool Protocol::_setSockOptions(const addrinfo &addr, SOCKET s, SOCKET_STATUS &status)
{
	int optval = 1; // allow reuse of local addresses
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		       &optval, sizeof(optval)) == SOCKET_ERROR) {
		UNS_DEBUG(L"Error: Can't set SO_REUSEADDR option %d", L"\n", errno);
		status = CANT_REUSE_ADDRESS;
		return false;
	}

	if (addr.ai_family != AF_INET6)
		return true;

	optval = 1; // the socket is restricted to sending and receiving IPv6 packets only
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
		       &optval, sizeof(optval)) == SOCKET_ERROR) {
		UNS_DEBUG(L"Error: Can't set IPV6_V6ONLY option %d", L"\n", errno);
		status = NO_IPV6_V6ONLY;
		return false;
	}
	return true;
}
#endif // WIN32

SOCKET Protocol::_createSocket(const addrinfo *caddr, SOCKET_STATUS &status)
{
	addrinfo addr = *caddr;
	if (addr.ai_socktype != SOCK_DGRAM) {
		addr.ai_socktype = SOCK_STREAM;
	}

	SOCKET s = socket(addr.ai_family, addr.ai_socktype, addr.ai_protocol);
	if (s == INVALID_SOCKET) {
		status = NOT_CREATED;
		return INVALID_SOCKET;
	}

	if (!_setSockOptions(addr, s, status)) {
		_closeSocket(s);
		return INVALID_SOCKET;
	}
	return s;
}

vector<SOCKET> Protocol::_createServerSocket(unsigned int family, unsigned int port, SOCKET_STATUS &status,bool cond_accept)
{
	status = NOT_CREATED;
	vector<SOCKET> vs;
	bool error = false;
	addrinfo *result = NULL, *resultCopy = NULL, hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
	
	std::stringstream ss; 
	ss << port;

	// Resolve the local address and port to be used by the server
	int e = getaddrinfo(NULL, ss.str().c_str(), &hints, &result);
	if (e != 0) {
		freeaddrinfo(result);
		return vs;
	}

	// Using resultCopy in order to free later the original allocated result pointer
	for (resultCopy = result;  resultCopy != NULL; resultCopy = resultCopy->ai_next) 
	{
		SOCKET s = _createSocket(resultCopy, status);
		if (s == INVALID_SOCKET) 
		{
			LMS_DEBUG_VAR(L"Failed to create a server socket for addr=%C", addr2str(* (sockaddr_storage*)resultCopy->ai_addr).c_str());
			error = true;
			break;
		}
		if (::bind(s, resultCopy->ai_addr, (int)resultCopy->ai_addrlen) == SOCKET_ERROR) 
		{
			LMS_DEBUG_VAR(L"Error %d in binding server socket.", WSAGetLastError()); 
			_closeSocket(s);
			status = NOT_BINDED;
			error = true;
			break;
		}
#ifdef WIN32
		if (cond_accept)
		{
			int optval = 1;
			if (setsockopt(s, SOL_SOCKET,  SO_CONDITIONAL_ACCEPT,
				(char *)&optval, sizeof(optval)) == SOCKET_ERROR) {
					UNS_DEBUG(L"Error: Can't bind socket using exclusive address", L"\n");
					_closeSocket(s);
					status = CONDITIONAL_ACCEPT_ERROR;
					error = true;
					break;
				
			}
		}
#endif // WIN32
		if (listen(s, 5) == SOCKET_ERROR) {
			_closeSocket(s);
			status = NOT_LISTENED;
			error = true;
			break;
		}

		status = ACTIVE;
		vs.push_back(s);
	}
	if (error)
	{
		for (size_t i=0; i<vs.size();i++)
		{
			_closeSocket(vs[i]);
		}
		vs.clear();
	}

	freeaddrinfo(result);

	if (vs.size() > 0)
	{
		status = ACTIVE;
	}
	return vs;
}

#ifdef WIN32
bool Protocol::_setNonBlocking(SOCKET s, bool enable)
{
	unsigned long param = (enable) ? 1 : 0;

	if (ioctlsocket(s, FIONBIO, &param) == SOCKET_ERROR) {
		return false;
	}

	return true;
}
#else
bool Protocol::_setNonBlocking(SOCKET s, bool enable)
{
	int flags;
	flags = fcntl(s, F_GETFL, 0);
	if (flags == -1)
		return false;
	if (fcntl(s, F_SETFL, (enable) ? flags | O_NONBLOCK : flags & ~(O_NONBLOCK)) < 0)
		return false;
	return true;
}
#endif // WIN32

SOCKET Protocol::_connect(addrinfo *addr, unsigned int port, int type, long timeOut)
{	
	SOCKET s = INVALID_SOCKET;
	vector<SOCKET> sockets;
	FuncEntryExit<SOCKET> fee(L"_connect", s);
	UNS_DEBUG(L"Port: %d", L"\n", port);

	for (addr; addr != NULL; addr = addr->ai_next) {

		SS_PORT(addr->ai_addr) = htons(port); 
		addr->ai_socktype = type;

		SOCKET_STATUS status;
		addrinfo hints = createAddrinfo(addr->ai_family,addr->ai_socktype,addr->ai_protocol,AI_NUMERICHOST);
		addrinfo *result = NULL;


		std::stringstream localss; 
		localss << 0;

		// Resolve the local address and port to be used by the server
		int e = getaddrinfo(NULL, localss.str().c_str(), &hints, &result);
		if (e != 0) {
			freeaddrinfo(result);
			continue;
		}

		s = _createSocket(result, status);
		if (s == INVALID_SOCKET) {
			freeaddrinfo(result);
			continue;
		}

		// Check if we are in Non Blocking Mode
		if (timeOut != 0) {
			_setNonBlocking(s, true);
		}
		if ( ((connect(s, addr->ai_addr, (int)addr->ai_addrlen) == SOCKET_ERROR)) &&
			(WSAGetLastError() != WSAEWOULDBLOCK)){
				_closeSocket(s);
				freeaddrinfo(result);
				s = INVALID_SOCKET;
				continue;
		}

		// Check if we are in Non Blocking Mode
		if (timeOut != 0) {
			sockets.push_back(s);
			freeaddrinfo(result);
			continue;
		}

		freeaddrinfo(result);
		break;
	}

	// Check if we are in Non Blocking Mode
	if (timeOut != 0) {

		fd_set fdSet, fdExpSet;
		timeval timeOutVal;

		timeOutVal.tv_usec = 0;
		timeOutVal.tv_sec = timeOut;

		FD_ZERO(&fdSet);
		FD_ZERO(&fdExpSet);

		int fdCount = 0;

		for (unsigned int i = 0; i < sockets.size(); i++) {

			SOCKET tempSock = sockets[i]; 
			FD_SET(tempSock, &fdSet);
			FD_SET(tempSock, &fdExpSet);

			if ((int)tempSock > fdCount) {
				fdCount = (int)tempSock;
			}
		}
		fdCount++;

		select(fdCount, NULL, &fdSet, &fdExpSet, &timeOutVal);

		s = INVALID_SOCKET;
		for (unsigned int i = 0; i < sockets.size(); i++) {

			SOCKET tempSock = sockets[i]; 

			if ((s == INVALID_SOCKET) && (FD_ISSET(tempSock, &fdSet))) {
				s = tempSock;
				continue;
			}
			if(FD_ISSET(tempSock, &fdExpSet))
			{
				LMS_DEBUG_VAR(L"connection attempt was failed on socket:%d",tempSock);
			}
			_closeSocket(tempSock);
		}
	}

	return s;
}

bool Protocol::_acceptConnection(SOCKET s, unsigned int port)
{
	sockaddr_storage addr;
	socklen_t addrLen = sizeof(addr);
	FuncEntryExit<void> fee(L"_acceptConnection");
	
	ConnectionAcceptCB cb_params(this, port);

#ifdef WIN32
	//Accept an incoming connnection request on the listening socket and transfer 
	//control to the accepting socket.	
	SOCKET s_new = WSAAccept(s, (sockaddr *)&addr, &addrLen,&ConditionAcceptFunc,(DWORD_PTR)&cb_params);
#else
	SOCKET s_new = accept(s, (sockaddr *)&addr, &addrLen);
#endif // WIN32
	if (s_new == INVALID_SOCKET) {
		int err = GetLastError();
	
		UNS_DEBUG(L"New connection denied (%d): %W ", L"\n", err, getErrMsg(err).c_str());
		_closeSocket(s_new);
		return false;
	}	

	PortForwardRequest *portForwardRequest = NULL;

	if (cb_params._PFReq == NULL)	//further detect test need to be done here (now we have the socket and we can pull the locall address)
									// (more details in checkAcceptLogic() )
	{
		portForwardRequest = _findFWReq(s_new,port,NULL);
		if (portForwardRequest == NULL)
		{
			UNS_DEBUG(L"New connection denied ", L"\n");
			_closeSocket(s_new);
			return false;
		}
	}
	else {
		portForwardRequest = cb_params._PFReq;
	}
	
	if(addr.ss_family == AF_INET)
	{
		UNS_DEBUG(L"AF_INET", L"\n");
	}
	else if(addr.ss_family == AF_INET6)
	{
		UNS_DEBUG(L"AF_INET6", L"\n");
	}
	
	//get address as a string:
	int originator_port = SS_PORT(&addr);
	SS_PORT(&addr) = 0;
	string address = addr2str(addr);
	string::size_type index = address.find('%');
	address = address.substr(0, index);
	SS_PORT(&addr) = originator_port;

	_setNonBlocking(s_new, true);

	Channel *c = new SocketChannel(portForwardRequest,s_new);
	c->SetStatus(Channel::NOT_OPENED);

	std::lock_guard<std::mutex> l(_channelsLock);

	c->GetPortForwardRequest()->IncreaseChannelCount();

	string connectedIP = (addr.ss_family == AF_INET) ? "127.0.0.1" : "::1";
	if (portForwardRequest->IsLocal()) {
		address = connectedIP;
	}

	if (!_lme.ChannelOpenForwardedRequest((uint32_t)s_new, connectedIP, port, address,ntohs(originator_port)))
	{
		LMS_DEBUG_VAR(L"ERROR: failed to send channel open request to LME. Sender %d. Address: %C:%d ", (int)s_new, 
			address.c_str(), ntohs(originator_port));

		_closeSocket(s_new);
		delete c;
		return false;
	}
	_openChannels[s_new] = c;
	LMS_DEBUG_VAR(L"Send channel open request to LME. Sender %d, Address: %C:%d ", (int)s_new, 
		address.c_str(), ntohs(originator_port));

	return true;
}

#ifdef WIN32
int CALLBACK ConditionAcceptFunc(
	LPWSABUF lpCallerId,
	LPWSABUF lpCallerData,
	LPQOS pQos,
	LPQOS lpGQOS,
	LPWSABUF lpCalleeId,
	LPWSABUF lpCalleeData,
	GROUP FAR * g,
	DWORD_PTR dwCallbackData
	)
{

	if ((dwCallbackData == NULL) ||(((ConnectionAcceptCB *)dwCallbackData)->_protocol == NULL))
	{
		UNS_DEBUG(L"Error: callback: ConditionAcceptFunc with illegall data", L"\n");
		return CF_REJECT;
	}
	Protocol *protocol	= ((ConnectionAcceptCB *)dwCallbackData)->_protocol;
	unsigned int port	= ((ConnectionAcceptCB *)dwCallbackData)->_port;
	
	sockaddr_storage caller_addr;
	memcpy_s(&caller_addr, sizeof(sockaddr_storage), lpCallerId->buf, lpCallerId->len);

	LMS_DEBUG_VAR(L"received new connection from %C", addr2str(caller_addr).c_str());
	
	return protocol->checkAcceptLogic(caller_addr, port,((ConnectionAcceptCB *)dwCallbackData)->_PFReq);
}

//------------------------------------------------------------------
// The LMS conditional accept logic is as follow:
// If hostVPNDisabled--> perform conditional accept:
//         if connection is from Local - accept 
 //        if from remote - dont accept (since remoteVPN is disabled)
// Else- accept connection(ack will be send), and only after then check environment detection:
//         if connection is from Local - accept and send to fw
//         if from remote - check environment detection:
//					if connection src adddr is inside org - send data to fw
//					outside org - close the socket
//------------------------------------------------------------------
int Protocol::checkAcceptLogic(sockaddr_storage& caller_addr, unsigned int port, PortForwardRequest* PW_req)
{

	int ret = CF_REJECT;

	if (_remoteAccessEnabledInAMT)
	{
		ret = CF_ACCEPT;	//(remote VPN) the detection test will be done after accepting the connection.
							//(maybe LMS will close the connection later)
	}
	else{
		PW_req = _findFWReq(NULL,port,&caller_addr);
		ret = ((PW_req == NULL)? CF_REJECT : CF_ACCEPT);
	}
	return ret;
}
#endif // WIN32

PortForwardRequest* Protocol::_findFWReq(SOCKET sock, unsigned int port, sockaddr_storage* caller_addr)
{
	PortForwardRequest* ret = NULL;

	//_portsLock is already aquired by the calling function: Select().
	PortMap::iterator it = _openPorts.find(port);
	if (it != _openPorts.end()) {	
		PortForwardRequestList::iterator it2 = it->second.begin();

		for (; it2 != it->second.end(); it2++) {

			if (((*it2)->GetStatus() == PortForwardRequest::LISTENING) &&
				((*it2)->IsConnectionPermitted(this, sock, caller_addr) == 1))
			{
					ret = *it2;
					break;
			}
		}

	}
	return ret;
}

int Protocol::Select()
{

	//timeval tv;
	//tv.tv_sec = 1;
	//tv.tv_usec = 0;

	int res;
	int fdCount = 0;
	unsigned int fdMin = UINT_MAX;
	fd_set rset;

	FD_ZERO(&rset);

	int _serverSignalSocket = (int)_signalPipe.read_handle();
	
	FD_SET(_serverSignalSocket, &rset);
	if ((int)_serverSignalSocket > fdCount) {
		fdCount = (int)_serverSignalSocket;
	}

	{
		//add "acceptors" sockets to select vector
		std::lock_guard<std::mutex> l(_portsLock);
		PortMap::iterator it = _openPorts.begin();
		
		for (; it != _openPorts.end(); it++) {
			if (it->second.size() > 0) {
				vector<SOCKET> vs = it->second[0]->GetListeningSockets();
				for (size_t i = 0; i < vs.size(); i++) {
					SOCKET serverSocket = vs[i]; 
					FD_SET(serverSocket, &rset);
					
					if ((int)serverSocket > fdCount) {
						fdCount = (int)serverSocket;
					}
				}
			}
		}
	}
	
	{
		std::lock_guard<std::mutex> l(_channelsLock);
		
		//add "channels" sockets to select vector
		ChannelMap::iterator it = _openChannels.begin();
		
		for (; it != _openChannels.end(); it++) {
			if ((it->second->GetStatus() == Channel::OPEN) &&
				(it->second->GetTxWindow() > 0) &&
				(dynamic_cast<SocketChannel*>(it->second))) {
				SOCKET socket = it->second->GetSocket();
				FD_SET(socket, &rset);
				if ((int)socket > fdCount) {
					fdCount = (int)socket;
				}
				if ((int)socket < fdMin) {
					fdMin = (int)socket;
				}
				
			}
		}
	}

	//TBD check that rset doesn't contain more than 64 sockets (windows limitation)
	fdCount++;
	res = select(fdCount, &rset, NULL, NULL, NULL);
	if (res == -1) {
		int err = GetLastError();
	
		UNS_DEBUG(L"Select error (%d): %W", L"\n", err, getErrMsg(err).c_str());
		return -1;
	}

	if (res == 0) {
		return 0;
	}

	if (FD_ISSET(_serverSignalSocket, &rset)) {	// Received a 'signal'
		char c = 0;
		_signalPipe.recv(&c, 1);
		FD_CLR(_serverSignalSocket, &rset);
		res--;
	}

	{
		std::lock_guard<std::mutex> l(_portsLock);
		PortMap::iterator it = _openPorts.begin();
		
		for (; it != _openPorts.end(); it++) {
			if (it->second.size() > 0) {
				vector<SOCKET> vs = it->second[0]->GetListeningSockets();
				for (unsigned int i = 0; i < vs.size(); i++) {
					SOCKET serverSocket = vs[i]; 
					if (FD_ISSET(serverSocket, &rset)) {
						
						UNS_DEBUG(L"Connection requested on port %d", L"\n", it->first);
						_acceptConnection(serverSocket, it->first);
						FD_CLR(serverSocket, &rset);
						res--;
					}
				}
			}
		}
	}

	int i;
	for (i = fdMin/*0*/; (fdMin != UINT_MAX) && (res > 0) && (i < fdCount); i++) { // TBD Better way?
		if (FD_ISSET(i, &rset)) {
			_rxFromSocket(i);	
			res--;
		}
	}

	return 1;
}

int Protocol::_rxFromSocket(SOCKET s)
{
	Channel *c = NULL;
	{
		std::lock_guard<std::mutex> l(_channelsLock);

		ChannelMap::iterator it = _openChannels.find(s);;
		
		if (it == _openChannels.end()) {
			// Data received from a socket that is not in the map.
			// Since we only select on our sockets, this means it was
			// in the map, but was removed, probably because we received
			// an End Connection message from the HECI.
			return 0;
		}
		SocketChannel *tmp = dynamic_cast<SocketChannel*>(it->second);
		if (!tmp)
		{// Should not happen, we can't receive data from unconnected SOAP socket...
			return 0;
		}
		c = new SocketChannel(*tmp);
		
	}

	int res = 0;

	int len = std::min(c->GetTxWindow(), (unsigned int) _rxSocketBufferSize);
	res = recv(s, _rxSocketBuffer, len, 0);
	if (res > 0) {
		// send data to LME
		UNS_DEBUG(L"Socket[%d] ==>: %d bytes", L"\n", (int)s, res);
#ifdef _DEBUG
		FILE *file = fopen("c:\\lms.log","a");
		if (file != NULL) {
			time_t rawtime;
			tm * timeinfo;
			time ( &rawtime );
			timeinfo = localtime(&rawtime);
			fprintf(file, "-----------------------From application---------------------------\n"
				"%s: Data received from socket %d. Sender %d, Receiver %d\n", 
				asctime(timeinfo), (int)s, 
				c->GetSenderChannel(), c->GetRecipientChannel());
			fwrite(_rxSocketBuffer, sizeof(char), res, file);
			fprintf(file, "-----------------------End from application---------------------------\n");
			fclose(file);
		}
#endif
		_lme.ChannelData(c->GetRecipientChannel(), res, (unsigned char *)_rxSocketBuffer);	
		goto out;
	} else if (res == 0) {
		// connection closed
		UNS_DEBUG(L"Socket[%d] ==>: 0 bytes", L"\n", (int)s);
		goto out;
	} else {
		int err = GetLastError();
		UNS_DEBUG(L"Socket[%d]: Error (%d): %W", L"\n", (int)s, err, getErrMsg(err).c_str());
		goto out;
	}

out:
	{
		std::lock_guard<std::mutex> l(_channelsLock);

		ChannelMap::iterator it = _openChannels.find(s);;
		
		if (it == _openChannels.end()) {
			// Data received from a socket that is not in the map.
			// Since we only select on our sockets, this means it was
			// in the map, but was removed, probably because we received
			// an End Connection message from the HECI.
			delete c;
			return 0;
		}
		if (res > 0) {
			it->second->RemoveBytesTxWindow(res);
		}
		else {
			it->second->SetStatus(Channel::WAITING_CLOSE);
			_lme.ChannelClose(c->GetRecipientChannel());
		}
	}
	delete c;
	
	return 0;
}

int VersionCompare(const uint32_t FirstMajorVersion, const uint32_t FirstMinorVersion, 
									const uint32_t SecondMajorVersion, const uint32_t SecondMinorVersion) 
{
	int diff =  FirstMajorVersion - SecondMajorVersion;
	diff = (diff != 0) ? diff: FirstMinorVersion - SecondMinorVersion;
	return diff;
}

void Protocol::SignalSelect()
{
	if (_sockets_active) {
		_signalSelect();
	}
}

	
void Protocol::_signalSelect()
{
	_signalPipe.send("s", 1); //Enforce a new execution of Select()
}

void Protocol::_closePortForwardRequest(PortForwardRequest *p) 
{

	PortMap::iterator it = _openPorts.find(p->GetPort());
	if (it == _openPorts.end()) {
		return;
	}

	bool found = false;
	PortForwardRequestList::iterator it2 = it->second.begin();
	for (; it2 != it->second.end(); it2++) {
		if ( (*it2)== p) {
			found = true;
			break;
		}
	}

	if (found == false)
	{
		UNS_DEBUG(L"Failed finding the closed port forward tunnel", L"\n");
		return;
	}
	
	if ((*it2)->GetStatus() == PortForwardRequest::NOT_ACTIVE) {

		vector<SOCKET> serverSockets = (*it2)->GetListeningSockets();
		delete (*it2);
		it->second.erase(it2);

		if (it->second.size() == 0) {

			for_each(serverSockets.begin(), serverSockets.end(), &Protocol::_closeSocket);

			_openPorts.erase(it);
		}
	}
}

bool Protocol::_checkProtocolFlow(LMEMessage *message)
{
	switch (message->MessageType) {

		case APF_SERVICE_REQUEST :
		case APF_USERAUTH_REQUEST:
			{
				std::lock_guard<std::mutex> l(_versionLock);
				if (_handshakingStatus != AGREED) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return false;
				}
				return true;
			}

		case APF_GLOBAL_REQUEST:
		case APF_CHANNEL_OPEN:
		case APF_CHANNEL_OPEN_CONFIRMATION:
		case APF_CHANNEL_OPEN_FAILURE:
		case APF_CHANNEL_CLOSE:
		case APF_CHANNEL_DATA:
		case APF_CHANNEL_WINDOW_ADJUST:
			{
				std::lock_guard<std::mutex> l(_versionLock);
				if ((_handshakingStatus != AGREED) || (_pfwdService != STARTED)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return false;
				}
				return true;
			}
	}

	return false;
}

void Protocol::_LmeCallback(void *param, void *buffer, unsigned int len, int *status) // TBD Remove Status?
{
	Protocol *prot = (Protocol *)param;

	prot->_LmeReceive(buffer, len, status);
}

//callback for waking the service from the Select() to allow re-initilization 
void Protocol::_SignalSelectCallback(void *protocol) 
{
	((Protocol *)protocol)->SignalSelect();
}

void Protocol::_LmeReceive(void *buffer, unsigned int len, int *status)  
{
	bool failure =false;
	if (len < sizeof(LMEMessage)) {
		_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
		Deinit();
		return;
	}

	LMEMessage *message = (LMEMessage *)buffer;

	*status = 0;

	switch (message->MessageType) {
		case APF_DISCONNECT:
			{
				if (len < sizeof(LMEDisconnectMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEDisconnectMessage *disconnectMessage = (LMEDisconnectMessage *)message;
				UNS_DEBUG(L"LME requested to disconnect with reason code 0x%08x", L"\n", disconnectMessage->ReasonCode);
				Deinit();
				return;	
			}
			break;

		case APF_SERVICE_REQUEST:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEServiceRequestMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEServiceRequestMessage *serviceRequestMessage = 
					(LMEServiceRequestMessage *)message;

				if ((serviceRequestMessage->ServiceName.compare(APF_SERVICE_AUTH) == 0) ||
					(serviceRequestMessage->ServiceName.compare(APF_SERVICE_PFWD) == 0)) {

					_lme.ServiceAccept(serviceRequestMessage->ServiceName);
					LMS_DEBUG_VAR(L"Accepting service: %C", serviceRequestMessage->ServiceName.c_str());
					if (serviceRequestMessage->ServiceName.compare(APF_SERVICE_PFWD) == 0){
						std::lock_guard<std::mutex> l(_versionLock);
						_pfwdService = STARTED;
					}
				}
				else {
					LMS_DEBUG_VAR(L"Requesting to disconnect from LME with reason code 0x%08x", 
						APF_DISCONNECT_SERVICE_NOT_AVAILABLE);
					_lme.Disconnect(APF_DISCONNECT_SERVICE_NOT_AVAILABLE);
					Deinit();
					return;	
				}
			}
			break;

		case APF_USERAUTH_REQUEST:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEUserAuthRequestMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
					
				UNS_DEBUG(L"Sending Userauth success message", L"\n");
				_lme.UserAuthSuccess();
			}
			break;
		
		case APF_PROTOCOLVERSION:
			{
				if (len < sizeof(LMEProtocolVersionMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}

				std::lock_guard<std::mutex> l(_versionLock);

				LMEProtocolVersionMessage *versionMessage = (LMEProtocolVersionMessage *)message;
				switch (_handshakingStatus) {
					case AGREED:
					case NOT_INITIATED:
						_lme.ProtocolVersion(MAX_PROT_VERSION);
					case INITIATED:
						if (VersionCompare(MIN_PROT_VERSION.MajorVersion, MIN_PROT_VERSION.MinorVersion,
							versionMessage->MajorVersion, versionMessage->MinorVersion) > 0) {

							UNS_DEBUG(L"Version %d.%d is not supported.", L"\n", versionMessage->MajorVersion, 
							versionMessage->MinorVersion);
							_lme.Disconnect(APF_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED);
							Deinit();
							return;
						}
						if (VersionCompare(MAX_PROT_VERSION.MajorVersion, MAX_PROT_VERSION.MinorVersion,
							versionMessage->MajorVersion, versionMessage->MinorVersion) > 0) {

							_AmtProtVersion.MajorVersion = versionMessage->MajorVersion;
							_AmtProtVersion.MinorVersion = versionMessage->MinorVersion;
						}		
						else {
							_AmtProtVersion.MajorVersion = MAX_PROT_VERSION.MajorVersion;
							_AmtProtVersion.MinorVersion = MAX_PROT_VERSION.MinorVersion;
						}
						_handshakingStatus = AGREED;
						break;
					default:
						_lme.Disconnect(APF_DISCONNECT_BY_APPLICATION);
						Deinit();
						break;
				}

			}
			break;
	

		case APF_GLOBAL_REQUEST:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEGlobalRequestMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}

				LMEGlobalRequestMessage *globalMessage = (LMEGlobalRequestMessage *)message;
				
				UNS_DEBUG(L"Global Request type 0x%02x", L"\n", globalMessage->RequestType);
				switch (globalMessage->RequestType) {
					
					case LMEGlobalRequestMessage::TCP_FORWARD_REQUEST:
						{
							if (len < sizeof(LMETcpForwardRequestMessage)) {
								_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
								Deinit();
								return;
							}

							LMETcpForwardRequestMessage *tcpForwardRequestMessage = 
															(LMETcpForwardRequestMessage *)globalMessage;
							
							IsConnectionPermittedCallback cb = NULL;
#ifdef _REMOTE_SUPPORT
							//"0.0.0.0" means remote interface also for IPv6 
								if (_isRemoteAPFAddress(tcpForwardRequestMessage->Address)) {
									LMS_DEBUG_VAR(L"--------->FW request to open remote tunnel- %C",tcpForwardRequestMessage->Address.c_str());
									cb = _isRemoteCallback;
								}
								else 
#endif
							{
								cb = _isLocalCallback;
							}

							{
								std::lock_guard<std::mutex> l(_portsLock);
								vector<SOCKET> serverSockets;
								PortMap::iterator it = _openPorts.find(tcpForwardRequestMessage->Port);
								if (it != _openPorts.end()) {	
									if (it->second.size() > 0) {
										
										serverSockets = it->second[0]->GetListeningSockets();

										PortForwardRequestList::iterator it2 = it->second.begin();
										bool exists = false;
										for (; it2 != it->second.end(); it2++) {
											if (((*it2)->GetStatus() != PortForwardRequest::NOT_ACTIVE) &&
												((*it2)->GetBindedAddress().compare(tcpForwardRequestMessage->Address) == 0)) {
												exists = true;
												break;
											}
										}

										if (exists) {
											// Log in Event Log
										
											std::stringstream ss; 
											ss << "LMS already accepted a request at " << tcpForwardRequestMessage->Address 
											<< ":" << tcpForwardRequestMessage->Port;
											
											_eventLogWrn(_eventLogParam, ss.str().c_str());
											LMS_DEBUG_VAR(L"%C", ss.str().c_str());
											// Send Failure replay to LME
											_lme.TcpForwardReplyFailure();
											return;
										}
									}
								}
								else {
									PortForwardRequestList portForwardRequestList;
									_openPorts[tcpForwardRequestMessage->Port] = portForwardRequestList;	
									UNS_DEBUG(L"New port %d", L"\n", tcpForwardRequestMessage->Port);
								}

								if (serverSockets.size() == 0) {
									SOCKET_STATUS socketStatus;

									serverSockets = _createServerSocket(AF_UNSPEC, tcpForwardRequestMessage->Port, 
										socketStatus,true);

									if ((serverSockets.size() == 0) || (socketStatus != Protocol::ACTIVE)) {
										// Log in Event Log
										UNS_DEBUG(L"Cannot listen at port %d", L"\n", tcpForwardRequestMessage->Port);
										
										if (_failureReported[tcpForwardRequestMessage->Port] == false)
										{
											std::stringstream ss; 
											_eventLogWrn(_eventLogParam, ss.str().c_str());
											_failureReported[tcpForwardRequestMessage->Port] = true;
										}

										// Send Failure replay to LME
										_lme.TcpForwardReplyFailure();
										failure = true;
									}
								}
								if (failure != true)
								{

									LMS_DEBUG_VAR(L"Listening at port %d at %C interface.", tcpForwardRequestMessage->Port, 
										(cb == _isLocalCallback)?"local":"remote");
									
									// Log in Event Log
									if (_failureReported[tcpForwardRequestMessage->Port] == true)
									{
										std::stringstream ss; 
										ss << "Listening at port " << tcpForwardRequestMessage->Port << "";
										_eventLogDbg(_eventLogParam,  ss.str().c_str());
									}

									PortForwardRequest *portForwardRequest = 
										new PortForwardRequest(tcpForwardRequestMessage->Address, tcpForwardRequestMessage->Port, 
										serverSockets, cb, (cb == _isLocalCallback));

									UNS_DEBUG(L"Add forward request to port:  %d", L"\n", tcpForwardRequestMessage->Port);
									_openPorts[tcpForwardRequestMessage->Port].push_back(portForwardRequest);

									// Send Success replay to LME
									_lme.TcpForwardReplySuccess(tcpForwardRequestMessage->Port);

									portForwardRequest->SetStatus(
										(cb == _isLocalCallback)?
										PortForwardRequest::LISTENING : 
									PortForwardRequest::PENDING_REQUEST);
									_failureReported[tcpForwardRequestMessage->Port] = false;

									_signalSelect();
								}
							}




							if (failure == true) {
								_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
								Deinit();
								*status =  -1;
								return;
							}

							PortFailureReports::iterator itr;
							for(itr = _failureReported.begin(); itr != _failureReported.end(); itr++)
							{
								if (itr->second == true)
								{
									failure = true;
									break;
								}
							}

							if (!failure) {
								if (cb == _isLocalCallback) {

									LMS_DEBUG_VAR(L"Listening at port %d addr:%C at %C interface.", tcpForwardRequestMessage->Port, 
										tcpForwardRequestMessage->Address.c_str(),
										(cb == _isLocalCallback)?L"local":L"remote");

									// Now it only updates for IPv4
									_updateIPFQDN(tcpForwardRequestMessage->Address);

								} else {
									UNS_DEBUG(L"--------->remote tunnel created - going to check remote support", L"\n");
									_checkRemoteSupport(true);
								}
							}
						}
						break;

					case LMEGlobalRequestMessage::TCP_FORWARD_CANCEL_REQUEST:
						{
							if (len < sizeof(LMETcpForwardCancelRequestMessage)) {
								_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
								Deinit();
								return;
							}

							LMETcpForwardCancelRequestMessage *tcpForwardCancelRequestMessage = 
															(LMETcpForwardCancelRequestMessage *)globalMessage;

							std::lock_guard<std::mutex> l(_portsLock);
							UNS_DEBUG(L"--------->FW request to CLOSE tunnel", L"\n");
							PortMap::iterator it = _openPorts.find(tcpForwardCancelRequestMessage->Port);
							if (it == _openPorts.end()) {
								LMS_DEBUG_VAR(L"Previous request on address %C and port %d doesn't exist.", 
									tcpForwardCancelRequestMessage->Address.c_str(), tcpForwardCancelRequestMessage->Port); 
								_lme.TcpForwardCancelReplyFailure();
								return;
							}
							
							if (_isRemoteAPFAddress(tcpForwardCancelRequestMessage->Address))
							{
								LMS_DEBUG_VAR(L"--------->FW request to CLOSE remote tunnel - %C",tcpForwardCancelRequestMessage->Address.c_str());
							}

							bool found = false;
							PortForwardRequestList::iterator it2 = it->second.begin();
							for (; it2 != it->second.end(); it2++) {
								if (((*it2)->GetBindedAddress().compare(tcpForwardCancelRequestMessage->Address) == 0) && 
									//((*it2)->GetPort() == tcpForwardCancelRequestMessage->Port)) {
									((*it2)->GetStatus() != PortForwardRequest::NOT_ACTIVE)) {
										found = true;
										break;
								}
							}

							if (found) {
							
								(*it2)->SetStatus(PortForwardRequest::NOT_ACTIVE);

								if ((*it2)->GetChannelCount() == 0) {
									_closePortForwardRequest(*it2);
								}

								if (_isRemoteAPFAddress(tcpForwardCancelRequestMessage->Address))
								{
									_checkRemoteAccessStatus();
								}

								_lme.TcpForwardCancelReplySuccess();							
								return;
							}
							else {
								LMS_DEBUG_VAR(L"Previous request on address %C and port %d doesn't exist.", 
									tcpForwardCancelRequestMessage->Address.c_str(), tcpForwardCancelRequestMessage->Port); 
								_lme.TcpForwardCancelReplyFailure();
								return;
							}		
						}
						break;
					
					case LMEGlobalRequestMessage::UDP_SEND_TO:
						{
							if (len < sizeof(LMEUdpSendToMessage)) {
								_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
								Deinit();
								return;
							}

							LMEUdpSendToMessage *udpSendToMessage = (LMEUdpSendToMessage *)globalMessage;
							
							addrinfo *info = NULL;
							addrinfo hint;

							memset(&hint, 0, sizeof(hint));
							hint.ai_family = AF_UNSPEC;
							hint.ai_socktype = SOCK_DGRAM;
							hint.ai_protocol = IPPROTO_UDP;

							if (getaddrinfo(udpSendToMessage->Address.c_str(), NULL, &hint, &info) != 0) {
								UNS_DEBUG(L"Unable to send UDP data.", L"\n"); 
								return;
							}

							if (info == NULL) {
								UNS_DEBUG(L"Unable to send UDP data.", L"\n"); 
								return;
							}
							
							//NOTE: Connect Time out is 8 Sec as Defined from AMT FW IMPELEMENTION, Be Careful when Changing!!!
							SOCKET s = _connect(info, udpSendToMessage->Port, SOCK_DGRAM);							
							
							freeaddrinfo(info);

							if (s == INVALID_SOCKET) {
								UNS_DEBUG(L"Unable to send UDP data.", L"\n"); 
								return;
							}

							int count = send(s, (char *)udpSendToMessage->Data.data(), udpSendToMessage->Data.size(), 0);
							LMS_DEBUG_VAR(L"Sent UDP data: %d bytes of %d.", count, udpSendToMessage->Data.size()); 
#ifdef _DEBUG
							FILE *file = fopen("c:\\lms.log","a");
							if (file != NULL) {
								time_t rawtime;
								tm * timeinfo;
								time ( &rawtime );
								timeinfo = localtime(&rawtime);
								fprintf(file, "-----------------------From FW UDP---------------------------\n"
									"%s: Data received from FW. Address %s, Port %d\n", 
									asctime(timeinfo), udpSendToMessage->Address.c_str(), 
									udpSendToMessage->Port);
								fwrite(udpSendToMessage->Data.data(), sizeof(char), udpSendToMessage->Data.size(), file);
								fprintf(file, "-----------------------End from application---------------------------\n");
								fclose(file);
							}
#endif
							_closeSocket(s);
						}
						break;

					default:
						_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
						Deinit();
						break;

				}
			
			}
			break;
		
		case APF_CHANNEL_OPEN:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEChannelOpenRequestMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEChannelOpenRequestMessage *channelOpenMessage = (LMEChannelOpenRequestMessage *)message;
				
				LMS_DEBUG_VAR(L"Got channel request from AMT.  Recipient channel %d for address %C, port %d.", 
					channelOpenMessage->SenderChannel, channelOpenMessage->Address.c_str(), channelOpenMessage->Port);

				Channel *c;
				if (channelOpenMessage->Port != 0) {
					addrinfo *info = NULL;
					addrinfo hint;

					memset(&hint, 0, sizeof(hint));
					hint.ai_family = AF_UNSPEC;
					hint.ai_socktype = SOCK_STREAM;
					hint.ai_protocol = IPPROTO_TCP;

					if (getaddrinfo((char *)channelOpenMessage->Address.c_str(), NULL, &hint, &info) != 0) {
						LMS_DEBUG_VAR(L"Unable to open direct channel to address %C.",
							channelOpenMessage->Address.c_str());
						_lme.ChannelOpenReplayFailure(channelOpenMessage->SenderChannel,
							OPEN_FAILURE_REASON_CONNECT_FAILED);
						return;
					}

					if (info == NULL) {
						LMS_DEBUG_VAR(L"Unable to open direct channel to address %C.",
							channelOpenMessage->Address.c_str());
						_lme.ChannelOpenReplayFailure(channelOpenMessage->SenderChannel,
							OPEN_FAILURE_REASON_CONNECT_FAILED);
						return;
					}

					SOCKET s = _connect(info, channelOpenMessage->Port, SOCK_STREAM);

					freeaddrinfo(info);

					if (s == INVALID_SOCKET) {
						LMS_DEBUG_VAR(L"Unable to open direct channel to address %C.",
							channelOpenMessage->Address.c_str());
						_lme.ChannelOpenReplayFailure(channelOpenMessage->SenderChannel,
							OPEN_FAILURE_REASON_CONNECT_FAILED);
						return;
					}

					_setNonBlocking(s, true);

					c = new SocketChannel(NULL, s);
				}
				else {// SOAP message, no real socket
					SOCKET s = socket(AF_LOCAL, SOCK_STREAM, 0); // Dummy socket for a map
					if (s == INVALID_SOCKET) {
						LMS_DEBUG_VAR(L"Unable to open direct channel to address %C.",
							channelOpenMessage->Address.c_str());
						_lme.ChannelOpenReplayFailure(channelOpenMessage->SenderChannel,
							OPEN_FAILURE_REASON_CONNECT_FAILED);
						return;
					}
					c = new SoapChannel(NULL, s);
				}

				c->AddBytesTxWindow(channelOpenMessage->InitialWindow);
				c->SetRecipientChannel(channelOpenMessage->SenderChannel);
				c->SetStatus(Channel::OPEN);

				std::lock_guard<std::mutex> l(_channelsLock);
				_openChannels[c->GetSenderChannel()] = c;
				_lme.ChannelOpenReplaySuccess(c->GetRecipientChannel(), c->GetSenderChannel());

				_signalSelect();

			}
			break;

		case APF_CHANNEL_OPEN_CONFIRMATION:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEChannelOpenReplaySuccessMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEChannelOpenReplaySuccessMessage *channelOpenSuccessMessage = 
																(LMEChannelOpenReplaySuccessMessage *)message;

				std::lock_guard<std::mutex> l(_channelsLock);

				ChannelMap::iterator it = _openChannels.find(channelOpenSuccessMessage->RecipientChannel);
				if (it != _openChannels.end()) {
					it->second->SetStatus(Channel::OPEN);
					it->second->SetRecipientChannel(channelOpenSuccessMessage->SenderChannel);
					it->second->AddBytesTxWindow(channelOpenSuccessMessage->InitialWindow);
				}

				_signalSelect();
			
			}
			break;

		case APF_CHANNEL_OPEN_FAILURE:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEChannelOpenReplayFailureMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEChannelOpenReplayFailureMessage *channelFailureMessage = 
																(LMEChannelOpenReplayFailureMessage *)message;
		
				PortForwardRequest *closePortForwardRequest = NULL;
				{
					std::lock_guard<std::mutex> l(_channelsLock);
					ChannelMap::iterator it = _openChannels.find(channelFailureMessage->RecipientChannel);
					if (it != _openChannels.end()) {

						_closeSocket(it->second->GetSocket());
						PortForwardRequest *p = it->second->GetPortForwardRequest();
						if ((p != NULL) && (p->DecreaseChannelCount() == 0)) {
							closePortForwardRequest = p;
						}

						delete it->second;
						_openChannels.erase(it);
						LMS_DEBUG_VAR(L"Channel open request was refused. Reason code: 0x%02x reason.", 
							channelFailureMessage->ReasonCode);
					}
				}

				if (closePortForwardRequest != NULL) {
					std::lock_guard<std::mutex> l(_portsLock);
					_closePortForwardRequest(closePortForwardRequest);
				}
				
			}
			break;

		case APF_CHANNEL_CLOSE:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEChannelCloseMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEChannelCloseMessage *channelCloseMessage = (LMEChannelCloseMessage *)message;
				
				PortForwardRequest *closePortForwardRequest = NULL;
				{
					std::lock_guard<std::mutex> l(_channelsLock);
					ChannelMap::iterator it = _openChannels.find(channelCloseMessage->RecipientChannel);
					if (it != _openChannels.end()) {
						Channel *c = it->second;
						switch(c->GetStatus()) {
							case Channel::OPEN:
								c->SetStatus(Channel::CLOSED);
								_lme.ChannelClose(c->GetRecipientChannel());
								LMS_DEBUG_VAR(L"Channel %d was closed by AMT.", c->GetSenderChannel());
								break;
							case Channel::WAITING_CLOSE:
								LMS_DEBUG_VAR(L"Received reply by AMT on closing channel %d.", c->GetSenderChannel());		
								break;
						}
						_closeSocket(c->GetSocket());
						PortForwardRequest *p = c->GetPortForwardRequest();
						if ((p != NULL) && (p->DecreaseChannelCount() == 0)) {
							closePortForwardRequest = p;
						}

						_openChannels.erase(it);
						delete c;	
					}
				}

				if (closePortForwardRequest != NULL) {
					std::lock_guard<std::mutex> l(_portsLock);
					_closePortForwardRequest(closePortForwardRequest);
				}
			
			}
			break;

		case APF_CHANNEL_DATA:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEChannelDataMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEChannelDataMessage *channelDataMessage = (LMEChannelDataMessage *)message;

				std::lock_guard<std::mutex> l(_channelsLock);

				ChannelMap::iterator it = _openChannels.find(channelDataMessage->RecipientChannel);
				if (it != _openChannels.end()) {
					if ((it->second->GetStatus() == Channel::OPEN) || (it->second->GetStatus() == Channel::WAITING_CLOSE)) {
						
						if (it->second->GetRxWindow() < channelDataMessage->Data.size()) {
							return;
						}

						bool request_close = false;
						int count = it->second->ProcessRx((char *)channelDataMessage->Data.data(), channelDataMessage->Data.size(), request_close);
						UNS_DEBUG(L"Sent %d bytes of %d from AMT to channel %d with socket %d.", L"\n", 
							count, channelDataMessage->Data.size(), channelDataMessage->RecipientChannel,
							it->second->GetSocket());
#ifdef _DEBUG
						FILE *file = fopen("c:\\lms.log","a");
						if (file != NULL) {
							time_t rawtime;
							tm * timeinfo;
							time ( &rawtime );
							timeinfo = localtime(&rawtime);
							fprintf(file, "-----------------------From FW TCP---------------------------\n"
								"%s: Data received from FW to socket %d. Sender %d, Receiver %d\n", 
								asctime(timeinfo), (int)it->second->GetSocket(), 
								it->second->GetSenderChannel(), it->second->GetRecipientChannel());
							fwrite(channelDataMessage->Data.data(), sizeof(char), 
								channelDataMessage->Data.size(), file);
							fprintf(file, "-----------------------End from application---------------------------\n");
							fclose(file);
						}
#endif
						
						_lme.ChannelWindowAdjust(it->second->GetRecipientChannel(), channelDataMessage->Data.size());
						if (request_close)
						{
							if (it->second->GetStatus() == Channel::OPEN)
							{
								UNS_DEBUG(L"[%u:%u] closing socket, as requested", L"\n",
									it->second->GetSenderChannel(), it->second->GetRecipientChannel());
								it->second->SetStatus(Channel::WAITING_CLOSE);
								_lme.ChannelClose(it->second->GetRecipientChannel());
							}
						}
					}
				}	
			}
			break;

		case APF_CHANNEL_WINDOW_ADJUST:
			{
				if (!_checkProtocolFlow(message)) {
					return;
				}

				if (len < sizeof(LMEChannelWindowAdjustMessage)) {
					_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
					Deinit();
					return;
				}
				
				LMEChannelWindowAdjustMessage *channelWindowMessage = (LMEChannelWindowAdjustMessage *)message;

				std::lock_guard<std::mutex> l(_channelsLock);

				ChannelMap::iterator it = _openChannels.find(channelWindowMessage->RecipientChannel);
				if (it != _openChannels.end()) {
					it->second->AddBytesTxWindow(channelWindowMessage->BytesToAdd);
					_signalSelect();	
				}	
			}
			break;

		default:
			_lme.Disconnect(APF_DISCONNECT_PROTOCOL_ERROR);
			Deinit();
			break;
	}
}
void Protocol::_checkRemoteAccessStatus()
{
	if (!_remoteTunnelExist())
	{
		//_updateEnterpriseAccessStatus(AdapterListInfo::GetLocalDNSSuffixList());
		std::lock_guard<std::mutex> l(_remoteAccessLock);
		_remoteAccessEnabledInAMT = false;
	}
}

// NOTE: you MUST enter here AFTER locking _portsLock
bool Protocol::_remoteTunnelExist()
{
	bool ret = false;

	for (PortMap::iterator it = _openPorts.begin(); it != _openPorts.end(); it++) {
		for (PortForwardRequestList::iterator it2 = it->second.begin();
			it2 != it->second.end(); it2++) {

				if (! (*it2)->IsLocal()) {
					ret = true;
					break;
				}
		}
	}
	return ret;
}

bool Protocol::_isRemoteAPFAddress(string addr)
{
	return ((addr.compare("0.0.0.0") == 0) ||(addr.compare("::")==0));
}

#ifdef _REMOTE_SUPPORT

void Protocol::_AdapterCallback(void *param, SuffixMap &localDNSSuffixes)
{
	Protocol *prot = (Protocol *)param;
	if (prot && prot->IsInitialized())
		prot->_updateEnterpriseAccessStatus(localDNSSuffixes);
	
}

bool Protocol::_checkRemoteSupport(bool requestDnsFromAmt)
{
	if (requestDnsFromAmt) {
		try
		{
			Intel::MEI_Client::AMTHI_Client::GetDNSSuffixListCommand getDNSSuffixListCommand;
			Intel::MEI_Client::AMTHI_Client::GET_DNS_SUFFIX_LIST_RESPONSE response = getDNSSuffixListCommand.getResponse();
			std::lock_guard<std::mutex> l(_AMTDNSLock);
			_AMTDNSSuffixes.clear();
			unsigned int n = response.HashHandles.size();
			if (n > 0)
			{
				vector<string> dnsSuffixes;
				string ss;
				for (unsigned i = 0; i < n; i++)
				{
					char c = response.HashHandles[i];
					if (c == '\0')
					{
						dnsSuffixes.push_back(ss);
						ss.clear();
					}
					else
						ss += c;
				}
				_AMTDNSSuffixes.assign(dnsSuffixes.begin(), dnsSuffixes.end());
			}
		}
		catch(Intel::MEI_Client::MEIClientException e)
		{
			LMS_DEBUG_VAR(L"_checkRemoteSupport: GetDNSSuffixListCommand failed: %C", e.what());
		}
	}

	_updateEnterpriseAccessStatus(AdapterListInfo::GetLocalDNSSuffixList(), true);
	
	return true; 
}

void Protocol::_updateEnterpriseAccessStatus(const SuffixMap &localDNSSuffixes, bool sendAnyWay) 
{
	FuncEntryExit<void> fee(L"_updateEnterpriseAccessStatus");

	bool raccess = false;
	sockaddr_storage localIp;
	{
		std::lock_guard<std::mutex> l(_AMTDNSLock);

		vector<string>::iterator remIt;
		vector<string>::iterator startIt = _AMTDNSSuffixes.begin();
		vector<string>::iterator endIt = _AMTDNSSuffixes.end();
		SuffixMap::const_iterator locIt;

		memset(&localIp, 0, sizeof(localIp));
		string suffix = "";
		for (locIt = localDNSSuffixes.begin(); locIt != localDNSSuffixes.end(); locIt++) {
			const std::string &b = locIt->second;
			remIt = find_if(startIt, endIt, [b](const std::string &a) { return CompareSuffix(a, b); });
			if (remIt != _AMTDNSSuffixes.end()) {
				raccess = true;
				suffix = locIt->second;
				if (locIt->first.ss_family == AF_INET) {
					localIp = locIt->first;
					//	break;
				}
			}
		}

		if (!sendAnyWay && (raccess == _remoteAccessCurrentlyPossible))
		{
			return;
		}
		_remoteAccessCurrentlyPossible = raccess;

		if (!raccess)
		{
			UNS_DEBUG(L"didn't find any shared suffix - Host VPN is DISABLED", L"\n");
		}
		else {
			LMS_DEBUG_VAR(L"found shared suffix:%C - Host VPN is enabled", suffix.c_str());
		}
	}

	if (raccess) {
		std::lock_guard<std::mutex> l(_portsLock);
		for (PortMap::iterator it = _openPorts.begin(); it != _openPorts.end(); it++) {
			for (PortForwardRequestList::iterator it2 = it->second.begin();
					it2 != it->second.end(); it2++) {

				if ((*it2)->GetStatus() == PortForwardRequest::PENDING_REQUEST) {
					(*it2)->SetStatus(PortForwardRequest::LISTENING);
				}
			}
		}

		_signalSelect();
	}

	uint8_t Flags;
	std::vector<uint8_t> HostIPAddress;
	uint8_t EnterpriseAccess = raccess ? 1 : 0;
	memset(&HostIPAddress, 0, sizeof(HostIPAddress));
	if(localIp.ss_family == AF_INET6)
	{
		Flags = 1;
		uint8_t *addr = reinterpret_cast<uint8_t*>(&(((sockaddr_in6*)(&localIp))->sin6_addr));
		HostIPAddress.assign(addr, addr + sizeof(in6_addr));
	}
	else
	{
		Flags = 0;
		uint8_t *addr = reinterpret_cast<uint8_t*>(&(((sockaddr_in*)(&localIp))->sin_addr));
		HostIPAddress.assign(addr, addr + sizeof(in_addr));
	}

	{
		std::lock_guard<std::mutex> l(_remoteAccessLock);
		_remoteAccessEnabledInAMT = false;
		try {
			Intel::MEI_Client::AMTHI_Client::SetEnterpriseAccessCommand setEnterpriseAccessCommand(Flags, HostIPAddress.data(), EnterpriseAccess);
			_remoteAccessEnabledInAMT = true;
			printf("Remote access is allowed. This state is deprecated.\n");
		}
		catch (Intel::MEI_Client::AMTHI_Client::AMTHIErrorException& e)
		{
			switch (e.getErr())
			{
			case PT_STATUS_REMOTE_ACCESS_NOT_GRANTED:
				UNS_DEBUG(L"Remote access is denied because AMT is directly connected "
					L"to enterprise network.");
				break;
			case PT_STATUS_REMOTE_ACCESS_HOST_VPN_IS_DISABLED:
				UNS_DEBUG(L"Remote access is disabled.", L"\n");
				break;
			case PT_STATUS_REMOTE_ACCESS_GRANTED_WITHOUT_DDNS:
				_remoteAccessEnabledInAMT = true;
				UNS_DEBUG(L"Remote access is allowed. No DNS update will be executed.", L"\n");
				break;
			case PT_STATUS_REMOTE_ACCESS_GRANTED_WITH_DDNS:
				_remoteAccessEnabledInAMT = true;
				UNS_DEBUG(L"Remote access is allowed. DNS update may be executed", L"\n");
				break;
			default:
				UNS_DEBUG(L"Remote access is disabled.", L"\n");
				break;
			}
		}
		catch (Intel::MEI_Client::MEIClientException e)
		{
			LMS_DEBUG_VAR(L"_checkRemoteSupport: _updateEnterpriseAccessStatus failed: %C", e.what());
		}
	}
}


#endif


int Protocol::_isLocalCallback(void *const param, SOCKET s, sockaddr_storage* caller_addr)
{
	Protocol *prot = (Protocol *)param;

	return prot->_isLocal(s, caller_addr);
}

int Protocol:: _isLocal(SOCKET s, sockaddr_storage* caller_addr) const
{
	int result = -1;
	FuncEntryExit<decltype(result)> fee(L"_isLocal", result);

	SuffixMap suffixMap;
	SuffixMap::iterator it;
	sockaddr_storage addrFromSocket;
	socklen_t addrLen = sizeof(addrFromSocket);
	memset(&addrFromSocket, 0, addrLen);

	if (caller_addr != NULL) {
		// both sides have the same size
		std::copy_n(reinterpret_cast<uint8_t*>(caller_addr), sizeof(sockaddr_storage),
			    reinterpret_cast<uint8_t*>(&addrFromSocket));
	} else if (getpeername(s, (sockaddr *)&addrFromSocket, &addrLen) != 0) {
		result = -1;
		goto out;
	}

	SS_PORT(&addrFromSocket) = 0;

	if (addrFromSocket.ss_family == AF_INET) {
		if (((sockaddr_in *)&addrFromSocket)->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
			result = 1;
			goto out;
		}
	}
	else if (addrFromSocket.ss_family == AF_INET6) {
		if (IN6_IS_ADDR_LOOPBACK(&((sockaddr_in6 *)&addrFromSocket)->sin6_addr)) {
			result = 1;
			goto out;
		}
	}
	else {
		return -1;
	}

	suffixMap = AdapterListInfo::GetLocalDNSSuffixList();
	it = suffixMap.find(addrFromSocket);

	if (it != suffixMap.end()) {
		result = 1; // found
		goto out;
	}
	
	// not found
	result = 0;

out:
	return result;
}

#ifdef _REMOTE_SUPPORT

int Protocol::_isRemoteCallback(void *const param, SOCKET s, sockaddr_storage* caller_addr)
{
	Protocol *prot = (Protocol *)param;

	return prot->_isRemote(s);
}

int Protocol::_isRemote(SOCKET s) const
{
	FuncEntryExit<void> fee(L"_isRemote");


	sockaddr_storage localAddr;
	memset(&localAddr, 0, sizeof(localAddr));

	socklen_t addrLen = sizeof(localAddr);

	if (getsockname(s, (sockaddr *)&localAddr, &addrLen) != 0) {
		return -1;
	}

	int result = 0;

	std::lock_guard<std::mutex> l(_remoteAccessLock);
	
	if (_remoteAccessEnabledInAMT) {	
		
		string dnsSuffix = AdapterListInfo::GetDNSSuffixFromLocalIP(localAddr);

		if (dnsSuffix.empty()) {
			return 0;
		}

		std::lock_guard<std::mutex> dns_lock(_AMTDNSLock);

		std::vector<std::string>::const_iterator it = 
			std::find_if(_AMTDNSSuffixes.begin(), _AMTDNSSuffixes.end(),
				[dnsSuffix](const std::string &a) { return CompareSuffix(a, dnsSuffix); });
		result = (it != _AMTDNSSuffixes.end());
	}

	return result;
}
#endif

int Protocol::_handleFQDNChange(const char *fqdn)
{
	FuncEntryExit<void> fee(L"_handleFQDNChange");
	std::string inFileName("");
	const std::string hostFile("hosts");
	const std::string sig("# LMS GENERATED LINE");
	bool hasFqdn = false, hasOldFqdn = false;

#ifdef WIN32
	size_t requiredSize;
	const std::string dir("\\system32\\drivers\\etc\\");

	getenv_s(&requiredSize, NULL, 0, "SystemRoot");
	if (requiredSize > MAX_PATH) {
		UNS_DEBUG(L"getenv_s asks for too much memory %d", L"\n", requiredSize);
		return -1;
	}

	std::vector<char> sysDrive(requiredSize);

	if (getenv_s(&requiredSize, &sysDrive[0], requiredSize, "SystemRoot") != 0)
		return -1;

	inFileName.assign(sysDrive.data());
#else
	const std::string dir("/etc/");
#endif	// WIN32

	inFileName += dir;
	inFileName += hostFile;

	std::ifstream ifp(inFileName);
	std::stringstream tmp;
	std::string line;
	char lastChar;

	if (!ifp.is_open()) {
		UNS_DEBUG(L"failed to open hosts file for reading", L"\n");
		goto HOSTS_FILE_ERR;
	}

	// First create a copy of the hosts file, without lines that were
	// previously added by the LMS.
	// Go over each line and copy it to the tmp file.
	while (std::getline(ifp, line)) {
		// don't copy the line if it was generated by the LMS
		if (line.find(sig) == std::string::npos) {
			tmp << line << std::endl;
		} else {
			// this is an LMS line, check the FQDN
			char oldFqdn[FQDN_MAX_SIZE + 1];

#ifdef WIN32
			if (sscanf_s(line.c_str(), "127.0.0.1       %256s #", oldFqdn, FQDN_MAX_SIZE + 1) != 1) {
#else
			if (sscanf(line.c_str(), "127.0.0.1       %256s #", oldFqdn) != 1) {
#endif // WIN32
				// Badly formatted line, even though it has our sig.
				// Copy it and continue.
				tmp << line << std::endl;
				continue;
			}
			hasOldFqdn = true; 

			if (strncmp(fqdn, oldFqdn, FQDN_MAX_SIZE) == 0) {
				// copy the old LMS line too, since it's up to date
				tmp << line << std::endl;
				hasFqdn = true;
			}
		}
	}

	// If there is already correct FQDN line
	// or we got empty fqdn and hosts file doesn't contain 
	// old fqdn entry then there's nothing to update
	if (hasFqdn || (!hasOldFqdn && fqdn[0] == 0)) {
		return 0;
	}

	// If the original hosts file does not end with a new line character,
	// add a new line at the end of the new file before adding our line.
	ifp.seekg(-1, std::ios_base::end);
	ifp.get(lastChar);
	if (lastChar != '\n') {
		tmp << std::endl;
	}

	// Add the specified FQDN to the end of the tmp file
	if ((fqdn != NULL) && (fqdn[0] != 0)) {
		tmp << "127.0.0.1       " << fqdn << " " << sig << std::endl;
	}

	ifp.close();

	{
		std::ofstream ofp(inFileName);
		if (!ofp.is_open()) {
			UNS_DEBUG(L"failed to open hosts file for writing", L"\n");

			goto HOSTS_FILE_ERR;
		}

		ofp << tmp.str();
		ofp.close();
	}
	_eventLogDbg(_eventLogParam, "Hosts file updated");
	_OutHostsFileLog = true;
	return 0;
HOSTS_FILE_ERR:
	if (_OutHostsFileLog)
	{
		_OutHostsFileLog = false;
		_eventLogWrn(_eventLogParam, "Hosts file update failed");
	}
	return -1;
}

int Protocol::_updateIPFQDN(const string &fqdn)
{
	FuncEntryExit<void> fee(L"_updateIPFQDN");
	// When fqdn is empty, _handleFQDNChange() will remove LMS line from hosts file.
	
	if (fqdn != _AMTFQDN) 
	{
		std::string localName;
		bool res = GetLocalFQDN(localName); // res is zero if failed to get a local FQDN
		size_t fqdnLen = fqdn.size();
		
		// If AMT FQDN is equal to local FQDN than we don't do anything
		if (localName.size() > fqdnLen)
			localName.resize(fqdnLen);

		if ((res == 0) ||
			(fqdn.length() != localName.length()) ||
			(_strnicmp(fqdn.c_str(), localName.c_str(), fqdn.length()) != 0))
		{
			if (_handleFQDNChange(fqdn.c_str()) < 0) {
				UNS_DEBUG(L"Error: failed to update FQDN info", L"\n");
				return -1;
			}
		}
		else {
			if (_handleFQDNChange("") < 0) {
				UNS_DEBUG(L"Error: failed to update FQDN info", L"\n");
				return -1;
			}
		}
	}

	_AMTFQDN = fqdn;

	LMS_DEBUG_VAR(L"Got FQDN: %C", _AMTFQDN.c_str());

	return 0;
}


LMEConnection& Protocol::GetLMEConnection()
{
	return _lme;
}