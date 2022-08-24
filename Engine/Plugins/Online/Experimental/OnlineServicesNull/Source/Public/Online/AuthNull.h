// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/AuthCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class FOnlineAccountIdString
{
public:
	FString Data;
	int32 AccountIndex;
	FAccountId Handle;
};

class FOnlineAccountIdRegistryNull : public IOnlineAccountIdRegistry
{
public:
	static FOnlineAccountIdRegistryNull& Get();

	FAccountId Find(FString UserId) const;
	FAccountId Find(FPlatformUserId UserId) const;
	FAccountId Find(int32 UserIndex) const;

	FAccountId Create(FString UserId, FPlatformUserId PlatformUserId = PLATFORMUSERID_NONE);

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const FAccountId& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FAccountId& Handle) const override;
	virtual FAccountId FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineAccountIdRegistry

	virtual ~FOnlineAccountIdRegistryNull() = default;

private:
	const FOnlineAccountIdString* GetInternal(const FAccountId& Handle) const;

	// how much of these are actually necessary?
	TArray<FOnlineAccountIdString> Ids;
	TMap<FString, FOnlineAccountIdString> StringToId;
	TMap<FPlatformUserId, FOnlineAccountIdString> LocalUserMap;

};

struct FAccountInfoNull final : public FAccountInfo
{
};


// Auth NULL is implemented in a way similar to console platforms where there is not an explicit
// login / logout from online services. On those platforms the user account is picked either before
// the game has started or as a part of selecting an input device.
class ONLINESERVICESNULL_API FAccountInfoRegistryNULL final : public FAccountInfoRegistry
{
public:
	using Super = FAccountInfoRegistry;

	virtual ~FAccountInfoRegistryNULL() = default;

	TSharedPtr<FAccountInfoNull> Find(FPlatformUserId PlatformUserId) const;
	TSharedPtr<FAccountInfoNull> Find(FAccountId AccountIdHandle) const;

	void Register(const TSharedRef<FAccountInfoNull>&UserAuthData);
	void Unregister(FAccountId AccountId);
};

class ONLINESERVICESNULL_API FAuthNull : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	FAuthNull(FOnlineServicesNull& InOwningSubsystem);
	virtual void Initialize() override;
	virtual void PreShutdown() override;

protected:
	virtual const FAccountInfoRegistry& GetAccountInfoRegistry() const override;

	void InitializeUsers();
	void UninitializeUsers();
	void OnInputDeviceConnectionChange(EInputDeviceConnectionState NewConnectionState, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId);

	FAccountInfoRegistryNULL AccountInfoRegistryNULL;
};

/* UE::Online */ }
