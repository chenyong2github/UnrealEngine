// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsEOSGS.h"

#include "Online/AuthEOSGS.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesEOSGSTypes.h"
#include "Online/NboSerializerEOSGSSvc.h"
#include "Online/OnlineErrorEOSGS.h"
#include "Online/SessionsEOSGSTypes.h"

#include "eos_sessions.h"

namespace UE::Online {

/** FOnlineSessionIdRegistryEOSGS */

FOnlineSessionIdRegistryEOSGS::FOnlineSessionIdRegistryEOSGS()
	: FOnlineSessionIdRegistryLAN(EOnlineServices::Epic)
{
}

FOnlineSessionIdRegistryEOSGS& FOnlineSessionIdRegistryEOSGS::Get()
{
	static FOnlineSessionIdRegistryEOSGS Instance;
	return Instance;
}

/** FOnlineSessionInviteIdRegistryEOSGS */

FOnlineSessionInviteIdRegistryEOSGS::FOnlineSessionInviteIdRegistryEOSGS()
	: FOnlineSessionInviteIdStringRegistry(EOnlineServices::Epic)
{
}

FOnlineSessionInviteIdRegistryEOSGS& FOnlineSessionInviteIdRegistryEOSGS::Get()
{
	static FOnlineSessionInviteIdRegistryEOSGS Instance;
	return Instance;
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
		SessionSettings.NumOpenPublicConnections = SessionDetailsInfo->NumOpenPublicConnections;
		// We could retrieve the Host Address here if we set it during creation or update

		// TODO: evaluate effect of bInvitesAllowed and bAllowJoinInProgress to know whether we can just read the values here or if we need the custom parameter
		SessionSettings.bAllowSanctionedPlayers = !SessionDetailsInfo->Settings->bSanctionsEnabled;
		SessionSettings.NumMaxPublicConnections = SessionDetailsInfo->Settings->NumPublicConnections;
		SessionSettings.JoinPolicy = FromServiceType(SessionDetailsInfo->Settings->PermissionLevel);
		SessionSettings.CustomSettings.Emplace(EOSGS_BUCKET_ID, FCustomSessionSetting{ FSchemaVariant(FString(UTF8_TO_TCHAR(SessionDetailsInfo->Settings->BucketId))), ESchemaAttributeVisibility::Public });

		// We retrieve all the session attributes
		EOS_SessionDetails_GetSessionAttributeCountOptions GetAttributeCountOptions = {};
		GetAttributeCountOptions.ApiVersion = EOS_SESSIONDETAILS_GETSESSIONATTRIBUTECOUNT_API_LATEST;
		static_assert(EOS_SESSIONDETAILS_GETSESSIONATTRIBUTECOUNT_API_LATEST == 1, "EOS_SessionDetails_GetSessionAttributeCountOptions updated, check new fields");

		uint32_t AttributeCount = EOS_SessionDetails_GetSessionAttributeCount(InSessionDetailsHandle, &GetAttributeCountOptions);
		for (uint32_t Index = 0; Index < AttributeCount; ++Index)
		{
			EOS_SessionDetails_CopySessionAttributeByIndexOptions CopyAttributeByIndexOptions = {};
			CopyAttributeByIndexOptions.ApiVersion = EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYINDEX_API_LATEST;
			static_assert(EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYINDEX_API_LATEST == 1, "EOS_SessionDetails_CopySessionAttributeByIndexOptions updated, check new fields");

			CopyAttributeByIndexOptions.AttrIndex = Index;

			EOS_SessionDetails_Attribute* Attribute = nullptr;
			EOS_EResult CopyAttributeByIndexResult = EOS_SessionDetails_CopySessionAttributeByIndex(InSessionDetailsHandle, &CopyAttributeByIndexOptions, &Attribute);
			if (CopyAttributeByIndexResult == EOS_EResult::EOS_Success)
			{
				// We parse a single attribute
				FString Key(Attribute->Data->Key);

				// If the Key contains the ':' character, it will be a user related entry, either a Session Member setting, or Registered Player information
				if (Key.Contains(TEXT(":")))
				{
					TArray<FString> KeyComponents;
					Key.ParseIntoArray(KeyComponents, TEXT(":"));

					// If the first element is the Registered Players key, we will process the entry as Registered Player information
					if (KeyComponents[0] == EOSGS_REGISTERED_PLAYERS.ToString())
					{
						// We retrieve the registered player id
						const FString& PlayerIdStr = KeyComponents[1];
						const EOS_ProductUserId ProductUserId = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*PlayerIdStr));
						const FOnlineAccountIdHandle& IdHandle = FindAccountId(ProductUserId);

						// This constructor should only be used by BuildSessionFromDetailsHandle, after all user ids in the session have been resolved
						check(IdHandle.IsValid());

						FRegisteredPlayer& RegisteredPlayer = SessionSettings.RegisteredPlayers.FindOrAdd(IdHandle);
						
						FSessionAttributeConverter<ESessionAttributeConversionType::FromService> CustomSettingConverter(*Attribute->Data);
						const TPair<FSchemaAttributeId, FSchemaVariant>& CustomSettingData = CustomSettingConverter.GetAttributeData();
						bool bParameterValue = CustomSettingData.Value.GetBoolean();

						// And set the value for the appropriate parameter
						const FString& ParameterName = KeyComponents[2];

						if (ParameterName == EOSGS_REGISTERED_PLAYER_IS_IN_SESSION.ToString())
						{
							RegisteredPlayer.bIsInSession = bParameterValue;
						}
						else if (ParameterName == EOSGS_REGISTERED_PLAYER_HAS_RESERVED_SLOT.ToString())
						{
							RegisteredPlayer.bHasReservedSlot = bParameterValue;
						}
					}
					else // If not, it will be parsed as a Session Member setting
					{
						// We retrieve the member id
						FString PlayerIdStr = KeyComponents[0];
						EOS_ProductUserId ProductUserId = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*PlayerIdStr));
						FOnlineAccountIdHandle IdHandle = FindAccountId(ProductUserId);

						FSessionMember& SessionMember = SessionSettings.SessionMembers.FindOrAdd(IdHandle);

						// And add the corresponding custom setting
						FSchemaAttributeId AttributeId(KeyComponents[1]);

						FSessionAttributeConverter<ESessionAttributeConversionType::FromService> CustomSettingConverter(*Attribute->Data);
						TPair<FSchemaAttributeId, FSchemaVariant> CustomSettingData = CustomSettingConverter.GetAttributeData();

						FCustomSessionSetting CustomSessionSetting;
						CustomSessionSetting.Visibility = FromServiceType(Attribute->AdvertisementType);
						CustomSessionSetting.Data = CustomSettingData.Value;

						SessionMember.MemberSettings.Emplace(AttributeId, CustomSessionSetting);
					}
				}
				else 
				{
					FSessionAttributeConverter<ESessionAttributeConversionType::FromService> CustomSettingConverter(*Attribute->Data);
					TPair<FSchemaAttributeId, FSchemaVariant> CustomSettingData = CustomSettingConverter.GetAttributeData();
					
					// Most Session Settings values get parsed in the same way as Custom Session Settings, so we will attempt to retrieve them
					if (Key == EOSGS_ALLOW_NEW_MEMBERS.ToString())
					{
						SessionSettings.bAllowNewMembers = CustomSettingData.Value.GetBoolean();
					}
					else if (Key == EOSGS_ALLOW_UNREGISTERED_PLAYERS.ToString())
					{
						SessionSettings.bAllowUnregisteredPlayers = CustomSettingData.Value.GetBoolean();
					}
					else if (Key == EOSGS_ANTI_CHEAT_PROTECTED.ToString())
					{
						SessionSettings.bAntiCheatProtected = CustomSettingData.Value.GetBoolean();
					}
					else if (Key == EOSGS_IS_DEDICATED_SERVER_SESSION.ToString())
					{
						SessionSettings.bIsDedicatedServerSession = CustomSettingData.Value.GetBoolean();
					}
					else if (Key == EOSGS_PRESENCE_ENABLED.ToString())
					{
						SessionSettings.bPresenceEnabled = CustomSettingData.Value.GetBoolean();
					}
					else if (Key == EOSGS_SCHEMA_NAME.ToString())
					{
						SessionSettings.SchemaName = FSchemaId(CustomSettingData.Value.GetString());
					}
					else if (Key == EOSGS_SESSION_ID_OVERRIDE.ToString())
					{
						SessionSettings.SessionIdOverride = CustomSettingData.Value.GetString();
					}
					else // The rest are parsed as a Custom Session Setting
					{
						FCustomSessionSetting CustomSessionSetting;
						CustomSessionSetting.Visibility = FromServiceType(Attribute->AdvertisementType);
						CustomSessionSetting.Data = CustomSettingData.Value;

						SessionSettings.CustomSettings.Emplace(Key, CustomSessionSetting);
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionEOSGS] EOS_SessionDetails_CopySessionAttributeByIndex failed with result [%s]"), *LexToString(CopyInfoResult));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionEOSGS] EOS_SessionDetails_CopyInfo failed with result [%s]"), *LexToString(CopyInfoResult));

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

		// Check if the Bucket Id custom setting is set. EOS Sessions can not be created without it
		if (!OpParams.SessionSettings.CustomSettings.Contains(EOSGS_BUCKET_ID))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::CreateSession] Could not create session without Custom Setting 'EOSGS_BUCKET_ID' (FString) set."));

			Op.SetError(Errors::InvalidParams());

			return MakeFulfilledPromise<TDefaultErrorResult<FUpdateSessionImpl>>().GetFuture();
		}

		// After all initial checks, we start the session creation operations

		EOS_Sessions_CreateSessionModificationOptions CreateSessionModificationOptions = {};
		CreateSessionModificationOptions.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
		static_assert(EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST == 4, "EOS_Sessions_CreateSessionModificationOptions updated, check new fields");

		CreateSessionModificationOptions.bPresenceEnabled = OpParams.SessionSettings.bPresenceEnabled;
		CreateSessionModificationOptions.bSanctionsEnabled = !OpParams.SessionSettings.bAllowSanctionedPlayers;

		const FCustomSessionSetting* BucketIdSetting = OpParams.SessionSettings.CustomSettings.Find(EOSGS_BUCKET_ID);
		const FTCHARToUTF8 BucketIdUtf8(BucketIdSetting ? *BucketIdSetting->Data.GetString() : nullptr);

		if (BucketIdUtf8.Length())
		{
			CreateSessionModificationOptions.BucketId = BucketIdUtf8.Get();
		}
	
		CreateSessionModificationOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);
		CreateSessionModificationOptions.MaxPlayers = OpParams.SessionSettings.NumMaxPrivateConnections + OpParams.SessionSettings.NumMaxPublicConnections;

		const FTCHARToUTF8 SessionIdUtf8(*OpParams.SessionSettings.SessionIdOverride);
		if (SessionIdUtf8.Length())
		{
			CreateSessionModificationOptions.SessionId = SessionIdUtf8.Get();
		}

		const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
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

		// Always update joinability on session creation
		FUpdateSessionImpl::Params UpdateSessionImplParams{ MakeShared<FSessionModificationHandleEOSGS>(SessionModificationHandle), FUpdateSessionJoinabilityParams{ OpParams.SessionName, OpParams.SessionSettings.bAllowNewMembers } };
		
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

void FSessionsEOSGS::SetJoinInProgressAllowed(EOS_HSessionModification& SessionModHandle, bool bIsJoinInProgressAllowed)
{
	EOS_SessionModification_SetJoinInProgressAllowedOptions Options = {};
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST == 1, "EOS_SessionModification_SetJoinInProgressAllowedOptions updated, check new fields");

	Options.bAllowJoinInProgress = bIsJoinInProgressAllowed;

	EOS_EResult ResultCode = EOS_SessionModification_SetJoinInProgressAllowed(SessionModHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_SetJoinInProgressAllowed failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetInvitesAllowed(EOS_HSessionModification& SessionModHandle, bool bAreInvitesAllowed)
{
	EOS_SessionModification_SetInvitesAllowedOptions Options = {};
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETINVITESALLOWED_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETINVITESALLOWED_API_LATEST == 1, "EOS_SessionModification_SetInvitesAllowedOptions updated, check new fields");

	Options.bInvitesAllowed = bAreInvitesAllowed;

	EOS_EResult ResultCode = EOS_SessionModification_SetInvitesAllowed(SessionModHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_SessionModification_SetInvitesAllowed failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetPermissionLevel(EOS_HSessionModification& SessionModificationHandle, const ESessionJoinPolicy& NewJoinPolicy)
{
	EOS_SessionModification_SetPermissionLevelOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST == 1, "EOS_SessionModification_SetPermissionLevelOptions updated, check new fields");

	Options.PermissionLevel = ToServiceType(NewJoinPolicy);

	EOS_EResult ResultCode = EOS_SessionModification_SetPermissionLevel(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetPermissionLevel] EOS_SessionModification_SetPermissionLevel failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::SetBucketId(EOS_HSessionModification& SessionModificationHandle, const FString& NewBucketId)
{
	EOS_SessionModification_SetBucketIdOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_SETBUCKETID_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_SETBUCKETID_API_LATEST == 1, "EOS_SessionModification_SetBucketIdOptions updated, check new fields");

	const FTCHARToUTF8 BucketIdUtf8(*NewBucketId);
	Options.BucketId = BucketIdUtf8.Get();

	EOS_EResult ResultCode = EOS_SessionModification_SetBucketId(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetBucketId] EOS_SessionModification_SetBucketId failed with result [%s]"), *LexToString(ResultCode));
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
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetMaxPlayers] EOS_SessionModification_SetMaxPlayers failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::AddAttribute(EOS_HSessionModification& SessionModificationHandle, const FSchemaAttributeId& Key, const FCustomSessionSetting& Value)
{
	EOS_SessionModification_AddAttributeOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST == 1, "EOS_SessionModification_AddAttributeOptions updated, check new fields");

	Options.AdvertisementType = ToServiceType(Value.Visibility);

	FSessionAttributeConverter<ESessionAttributeConversionType::ToService> SessionAttribute(Key, Value.Data);
	Options.SessionAttribute = &SessionAttribute.GetAttributeData();

	EOS_EResult ResultCode = EOS_SessionModification_AddAttribute(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::AddAttribute] EOS_SessionModification_AddAttribute failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::RemoveAttribute(EOS_HSessionModification& SessionModificationHandle, const FSchemaAttributeId& Key)
{
	EOS_SessionModification_RemoveAttributeOptions Options = { };
	Options.ApiVersion = EOS_SESSIONMODIFICATION_REMOVEATTRIBUTE_API_LATEST;
	static_assert(EOS_SESSIONMODIFICATION_REMOVEATTRIBUTE_API_LATEST == 1, "EOS_SessionModification_RemoveAttributeOptions updated, check new fields");

	const FTCHARToUTF8 KeyUtf8(Key.ToString());
	Options.Key = KeyUtf8.Get();

	EOS_EResult ResultCode = EOS_SessionModification_RemoveAttribute(SessionModificationHandle, &Options);
	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::RemoveAttribute] EOS_SessionModification_RemoveAttribute failed with result [%s]"), *LexToString(ResultCode));
	}
}

void FSessionsEOSGS::WriteCreateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FSessionSettings& SessionSettings)
{
	// TODO: We have the option to call EOS_SessionModification_SetHostAddress in EOS, useful if the session owner changes

	// We'll update this setting in the session modification step, and start or end the session accordingly to bock or allow join processes
	SetJoinInProgressAllowed(SessionModificationHandle, SessionSettings.bAllowNewMembers);

	// We'll also update invite permissions for the session
	SetInvitesAllowed(SessionModificationHandle, SessionSettings.bAllowNewMembers);

	// We won't copy bIsLANSession since it' irrelevant for EOS Sessions
	AddAttribute(SessionModificationHandle, EOSGS_ALLOW_NEW_MEMBERS, { FSchemaVariant(SessionSettings.bAllowNewMembers), ESchemaAttributeVisibility::Public });
	AddAttribute(SessionModificationHandle, EOSGS_ALLOW_UNREGISTERED_PLAYERS, { FSchemaVariant(SessionSettings.bAllowUnregisteredPlayers), ESchemaAttributeVisibility::Public });
	AddAttribute(SessionModificationHandle, EOSGS_ANTI_CHEAT_PROTECTED, { FSchemaVariant(SessionSettings.bAntiCheatProtected), ESchemaAttributeVisibility::Public });
	AddAttribute(SessionModificationHandle, EOSGS_IS_DEDICATED_SERVER_SESSION, { FSchemaVariant(SessionSettings.bIsDedicatedServerSession), ESchemaAttributeVisibility::Public });
	AddAttribute(SessionModificationHandle, EOSGS_PRESENCE_ENABLED, { FSchemaVariant(SessionSettings.bPresenceEnabled), ESchemaAttributeVisibility::Public });

	SetPermissionLevel(SessionModificationHandle, SessionSettings.JoinPolicy);

	SetMaxPlayers(SessionModificationHandle, SessionSettings.NumMaxPrivateConnections + SessionSettings.NumMaxPublicConnections);

	AddAttribute(SessionModificationHandle, EOSGS_SCHEMA_NAME, { FSchemaVariant(SessionSettings.SchemaName.ToString()), ESchemaAttributeVisibility::Public });
	AddAttribute(SessionModificationHandle, EOSGS_SESSION_ID_OVERRIDE, { FSchemaVariant(SessionSettings.SessionIdOverride), ESchemaAttributeVisibility::Public });

	// Custom Settings

 	for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : SessionSettings.CustomSettings)
 	{
		AddAttribute(SessionModificationHandle, Entry.Key, Entry.Value);
	}

	// BucketId has its own set method on the API
	const FCustomSessionSetting* NewBucketIdSetting = SessionSettings.CustomSettings.Find(EOSGS_BUCKET_ID);
	if (NewBucketIdSetting)
	{
		SetBucketId(SessionModificationHandle, NewBucketIdSetting->Data.GetString());
	}

	// Session Members

	for (const TPair<FOnlineAccountIdHandle, FSessionMember>& SessionMemberEntry : SessionSettings.SessionMembers)
	{
		const FSessionMember& SessionMember = SessionMemberEntry.Value;
		
		for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& CustomSettingEntry : SessionMember.MemberSettings)
		{
			const FName& Key = FName(LexToString(GetProductUserIdChecked(SessionMemberEntry.Key)) + TEXT(":") + CustomSettingEntry.Key.ToString());
			AddAttribute(SessionModificationHandle, Key, CustomSettingEntry.Value);
		}		
	}

	// TODO: Should always be empty at session creation time so might not be needed
	// Registered Players
	for (const TPair<FOnlineAccountIdHandle, FRegisteredPlayer>& Entry : SessionSettings.RegisteredPlayers)
	{
		const FName& HasReservedSlotKey = FName(EOSGS_REGISTERED_PLAYERS.ToString() + TEXT(":") + LexToString(GetProductUserIdChecked(Entry.Key)) + TEXT(":") + EOSGS_REGISTERED_PLAYER_HAS_RESERVED_SLOT.ToString());
		AddAttribute(SessionModificationHandle, HasReservedSlotKey, { FSchemaVariant(Entry.Value.bHasReservedSlot), ESchemaAttributeVisibility::Public });

		const FName& IsInSessionKey = FName(EOSGS_REGISTERED_PLAYERS.ToString() + TEXT(":") + LexToString(GetProductUserIdChecked(Entry.Key)) + TEXT(":") + EOSGS_REGISTERED_PLAYER_IS_IN_SESSION.ToString());
		AddAttribute(SessionModificationHandle, IsInSessionKey, { FSchemaVariant(Entry.Value.bIsInSession), ESchemaAttributeVisibility::Public });
	}
}

void FSessionsEOSGS::WriteUpdateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FName& SessionName, const FSessionSettingsUpdate& NewSettings)
{
	// TODO: We have the option to call EOS_SessionModification_SetHostAddress in EOS, useful if the session owner changes
	

	if (NewSettings.bAllowNewMembers.IsSet())
	{
		// We'll update this setting in the session modification step, and start or end the session accordingly to bock or allow join processes
		SetJoinInProgressAllowed(SessionModificationHandle, NewSettings.bAllowNewMembers.GetValue());

		// We'll also update invite permissions for the session
		SetInvitesAllowed(SessionModificationHandle, NewSettings.bAllowNewMembers.GetValue());
	}

	if (NewSettings.bAllowUnregisteredPlayers.IsSet())
	{
		AddAttribute(SessionModificationHandle, EOSGS_ALLOW_UNREGISTERED_PLAYERS, { FSchemaVariant(NewSettings.bAllowUnregisteredPlayers.GetValue()), ESchemaAttributeVisibility::Public });
	}

	if (NewSettings.bAntiCheatProtected.IsSet())
	{
		AddAttribute(SessionModificationHandle, EOSGS_ANTI_CHEAT_PROTECTED, { FSchemaVariant(NewSettings.bAntiCheatProtected.GetValue()), ESchemaAttributeVisibility::Public });
	}

	if (NewSettings.bIsDedicatedServerSession.IsSet())
	{
		AddAttribute(SessionModificationHandle, EOSGS_IS_DEDICATED_SERVER_SESSION, { FSchemaVariant(NewSettings.bIsDedicatedServerSession.GetValue()), ESchemaAttributeVisibility::Public });
	}

	if (NewSettings.bPresenceEnabled.IsSet())
	{
		AddAttribute(SessionModificationHandle, EOSGS_PRESENCE_ENABLED, { FSchemaVariant(NewSettings.bPresenceEnabled.GetValue()), ESchemaAttributeVisibility::Public });
	}

	if (NewSettings.SchemaName.IsSet())
	{
		AddAttribute(SessionModificationHandle, EOSGS_SCHEMA_NAME, { FSchemaVariant(NewSettings.SchemaName.GetValue().ToString()), ESchemaAttributeVisibility::Public });
	}

	if (NewSettings.SessionIdOverride.IsSet())
	{
		AddAttribute(SessionModificationHandle, EOSGS_SESSION_ID_OVERRIDE, { FSchemaVariant(NewSettings.SessionIdOverride.GetValue()), ESchemaAttributeVisibility::Public });
	}

	const TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(SessionName);
	const FSessionSettings& SessionSettings = FoundSession->SessionSettings;

	if (NewSettings.JoinPolicy.IsSet())
	{
		SetPermissionLevel(SessionModificationHandle, NewSettings.JoinPolicy.GetValue());
	}

	if (NewSettings.NumMaxPrivateConnections.IsSet() || NewSettings.NumMaxPublicConnections.IsSet())
	{
		// We set MaxPlayers to the sum of any new values (if set) and old values (if not set)
		uint32 DefaultNumMaxPrivateConnections = SessionSettings.NumMaxPrivateConnections;
		uint32 DefaultNumMaxPublicConnections = SessionSettings.NumMaxPublicConnections;

		SetMaxPlayers(SessionModificationHandle, NewSettings.NumMaxPrivateConnections.Get(DefaultNumMaxPrivateConnections) + NewSettings.NumMaxPublicConnections.Get(DefaultNumMaxPublicConnections));
	}

	// BucketId has its own set method on the API
	const FCustomSessionSetting* NewBucketIdSetting = NewSettings.UpdatedCustomSettings.Find(EOSGS_BUCKET_ID);
	if (NewBucketIdSetting)
	{
		SetBucketId(SessionModificationHandle, NewBucketIdSetting->Data.GetString());
	}

	// Custom Settings

	for (const FSchemaAttributeId& Key : NewSettings.RemovedCustomSettings)
	{
		RemoveAttribute(SessionModificationHandle, Key);
	}

	for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : NewSettings.UpdatedCustomSettings)
	{
		AddAttribute(SessionModificationHandle, Entry.Key, Entry.Value);
	}

	// Session Members
	
	for (const FOnlineAccountIdHandle& SessionMemberId : NewSettings.RemovedSessionMembers)
	{
		// To remove a session member, we'll remove all their attributes
 		if (const FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(SessionMemberId))
 		{
 			TArray<FName> SettingsKeyArray;
 			SessionMember->MemberSettings.GenerateKeyArray(SettingsKeyArray);
 			for (const FSchemaAttributeId& CustomSettingEntryKey : SettingsKeyArray)
 			{
 				const FSchemaAttributeId& Key = FName(LexToString(GetProductUserIdChecked(SessionMemberId)) + TEXT(":") + CustomSettingEntryKey.ToString());
				RemoveAttribute(SessionModificationHandle, Key);
 			}
 		}
	}

	for (const TPair<FOnlineAccountIdHandle, FSessionMemberUpdate>& SessionMemberEntry : NewSettings.UpdatedSessionMembers)
	{
		const FSessionMemberUpdate& SessionMemberUpdate = SessionMemberEntry.Value;

		for (const FSchemaAttributeId& CustomSettingEntryKey : SessionMemberUpdate.RemovedMemberSettings)
		{
			const FSchemaAttributeId& Key = FSchemaAttributeId(LexToString(GetProductUserIdChecked(SessionMemberEntry.Key)) + TEXT(":") + CustomSettingEntryKey.ToString());
			RemoveAttribute(SessionModificationHandle, Key);
		}

		for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& CustomSettingEntry : SessionMemberUpdate.UpdatedMemberSettings)
		{
			const FSchemaAttributeId& Key = FSchemaAttributeId(LexToString(GetProductUserIdChecked(SessionMemberEntry.Key)) + TEXT(":") + CustomSettingEntry.Key.ToString());
			AddAttribute(SessionModificationHandle, Key, CustomSettingEntry.Value);
		}
	}

	// Registered Players
	
	for (const FOnlineAccountIdHandle& RemovedEntry : NewSettings.RemovedRegisteredPlayers)
	{
		const FSchemaAttributeId& HasReservedSlotKey = FSchemaAttributeId(EOSGS_REGISTERED_PLAYERS.ToString() + TEXT(":") + LexToString(GetProductUserIdChecked(RemovedEntry)) + TEXT(":") + EOSGS_REGISTERED_PLAYER_HAS_RESERVED_SLOT.ToString());
		RemoveAttribute(SessionModificationHandle, HasReservedSlotKey);

		const FSchemaAttributeId& IsInSessionKey = FSchemaAttributeId(EOSGS_REGISTERED_PLAYERS.ToString() + TEXT(":") + LexToString(GetProductUserIdChecked(RemovedEntry)) + TEXT(":") + EOSGS_REGISTERED_PLAYER_IS_IN_SESSION.ToString());
		RemoveAttribute(SessionModificationHandle, IsInSessionKey);
	}

	for (const TPair<FOnlineAccountIdHandle, FRegisteredPlayer>& Entry : NewSettings.UpdatedRegisteredPlayers)
	{
		const FSchemaAttributeId& HasReservedSlotKey = FSchemaAttributeId(EOSGS_REGISTERED_PLAYERS.ToString() + TEXT(":") + LexToString(GetProductUserIdChecked(Entry.Key)) + TEXT(":") + EOSGS_REGISTERED_PLAYER_HAS_RESERVED_SLOT.ToString());
		AddAttribute(SessionModificationHandle, HasReservedSlotKey, { FSchemaVariant(Entry.Value.bHasReservedSlot), ESchemaAttributeVisibility::Public });

		const FSchemaAttributeId& IsInSessionKey = FSchemaAttributeId(EOSGS_REGISTERED_PLAYERS.ToString() + TEXT(":") + LexToString(GetProductUserIdChecked(Entry.Key)) + TEXT(":") + EOSGS_REGISTERED_PLAYER_IS_IN_SESSION.ToString());
		AddAttribute(SessionModificationHandle, IsInSessionKey, { FSchemaVariant(Entry.Value.bIsInSession), ESchemaAttributeVisibility::Public });
	}
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

		const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
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

		// Whether we update joinability or not will depend on inf bAllowNewMembers was set to a new value
		FUpdateSessionImpl::Params UpdateSessionImplParams = 
		{
		MakeShared<FSessionModificationHandleEOSGS>(SessionModificationHandle),
		OpParams.Mutations.bAllowNewMembers.IsSet() ?
			FUpdateSessionJoinabilityParams{ OpParams.SessionName, OpParams.Mutations.bAllowNewMembers.GetValue() } :
			TOptional<FUpdateSessionJoinabilityParams>() // If bAllowNewMembers is not set, the TOptional will be unset too
		};

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
	[this, Promise = MoveTemp(Promise), Params = MoveTemp(Params)](const EOS_Sessions_UpdateSessionCallbackInfo* Result) mutable
	{
		// If we only change bAllowNewMembers, the session update will yield an EOS_NoChange result, but we still need to continue to the next step
		if (Result->ResultCode != EOS_EResult::EOS_Success && Result->ResultCode != EOS_EResult::EOS_Sessions_OutOfSync && Result->ResultCode != EOS_EResult::EOS_NoChange)
		{
			UE_LOG(LogTemp, Warning, TEXT("EOS_Sessions_UpdateSession failed with result [%s]"), *LexToString(Result->ResultCode));
			Promise.EmplaceValue(Errors::FromEOSResult(Result->ResultCode));
			return;
		}

		// In session creation calls, we'll need to set the new id for the session
		TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(FName(UTF8_TO_TCHAR(Result->SessionName)));
		if (!FoundSession->SessionId.IsValid())
		{
			FoundSession->SessionId = CreateSessionId(UTF8_TO_TCHAR(Result->SessionId));
		}

		// After the successful general update, if indicated, we'll update the joinability
		if (Params.UpdateJoinabilitySettings.IsSet())
		{
			UpdateSessionJoinabilityImpl(MoveTemp(Params.UpdateJoinabilitySettings.GetValue()))
				.Next([this, Promise = MoveTemp(Promise)](TDefaultErrorResult<FUpdateSessionJoinabilityImpl>&& Result) mutable
			{
				if (Result.IsOk())
				{
					Promise.EmplaceValue(FUpdateSessionImpl::Result{ });
				}
				else
				{
					Promise.EmplaceValue(Result.GetErrorValue());
				}
			});
		}
		else
		{
			Promise.EmplaceValue(FUpdateSessionImpl::Result{ });
		}
	});

	return Future;
}

TFuture<TDefaultErrorResult<FUpdateSessionJoinabilityImpl>> FSessionsEOSGS::UpdateSessionJoinabilityImpl(FUpdateSessionJoinabilityImpl::Params&& Params)
{
	TPromise<TDefaultErrorResult<FUpdateSessionJoinabilityImpl>> Promise;
	TFuture<TDefaultErrorResult<FUpdateSessionJoinabilityImpl>> Future = Promise.GetFuture();

	// We get the active session handle with the session name
	EOS_Sessions_CopyActiveSessionHandleOptions CopyActiveSessionHandleOptions = {};
	CopyActiveSessionHandleOptions.ApiVersion = EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST;
	static_assert(EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST == 1, "EOS_Sessions_CopyActiveSessionHandleOptions updated, check new fields");

	const FTCHARToUTF8 SessionNameUtf8(*Params.SessionName.ToString());
	CopyActiveSessionHandleOptions.SessionName = SessionNameUtf8.Get();

	EOS_HActiveSession ActiveSessionHandle;
	EOS_EResult CopyActiveSessionHandleResult = EOS_Sessions_CopyActiveSessionHandle(SessionsHandle, &CopyActiveSessionHandleOptions, &ActiveSessionHandle);
	if (CopyActiveSessionHandleResult != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_Sessions_CopyActiveSessionHandle failed with result [%s]"), *LexToString(CopyActiveSessionHandleResult));
	}

	// We get the active session info with the handle
	EOS_ActiveSession_CopyInfoOptions CopyInfoOptions = {};
	CopyInfoOptions.ApiVersion = EOS_ACTIVESESSION_COPYINFO_API_LATEST;
	static_assert(EOS_ACTIVESESSION_COPYINFO_API_LATEST == 1, "EOS_ActiveSession_CopyInfoOptions updated, check new fields");

	EOS_ActiveSession_Info* ActiveSessionInfo = nullptr;
	EOS_EResult CopyInfoResult = EOS_ActiveSession_CopyInfo(ActiveSessionHandle, &CopyInfoOptions, &ActiveSessionInfo);
	if (CopyInfoResult != EOS_EResult::EOS_Success)
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS_ActiveSession_CopyInfo failed with result [%s]"), *LexToString(CopyInfoResult));
	}

	// If not, we start or end the session to make it joinable or not (as we set JIP to false at creation time)
	if (Params.bAllowNewMembers)
	{
		// We check state. If bAllowNewMembers is true and session has not started, there's no need to do anything
		if (ActiveSessionInfo->State == EOS_EOnlineSessionState::EOS_OSS_InProgress)
		{
			EOS_Sessions_EndSessionOptions Options = {};
			Options.ApiVersion = EOS_SESSIONS_ENDSESSION_API_LATEST;
			static_assert(EOS_SESSIONS_ENDSESSION_API_LATEST == 1, "EOS_Sessions_EndSessionOptions updated, check new fields");

			Options.SessionName = SessionNameUtf8.Get();

			EOS_Async(EOS_Sessions_EndSession, SessionsHandle, Options,
				[this, Promise = MoveTemp(Promise)](const EOS_Sessions_EndSessionCallbackInfo* Result) mutable
			{
				if (Result->ResultCode != EOS_EResult::EOS_Success && Result->ResultCode != EOS_EResult::EOS_Sessions_OutOfSync)
				{
					UE_LOG(LogTemp, Warning, TEXT("EOS_Sessions_EndSession failed with result [%s]"), *LexToString(Result->ResultCode));
					Promise.EmplaceValue(Errors::FromEOSResult(Result->ResultCode));
					return;
				}

				Promise.EmplaceValue(FUpdateSessionJoinabilityImpl::Result{ });
			});
		}
		else
		{
			Promise.EmplaceValue(FUpdateSessionJoinabilityImpl::Result{ });
		}
	}
	else
	{
		// We check state. If bAllowNewMembers is false and the session has started, there's no need to do anything
		if (ActiveSessionInfo->State == EOS_EOnlineSessionState::EOS_OSS_Pending || ActiveSessionInfo->State == EOS_EOnlineSessionState::EOS_OSS_Ended)
		{
			EOS_Sessions_StartSessionOptions Options = {};
			Options.ApiVersion = EOS_SESSIONS_STARTSESSION_API_LATEST;
			static_assert(EOS_SESSIONS_STARTSESSION_API_LATEST == 1, "EOS_Sessions_StartSessionOptions updated, check new fields");

			Options.SessionName = SessionNameUtf8.Get();

			EOS_Async(EOS_Sessions_StartSession, SessionsHandle, Options,
				[this, Promise = MoveTemp(Promise)](const EOS_Sessions_StartSessionCallbackInfo* Result) mutable
			{
				if (Result->ResultCode != EOS_EResult::EOS_Success && Result->ResultCode != EOS_EResult::EOS_Sessions_OutOfSync)
				{
					UE_LOG(LogTemp, Warning, TEXT("EOS_Sessions_StartSession failed with result [%s]"), *LexToString(Result->ResultCode));
					Promise.EmplaceValue(Errors::FromEOSResult(Result->ResultCode));
					return;
				}

				Promise.EmplaceValue(FUpdateSessionJoinabilityImpl::Result{ });
			});
		}
		else
		{
			Promise.EmplaceValue(FUpdateSessionJoinabilityImpl::Result{ });
		}
	}

	return Future;
}

TOnlineAsyncOpHandle<FSendSingleSessionInviteImpl> FSessionsEOSGS::SendSingleSessionInviteImpl(FSendSingleSessionInviteImpl::Params&& Params)
{
	TOnlineAsyncOpRef<FSendSingleSessionInviteImpl> Op = GetOp<FSendSingleSessionInviteImpl>(MoveTemp(Params));
	const FSendSingleSessionInviteImpl::Params& OpParams = Op->GetParams();

	EOS_Sessions_SendInviteOptions SendInviteOptions = { };
	SendInviteOptions.ApiVersion = EOS_SESSIONS_SENDINVITE_API_LATEST;
	static_assert(EOS_SESSIONS_SENDINVITE_API_LATEST == 1, "EOS_Sessions_SendInviteOptions updated, check new fields");

	SendInviteOptions.LocalUserId = GetProductUserIdChecked(OpParams.LocalUserId);

	const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
	SendInviteOptions.SessionName = SessionNameUtf8.Get();

	SendInviteOptions.TargetUserId = GetProductUserIdChecked(OpParams.TargetUserId);

	EOS_Async(EOS_Sessions_SendInvite, SessionsHandle, SendInviteOptions,
		[this, WeakOp = Op->AsWeak()](const EOS_Sessions_SendInviteCallbackInfo* Result) mutable
	{
		if (TOnlineAsyncOpPtr<FSendSingleSessionInviteImpl> StrongOp = WeakOp.Pin())
		{
			if (Result->ResultCode != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogTemp, Warning, TEXT("EOS_Sessions_SendInvite failed with result [%s]"), *LexToString(Result->ResultCode));
				StrongOp->SetError(Errors::FromEOSResult(Result->ResultCode));
				return;
			}

			StrongOp->SetResult(FSendSingleSessionInviteImpl::Result{ });
		}
	});

	return Op->GetHandle();
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

		const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
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
	for (const FFindSessionsSearchFilter& Filter : Filters)
	{
		EOS_SessionSearch_SetParameterOptions Options = { };
		Options.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST == 1, "EOS_SessionSearch_SetMaxResultsOptions updated, check new fields");

		Options.ComparisonOp = ToServiceType(Filter.ComparisonOp);
		
		FSessionAttributeConverter<ESessionAttributeConversionType::ToService> Parameter(Filter.Key, Filter.Value);
		Options.Parameter = &Parameter.GetAttributeData();

		EOS_EResult ResultCode = EOS_SessionSearch_SetParameter(SessionSearchHandle.SearchHandle, &Options);
		if (ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::SetSessionSearchParameters] EOS_SessionSearch_SetParameter failed with result [%s]"), *LexToString(ResultCode));
		}
	}
}

void FSessionsEOSGS::SetSessionSearchSessionId(FSessionSearchHandleEOSGS& SessionSearchHandle, const FOnlineSessionIdHandle& SessionId)
{
	EOS_SessionSearch_SetSessionIdOptions Options = { };
	Options.ApiVersion = EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST;
	static_assert(EOS_SESSIONSEARCH_SETSESSIONID_API_LATEST == 1, "EOS_SessionSearch_SetSessionIdOptions updated, check new fields");

	const FTCHARToUTF8 SessionIdUtf8(*FOnlineSessionIdRegistryEOSGS::Get().ToLogString(SessionId));
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

		const FFindSessions::Params& OpParams = Op.GetParams();

		// For a successful session, we'll get the search results
		EOS_SessionSearch_GetSearchResultCountOptions GetSearchResultCountOptions = { };
		GetSearchResultCountOptions.ApiVersion = EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST == 1, "EOS_SessionSearch_GetSearchResultCountOptions updated, check new fields");
		int32 NumSearchResults = EOS_SessionSearch_GetSearchResultCount(CurrentSessionSearchHandleEOSGS->SearchHandle, &GetSearchResultCountOptions);

		EOS_SessionSearch_CopySearchResultByIndexOptions CopySearchResultByIndexOptions = { };
		CopySearchResultByIndexOptions.ApiVersion = EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
		static_assert(EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST == 1, "EOS_SessionSearch_CopySearchResultByIndexOptions updated, check new fields");


		TArray<TFuture<TDefaultErrorResult<FBuildSessionFromDetailsHandle>>> PendingSessionsBuilt;

		for (int32 Index = 0; Index < NumSearchResults; Index++)
		{
			EOS_HSessionDetails SessionDetailsHandle = nullptr;

			CopySearchResultByIndexOptions.SessionIndex = Index;
			EOS_EResult CopySearchResultByIndexResult = EOS_SessionSearch_CopySearchResultByIndex(CurrentSessionSearchHandleEOSGS->SearchHandle, &CopySearchResultByIndexOptions, &SessionDetailsHandle);
			if (CopySearchResultByIndexResult == EOS_EResult::EOS_Success)
			{
				TSharedRef<TPromise<TDefaultErrorResult<FBuildSessionFromDetailsHandle>>> BuildSessionPromise = MakeShared<TPromise<TDefaultErrorResult<FBuildSessionFromDetailsHandle>>>();
				BuildSessionFromDetailsHandle({ OpParams.LocalUserId, MakeShared<FSessionDetailsHandleEOSGS>(SessionDetailsHandle) })
				.OnComplete([this, BuildSessionPromise](const TOnlineResult<FBuildSessionFromDetailsHandle>& Result) mutable
				{
					if (Result.IsOk())
					{
						CurrentSessionSearch->FoundSessions.Add(Result.GetOkValue().Session);

						TMap<FOnlineSessionIdHandle, TSharedRef<FSession>>& UserMap = SessionSearchResultsUserMap.FindOrAdd(Result.GetOkValue().LocalUserId);
						UserMap.Emplace(Result.GetOkValue().Session->SessionId, Result.GetOkValue().Session);

						BuildSessionPromise->EmplaceValue(Result.GetOkValue());
					}
					else
					{
						BuildSessionPromise->EmplaceValue(Result.GetErrorValue());
					}
				});
			
				PendingSessionsBuilt.Add(BuildSessionPromise->GetFuture());
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[FSessionsEOSGS::FindSessions] EOS_SessionSearch_CopySearchResultByIndex failed for index [%d] with result [%s]"), Index, *LexToString(CopySearchResultByIndexResult));

				Op.SetError(Errors::FromEOSResult(CopySearchResultByIndexResult));
			}
		}

		WhenAll(MoveTemp(PendingSessionsBuilt))
			.Next([this, WeakOp = Op.AsWeak()](TArray<TDefaultErrorResult<FBuildSessionFromDetailsHandle>>&& Results) mutable
		{
			if (TOnlineAsyncOpPtr<FFindSessions> StrongOp = WeakOp.Pin())
			{
				for (TDefaultErrorResult<FBuildSessionFromDetailsHandle>& Result : Results)
				{
					if (Result.IsError())
					{
						// Store first encountered error to return as result.
						StrongOp->SetError(MoveTemp(Result.GetErrorValue()));
						return;
					}
				}

				StrongOp->SetResult(MoveTemp(*CurrentSessionSearch));

				CurrentSessionSearch.Reset();
				CurrentSessionSearchHandleEOSGS.Reset();
			}
		});
	});

	// TODO: Call BuildSessionFromDetailsHandle as many times as we have details handles

	Op->Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FJoinSession> FSessionsEOSGS::JoinSession(FJoinSession::Params&& Params)
{
	TOnlineAsyncOpRef<FJoinSession> Op = GetOp<FJoinSession>(MoveTemp(Params));
	const FJoinSession::Params& OpParams = Op->GetParams();

	TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalUserId, OpParams.SessionId });
	if (GetSessionByIdResult.IsError())
	{
		// If no result is found, the id might be expired, which we should notify
		if (FOnlineSessionIdRegistryEOSGS::Get().IsSessionIdExpired(OpParams.SessionId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::JoinSession] SessionId parameter [%s] is expired. Please call FindSessions to get an updated list of available sessions "), *ToLogString(OpParams.SessionId));
		}

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
			// If no result is found, the id might be expired, which we should notify
			if (FOnlineSessionIdRegistryEOSGS::Get().IsSessionIdExpired(OpParams.SessionId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::JoinSession] SessionId parameter [%s] is expired. Please call FindSessions to get an updated list of available sessions "), *ToLogString(OpParams.SessionId));
			}

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

		const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
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

			Op.SetResult(FJoinSession::Result{ *FoundSession });

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

	const FTCHARToUTF8 InviteIdUtf8(*InInviteId);
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

TResult<TArray<EOS_ProductUserId>, FOnlineError> GetProductUserIdsFromEOSGSSession(const EOS_HSessionDetails& SessionDetailsHandle)
{
	TArray<EOS_ProductUserId> Result;

	EOS_SessionDetails_CopyInfoOptions CopyInfoOptions = { };
	CopyInfoOptions.ApiVersion = EOS_SESSIONDETAILS_COPYINFO_API_LATEST;
	static_assert(EOS_SESSIONDETAILS_COPYINFO_API_LATEST == 1, "EOS_SessionDetails_CopyInfoOptions updated, check new fields");

	EOS_SessionDetails_Info* SessionDetailsInfo = nullptr;
	EOS_EResult CopyInfoResult = EOS_SessionDetails_CopyInfo(SessionDetailsHandle, &CopyInfoOptions, &SessionDetailsInfo);
	if (CopyInfoResult == EOS_EResult::EOS_Success)
	{
		// We retrieve all the session attributes
		EOS_SessionDetails_GetSessionAttributeCountOptions GetAttributeCountOptions = {};
		GetAttributeCountOptions.ApiVersion = EOS_SESSIONDETAILS_GETSESSIONATTRIBUTECOUNT_API_LATEST;
		static_assert(EOS_SESSIONDETAILS_GETSESSIONATTRIBUTECOUNT_API_LATEST == 1, "EOS_SessionDetails_GetSessionAttributeCountOptions updated, check new fields");
		
		uint32_t AttributeCount = EOS_SessionDetails_GetSessionAttributeCount(SessionDetailsHandle, &GetAttributeCountOptions);
		for (uint32_t Index = 0; Index < AttributeCount; ++Index)
		{
			EOS_SessionDetails_CopySessionAttributeByIndexOptions CopyAttributeByIndexOptions = {};
			CopyAttributeByIndexOptions.ApiVersion = EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYINDEX_API_LATEST;
			static_assert(EOS_SESSIONDETAILS_COPYSESSIONATTRIBUTEBYINDEX_API_LATEST == 1, "EOS_SessionDetails_CopySessionAttributeByIndexOptions updated, check new fields");

			CopyAttributeByIndexOptions.AttrIndex = Index;

			EOS_SessionDetails_Attribute* Attribute = nullptr;
			EOS_EResult CopyAttributeByIndexResult = EOS_SessionDetails_CopySessionAttributeByIndex(SessionDetailsHandle, &CopyAttributeByIndexOptions, &Attribute);
			if (CopyAttributeByIndexResult == EOS_EResult::EOS_Success)
			{
				// We parse a single attribute
				FString Key(Attribute->Data->Key);

				// If the Key contains the ':' character, it will contain a user id
				if (Key.Contains(TEXT(":")))
				{
					TArray<FString> KeyComponents;
					Key.ParseIntoArray(KeyComponents, TEXT(":"));
					const FString& PlayerIdStr = KeyComponents[0] == EOSGS_REGISTERED_PLAYERS.ToString() ? KeyComponents[1] : KeyComponents[0];
					const EOS_ProductUserId ProductUserId = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(*PlayerIdStr));
					Result.AddUnique(ProductUserId);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::BuildSessionFromDetailsHandle] EOS_SessionDetails_CopySessionAttributeByIndex failed with result [%s]"), *LexToString(CopyAttributeByIndexResult));

				TResult<TArray<EOS_ProductUserId>, FOnlineError>(Errors::FromEOSResult(CopyAttributeByIndexResult));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsEOSGS::BuildSessionFromDetailsHandle] EOS_SessionDetails_CopyInfo failed with result [%s]"), *LexToString(CopyInfoResult));

		TResult<TArray<EOS_ProductUserId>, FOnlineError>(Errors::FromEOSResult(CopyInfoResult));
	}

	return TResult<TArray<EOS_ProductUserId>, FOnlineError>(Result);
}

TOnlineAsyncOpHandle<FBuildSessionFromDetailsHandle> FSessionsEOSGS::BuildSessionFromDetailsHandle(FBuildSessionFromDetailsHandle::Params&& Params)
{
	TOnlineAsyncOpRef<FBuildSessionFromDetailsHandle> Op = GetOp<FBuildSessionFromDetailsHandle>(MoveTemp(Params));
	const FBuildSessionFromDetailsHandle::Params& OpParams = Op->GetParams();

	// The first step in the process will be retrieving all ids in the session that need resolution
	TResult<TArray<EOS_ProductUserId>, FOnlineError> GetIdsResult = GetProductUserIdsFromEOSGSSession(Params.SessionDetailsHandleEOSGS->SessionDetailsHandle);

	if (GetIdsResult.IsOk())
	{
		Services.Get<FAuthEOSGS>()->ResolveAccountIds(Params.LocalUserId, GetIdsResult.GetOkValue())
			.Next([this, WeakOp = Op->AsWeak(), Params = MoveTemp(Params)](TArray<FOnlineAccountIdHandle> ResolvedAccountIds) mutable
		{
			if (TOnlineAsyncOpPtr<FBuildSessionFromDetailsHandle> StrongOp = WeakOp.Pin())
			{
				// After all the ids are resolved, we can build the session safely
				StrongOp->SetResult(FBuildSessionFromDetailsHandle::Result{ StrongOp->GetParams().LocalUserId, MakeShared<FSessionEOSGS>(Params.SessionDetailsHandleEOSGS->SessionDetailsHandle) });
			}
		});
	}
	else
	{
		Op->SetError(MoveTemp(GetIdsResult.GetErrorValue()));
	}

	return Op->GetHandle();
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

		TArray<TFuture<TDefaultErrorResult<FSendSingleSessionInviteImpl>>> PendingSessionInvites;
		for (const FOnlineAccountIdHandle& TargetUser : OpParams.TargetUsers)
		{
			FSendSingleSessionInviteImpl::Params FSendSingleSessionInviteParams;
			FSendSingleSessionInviteParams.LocalUserId = OpParams.LocalUserId;
			FSendSingleSessionInviteParams.SessionName = OpParams.SessionName;
			FSendSingleSessionInviteParams.TargetUserId = TargetUser;

			TSharedRef<TPromise<TDefaultErrorResult<FSendSingleSessionInviteImpl>>> SessionInvitePromise = MakeShared<TPromise<TDefaultErrorResult<FSendSingleSessionInviteImpl>>>();
			SendSingleSessionInviteImpl(MoveTemp(FSendSingleSessionInviteParams))
				.OnComplete([SessionInvitePromise](const TOnlineResult<FSendSingleSessionInviteImpl>& Result)
					{
						if (Result.IsOk())
						{
							SessionInvitePromise->EmplaceValue(Result.GetOkValue());
						}
						else
						{
							SessionInvitePromise->EmplaceValue(Result.GetErrorValue());
						}
					});

			PendingSessionInvites.Emplace(SessionInvitePromise->GetFuture());
		}

		WhenAll(MoveTemp(PendingSessionInvites))
			.Next([WeakOp = Op.AsWeak()](TArray<TDefaultErrorResult<FSendSingleSessionInviteImpl>>&& Results) mutable
		{
			if (TOnlineAsyncOpPtr<FSendSessionInvite> StrongOp = WeakOp.Pin())
			{
				for (TDefaultErrorResult<FSendSingleSessionInviteImpl>& Result : Results)
				{
					if (Result.IsError())
					{
						StrongOp->SetError(MoveTemp(Result.GetErrorValue()));
						return;
					}
				}

				StrongOp->SetResult(FSendSessionInvite::Result{ });
			}
		});
	});

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
		const FTCHARToUTF8 InviteIdUtf8(*InviteIdStr);
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

		const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
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

		const FTCHARToUTF8 SessionNameUtf8(*OpParams.SessionName.ToString());
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
	using namespace NboSerializerLANSvc;
	using namespace NboSerializerEOSGSSvc;

	SerializeToBuffer(Packet, Session);
	SerializeToBuffer(Packet, Session.OwnerUserId);
	SerializeToBuffer(Packet, Session.SessionId);
	SerializeToBuffer(Packet, Session.SessionSettings.RegisteredPlayers);
	SerializeToBuffer(Packet, Session.SessionSettings.SessionMembers);
}

void FSessionsEOSGS::ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionLAN& Session)
{
	using namespace NboSerializerLANSvc;
	using namespace NboSerializerEOSGSSvc;

	SerializeFromBuffer(Packet, Session);
	SerializeFromBuffer(Packet, Session.OwnerUserId);
	SerializeFromBuffer(Packet, Session.SessionId);
	SerializeFromBuffer(Packet, Session.SessionSettings.RegisteredPlayers);
	SerializeFromBuffer(Packet, Session.SessionSettings.SessionMembers);
}

/* UE::Online */ }