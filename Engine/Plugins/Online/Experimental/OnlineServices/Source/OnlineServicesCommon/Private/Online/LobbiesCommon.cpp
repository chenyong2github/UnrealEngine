// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/LobbiesCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

template <typename DataType, typename OpType>
const DataType& GetOpDataChecked(const TOnlineAsyncOp<OpType>& Op, const FString& Key)
{
	const DataType* Data = Op.Data.template Get<DataType>(Key);
	check(Data);
	return *Data;
}

TSharedPtr<FLobbySchema> FLobbySchema::Create(FLobbySchemaConfig LobbySchemaConfig)
{
	TSharedRef<FLobbySchema> Schema = MakeShared<FLobbySchema>();
	Schema->SchemaId = LobbySchemaConfig.SchemaName;

	// Todo: for real validation and init
	if (Schema->SchemaId == FLobbySchemaId())
	{
		return TSharedPtr<FLobbySchema>();
	}
	else
	{
		return Schema;
	}
}

TSharedRef<FLobbyServiceAttributeChanges> FLobbySchema::TranslateLobbyAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyServiceAttributeChanges> Changes = MakeShared<FLobbyServiceAttributeChanges>();
	Changes->MutatedAttributes = ClientAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ClientAttributeChanges.ClearedAttributes;
	return Changes;
}

TSharedRef<FLobbyClientAttributeChanges> FLobbySchema::TranslateLobbyAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyClientAttributeChanges> Changes = MakeShared<FLobbyClientAttributeChanges>();
	Changes->MutatedAttributes = ServiceAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ServiceAttributeChanges.ClearedAttributes;
	return Changes;
}

TSharedRef<FLobbyServiceAttributeChanges> FLobbySchema::TranslateLobbyMemberAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyServiceAttributeChanges> Changes = MakeShared<FLobbyServiceAttributeChanges>();
	Changes->MutatedAttributes = ClientAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ClientAttributeChanges.ClearedAttributes;
	return Changes;
}

TSharedRef<FLobbyClientAttributeChanges> FLobbySchema::TranslateLobbyMemberAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyClientAttributeChanges> Changes = MakeShared<FLobbyClientAttributeChanges>();
	Changes->MutatedAttributes = ServiceAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ServiceAttributeChanges.ClearedAttributes;
	return Changes;
}

bool FLobbySchemaRegistry::Initialize(TArray<FLobbySchemaConfig> LobbySchemaConfigs)
{
	bool bSuccess = true;

	for (FLobbySchemaConfig& LobbySchemaConfig : LobbySchemaConfigs)
	{
		bSuccess &= RegisterSchema(MoveTemp(LobbySchemaConfig));
	}

	return bSuccess;
}

TSharedPtr<FLobbySchema> FLobbySchemaRegistry::FindSchema(FLobbySchemaId SchemaId)
{
	TSharedRef<FLobbySchema>* Schema = RegisteredSchemas.Find(SchemaId);
	return Schema ? *Schema : TSharedPtr<FLobbySchema>();
}

bool FLobbySchemaRegistry::RegisterSchema(FLobbySchemaConfig LobbySchemaConfig)
{
	// Todo: handle schema hierarchy.
	// Todo: handle schema parsing.

	FLobbySchemaId SchemaName = LobbySchemaConfig.SchemaName;

	if (TSharedPtr<FLobbySchema> Schema = FLobbySchema::Create(MoveTemp(LobbySchemaConfig)))
	{
		RegisteredSchemas.Emplace(SchemaName, Schema.ToSharedRef());
		return true;
	}
	else
	{
		return false;
	}
}

FLobbiesCommon::FLobbiesCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Lobbies"), InServices)
{
}

void FLobbiesCommon::Initialize()
{
	TOnlineComponent<ILobbies>::Initialize();

	LobbySchemaRegistry = MakeShared<FLobbySchemaRegistry>();
	ServiceSchema = MakeShared<FLobbySchema>();

	FLobbyConfig LobbyConfig;
	// Todo: LoadConfig(LobbyConfig);

	if (LobbyConfig.RegisteredSchemas.Num() > 0)
	{
		TArray<FLobbySchemaConfig> LobbySchemaConfigs;
		LobbySchemaConfigs.Reserve(LobbyConfig.RegisteredSchemas.Num());

		for (FLobbySchemaId LobbySchemaId : LobbyConfig.RegisteredSchemas)
		{
			FLobbySchemaConfig LobbySchemaConfig;
			// Todo: LoadConfig(LobbySchemaConfig, LobbySchemaId.ToString());
			LobbySchemaConfigs.Add(MoveTemp(LobbySchemaConfig));
		}

		if (!LobbySchemaRegistry->Initialize(MoveTemp(LobbySchemaConfigs)))
		{
			// Todo: Log error.
		}
	}
	else
	{
		// Todo: Log error.
	}
}

void FLobbiesCommon::RegisterCommands()
{
	TOnlineComponent<ILobbies>::RegisterCommands();

	RegisterCommand(&FLobbiesCommon::CreateLobby);
	RegisterCommand(&FLobbiesCommon::FindLobby);
	RegisterCommand(&FLobbiesCommon::RestoreLobbies);
	RegisterCommand(&FLobbiesCommon::JoinLobby);
	RegisterCommand(&FLobbiesCommon::LeaveLobby);
	RegisterCommand(&FLobbiesCommon::InviteLobbyMember);
	RegisterCommand(&FLobbiesCommon::DeclineLobbyInvitation);
	RegisterCommand(&FLobbiesCommon::KickLobbyMember);
	RegisterCommand(&FLobbiesCommon::PromoteLobbyMember);
	RegisterCommand(&FLobbiesCommon::ModifyLobbySchema);
	RegisterCommand(&FLobbiesCommon::ModifyLobbyJoinPolicy);
	RegisterCommand(&FLobbiesCommon::ModifyLobbyAttributes);
	RegisterCommand(&FLobbiesCommon::ModifyLobbyMemberAttributes);
	RegisterCommand(&FLobbiesCommon::GetJoinedLobbies);
	RegisterCommand(&FLobbiesCommon::GetReceivedInvitations);

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
	RegisterCommand(&FLobbiesCommon::FunctionalTest);
#endif
}

TOnlineAsyncOpHandle<FCreateLobby> FLobbiesCommon::CreateLobby(FCreateLobby::Params&& Params)
{
	TOnlineAsyncOpRef<FCreateLobby> Operation = GetOp<FCreateLobby>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FFindLobby> FLobbiesCommon::FindLobby(FFindLobby::Params&& Params)
{
	TOnlineAsyncOpRef<FFindLobby> Operation = GetOp<FFindLobby>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FRestoreLobbies> FLobbiesCommon::RestoreLobbies(FRestoreLobbies::Params&& Params)
{
	TOnlineAsyncOpRef<FRestoreLobbies> Operation = GetOp<FRestoreLobbies>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FJoinLobby> FLobbiesCommon::JoinLobby(FJoinLobby::Params&& Params)
{
	TOnlineAsyncOpRef<FJoinLobby> Operation = GetOp<FJoinLobby>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FLeaveLobby> FLobbiesCommon::LeaveLobby(FLeaveLobby::Params&& Params)
{
	TOnlineAsyncOpRef<FLeaveLobby> Operation = GetOp<FLeaveLobby>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FInviteLobbyMember> FLobbiesCommon::InviteLobbyMember(FInviteLobbyMember::Params&& Params)
{
	TOnlineAsyncOpRef<FInviteLobbyMember> Operation = GetOp<FInviteLobbyMember>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FDeclineLobbyInvitation> FLobbiesCommon::DeclineLobbyInvitation(FDeclineLobbyInvitation::Params&& Params)
{
	TOnlineAsyncOpRef<FDeclineLobbyInvitation> Operation = GetOp<FDeclineLobbyInvitation>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FKickLobbyMember> FLobbiesCommon::KickLobbyMember(FKickLobbyMember::Params&& Params)
{
	TOnlineAsyncOpRef<FKickLobbyMember> Operation = GetOp<FKickLobbyMember>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FPromoteLobbyMember> FLobbiesCommon::PromoteLobbyMember(FPromoteLobbyMember::Params&& Params)
{
	TOnlineAsyncOpRef<FPromoteLobbyMember> Operation = GetOp<FPromoteLobbyMember>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FModifyLobbySchema> FLobbiesCommon::ModifyLobbySchema(FModifyLobbySchema::Params&& Params)
{
	TOnlineAsyncOpRef<FModifyLobbySchema> Operation = GetOp<FModifyLobbySchema>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FModifyLobbyJoinPolicy> FLobbiesCommon::ModifyLobbyJoinPolicy(FModifyLobbyJoinPolicy::Params&& Params)
{
	TOnlineAsyncOpRef<FModifyLobbyJoinPolicy> Operation = GetOp<FModifyLobbyJoinPolicy>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FModifyLobbyAttributes> FLobbiesCommon::ModifyLobbyAttributes(FModifyLobbyAttributes::Params&& Params)
{
	TOnlineAsyncOpRef<FModifyLobbyAttributes> Operation = GetOp<FModifyLobbyAttributes>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FModifyLobbyMemberAttributes> FLobbiesCommon::ModifyLobbyMemberAttributes(FModifyLobbyMemberAttributes::Params&& Params)
{
	TOnlineAsyncOpRef<FModifyLobbyMemberAttributes> Operation = GetOp<FModifyLobbyMemberAttributes>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FGetJoinedLobbies> FLobbiesCommon::GetJoinedLobbies(FGetJoinedLobbies::Params&& Params)
{
	return TOnlineResult<FGetJoinedLobbies>(Errors::NotImplemented());
}

TOnlineResult<FGetReceivedInvitations> FLobbiesCommon::GetReceivedInvitations(FGetReceivedInvitations::Params&& Params)
{
	return TOnlineResult<FGetReceivedInvitations>(Errors::NotImplemented());
}

TOnlineEvent<void(const FLobbyJoined&)> FLobbiesCommon::OnLobbyJoined()
{
	return OnLobbyJoinedEvent;
}

TOnlineEvent<void(const FLobbyLeft&)> FLobbiesCommon::OnLobbyLeft()
{
	return OnLobbyLeftEvent;
}

TOnlineEvent<void(const FLobbyMemberJoined&)> FLobbiesCommon::OnLobbyMemberJoined()
{
	return OnLobbyMemberJoinedEvent;
}

TOnlineEvent<void(const FLobbyMemberLeft&)> FLobbiesCommon::OnLobbyMemberLeft()
{
	return OnLobbyMemberLeftEvent;
}

TOnlineEvent<void(const FLobbyLeaderChanged&)> FLobbiesCommon::OnLobbyLeaderChanged()
{
	return OnLobbyLeaderChangedEvent;
}

TOnlineEvent<void(const FLobbySchemaChanged&)> FLobbiesCommon::OnLobbySchemaChanged()
{
	return OnLobbySchemaChangedEvent;
}

TOnlineEvent<void(const FLobbyAttributesChanged&)> FLobbiesCommon::OnLobbyAttributesChanged()
{
	return OnLobbyAttributesChangedEvent;
}

TOnlineEvent<void(const FLobbyMemberAttributesChanged&)> FLobbiesCommon::OnLobbyMemberAttributesChanged()
{
	return OnLobbyMemberAttributesChangedEvent;
}

TOnlineEvent<void(const FLobbyInvitationAdded&)> FLobbiesCommon::OnLobbyInvitationAdded()
{
	return OnLobbyInvitationAddedEvent;
}

TOnlineEvent<void(const FLobbyInvitationRemoved&)> FLobbiesCommon::OnLobbyInvitationRemoved()
{
	return OnLobbyInvitationRemovedEvent;
}

TFuture<TDefaultErrorResultInternal<FOnlineLobbyIdHandle>> FLobbiesCommon::AwaitInvitation(
	FOnlineAccountIdHandle TargetAccountId,
	FOnlineLobbyIdHandle LobbyId,
	float TimeoutSeconds)
{
	// todo: Make this nicer - current implementation has issue on shutdown.
	struct AwaitInvitationState
	{
		TPromise<TDefaultErrorResultInternal<FOnlineLobbyIdHandle>> Promise;
		FOnlineEventDelegateHandle OnLobbyInvitationAddedHandle;
		FTSTicker::FDelegateHandle OnAwaitExpiredHandle;
		bool bSignaled = false;
	};

	TSharedRef<AwaitInvitationState> AwaitState = MakeShared<AwaitInvitationState>();

	AwaitState->OnLobbyInvitationAddedHandle = OnLobbyInvitationAdded().Add(
	[this, TargetAccountId, LobbyId, AwaitState](const FLobbyInvitationAdded& Notification)
	{
		FOnlineEventDelegateHandle DelegateHandle = MoveTemp(AwaitState->OnLobbyInvitationAddedHandle);

		if (!AwaitState->bSignaled)
		{
			if (Notification.Lobby->LobbyId == LobbyId && Notification.LocalUserId == TargetAccountId)
			{
				FTSTicker::GetCoreTicker().RemoveTicker(AwaitState->OnAwaitExpiredHandle);
				AwaitState->bSignaled = true;
				AwaitState->Promise.EmplaceValue(LobbyId);
			}
		}
	});

	AwaitState->OnAwaitExpiredHandle = FTSTicker::GetCoreTicker().AddTicker(
	TEXT("FLobbiesCommon::AwaitInvitation"),
	TimeoutSeconds,
	[this, AwaitState](float)
	{
		if (!AwaitState->bSignaled)
		{
			// Todo: Errors.
			AwaitState->bSignaled = true;
			AwaitState->OnLobbyInvitationAddedHandle.Unbind();
			AwaitState->Promise.EmplaceValue(Errors::NotImplemented());
			FTSTicker::GetCoreTicker().RemoveTicker(AwaitState->OnAwaitExpiredHandle);
		}

		return false;
	});

	return AwaitState->Promise.GetFuture();
}

#if LOBBIES_FUNCTIONAL_TEST_ENABLED
struct FFunctionalTestConfig
{
	FString TestAccount1Type;
	FString TestAccount1Id;
	FString TestAccount1Token;

	FString TestAccount2Type;
	FString TestAccount2Id;
	FString TestAccount2Token;

	float InvitationWaitSeconds = 10.f;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FFunctionalTestConfig)
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount1Type),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount1Id),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount1Token),

	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount2Type),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount2Id),
	ONLINE_STRUCT_FIELD(FFunctionalTestConfig, TestAccount2Token)
END_ONLINE_STRUCT_META()

/* Meta */ }

class FLobbyEventCapture final
{
public:
	UE_NONCOPYABLE(FLobbyEventCapture);

	FLobbyEventCapture(FLobbiesCommon& Lobbies)
		: LobbyJoinedHandle(Lobbies.OnLobbyJoined().Add(this, &FLobbyEventCapture::OnLobbyJoined))
		, LobbyLeftHandle(Lobbies.OnLobbyLeft().Add(this, &FLobbyEventCapture::OnLobbyLeft))
		, LobbyMemberJoinedHandle(Lobbies.OnLobbyMemberJoined().Add(this, &FLobbyEventCapture::OnLobbyMemberJoined))
		, LobbyMemberLeftHandle(Lobbies.OnLobbyMemberLeft().Add(this, &FLobbyEventCapture::OnLobbyMemberLeft))
		, LobbyLeaderChangedHandle(Lobbies.OnLobbyLeaderChanged().Add(this, &FLobbyEventCapture::OnLobbyLeaderChanged))
		, LobbySchemaChangedHandle(Lobbies.OnLobbySchemaChanged().Add(this, &FLobbyEventCapture::OnLobbySchemaChanged))
		, LobbyAttributesChangedHandle(Lobbies.OnLobbyAttributesChanged().Add(this, &FLobbyEventCapture::OnLobbyAttributesChanged))
		, LobbyMemberAttributesChangedHandle(Lobbies.OnLobbyMemberAttributesChanged().Add(this, &FLobbyEventCapture::OnLobbyMemberAttributesChanged))
		, LobbyInvitationAddedHandle(Lobbies.OnLobbyInvitationAdded().Add(this, &FLobbyEventCapture::OnLobbyInvitationAdded))
		, LobbyInvitationRemovedHandle(Lobbies.OnLobbyInvitationRemoved().Add(this, &FLobbyEventCapture::OnLobbyInvitationRemoved))
	{
	}

	void Empty()
	{
		LobbyJoined.Empty();
		LobbyLeft.Empty();
		LobbyMemberJoined.Empty();
		LobbyMemberLeft.Empty();
		LobbyLeaderChanged.Empty();
		LobbySchemaChanged.Empty();
		LobbyAttributesChanged.Empty();
		LobbyMemberAttributesChanged.Empty();
		LobbyInvitationAdded.Empty();
		LobbyInvitationRemoved.Empty();
		NextIndex = 0;
		TotalNotificationsReceived = 0;
	}

	uint32 GetTotalNotificationsReceived() const
	{
		return TotalNotificationsReceived;
	}

	template <typename NotificationType>
	struct TNotificationInfo
	{
		NotificationType Notification;
		int32 GlobalIndex = 0;
	};

	TArray<TNotificationInfo<FLobbyJoined>> LobbyJoined;
	TArray<TNotificationInfo<FLobbyLeft>> LobbyLeft;
	TArray<TNotificationInfo<FLobbyMemberJoined>> LobbyMemberJoined;
	TArray<TNotificationInfo<FLobbyMemberLeft>> LobbyMemberLeft;
	TArray<TNotificationInfo<FLobbyLeaderChanged>> LobbyLeaderChanged;
	TArray<TNotificationInfo<FLobbySchemaChanged>> LobbySchemaChanged;
	TArray<TNotificationInfo<FLobbyAttributesChanged>> LobbyAttributesChanged;
	TArray<TNotificationInfo<FLobbyMemberAttributesChanged>> LobbyMemberAttributesChanged;
	TArray<TNotificationInfo<FLobbyInvitationAdded>> LobbyInvitationAdded;
	TArray<TNotificationInfo<FLobbyInvitationRemoved>> LobbyInvitationRemoved;

private:
	template <typename ContainerType, typename NotificationType>
	void AddEvent(ContainerType& Container, const NotificationType& Notification)
	{
		Container.Add({Notification, NextIndex++});
		++TotalNotificationsReceived;
	}

	void OnLobbyJoined(const FLobbyJoined& Notification) { AddEvent(LobbyJoined, Notification); }
	void OnLobbyLeft(const FLobbyLeft& Notification) { AddEvent(LobbyLeft, Notification); }
	void OnLobbyMemberJoined(const FLobbyMemberJoined& Notification) { AddEvent(LobbyMemberJoined, Notification); }
	void OnLobbyMemberLeft(const FLobbyMemberLeft& Notification) { AddEvent(LobbyMemberLeft, Notification); }
	void OnLobbyLeaderChanged(const FLobbyLeaderChanged& Notification) { AddEvent(LobbyLeaderChanged, Notification); }
	void OnLobbySchemaChanged(const FLobbySchemaChanged& Notification) { AddEvent(LobbySchemaChanged, Notification); }
	void OnLobbyAttributesChanged(const FLobbyAttributesChanged& Notification) { AddEvent(LobbyAttributesChanged, Notification); }
	void OnLobbyMemberAttributesChanged(const FLobbyMemberAttributesChanged& Notification) { AddEvent(LobbyMemberAttributesChanged, Notification); }
	void OnLobbyInvitationAdded(const FLobbyInvitationAdded& Notification) { AddEvent(LobbyInvitationAdded, Notification); }
	void OnLobbyInvitationRemoved(const FLobbyInvitationRemoved& Notification) { AddEvent(LobbyInvitationRemoved, Notification); }

	FOnlineEventDelegateHandle LobbyJoinedHandle;
	FOnlineEventDelegateHandle LobbyLeftHandle;
	FOnlineEventDelegateHandle LobbyMemberJoinedHandle;
	FOnlineEventDelegateHandle LobbyMemberLeftHandle;
	FOnlineEventDelegateHandle LobbyLeaderChangedHandle;
	FOnlineEventDelegateHandle LobbySchemaChangedHandle;
	FOnlineEventDelegateHandle LobbyAttributesChangedHandle;
	FOnlineEventDelegateHandle LobbyMemberAttributesChangedHandle;
	FOnlineEventDelegateHandle LobbyInvitationAddedHandle;
	FOnlineEventDelegateHandle LobbyInvitationRemovedHandle;
	int32 NextIndex = 0;
	uint32 TotalNotificationsReceived = 0;
};

static const FString ConfigName = TEXT("FunctionalTest");
static const FString User1KeyName = TEXT("User1");
static const FString User2KeyName = TEXT("User2");
static const FString CreateLobbyKeyName = TEXT("CreateLobby");
static const FString FindLobbyKeyName = TEXT("FindLobby");
static const FString LobbyEventCaptureKeyName = TEXT("LobbyEventCapture");
static const FString ConfigNameKeyName = TEXT("Config");

TOnlineAsyncOpHandle<FFunctionalTestLobbies> FLobbiesCommon::FunctionalTest(FFunctionalTestLobbies::Params&& InParams)
{
	TSharedRef<FFunctionalTestConfig> TestConfig = MakeShared<FFunctionalTestConfig>();
	LoadConfig(*TestConfig, ConfigName);

	TOnlineAsyncOpRef<FFunctionalTestLobbies> Op = GetOp<FFunctionalTestLobbies>(MoveTemp(InParams));

	// Set up event capturing.
	Op->Data.Set(LobbyEventCaptureKeyName, MakeShared<FLobbyEventCapture>(*this));

	Op->Then(ConsumeStepResult<FFunctionalTestLogoutAllUsers>(*Op, &FLobbiesCommon::FunctionalTestLogoutAllUsers, FFunctionalTestLogoutAllUsers::Params{}))
	.Then(CaptureStepResult<FFunctionalTestLoginUser>(*Op, User1KeyName, &FLobbiesCommon::FunctionalTestLoginUser, FFunctionalTestLoginUser::Params{FPlatformMisc::GetPlatformUserForUserIndex(0), TestConfig->TestAccount1Type, TestConfig->TestAccount1Id, TestConfig->TestAccount1Token}))
	.Then(CaptureStepResult<FFunctionalTestLoginUser>(*Op, User2KeyName, &FLobbiesCommon::FunctionalTestLoginUser, FFunctionalTestLoginUser::Params{FPlatformMisc::GetPlatformUserForUserIndex(1), TestConfig->TestAccount2Type, TestConfig->TestAccount2Id, TestConfig->TestAccount2Token}))

	//----------------------------------------------------------------------------------------------
	// Test 1:
	//    Step 1: Create a lobby with primary user.
	//    Step 2: Leave lobby with primary user.
	//----------------------------------------------------------------------------------------------

	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		// Create a lobby
		FCreateLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.SchemaName = TEXT("test");
		Params.MaxMembers = 2;
		Params.JoinPolicy = ELobbyJoinPolicy::InvitationOnly;
		//Params.Attributes;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User1Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(CaptureOperationStepResult<FCreateLobby>(*Op, CreateLobbyKeyName, &FLobbiesCommon::CreateLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyJoined.Num() != 1 || EventCapture->LobbyMemberJoined.Num() != 1
			|| EventCapture->LobbyJoined[0].GlobalIndex > EventCapture->LobbyMemberJoined[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(ConsumeOperationStepResult<FLeaveLobby>(*Op, &FLobbiesCommon::LeaveLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyLeft.Num() != 1 || EventCapture->LobbyMemberLeft.Num() != 1
			|| EventCapture->LobbyLeft[0].GlobalIndex < EventCapture->LobbyMemberLeft[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})

	//----------------------------------------------------------------------------------------------
	// Test 2:
	//    Step 1: Create a lobby with primary user.
	//    Step 2: Primary user invites secondary user.
	//    Step 3: Join lobby with secondary user.
	//    Step 4: Leave lobby with secondary user.
	//    Step 5: Leave lobby with primary user.
	//----------------------------------------------------------------------------------------------

	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		FCreateLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.SchemaName = TEXT("test");
		Params.MaxMembers = 2;
		Params.JoinPolicy = ELobbyJoinPolicy::InvitationOnly;
		//Params.Attributes;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User1Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(CaptureOperationStepResult<FCreateLobby>(*Op, CreateLobbyKeyName, &FLobbiesCommon::CreateLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyJoined.Num() != 1 || EventCapture->LobbyMemberJoined.Num() != 1
			|| EventCapture->LobbyJoined[0].GlobalIndex > EventCapture->LobbyMemberJoined[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FInviteLobbyMember::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		Params.TargetUserId = User2Info.AccountInfo->UserId;
		return Params;
	})
	.Then(ConsumeOperationStepResult<FInviteLobbyMember>(*Op, &FLobbiesCommon::InviteLobbyMember))
	.Then([this, TestConfig](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);
		return AwaitInvitation(User2Info.AccountInfo->UserId, CreateLobbyResult.Lobby->LobbyId, TestConfig->InvitationWaitSeconds);
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp, TDefaultErrorResultInternal<FOnlineLobbyIdHandle>&& Result)
	{
		if (Result.IsError())
		{
			InAsyncOp.SetError(Errors::Cancelled(Result.GetErrorValue()));
		}
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FJoinLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User2Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(ConsumeOperationStepResult<FJoinLobby>(*Op, &FLobbiesCommon::JoinLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
	// todo: currently busted
	#if 0
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1 || EventCapture->LobbyMemberJoined.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	#endif
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(ConsumeOperationStepResult<FLeaveLobby>(*Op, &FLobbiesCommon::LeaveLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
	// todo: currently busted
	#if 0
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1|| EventCapture->LobbyMemberLeft.Num())
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	#endif
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(ConsumeOperationStepResult<FLeaveLobby>(*Op, &FLobbiesCommon::LeaveLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
	// todo: currently busted
	#if 0
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyLeft.Num() != 1 || EventCapture->LobbyMemberLeft.Num() != 1
			|| EventCapture->LobbyLeft[0].GlobalIndex < EventCapture->LobbyMemberLeft[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	#endif
	})

	//----------------------------------------------------------------------------------------------
	// Test 3: Simple search
	//    Step 1: Create a lobby with primary user.
	//    Step 2: Secondary user searches for lobbies.
	//    Step 3: Join lobby with secondary user.
	//    Step 4: Leave lobby with secondary user.
	//    Step 5: Leave lobby with primary user.
	//----------------------------------------------------------------------------------------------

	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);

		FCreateLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.SchemaName = TEXT("test");
		Params.MaxMembers = 2;
		Params.JoinPolicy = ELobbyJoinPolicy::PublicAdvertised;
		//Params.Attributes;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User1Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(CaptureOperationStepResult<FCreateLobby>(*Op, CreateLobbyKeyName, &FLobbiesCommon::CreateLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyJoined.Num() != 1 || EventCapture->LobbyMemberJoined.Num() != 1
			|| EventCapture->LobbyJoined[0].GlobalIndex > EventCapture->LobbyMemberJoined[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		// Search for lobby from create
		FFindLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(CaptureOperationStepResult<FFindLobby>(*Op, FindLobbyKeyName, &FLobbiesCommon::FindLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);
		const FFindLobby::Result& FindResults = GetOpDataChecked<FFindLobby::Result>(InAsyncOp, FindLobbyKeyName);

		// Not a valid expectation without a custom bucket.
		if (FindResults.Lobbies.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
			return FJoinLobby::Params{};
		}

		FJoinLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LocalName = TEXT("test");
		Params.LobbyId = FindResults.Lobbies[0]->LobbyId;
		Params.LocalUsers.Emplace(FJoinLobbyLocalUserData{User2Info.AccountInfo->UserId, {}});
		return Params;
	})
	.Then(ConsumeOperationStepResult<FJoinLobby>(*Op, &FLobbiesCommon::JoinLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
	// todo: currently busted
	#if 0
		// Check both lobby joined events were received and in the correct order.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1 || EventCapture->LobbyMemberJoined.Num() != 1)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	#endif
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User2Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User2KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User2Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(ConsumeOperationStepResult<FLeaveLobby>(*Op, &FLobbiesCommon::LeaveLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
	// todo: currently busted
	#if 0
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 1|| EventCapture->LobbyMemberLeft.Num())
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	#endif
	})
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		// Prepare for next test check.
		GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName)->Empty();

		const FFunctionalTestLoginUser::Result& User1Info = GetOpDataChecked<FFunctionalTestLoginUser::Result>(InAsyncOp, User1KeyName);
		const FCreateLobby::Result& CreateLobbyResult = GetOpDataChecked<FCreateLobby::Result>(InAsyncOp, CreateLobbyKeyName);

		FLeaveLobby::Params Params;
		Params.LocalUserId = User1Info.AccountInfo->UserId;
		Params.LobbyId = CreateLobbyResult.Lobby->LobbyId;
		return Params;
	})
	.Then(ConsumeOperationStepResult<FLeaveLobby>(*Op, &FLobbiesCommon::LeaveLobby))
	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
	// todo: currently busted
	#if 0
		// Check for expected events.
		const TSharedRef<FLobbyEventCapture>& EventCapture = GetOpDataChecked<TSharedRef<FLobbyEventCapture>>(InAsyncOp, LobbyEventCaptureKeyName);
		if (EventCapture->GetTotalNotificationsReceived() != 2
			|| EventCapture->LobbyLeft.Num() != 1 || EventCapture->LobbyMemberLeft.Num() != 1
			|| EventCapture->LobbyLeft[0].GlobalIndex < EventCapture->LobbyMemberLeft[0].GlobalIndex)
		{
			InAsyncOp.SetError(Errors::Cancelled());
		}
	#endif
	})

	//----------------------------------------------------------------------------------------------
	// Complete
	//----------------------------------------------------------------------------------------------

	.Then([this](TOnlineAsyncOp<FFunctionalTestLobbies>& InAsyncOp)
	{
		InAsyncOp.SetResult(FFunctionalTestLobbies::Result{});
	})
	.Enqueue(GetServices().GetParallelQueue());

	return Op->GetHandle();
}

TFuture<TOnlineResult<FLobbiesCommon::FFunctionalTestLoginUser>> FLobbiesCommon::FunctionalTestLoginUser(FFunctionalTestLoginUser::Params&& Params)
{
	UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FunctionalTestLoginUser] Logging in user. PlatformUserId: %d"), FPlatformMisc::GetUserIndexForPlatformUser(Params.PlatformUserId));

	IAuth* AuthInterface = Services.Get<IAuth>();
	if (!AuthInterface)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FLobbiesCommon::FunctionalTestLoginUser] Login failed. PlatformUserId: %d"), FPlatformMisc::GetUserIndexForPlatformUser(Params.PlatformUserId));
		return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLoginUser>>(Errors::MissingInterface()).GetFuture();
	}

	TSharedRef<TPromise<TOnlineResult<FFunctionalTestLoginUser>>> Promise = MakeShared<TPromise<TOnlineResult<FFunctionalTestLoginUser>>>();
	TFuture<TOnlineResult<FFunctionalTestLoginUser>> Future = Promise->GetFuture();

	FAuthLogin::Params LoginParams;
	LoginParams.PlatformUserId = Params.PlatformUserId;
	LoginParams.CredentialsType = Params.Type;
	LoginParams.CredentialsId = Params.Id;
	LoginParams.CredentialsToken.Set<FString>(Params.Token);

	AuthInterface->Login(MoveTemp(LoginParams))
	.OnComplete([Promise, PlatformUserId = Params.PlatformUserId](const TOnlineResult<FAuthLogin>& LoginResult)
	{
		if (LoginResult.IsError())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FLobbiesCommon::FunctionalTestLoginUser] Login failed. PlatformUserId: %d, Result: %s"), FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId), *LoginResult.GetErrorValue().GetLogString());
			Promise->EmplaceValue(Errors::RequestFailure());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FunctionalTestLoginUser] Login Succeeded. PlatformUserId: %d, User: %s"), FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId), *ToLogString(LoginResult.GetOkValue().AccountInfo->UserId));
			Promise->EmplaceValue(FFunctionalTestLoginUser::Result{LoginResult.GetOkValue().AccountInfo});
		}
	});

	return Future;
}

TFuture<TOnlineResult<FLobbiesCommon::FFunctionalTestLogoutUser>> FLobbiesCommon::FunctionalTestLogoutUser(FFunctionalTestLogoutUser::Params&& Params)
{
	UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logging out user. PlatformUserId: %d"), FPlatformMisc::GetUserIndexForPlatformUser(Params.PlatformUserId));

	IAuth* AuthInterface = Services.Get<IAuth>();
	if (!AuthInterface)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logout failed. PlatformUserId: %d"), FPlatformMisc::GetUserIndexForPlatformUser(Params.PlatformUserId));
		return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(Errors::MissingInterface()).GetFuture();
	}

	FAuthGetAccountByPlatformUserId::Params GetAccountParams;
	GetAccountParams.PlatformUserId = Params.PlatformUserId;
	TOnlineResult<FAuthGetAccountByPlatformUserId> Result = AuthInterface->GetAccountByPlatformUserId(MoveTemp(GetAccountParams));
	if (Result.IsError())
	{
		// Ignore errors for now. This function should ignore logged out users.
		if (Result.GetErrorValue() == Errors::Unknown())
		{
			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logout Succeeded - already logged out. PlatformUserId: %d"), FPlatformMisc::GetUserIndexForPlatformUser(Params.PlatformUserId));
			return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(FFunctionalTestLogoutUser::Result{}).GetFuture();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logout failed. PlatformUserId: %d, Result: %s"), FPlatformMisc::GetUserIndexForPlatformUser(Params.PlatformUserId), *Result.GetErrorValue().GetLogString());
			return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(Errors::Unknown(MoveTemp(Result.GetErrorValue()))).GetFuture();
		}
	}

	TSharedRef<FAccountInfo> AccountInfo = Result.GetOkValue().AccountInfo;
	if (AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
	{
		return MakeFulfilledPromise<TOnlineResult<FFunctionalTestLogoutUser>>(Errors::InvalidUser()).GetFuture();
	}

	TSharedRef<TPromise<TOnlineResult<FFunctionalTestLogoutUser>>> Promise = MakeShared<TPromise<TOnlineResult<FFunctionalTestLogoutUser>>>();
	TFuture<TOnlineResult<FFunctionalTestLogoutUser>> Future = Promise->GetFuture();

	FAuthLogout::Params LogoutParams;
	LogoutParams.LocalUserId = AccountInfo->UserId;

	AuthInterface->Logout(MoveTemp(LogoutParams))
	.OnComplete([Promise, PlatformUserId = Params.PlatformUserId](const TOnlineResult<FAuthLogout>& LogoutResult) mutable
	{
		if (LogoutResult.IsError())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logout failed. PlatformUserId: %d, Result: %s"), FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId), *LogoutResult.GetErrorValue().GetLogString());
			Promise->EmplaceValue(Errors::RequestFailure());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logout Succeeded. PlatformUserId: %d"), FPlatformMisc::GetUserIndexForPlatformUser(PlatformUserId));
			Promise->EmplaceValue(FFunctionalTestLogoutUser::Result{});
		}
	});

	return Future;
}

TFuture<TOnlineResult<FLobbiesCommon::FFunctionalTestLogoutAllUsers>> FLobbiesCommon::FunctionalTestLogoutAllUsers(FFunctionalTestLogoutAllUsers::Params&& Params)
{
	UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FFunctionalTestLogoutAllUsers] Logging out all users."));

	TArray<TFuture<TOnlineResult<FLobbiesCommon::FFunctionalTestLogoutUser>>> LogoutFutures;
	for (int32 index = 0; index < MAX_LOCAL_PLAYERS; ++index)
	{
		LogoutFutures.Emplace(FunctionalTestLogoutUser(FFunctionalTestLogoutUser::Params{FPlatformMisc::GetPlatformUserForUserIndex(index)}));
	}

	TSharedRef<TPromise<TOnlineResult<FFunctionalTestLogoutAllUsers>>> Promise = MakeShared<TPromise<TOnlineResult<FFunctionalTestLogoutAllUsers>>>();
	TFuture<TOnlineResult<FFunctionalTestLogoutAllUsers>> Future = Promise->GetFuture();

	WhenAll(MoveTempIfPossible(LogoutFutures))
	.Then([Promise = MoveTemp(Promise)](TFuture<TArray<TOnlineResult<FLobbiesCommon::FFunctionalTestLogoutUser>>>&& Results)
	{
		bool HasAnyError = false;
		for (TOnlineResult<FLobbiesCommon::FFunctionalTestLogoutUser>& Result : Results.Get())
		{
			HasAnyError |= Result.IsError();
		}

		if (HasAnyError)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FLobbiesCommon::FunctionalTestLogoutUser] Logout all users Failed."));
			Promise->EmplaceValue(Errors::RequestFailure());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[FLobbiesCommon::FFunctionalTestLogoutAllUsers] Logout all users Succeeded."));
			Promise->EmplaceValue(FFunctionalTestLogoutAllUsers::Result{});
		}
	});

	return Future;
}

#endif

/* UE::Online */ }
