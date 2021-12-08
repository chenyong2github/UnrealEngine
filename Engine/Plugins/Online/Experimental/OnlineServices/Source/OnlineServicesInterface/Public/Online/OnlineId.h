// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/TypeHash.h"

#include "Online/OnlineServicesTypes.h"

namespace UE::Online {

/** Tags used as template argument to TOnlineIdHandle to make it a compile error to assign between id's of different types */
namespace OnlineIdHandleTags
{
	struct FAccount {};
	struct FSession {};
	struct FParty {};
}

/**
 * A handle to an id which uniquely identifies a persistent or transient online resource, i.e. account/session/party etc, within a given Online Services provider.
 * At most one id, and therefore one handle, exists for any given resource. The id and handle persist until the OnlineServices module is unloaded.
 * Passed to and returned from OnlineServices APIs.
 */ 
template<typename IdType>
class TOnlineIdHandle
{
public:
	TOnlineIdHandle() = default;
	TOnlineIdHandle(EOnlineServices Type, uint32 Handle)
	{
		check(Handle < 0xFF000000);
		Value = (Handle & 0x00FFFFFF) | (uint32(Type) << 24);
	}

	inline bool IsValid() const { return GetHandle() != 0; }

	EOnlineServices GetOnlineServicesType() const { return EOnlineServices(Value >> 24); }
	uint32 GetHandle() const { return Value & 0x00FFFFFF; }

	bool operator==(const TOnlineIdHandle& Other) const { return Value == Other.Value; }
	bool operator!=(const TOnlineIdHandle& Other) const { return Value != Other.Value; }

private:
	uint32 Value = uint32(EOnlineServices::Null) << 24;
};

using FOnlineAccountIdHandle = TOnlineIdHandle<OnlineIdHandleTags::FAccount>;

template<typename IdType>
inline uint32 GetTypeHash(const TOnlineIdHandle<IdType>& Handle)
{
	using ::GetTypeHash;
	return HashCombine(GetTypeHash(Handle.GetOnlineServicesType()), GetTypeHash(Handle.GetHandle()));
}

template<typename IdType>
inline void LexFromString(const TOnlineIdHandle<IdType>& Id, const TCHAR* String)
{
	// TODO: should instead just implement ParseOnlineExecParams
}

ONLINESERVICESINTERFACE_API FString ToLogString(const FOnlineAccountIdHandle& Id);

template<typename IdType>
class IOnlineIdRegistry
{
public:
	virtual FString ToLogString(const TOnlineIdHandle<IdType>& Handle) const = 0;
};

using IOnlineAccountIdRegistry = IOnlineIdRegistry<OnlineIdHandleTags::FAccount>;

class FOnlineIdRegistryRegistry
{
public:
	/**
	 * Get the FOnlineIdRegistryRegistry singleton
	 *
	 * @return The FOnlineIdRegistryRegistry singleton instance
	 */
	ONLINESERVICESINTERFACE_API static FOnlineIdRegistryRegistry& Get();

	/**
	 * Tear down the singleton instance
	 */
	ONLINESERVICESINTERFACE_API static void TearDown();

	/**
	 * Register a registry for a given OnlineServices implementation and TOnlineIdHandle type
	 *
	 * @param OnlineServices Services that the registry is for
	 * @param Registry the registry of online ids
	 * @param Priority Integer priority, allows an existing registry to be extended and registered with a higher priority so it is used instead
	 */
	ONLINESERVICESINTERFACE_API void RegisterAccountIdRegistry(EOnlineServices OnlineServices, IOnlineAccountIdRegistry* Registry, int32 Priority = 0);

	/**
	* Unregister a previously registered Id registry
	*
	* @param OnlineServices Services that the registry is for
	* @param Priority Integer priority, will be unregistered only if the priority matches the one that is registered
	*/
	ONLINESERVICESINTERFACE_API void UnregisterAccountIdRegistry(EOnlineServices OnlineServices, int32 Priority = 0);

	/**
	 * Get the account id registry
	 *
	 * @param OnlineServices Type of online services for the IOnlineServices instance
	 *
	 * @return The account id registry, or an invalid pointer if it is not registered
	 */
	ONLINESERVICESINTERFACE_API IOnlineAccountIdRegistry* GetAccountIdRegistry(EOnlineServices OnlineServices);

	~FOnlineIdRegistryRegistry() = default;
	FOnlineIdRegistryRegistry() = default;

private:
	struct FAccountIdRegistryAndPriority
	{
		FAccountIdRegistryAndPriority(IOnlineAccountIdRegistry* InRegistry, int32 InPriority)
			: Registry(InRegistry), Priority(InPriority) {}

		IOnlineAccountIdRegistry* Registry;
		int32 Priority;
	};

	TMap<EOnlineServices, FAccountIdRegistryAndPriority> AccountIdRegistries;

	friend class FLazySingleton;
};
/* UE::Online */ }
