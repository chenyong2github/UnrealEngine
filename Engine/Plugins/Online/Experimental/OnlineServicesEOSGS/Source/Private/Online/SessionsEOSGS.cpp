// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsEOSGS.h"

#include "Online/AuthEOSGS.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesEOSGSTypes.h"

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

/** FSessionEOSGS */

FSessionEOSGS::FSessionEOSGS()
	: FSessionEOSGS(FSession())
{
}

FSessionEOSGS::FSessionEOSGS(const FSession& InSession)
	: FSession(InSession)
{
	const FSessionEOSGS& Session = Cast(InSession);

	SessionDetailsHandle = Session.SessionDetailsHandle;
}

FSessionEOSGS::FSessionEOSGS(const EOS_HSessionDetails& InSessionDetailsHandle)
	: SessionDetailsHandle(MakeShareable<FSessionDetailsHandleEOSGS>(new FSessionDetailsHandleEOSGS(InSessionDetailsHandle)))
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

	return *static_cast<const FSessionEOSGS*>(&InSession);
}

/** FSessionsEOSGS */

FSessionsEOSGS::FSessionsEOSGS(FOnlineServicesEOSGS& InServices)
	: Super(InServices)
	, Services(InServices)
{
}

void FSessionsEOSGS::Initialize()
{
	Super::Initialize();

	SessionsHandle = EOS_Platform_GetSessionsInterface(static_cast<FOnlineServicesEOSGS&>(GetServices()).GetEOSPlatformHandle());
	check(SessionsHandle);
}

TOnlineAsyncOpHandle<FCreateSession> FSessionsEOSGS::CreateSession(FCreateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FCreateSession> Op = GetOp<FCreateSession>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FCreateSession>& Op)
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		if (TSharedRef<FSessionEOSGS>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::CreateSession] Could not create session with name [%s]. A session with that name already exists"), *OpParams.SessionName.ToString());

			Op.SetError(Errors::InvalidState()); // TODO: New error: Session with name %s already exists

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		if (OpParams.SessionSettings.NumMaxPrivateConnections == 0 && OpParams.SessionSettings.NumMaxPublicConnections == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::CreateSession] Could not create session with no valid NumMaxPrivateConnections [%d] or NumMaxPublicConnections [%d]"), OpParams.SessionSettings.NumMaxPrivateConnections, OpParams.SessionSettings.NumMaxPublicConnections);

			Op.SetError(Errors::InvalidParams());

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		if (!OpParams.SessionSettings.IsDedicatedServerSession)
		{
			IAuthPtr Auth = Services.GetAuthInterface();

			FAuthGetAccountByAccountId::Params GetAccountByAccountIParams { OpParams.LocalUserId };
			TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId(MoveTemp(GetAccountByAccountIParams));

			if (Result.IsOk())
			{
				if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
				{
					UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::CreateSession] Could not create session with user [%s] not logged in"), *ToLogString(OpParams.LocalUserId));

					Op.SetError(Errors::InvalidUser());

					return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
				}
			}
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

			Op.SetError(FromEOSError(ResultCode));

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		// TODO: We could call EOS_SessionModification_SetHostAddress at this point, although it's not necessary

		// We write all SessionSettings values into the SessionModificationHandle
		WriteCreateSessionModificationHandle(SessionModificationHandle, OpParams.SessionSettings);

		// If the session modification handle is created successfully, we'll create the local session object
		FSessionEOSGS NewSession = FSessionEOSGS();
		NewSession.CurrentState = ESessionState::Creating;
		NewSession.OwnerUserId = OpParams.LocalUserId;
		NewSession.SessionSettings = OpParams.SessionSettings;

 		TSharedRef<FSessionEOSGS> NewSessionEOSGSRef = MakeShared<FSessionEOSGS>(NewSession);

 		SessionsByName.Add(OpParams.SessionName, NewSessionEOSGSRef);
 		SessionsById.Add(NewSession.SessionId, NewSessionEOSGSRef);

		FUpdateSessionImpl::Params UpdateSessionImplParams { SessionModificationHandle };
		
		return UpdateSessionImpl(MoveTemp(UpdateSessionImplParams));
	})
	.Then([this](TOnlineAsyncOp<FCreateSession>& Op, TDefaultErrorResult<FUpdateSessionImpl>&& Result)
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		if (Result.IsOk())
		{
			if (TSharedRef<FSessionEOSGS>* Session = SessionsByName.Find(OpParams.SessionName))
			{
				Session->Get().CurrentState = ESessionState::Valid;

				// TODO: Make additional local users join the created session

				Op.SetResult(FCreateSession::Result{ *Session });
			}
			else
			{
				Op.SetError(Errors::NotFound());
			}
		}
		else
		{	
			// If creation failed, we remove the session objects
			if (TSharedRef<FSessionEOSGS>* Session = SessionsByName.Find(OpParams.SessionName))
			{
				SessionsByName.Remove(OpParams.SessionName);
				SessionsById.Remove(Session->Get().SessionId);
			}

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
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_SetMaxPlayers failed with result [%s]"), *LexToString(ResultCode));
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

void FSessionsEOSGS::WriteUpdateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, FSessionSettings& SessionSettings, const FSessionSettingsUpdate& NewSettings)
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

	if (NewSettings.bAllowNewMembers.IsSet())
	{
		SessionSettings.bAllowNewMembers = NewSettings.bAllowNewMembers.GetValue();
	}

	if (NewSettings.bAllowSanctionedPlayers.IsSet())
	{
		SessionSettings.bAllowSanctionedPlayers = NewSettings.bAllowSanctionedPlayers.GetValue();
	}

	if (NewSettings.bAllowUnregisteredPlayers.IsSet())
	{
		SessionSettings.bAllowUnregisteredPlayers = NewSettings.bAllowUnregisteredPlayers.GetValue();
	}

	if (NewSettings.bAntiCheatProtected.IsSet())
	{
		SessionSettings.bAntiCheatProtected = NewSettings.bAntiCheatProtected.GetValue();
	}

	if (NewSettings.bPresenceEnabled.IsSet())
	{
		SessionSettings.bPresenceEnabled = NewSettings.bPresenceEnabled.GetValue();
	}

	if (NewSettings.IsDedicatedServerSession.IsSet())
	{
		SessionSettings.IsDedicatedServerSession = NewSettings.IsDedicatedServerSession.GetValue();
	}

	if (NewSettings.IsLANSession.IsSet())
	{
		SessionSettings.IsLANSession = NewSettings.IsLANSession.GetValue();
	}

	if (NewSettings.JoinPolicy.IsSet())
	{
		SessionSettings.JoinPolicy = NewSettings.JoinPolicy.GetValue();

		SetPermissionLevel(SessionModificationHandle, SessionSettings.JoinPolicy);
	}

	bool bMaxPlayersUpdated = false;

	if (NewSettings.NumMaxPrivateConnections.IsSet())
	{
		SessionSettings.NumMaxPrivateConnections = NewSettings.NumMaxPrivateConnections.GetValue();
		bMaxPlayersUpdated = true;
	}

	if (NewSettings.NumMaxPublicConnections.IsSet())
	{
		SessionSettings.NumMaxPublicConnections = NewSettings.NumMaxPublicConnections.GetValue();
		bMaxPlayersUpdated = true;
	}

	if (bMaxPlayersUpdated)
	{
		SetMaxPlayers(SessionModificationHandle, SessionSettings.NumMaxPrivateConnections + SessionSettings.NumMaxPublicConnections);
	}

	if (NewSettings.NumOpenPrivateConnections.IsSet())
	{
		SessionSettings.NumOpenPrivateConnections = NewSettings.NumOpenPrivateConnections.GetValue();
	}

	if (NewSettings.NumOpenPublicConnections.IsSet())
	{
		SessionSettings.NumOpenPublicConnections = NewSettings.NumOpenPublicConnections.GetValue();
	}

	// TODO: We may need some additional logic for schema changes
	if (NewSettings.SchemaName.IsSet())
	{
		SessionSettings.SchemaName = NewSettings.SchemaName.GetValue();
	}

	if (NewSettings.SessionIdOverride.IsSet())
	{
		SessionSettings.SessionIdOverride = NewSettings.SessionIdOverride.GetValue();
	}

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

	// BucketId has its own set method on the API
	const FCustomSessionSetting* NewBucketIdSetting = NewSettings.UpdatedCustomSettings.Find(EOS_SESSIONS_BUCKET_ID);
	if (NewBucketIdSetting)
	{
		SetBucketId(SessionModificationHandle, NewBucketIdSetting->Data.Get<FString>());
	}

	// Registered Users

	for (const FOnlineAccountIdHandle& Key : NewSettings.RemovedRegisteredUsers)
	{
		SessionSettings.RegisteredUsers.Remove(Key);
	}

	SessionSettings.RegisteredUsers.Append(NewSettings.UpdatedRegisteredUsers);

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
}

TOnlineAsyncOpHandle<FUpdateSession> FSessionsEOSGS::UpdateSession(FUpdateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdateSession> Op = GetOp<FUpdateSession>(MoveTemp(Params));
	const FUpdateSession::Params& OpParams = Op->GetParams();

	Op->Then([this](TOnlineAsyncOp<FUpdateSession>& Op)
	{
		const FUpdateSession::Params& OpParams = Op.GetParams();

		if (TSharedRef<FSessionEOSGS>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			if (FoundSession->Get().CurrentState != ESessionState::Valid)
			{
				Op.SetError(Errors::InvalidState());

				return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
			}

			if (!FoundSession->Get().SessionSettings.IsDedicatedServerSession)
			{
				IAuthPtr Auth = Services.GetAuthInterface();

				TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId({ OpParams.LocalUserId });

				if (Result.IsOk())
				{
					if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
					{
						UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::UpdateSession] Could not update session with user [%s] not logged in"), *ToLogString(OpParams.LocalUserId));

						Op.SetError(Errors::InvalidUser());

						return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
					}
				}
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

				Op.SetError(FromEOSError(ResultCode));

				return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
			}

			// After creating the session modification handle, we'll add all the updated data to it
			WriteUpdateSessionModificationHandle(SessionModificationHandle, FoundSession->Get().SessionSettings, OpParams.Mutations);

			FUpdateSessionImpl::Params UpdateSessionImplParams{ SessionModificationHandle };

			return UpdateSessionImpl(MoveTemp(UpdateSessionImplParams));
		}
		else
		{
			Op.SetError(Errors::NotFound());

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}
	})
	.Then([this](TOnlineAsyncOp<FUpdateSession>& Op, TDefaultErrorResult<FUpdateSessionImpl>&& Result)
	{
		if (Result.IsOk())
		{
			const FUpdateSession::Params& OpParams = Op.GetParams();

			if (TSharedRef<FSessionEOSGS>* Session = SessionsByName.Find(OpParams.SessionName))
			{
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

	UpdateSessionOptions.SessionModificationHandle = Params.SessionModificationHandle;

	TPromise<TDefaultErrorResult<FUpdateSessionImpl>> Promise;
	TFuture<TDefaultErrorResult<FUpdateSessionImpl>> Future = Promise.GetFuture();

	EOS_Async(EOS_Sessions_UpdateSession, SessionsHandle, UpdateSessionOptions,
	[this, Promise = MoveTemp(Promise)](const EOS_Sessions_UpdateSessionCallbackInfo* Result) mutable
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success && Result->ResultCode != EOS_EResult::EOS_Sessions_OutOfSync)
		{
			Promise.EmplaceValue(FromEOSError(Result->ResultCode));
			return;
		}

		if (TSharedRef<FSessionEOSGS>* FoundSession = SessionsByName.Find(FName(Result->SessionName)))
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
	TOnlineAsyncOpRef<FLeaveSession> Op = GetOp<FLeaveSession>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FLeaveSession>& Op, TPromise<const EOS_Sessions_DestroySessionCallbackInfo*>&& Promise)
	{
		const FLeaveSession::Params& OpParams = Op.GetParams();

		// We do not check if OpParams.LocalUserId is logged in because it might be a dedicated server destroying the session

		if (TSharedRef<FSessionEOSGS>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			EOS_Sessions_DestroySessionOptions DestroySessionOptions = {};
			DestroySessionOptions.ApiVersion = EOS_SESSIONS_DESTROYSESSION_API_LATEST;
			static_assert(EOS_SESSIONS_DESTROYSESSION_API_LATEST == 1, "EOS_Sessions_DestroySessionOptions updated, check new fields");

			FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
			DestroySessionOptions.SessionName = SessionNameUtf8.Get();

			EOS_Async(EOS_Sessions_DestroySession, SessionsHandle, DestroySessionOptions, MoveTemp(Promise));
			return;
		}
		else
		{
			Op.SetError(Errors::NotFound());
		}
		Promise.EmplaceValue();
	})
	.Then([this](TOnlineAsyncOp<FLeaveSession>& Op, const EOS_Sessions_DestroySessionCallbackInfo* Result)
	{
		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(FromEOSError(Result->ResultCode));
			return;
		}

		const FLeaveSession::Params& OpParams = Op.GetParams();

		if (TSharedRef<FSessionEOSGS>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			SessionsById.Remove((*FoundSession)->SessionId);
			SessionsByName.Remove(OpParams.SessionName);

			Op.SetResult(FLeaveSession::Result{ });

			FSessionLeft SessionLeftEvent;
			SessionLeftEvent.LocalUserIds.Add(OpParams.LocalUserId);
			//SessionLeftEvent.LocalUserIds.Append(OpParams.AdditionalLocalUsers); // TODO: Process multiple users in Session destruction
			SessionEvents.OnSessionLeft.Broadcast(SessionLeftEvent);
		}
		else
		{
			Op.SetError(Errors::NotFound());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

void FSessionsEOSGS::WriteSessionSearchHandle(FSessionSearchHandleEOSGS& SessionSearchHandle, const FFindSessions::Params& Params)
{
	if (Params.MaxResults > 0)
	{
		EOS_SessionSearch_SetMaxResultsOptions Options = { };
		Options.ApiVersion = EOS_SESSIONSEARCH_SETMAXSEARCHRESULTS_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_SETMAXSEARCHRESULTS_API_LATEST == 1, "EOS_SessionSearch_SetMaxResultsOptions updated, check new fields");

		// We truncate the max results parameter up to the value of EOS_SESSIONS_MAX_SEARCH_RESULTS
		Options.MaxSearchResults = FMath::Clamp(Params.MaxResults, 1, EOS_SESSIONS_MAX_SEARCH_RESULTS);

		EOS_EResult ResultCode = EOS_SessionSearch_SetMaxResults(SessionSearchHandle.SearchHandle, &Options);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::WriteSessionSearchHandle] EOS_SessionSearch_SetMaxResults failed with result [%s]"), *LexToString(ResultCode));
		}
	}

	if (!Params.Filters.IsEmpty())
	{
		// TODO: Pending SchemaVariant work
		// Here we'll call EOS_SessionSearch_SetParameter
	}

	if (Params.SessionId.IsSet())
	{
		EOS_SessionSearch_SetSessionIdOptions Options = { };
		Options.ApiVersion = EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST == 1, "EOS_SessionSearch_SetSessionIdOptions updated, check new fields");

		FTCHARToUTF8 SessionIdUtf8(FOnlineSessionIdRegistryEOSGS::Get().ToLogString(*Params.SessionId));
		Options.SessionId = SessionIdUtf8.Get();

		EOS_EResult ResultCode = EOS_SessionSearch_SetSessionId(SessionSearchHandle.SearchHandle, &Options);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::WriteSessionSearchHandle] EOS_SessionSearch_SetSessionId failed with result [%s]"), *LexToString(ResultCode));
		}
	}

	if (Params.TargetUser.IsSet())
	{
		EOS_SessionSearch_SetTargetUserIdOptions Options = { };
		Options.ApiVersion = EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST == 1, "EOS_SessionSearch_SetTargetUserIdOptions updated, check new fields");
		Options.TargetUserId = GetProductUserIdChecked(*Params.TargetUser);

		EOS_EResult ResultCode = EOS_SessionSearch_SetTargetUserId(SessionSearchHandle.SearchHandle, &Options);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::WriteSessionSearchHandle] EOS_SessionSearch_SetTargetUserId failed with result [%s]"), *LexToString(ResultCode));
		}
	}
}

FOnlineSessionIdHandle FSessionsEOSGS::CreateSessionId(const FString& SessionId)
{
	return FOnlineSessionIdRegistryEOSGS::Get().BasicRegistry.FindOrAddHandle(SessionId);
}

TOnlineAsyncOpHandle<FFindSessions> FSessionsEOSGS::FindSessions(FFindSessions::Params&& Params)
{
	TOnlineAsyncOpRef<FFindSessions> Op = GetOp<FFindSessions>(MoveTemp(Params));
	const FFindSessions::Params& OpParams = Op->GetParams();

	// We check if the user is logged in
	IAuthPtr Auth = Services.GetAuthInterface();
	TOnlineResult<FAuthGetAccountByAccountId> Result = Auth->GetAccountByAccountId({ OpParams.LocalUserId });

	if (Result.IsOk())
	{
		if (Result.GetOkValue().AccountInfo->LoginStatus == ELoginStatus::NotLoggedIn)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::FindSessions] Could not find sessions with user [%s] not logged in"), *ToLogString(OpParams.LocalUserId));

			Op->SetError(Errors::InvalidUser());
			return Op->GetHandle();
		}
	}

	// We check if MaxResults has a valid value
	if (OpParams.MaxResults > 0 && OpParams.MaxResults < EOS_SESSIONS_MAX_SEARCH_RESULTS)
	{
		UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::FindSessions] FFindSessions::Params::MaxResults had invalid value [%d]. Value must be between [0-%d]"), Params.MaxResults, EOS_SESSIONS_MAX_SEARCH_RESULTS);

		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	// We start the find operation
	Op->Then([this](TOnlineAsyncOp<FFindSessions>& Op, TPromise<const EOS_SessionSearch_FindCallbackInfo*>&& Promise)
	{
		if (!CurrentSessionSearch.IsValid())
		{
			CurrentSessionSearch = MakeShared<FFindSessions::Result>();
			CurrentSessionSearchAsyncOpHandle = Op.AsShared();

			const FFindSessions::Params& OpParams = Op.GetParams();

			EOS_Sessions_CreateSessionSearchOptions CreateSessionSearchOptions = { };
			CreateSessionSearchOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST;
			static_assert(EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST == 1, "EOS_Sessions_CreateSessionSearchOptions updated, check new fields");
			CreateSessionSearchOptions.MaxSearchResults = FMath::Clamp(OpParams.MaxResults, 0, EOS_SESSIONS_MAX_SEARCH_RESULTS);

			EOS_HSessionSearch SearchHandle = nullptr;
			EOS_EResult ResultCode = EOS_Sessions_CreateSessionSearch(SessionsHandle, &CreateSessionSearchOptions, &SearchHandle);
			if (ResultCode != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::FindSessions] EOS_Sessions_CreateSessionSearch failed with result [%s]"), *LexToString(ResultCode));

				Op.SetError(FromEOSError(ResultCode));

				Promise.EmplaceValue();
				return;
			}

			CurrentSessionSearchHandleEOSGS = MakeShareable(new FSessionSearchHandleEOSGS(SearchHandle));

			// We write the search attributes
			WriteSessionSearchHandle(*CurrentSessionSearchHandleEOSGS, OpParams);

			EOS_SessionSearch_FindOptions FindOptions = { };
			FindOptions.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
			static_assert(EOS_SESSIONSEARCH_FIND_API_LATEST == 2, "EOS_SessionSearch_FindOptions updated, check new fields");
			FindOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);

			EOS_Async(EOS_SessionSearch_Find, CurrentSessionSearchHandleEOSGS->SearchHandle, FindOptions, MoveTemp(Promise));
			return;
		}
		else
		{
			Op.SetError(Errors::AlreadyPending());
		}
		Promise.EmplaceValue();
	})
	.Then([this](TOnlineAsyncOp<FFindSessions>& Op, const EOS_SessionSearch_FindCallbackInfo*&& FindCallbackInfoResult)
	{
		if (FindCallbackInfoResult->ResultCode != EOS_EResult::EOS_Success)
		{
			Op.SetError(FromEOSError(FindCallbackInfoResult->ResultCode));
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
				FSessionEOSGS NewSession = FSessionEOSGS(SessionDetailsHandle);

				TSharedRef<FSessionEOSGS> NewSessionEOSGSRef = MakeShared<FSessionEOSGS>(NewSession);

				CurrentSessionSearch->FoundSessions.Add(NewSessionEOSGSRef);
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

	// TODO: Call CheckJoinSessionParams after it's submitted, add the state check to it
	// 
	// We check that the passed session is valid
	if (Params.Session->CurrentState != ESessionState::Valid)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::FJoinSession] Could not join session with invalid session state [%s]"), *LexToString(Params.Session->CurrentState));

		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	// We check that the passed session has a valid details handle
	const FSessionEOSGS& SessionEOSGS = FSessionEOSGS::Cast(*Params.Session);
	if (!SessionEOSGS.SessionDetailsHandle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::FJoinSession] Could not join session with invalid session details handle in session with id [%s]"), *ToLogString(Params.Session->SessionId));

		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	// TODO: Routine JoinSession checks

	// We start the join operation
	Op->Then([this](TOnlineAsyncOp<FJoinSession>& Op, TPromise<const EOS_Sessions_JoinSessionCallbackInfo*>&& Promise)
	{
		const FJoinSession::Params& OpParams = Op.GetParams();

		// TODO: Call CheckJoinSessionState after it's submitted

		// We make a copy of the Session object
		FSessionEOSGS NewSession = FSessionEOSGS(*OpParams.Session);
		NewSession.CurrentState = ESessionState::Joining;

		TSharedRef<FSessionEOSGS> NewSessionEOSGSRef = MakeShared<FSessionEOSGS>(NewSession);

		SessionsByName.Add(OpParams.SessionName, NewSessionEOSGSRef);
		SessionsById.Add(NewSession.SessionId, NewSessionEOSGSRef);

		// We start setup for the API call
		EOS_Sessions_JoinSessionOptions JoinSessionOptions = { };
		JoinSessionOptions.ApiVersion = EOS_SESSIONS_JOINSESSION_API_LATEST;
		static_assert(EOS_SESSIONS_JOINSESSION_API_LATEST == 2, "EOS_Sessions_JoinSessionOptions updated, check new fields");

		JoinSessionOptions.bPresenceEnabled = OpParams.Session->SessionSettings.bPresenceEnabled;

		JoinSessionOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);

		FTCHARToUTF8 SessionNameUtf8(OpParams.SessionName.ToString());
		JoinSessionOptions.SessionName = SessionNameUtf8.Get();

		const FSessionEOSGS& SessionEOSGS = FSessionEOSGS::Cast(*OpParams.Session);
		JoinSessionOptions.SessionHandle = SessionEOSGS.SessionDetailsHandle->SessionDetailsHandle;

		EOS_Async(EOS_Sessions_JoinSession, SessionsHandle, JoinSessionOptions, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FJoinSession>& Op, const EOS_Sessions_JoinSessionCallbackInfo*&& Result)
	{
		const FJoinSession::Params& OpParams = Op.GetParams();

		if (Result->ResultCode != EOS_EResult::EOS_Success)
		{
			// If join failed, we remove the session objects
			if (TSharedRef<FSessionEOSGS>* Session = SessionsByName.Find(OpParams.SessionName))
			{
				SessionsByName.Remove(OpParams.SessionName);
				SessionsById.Remove(Session->Get().SessionId);
			}

			Op.SetError(FromEOSError(Result->ResultCode));
			return;
		}
		
		if (TSharedRef<FSessionEOSGS>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			FoundSession->Get().CurrentState = ESessionState::Valid;
		}

		// A successful join allows the client to server travel, after which RegisterPlayers will be called by the engine

		// TODO: Support multiple local users joining the session
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/* UE::Online */ }