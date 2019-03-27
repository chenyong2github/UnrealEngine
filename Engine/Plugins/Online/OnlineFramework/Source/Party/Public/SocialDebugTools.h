// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "SocialTypes.h"
#include "SocialDebugTools.generated.h"

class IOnlineSubsystem;
class IOnlinePartyJoinInfo;
class FOnlineAccountCredentials;
class FOnlinePartyData;

UCLASS(Within = SocialManager, Config = Game)
class PARTY_API USocialDebugTools : public UObject, public FExec
{
	GENERATED_BODY()

	static const int32 LocalUserNum = 0;

public:
	// FExec
	virtual bool Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Out) override;

	// USocialDebugTools

	USocialDebugTools();
	virtual void Shutdown();

	DECLARE_DELEGATE_OneParam(FLoginComplete, bool);
	virtual void Login(const FString& Instance, const FOnlineAccountCredentials& Credentials, const FLoginComplete& OnComplete);

	DECLARE_DELEGATE_OneParam(FLogoutComplete, bool);
	virtual void Logout(const FString& Instance, const FLogoutComplete& OnComplete);

	DECLARE_DELEGATE_OneParam(FJoinPartyComplete, bool);
	virtual void JoinParty(const FString& Instance, const FString& FriendName, const FJoinPartyComplete& OnComplete);

	DECLARE_DELEGATE_OneParam(FLeavePartyComplete, bool);
	virtual void LeaveParty(const FString& Instance, const FLeavePartyComplete& OnComplete);

	DECLARE_DELEGATE_OneParam(FSetPartyMemberDataComplete, bool);
	virtual void SetPartyMemberData(const FString& Instance, const UStruct* StructType, const void* StructData, const FSetPartyMemberDataComplete& OnComplete);
	virtual void SetPartyMemberDataJson(const FString& Instance, const FString& JsonStr, const FSetPartyMemberDataComplete& OnComplete);

	virtual void GetContextNames(TArray<FString>& OutContextNames) const { Contexts.GenerateKeyArray(OutContextNames); }

	struct FInstanceContext
	{
		FInstanceContext(const FString& InstanceName, USocialDebugTools& SocialDebugTools)
			: Name(InstanceName)
			, OnlineSub(nullptr)
			, Owner(SocialDebugTools)
		{}

		void Init();
		void Shutdown();
		inline IOnlineSubsystem* GetOSS() { return OnlineSub; }
		inline TSharedPtr<FOnlinePartyData> GetPartyMemberData() { return PartyMemberData; }

		FString Name;
		IOnlineSubsystem* OnlineSub;
		USocialDebugTools& Owner;
		TSharedPtr<FOnlinePartyData> PartyMemberData;

		// delegates
		FDelegateHandle LoginCompleteDelegateHandle;
		FDelegateHandle LogoutCompleteDelegateHandle;
		FDelegateHandle PresenceReceivedDelegateHandle;
		FDelegateHandle FriendInviteReceivedDelegateHandle;
		FDelegateHandle PartyInviteReceivedDelegateHandle;
	};

	FInstanceContext& GetContext(const FString& Instance);
	FInstanceContext* GetContextForUser(const FUniqueNetId& UserId);
private:

	bool bAutoAcceptFriendInvites;
	bool bAutoAcceptPartyInvites;

	TMap<FString, FInstanceContext> Contexts;

	TSharedPtr<IOnlinePartyJoinInfo> GetDefaultPartyJoinInfo() const;
	IOnlineSubsystem* GetDefaultOSS() const;
	void PrintExecUsage();
	void PrintExecCommands();

	// OSS callback handlers
	void HandleFriendInviteReceived(const FUniqueNetId& LocalUserId, const FUniqueNetId& FriendId);
	void HandlePartyInviteReceived(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& SenderId);
};
