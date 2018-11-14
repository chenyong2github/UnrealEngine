// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "OnlineSubsystemTypes.h"
#include "OnlineDelegateMacros.h"

struct FOnlineError;

ONLINESUBSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogOnlineIdentity, Log, All);

#define UE_LOG_ONLINE_IDENTITY(Verbosity, Format, ...) \
{ \
	UE_LOG(LogOnlineIdentity, Verbosity, TEXT("%s%s"), ONLINE_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

#define UE_CLOG_ONLINE_IDENTITY(Conditional, Verbosity, Format, ...) \
{ \
	UE_CLOG(Conditional, LogOnlineIdentity, Verbosity, TEXT("%s%s"), ONLINE_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

/**
 * Account credentials needed to sign in to an online service
 */
class FOnlineAccountCredentials
{
public:

	/** Type of account. Needed to identity the auth method to use (epic, internal, facebook, etc) */
	FString Type;
	/** Id of the user logging in (email, display name, facebook id, etc) */
	FString Id;
	/** Credentials of the user logging in (password or auth token) */
	FString Token;

	/**
	 * Equality operator
	 */
	bool operator==(const FOnlineAccountCredentials& Other) const
	{
		return Other.Type == Type && 
			Other.Id == Id;
	}

	/**
	 * Constructor
	 */
	FOnlineAccountCredentials(const FString& InType, const FString& InId, const FString& InToken) :
		Type(InType),
		Id(InId),
		Token(InToken)
	{
	}

	/**
	 * Constructor
	 */
	FOnlineAccountCredentials()
	{
	}

	FString ToDebugString() const
	{
		return FString::Printf(TEXT("{Id: %s, Token: %s, Type: %s}"), *Id, OSS_REDACT(*Token), *Type);
	}
};

namespace EUserPrivileges
{
	enum Type
	{
		/** Whether the user can play at all, online or offline - may be age restricted */
		CanPlay,
		/** Whether the user can play in online modes */
		CanPlayOnline,
		/** Whether the user can use voice and text chat */
		CanCommunicateOnline,
		/** Whether the user can use content generated by other users */
		CanUseUserGeneratedContent,
		/** Whether the user can ever participate in cross-play due to age restrictions */
		CanUserCrossPlay
	};
}

/**
 * Delegate called when a player logs in/out
 *
 * @param LocalUserNum the controller number of the associated user
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoginChanged, int32 /*LocalUserNum*/);
typedef FOnLoginChanged::FDelegate FOnLoginChangedDelegate;

/**
 * Delegate called when a player's status changes but doesn't change profiles
 *
 * @param LocalUserNum the controller number of the associated user
 * @param OldStatus the old login status for the user
 * @param NewStatus the new login status for the user
 * @param NewId the new id to associate with the user
 */
DECLARE_MULTICAST_DELEGATE_FourParams(FOnLoginStatusChanged, int32 /*LocalUserNum*/, ELoginStatus::Type /*OldStatus*/, ELoginStatus::Type /*NewStatus*/, const FUniqueNetId& /*NewId*/);
typedef FOnLoginStatusChanged::FDelegate FOnLoginStatusChangedDelegate;

/**
 * Delegate called when a controller-user pairing changes
 *
 * @param LocalUserNum the controller number of the controller whose pairing changed
 * @param PreviousUser the user that used to be paired with this controller
 * @param NewUser the user that is currently paired with this controller
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnControllerPairingChanged, int /*LocalUserNum*/, const FUniqueNetId& /*PreviousUser*/, const FUniqueNetId& /*NewUser*/);
typedef FOnControllerPairingChanged::FDelegate FOnControllerPairingChangedDelegate;

/**
 * Called when user account login has completed after calling Login() or AutoLogin()
 *
 * @param LocalUserNum the controller number of the associated user
 * @param bWasSuccessful true if server was contacted and a valid result received
 * @param UserId the user id received from the server on successful login
 * @param Error string representing the error condition
 */
DECLARE_MULTICAST_DELEGATE_FourParams(FOnLoginComplete, int32 /*LocalUserNum*/, bool /*bWasSuccessful*/, const FUniqueNetId& /*UserId*/, const FString& /*Error*/);
typedef FOnLoginComplete::FDelegate FOnLoginCompleteDelegate;


/**
 * Delegate used in notifying the UI/game that the manual logout completed
 *
 * @param LocalUserNum the controller number of the associated user
 * @param bWasSuccessful whether the async call completed properly or not
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLogoutComplete, int32 /*LocalUserNum*/, bool /*bWasSuccessful*/);
typedef FOnLogoutComplete::FDelegate FOnLogoutCompleteDelegate;

/**
 * Delegate called when logout requires login flow to cleanup
 *
 * @param LoginDomains the login domains to clean up
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoginFlowLogout, const TArray<FString>& /*LoginDomains*/);
typedef FOnLoginFlowLogout::FDelegate FOnLoginFlowLogoutDelegate;

/**
 * Delegate executed when we get a user privilege result.
 *
 * @param UserId The unique id of the user who was queried
 * @param OnlineError the result of the operation
 */
DECLARE_DELEGATE_TwoParams(FOnRevokeAuthTokenCompleteDelegate, const FUniqueNetId&, const FOnlineError&);

/**
 * Interface for registration/authentication of user identities
 */
class IOnlineIdentity
{

protected:
	IOnlineIdentity() {};	

public:

	enum class EPrivilegeResults : uint32
	{
		/** The user has the requested privilege */
		NoFailures				=	0,
		/** Patch required before the user can use the privilege */
		RequiredPatchAvailable	=	1 << 0,
		/** System update required before the user can use the privilege */
		RequiredSystemUpdate	=	1 << 1,
		/** Parental control failure usually */
		AgeRestrictionFailure	=	1 << 2,
		/** XboxLive Gold / PSPlus required but not available */
		AccountTypeFailure		=	1 << 3,
		/** Invalid user */
		UserNotFound			=	1 << 4,
		/** User must be logged in */
		UserNotLoggedIn			=	1 << 5,
		/** User restricted from chat */
		ChatRestriction			=	1 << 6,
		/** User restricted from User Generated Content */
		UGCRestriction			=	1 << 7,
		/** Platform failed for unknown reason and handles its own dialogs */
		GenericFailure			=	1 << 8,
		/** Online play is restricted */
		OnlinePlayRestricted	=	1 << 9,
		/** Check failed because network is unavailable */
		NetworkConnectionUnavailable =	1 << 10,
	};

	virtual ~IOnlineIdentity() {};

	/**
	 * Delegate called when a player logs in/out
	 *
	 * @param LocalUserNum the player that logged in/out
	 */
	DEFINE_ONLINE_DELEGATE_ONE_PARAM(OnLoginChanged, int32);

	/**
	 * Delegate called when a player's login status changes but doesn't change identity
	 *
	 * @param LocalUserNum the controller number of the associated user
	 * @param OldStatus the previous status of the user
	 * @param NewStatus the new login status for the user
	 * @param NewId the new id to associate with the user
	 */
	DEFINE_ONLINE_PLAYER_DELEGATE_THREE_PARAM(MAX_LOCAL_PLAYERS, OnLoginStatusChanged, ELoginStatus::Type /*Previous*/, ELoginStatus::Type /*Current*/, const FUniqueNetId&);

	/**
	 * Delegate called when a controller-user pairing changes
	 *
	 * @param LocalUserNum the controller number of the associated user
	 * @param PreviousUser the new login status for the user
	 * @param NewUser the new id to associate with the user
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnControllerPairingChanged, int, const FUniqueNetId&, const FUniqueNetId&);

	/**
	 * Login/Authenticate with user credentials.
	 * Will call OnLoginComplete() delegate when async task completes
	 *
	 * @param LocalUserNum the controller number of the associated user
	 * @param AccountCredentials user account credentials needed to sign in to the online service
	 *
	 * @return true if login task was started
	 */
	virtual bool Login(int32 LocalUserNum, const FOnlineAccountCredentials& AccountCredentials) = 0;

	/**
	 * Called when user account login has completed after calling Login() or AutoLogin()
	 *
	 * @param LocalUserNum the controller number of the associated user
	 * @param bWasSuccessful true if server was contacted and a valid result received
	 * @param UserId the user id received from the server on successful login
	 * @param Error string representing the error condition
	 */
	DEFINE_ONLINE_PLAYER_DELEGATE_THREE_PARAM(MAX_LOCAL_PLAYERS, OnLoginComplete, bool, const FUniqueNetId&, const FString&);

	/**
	 * Signs the player out of the online service
	 * Will call OnLogoutComplete() delegate when async task completes
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return true if logout task was started
	 */
	virtual bool Logout(int32 LocalUserNum) = 0;

	/**
	 * Delegate used in notifying the that manual logout completed
	 *
	 * @param LocalUserNum the controller number of the associated user
	 * @param bWasSuccessful whether the async call completed properly or not
	 */
	DEFINE_ONLINE_PLAYER_DELEGATE_ONE_PARAM(MAX_LOCAL_PLAYERS, OnLogoutComplete, bool);

	/**
	 * Delegate called when the online subsystem requires the login flow to logout and cleanup
	 * @param LoginDomains login domains to be cleaned up
	 */
	DEFINE_ONLINE_DELEGATE_ONE_PARAM(OnLoginFlowLogout, const TArray<FString>& /*LoginDomains*/);

	/**
	 * Logs the player into the online service using parameters passed on the
	 * command line. Expects -AUTH_LOGIN=<UserName> -AUTH_PASSWORD=<password>. If either
	 * are missing, the function returns false and doesn't start the login
	 * process
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return true if the async call started ok, false otherwise
	 */
	virtual bool AutoLogin(int32 LocalUserNum) = 0;

	/**
	 * Obtain online account info for a user that has been registered
	 *
	 * @param UserId user to search for
	 *
	 * @return info about the user if found, NULL otherwise
	 */
	virtual TSharedPtr<FUserOnlineAccount> GetUserAccount(const FUniqueNetId& UserId) const = 0;

	/**
	 * Obtain list of all known/registered user accounts
	 *
	 * @return info about the user if found, NULL otherwise
	 */
	virtual TArray<TSharedPtr<FUserOnlineAccount> > GetAllUserAccounts() const = 0;

	/**
	 * Gets the platform specific unique id for the specified player
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return Valid player id object if the call succeeded, NULL otherwise
	 */
	virtual TSharedPtr<const FUniqueNetId> GetUniquePlayerId(int32 LocalUserNum) const = 0;

	/**
	 * Gets the platform specific unique id for the sponsor of the specified player
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return Valid player id object if the sponsor exists, NULL otherwise
	 */
	virtual TSharedPtr<const FUniqueNetId> GetSponsorUniquePlayerId(int32 LocalUserNum) const { return nullptr; }

	/**
	 * Create a unique id from binary data (used during replication)
	 * 
	 * @param Bytes opaque data from appropriate platform
	 * @param Size size of opaque data
	 *
	 * @return UniqueId from the given data, NULL otherwise
	 */
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(uint8* Bytes, int32 Size) = 0;

	/**
	 * Create a unique id from string
	 * 
	 * @param Str string holding textual representation of an Id
	 *
	 * @return UniqueId from the given data, NULL otherwise
	 */
	virtual TSharedPtr<const FUniqueNetId> CreateUniquePlayerId(const FString& Str) = 0;

	/**
	 * Fetches the login status for a given player
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return the enum value of their status
	 */
	virtual ELoginStatus::Type GetLoginStatus(int32 LocalUserNum) const = 0;

	/**
	 * Fetches the login status for a given player
	 *
	 * @param UserId the unique net id of the associated user
	 *
	 * @return the enum value of their status
	 */
	virtual ELoginStatus::Type GetLoginStatus(const FUniqueNetId& UserId) const = 0;

	/**
	 * Reads the player's nick name from the online service
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return a string containing the players nick name
	 */
	//@todo - move to user interface
	virtual FString GetPlayerNickname(int32 LocalUserNum) const = 0;

	/**
	 * Reads the player's nick name from the online service
	 *
	 * @param UserId the unique net of the associated user
	 *
	 * @return a string containing the players nick name
	 */
	//@todo - move to user interface
	virtual FString GetPlayerNickname(const FUniqueNetId& UserId) const = 0;

	/**
	 * Gets a user's platform specific authentication token to verify their identity
	 *
	 * @param LocalUserNum the controller number of the associated user
	 *
	 * @return String representing the authentication token
	 */
	//@todo - remove and use GetUserAccount instead
	virtual FString GetAuthToken(int32 LocalUserNum) const = 0;

	/**
	 * Revoke the user's registered auth token.
	 *
	 * @param UserId the unique net of the associated user
	 * @param Delegate delegate to execute when the async task completes
	 */
	virtual void RevokeAuthToken(const FUniqueNetId& UserId, const FOnRevokeAuthTokenCompleteDelegate& Delegate) = 0;

	/**
	 * Delegate executed when we get a user privilege result.
	 *
	 * @param UniqueId The unique id of the user who was queried
	 * @param Privilege the privilege that was queried
	 * @param PrivilegeResult bitwise OR of any privilege failures. 0 is success.
	 */
	DECLARE_DELEGATE_ThreeParams(FOnGetUserPrivilegeCompleteDelegate, const FUniqueNetId&, EUserPrivileges::Type, uint32);

	/**
	 * Gets the status of a user's privilege.
	 *
	 * @param UserId the unique id of the user to query
	 * @param Privilege the privilege you want to know about
	 * @param Delegate delegate to execute when the async task completes
	 */
	virtual void GetUserPrivilege(const FUniqueNetId& UserId, EUserPrivileges::Type Privilege, const FOnGetUserPrivilegeCompleteDelegate& Delegate) = 0;

	/**
	 * Temporary hack to get a corresponding FUniqueNetId from a PlatformUserId
	 *
	 * @param UniqueNetId The unique id to look up
	 * @return The corresponding id or PLATFORMID_NONE if not found
	 */
	virtual FPlatformUserId GetPlatformUserIdFromUniqueNetId(const FUniqueNetId& UniqueNetId) const = 0;

	/**
	 * Get the auth type associated with accounts for this platform
	 *
	 * @return The auth type associated with accounts for this platform
	 */
	virtual FString GetAuthType() const = 0;
};

typedef TSharedPtr<IOnlineIdentity, ESPMode::ThreadSafe> IOnlineIdentityPtr;
