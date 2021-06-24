// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace UE::Online {

enum class EOnlineServices : uint8
{
	// Null, Providing minimal functionality when no backend services are required
	Null,
	// Epic Online Services
	Epic,
	// Xbox services
	Xbox,
	// PlayStation Network
	PSN,
	// Nintendo
	Nintendo,
	// Stadia,
	Stadia,
	// Steam
	Steam,
	// Google
	Google,
	// GooglePlay
	GooglePlay,
	// Apple
	Apple,
	// GameKit
	AppleGameKit,
	// Samsung
	Samsung,
	// Oculus
	Oculus,
	// Tencent
	Tencent,
	// Reserved for future use/platform extensions
	Reserved_14,
	Reserved_15,
	Reserved_16,
	Reserved_17,
	Reserved_18,
	Reserved_19,
	Reserved_20,
	Reserved_21,
	Reserved_22,
	Reserved_23,
	Reserved_24,
	Reserved_25,
	Reserved_26,
	Reserved_27,
	// For game specific Online Services
	GameDefined_0 = 28,
	GameDefined_1,
	GameDefined_2,
	GameDefined_3,
	// Platform native, may not exist for all platforms
	Platform = 254,
	// Default, configured via ini, TODO: List specific ini section/key
	Default = 255
};

// Interfaces
using IAuthPtr = TSharedPtr<class IAuth>;
using IFriendsPtr = TSharedPtr<class IFriends>;

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
};

/**
 * Get an instance of the online subsystem
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
 * Destroy an instance of the online subsystem
 *
 * @param OnlineServices Type of online services to destroy
 * @param InstanceName Name of the services instance to destroy
 */
ONLINESERVICESINTERFACE_API void DestroyServices(EOnlineServices OnlineServices = EOnlineServices::Default, FName InstanceName = NAME_None);


/* UE::Online */ }
