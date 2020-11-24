// Copyright Epic Games, Inc. All Rights Reserved.

#include "UserManagerEOS.h"
#include "OnlineSubsystemEOS.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/OutputDeviceRedirector.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "OnlineError.h"

#if WITH_EOS_SDK

#include "eos_auth.h"
#include "eos_userinfo.h"
#include "eos_friends.h"
#include "eos_presence.h"

static inline EInviteStatus::Type ToEInviteStatus(EOS_EFriendsStatus InStatus)
{
	switch (InStatus)
	{
		case EOS_EFriendsStatus::EOS_FS_InviteSent:
		{
			return EInviteStatus::PendingOutbound;
		}
		case EOS_EFriendsStatus::EOS_FS_InviteReceived:
		{
			return EInviteStatus::PendingInbound;
		}
		case EOS_EFriendsStatus::EOS_FS_Friends:
		{
			return EInviteStatus::Accepted;
		}
	}
	return EInviteStatus::Unknown;
}

static inline EOnlinePresenceState::Type ToEOnlinePresenceState(EOS_Presence_EStatus InStatus)
{
	switch (InStatus)
	{
		case EOS_Presence_EStatus::EOS_PS_Online:
		{
			return EOnlinePresenceState::Online;
		}
		case EOS_Presence_EStatus::EOS_PS_Away:
		{
			return EOnlinePresenceState::Away;
		}
		case EOS_Presence_EStatus::EOS_PS_ExtendedAway:
		{
			return EOnlinePresenceState::ExtendedAway;
		}
		case EOS_Presence_EStatus::EOS_PS_DoNotDisturb:
		{
			return EOnlinePresenceState::DoNotDisturb;
		}
	}
	return EOnlinePresenceState::Offline;
}

static inline EOS_Presence_EStatus ToEOS_Presence_EStatus(EOnlinePresenceState::Type InStatus)
{
	switch (InStatus)
	{
		case EOnlinePresenceState::Online:
		{
			return EOS_Presence_EStatus::EOS_PS_Online;
		}
		case EOnlinePresenceState::Away:
		{
			return EOS_Presence_EStatus::EOS_PS_Away;
		}
		case EOnlinePresenceState::ExtendedAway:
		{
			return EOS_Presence_EStatus::EOS_PS_ExtendedAway;
		}
		case EOnlinePresenceState::DoNotDisturb:
		{
			return EOS_Presence_EStatus::EOS_PS_DoNotDisturb;
		}
	}
	return EOS_Presence_EStatus::EOS_PS_Offline;
}

/** Delegates that are used for internal calls and are meant to be ignored */
FOnReadFriendsListComplete IgnoredFriendsDelegate;
IOnlinePresence::FOnPresenceTaskCompleteDelegate IgnoredPresenceDelegate;
IOnlineUser::FOnQueryExternalIdMappingsComplete IgnoredMappingDelegate;

FUserManagerEOS::FUserManagerEOS(FOnlineSubsystemEOS* InSubsystem)
	: EOSSubsystem(InSubsystem)
	, DefaultLocalUser(-1)
	, LoginNotificationId(0)
	, LoginNotificationCallback(nullptr)
	, FriendsNotificationId(0)
	, FriendsNotificationCallback(nullptr)
	, PresenceNotificationId(0)
	, PresenceNotificationCallback(nullptr)
{
}

FUserManagerEOS::~FUserManagerEOS()
{
}

void FUserManagerEOS::LoginStatusChanged(const EOS_Auth_LoginStatusChangedCallbackInfo* Data)
{
	if (Data->CurrentStatus == EOS_ELoginStatus::EOS_LS_NotLoggedIn)
	{
		if (AccountIdToUserNumMap.Contains(Data->LocalUserId))
		{
			int32 LocalUserNum = AccountIdToUserNumMap[Data->LocalUserId];
			FUniqueNetIdEOSPtr UserNetId = UserNumToNetIdMap[LocalUserNum];
			TriggerOnLoginStatusChangedDelegates(LocalUserNum, ELoginStatus::LoggedIn, ELoginStatus::NotLoggedIn, *UserNetId);
			// Need to remove the local user
			RemoveLocalUser(LocalUserNum);

			// Clean up user based notifies if we have no logged in users
			if (UserNumToNetIdMap.Num() == 0)
			{
				if (LoginNotificationId > 0)
				{
					// Remove the callback
					EOS_Auth_RemoveNotifyLoginStatusChanged(EOSSubsystem->AuthHandle, LoginNotificationId);
					delete LoginNotificationCallback;
					LoginNotificationCallback = nullptr;
					LoginNotificationId = 0;
				}
				if (FriendsNotificationId > 0)
				{
					EOS_Friends_RemoveNotifyFriendsUpdate(EOSSubsystem->FriendsHandle, FriendsNotificationId);
					delete FriendsNotificationCallback;
					FriendsNotificationCallback = nullptr;
					FriendsNotificationId = 0;
				}
				if (PresenceNotificationId > 0)
				{
					EOS_Presence_RemoveNotifyOnPresenceChanged(EOSSubsystem->PresenceHandle, PresenceNotificationId);
					delete PresenceNotificationCallback;
					PresenceNotificationCallback = nullptr;
					PresenceNotificationId = 0;
				}
				// Remove the per user connect login notification
				if (LocalUserNumToConnectLoginNotifcationMap.Contains(LocalUserNum))
				{
					FNotificationIdCallbackPair* NotificationPair = LocalUserNumToConnectLoginNotifcationMap[LocalUserNum];
					LocalUserNumToConnectLoginNotifcationMap.Remove(LocalUserNum);

					EOS_Connect_RemoveNotifyAuthExpiration(EOSSubsystem->ConnectHandle, NotificationPair->NotificationId);

					delete NotificationPair;
				}
			}
		}
	}
}

typedef TEOSCallback<EOS_Auth_OnLoginCallback, EOS_Auth_LoginCallbackInfo> FLoginCallback;

struct FAuthCredentials :
	public EOS_Auth_Credentials
{
	FAuthCredentials() :
		EOS_Auth_Credentials()
	{
		ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
		Id = IdAnsi;
		Token = TokenAnsi;
	}
	char IdAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
	char TokenAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
};

bool FUserManagerEOS::Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials)
{
	// We don't support offline logged in, so they are either logged in or not
	if (GetLoginStatus(LocalUserNum) == ELoginStatus::LoggedIn)
	{
		UE_LOG_ONLINE(Warning, TEXT("User (%d) already logged in."), LocalUserNum);
		TriggerOnLoginCompleteDelegates(LocalUserNum, false, *GetLocalUniqueNetIdEOS(LocalUserNum), FString(TEXT("Already logged in")));
		return true;
	}

	EOS_Auth_LoginOptions LoginOptions = { };
	LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
	LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;

	FAuthCredentials Credentials;
	LoginOptions.Credentials = &Credentials;

	if (AccountCredentials.Type == TEXT("exchangecode"))
	{
		// This is how the Epic launcher will pass credentials to you
		FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*AccountCredentials.Token), EOS_OSS_STRING_BUFFER_LENGTH);
		Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
	}
	else if (AccountCredentials.Type == TEXT("developer"))
	{
		// This is auth via the EOS auth tool
		Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
		FCStringAnsi::Strncpy(Credentials.IdAnsi, TCHAR_TO_UTF8(*AccountCredentials.Id), EOS_OSS_STRING_BUFFER_LENGTH);
		FCStringAnsi::Strncpy(Credentials.TokenAnsi, TCHAR_TO_UTF8(*AccountCredentials.Token), EOS_OSS_STRING_BUFFER_LENGTH);
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Unable to Login() user (%d) due to missing auth parameters"), LocalUserNum);
		TriggerOnLoginCompleteDelegates(LocalUserNum, false, FUniqueNetIdEOS(), FString(TEXT("Missing auth parameters")));
		return false;
	}
	FLoginCallback* CallbackObj = new FLoginCallback();
	CallbackObj->CallbackLambda = [this, LocalUserNum](const EOS_Auth_LoginCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			// Continue the login process by getting the product user id
			ConnectLogin(LocalUserNum, Data->LocalUserId);
		}
		else
		{
			FString ErrorString = FString::Printf(TEXT("Login(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
			UE_LOG_ONLINE(Warning, TEXT("%s"), *ErrorString);
			TriggerOnLoginCompleteDelegates(LocalUserNum, false, FUniqueNetIdEOS(), ErrorString);
		}
	};
	// Perform the auth call
	EOS_Auth_Login(EOSSubsystem->AuthHandle, &LoginOptions, (void*)CallbackObj, CallbackObj->GetCallbackPtr());
	return true;
}

typedef TEOSCallback<EOS_Connect_OnLoginCallback, EOS_Connect_LoginCallbackInfo> FConnectLoginCallback;

void FUserManagerEOS::ConnectLogin(int32 LocalUserNum, EOS_EpicAccountId AccountId)
{
	EOS_Auth_Token* AuthToken = nullptr;
	EOS_Auth_CopyUserAuthTokenOptions CopyOptions = { };
	CopyOptions.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;

	EOS_EResult CopyResult = EOS_Auth_CopyUserAuthToken(EOSSubsystem->AuthHandle, &CopyOptions, AccountId, &AuthToken);
	if (CopyResult == EOS_EResult::EOS_Success)
	{
		EOS_Connect_Credentials Credentials = { };
		Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_EPIC;
		Credentials.Token = AuthToken->AccessToken;

		EOS_Connect_LoginOptions Options = { };
		Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		Options.Credentials = &Credentials;

		FConnectLoginCallback* CallbackObj = new FConnectLoginCallback();
		CallbackObj->CallbackLambda = [LocalUserNum, AccountId, this](const EOS_Connect_LoginCallbackInfo* Data)
		{
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				// We have an account mapping, skip to final login
				FullLoginCallback(LocalUserNum, AccountId, Data->LocalUserId);
			}
			else if (Data->ResultCode == EOS_EResult::EOS_InvalidUser)
			{
				// We need to create the mapping for this user using the continuation token
				CreateConnectedLogin(LocalUserNum, AccountId, Data->ContinuanceToken);
			}
			else
			{
				UE_LOG_ONLINE(Error, TEXT("ConnectLogin(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
				Logout(LocalUserNum);
			}
		};
		EOS_Connect_Login(EOSSubsystem->ConnectHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());

		EOS_Auth_Token_Release(AuthToken);
	}
	else
	{
		UE_LOG_ONLINE(Error, TEXT("ConnectLogin(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(CopyResult)));
		Logout(LocalUserNum);
	}
}

void FUserManagerEOS::RefreshConnectLogin(int32 LocalUserNum)
{
	if (!UserNumToAccountIdMap.Contains(LocalUserNum))
	{
		UE_LOG_ONLINE(Error, TEXT("Can't refresh ConnectLogin(%d) since (%d) is not logged in"), LocalUserNum, LocalUserNum);
		return;
	}

	EOS_EpicAccountId AccountId = UserNumToAccountIdMap[LocalUserNum];
	EOS_Auth_Token* AuthToken = nullptr;
	EOS_Auth_CopyUserAuthTokenOptions CopyOptions = { };
	CopyOptions.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;

	EOS_EResult CopyResult = EOS_Auth_CopyUserAuthToken(EOSSubsystem->AuthHandle, &CopyOptions, AccountId, &AuthToken);
	if (CopyResult == EOS_EResult::EOS_Success)
	{
		EOS_Connect_Credentials Credentials = { };
		Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_EPIC;
		Credentials.Token = AuthToken->AccessToken;

		EOS_Connect_LoginOptions Options = { };
		Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		Options.Credentials = &Credentials;

		FConnectLoginCallback* CallbackObj = new FConnectLoginCallback();
		CallbackObj->CallbackLambda = [LocalUserNum, AccountId, this](const EOS_Connect_LoginCallbackInfo* Data)
		{
			if (Data->ResultCode != EOS_EResult::EOS_Success)
			{
				UE_LOG_ONLINE(Error, TEXT("Failed to refresh ConnectLogin(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
				Logout(LocalUserNum);
			}
		};
		EOS_Connect_Login(EOSSubsystem->ConnectHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());

		EOS_Auth_Token_Release(AuthToken);
	}
	else
	{
		UE_LOG_ONLINE(Error, TEXT("Failed to refresh ConnectLogin(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(CopyResult)));
		Logout(LocalUserNum);
	}
}

typedef TEOSCallback<EOS_Connect_OnCreateUserCallback, EOS_Connect_CreateUserCallbackInfo> FCreateUserCallback;

void FUserManagerEOS::CreateConnectedLogin(int32 LocalUserNum, EOS_EpicAccountId AccountId, EOS_ContinuanceToken Token)
{
	EOS_Connect_CreateUserOptions Options = { };
	Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
	Options.ContinuanceToken = Token;

	FCreateUserCallback* CallbackObj = new FCreateUserCallback();
	CallbackObj->CallbackLambda = [LocalUserNum, AccountId, this](const EOS_Connect_CreateUserCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			// We have an account mapping, skip to final login
			FullLoginCallback(LocalUserNum, AccountId, Data->LocalUserId);
		}
		else
		{
// @todo joeg - logout?
			FString ErrorString = FString::Printf(TEXT("Login(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
			TriggerOnLoginCompleteDelegates(LocalUserNum, false, FUniqueNetIdEOS(), ErrorString);
		}
	};
	EOS_Connect_CreateUser(EOSSubsystem->ConnectHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
}

typedef TEOSGlobalCallback<EOS_Connect_OnAuthExpirationCallback, EOS_Connect_AuthExpirationCallbackInfo> FRefreshAuthCallback;
typedef TEOSGlobalCallback<EOS_Presence_OnPresenceChangedCallback, EOS_Presence_PresenceChangedCallbackInfo> FPresenceChangedCallback;
typedef TEOSGlobalCallback<EOS_Friends_OnFriendsUpdateCallback, EOS_Friends_OnFriendsUpdateInfo> FFriendsStatusUpdateCallback;
typedef TEOSGlobalCallback<EOS_Auth_OnLoginStatusChangedCallback, EOS_Auth_LoginStatusChangedCallbackInfo> FLoginStatusChangedCallback;

void FUserManagerEOS::FullLoginCallback(int32 LocalUserNum, EOS_EpicAccountId AccountId, EOS_ProductUserId UserId)
{
	// Add our login status changed callback if not already set
	if (LoginNotificationId == 0)
	{
		FLoginStatusChangedCallback* CallbackObj = new FLoginStatusChangedCallback();
		LoginNotificationCallback = CallbackObj;
		CallbackObj->CallbackLambda = [this](const EOS_Auth_LoginStatusChangedCallbackInfo* Data)
		{
			LoginStatusChanged(Data);
		};

		EOS_Auth_AddNotifyLoginStatusChangedOptions Options = { };
		Options.ApiVersion = EOS_AUTH_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
		LoginNotificationId = EOS_Auth_AddNotifyLoginStatusChanged(EOSSubsystem->AuthHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
	}
	// Register for friends updates if not set yet
	if (FriendsNotificationId == 0)
	{
		FFriendsStatusUpdateCallback* CallbackObj = new FFriendsStatusUpdateCallback();
		FriendsNotificationCallback = CallbackObj;
		CallbackObj->CallbackLambda = [LocalUserNum, this](const EOS_Friends_OnFriendsUpdateInfo* Data)
		{
			FriendStatusChanged(Data);
		};

		EOS_Friends_AddNotifyFriendsUpdateOptions Options = { };
		Options.ApiVersion = EOS_FRIENDS_ADDNOTIFYFRIENDSUPDATE_API_LATEST;
		FriendsNotificationId = EOS_Friends_AddNotifyFriendsUpdate(EOSSubsystem->FriendsHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
	}
	// Register for presence updates if not set yet
	if (PresenceNotificationId == 0)
	{
		FPresenceChangedCallback* CallbackObj = new FPresenceChangedCallback();
		PresenceNotificationCallback = CallbackObj;
		CallbackObj->CallbackLambda = [LocalUserNum, this](const EOS_Presence_PresenceChangedCallbackInfo* Data)
		{
			if (EpicAccountIdToOnlineUserMap.Contains(Data->PresenceUserId))
			{
				// Update the presence data to the most recent
				UpdatePresence(Data->PresenceUserId);
				return;
			}
		};

		EOS_Presence_AddNotifyOnPresenceChangedOptions Options = { };
		Options.ApiVersion = EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST;
		PresenceNotificationId = EOS_Presence_AddNotifyOnPresenceChanged(EOSSubsystem->PresenceHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
	}
	// Add auth refresh notification if not set for this user yet
	if (!LocalUserNumToConnectLoginNotifcationMap.Contains(LocalUserNum))
	{
		FNotificationIdCallbackPair* NotificationPair = new FNotificationIdCallbackPair();
		LocalUserNumToConnectLoginNotifcationMap.Add(LocalUserNum, NotificationPair);

		FRefreshAuthCallback* CallbackObj = new FRefreshAuthCallback();
		NotificationPair->Callback = CallbackObj;
		CallbackObj->CallbackLambda = [LocalUserNum, this](const EOS_Connect_AuthExpirationCallbackInfo* Data)
		{
			RefreshConnectLogin(LocalUserNum);
		};

		EOS_Connect_AddNotifyAuthExpirationOptions Options = { };
		Options.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
		NotificationPair->NotificationId = EOS_Connect_AddNotifyAuthExpiration(EOSSubsystem->ConnectHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
	}

	AddLocalUser(LocalUserNum, AccountId, UserId);
	FUniqueNetIdEOSPtr UserNetId = GetLocalUniqueNetIdEOS(LocalUserNum);
	check(UserNetId.IsValid());

	TriggerOnLoginCompleteDelegates(LocalUserNum, true, *UserNetId, FString());
	TriggerOnLoginStatusChangedDelegates(LocalUserNum, ELoginStatus::NotLoggedIn, ELoginStatus::LoggedIn, *UserNetId);
}

typedef TEOSCallback<EOS_Auth_OnLogoutCallback, EOS_Auth_LogoutCallbackInfo> FLogoutCallback;

bool FUserManagerEOS::Logout(int32 LocalUserNum)
{
	FUniqueNetIdEOSPtr UserId = GetLocalUniqueNetIdEOS(LocalUserNum);
	if (!UserId.IsValid())
	{
		UE_LOG_ONLINE(Warning, TEXT("No logged in user found for LocalUserNum=%d."),
			LocalUserNum);
		TriggerOnLogoutCompleteDelegates(LocalUserNum, false);
		return false;
	}

	FLogoutCallback* CallbackObj = new FLogoutCallback();
	CallbackObj->CallbackLambda = [LocalUserNum, this](const EOS_Auth_LogoutCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			RemoveLocalUser(LocalUserNum);

			TriggerOnLogoutCompleteDelegates(LocalUserNum, true);
		}
		else
		{
			TriggerOnLogoutCompleteDelegates(LocalUserNum, false);
		}
	};

	EOS_Auth_LogoutOptions LogoutOptions = { };
	LogoutOptions.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
	LogoutOptions.LocalUserId = StringToAccountIdMap[UserId->UniqueNetIdStr];

	EOS_Auth_Logout(EOSSubsystem->AuthHandle, &LogoutOptions, CallbackObj, CallbackObj->GetCallbackPtr());

	return true;
}

bool FUserManagerEOS::AutoLogin(int32 LocalUserNum)
{
	FString LoginId;
	FString Password;
	FString AuthType;

	FParse::Value(FCommandLine::Get(), TEXT("AUTH_LOGIN="), LoginId);
	FParse::Value(FCommandLine::Get(), TEXT("AUTH_PASSWORD="), Password);
	FParse::Value(FCommandLine::Get(), TEXT("AUTH_TYPE="), AuthType);

	if (LoginId.IsEmpty() || Password.IsEmpty() || AuthType.IsEmpty())
	{
		UE_LOG_ONLINE(Warning, TEXT("Unable to AutoLogin user (%d) due to missing auth command line args"), LocalUserNum);
		return false;
	}
	FOnlineAccountCredentials Creds(AuthType, LoginId, Password);
	return Login(LocalUserNum, Creds);
}

void FUserManagerEOS::AddLocalUser(int32 LocalUserNum, EOS_EpicAccountId EpicAccountId, EOS_ProductUserId UserId)
{
	// Set the default user to the first one that logs in
	if (DefaultLocalUser == -1)
	{
		DefaultLocalUser = LocalUserNum;
	}

	const FString& NetId = MakeNetIdStringFromIds(EpicAccountId, UserId);
	FUniqueNetIdEOSRef UserNetId(new FUniqueNetIdEOS(NetId));
	FUserOnlineAccountEOSRef UserAccountRef(new FUserOnlineAccountEOS(UserNetId));

	UserNumToNetIdMap.Add(LocalUserNum, UserNetId);
	UserNumToAccountIdMap.Add(LocalUserNum, EpicAccountId);
	AccountIdToUserNumMap.Add(EpicAccountId, LocalUserNum);
	NetIdStringToOnlineUserMap.Add(*NetId, UserAccountRef);
	StringToUserAccountMap.Add(NetId, UserAccountRef);
	AccountIdToStringMap.Add(EpicAccountId, NetId);
	ProductUserIdToStringMap.Add(UserId, *NetId);
	StringToAccountIdMap.Add(NetId, EpicAccountId);
	EpicAccountIdToAttributeAccessMap.Add(EpicAccountId, UserAccountRef);
	UserNumToProductUserIdMap.Add(LocalUserNum, UserId);
	ProductUserIdToUserNumMap.Add(UserId, LocalUserNum);
	StringToProductUserIdMap.Add(NetId, UserId);

	// Init player lists
	FFriendsListEOSRef FriendsList = MakeShareable(new FFriendsListEOS(LocalUserNum, UserNetId));
	FBlockedPlayersListEOSRef BlockedPlayersList = MakeShareable(new FBlockedPlayersListEOS(LocalUserNum, UserNetId));
	FRecentPlayersListEOSRef RecentPlayersList = MakeShareable(new FRecentPlayersListEOS(LocalUserNum, UserNetId));

	LocalUserNumToFriendsListMap.Add(LocalUserNum, FriendsList);
	NetIdStringToFriendsListMap.Add(NetId, FriendsList);
	LocalUserNumToBlockedPlayerListMap.Add(LocalUserNum, BlockedPlayersList);
	NetIdStringToBlockedPlayerListMap.Add(NetId, BlockedPlayersList);
	LocalUserNumToRecentPlayerListMap.Add(LocalUserNum, RecentPlayersList);
	NetIdStringToRecentPlayerListMap.Add(NetId, RecentPlayersList);

	// Get auth token info
	EOS_Auth_Token* AuthToken = nullptr;
	EOS_Auth_CopyUserAuthTokenOptions Options = { };
	Options.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;

	EOS_EResult CopyResult = EOS_Auth_CopyUserAuthToken(EOSSubsystem->AuthHandle, &Options, EpicAccountId, &AuthToken);
	if (CopyResult == EOS_EResult::EOS_Success)
	{
		UserAccountRef->SetAuthAttribute(AUTH_ATTR_ID_TOKEN, AuthToken->AccessToken);
		EOS_Auth_Token_Release(AuthToken);

		UpdateUserInfo(UserAccountRef, EpicAccountId, EpicAccountId);
	}

	// Kick off reads of the friends, recent, and block players lists
	ReadFriendsList(LocalUserNum, FString(), IgnoredFriendsDelegate);
	QueryRecentPlayers(*UserNetId, FString());
	QueryBlockedPlayers(*UserNetId);
}

void FUserManagerEOS::UpdateUserInfo(IAttributeAccessInterfaceRef AttributeAccessRef, EOS_EpicAccountId LocalId, EOS_EpicAccountId AccountId)
{
	EOS_UserInfo_CopyUserInfoOptions Options = { };
	Options.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
	Options.LocalUserId = LocalId;
	Options.TargetUserId = AccountId;

	EOS_UserInfo* UserInfo = nullptr;

	EOS_EResult CopyResult = EOS_UserInfo_CopyUserInfo(EOSSubsystem->UserInfoHandle, &Options, &UserInfo);
	if (CopyResult == EOS_EResult::EOS_Success)
	{
		AttributeAccessRef->SetInternalAttribute(USER_ATTR_DISPLAY_NAME, UserInfo->DisplayName);
		AttributeAccessRef->SetInternalAttribute(USER_ATTR_COUNTRY, UserInfo->Country);
		AttributeAccessRef->SetInternalAttribute(USER_ATTR_LANG, UserInfo->PreferredLanguage);
		EOS_UserInfo_Release(UserInfo);
	}
}

TSharedPtr<FUserOnlineAccount> FUserManagerEOS::GetUserAccount(const FUniqueNetId& UserId) const
{
	TSharedPtr<FUserOnlineAccount> Result;

	FUniqueNetIdEOS EOSID(UserId);
	const FUserOnlineAccountEOSRef* FoundUserAccount = StringToUserAccountMap.Find(EOSID.UniqueNetIdStr);
	if (FoundUserAccount != nullptr)
	{
		return *FoundUserAccount;
	}
	return nullptr;
}

TArray<TSharedPtr<FUserOnlineAccount>> FUserManagerEOS::GetAllUserAccounts() const
{
	TArray<TSharedPtr<FUserOnlineAccount>> Result;

	for (TMap<FString, FUserOnlineAccountEOSRef>::TConstIterator It(StringToUserAccountMap); It; ++It)
	{
		Result.Add(It.Value());
	}
	return Result;
}

TSharedPtr<const FUniqueNetId> FUserManagerEOS::GetUniquePlayerId(int32 LocalUserNum) const
{
	return GetLocalUniqueNetIdEOS(LocalUserNum);
}

int32 FUserManagerEOS::GetLocalUserNumFromUniqueNetId(const FUniqueNetId& NetId) const
{
	FUniqueNetIdEOS EosId(NetId);
	if (StringToAccountIdMap.Contains(EosId.UniqueNetIdStr))
	{
		EOS_EpicAccountId AccountId = StringToAccountIdMap[EosId.UniqueNetIdStr];
		if (AccountIdToUserNumMap.Contains(AccountId))
		{
			return AccountIdToUserNumMap[AccountId];
		}
	}
	// Use the default user if we can't find the person that they want
	return DefaultLocalUser;
}

FUniqueNetIdEOSPtr FUserManagerEOS::GetLocalUniqueNetIdEOS(int32 LocalUserNum) const
{
	const FUniqueNetIdEOSPtr* FoundId = UserNumToNetIdMap.Find(LocalUserNum);
	if (FoundId != nullptr)
	{
		return *FoundId;
	}
	return nullptr;
}

FUniqueNetIdEOSPtr FUserManagerEOS::GetLocalUniqueNetIdEOS(EOS_ProductUserId UserId) const
{
	if (ProductUserIdToUserNumMap.Contains(UserId))
	{
		return GetLocalUniqueNetIdEOS(ProductUserIdToUserNumMap[UserId]);
	}
	return nullptr;
}

FUniqueNetIdEOSPtr FUserManagerEOS::GetLocalUniqueNetIdEOS(EOS_EpicAccountId AccountId) const
{
	if (AccountIdToUserNumMap.Contains(AccountId))
	{
		return GetLocalUniqueNetIdEOS(AccountIdToUserNumMap[AccountId]);
	}
	return nullptr;
}

EOS_EpicAccountId FUserManagerEOS::GetLocalEpicAccountId(int32 LocalUserNum) const
{
	if (UserNumToAccountIdMap.Contains(LocalUserNum))
	{
		return UserNumToAccountIdMap[LocalUserNum];
	}
	return nullptr;
}

EOS_EpicAccountId FUserManagerEOS::GetLocalEpicAccountId() const
{
	return GetLocalEpicAccountId(DefaultLocalUser);
}

EOS_ProductUserId FUserManagerEOS::GetLocalProductUserId(int32 LocalUserNum) const
{
	if (UserNumToProductUserIdMap.Contains(LocalUserNum))
	{
		return UserNumToProductUserIdMap[LocalUserNum];
	}
	return nullptr;
}

EOS_ProductUserId FUserManagerEOS::GetLocalProductUserId() const
{
	return GetLocalProductUserId(DefaultLocalUser);
}

EOS_EpicAccountId FUserManagerEOS::GetLocalEpicAccountId(EOS_ProductUserId UserId) const
{
	if (ProductUserIdToUserNumMap.Contains(UserId))
	{
		return GetLocalEpicAccountId(ProductUserIdToUserNumMap[UserId]);
	}
	return nullptr;
}

EOS_ProductUserId FUserManagerEOS::GetLocalProductUserId(EOS_EpicAccountId AccountId) const
{
	if (AccountIdToUserNumMap.Contains(AccountId))
	{
		return GetLocalProductUserId(AccountIdToUserNumMap[AccountId]);
	}
	return nullptr;
}

EOS_EpicAccountId FUserManagerEOS::GetEpicAccountId(const FUniqueNetId& NetId) const
{
	FUniqueNetIdEOS EOSId(NetId);

	if (StringToAccountIdMap.Contains(EOSId.UniqueNetIdStr))
	{
		return StringToAccountIdMap[EOSId.UniqueNetIdStr];
	}
	return nullptr;
}

EOS_ProductUserId FUserManagerEOS::GetProductUserId(const FUniqueNetId& NetId) const
{
	FUniqueNetIdEOS EOSId(NetId);

	if (StringToProductUserIdMap.Contains(EOSId.UniqueNetIdStr))
	{
		return StringToProductUserIdMap[EOSId.UniqueNetIdStr];
	}
	return nullptr;
}

FOnlineUserPtr FUserManagerEOS::GetLocalOnlineUser(int32 LocalUserNum) const
{
	FOnlineUserPtr OnlineUser;
	if (UserNumToNetIdMap.Contains(LocalUserNum))
	{
		const FUniqueNetIdEOSPtr NetId = UserNumToNetIdMap.FindRef(LocalUserNum);
		if (NetIdStringToOnlineUserMap.Contains(*NetId->UniqueNetIdStr))
		{
			OnlineUser = NetIdStringToOnlineUserMap.FindRef(*NetId->UniqueNetIdStr);
		}
	}
	return OnlineUser;
}

FOnlineUserPtr FUserManagerEOS::GetOnlineUser(EOS_ProductUserId UserId) const
{
	FOnlineUserPtr OnlineUser;
	if (ProductUserIdToStringMap.Contains(UserId))
	{
		const FString& NetId = ProductUserIdToStringMap.FindRef(UserId);
		if (NetIdStringToOnlineUserMap.Contains(*NetId))
		{
			OnlineUser = NetIdStringToOnlineUserMap.FindRef(*NetId);
		}
	}
	return OnlineUser;
}

FOnlineUserPtr FUserManagerEOS::GetOnlineUser(EOS_EpicAccountId AccountId) const
{
	FOnlineUserPtr OnlineUser;
	if (AccountIdToStringMap.Contains(AccountId))
	{
		const FString& NetId = AccountIdToStringMap.FindRef(AccountId);
		if (NetIdStringToOnlineUserMap.Contains(*NetId))
		{
			OnlineUser = NetIdStringToOnlineUserMap.FindRef(*NetId);
		}
	}
	return OnlineUser;
}

void FUserManagerEOS::RemoveLocalUser(int32 LocalUserNum)
{
	const FUniqueNetIdEOSPtr* FoundId = UserNumToNetIdMap.Find(LocalUserNum);
	if (FoundId != nullptr)
	{
		LocalUserNumToFriendsListMap.Remove(LocalUserNum);
		const FString& NetId = (*FoundId)->UniqueNetIdStr;
		EOS_EpicAccountId AccountId = StringToAccountIdMap[NetId];
		AccountIdToStringMap.Remove(AccountId);
		AccountIdToUserNumMap.Remove(AccountId);
		NetIdStringToOnlineUserMap.Remove(NetId);
		StringToAccountIdMap.Remove(NetId);
		StringToUserAccountMap.Remove(NetId);
		UserNumToNetIdMap.Remove(LocalUserNum);
		UserNumToAccountIdMap.Remove(LocalUserNum);
		EOS_ProductUserId UserId = UserNumToProductUserIdMap[LocalUserNum];
		ProductUserIdToUserNumMap.Remove(UserId);
		ProductUserIdToStringMap.Remove(UserId);
		UserNumToProductUserIdMap.Remove(LocalUserNum);
	}
	// Reset this for the next user login
	if (LocalUserNum == DefaultLocalUser)
	{
		DefaultLocalUser = -1;
	}
}

TSharedPtr<const FUniqueNetId> FUserManagerEOS::CreateUniquePlayerId(uint8* Bytes, int32 Size)
{
	if (Bytes != nullptr && Size > 0)
	{
		FString StrId(Size, (TCHAR*)Bytes);
		return MakeShareable(new FUniqueNetIdEOS(StrId));
	}
	return nullptr;
}

TSharedPtr<const FUniqueNetId> FUserManagerEOS::CreateUniquePlayerId(const FString& Str)
{
	return MakeShareable(new FUniqueNetIdEOS(Str));
}

ELoginStatus::Type FUserManagerEOS::GetLoginStatus(int32 LocalUserNum) const
{
	FUniqueNetIdEOSPtr UserId = GetLocalUniqueNetIdEOS(LocalUserNum);
	if (UserId.IsValid())
	{
		return GetLoginStatus(*UserId);
	}
	return ELoginStatus::NotLoggedIn;
}

ELoginStatus::Type FUserManagerEOS::GetLoginStatus(const FUniqueNetIdEOS& UserId) const
{
	if (!StringToAccountIdMap.Contains(UserId.UniqueNetIdStr))
	{
		return ELoginStatus::NotLoggedIn;
	}

	EOS_EpicAccountId AccountId = StringToAccountIdMap[UserId.UniqueNetIdStr];
	if (AccountId == nullptr)
	{
		return ELoginStatus::NotLoggedIn;
	}

	EOS_ELoginStatus LoginStatus = EOS_Auth_GetLoginStatus(EOSSubsystem->AuthHandle, AccountId);
	switch (LoginStatus)
	{
		case EOS_ELoginStatus::EOS_LS_LoggedIn:
		{
			return ELoginStatus::LoggedIn;
		}
		case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:
		{
			return ELoginStatus::UsingLocalProfile;
		}
	}
	return ELoginStatus::NotLoggedIn;
}

ELoginStatus::Type FUserManagerEOS::GetLoginStatus(const FUniqueNetId& UserId) const
{
	FUniqueNetIdEOS EosId(UserId);
	return GetLoginStatus(EosId);
}

FString FUserManagerEOS::GetPlayerNickname(int32 LocalUserNum) const
{
	FUniqueNetIdEOSPtr UserId = GetLocalUniqueNetIdEOS(LocalUserNum);
	if (UserId.IsValid())
	{
		TSharedPtr<FUserOnlineAccount> UserAccount = GetUserAccount(*UserId);
		if (UserAccount.IsValid())
		{
			return UserAccount->GetDisplayName();
		}
	}
	return FString();
}

FString FUserManagerEOS::GetPlayerNickname(const FUniqueNetId& UserId) const
{
	TSharedPtr<FUserOnlineAccount> UserAccount = GetUserAccount(UserId);
	if (UserAccount.IsValid())
	{
		return UserAccount->GetDisplayName();
	}
	return FString();
}

FString FUserManagerEOS::GetAuthToken(int32 LocalUserNum) const
{
	TSharedPtr<const FUniqueNetId> UserId = GetUniquePlayerId(LocalUserNum);
	if (UserId.IsValid())
	{
		TSharedPtr<FUserOnlineAccount> UserAccount = GetUserAccount(*UserId);
		if (UserAccount.IsValid())
		{
			return UserAccount->GetAccessToken();
		}
	}
	return FString();
}

void FUserManagerEOS::RevokeAuthToken(const FUniqueNetId& LocalUserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(LocalUserId, FOnlineError(EOnlineErrorResult::NotImplemented));
}

FPlatformUserId FUserManagerEOS::GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const
{
	return GetLocalUserNumFromUniqueNetId(UniqueNetId);
}

void FUserManagerEOS::GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate)
{
	Delegate.ExecuteIfBound(UserId, Privilege, (uint32)EPrivilegeResults::NoFailures);
}

FString FUserManagerEOS::GetAuthType() const
{
	return TEXT("epic");
}

typedef TEOSCallback<EOS_Friends_OnQueryFriendsCallback, EOS_Friends_QueryFriendsCallbackInfo> FReadFriendsCallback;

bool FUserManagerEOS::ReadFriendsList(int32 LocalUserNum, const FString& ListName, const FOnReadFriendsListComplete& Delegate)
{
	if (!UserNumToNetIdMap.Contains(LocalUserNum))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't ReadFriendsList() for user (%d) since they are not logged in"), LocalUserNum);
		Delegate.ExecuteIfBound(LocalUserNum, false, ListName, FString(TEXT("Can't ReadFriendsList() for user (%d) since they are not logged in"), LocalUserNum));
		return false;
	}

	EOS_Friends_QueryFriendsOptions Options = { };
	Options.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[LocalUserNum];

	FReadFriendsCallback* CallbackObj = new FReadFriendsCallback();
	CallbackObj->CallbackLambda = [LocalUserNum, ListName, this, Delegate](const EOS_Friends_QueryFriendsCallbackInfo* Data)
	{
		EOS_EResult Result = Data->ResultCode;
		if (GetLoginStatus(LocalUserNum) != ELoginStatus::LoggedIn)
		{
			// Handle the user logging out while a read is in progress
			Result = EOS_EResult::EOS_InvalidUser;
		}

		FString ErrorString;
		bool bWasSuccessful = Result == EOS_EResult::EOS_Success;
		if (bWasSuccessful)
		{
			EOS_Friends_GetFriendsCountOptions Options = { };
			Options.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
			Options.LocalUserId = UserNumToAccountIdMap[LocalUserNum];
			int32 FriendCount = EOS_Friends_GetFriendsCount(EOSSubsystem->FriendsHandle, &Options);

			// Process each friend returned
			for (int32 Index = 0; Index < FriendCount; Index++)
			{
				EOS_Friends_GetFriendAtIndexOptions FriendIndexOptions = { };
				FriendIndexOptions.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
				FriendIndexOptions.Index = Index;
				FriendIndexOptions.LocalUserId = Options.LocalUserId;
				EOS_EpicAccountId FriendEpicAccountId = EOS_Friends_GetFriendAtIndex(EOSSubsystem->FriendsHandle, &FriendIndexOptions);
				if (FriendEpicAccountId != nullptr)
				{
					// Make sure this friend wasn't added via friend status change
					if (!AccountIdToStringMap.Contains(FriendEpicAccountId))
					{
						AddFriend(LocalUserNum, FriendEpicAccountId);
					}
				}
			}
		}
		else
		{
			ErrorString = FString::Printf(TEXT("ReadFriendsList(%d) failed with EOS result code (%s)"), LocalUserNum, ANSI_TO_TCHAR(EOS_EResult_ToString(Result)));
		}

		Delegate.ExecuteIfBound(LocalUserNum, bWasSuccessful, ListName, ErrorString);
		TriggerOnFriendsChangeDelegates(LocalUserNum);
	};
	EOS_Friends_QueryFriends(EOSSubsystem->FriendsHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());

	return true;
}

void FUserManagerEOS::FriendStatusChanged(const EOS_Friends_OnFriendsUpdateInfo* Data)
{
	// This seems to happen due to the SDK's local cache going from empty to filled, so ignore it
	// It's not really a valid transition since there should have been a pending invite inbetween
	if (Data->PreviousStatus == EOS_EFriendsStatus::EOS_FS_NotFriends && Data->CurrentStatus == EOS_EFriendsStatus::EOS_FS_Friends)
	{
		return;
	}

	// Get the local user information
	if (AccountIdToUserNumMap.Contains(Data->LocalUserId))
	{
		int32 LocalUserNum = AccountIdToUserNumMap[Data->LocalUserId];
		FUniqueNetIdEOSPtr LocalEOSID = UserNumToNetIdMap[LocalUserNum];
		// If we don't know them yet, then add them to kick off the reads
		if (!AccountIdToStringMap.Contains(Data->TargetUserId))
		{
			AddFriend(LocalUserNum, Data->TargetUserId);
		}
		// They are in our list now
		FOnlineUserPtr OnlineUser = EpicAccountIdToOnlineUserMap[Data->TargetUserId];
		FOnlineFriendEOSPtr Friend = LocalUserNumToFriendsListMap[LocalUserNum]->GetByNetIdString(AccountIdToStringMap[Data->TargetUserId]);
		// Figure out which notification to fire
		if (Data->CurrentStatus == EOS_EFriendsStatus::EOS_FS_Friends)
		{
			Friend->SetInviteStatus(EInviteStatus::Accepted);
			TriggerOnInviteAcceptedDelegates(*LocalEOSID, *OnlineUser->GetUserId());
		}
		else if (Data->PreviousStatus == EOS_EFriendsStatus::EOS_FS_Friends && Data->CurrentStatus == EOS_EFriendsStatus::EOS_FS_NotFriends)
		{
			LocalUserNumToFriendsListMap[LocalUserNum]->Remove(AccountIdToStringMap[Data->TargetUserId], Friend.ToSharedRef());
			Friend->SetInviteStatus(EInviteStatus::Unknown);
			TriggerOnFriendRemovedDelegates(*LocalEOSID, *OnlineUser->GetUserId());
		}
		else if (Data->PreviousStatus < EOS_EFriendsStatus::EOS_FS_Friends && Data->CurrentStatus == EOS_EFriendsStatus::EOS_FS_NotFriends)
		{
			LocalUserNumToFriendsListMap[LocalUserNum]->Remove(AccountIdToStringMap[Data->TargetUserId], Friend.ToSharedRef());
			Friend->SetInviteStatus(EInviteStatus::Unknown);
			TriggerOnInviteRejectedDelegates(*LocalEOSID, *OnlineUser->GetUserId());
		}
		else if (Data->CurrentStatus == EOS_EFriendsStatus::EOS_FS_InviteReceived)
		{
			Friend->SetInviteStatus(EInviteStatus::PendingInbound);
			TriggerOnInviteReceivedDelegates(*LocalEOSID, *OnlineUser->GetUserId());
		}
		TriggerOnFriendsChangeDelegates(LocalUserNum);
	}
}

void FUserManagerEOS::AddFriend(int32 LocalUserNum, EOS_EpicAccountId EpicAccountId)
{
	const FString& NetId = MakeStringFromEpicAccountId(EpicAccountId);
	FUniqueNetIdEOSRef FriendNetId(new FUniqueNetIdEOS(NetId));
	FOnlineFriendEOSRef FriendRef = MakeShareable(new FOnlineFriendEOS(FriendNetId));
	LocalUserNumToFriendsListMap[LocalUserNum]->Add(NetId, FriendRef);

	EOS_Friends_GetStatusOptions Options = { };
	Options.ApiVersion = EOS_FRIENDS_GETSTATUS_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[LocalUserNum];
	Options.TargetUserId = EpicAccountId;
	EOS_EFriendsStatus Status = EOS_Friends_GetStatus(EOSSubsystem->FriendsHandle, &Options);
	
	FriendRef->SetInviteStatus(ToEInviteStatus(Status));

	// Add this friend as a remote (this will grab presence & user info)
	AddRemotePlayer(NetId, EpicAccountId, FriendNetId, FriendRef, FriendRef);
}

void FUserManagerEOS::AddRemotePlayer(const FString& NetId, EOS_EpicAccountId EpicAccountId)
{
	FUniqueNetIdEOSRef EOSID(new FUniqueNetIdEOS(NetId));
	FOnlineUserEOSRef UserRef = MakeShareable(new FOnlineUserEOS(EOSID));
	// Add this user as a remote (this will grab presence & user info)
	AddRemotePlayer(NetId, EpicAccountId, EOSID, UserRef, UserRef);
}

void FUserManagerEOS::AddRemotePlayer(const FString& NetId, EOS_EpicAccountId EpicAccountId, FUniqueNetIdEOSPtr UniqueNetId, FOnlineUserPtr OnlineUser, IAttributeAccessInterfaceRef AttributeRef)
{
	NetIdStringToOnlineUserMap.Add(NetId, OnlineUser);
	EpicAccountIdToOnlineUserMap.Add(EpicAccountId, OnlineUser);
	NetIdStringToAttributeAccessMap.Add(NetId, AttributeRef);
	EpicAccountIdToAttributeAccessMap.Add(EpicAccountId, AttributeRef);

	StringToAccountIdMap.Add(NetId, EpicAccountId);
	AccountIdToStringMap.Add(EpicAccountId, NetId);

	// Read the user info for this player
	ReadUserInfo(EpicAccountId);
	// Read presence for this remote player
	QueryPresence(*UniqueNetId, IgnoredPresenceDelegate);
	// Get their product id mapping
	FUniqueNetIdEOSPtr LocalNetId = GetLocalUniqueNetIdEOS(DefaultLocalUser);
	if (LocalNetId.IsValid())
	{
		TArray<FString> ExternalIds;
		ExternalIds.Add(NetId);
		QueryExternalIdMappings(*LocalNetId, FExternalIdQueryOptions(), ExternalIds, IgnoredMappingDelegate);
	}
}

void FUserManagerEOS::UpdateRemotePlayerProductUserId(EOS_EpicAccountId AccountId, EOS_ProductUserId UserId)
{
	// See if the net ids have changed for this user and bail if they are the same
	FString NewNetIdStr = MakeNetIdStringFromIds(AccountId, UserId);
	const FString PrevNetIdStr = AccountIdToStringMap[AccountId];
	if (PrevNetIdStr == NewNetIdStr)
	{
		// No change, so skip any work
		return;
	}

	FString AccountIdStr = MakeStringFromEpicAccountId(AccountId);
	FString UserIdStr = MakeStringFromProductUserId(UserId);

	// Get the unique net id and rebuild the string for it
	IAttributeAccessInterfaceRef AttrAccess = NetIdStringToAttributeAccessMap[PrevNetIdStr];
	FUniqueNetIdEOSPtr NetIdEOS = AttrAccess->GetUniqueNetIdEOS();
	if (NetIdEOS.IsValid())
	{
		NetIdEOS->UpdateNetIdStr(NewNetIdStr);
	}
	// Update any old friends entries with the new net id key
	for (TMap<int32, FFriendsListEOSRef>::TConstIterator It(LocalUserNumToFriendsListMap); It; ++It)
	{
		FFriendsListEOSRef FriendsList = It.Value();
		FOnlineFriendEOSPtr FoundFriend = FriendsList->GetByNetIdString(PrevNetIdStr);
		if (FoundFriend.IsValid())
		{
			FriendsList->UpdateNetIdStr(PrevNetIdStr, NewNetIdStr);
		}
	}
	// Update all of the other net id to X mappings
	AccountIdToStringMap.Remove(AccountId);
	AccountIdToStringMap.Add(AccountId, NewNetIdStr);
	ProductUserIdToStringMap.Remove(UserId);
	ProductUserIdToStringMap.Add(UserId, *NewNetIdStr);
	StringToAccountIdMap.Remove(PrevNetIdStr);
	StringToAccountIdMap.Add(NewNetIdStr, AccountId);
	StringToProductUserIdMap.Add(NewNetIdStr, UserId);
	FOnlineUserPtr OnlineUser = NetIdStringToOnlineUserMap[PrevNetIdStr];
	NetIdStringToOnlineUserMap.Remove(PrevNetIdStr);
	NetIdStringToOnlineUserMap.Add(NewNetIdStr, OnlineUser);
	NetIdStringToAttributeAccessMap.Remove(PrevNetIdStr);
	NetIdStringToAttributeAccessMap.Add(NewNetIdStr, AttrAccess);
	// Presence may not be available for all online users
	if (NetIdStringToOnlineUserPresenceMap.Contains(PrevNetIdStr))
	{
		FOnlineUserPresenceRef UserPresence = NetIdStringToOnlineUserPresenceMap[PrevNetIdStr];
		NetIdStringToOnlineUserPresenceMap.Remove(PrevNetIdStr);
		NetIdStringToOnlineUserPresenceMap.Add(NewNetIdStr, UserPresence);
	}
}

void FUserManagerEOS::SetFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FString& Alias, const FOnSetFriendAliasComplete& Delegate)
{
	Delegate.ExecuteIfBound(LocalUserNum, FriendId, ListName, FOnlineError(EOnlineErrorResult::NotImplemented));
}

void FUserManagerEOS::DeleteFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnDeleteFriendAliasComplete& Delegate)
{
	Delegate.ExecuteIfBound(LocalUserNum, FriendId, ListName, FOnlineError(EOnlineErrorResult::NotImplemented));
}

bool FUserManagerEOS::DeleteFriendsList(int32 LocalUserNum, const FString& ListName, const FOnDeleteFriendsListComplete& Delegate)
{
	Delegate.ExecuteIfBound(LocalUserNum, false, ListName, TEXT("Not supported"));
	return false;
}

typedef TEOSCallback<EOS_Friends_OnSendInviteCallback, EOS_Friends_SendInviteCallbackInfo> FSendInviteCallback;

bool FUserManagerEOS::SendInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnSendInviteComplete& Delegate)
{
	if (!UserNumToNetIdMap.Contains(LocalUserNum))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't AcceptInvite() for user (%d) since they are not logged in"), LocalUserNum);
		Delegate.ExecuteIfBound(LocalUserNum, false, FriendId, ListName, FString(TEXT("Can't AcceptInvite() for user (%d) since they are not logged in"), LocalUserNum));
		return false;
	}

	FUniqueNetIdEOS EOSID(FriendId);
	if (!StringToAccountIdMap.Contains(EOSID.UniqueNetIdStr))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't SendInvite() for user (%d) since the potential player id is unknown"), LocalUserNum);
		Delegate.ExecuteIfBound(LocalUserNum, false, FriendId, ListName, FString(TEXT("Can't AcceptInvite() for user (%d) since the player id is unknown"), LocalUserNum));
		return false;
	}

	FSendInviteCallback* CallbackObj = new FSendInviteCallback();
	CallbackObj->CallbackLambda = [LocalUserNum, ListName, this, Delegate](const EOS_Friends_SendInviteCallbackInfo* Data)
	{
		FString NetId = AccountIdToStringMap[Data->TargetUserId];
		FUniqueNetIdEOS EOSID(NetId);

		FString ErrorString;
		bool bWasSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
		if (!bWasSuccessful)
		{
			ErrorString = FString::Printf(TEXT("Failed to send invite for user (%d) to player (%s) with result code (%s)"), LocalUserNum, *NetId, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		}
		Delegate.ExecuteIfBound(LocalUserNum, bWasSuccessful, EOSID, ListName, ErrorString);
	};

	EOS_Friends_SendInviteOptions Options = { };
	Options.ApiVersion = EOS_FRIENDS_SENDINVITE_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[LocalUserNum];
	Options.TargetUserId = StringToAccountIdMap[EOSID.UniqueNetIdStr];
	EOS_Friends_SendInvite(EOSSubsystem->FriendsHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());

	return true;
}

typedef TEOSCallback<EOS_Friends_OnAcceptInviteCallback, EOS_Friends_AcceptInviteCallbackInfo> FAcceptInviteCallback;

bool FUserManagerEOS::AcceptInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnAcceptInviteComplete& Delegate)
{
	if (!UserNumToNetIdMap.Contains(LocalUserNum))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't AcceptInvite() for user (%d) since they are not logged in"), LocalUserNum);
		Delegate.ExecuteIfBound(LocalUserNum, false, FriendId, ListName, FString(TEXT("Can't AcceptInvite() for user (%d) since they are not logged in"), LocalUserNum));
		return false;
	}

	FUniqueNetIdEOS EOSID(FriendId);
	if (!StringToAccountIdMap.Contains(EOSID.UniqueNetIdStr))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't AcceptInvite() for user (%d) since the friend is not in their list"), LocalUserNum);
		Delegate.ExecuteIfBound(LocalUserNum, false, FriendId, ListName, FString(TEXT("Can't AcceptInvite() for user (%d) since the friend is not in their list"), LocalUserNum));
		return false;
	}

	FAcceptInviteCallback* CallbackObj = new FAcceptInviteCallback();
	CallbackObj->CallbackLambda = [LocalUserNum, ListName, this, Delegate](const EOS_Friends_AcceptInviteCallbackInfo* Data)
	{
		FString NetId = AccountIdToStringMap[Data->TargetUserId];
		FUniqueNetIdEOS EOSID(NetId);

		FString ErrorString;
		bool bWasSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
		if (!bWasSuccessful)
		{
			ErrorString = FString::Printf(TEXT("Failed to accept invite for user (%d) from friend (%s) with result code (%s)"), LocalUserNum, *NetId, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		}
		Delegate.ExecuteIfBound(LocalUserNum, bWasSuccessful, EOSID, ListName, ErrorString);
	};

	EOS_Friends_AcceptInviteOptions Options = { };
	Options.ApiVersion = EOS_FRIENDS_ACCEPTINVITE_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[LocalUserNum];
	Options.TargetUserId = StringToAccountIdMap[EOSID.UniqueNetIdStr];
	EOS_Friends_AcceptInvite(EOSSubsystem->FriendsHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
	return true;
}

void EOS_CALL EOSRejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data)
{
	// We don't need to notify anyone so ignore
}

bool FUserManagerEOS::RejectInvite(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	if (!UserNumToNetIdMap.Contains(LocalUserNum))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't RejectInvite() for user (%d) since they are not logged in"), LocalUserNum);
		return false;
	}

	FUniqueNetIdEOS EOSID(FriendId);
	if (!StringToAccountIdMap.Contains(EOSID.UniqueNetIdStr))
	{
		UE_LOG_ONLINE(Warning, TEXT("Can't RejectInvite() for user (%d) since the friend is not in their list"), LocalUserNum);
		return false;
	}

	EOS_Friends_RejectInviteOptions Options{ 0 };
	Options.ApiVersion = EOS_FRIENDS_REJECTINVITE_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[LocalUserNum];
	Options.TargetUserId = StringToAccountIdMap[EOSID.UniqueNetIdStr];
	EOS_Friends_RejectInvite(EOSSubsystem->FriendsHandle, &Options, nullptr, &EOSRejectInviteCallback);
	return true;
}

bool FUserManagerEOS::DeleteFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	UE_LOG_ONLINE(Error, TEXT("Only the Epic Games Launcher can delete friends"));
	return false;
}

bool FUserManagerEOS::GetFriendsList(int32 LocalUserNum, const FString& ListName, TArray<TSharedRef<FOnlineFriend>>& OutFriends)
{
	OutFriends.Reset();
	if (LocalUserNumToFriendsListMap.Contains(LocalUserNum))
	{
		FFriendsListEOSRef FriendsList = LocalUserNumToFriendsListMap[LocalUserNum];
		for (FOnlineFriendEOSRef Friend : FriendsList->GetList())
		{
			const FOnlineUserPresence& Presence = Friend->GetPresence();
			// See if they only want online only
			if (ListName == EFriendsLists::ToString(EFriendsLists::OnlinePlayers) && !Presence.bIsOnline)
			{
				continue;
			}
			// Of if they only want friends playing this game
			else if (ListName == EFriendsLists::ToString(EFriendsLists::InGamePlayers) && !Presence.bIsPlayingThisGame)
			{
				continue;
			}
			// If the service hasn't returned the info yet, skip them
			else if (Friend->GetDisplayName().IsEmpty())
			{
				continue;
			}
			OutFriends.Add(Friend);
		}
		// Sort these by those playing the game first, alphabetically, then not playing, then not online
		OutFriends.Sort([](TSharedRef<FOnlineFriend> A, TSharedRef<FOnlineFriend> B)
		{
			const FOnlineUserPresence& APres = A->GetPresence();
			const FOnlineUserPresence& BPres = B->GetPresence();
			// If they are the same, then check playing this game
			if (APres.bIsOnline == BPres.bIsOnline)
			{
				// If they are the same, then sort by name
				if (APres.bIsPlayingThisGame == APres.bIsPlayingThisGame)
				{
					const EInviteStatus::Type AFriendStatus = A->GetInviteStatus();
					const EInviteStatus::Type BFriendStatus = B->GetInviteStatus();
					// Sort pending friends below accepted friends
					if (AFriendStatus == BFriendStatus && AFriendStatus == EInviteStatus::Accepted)
					{
						const FString& AName = A->GetDisplayName();
						const FString& BName = B->GetDisplayName();
						return AName < BName;
					}
				}
			}
			return false;
		});
		return true;
	}
	return false;
}

TSharedPtr<FOnlineFriend> FUserManagerEOS::GetFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	if (LocalUserNumToFriendsListMap.Contains(LocalUserNum))
	{
		FFriendsListEOSRef FriendsList = LocalUserNumToFriendsListMap[LocalUserNum];
		FUniqueNetIdEOS EosId(FriendId);
		FOnlineFriendEOSPtr FoundFriend = FriendsList->GetByNetIdString(EosId.UniqueNetIdStr);
		if (FoundFriend.IsValid())
		{
			const FOnlineUserPresence& Presence = FoundFriend->GetPresence();
			// See if they only want online only
			if (ListName == EFriendsLists::ToString(EFriendsLists::OnlinePlayers) && !Presence.bIsOnline)
			{
				return TSharedPtr<FOnlineFriend>();
			}
			// Of if they only want friends playing this game
			else if (ListName == EFriendsLists::ToString(EFriendsLists::InGamePlayers) && !Presence.bIsPlayingThisGame)
			{
				return TSharedPtr<FOnlineFriend>();
			}
			return FoundFriend;
		}
	}
	return TSharedPtr<FOnlineFriend>();
}

bool FUserManagerEOS::IsFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	return GetFriend(LocalUserNum, FriendId, ListName).IsValid();
}

bool FUserManagerEOS::QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace)
{
	return false;
}

bool FUserManagerEOS::GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace, TArray<TSharedRef<FOnlineRecentPlayer>>& OutRecentPlayers)
{
	OutRecentPlayers.Reset();
	FUniqueNetIdEOS EosId(UserId);
	if (NetIdStringToRecentPlayerListMap.Contains(EosId.UniqueNetIdStr))
	{
		FRecentPlayersListEOSRef List = NetIdStringToRecentPlayerListMap[EosId.UniqueNetIdStr];
		OutRecentPlayers.Append(List->GetList());
		return true;
	}
	return false;
}

bool FUserManagerEOS::BlockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId)
{
	return false;
}

bool FUserManagerEOS::UnblockPlayer(int32 LocalUserNum, const FUniqueNetId& PlayerId)
{
	return false;
}

bool FUserManagerEOS::QueryBlockedPlayers(const FUniqueNetId& UserId)
{
	return false;
}

bool FUserManagerEOS::GetBlockedPlayers(const FUniqueNetId& UserId, TArray<TSharedRef<FOnlineBlockedPlayer>>& OutBlockedPlayers)
{
	OutBlockedPlayers.Reset();
	FUniqueNetIdEOS EosId(UserId);
	if (NetIdStringToBlockedPlayerListMap.Contains(EosId.UniqueNetIdStr))
	{
		FBlockedPlayersListEOSRef List = NetIdStringToBlockedPlayerListMap[EosId.UniqueNetIdStr];
		OutBlockedPlayers.Append(List->GetList());
		return true;
	}
	return false;
}

void FUserManagerEOS::DumpBlockedPlayers() const
{
}

void FUserManagerEOS::DumpRecentPlayers() const
{
}

struct FPresenceStrings
{
	char Key[EOS_PRESENCE_DATA_MAX_KEY_LENGTH];
	char Value[EOS_PRESENCE_DATA_MAX_VALUE_LENGTH];
};

struct FRichTextOptions :
	public EOS_PresenceModification_SetRawRichTextOptions
{
	FRichTextOptions() :
		EOS_PresenceModification_SetRawRichTextOptions()
	{
		ApiVersion = EOS_PRESENCE_SETRAWRICHTEXT_API_LATEST;
		RichText = RichTextAnsi;
	}
	char RichTextAnsi[EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH];
};

typedef TEOSCallback<EOS_Presence_SetPresenceCompleteCallback, EOS_Presence_SetPresenceCallbackInfo> FSetPresenceCallback;

void FUserManagerEOS::SetPresence(const FUniqueNetId& UserId, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	FUniqueNetIdEOS EOSID(UserId);
	if (!StringToAccountIdMap.Contains(EOSID.UniqueNetIdStr))
	{
		UE_LOG_ONLINE(Error, TEXT("Can't SetPresence() for user (%s) since they are not logged in"), *EOSID.UniqueNetIdStr);
		return;
	}

	EOS_HPresenceModification ChangeHandle = nullptr;
	EOS_Presence_CreatePresenceModificationOptions Options = { };
	Options.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
	Options.LocalUserId = StringToAccountIdMap[EOSID.UniqueNetIdStr];
	EOS_Presence_CreatePresenceModification(EOSSubsystem->PresenceHandle, &Options, &ChangeHandle);
	if (ChangeHandle == nullptr)
	{
		UE_LOG_ONLINE(Error, TEXT("Failed to create a modification handle for setting presence"));
		return;
	}

	EOS_PresenceModification_SetStatusOptions StatusOptions = { };
	StatusOptions.ApiVersion = EOS_PRESENCE_SETSTATUS_API_LATEST;
	StatusOptions.Status = ToEOS_Presence_EStatus(Status.State);
	EOS_EResult SetStatusResult = EOS_PresenceModification_SetStatus(ChangeHandle, &StatusOptions);
	if (SetStatusResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("EOS_PresenceModification_SetStatus() failed with result code (%d)"), (int32)SetStatusResult);
	}

	// Convert the status string as the rich text string
	FRichTextOptions TextOptions;
	FCStringAnsi::Strncpy(TextOptions.RichTextAnsi, TCHAR_TO_UTF8(*Status.StatusStr), EOS_PRESENCE_RICH_TEXT_MAX_VALUE_LENGTH);
	EOS_EResult SetRichTextResult = EOS_PresenceModification_SetRawRichText(ChangeHandle, &TextOptions);
	if (SetRichTextResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("EOS_PresenceModification_SetRawRichText() failed with result code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(SetRichTextResult)));
	}

	FPresenceStrings RawStrings[EOS_PRESENCE_DATA_MAX_KEYS];
	TArray<EOS_Presence_DataRecord, TInlineAllocator<EOS_PRESENCE_DATA_MAX_KEYS>> Records;
	int32 CurrentIndex = 0;
	// Loop through the properties building records
	for (FPresenceProperties::TConstIterator It(Status.Properties); It && CurrentIndex < EOS_PRESENCE_DATA_MAX_KEYS; ++It, ++CurrentIndex)
	{
		// Since the TCHAR_TO_UTF8 macros are scoped, we need to copy to a chunk of memory while building the data up
		FCStringAnsi::Strncpy(RawStrings[CurrentIndex].Key, TCHAR_TO_UTF8(*It.Key()), EOS_PRESENCE_DATA_MAX_KEY_LENGTH);
		FCStringAnsi::Strncpy(RawStrings[CurrentIndex].Value, TCHAR_TO_UTF8(*It.Value().ToString()), EOS_PRESENCE_DATA_MAX_VALUE_LENGTH);

		EOS_Presence_DataRecord Record;
		Record.ApiVersion = EOS_PRESENCE_DATARECORD_API_LATEST;
		Record.Key = RawStrings[CurrentIndex].Key;
		Record.Value = RawStrings[CurrentIndex].Value;
		Records.Add(Record);
	}
	EOS_PresenceModification_SetDataOptions DataOptions = { };
	DataOptions.ApiVersion = EOS_PRESENCE_SETDATA_API_LATEST;
	DataOptions.RecordsCount = Records.Num();
	DataOptions.Records = Records.GetData();
	EOS_EResult SetDataResult = EOS_PresenceModification_SetData(ChangeHandle, &DataOptions);
	if (SetDataResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Error, TEXT("EOS_PresenceModification_SetData() failed with result code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(SetDataResult)));
	}

	FSetPresenceCallback* CallbackObj = new FSetPresenceCallback();
	CallbackObj->CallbackLambda = [this, Delegate](const EOS_Presence_SetPresenceCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success && AccountIdToStringMap.Contains(Data->LocalUserId))
		{
			FUniqueNetIdEOS EOSID(AccountIdToStringMap[Data->LocalUserId]);
			Delegate.ExecuteIfBound(EOSID, true);
			return;
		}
		UE_LOG_ONLINE(Error, TEXT("SetPresence() failed with result code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		Delegate.ExecuteIfBound(FUniqueNetIdEOS(), false);
	};

	EOS_Presence_SetPresenceOptions PresOptions = { };
	PresOptions.ApiVersion = EOS_PRESENCE_SETPRESENCE_API_LATEST;
	PresOptions.LocalUserId = StringToAccountIdMap[EOSID.UniqueNetIdStr];
	PresOptions.PresenceModificationHandle = ChangeHandle;
	// Last step commit the changes
	EOS_Presence_SetPresence(EOSSubsystem->PresenceHandle, &PresOptions, CallbackObj, CallbackObj->GetCallbackPtr());
	EOS_PresenceModification_Release(ChangeHandle);
}

typedef TEOSCallback<EOS_Presence_OnQueryPresenceCompleteCallback, EOS_Presence_QueryPresenceCallbackInfo> FQueryPresenceCallback;

void FUserManagerEOS::QueryPresence(const FUniqueNetId& UserId, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	if (DefaultLocalUser < 0)
	{
		UE_LOG_ONLINE(Error, TEXT("Can't QueryPresence() due to no users being signed in"));
		Delegate.ExecuteIfBound(UserId, false);
		return;
	}

	FUniqueNetIdEOS EOSID(UserId);
	const FString& NetId = EOSID.UniqueNetIdStr;
	if (!StringToAccountIdMap.Contains(NetId))
	{
		UE_LOG_ONLINE(Error, TEXT("Can't QueryPresence(%s) for unknown unique net id"), *NetId);
		Delegate.ExecuteIfBound(UserId, false);
		return;
	}

	EOS_Presence_HasPresenceOptions HasOptions = { };
	HasOptions.ApiVersion = EOS_PRESENCE_HASPRESENCE_API_LATEST;
	HasOptions.LocalUserId = UserNumToAccountIdMap[DefaultLocalUser];
	HasOptions.TargetUserId = StringToAccountIdMap[NetId];
	EOS_Bool bHasPresence = EOS_Presence_HasPresence(EOSSubsystem->PresenceHandle, &HasOptions);
	if (bHasPresence == EOS_FALSE)
	{
		FQueryPresenceCallback* CallbackObj = new FQueryPresenceCallback();
		CallbackObj->CallbackLambda = [this, Delegate](const EOS_Presence_QueryPresenceCallbackInfo* Data)
		{
			if (Data->ResultCode == EOS_EResult::EOS_Success && EpicAccountIdToOnlineUserMap.Contains(Data->TargetUserId))
			{
				// Update the presence data to the most recent
				UpdatePresence(Data->TargetUserId);
				FOnlineUserPtr OnlineUser = EpicAccountIdToOnlineUserMap[Data->TargetUserId];
				Delegate.ExecuteIfBound(*OnlineUser->GetUserId(), true);
				return;
			}
			const FString& TargetUser = MakeNetIdStringFromIds(Data->TargetUserId, nullptr);
			UE_LOG_ONLINE(Error, TEXT("QueryPresence() for user (%s) failed with result code (%s)"), *TargetUser, ANSI_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
			Delegate.ExecuteIfBound(FUniqueNetIdEOS(), false);
		};

		// Query for updated presence
		EOS_Presence_QueryPresenceOptions Options = { };
		Options.ApiVersion = EOS_PRESENCE_QUERYPRESENCE_API_LATEST;
		Options.LocalUserId = HasOptions.LocalUserId;
		Options.TargetUserId = HasOptions.TargetUserId;
		EOS_Presence_QueryPresence(EOSSubsystem->PresenceHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
		return;
	}

	// Update the presence data to the most recent
	UpdatePresence(HasOptions.TargetUserId);
	// It's already present so trigger that it's done
	Delegate.ExecuteIfBound(UserId, true);
}

void FUserManagerEOS::UpdatePresence(EOS_EpicAccountId AccountId)
{
	EOS_Presence_Info* PresenceInfo = nullptr;
	EOS_Presence_CopyPresenceOptions Options = { };
	Options.ApiVersion = EOS_PRESENCE_COPYPRESENCE_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[DefaultLocalUser];
	Options.TargetUserId = AccountId;
	EOS_EResult CopyResult = EOS_Presence_CopyPresence(EOSSubsystem->PresenceHandle, &Options, &PresenceInfo);
	if (CopyResult == EOS_EResult::EOS_Success)
	{
		const FString& NetId = AccountIdToStringMap[AccountId];
		// Create it on demand if we don't have one yet
		if (!NetIdStringToOnlineUserPresenceMap.Contains(NetId))
		{
			FOnlineUserPresenceRef PresenceRef = MakeShareable(new FOnlineUserPresence());
			NetIdStringToOnlineUserPresenceMap.Add(NetId, PresenceRef);
		}

		FOnlineUserPresenceRef PresenceRef = NetIdStringToOnlineUserPresenceMap[NetId];
		FString ProductId(UTF8_TO_TCHAR(PresenceInfo->ProductId));
		FString ProdVersion(UTF8_TO_TCHAR(PresenceInfo->ProductVersion));
		FString Platform(UTF8_TO_TCHAR(PresenceInfo->Platform));
		// Convert the presence data to our format
		PresenceRef->Status.State = ToEOnlinePresenceState(PresenceInfo->Status);
		PresenceRef->Status.StatusStr = PresenceInfo->RichText;
		PresenceRef->bIsOnline = PresenceRef->Status.State == EOnlinePresenceState::Online;
		PresenceRef->bIsPlaying = !ProductId.IsEmpty();
		PresenceRef->bIsPlayingThisGame = FCStringAnsi::Strcmp(PresenceInfo->ProductId, EOSSubsystem->ProductNameAnsi) == 0 &&
			FCStringAnsi::Strcmp(PresenceInfo->ProductVersion, EOSSubsystem->ProductVersionAnsi) == 0;
//		PresenceRef->bIsJoinable = ???;
//		PresenceRef->bHasVoiceSupport = ???;
		PresenceRef->Status.Properties.Add(TEXT("ProductId"), ProductId);
		PresenceRef->Status.Properties.Add(TEXT("ProductVersion"), ProdVersion);
		PresenceRef->Status.Properties.Add(TEXT("Platform"), Platform);
		for (int32 Index = 0; Index < PresenceInfo->RecordsCount; Index++)
		{
			const EOS_Presence_DataRecord& Record = PresenceInfo->Records[Index];
			PresenceRef->Status.Properties.Add(Record.Key, UTF8_TO_TCHAR(Record.Value));
		}

		// Copy the presence if this is a friend that was updated, so that their data is in sync
		UpdateFriendPresence(NetId, PresenceRef);

		EOS_Presence_Info_Release(PresenceInfo);
	}
	else
	{
		UE_LOG_ONLINE(Error, TEXT("Failed to copy presence data with error code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(CopyResult)));
	}
}

void FUserManagerEOS::UpdateFriendPresence(const FString& FriendId, FOnlineUserPresenceRef Presence)
{
	for (TMap<int32, FFriendsListEOSRef>::TConstIterator It(LocalUserNumToFriendsListMap); It; ++It)
	{
		FFriendsListEOSRef FriendsList = It.Value();
		FOnlineFriendEOSPtr Friend = FriendsList->GetByNetIdString(FriendId);
		if (Friend.IsValid())
		{
			Friend->SetPresence(Presence);
		}
	}
}

EOnlineCachedResult::Type FUserManagerEOS::GetCachedPresence(const FUniqueNetId& UserId, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	FUniqueNetIdEOS EOSID(UserId);
	if (NetIdStringToOnlineUserPresenceMap.Contains(EOSID.UniqueNetIdStr))
	{
		OutPresence = NetIdStringToOnlineUserPresenceMap[EOSID.UniqueNetIdStr];
		return EOnlineCachedResult::Success;
	}
	return EOnlineCachedResult::NotFound;
}

EOnlineCachedResult::Type FUserManagerEOS::GetCachedPresenceForApp(const FUniqueNetId&, const FUniqueNetId& UserId, const FString&, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	return GetCachedPresence(UserId, OutPresence);
}

bool FUserManagerEOS::QueryUserInfo(int32 LocalUserNum, const TArray<TSharedRef<const FUniqueNetId>>& UserIds)
{
	// Trigger a query for each user in the list
	for (FUniqueNetIdRef NetId : UserIds)
	{
		FUniqueNetIdEOS EOSID(*NetId);
		// Skip querying for local users since we already have that data
		if (StringToUserAccountMap.Contains(EOSID.UniqueNetIdStr))
		{
			continue;
		}
		// Check to see if we know about this user or not
		if (StringToAccountIdMap.Contains(EOSID.UniqueNetIdStr))
		{
			EOS_EpicAccountId AccountId = StringToAccountIdMap[EOSID.UniqueNetIdStr];
			ReadUserInfo(AccountId);
		}
		else
		{
			// We need to build this one from the string
			EOS_EpicAccountId AccountId = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*EOSID.EpicAccountIdStr));
			if (EOS_EpicAccountId_IsValid(AccountId) == EOS_TRUE)
			{
				// Registering the player will also query the user info data
				AddRemotePlayer(EOSID.UniqueNetIdStr, AccountId);
			}
		}
	}
	return true;
}

typedef TEOSCallback<EOS_UserInfo_OnQueryUserInfoCallback, EOS_UserInfo_QueryUserInfoCallbackInfo> FReadUserInfoCallback;

void FUserManagerEOS::ReadUserInfo(EOS_EpicAccountId EpicAccountId)
{
	FReadUserInfoCallback* CallbackObj = new FReadUserInfoCallback();
	CallbackObj->CallbackLambda = [this](const EOS_UserInfo_QueryUserInfoCallbackInfo* Data)
	{
		if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			IAttributeAccessInterfaceRef AttributeAccessRef = EpicAccountIdToAttributeAccessMap[Data->TargetUserId];
			UpdateUserInfo(AttributeAccessRef, Data->LocalUserId, Data->TargetUserId);
		}
	};

	EOS_UserInfo_QueryUserInfoOptions Options = { };
	Options.ApiVersion = EOS_USERINFO_QUERYUSERINFO_API_LATEST;
	Options.LocalUserId = UserNumToAccountIdMap[DefaultLocalUser];
	Options.TargetUserId = EpicAccountId;
	EOS_UserInfo_QueryUserInfo(EOSSubsystem->UserInfoHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
}

bool FUserManagerEOS::GetAllUserInfo(int32 LocalUserNum, TArray<TSharedRef<FOnlineUser>>& OutUsers)
{
	OutUsers.Reset();
	// Get remote users
	for (TMap<FString, FOnlineUserPtr>::TConstIterator It(NetIdStringToOnlineUserMap); It; ++It)
	{
		if (It.Value().IsValid())
		{
			OutUsers.Add(It.Value().ToSharedRef());
		}
	}
	// Get local users
	for (TMap<FString, FUserOnlineAccountEOSRef>::TConstIterator It(StringToUserAccountMap); It; ++It)
	{
		OutUsers.Add(It.Value());
	}
	return true;
}

TSharedPtr<FOnlineUser> FUserManagerEOS::GetUserInfo(int32 LocalUserNum, const class FUniqueNetId& UserId)
{
	TSharedPtr<FOnlineUser> OnlineUser;
	FUniqueNetIdEOS EOSID(UserId);
	if (NetIdStringToOnlineUserMap.Contains(EOSID.UniqueNetIdStr))
	{
		OnlineUser = NetIdStringToOnlineUserMap[EOSID.UniqueNetIdStr];
	}
	return OnlineUser;
}

struct FQueryByDisplayNameOptions :
	public EOS_UserInfo_QueryUserInfoByDisplayNameOptions
{
	FQueryByDisplayNameOptions() :
		EOS_UserInfo_QueryUserInfoByDisplayNameOptions()
	{
		ApiVersion = EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST;
		DisplayName = DisplayNameAnsi;
	}
	char DisplayNameAnsi[EOS_OSS_STRING_BUFFER_LENGTH];
};

typedef TEOSCallback<EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback, EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo> FQueryInfoByNameCallback;

bool FUserManagerEOS::QueryUserIdMapping(const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FOnQueryUserMappingComplete& Delegate)
{
	FUniqueNetIdEOS EOSID(UserId);
	const FString& NetId = EOSID.UniqueNetIdStr;
	if (!StringToAccountIdMap.Contains(NetId))
	{
		UE_LOG_ONLINE(Error, TEXT("Specified local user (%s) is not known"), *EOSID.UniqueNetIdStr);
		Delegate.ExecuteIfBound(false, UserId, DisplayNameOrEmail, FUniqueNetIdEOS(), FString::Printf(TEXT("Specified local user (%s) is not known"), *EOSID.UniqueNetIdStr));
		return false;
	}
	int32 LocalUserNum = GetLocalUserNumFromUniqueNetId(UserId);

	FQueryInfoByNameCallback* CallbackObj = new FQueryInfoByNameCallback();
	CallbackObj->CallbackLambda = [LocalUserNum, DisplayNameOrEmail, this, Delegate](const EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo* Data)
	{
		EOS_EResult Result = Data->ResultCode;
		if (GetLoginStatus(LocalUserNum) != ELoginStatus::LoggedIn)
		{
			// Handle the user logging out while a read is in progress
			Result = EOS_EResult::EOS_InvalidUser;
		}

		FString ErrorString;
		bool bWasSuccessful = Result == EOS_EResult::EOS_Success;
		if (bWasSuccessful)
		{
			const FString& NetIdStr = MakeStringFromEpicAccountId(Data->TargetUserId);
			FUniqueNetIdEOSPtr LocalUserId = UserNumToNetIdMap[DefaultLocalUser];
			if (!EpicAccountIdToOnlineUserMap.Contains(Data->TargetUserId))
			{
				// Registering the player will also query the presence/user info data
				AddRemotePlayer(NetIdStr, Data->TargetUserId);
			}

			Delegate.ExecuteIfBound(true, *LocalUserId, DisplayNameOrEmail, FUniqueNetIdEOS(NetIdStr), ErrorString);
		}
		else
		{
			ErrorString = FString::Printf(TEXT("QueryUserIdMapping(%d, '%s') failed with EOS result code (%s)"), DefaultLocalUser, *DisplayNameOrEmail, ANSI_TO_TCHAR(EOS_EResult_ToString(Result)));
		}
		Delegate.ExecuteIfBound(false, FUniqueNetIdEOS(), DisplayNameOrEmail, FUniqueNetIdEOS(), ErrorString);
	};

	FQueryByDisplayNameOptions Options;
	FCStringAnsi::Strncpy(Options.DisplayNameAnsi, TCHAR_TO_UTF8(*DisplayNameOrEmail), EOS_PRODUCTNAME_MAX_BUFFER_LEN);
	Options.LocalUserId = StringToAccountIdMap[NetId];
	EOS_UserInfo_QueryUserInfoByDisplayName(EOSSubsystem->UserInfoHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());

	return true;
}

struct FQueryByStringIdsOptions :
	public EOS_Connect_QueryExternalAccountMappingsOptions
{
	FQueryByStringIdsOptions(const uint32 InNumStringIds, EOS_ProductUserId InLocalUserId) :
		EOS_Connect_QueryExternalAccountMappingsOptions()
	{
		PointerArray.AddZeroed(InNumStringIds);
		for (int32 Index = 0; Index < PointerArray.Num(); Index++)
		{
			PointerArray[Index] = new char[EOS_CONNECT_EXTERNAL_ACCOUNT_ID_MAX_LENGTH];
		}
		ApiVersion = EOS_CONNECT_QUERYEXTERNALACCOUNTMAPPINGS_API_LATEST;
		AccountIdType = EOS_EExternalAccountType::EOS_EAT_EPIC;
		ExternalAccountIds = (const char**)PointerArray.GetData();
		ExternalAccountIdCount = InNumStringIds;
		LocalUserId = InLocalUserId;
	}

	~FQueryByStringIdsOptions()
	{
		for (int32 Index = 0; Index < PointerArray.Num(); Index++)
		{
			delete [] PointerArray[Index];
		}
	}
	TArray<char*> PointerArray;
};

struct FGetAccountMappingOptions :
	public EOS_Connect_GetExternalAccountMappingsOptions
{
	FGetAccountMappingOptions() :
		EOS_Connect_GetExternalAccountMappingsOptions()
	{
		ApiVersion = EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST;
		AccountIdType = EOS_EExternalAccountType::EOS_EAT_EPIC;
		TargetExternalUserId = AccountId;
	}
	char AccountId[EOS_CONNECT_EXTERNAL_ACCOUNT_ID_MAX_LENGTH];
};

typedef TEOSCallback<EOS_Connect_OnQueryExternalAccountMappingsCallback, EOS_Connect_QueryExternalAccountMappingsCallbackInfo> FQueryByStringIdsCallback;

bool FUserManagerEOS::QueryExternalIdMappings(const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FOnQueryExternalIdMappingsComplete& Delegate)
{
	FUniqueNetIdEOS EOSID(UserId);
	if (!StringToProductUserIdMap.Contains(EOSID.UniqueNetIdStr))
	{
		Delegate.ExecuteIfBound(false, UserId, QueryOptions, ExternalIds, FString::Printf(TEXT("User (%s) is not logged in, so can't query external account ids"), *EOSID.UniqueNetIdStr));
		return false;
	}
	int32 LocalUserNum = GetLocalUserNumFromUniqueNetId(UserId);

	EOS_ProductUserId LocalUserId = StringToProductUserIdMap[EOSID.UniqueNetIdStr];
	const int32 NumBatches = (ExternalIds.Num() / EOS_CONNECT_QUERYEXTERNALACCOUNTMAPPINGS_MAX_ACCOUNT_IDS) + 1;
	int32 QueryStart = 0;
	// Process queries in batches since there's a max that can be done at once
	for (int32 BatchCount = 0; BatchCount < NumBatches; BatchCount++)
	{
		const uint32 AmountToProcess = FMath::Min(ExternalIds.Num() - QueryStart, EOS_CONNECT_QUERYEXTERNALACCOUNTMAPPINGS_MAX_ACCOUNT_IDS);
		TArray<FString> BatchIds;
		BatchIds.Empty(AmountToProcess);
		FQueryByStringIdsOptions Options(AmountToProcess, LocalUserId);
		// Build an options up per batch
		for (uint32 ProcessedCount = 0; ProcessedCount < AmountToProcess; ProcessedCount++, QueryStart++)
		{
			FCStringAnsi::Strncpy(Options.PointerArray[ProcessedCount], TCHAR_TO_UTF8(*ExternalIds[ProcessedCount]), EOS_CONNECT_EXTERNAL_ACCOUNT_ID_MAX_LENGTH);
			BatchIds.Add(ExternalIds[ProcessedCount]);
		}
		FQueryByStringIdsCallback* CallbackObj = new FQueryByStringIdsCallback();
		CallbackObj->CallbackLambda = [LocalUserNum, QueryOptions, BatchIds, this, Delegate](const EOS_Connect_QueryExternalAccountMappingsCallbackInfo* Data)
		{
			EOS_EResult Result = Data->ResultCode;
			if (GetLoginStatus(LocalUserNum) != ELoginStatus::LoggedIn)
			{
				// Handle the user logging out while a read is in progress
				Result = EOS_EResult::EOS_InvalidUser;
			}

			FString ErrorString;
			FUniqueNetIdEOS EOSID;
			if (Result == EOS_EResult::EOS_Success)
			{
				EOSID = *UserNumToNetIdMap[LocalUserNum];

				FGetAccountMappingOptions Options;
				Options.LocalUserId = UserNumToProductUserIdMap[DefaultLocalUser];
				// Get the product id for each epic account passed in
				for (const FString& StringId : BatchIds)
				{
					FCStringAnsi::Strncpy(Options.AccountId, TCHAR_TO_UTF8(*StringId), EOS_CONNECT_EXTERNAL_ACCOUNT_ID_MAX_LENGTH);
					EOS_ProductUserId ProductUserId = EOS_Connect_GetExternalAccountMapping(EOSSubsystem->ConnectHandle, &Options);
					if (EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE)
					{
						EOS_EpicAccountId AccountId = EOS_EpicAccountId_FromString(Options.AccountId);
						UpdateRemotePlayerProductUserId(AccountId, ProductUserId);
					}
				}
			}
			else
			{
				ErrorString = FString::Printf(TEXT("EOS_Connect_QueryExternalAccountMappings() failed with result code (%s)"), ANSI_TO_TCHAR(EOS_EResult_ToString(Result)));
			}
			Delegate.ExecuteIfBound(false, EOSID, QueryOptions, BatchIds, ErrorString);
		};

		EOS_Connect_QueryExternalAccountMappings(EOSSubsystem->ConnectHandle, &Options, CallbackObj, CallbackObj->GetCallbackPtr());
	}
	return true;
}

void FUserManagerEOS::GetExternalIdMappings(const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, TArray<TSharedPtr<const FUniqueNetId>>& OutIds)
{
	OutIds.Reset();
	for (const FString& AccountIdStr : ExternalIds)
	{
		OutIds.Add(GetExternalIdMapping(QueryOptions, AccountIdStr));
	}
}

TSharedPtr<const FUniqueNetId> FUserManagerEOS::GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId)
{
	TSharedPtr<const FUniqueNetId> NetId;
	EOS_EpicAccountId AccountId = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*ExternalId));
	if (EOS_EpicAccountId_IsValid(AccountId) == EOS_TRUE && AccountIdToStringMap.Contains(AccountId))
	{
		const FString& NetIdStr = AccountIdToStringMap[AccountId];
		NetId = NetIdStringToOnlineUserMap[NetIdStr]->GetUserId();
	}
	return NetId;
}

#endif