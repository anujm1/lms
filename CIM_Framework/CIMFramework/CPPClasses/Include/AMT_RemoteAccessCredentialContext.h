//----------------------------------------------------------------------------
//
//  Copyright (c) Intel Corporation, 2003 - 2012  All Rights Reserved.
//
//  File:       AMT_RemoteAccessCredentialContext.h
//
//  Contents:   Association between an instance of AMT_ManagementPresenceRemoteSAP and the credential it uses.
//
//              This file was automatically generated from AMT_RemoteAccessCredentialContext.mof,  version: 6.0.0
//
//----------------------------------------------------------------------------
#ifndef AMT_REMOTEACCESSCREDENTIALCONTEXT_H
#define AMT_REMOTEACCESSCREDENTIALCONTEXT_H 1
#include "CIM_CredentialContext.h"
namespace Intel
{
namespace Manageability
{
namespace Cim
{
namespace Typed 
{
	// Association between an instance of AMT_ManagementPresenceRemoteSAP and the credential it uses.
	class CIMFRAMEWORK_API AMT_RemoteAccessCredentialContext  : public CIM_CredentialContext
	{
	public:

		//Default constructor
		AMT_RemoteAccessCredentialContext()
			: CIM_CredentialContext(NULL, CLASS_NAME, CLASS_NS, CLASS_NS_PREFIX, CLASS_URI)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_CredentialContext::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 2);
			}
		}

		//constructor which receives WSMan client
		AMT_RemoteAccessCredentialContext(ICimWsmanClient *client)
			: CIM_CredentialContext(client, CLASS_NAME, CLASS_NS, CLASS_NS_PREFIX, CLASS_URI)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_CredentialContext::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 2);
			}
		}

		//Destructor
		virtual ~AMT_RemoteAccessCredentialContext(){}

		// The "type" information for the object. Gettors only.
		virtual const string& ResourceURI() const { return CLASS_URI; }
		static const string& ClassResourceURI() { return CLASS_URI; }
		virtual const string& XmlNamespace() const { return CLASS_NS; }
		virtual const string& XmlPrefix() const { return CLASS_NS_PREFIX; }
		virtual const string& ObjectType() const { return CLASS_NAME; }
		static const string& ClassObjectType() { return CLASS_NAME; }

		// Class representing AMT_RemoteAccessCredentialContext keys
		class CimKeys : public CIM_CredentialContext::CimKeys
		{
		public:
			// Key, Required, A certificate whose context is defined.
			const CimReference ElementInContext() const
			{
				CimReference ret;
				TypeConverter::StringToType(GetKey("ElementInContext"), ret);
				return ret;
			}

			// Key, Required, A certificate whose context is defined.
			void ElementInContext(const CimReference &value)
			{
				SetOrAddKey("ElementInContext", TypeConverter::TypeToString(value), true);
			}

			// Key, Required, The MpServer that provides context or scope to the Credential.
			const CimReference ElementProvidingContext() const
			{
				CimReference ret;
				TypeConverter::StringToType(GetKey("ElementProvidingContext"), ret);
				return ret;
			}

			// Key, Required, The MpServer that provides context or scope to the Credential.
			void ElementProvidingContext(const CimReference &value)
			{
				SetOrAddKey("ElementProvidingContext", TypeConverter::TypeToString(value), true);
			}

		};
		 // Function used by the factory
		static CimBase *CreateFromCimObject(const CimObject &object);

		static vector<shared_ptr<AMT_RemoteAccessCredentialContext> > Enumerate(ICimWsmanClient *client, const CimKeys &keys = CimKeys()) ;

		// Overloaded delete which supplies the internal resourceURI
		static void Delete(ICimWsmanClient *client, const CimKeys &keys = CimKeys()) ;

		using CimBase::Delete;

	protected:
		 // Protected constructor to be used by derived classes
		AMT_RemoteAccessCredentialContext(ICimWsmanClient *client, const string &class_name,
			const string &class_ns, const string &prefix, const string &uri): CIM_CredentialContext(client, class_name, class_ns, prefix, uri)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_CredentialContext::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 2);
			}
		}
		 // Protected constructor which receives CimObject
		AMT_RemoteAccessCredentialContext(const CimObject &object)
			: CIM_CredentialContext(object)
		{
			if(_classMetaData.size() == 0)
			{
				CIM_CredentialContext::SetMetaData(_classMetaData);
				CimBase::SetMetaData(_classMetaData, _metadata, 2);
			}
		}
		// Called by derived classes
		void SetMetaData(vector<CimFieldAttribute>& childMetaData)
		{
			CIM_CredentialContext::SetMetaData(childMetaData);
			CimBase::SetMetaData(childMetaData, _metadata, 2);
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
#endif // AMT_REMOTEACCESSCREDENTIALCONTEXT_H
