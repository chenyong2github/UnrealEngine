// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineUserInterface.h"

class FOnlineSubsystemEOSPlus;

/**
 * Unique net id wrapper for a EOS account ids. The underlying string is a combination
 * of both account ids concatenated. "<EOS_EpicAccountId>|<EOS_ProductAccountId>"
 */
class FUniqueNetIdEOSPlus :
	public FUniqueNetId
{
public:
	FUniqueNetIdEOSPlus()
	{
	}

	explicit FUniqueNetIdEOSPlus(TSharedPtr<const FUniqueNetId> InBaseUniqueNetId, TSharedPtr<const FUniqueNetId> InEOSUniqueNetId);

// FUniqueNetId interface
	virtual FName GetType() const override;
	virtual const uint8* GetBytes() const override;
	virtual int32 GetSize() const override;
	virtual bool IsValid() const override;
	virtual FString ToString() const override;
	virtual FString ToDebugString() const override;
// ~FUniqueNetId interface

private:
	TSharedPtr<const FUniqueNetId> BaseUniqueNetId;
	TSharedPtr<const FUniqueNetId> EOSUniqueNetId;
	TArray<uint8> RawBytes;
};

/**
 * Interface for combining platform users with EOS/EAS users
 */
class FOnlineUserEOSPlus :
	public IOnlineIdentity,
	public IOnlineUser
{
public:
	FOnlineUserEOSPlus() = delete;
	virtual ~FOnlineUserEOSPlus();

// IOnlineUser Interface
	virtual bool QueryUserInfo(int32 LocalUserNum, const TArray<TSharedRef<const FUniqueNetId>>& UserIds) override;
	virtual bool GetAllUserInfo(int32 LocalUserNum, TArray<TSharedRef<FOnlineUser>>& OutUsers) override;
	virtual TSharedPtr<FOnlineUser> GetUserInfo(int32 LocalUserNum, const FUniqueNetId& UserId) override;
	virtual bool QueryUserIdMapping(const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FOnQueryUserMappingComplete& Delegate = FOnQueryUserMappingComplete()) override;
	virtual bool QueryExternalIdMappings(const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FOnQueryExternalIdMappingsComplete& Delegate = FOnQueryExternalIdMappingsComplete()) override;
	virtual void GetExternalIdMappings(const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, TArray<TSharedPtr<const FUniqueNetId>>& OutIds) override;
	virtual TSharedPtr<const FUniqueNetId> GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId) override;
// ~IOnlineUser Interface

// IOnlineIdentity Interface
	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) override;
	virtual bool Logout(int32 LocalUserNum) override;
	virtual bool AutoLogin(int32 LocalUserNum) override;
	virtual TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const override;
	virtual TArray<TSharedPtr<FUserOnlineAccount>> GetAllUserAccounts() const override;
	virtual TSharedPtr<const FUniqueNetId> GetUniquePlayerId(int32 LocalUserNum) const override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(uint8* Bytes, int32 Size) override;
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) override;
	virtual ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const override;
	virtual ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const override;
	virtual FString GetPlayerNickname(int32 LocalUserNum) const override;
	virtual FString GetPlayerNickname(const FUniqueNetId& UserId) const override;
	virtual FString GetAuthToken(int32 LocalUserNum) const override;
	virtual void GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) override;
	virtual FString GetAuthType() const override;
	virtual void RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate) override;
	virtual FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const override;
// ~IOnlineIdentity Interface

PACKAGE_SCOPE:
	FOnlineUserEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem);

private:
	/** Reference to the owning EOS plus subsystem */
	FOnlineSubsystemEOSPlus* EOSPlus;

	IOnlineUserPtr BaseUserInterface;
	IOnlineIdentityPtr BaseIdentityInterface;
	IOnlineUserPtr EOSUserInterface;
	IOnlineIdentityPtr EOSIdentityInterface;

	FOnQueryUserInfoCompleteDelegate IntermediateOnQueryUserInfoCompleteDelegateHandle;
	FOnQueryUserInfoCompleteDelegate FinalOnQueryUserInfoCompleteDelegateHandle;

	void IntermediateOnQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& UserIds, const FString& ErrorStr);
	void FinalOnQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& UserIds, const FString& ErrorStr);
};

typedef TSharedPtr<FOnlineUserEOSPlus, ESPMode::ThreadSafe> FOnlineUserEOSPlusPtr;
