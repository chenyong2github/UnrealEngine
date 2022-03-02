// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Templates/SharedPointer.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineMeta.h"

class FString;

namespace UE::Online {

// Interfaces
using IOnlineServicesPtr = TSharedPtr<class IOnlineServices>;
using IAuthPtr = TSharedPtr<class IAuth>;
using IFriendsPtr = TSharedPtr<class IFriends>;
using IPresencePtr = TSharedPtr<class IPresence>;
using IExternalUIPtr = TSharedPtr<class IExternalUI>;
using ILobbiesPtr = TSharedPtr<class ILobbies>;
using IConnectivityPtr = TSharedPtr<class IConnectivity>;
using IPrivilegesPtr = TSharedPtr<class IPrivileges>;

struct FGetResolvedConnectString
{
	static constexpr TCHAR Name[] = TEXT("GetResolvedConnectString");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
		FOnlineLobbyIdHandle LobbyId;
		FName PortType;
	};

	struct Result
	{
		FString ResolvedConnectString;
	};
};

class ONLINESERVICESINTERFACE_API IOnlineServices
{
public:
	/**
	 *
	 */
	virtual void Init() = 0;

	/**
	 *
	 */
	virtual void Destroy() = 0;
	
	/**
	 *
	 */
	virtual IAuthPtr GetAuthInterface() = 0;

	/**
	 *
	 */
	virtual IFriendsPtr GetFriendsInterface() = 0;

	/**
	 *
	 */
	virtual IPresencePtr GetPresenceInterface() = 0;

	/**
	 *
	 */
	virtual IExternalUIPtr GetExternalUIInterface() = 0;

	/**
	 * Get the lobbies implementation
	 * @return lobbies implementation, may be null if not implemented for this service
	 */
	virtual ILobbiesPtr GetLobbiesInterface() = 0;

	/**
	 * 
	 */
	virtual IConnectivityPtr GetConnectivityInterface() = 0;

	/**
	 *
	 */
	virtual IPrivilegesPtr GetPrivilegesInterface() = 0;

	/** 
	 * Get the connectivity string used for client travel
	 */
	virtual TOnlineResult<FGetResolvedConnectString> GetResolvedConnectString(FGetResolvedConnectString::Params&& Params) = 0;

	/**
	 * Get the provider type of this instance.
	 * @return provider type
	 */
	virtual EOnlineServices GetServicesProvider() const = 0;
};

/**
 * Retrieve the unique id for the executable build. The build id is used for ensuring that client
 * and server builds are compatible.
 *
 * @return The unique id for the executable build.
 */
ONLINESERVICESINTERFACE_API int32 GetBuildUniqueId();

/**
 * Check if an instance of the online service is loaded
 *
 * @param OnlineServices Type of online services to retrieve
 * @param InstanceName Name of the services instance to retrieve
 * @return The services instance or an invalid pointer if the services is unavailable
 */
ONLINESERVICESINTERFACE_API bool IsLoaded(EOnlineServices OnlineServices = EOnlineServices::Default, FName InstanceName = NAME_None);

/**
 * Get an instance of the online service
 *
 * @param OnlineServices Type of online services to retrieve
 * @param InstanceName Name of the services instance to retrieve
 * @return The services instance or an invalid pointer if the services is unavailable
 */
ONLINESERVICESINTERFACE_API TSharedPtr<IOnlineServices> GetServices(EOnlineServices OnlineServices = EOnlineServices::Default, FName InstanceName = NAME_None);

/**
 * Get a specific services type and cast to the specific services type
 *
 * @param InstanceName Name of the services instance to retrieve
 * @return The services instance or an invalid pointer if the services is unavailable
 */
template <typename ServicesClass>
TSharedPtr<ServicesClass> GetServices(FName InstanceName = NAME_None)
{
	return StaticCastSharedPtr<ServicesClass>(GetServices(ServicesClass::GetServicesProvider(), InstanceName));
}

/**
 * Destroy an instance of the online service
 *
 * @param OnlineServices Type of online services to destroy
 * @param InstanceName Name of the services instance to destroy
 */
ONLINESERVICESINTERFACE_API void DestroyServices(EOnlineServices OnlineServices = EOnlineServices::Default, FName InstanceName = NAME_None);

namespace Meta {
// TODO: Move to OnlineServices_Meta.inl file?

BEGIN_ONLINE_STRUCT_META(FGetResolvedConnectString::Params)
	ONLINE_STRUCT_FIELD(FGetResolvedConnectString::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FGetResolvedConnectString::Params, LobbyId),
	ONLINE_STRUCT_FIELD(FGetResolvedConnectString::Params, PortType)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FGetResolvedConnectString::Result)
	ONLINE_STRUCT_FIELD(FGetResolvedConnectString::Result, ResolvedConnectString)
END_ONLINE_STRUCT_META()

/* Meta*/}

/* UE::Online */ }
