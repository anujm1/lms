//----------------------------------------------------------------------------
//
//  Copyright (c) Intel Corporation, 2003 - 2012  All Rights Reserved.
//
//  File:       IPS_IPv6PortSettings.h
//
//  Contents:   Intel(R) AMT IPv6 settings.
//
//              This file was automatically generated from IPS_IPv6PortSettings.mof,  version: 6.0.0
//
//----------------------------------------------------------------------------
#ifndef IPS_IPV6PORTSETTINGS_H
#define IPS_IPV6PORTSETTINGS_H 1
#include "CIM_SettingData.h"
namespace Intel
{
namespace Manageability
{
namespace Cim
{
namespace Typed 
{
	// Intel(R) AMT IPv6 settings.
	class CIMFRAMEWORK_API IPS_IPv6PortSettings  : public CIM_SettingData
	{
	public:

		//Default constructor
		IPS_IPv6PortSettings()
			: CIM_SettingData(NULL, CLASS_NAME, CLASS_NS, CLASS_NS_PREFIX, CLASS_URI)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_SettingData::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 11);
			}
		}

		//constructor which receives WSMan client
		IPS_IPv6PortSettings(ICimWsmanClient *client)
			: CIM_SettingData(client, CLASS_NAME, CLASS_NS, CLASS_NS_PREFIX, CLASS_URI)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_SettingData::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 11);
			}
		}

		//Destructor
		virtual ~IPS_IPv6PortSettings(){}

		// The "type" information for the object. Gettors only.
		virtual const string& ResourceURI() const { return CLASS_URI; }
		static const string& ClassResourceURI() { return CLASS_URI; }
		virtual const string& XmlNamespace() const { return CLASS_NS; }
		virtual const string& XmlPrefix() const { return CLASS_NS_PREFIX; }
		virtual const string& ObjectType() const { return CLASS_NAME; }
		static const string& ClassObjectType() { return CLASS_NAME; }

		// Class representing IPS_IPv6PortSettings keys
		class CimKeys : public CIM_SettingData::CimKeys
		{
		public:
			// Key, Required, Intel(R) IPS IPv6 Settings <d>, where <d> is incremented for every existing instance. This key property cannot be modified after creation.
			const string InstanceID() const
			{
				return GetKey("InstanceID");
			}

			// Key, Required, Intel(R) IPS IPv6 Settings <d>, where <d> is incremented for every existing instance. This key property cannot be modified after creation.
			void InstanceID(const string &value)
			{
				SetOrAddKey("InstanceID", value);
			}

		};

		// class fields declarations

		// Optional, Interface ID Type.
		// "Randomized": the auto configured address will be based on random interface ID (RFC: 3041).
		// "Intel MAC based": the auto configured address will be based on an interface ID generated by appending 0x8086 to the interface MAC address.
		// "Manual": the auto configured address will be based on user set interface ID. Selecting this type requires that ManualInterfaceID is set with a valid value
		const unsigned int InterfaceIDType() const;

		// Optional, Interface ID Type.
		// "Randomized": the auto configured address will be based on random interface ID (RFC: 3041).
		// "Intel MAC based": the auto configured address will be based on an interface ID generated by appending 0x8086 to the interface MAC address.
		// "Manual": the auto configured address will be based on user set interface ID. Selecting this type requires that ManualInterfaceID is set with a valid value
		void InterfaceIDType(const unsigned int value); 

		// Is true if the field InterfaceIDType exists in the current object, otherwise is false.
		bool InterfaceIDTypeExists() const;

		// Remove InterfaceIDType field.
		void RemoveInterfaceIDType(); 

		// Optional, On write, this setting will be stored, however used only when InterfaceIDType is manual. On read, this parameter represents the manually set InterfaceID and not the currently configured interface ID unless the InterfaceIDType is set to Manual. The currently configured interface ID is apparent in the interface's link local address FE80::<Interface ID>. On write, this value should be unique among objects of this type. If not, it may result in two network interfaces with the same IP address. 
		const HexBinary ManualInterfaceID() const;

		// Optional, On write, this setting will be stored, however used only when InterfaceIDType is manual. On read, this parameter represents the manually set InterfaceID and not the currently configured interface ID unless the InterfaceIDType is set to Manual. The currently configured interface ID is apparent in the interface's link local address FE80::<Interface ID>. On write, this value should be unique among objects of this type. If not, it may result in two network interfaces with the same IP address. 
		void ManualInterfaceID(const HexBinary &value); 

		// Is true if the field ManualInterfaceID exists in the current object, otherwise is false.
		bool ManualInterfaceIDExists() const;

		// Remove ManualInterfaceID field.
		void RemoveManualInterfaceID(); 

		// Optional, A static IPv6 address. Will be configured in parallel to the auto-configured ipv6 addresses.Unset this parameter by passing "::0"
		// This parameter is relevant for wired interface.
		const string IPv6Address() const;

		// Optional, A static IPv6 address. Will be configured in parallel to the auto-configured ipv6 addresses.Unset this parameter by passing "::0"
		// This parameter is relevant for wired interface.
		void IPv6Address(const string &value); 

		// Is true if the field IPv6Address exists in the current object, otherwise is false.
		bool IPv6AddressExists() const;

		// Remove IPv6Address field.
		void RemoveIPv6Address(); 

		// Optional, Used to describe a static Primary DNS Address (IPv6).
		// Used only if the DNS IPv6 addresses were not configured by DHCPv6.
		// Relevant only for wired interface.
		const string PrimaryDNS() const;

		// Optional, Used to describe a static Primary DNS Address (IPv6).
		// Used only if the DNS IPv6 addresses were not configured by DHCPv6.
		// Relevant only for wired interface.
		void PrimaryDNS(const string &value); 

		// Is true if the field PrimaryDNS exists in the current object, otherwise is false.
		bool PrimaryDNSExists() const;

		// Remove PrimaryDNS field.
		void RemovePrimaryDNS(); 

		// Optional, Used to describe a static Secondary DNS Address (IPv6).
		// Used only if the DNS IPv6 addresses were not configured by DHCPv6.
		// Relevant only for wired interface.
		const string SecondaryDNS() const;

		// Optional, Used to describe a static Secondary DNS Address (IPv6).
		// Used only if the DNS IPv6 addresses were not configured by DHCPv6.
		// Relevant only for wired interface.
		void SecondaryDNS(const string &value); 

		// Is true if the field SecondaryDNS exists in the current object, otherwise is false.
		bool SecondaryDNSExists() const;

		// Remove SecondaryDNS field.
		void RemoveSecondaryDNS(); 

		// Optional, Used to describe the Default router Address (IPv6).
		// Used only if default router was not auto-configured via router advertisements.
		// Relevant only for wired interface.
		const string DefaultRouter() const;

		// Optional, Used to describe the Default router Address (IPv6).
		// Used only if default router was not auto-configured via router advertisements.
		// Relevant only for wired interface.
		void DefaultRouter(const string &value); 

		// Is true if the field DefaultRouter exists in the current object, otherwise is false.
		bool DefaultRouterExists() const;

		// Remove DefaultRouter field.
		void RemoveDefaultRouter(); 

		// Optional, Contains formatted strings of configured addresses.
		// The format of each string is as follows: %IPv6Addr%,%AddrType%,%AddrState%
		// Possible values for %AddrType%: 0 - Address generated from: fe80::/64 + Interface ID, 1 - Address generated from: [Router advertised prefix] + [Interface ID], 2 - Global IPv6 address obtained from DHCPv6 Server, 3 - IPv6 unicast address configured by the user, 4 - Returned by FW for IPv6 addresses that are not owned by FW (such as IPv6 router address, configuration server address etc.)
		// Possible values for %AddrState%: 0 - DAD is still in process for this address, 1 - Address is valid and may be used for new communication, 2 - Address is deprecated and should not be used for new communication, 3 - Covers both the preferred and deprecated states, 4 - The valid lifetime of the address has expired, 5 - An interface ID collision has been detected for this address when performing DAD, 6 - Returned by FW for IPv6 addresses that are not owned by FW (such as IPv6 router address, configuration server address etc.
		// Note: when setting class properties with PUT, the current address info returned will indicate the IPv6 settings that were valid before the command invocation, as the new settings are not applied until the response is returned to the caller.
		const vector<string> CurrentAddressInfo() const;

		// Optional, Contains formatted strings of configured addresses.
		// The format of each string is as follows: %IPv6Addr%,%AddrType%,%AddrState%
		// Possible values for %AddrType%: 0 - Address generated from: fe80::/64 + Interface ID, 1 - Address generated from: [Router advertised prefix] + [Interface ID], 2 - Global IPv6 address obtained from DHCPv6 Server, 3 - IPv6 unicast address configured by the user, 4 - Returned by FW for IPv6 addresses that are not owned by FW (such as IPv6 router address, configuration server address etc.)
		// Possible values for %AddrState%: 0 - DAD is still in process for this address, 1 - Address is valid and may be used for new communication, 2 - Address is deprecated and should not be used for new communication, 3 - Covers both the preferred and deprecated states, 4 - The valid lifetime of the address has expired, 5 - An interface ID collision has been detected for this address when performing DAD, 6 - Returned by FW for IPv6 addresses that are not owned by FW (such as IPv6 router address, configuration server address etc.
		// Note: when setting class properties with PUT, the current address info returned will indicate the IPv6 settings that were valid before the command invocation, as the new settings are not applied until the response is returned to the caller.
		void CurrentAddressInfo(const vector<string> &value); 

		// Is true if the field CurrentAddressInfo exists in the current object, otherwise is false.
		bool CurrentAddressInfoExists() const;

		// Remove CurrentAddressInfo field.
		void RemoveCurrentAddressInfo(); 

		// Optional, Used to describe the currently used primary DNS Address (IPv6).
		const string CurrentPrimaryDNS() const;

		// Optional, Used to describe the currently used primary DNS Address (IPv6).
		void CurrentPrimaryDNS(const string &value); 

		// Is true if the field CurrentPrimaryDNS exists in the current object, otherwise is false.
		bool CurrentPrimaryDNSExists() const;

		// Remove CurrentPrimaryDNS field.
		void RemoveCurrentPrimaryDNS(); 

		// Optional, Used to describe the currently used secondary DNS Address (IPv6).
		const string CurrentSecondaryDNS() const;

		// Optional, Used to describe the currently used secondary DNS Address (IPv6).
		void CurrentSecondaryDNS(const string &value); 

		// Is true if the field CurrentSecondaryDNS exists in the current object, otherwise is false.
		bool CurrentSecondaryDNSExists() const;

		// Remove CurrentSecondaryDNS field.
		void RemoveCurrentSecondaryDNS(); 

		// Optional, Used to describe the currently used default router Address (IPv6).
		const string CurrentDefaultRouter() const;

		// Optional, Used to describe the currently used default router Address (IPv6).
		void CurrentDefaultRouter(const string &value); 

		// Is true if the field CurrentDefaultRouter exists in the current object, otherwise is false.
		bool CurrentDefaultRouterExists() const;

		// Remove CurrentDefaultRouter field.
		void RemoveCurrentDefaultRouter(); 

		 // Function used by the factory
		static CimBase *CreateFromCimObject(const CimObject &object);

		static vector<shared_ptr<IPS_IPv6PortSettings> > Enumerate(ICimWsmanClient *client, const CimKeys &keys = CimKeys()) ;

		// Overloaded delete which supplies the internal resourceURI
		static void Delete(ICimWsmanClient *client, const CimKeys &keys = CimKeys()) ;

		using CimBase::Delete;

	protected:
		 // Protected constructor to be used by derived classes
		IPS_IPv6PortSettings(ICimWsmanClient *client, const string &class_name,
			const string &class_ns, const string &prefix, const string &uri): CIM_SettingData(client, class_name, class_ns, prefix, uri)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_SettingData::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 11);
			}
		}
		 // Protected constructor which receives CimObject
		IPS_IPv6PortSettings(const CimObject &object)
			: CIM_SettingData(object)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_SettingData::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 11);
			}
		}
		// Called by derived classes
		void SetMetaData(vector<CimFieldAttribute>& childMetaData)
		{
			CIM_SettingData::SetMetaData(childMetaData);
			CimBase::SetMetaData(childMetaData, _metadata, 11);
		}
		const vector<CimFieldAttribute> &GetMetaData() const;
	private:
		static const CimFieldAttribute _metadata[];
		static const string CLASS_NAME;
		static const string CLASS_URI;
		static const string CLASS_NS;
		static const string CLASS_NS_PREFIX;
		static vector<CimFieldAttribute> _classMetaData;
	};

} // close namespace Typed
} // close namespace Cim
} // close namespace Manageability
} // close namespace Intel
#endif // IPS_IPV6PORTSETTINGS_H