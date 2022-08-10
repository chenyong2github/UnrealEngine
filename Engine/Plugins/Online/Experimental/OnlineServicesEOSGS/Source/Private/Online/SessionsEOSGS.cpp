// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsEOSGS.h"

#include "Online/AuthEOSGS.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesEOSGSTypes.h"
#include "Online/NboSerializerEOSGSSvc.h"

#include "eos_sessions.h"
#include "eos_sessions_types.h"

namespace UE::Online {

/** Auxiliary methods */

EOS_EOnlineSessionPermissionLevel GetEOSGSType(const ESessionJoinPolicy& Value)
{
	switch (Value)
	{
	case ESessionJoinPolicy::Public:		return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
	case ESessionJoinPolicy::FriendsOnly:	return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_JoinViaPresence;
	case ESessionJoinPolicy::InviteOnly:	return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly;
	}

	checkNoEntry();
	return EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly;
}

/** FOnlineSessionIdRegistryEOSGS */

FOnlineSessionIdRegistryEOSGS& FOnlineSessionIdRegistryEOSGS::Get()
{
	static FOnlineSessionIdRegistryEOSGS Instance;
	return Instance;
}

bool FOnlineSessionIdRegistryEOSGS::IsSessionIdExpired(const FOnlineSessionIdHandle& InHandle) const
{
	return BasicRegistry.FindIdValue(InHandle).IsEmpty();
}

/** FOnlineSessionInviteIdRegistryEOSGS */

FOnlineSessionInviteIdRegistryEOSGS& FOnlineSessionInviteIdRegistryEOSGS::Get()
{
	static FOnlineSessionInviteIdRegistryEOSGS Instance;
	return Instance;
}

bool FOnlineSessionInviteIdRegistryEOSGS::IsSessionInviteIdExpired(const FOnlineSessionInviteIdHandle& InHandle) const
{
	return BasicRegistry.FindIdValue(InHandle).IsEmpty();
}

/** FSessionEOSGS */

FSessionEOSGS::FSessionEOSGS(const EOS_HSessionDetails& InSessionDetailsHandle)
	: SessionDetailsHandle(MakeShared<FSessionDetailsHandleEOSGS>(InSessionDetailsHandle))
{
	EOS_SessionDetails_CopyInfoOptions CopyInfoOptions = { };
	CopyInfoOptions.ApiVersion = EOS_SESSIONDETAILS_COPYINFO_API_LATEST;
	static_assert(EOS_SESSIONDETAILS_COPYINFO_API_LATEST == 1, "EOS_SessionDetails_CopyInfoOptions updated, check new fields");

	EOS_SessionDetails_Info* SessionDetailsInfo = nullptr;
	EOS_EResult CopyInfoResult = EOS_SessionDetails_CopyInfo(InSessionDetailsHandle, &CopyInfoOptions, &SessionDetailsInfo);
	if (CopyInfoResult == EOS_EResult::EOS_Success)
	{
		CurrentState = ESessionState::Valid;
		SessionId = FSessionsEOSGS::CreateSessionId(FString(SessionDetailsInfo->SessionId));

		// TODO: Load all session settings after SchemaVariant work
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FSessionEOSGS] EOS_SessionDetails_CopyInfo failed with result [%s]"), *LexToString(CopyInfoResult));

		CurrentState = ESessionState::Invalid;
	}
}

const FSessionEOSGS& FSessionEOSGS::Cast(const FSession& InSession)
{
	check(InSession.SessionId.GetOnlineServicesType() == EOnlineServices::Epic);

	return static_cast<const FSessionEOSGS&>(InSession);
}

/** FSessionsEOSGS */

FSessionsEOSGS::FSessionsEOSGS(FOnlineServicesEOSGS& InServices)
	: Super(InServices)
{
}

void FSessionsEOSGS::Initialize()
{
	Super::Initialize();

	SessionsHandle = EOS_Platform_GetSessionsInterface(static_cast<FOnlineServicesEOSGS&>(GetServices()).GetEOSPlatformHandle());
	check(SessionsHandle);

	RegisterEventHandlers();
}

void FSessionsEOSGS::Shutdown()
{
	Super::Shutdown();

	UnregisterEventHandlers();
}

void FSessionsEOSGS::RegisterEventHandlers()
{
	// Register for session invites received events
	OnSessionInviteReceivedEventRegistration = EOS_RegisterComponentEventHandler(
		this,
		SessionsHandle,
		EOS_SESSIONS_ADDNOTIFYSESSIONINVITERECEIVED_API_LATEST,
		&EOS_Sessions_AddNotifySessionInviteReceived,
		&EOS_Sessions_RemoveNotifySessionInviteReceived,
		&FSessionsEOSGS::HandleSessionInviteReceived);

	static_assert(EOS_SESSIONS_ADDNOTIFYSESSIONINVITERECEIVED_API_LATEST == 1, "EOS_Sessions_AddNotifySessionInviteReceivedOptions updated, check new fields");

	// Register for session invites accepted events
	OnSessionInviteAcceptedEventRegistration = EOS_RegisterComponentEventHandler(
		this,
		SessionsHandle,
		EOS_SESSIONS_ADDNOTIFYSESSIONINVITEACCEPTED_API_LATEST,
		&EOS_Sessions_AddNotifySessionInviteAccepted,
		&EOS_Sessions_RemoveNotifySessionInviteAccepted,
		&FSessionsEOSGS::HandleSessionInviteAccepted);

	static_assert(EOS_SESSIONS_ADDNOTIFYSESSIONINVITEACCEPTED_API_LATEST == 1, "EOS_Sessions_AddNotifySessionInviteAcceptedOptions updated, check new fields");

	// Register for join session accepted events
	OnJoinSessionAcceptedEventRegistration = EOS_RegisterComponentEventHandler(
		this,
		SessionsHandle,
		EOS_SESSIONS_ADDNOTIFYJOINSESSIONACCEPTED_API_LATEST,
		&EOS_Sessions_AddNotifyJoinSessionAccepted,
		&EOS_Sessions_RemoveNotifyJoinSessionAccepted,
		&FSessionsEOSGS::HandleJoinSessionAccepted);

	static_assert(EOS_SESSIONS_ADDNOTIFYJOINSESSIONACCEPTED_API_LATEST == 1, "EOS_Sessions_AddNotifyJoinSessionAcceptedOptions updated, check new fields");
}

void FSessionsEOSGS::UnregisterEventHandlers()
{
	OnSessionInviteReceivedEventRegistration = nullptr;
	OnSessionInviteAcceptedEventRegistration = nullptr;
	OnJoinSessionAcceptedEventRegistration = nullptr;
}

void FSessionsEOSGS::HandleSessionInviteReceived(const EOS_Sessions_SessionInviteReceivedCallbackInfo* Data)
{
	FOnlineAccountIdHandle LocalUserId = FindAccountId(Data->LocalUserId);
	if (LocalUserId.IsValid())
	{
		Services.Get<FAuthEOSGS>()->ResolveAccountIds(LocalUserId, { Data->LocalUserId, Data->TargetUserId })
			.Next([this, WeakThis = AsWeak(), LocalUserId, InviteId = FString(UTF8_TO_TCHAR(Data->InviteId))](TArray<FOnlineAccountIdHandle> ResolvedAccountIds)
		{
			if (const TSharedPtr<ISessions> StrongThis = WeakThis.Pin())
			{
				// First and second place in the array will be occupied by the receiver and the sender, respectively, since the same order is kept in the array of resolved ids
				const FOnlineAccountIdHandle& ReceiverId = ResolvedAccountIds[0];
				const FOnlineAccountIdHandle& SenderId = ResolvedAccountIds[1];

				TResult<TSharedRef<const FSession>, FOnlineError> SessionInviteResult = BuildSessionFromInvite(InviteId);
				
				if (SessionInviteResult.IsOk())
				{
					FOnlineSessionInviteIdHandle InviteIdHandle = CreateSessionInviteId(InviteId);

					TSharedRef<FSessionInvite> SessionInviteRef = MakeShared<FSessionInvite>(FSessionInvite{
						ReceiverId,
						SenderId,
						InviteIdHandle,
						SessionInviteResult.GetOkValue()
					});

					TMap<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>& UserMap = SessionInvitesUserMap.FindOrAdd(ReceiverId);
					UserMap.Emplace(InviteIdHandle, SessionInviteRef);

					FSessionInviteReceived Event = { LocalUserId, SessionInviteRef };

					SessionEvents.OnSessionInviteReceived.Broadcast(Event);
				}

				// We won't broadcast the event if there was an error retrieving the session information
			}
		});
	}
}

void FSessionsEOSGS::HandleSessionInviteAccepted(const EOS_Sessions_SessionInviteAcceptedCallbackInfo* Data)
{
	FOnlineAccountIdHandle LocalUserId = FindAccountId(Data->LocalUserId);
	if (LocalUserId.IsValid())
	{
		FUISessionJoinRequested Event{
			LocalUserId,
			BuildSessionFromInvite(FString(Data->InviteId)),
			EUISessionJoinRequestedSource::FromInvitation
		};

		SessionEvents.OnUISessionJoinRequested.Broadcast(Event);

		// The game can react to the OnUISessionJoinRequested event by starting the JoinSession process
	}
}

void FSessionsEOSGS::HandleJoinSessionAccepted(const EOS_Sessions_JoinSessionAcceptedCallbackInfo* Data)
{
	FOnlineAccountIdHandle LocalUserId = FindAccountId(Data->LocalUserId);
	if (LocalUserId.IsValid())
	{
		FUISessionJoinRequested Event {
			LocalUserId,
			BuildSessionFromUIEvent(Data->UiEventId),
			EUISessionJoinRequestedSource::Unspecified
		};

		SessionEvents.OnUISessionJoinRequested.Broadcast(Event);

		// The game can react to the OnUISessionJoinRequested event by starting the JoinSession process
	}
}

TOnlineAsyncOpHandle<FCreateSession> FSessionsEOSGS::CreateSession(FCreateSession::Params&& Params)
{
	// LAN Sessions
	if (Params.SessionSettings.bIsLANSession)
	{
		return FSessionsLAN::CreateSession(MoveTemp(Params));
	}

	// EOSGS Sessions

	TOnlineAsyncOpRef<FCreateSession> Op = GetOp<FCreateSession>(MoveTemp(Params));
	const FCreateSession::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckCreateSessionParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FCreateSession>& Op)
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckCreateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		if (!OpParams.SessionSettings.SessionIdOverride.IsEmpty())
		{
			int32 Length = OpParams.SessionSettings.SessionIdOverride.Len();

			if (Length < EOS_SESSIONMODIFICATION_MIN_SESSIONIDOVERRIDE_LENGTH || Length > EOS_SESSIONMODIFICATION_MAX_SESSIONIDOVERRIDE_LENGTH)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::CreateSession] Could not create session with SessionIdOverride [%s] of size [%d]. SessionIdOverride size must be between [%d] and [%d] characters long"), *OpParams.SessionSettings.SessionIdOverride, Length, EOS_SESSIONMODIFICATION_MIN_SESSIONIDOVERRIDE_LENGTH, EOS_SESSIONMODIFICATION_MAX_SESSIONIDOVERRIDE_LENGTH);

				Op.SetError(Errors::InvalidParams());

				return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
			}
		}

		// TODO: Check if bucket id is set. Depends on if the method can be called successfully without it

		// After all initial checks, we start the session creation operations

		EOS_Sessions_CreateSessionModificationOptions CreateSessionModificationOptions = {};
		CreateSessionModificationOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
		static_assert(EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST == 4, "EOS_Sessions_CreateSessionModificationOptions updated, check new fields");

		CreateSessionModificationOptions.bPresenceEnabled = OpParams.SessionSettings.bPresenceEnabled;
		CreateSessionModificationOptions.bSanctionsEnabled = !OpParams.SessionSettings.bAllowSanctionedPlayers;

		const FCustomSessionSetting* BucketIdSetting = OpParams.SessionSettings.CustomSettings.Find(EOS_SESSIONS_BUCKET_ID);
		FTCHARToUTF8 BucketIdUtf8(BucketIdSetting != nullptr ? BucketIdSetting->Data.Get<FString>() : FString());

		if (BucketIdUtf8.Length())
		{
			CreateSessionModificationOptions.BucketId = BucketIdUtf8.Get();
		}
	
		CreateSessionModificationOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);
		CreateSessionModificationOptions.MaxPlayers = OpParams.SessionSettings.NumMaxPrivateConnections + OpParams.SessionSettings.NumMaxPublicConnections;

		FTCHARToUTF8 SessionIdUtf8(OpParams.SessionSettings.SessionIdOverride);
		if (SessionIdUtf8.Length())
		{
			CreateSessionModificationOptions.SessionId = SessionIdUtf8.Get();
		}

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		CreateSessionModificationOptions.SessionName = SessionNameUtf8.Get();

		EOS_HSessionModification SessionModificationHandle = nullptr;
		EOS_EResult ResultCode = EOS_Sessions_CreateSessionModification(SessionsHandle, &CreateSessionModificationOptions, &SessionModificationHandle);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::CreateSession] EOS_Sessions_CreateSessionModification failed with result [%s]"), *LexToString(ResultCode));

			Op.SetError(Errors::FromEOSResult(ResultCode));

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		// TODO: We could call EOS_SessionModification_SetHostAddress at this point, although it's not necessary

		// We write all SessionSettings values into the SessionModificationHandle
		WriteCreateSessionModificationHandle(SessionModificationHandle, OpParams.SessionSettings);

		// If the session modification handle is created successfully, we'll create the local session object
 		TSharedRef<FSessionEOSGS> NewSessionEOSGSRef = MakeShared<FSessionEOSGS>();
		NewSessionEOSGSRef->CurrentState = ESessionState::Creating;
		NewSessionEOSGSRef->OwnerUserId = OpParams.LocalUserId;
		NewSessionEOSGSRef->SessionSettings = OpParams.SessionSettings;

		LocalSessionsByName.Add(OpParams.SessionName, NewSessionEOSGSRef);

		FUpdateSessionImpl::Params UpdateSessionImplParams { MakeShared<FSessionModificationHandleEOSGS>(SessionModificationHandle) };
		
		return UpdateSessionImpl(MoveTemp(UpdateSessionImplParams));
	})
	.Then([this](TOnlineAsyncOp<FCreateSession>& Op, TDefaultErrorResult<FUpdateSessionImpl>&& Result)
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		if (Result.IsOk())
		{
			TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(OpParams.SessionName);
			FoundSession->CurrentState = ESessionState::Valid;

			// TODO: Make additional local users join the created session

			Op.SetResult(FCreateSession::Result{ FoundSession });

		}
		else
		{	
			// If creation failed, we remove the session objects
			LocalSessionsByName.Remove(OpParams.SessionName);

			Op.SetError(MoveTemp(Result.GetErrorValue()));
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

void FSessionsEOSGS::SetPermissionLevel(EOS_HSessionModification& SessionModificationHandle, const ESessionJoinPolicy& NewJoinPolicy)
{
	EOS_SessionModification_SetPermissionLevelOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST == 1, "EOS_SessionModification_SetPermissionLevelOptions updated, check new fields");

	Options.PermissionLevel = GetEOSGSType(NewJoinPolicy);

	EOS_EResult ResultCode = EOS_SessionModification_SetPermissionLevel(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_SetPermissionLevel failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetBucketId(EOS_HSessionModification& SessionModificationHandle, const FString& NewBucketId)
{
	EOS_SessionModification_SetBucketIdOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETBUCKETID_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETBUCKETID_API_LATEST == 1, "EOS_SessionModification_SetBucketIdOptions updated, check new fields");

	FTCHARToUTF8 BucketIdUtf8(*NewBucketId);
	Options.BucketId = BucketIdUtf8.Get();

	EOS_EResult ResultCode = EOS_SessionModification_SetBucketId(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_SetBucketId failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetMaxPlayers(EOS_HSessionModification& SessionModificationHandle, const uint32& NewMaxPlayers)
{
	EOS_SessionModification_SetMaxPlayersOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST == 1, "EOS_SessionModification_SetMaxPlayersOptions updated, check new fields");

	Options.MaxPlayers = NewMaxPlayers;

	EOS_EResult ResultCode = EOS_SessionModification_SetMaxPlayers(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_SetMaxPlayers failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::WriteCreateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FSessionSettings& SessionSettings)
{
	// TODO: We have the option to call EOS_SessionModification_SetHostAddress in EOS, useful if the session owner changes

	// TODO: We either call Start or End session, EOS_SessionModification_SetJoinInProgressAllowed and EOS_SessionModification_SetInvitesAllowed depending on this value changing, or a custom setting.
	// Investigation pending to see if there is overlay changes when the session is not joinable (started + JIP false, or InvitesAllowed false)

	// TODO: Add non-custom settings as attributes on the API call to transmit that information in session searches. Example:

// 	FCustomSessionSetting NewSetting;
// 	NewSetting.Data.Set<bool>(SessionSettings.bAllowNewMembers);
// 	NewSetting.Visibility = ECustomSessionSettingVisibility::ViaOnlineService;
// 
// 	AddAttribute(SessionModificationHandle, EOS_SESSIONS_ALLOW_NEW_MEMBERS, NewSetting);

	SetPermissionLevel(SessionModificationHandle, SessionSettings.JoinPolicy);

	SetMaxPlayers(SessionModificationHandle, SessionSettings.NumMaxPrivateConnections + SessionSettings.NumMaxPublicConnections);

	// Custom Settings

// 	for (FCustomSessionSettingsMap::TConstIterator It(SessionSettings.CustomSettings); It; ++It)
// 	{
// 		// TODO: Add the attribute to the SessionModification. Pending SchemaVariant work.
// 	}

	// BucketId has its own set method on the API
	const FCustomSessionSetting* NewBucketIdSetting = SessionSettings.CustomSettings.Find(EOS_SESSIONS_BUCKET_ID);
	if (NewBucketIdSetting)
	{
		SetBucketId(SessionModificationHandle, NewBucketIdSetting->Data.Get<FString>());
	}

	// Session Members

// 	for (FSessionMembersMap::TConstIterator MemberIt(SessionSettings.SessionMembers); MemberIt; ++MemberIt)
// 	{
// 		const FSessionMember& SessionMember = MemberIt.Value();
// 		
// 		for (FCustomSessionSettingsMap::TConstIterator SettingIt(SessionMember.MemberSettings); SettingIt; ++SettingIt)
// 		{
// 			const FName& Key = FName(LexToString(GetProductUserIdChecked(MemberIt.Key())) + TEXT(":") + SettingIt.Key().ToString());
// 			const FCustomSessionSetting& Value = SettingIt.Value();
// 
// 			// TODO: Add the attribute to the SessionModification. Pending SchemaVariant work.
// 		}		
// 	}
}

void FSessionsEOSGS::WriteUpdateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FName& SessionName, const FSessionSettingsUpdate& NewSettings)
{
	// TODO: We have the option to call EOS_SessionModification_SetHostAddress in EOS, useful if the session owner changes
	
	// TODO: We either call Start or End session, EOS_SessionModification_SetJoinInProgressAllowed and EOS_SessionModification_SetInvitesAllowed depending on this value changing, or a custom setting.
	// Investigation pending to see if there is overlay changes when the session is not joinable (started + JIP false, or InvitesAllowed false)

	// TODO: Add non-custom settings as attributes on the API call to transmit that information in session searches. Example:

// 	FCustomSessionSetting NewSetting;
// 	NewSetting.Data.Set<bool>(SessionSettings.bAllowNewMembers);
// 	NewSetting.Visibility = ECustomSessionSettingVisibility::ViaOnlineService;
// 
// 	AddAttribute(SessionModificationHandle, EOS_SESSIONS_ALLOW_NEW_MEMBERS, NewSetting);
	const TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(SessionName);

	if (NewSettings.JoinPolicy.IsSet())
	{
		SetPermissionLevel(SessionModificationHandle, NewSettings.JoinPolicy.GetValue());
	}

	if (NewSettings.NumMaxPrivateConnections.IsSet() || NewSettings.NumMaxPublicConnections.IsSet())
	{
		// We set MaxPlayers to the sum of any new values (if set) and old values (if not set)
		uint32 DefaultNumMaxPrivateConnections = FoundSession->SessionSettings.NumMaxPrivateConnections;
		uint32 DefaultNumMaxPublicConnections = FoundSession->SessionSettings.NumMaxPublicConnections;

		SetMaxPlayers(SessionModificationHandle, NewSettings.NumMaxPrivateConnections.Get(DefaultNumMaxPrivateConnections) + NewSettings.NumMaxPublicConnections.Get(DefaultNumMaxPublicConnections));
	}

	// BucketId has its own set method on the API
	const FCustomSessionSetting* NewBucketIdSetting = NewSettings.UpdatedCustomSettings.Find(EOS_SESSIONS_BUCKET_ID);
	if (NewBucketIdSetting)
	{
		SetBucketId(SessionModificationHandle, NewBucketIdSetting->Data.Get<FString>());
	}

	// TODO: This next section will be reactivated as part of the CL to add support for SchemaVariant
#if 0
	// Custom Settings

	for (const FName& Key : NewSettings.RemovedCustomSettings)
	{
		SessionSettings.CustomSettings.Remove(Key);

		// TODO: Remove the attribute to the SessionModification. Pending SchemaVariant work.
	}

// 	for (FCustomSessionSettingsMap::TConstIterator It(NewSettings.UpdatedCustomSettings); It; ++It)
// 	{
// 		// TODO: Add the attribute to the SessionModification. Pending SchemaVariant work.
// 	}

	SessionSettings.CustomSettings.Append(NewSettings.UpdatedCustomSettings);

	// registered players

	for (const FOnlineAccountIdHandle& Key : NewSettings.RemovedRegisteredPlayers)
	{
		SessionSettings.RegisteredPlayers.Remove(Key);
	}

	SessionSettings.RegisteredPlayers.Append(NewSettings.UpdatedRegisteredPlayers);

	// Session Members

	for (const FOnlineAccountIdHandle& Key : NewSettings.RemovedSessionMembers)
	{
// 		// Before removing a session member, we'll remove all their attributes
// 		if (FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(Key))
// 		{
// 			TArray<FName> SettingsKeyArray;
// 			SessionMember->MemberSettings.GenerateKeyArray(SettingsKeyArray);
// 
// 			for (const FName& SettingKey : SettingsKeyArray)
// 			{
// 				const FName& MemberKey = FName(LexToString(GetProductUserIdChecked(Key)) + TEXT(":") + SettingKey.ToString());
// 				
// 				// TODO: Remove the attribute to the SessionModification. Pending SchemaVariant work.
// 			}
// 		}

		SessionSettings.SessionMembers.Remove(Key);
	}

	for (FSessionMemberUpdatesMap::TConstIterator MemberIt(NewSettings.UpdatedSessionMembers); MemberIt; ++MemberIt)
	{
		if (FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(MemberIt.Key()))
		{
			const FSessionMemberUpdate& SessionMemberUpdate = MemberIt.Value();

			for (const FName& Key : SessionMemberUpdate.RemovedMemberSettings)
			{
				SessionMember->MemberSettings.Remove(Key);

// 				const FName& MemberKey = FName(LexToString(GetProductUserIdChecked(MemberIt.Key())) + TEXT(":") + Key.ToString());
// 				
// 				// TODO: Remove the attribute to the SessionModification. Pending SchemaVariant work.
			}
			
// 			for (FCustomSessionSettingsMap::TConstIterator SettingIt(SessionMemberUpdate.UpdatedMemberSettings); SettingIt; ++SettingIt)
// 			{
// 				const FName& Key = FName(LexToString(GetProductUserIdChecked(MemberIt.Key())) + TEXT(":") + SettingIt.Key().ToString());
// 				const FCustomSessionSetting& Value = SettingIt.Value();
// 
// 				// TODO: Add the attribute to the SessionModification. Pending SchemaVariant work.
// 			}

			SessionMember->MemberSettings.Append(SessionMemberUpdate.UpdatedMemberSettings);
		}
	}
#endif
}

TOnlineAsyncOpHandle<FUpdateSession> FSessionsEOSGS::UpdateSession(FUpdateSession::Params&& Params)
{
	// LAN Sessions
	TOnlineResult<FGetSessionByName> Result = GetSessionByName({ Params.SessionName });
	if (Result.IsOk() && Result.GetOkValue().Session->SessionSettings.bIsLANSession)
	{
		return FSessionsLAN::UpdateSession(MoveTemp(Params));
	}

	// EOSGS Sessions

	TOnlineAsyncOpRef<FUpdateSession> Op = GetOp<FUpdateSession>(MoveTemp(Params));
	const FUpdateSession::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FUpdateSession>& Op)
	{
		const FUpdateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckUpdateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		EOS_Sessions_UpdateSessionModificationOptions UpdateSessionModificationOptions = {};
		UpdateSessionModificationOptions.ApiVersion = EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST;
		static_assert(EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST == 1, "EOS_Sessions_UpdateSessionModificationOptions updated, check new fields");

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		UpdateSessionModificationOptions.SessionName = SessionNameUtf8.Get();

		EOS_HSessionModification SessionModificationHandle = nullptr;
		EOS_EResult ResultCode = EOS_Sessions_UpdateSessionModification(SessionsHandle, &UpdateSessionModificationOptions, &SessionModificationHandle);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::UpdateSession] EOS_Sessions_UpdateSessionModification failed with result [%s]"), *LexToString(ResultCode));

			Op.SetError(Errors::FromEOSResult(ResultCode));

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		// After creating the session modification handle, we'll add all the updated data to it
		WriteUpdateSessionModificationHandle(SessionModificationHandle, OpParams.SessionName, OpParams.Mutations);

		FUpdateSessionImpl::Params UpdateSessionImplParams{ MakeShared<FSessionModificationHandleEOSGS>(SessionModificationHandle) };

		return UpdateSessionImpl(MoveTemp(UpdateSessionImplParams));
	})
	.Then([this](TOnlineAsyncOp<FUpdateSession>& Op, TDefaultErrorResult<FUpdateSessionImpl>&& Result)
	{
		if (Result.IsOk())
		{
			const FUpdateSession::Params& OpParams = Op.GetParams();

			if (TSharedRef<FSession>* Session = LocalSessionsByName.Find(OpParams.SessionName))
			{
				// Now that the API Session update has processed successfully, we'll update our local session with the same data
				Session->Get().SessionSettings += OpParams.Mutations;

				// We set the result and fire the event
				Op.SetResult(FUpdateSession::Result{ *Session });

				FSessionUpdated SessionUpdatedEvent{ *Session, OpParams.Mutations };
				SessionEvents.OnSessionUpdated.Broadcast(SessionUpdatedEvent);
			}
			else
			{
				Op.SetError(Errors::NotFound());
			}
		}
		else
		{
			Op.SetError(MoveTemp(Result.GetErrorValue()));
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TFuture<TDefaultErrorResult<FUpdateSessionImpl>> FSessionsEOSGS::UpdateSessionImpl(FUpdateSessionImpl::Params&& Params)
{
	EOS_Sessions_UpdateSessionOptions UpdateSessionOptions = {};
	UpdateSessionOptions.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
	static_assert(EOS_SESSIONS_UPDATESESSION_API_LATEST == 1, "EOS_Sessions_UpdateSessionOptions updated, check new fields");

	UpdateSessionOptions.SessionModificationHandle = Params.SessionModificationHandle->ModificationHandle;

	TPromise<TDefaultErrorResult<FUpdateSessionImpl>> Promise;
	TFuture<TDefaultErrorResult<FUpdateSessionImpl>> Future = Promise.GetFuture();

	EOS_Async(EOS_Sessions_UpdateSession, SessionsHandle, UpdateSessionOptions,
	[this, Promise = MoveTemp(Promise)](const EOS_Sessions_UpdateSessionCallbackInfo* Result) mutable
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success && Result->ResultCode != EOS_EResult::EOS_Sessions_OutOfSync)
		{
			Promise.EmplaceValue(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		if (TSharedRef<FSession>* FoundSession = LocalSessionsByName.Find(FName(Result->SessionName)))
		{
			// In session creation calls, we'll need to set the new id for the session
			if (!FoundSession->Get().SessionId.IsValid())
			{
				FoundSession->Get().SessionId = CreateSessionId(FString(Result->SessionId));
			}
		}

		Promise.EmplaceValue(FUpdateSessionImpl::Result { });
	});

	return Future;
}

TOnlineAsyncOpHandle<FLeaveSession> FSessionsEOSGS::LeaveSession(FLeaveSession::Params&& Params)
{
	// LAN Sessions
	TOnlineResult<FGetSessionByName> Result = GetSessionByName({ Params.SessionName });
	if (Result.IsOk() && Result.GetOkValue().Session->SessionSettings.bIsLANSession)
	{
		return FSessionsLAN::LeaveSession(MoveTemp(Params));
	}

	// EOSGS Sessions

	TOnlineAsyncOpRef<FLeaveSession> Op = GetOp<FLeaveSession>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FLeaveSession>& Op, TPromise<const EOS_Sessions_DestroySessionCallbackInfo*>&& Promise)
	{
		const FLeaveSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckLeaveSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		EOS_Sessions_DestroySessionOptions DestroySessionOptions = {};
		DestroySessionOptions.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
		static_assert(EOS_SESSIONS_DESTROYSESSION_API_LATEST == 1, "EOS_Sessions_DestroySessionOptions updated, check new fields");

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		DestroySessionOptions.SessionName = SessionNameUtf8.Get();

		EOS_Async(EOS_Sessions_DestroySession, SessionsHandle, DestroySessionOptions, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FLeaveSession>& Op, const EOS_Sessions_DestroySessionCallbackInfo* Result)
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		const FLeaveSession::Params& OpParams = Op.GetParams();

		LocalSessionsByName.Remove(OpParams.SessionName);

		Op.SetResult(FLeaveSession::Result{ });

		FSessionLeft SessionLeftEvent;
		SessionLeftEvent.LocalUserIds.Add(OpParams.LocalUserId);
		//SessionLeftEvent.LocalUserIds.Append(OpParams.AdditionalLocalUsers); // TODO: Process multiple users in Session destruction
		SessionEvents.OnSessionLeft.Broadcast(SessionLeftEvent);

	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

void FSessionsEOSGS::SetSessionSearchMaxResults(FSessionSearchHandleEOSGS& SessionSearchHandle, uint32 MaxResults)
{
	EOS_SessionSearch_SetMaxResultsOptions Options = { };
	Options.ApiVersion = EOS_SESSIONSEARCH_SETMAXSEARCHRESULTS_API_LATEST;
	static_assert(EOS_SESSIONSEARCH_SETMAXSEARCHRESULTS_API_LATEST == 1, "EOS_SessionSearch_SetMaxResultsOptions updated, check new fields");

	// We truncate the max results parameter up to the value of EOS_SESSIONS_MAX_SEARCH_RESULTS
	Options.MaxSearchResults = FMath::Clamp(MaxResults, 1, EOS_SESSIONS_MAX_SEARCH_RESULTS);

	EOS_EResult ResultCode = EOS_SessionSearch_SetMaxResults(SessionSearchHandle.SearchHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetSessionSearchMaxResults] EOS_SessionSearch_SetMaxResults failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetSessionSearchParameters(FSessionSearchHandleEOSGS& SessionSearchHandle, TArray<FFindSessionsSearchFilter> Filters)
{
	// TODO: Pending SchemaVariant work
	// Here we'll call EOS_SessionSearch_SetParameter
}

void FSessionsEOSGS::SetSessionSearchSessionId(FSessionSearchHandleEOSGS& SessionSearchHandle, const FOnlineSessionIdHandle& SessionId)
{
	EOS_SessionSearch_SetSessionIdOptions Options = { };
	Options.ApiVersion = EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST;
	static_assert(EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST == 1, "EOS_SessionSearch_SetSessionIdOptions updated, check new fields");

	FTCHARToUTF8 SessionIdUtf8(FOnlineSessionIdRegistryEOSGS::Get().ToLogString(SessionId));
	Options.SessionId = SessionIdUtf8.Get();

	EOS_EResult ResultCode = EOS_SessionSearch_SetSessionId(SessionSearchHandle.SearchHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetSessionSearchSessionId] EOS_SessionSearch_SetSessionId failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetSessionSearchTargetId(FSessionSearchHandleEOSGS& SessionSearchHandle, const FOnlineAccountIdHandle& TargetUserId)
{
	EOS_SessionSearch_SetTargetUserIdOptions Options = { };
	Options.ApiVersion = EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST;
	static_assert(EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST == 1, "EOS_SessionSearch_SetTargetUserIdOptions updated, check new fields");
	Options.TargetUserId = GetProductUserIdChecked(TargetUserId);

	EOS_EResult ResultCode = EOS_SessionSearch_SetTargetUserId(SessionSearchHandle.SearchHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetSessionSearchTargetId] EOS_SessionSearch_SetTargetUserId failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::WriteSessionSearchHandle(FSessionSearchHandleEOSGS& SessionSearchHandle, const FFindSessions::Params& Params)
{
	SetSessionSearchMaxResults(SessionSearchHandle, Params.MaxResults);

	if (!Params.Filters.IsEmpty())
	{
		SetSessionSearchParameters(SessionSearchHandle, Params.Filters);
	}

	if (Params.SessionId.IsSet())
	{
		SetSessionSearchSessionId(SessionSearchHandle, Params.SessionId.GetValue());
	}

	if (Params.TargetUser.IsSet())
	{
		SetSessionSearchTargetId(SessionSearchHandle, Params.TargetUser.GetValue());
	}
}

FOnlineSessionIdHandle FSessionsEOSGS::CreateSessionId(const FString& SessionId)
{
	return FOnlineSessionIdRegistryEOSGS::Get().BasicRegistry.FindOrAddHandle(SessionId);
}

FOnlineSessionInviteIdHandle FSessionsEOSGS::CreateSessionInviteId(const FString& SessionInviteId) const
{
	return FOnlineSessionInviteIdRegistryEOSGS::Get().BasicRegistry.FindOrAddHandle(SessionInviteId);
}

TOnlineAsyncOpHandle<FFindSessions> FSessionsEOSGS::FindSessions(FFindSessions::Params&& Params)
{
	// LAN Sessions
	if (Params.bFindLANSessions)
	{
		return FSessionsLAN::FindSessions(MoveTemp(Params));
	}

	// EOSGS Sessions

	TOnlineAsyncOpRef<FFindSessions> Op = GetOp<FFindSessions>(MoveTemp(Params));
	const FFindSessions::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckFindSessionsParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	// We start the find operation
	Op->Then([this](TOnlineAsyncOp<FFindSessions>& Op, TPromise<const EOS_SessionSearch_FindCallbackInfo*>&& Promise)
	{
		const FFindSessions::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckFindSessionsState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		// Before we start the search, we reset the variables and cache
		CurrentSessionSearch = MakeShared<FFindSessions::Result>();
		CurrentSessionSearchHandle = Op.AsShared();

		SessionSearchResultsUserMap.FindOrAdd(OpParams.LocalUserId).Reset();

		// We start preparing the search
		EOS_Sessions_CreateSessionSearchOptions CreateSessionSearchOptions = { };
		CreateSessionSearchOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
		static_assert(EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST == 1, "EOS_Sessions_CreateSessionSearchOptions updated, check new fields");
		CreateSessionSearchOptions.MaxSearchResults = FMath::Clamp(OpParams.MaxResults, 0, EOS_SESSIONS_MAX_SEARCH_RESULTS);

		EOS_HSessionSearch SearchHandle = nullptr;
		EOS_EResult ResultCode = EOS_Sessions_CreateSessionSearch(SessionsHandle, &CreateSessionSearchOptions, &SearchHandle);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::FindSessions] EOS_Sessions_CreateSessionSearch failed with result [%s]"), *LexToString(ResultCode));

			Op.SetError(Errors::FromEOSResult(ResultCode));
			Promise.EmplaceValue();
			return;
		}

		CurrentSessionSearchHandleEOSGS = MakeShared<FSessionSearchHandleEOSGS>(SearchHandle);

		// We write the search attributes
		WriteSessionSearchHandle(*CurrentSessionSearchHandleEOSGS, OpParams);

		EOS_SessionSearch_FindOptions FindOptions = { };
		FindOptions.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_FIND_API_LATEST == 2, "EOS_SessionSearch_FindOptions updated, check new fields");
		FindOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);

		EOS_Async(EOS_SessionSearch_Find, CurrentSessionSearchHandleEOSGS->SearchHandle, FindOptions, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FFindSessions>& Op, const EOS_SessionSearch_FindCallbackInfo*&& FindCallbackInfoResult)
	{
		if (FindCallbackInfoResult->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(Errors::FromEOSResult(FindCallbackInfoResult->ResultCode));
			return;
		}

		// For a successful session, we'll get the search results
		EOS_SessionSearch_GetSearchResultCountOptions GetSearchResultCountOptions = { };
		GetSearchResultCountOptions.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST == 1, "EOS_SessionSearch_GetSearchResultCountOptions updated, check new fields");
		int32 NumSearchResults = EOS_SessionSearch_GetSearchResultCount(CurrentSessionSearchHandleEOSGS->SearchHandle, &GetSearchResultCountOptions);

		EOS_SessionSearch_CopySearchResultByIndexOptions CopySearchResultByIndexOptions = { };
		CopySearchResultByIndexOptions.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST == 1, "EOS_SessionSearch_CopySearchResultByIndexOptions updated, check new fields");

		for (int32 Index = 0; Index < NumSearchResults; Index++)
		{
			EOS_HSessionDetails SessionDetailsHandle = nullptr;

			CopySearchResultByIndexOptions.SessionIndex = Index;
			EOS_EResult CopySearchResultByIndexResult = EOS_SessionSearch_CopySearchResultByIndex(CurrentSessionSearchHandleEOSGS->SearchHandle, &CopySearchResultByIndexOptions, &SessionDetailsHandle);
			if (CopySearchResultByIndexResult == EOS_EResult::EOS_Success)
			{
				// All session attributes get extracted with the SessionDetailsHandle
				const TSharedRef<FSessionEOSGS>& NewSessionEOSGSRef = MakeShared<FSessionEOSGS>(SessionDetailsHandle);

				CurrentSessionSearch->FoundSessions.Add(NewSessionEOSGSRef);

				TMap<FOnlineSessionIdHandle, TSharedRef<FSession>>& UserMap = SessionSearchResultsUserMap.FindOrAdd(Op.GetParams().LocalUserId);
				UserMap.Emplace(NewSessionEOSGSRef->SessionId, NewSessionEOSGSRef);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::FindSessions] EOS_SessionSearch_CopySearchResultByIndex failed for index [%d] with result [%s]"), Index, *LexToString(CopySearchResultByIndexResult));
			}
		}

		Op.SetResult(MoveTemp(*CurrentSessionSearch));

		CurrentSessionSearch.Reset();
		CurrentSessionSearchHandleEOSGS.Reset();
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FJoinSession> FSessionsEOSGS::JoinSession(FJoinSession::Params&& Params)
{
	TOnlineAsyncOpRef<FJoinSession> Op = GetOp<FJoinSession>(MoveTemp(Params));
	const FJoinSession::Params& OpParams = Op->GetParams();

	TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalUserId, OpParams.SessionId });
	if (GetSessionByIdResult.IsError())
	{
		Op->SetError(MoveTemp(GetSessionByIdResult.GetErrorValue()));
		return Op->GetHandle();
	}

	const TSharedRef<const FSession>& FoundSession = GetSessionByIdResult.GetOkValue().Session;

	// LAN Sessions
	if (FoundSession->SessionSettings.bIsLANSession)
	{
		return FSessionsLAN::JoinSession(MoveTemp(Params));
	}

	// EOSGS Sessions

	// We check that the passed session has a valid details handle
	const FSessionEOSGS& SessionEOSGS = FSessionEOSGS::Cast(*FoundSession);
	if (!SessionEOSGS.SessionDetailsHandle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::FJoinSession] Could not join session with invalid session details handle in session with id [%s]"), *ToLogString(OpParams.SessionId));

		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	FOnlineError ParamsCheck = CheckJoinSessionParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	// We start the join operation
	Op->Then([this](TOnlineAsyncOp<FJoinSession>& Op, TPromise<const EOS_Sessions_JoinSessionCallbackInfo*>&& Promise)
	{
		const FJoinSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckJoinSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalUserId, OpParams.SessionId });
		if (GetSessionByIdResult.IsError())
		{
			Op.SetError(MoveTemp(GetSessionByIdResult.GetErrorValue()));
			Promise.EmplaceValue();
			return;
		}

		const TSharedRef<const FSession>& FoundSession = GetSessionByIdResult.GetOkValue().Session;
		TSharedRef<FSession> NewSessionRef = ConstCastSharedRef<FSession>(FoundSession);
		NewSessionRef->CurrentState = ESessionState::Joining;

		LocalSessionsByName.Add(OpParams.SessionName, NewSessionRef);

		// We start setup for the API call
		EOS_Sessions_JoinSessionOptions JoinSessionOptions = { };
		JoinSessionOptions.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
		static_assert(EOS_SESSIONS_JOINSESSION_API_LATEST == 2, "EOS_Sessions_JoinSessionOptions updated, check new fields");

		JoinSessionOptions.bPresenceEnabled = FoundSession->SessionSettings.bPresenceEnabled;

		JoinSessionOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		JoinSessionOptions.SessionName = SessionNameUtf8.Get();

		const FSessionEOSGS& SessionEOSGS = FSessionEOSGS::Cast(*FoundSession);
		JoinSessionOptions.SessionHandle = SessionEOSGS.SessionDetailsHandle->SessionDetailsHandle;

		EOS_Async(EOS_Sessions_JoinSession, SessionsHandle, JoinSessionOptions, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FJoinSession>& Op, const EOS_Sessions_JoinSessionCallbackInfo*&& Result)
	{
		const FJoinSession::Params& OpParams = Op.GetParams();

		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			LocalSessionsByName.Remove(OpParams.SessionName);

			Op.SetError(Errors::FromEOSResult(Result->ResultCode));
			return;
		}
		
		if (TSharedRef<FSession>* FoundSession = LocalSessionsByName.Find(OpParams.SessionName))
		{
			FoundSession->Get().CurrentState = ESessionState::Valid;

			// After successfully joining a session, we'll remove all related invites if any are found
			if (TMap<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(OpParams.LocalUserId))
			{
				TArray<FOnlineSessionInviteIdHandle> InviteIdsToRemove;
				for (const TPair<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>& Entry : *UserMap)
				{
					if (Entry.Value->Session->SessionId == FoundSession->Get().SessionId)
					{
						InviteIdsToRemove.Add(Entry.Key);
					}
				}
				for (const FOnlineSessionInviteIdHandle& InviteId : InviteIdsToRemove)
				{
					UserMap->Remove(InviteId);
				}
			}

			FSessionJoined Event = { { OpParams.LocalUserId }, *FoundSession };

			SessionEvents.OnSessionJoined.Broadcast(Event);
		}

		// A successful join allows the client to server travel, after which RegisterPlayers will be called by the engine

		// TODO: Support multiple local users joining the session
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TResult<TSharedRef<const FSession>, FOnlineError> FSessionsEOSGS::BuildSessionFromInvite(const FString& InInviteId) const
{
	EOS_Sessions_CopySessionHandleByInviteIdOptions CopySessionHandleByInviteIdOptions = { };
	CopySessionHandleByInviteIdOptions.ApiVersion = EOS_SESSIONS_COPYSESSIONHANDLEBYINVITEID_API_LATEST;
	static_assert(EOS_SESSIONS_COPYSESSIONHANDLEBYINVITEID_API_LATEST == 1, "EOS_Sessions_CopySessionHandleByInviteIdOptions updated, check new fields");

	FTCHARToUTF8 InviteIdUtf8(InInviteId);
	CopySessionHandleByInviteIdOptions.InviteId = InviteIdUtf8.Get();

	EOS_HSessionDetails SessionDetailsHandle;
	EOS_EResult CopySessionHandleByInviteIdResult = EOS_Sessions_CopySessionHandleByInviteId(SessionsHandle, &CopySessionHandleByInviteIdOptions, &SessionDetailsHandle);
	if (CopySessionHandleByInviteIdResult == EOS_EResult::EOS_Success)
	{
		return TResult<TSharedRef<const FSession>, FOnlineError>(MakeShared<const FSessionEOSGS>(SessionDetailsHandle));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::BuildSessionFromInvite] EOS_Sessions_CopySessionHandleByInviteId failed with result [%s]"), *LexToString(CopySessionHandleByInviteIdResult));

		return TResult<TSharedRef<const FSession>, FOnlineError>(Errors::FromEOSResult(CopySessionHandleByInviteIdResult));
	}
}

TResult<TSharedRef<const FSession>, FOnlineError> FSessionsEOSGS::BuildSessionFromUIEvent(const EOS_UI_EventId& UIEventId) const
{
	EOS_Sessions_CopySessionHandleByUiEventIdOptions CopySessionHandleByUiEventIdOptions = { };
	CopySessionHandleByUiEventIdOptions.ApiVersion = EOS_SESSIONS_COPYSESSIONHANDLEBYUIEVENTID_API_LATEST;
	static_assert(EOS_SESSIONS_COPYSESSIONHANDLEBYUIEVENTID_API_LATEST == 1, "EOS_Sessions_CopySessionHandleByUiEventIdOptions updated, check new fields");

	CopySessionHandleByUiEventIdOptions.UiEventId = UIEventId;

	EOS_HSessionDetails SessionDetailsHandle;
	EOS_EResult CopySessionHandleByUiEventIdResult = EOS_Sessions_CopySessionHandleByUiEventId(SessionsHandle, &CopySessionHandleByUiEventIdOptions, &SessionDetailsHandle);
	if (CopySessionHandleByUiEventIdResult == EOS_EResult::EOS_Success)
	{
		return TResult<TSharedRef<const FSession>, FOnlineError>(MakeShared<FSessionEOSGS>(SessionDetailsHandle));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::BuildSessionFromUIEvent] EOS_Sessions_CopySessionHandleByUiEventId failed with result [%s]"), *LexToString(CopySessionHandleByUiEventIdResult));

		return TResult<TSharedRef<const FSession>, FOnlineError>(Errors::FromEOSResult(CopySessionHandleByUiEventIdResult));
	}
}

TOnlineAsyncOpHandle<FSendSessionInvite> FSessionsEOSGS::SendSessionInvite(FSendSessionInvite::Params&& Params)
{
	TOnlineAsyncOpRef<FSendSessionInvite> Op = GetOp<FSendSessionInvite>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FSendSessionInvite>& Op, TPromise<const EOS_Sessions_SendInviteCallbackInfo*>&& Promise)
	{
		const FSendSessionInvite::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckSendSessionInviteState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		EOS_Sessions_SendInviteOptions SendInviteOptions = { };
		SendInviteOptions.ApiVersion = EOS_SESSIONS_SENDINVITE_API_LATEST;
		static_assert(EOS_SESSIONS_SENDINVITE_API_LATEST == 1, "EOS_Sessions_SendInviteOptions updated, check new fields");

		SendInviteOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);
		
		const FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		SendInviteOptions.SessionName = SessionNameUtf8.Get();

		SendInviteOptions.TargetUserId = GetProductUserIdChecked(OpParams.TargetUsers[0]); // TODO: Multiple user invitations using WhenAll like JoinLobbyImpl

		EOS_Async(EOS_Sessions_SendInvite, SessionsHandle, SendInviteOptions, MoveTemp(Promise));
		return;
	})
	.Then([this](TOnlineAsyncOp<FSendSessionInvite>& Op, const EOS_Sessions_SendInviteCallbackInfo* Result)
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		Op.SetResult(FSendSessionInvite::Result{ });
	})
	.Enqueue(GetSerialQueue()); // TODO: Use the parallel queue instead when possible

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FRejectSessionInvite> FSessionsEOSGS::RejectSessionInvite(FRejectSessionInvite::Params&& Params)
{
	TOnlineAsyncOpRef<FRejectSessionInvite> Op = GetOp<FRejectSessionInvite>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FRejectSessionInvite>& Op, TPromise<const EOS_Sessions_RejectInviteCallbackInfo*>&& Promise)
	{
		const FRejectSessionInvite::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckRejectSessionInviteState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		EOS_Sessions_RejectInviteOptions RejectInviteOptions = { };
		RejectInviteOptions.ApiVersion = EOS_SESSIONS_REJECTINVITE_API_LATEST;
		static_assert(EOS_SESSIONS_REJECTINVITE_API_LATEST == 1, "EOS_Sessions_RejectInviteOptions updated, check new fields");

		RejectInviteOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);

		const FString& InviteIdStr = FOnlineSessionInviteIdRegistryEOSGS::Get().BasicRegistry.FindIdValue(OpParams.SessionInviteId);
		const FTCHARToUTF8 InviteIdUtf8(InviteIdStr);
		RejectInviteOptions.InviteId = InviteIdUtf8.Get();

		EOS_Async(EOS_Sessions_RejectInvite, SessionsHandle, RejectInviteOptions, MoveTemp(Promise));
		return;
	})
	.Then([this](TOnlineAsyncOp<FRejectSessionInvite>& Op, const EOS_Sessions_RejectInviteCallbackInfo* Result)
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		const FRejectSessionInvite::Params& OpParams = Op.GetParams();

		if (TMap<FOnlineSessionInviteIdHandle, TSharedRef<FSessionInvite>>* UserMap = SessionInvitesUserMap.Find(OpParams.LocalUserId))
		{
			UserMap->Remove(OpParams.SessionInviteId);
		}

		Op.SetResult(FRejectSessionInvite::Result{ });
	})
	.Enqueue(GetSerialQueue()); // TODO: Use the parallel queue instead when possible

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FAddSessionMembers> FSessionsEOSGS::AddSessionMembers(FAddSessionMembers::Params&& Params)
{
	// LAN Sessions
	TOnlineResult<FGetSessionByName> Result = GetSessionByName({ Params.SessionName });
	if (Result.IsOk() && Result.GetOkValue().Session->SessionSettings.bIsLANSession)
	{
		return FSessionsLAN::AddSessionMembers(MoveTemp(Params));
	}

	// EOSGS Sessions

	TOnlineAsyncOpRef<FAddSessionMembers> Op = GetOp<FAddSessionMembers>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FAddSessionMembers>& Op, TPromise<const EOS_Sessions_RegisterPlayersCallbackInfo*>&& Promise)
	{
		const FAddSessionMembers::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckAddSessionMembersState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		EOS_Sessions_RegisterPlayersOptions Options = { };
		Options.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
		static_assert(EOS_SESSIONS_REGISTERPLAYERS_API_LATEST == 2, "EOS_Sessions_RegisterPlayersOptions updated, check new fields");

		Options.PlayersToRegisterCount = OpParams.NewSessionMembers.Num();

		TArray<EOS_ProductUserId> PlayersToRegister;
		TArray<FOnlineAccountIdHandle> SessionMemberIds;
		OpParams.NewSessionMembers.GenerateKeyArray(SessionMemberIds);		
		for (FOnlineAccountIdHandle IdHandle : SessionMemberIds)
		{
			// TODO: Do this with ResolveAccountIds instead
			PlayersToRegister.Add(GetProductUserIdChecked(IdHandle));
		}

		Options.PlayersToRegister = PlayersToRegister.GetData();

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		Options.SessionName = SessionNameUtf8.Get();

		EOS_Async(EOS_Sessions_RegisterPlayers, SessionsHandle, Options, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FAddSessionMembers>& Op, const EOS_Sessions_RegisterPlayersCallbackInfo* Result)
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		TOnlineResult<FAddSessionMembers> ImplResult = AddSessionMembersImpl(Op.GetParams());
		if (ImplResult.IsOk())
		{
			Op.SetResult(MoveTemp(ImplResult.GetOkValue()));
		}
		else
		{
			Op.SetError(MoveTemp(ImplResult.GetErrorValue()));
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FRemoveSessionMembers> FSessionsEOSGS::RemoveSessionMembers(FRemoveSessionMembers::Params&& Params)
{
	// LAN Sessions
	TOnlineResult<FGetSessionByName> Result = GetSessionByName({ Params.SessionName });
	if (Result.IsOk() && Result.GetOkValue().Session->SessionSettings.bIsLANSession)
	{
		return FSessionsLAN::RemoveSessionMembers(MoveTemp(Params));
	}

	// EOSGS Sessions

	TOnlineAsyncOpRef<FRemoveSessionMembers> Op = GetOp<FRemoveSessionMembers>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FRemoveSessionMembers>& Op, TPromise<const EOS_Sessions_UnregisterPlayersCallbackInfo*>&& Promise)
	{
		const FRemoveSessionMembers::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckRemoveSessionMembersState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			Promise.EmplaceValue();
			return;
		}

		EOS_Sessions_UnregisterPlayersOptions Options = { };
		Options.ApiVersion = EOS_SESSIONS_UNREGISTERPLAYERS_API_LATEST;
		static_assert(EOS_SESSIONS_UNREGISTERPLAYERS_API_LATEST == 2, "EOS_Sessions_UnregisterPlayersOptions updated, check new fields");

		Options.PlayersToUnregisterCount = OpParams.SessionMemberIds.Num();

		TArray<EOS_ProductUserId> PlayersToUnregister;
		for (FOnlineAccountIdHandle IdHandle : OpParams.SessionMemberIds)
		{
			// TODO: Do this with ResolveAccountIds instead
			PlayersToUnregister.Add(GetProductUserIdChecked(IdHandle));
		}

		Options.PlayersToUnregister = PlayersToUnregister.GetData();

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		Options.SessionName = SessionNameUtf8.Get();

		EOS_Async(EOS_Sessions_UnregisterPlayers, SessionsHandle, Options, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FRemoveSessionMembers>& Op, const EOS_Sessions_UnregisterPlayersCallbackInfo* Result)
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		TOnlineResult<FRemoveSessionMembers> ImplResult = RemoveSessionMembersImpl(Op.GetParams());
		if (ImplResult.IsOk())
		{
			Op.SetResult(MoveTemp(ImplResult.GetOkValue()));
		}
		else
		{
			Op.SetError(MoveTemp(ImplResult.GetErrorValue()));
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/** FSessionsLAN */

void FSessionsEOSGS::AppendSessionToPacket(FNboSerializeToBuffer& Packet, const FSessionLAN& Session)
{
	using namespace NboSerializerEOSGSSvc;

	// We won't save CurrentState in the packet as all advertised sessions will be Valid
	SerializeToBuffer(Packet, Session.OwnerUserId);
	SerializeToBuffer(Packet, Session.SessionId);
	Packet << *Session.OwnerInternetAddr;

	// TODO: Write session settings to packet, after SchemaVariant work
}

void FSessionsEOSGS::ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionLAN& Session)
{
	using namespace NboSerializerEOSGSSvc;

	SerializeFromBuffer(Packet, Session.OwnerUserId);
	SerializeFromBuffer(Packet, Session.SessionId);
	Packet >> *Session.OwnerInternetAddr;

	// We'll set the connect address for the remote session as a custom parameter, so it can be read in OnlineServices' GetResolvedConnectString
	FCustomSessionSetting ConnectString;
	ConnectString.Data.Set<FString>(Session.OwnerInternetAddr->ToString(true));
	ConnectString.Visibility = ECustomSessionSettingVisibility::ViaOnlineService;
	Session.SessionSettings.CustomSettings.Add(CONNECT_STRING_TAG, ConnectString);

	// TODO:: Read session settings from packet, after SchemaVariant work
}

/* UE::Online */ }