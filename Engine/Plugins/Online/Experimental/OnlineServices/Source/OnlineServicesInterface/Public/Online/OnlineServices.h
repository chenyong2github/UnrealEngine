// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Templates/SharedPointer.h"

class FString;

namespace UE::Online {

// Interfaces
using IOnlineServicesPtr = TSharedPtr<class IOnlineServices>;
using IAuthPtr = TSharedPtr<class IAuth>;
using IFriendsPtr = TSharedPtr<class IFriends>;
using IPresencePtr = TSharedPtr<class IPresence>;
using IExternalUIPtr = TSharedPtr<class IExternalUI>;
using ILobbiesPtr = TSharedPtr<class ILobbies>;

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
};

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

/* UE::Online */ }
