// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemTypes.h"

#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Interfaces/OnlineUserInterface.h"

#include "OnlineSubsystemEOSPackage.h"

#if WITH_EOS_SDK
	#include "eos_common.h"
#endif

// Expect URLs to look like "EOS:PUID:SocketName:Channel"
#define EOS_CONNECTION_URL_PREFIX TEXT("EOS")
#define EOS_URL_SEPARATOR TEXT(":")

#define EOS_OSS_STRING_BUFFER_LENGTH 256

class FOnlineSubsystemEOS;

typedef TSharedRef<const FUniqueNetId> FUniqueNetIdRef;

#define EOS_ID_SEPARATOR TEXT("|")

/**
 * Unique net id wrapper for a EOS account ids. The underlying string is a combination
 * of both account ids concatenated. "<EOS_EpicAccountId>|<EOS_ProductAccountId>"
 */
class FUniqueNetIdEOS :
	public FUniqueNetIdString
{
public:
	FUniqueNetIdEOS()
		: FUniqueNetIdString()
	{
	}

	explicit FUniqueNetIdEOS(const FString& InUniqueNetId)
		: FUniqueNetIdString(InUniqueNetId)
	{
		ParseAccountIds();
	}

	explicit FUniqueNetIdEOS(FString&& InUniqueNetId)
		: FUniqueNetIdString(MoveTemp(InUniqueNetId))
	{
		ParseAccountIds();
	}

	explicit FUniqueNetIdEOS(const FUniqueNetId& Src)
		: FUniqueNetIdString(Src)
	{
		ParseAccountIds();
	}

	friend uint32 GetTypeHash(const FUniqueNetIdEOS& A)
	{
		return ::GetTypeHash(A.UniqueNetIdStr);
	}

	/** global static instance of invalid (zero) id */
	static const TSharedRef<const FUniqueNetId>& EmptyId()
	{
		static const TSharedRef<const FUniqueNetId> EmptyId(MakeShared<FUniqueNetIdEOS>());
		return EmptyId;
	}

	virtual FName GetType() const override
	{
		static FName NAME_Eos(TEXT("EOS"));
		return NAME_Eos;
	}

PACKAGE_SCOPE:
	void UpdateNetIdStr(const FString& InNetIdStr)
	{
		UniqueNetIdStr = InNetIdStr;
		ParseAccountIds();
	}

	void ParseAccountIds()
	{
		TArray<FString> AccountIds;
		UniqueNetIdStr.ParseIntoArray(AccountIds, EOS_ID_SEPARATOR, false);
		if (AccountIds.Num() > 0)
		{
			EpicAccountIdStr = AccountIds[0];
		}
		if (AccountIds.Num() > 1)
		{
			ProductUserIdStr = AccountIds[1];
		}
	}

	FString EpicAccountIdStr;
	FString ProductUserIdStr;
};

typedef TSharedPtr<FUniqueNetIdEOS> FUniqueNetIdEOSPtr;
typedef TSharedRef<FUniqueNetIdEOS> FUniqueNetIdEOSRef;

#ifndef AUTH_ATTR_REFRESH_TOKEN
	#define AUTH_ATTR_REFRESH_TOKEN TEXT("refresh_token")
#endif
#ifndef AUTH_ATTR_ID_TOKEN
	#define AUTH_ATTR_ID_TOKEN TEXT("id_token")
#endif
#ifndef AUTH_ATTR_AUTHORIZATION_CODE
	#define AUTH_ATTR_AUTHORIZATION_CODE TEXT("authorization_code")
#endif

#define USER_ATTR_DISPLAY_NAME TEXT("display_name")
#define USER_ATTR_COUNTRY TEXT("country")
#define USER_ATTR_LANG TEXT("language")

#if WITH_EOS_SDK

/** Used to update all types of FOnlineUser classes, irrespective of leaf most class type */
class IAttributeAccessInterface
{
public:
	virtual void SetInternalAttribute(const FString& AttrName, const FString& AttrValue)
	{
	}

	virtual FUniqueNetIdEOSPtr GetUniqueNetIdEOS() const
	{
		return FUniqueNetIdEOSPtr();
	}
};

typedef TSharedPtr<IAttributeAccessInterface> IAttributeAccessInterfacePtr;
typedef TSharedRef<IAttributeAccessInterface> IAttributeAccessInterfaceRef;

/**
 * Implementation of FOnlineUser that can be shared across multiple class hiearchies
 */
template<class BaseClass, class AttributeAccessClass>
class TOnlineUserEOS
	: public BaseClass
	, public AttributeAccessClass
{
public:
	TOnlineUserEOS(FUniqueNetIdEOSRef InNetIdRef)
		: UserIdRef(InNetIdRef)
	{
	}

	virtual ~TOnlineUserEOS()
	{
	}

// FOnlineUser
	virtual TSharedRef<const FUniqueNetId> GetUserId() const override
	{
		return UserIdRef;
	}

	virtual FString GetRealName() const override
	{
		return FString();
	}

	virtual FString GetDisplayName(const FString& Platform = FString()) const override
	{
		FString ReturnValue;
		GetUserAttribute(USER_ATTR_DISPLAY_NAME, ReturnValue);
		return ReturnValue;
	}

	virtual bool GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const override
	{
		const FString* FoundAttr = UserAttributes.Find(AttrName);
		if (FoundAttr != nullptr)
		{
			OutAttrValue = *FoundAttr;
			return true;
		}
		return false;
	}
//~FOnlineUser

	virtual void SetInternalAttribute(const FString& AttrName, const FString& AttrValue)
	{
		UserAttributes.Add(AttrName, AttrValue);
	}

	virtual FUniqueNetIdEOSPtr GetUniqueNetIdEOS() const
	{
		return UserIdRef;
	}

protected:
	/** User Id represented as a FUniqueNetId */
	FUniqueNetIdEOSRef UserIdRef;
	/** Additional key/value pair data related to user attribution */
	TMap<FString, FString> UserAttributes;
};

/**
 * Implementation of FUserOnlineAccount methods that adds in the online user template to complete the interface
 */
template<class BaseClass>
class TUserOnlineAccountEOS :
	public TOnlineUserEOS<BaseClass, IAttributeAccessInterface>
{
public:
	TUserOnlineAccountEOS(FUniqueNetIdEOSRef InNetIdRef)
		: TOnlineUserEOS<BaseClass, IAttributeAccessInterface>(InNetIdRef)
	{
	}

// FUserOnlineAccount
	virtual FString GetAccessToken() const override
	{
		FString Token;
		GetAuthAttribute(AUTH_ATTR_ID_TOKEN, Token);
		return Token;
	}

	virtual bool GetAuthAttribute(const FString& AttrName, FString& OutAttrValue) const override
	{
		const FString* FoundAttr = AdditionalAuthData.Find(AttrName);
		if (FoundAttr != nullptr)
		{
			OutAttrValue = *FoundAttr;
			return true;
		}
		return false;
	}

	virtual bool SetUserAttribute(const FString& AttrName, const FString& AttrValue) override
	{
		const FString* FoundAttr = this->UserAttributes.Find(AttrName);
		if (FoundAttr == nullptr || *FoundAttr != AttrValue)
		{
			this->UserAttributes.Add(AttrName, AttrValue);
			return true;
		}
		return false;
	}
//~FUserOnlineAccount

	void SetAuthAttribute(const FString& AttrName, const FString& AttrValue)
	{
		AdditionalAuthData.Add(AttrName, AttrValue);
	}

protected:
	/** Additional key/value pair data related to auth */
	TMap<FString, FString> AdditionalAuthData;
};

typedef TSharedRef<FOnlineUserPresence> FOnlineUserPresenceRef;

/**
 * Implementation of FOnlineFriend methods that adds in the online user template to complete the interface
 */
template<class BaseClass>
class TOnlineFriendEOS :
	public TOnlineUserEOS<BaseClass, IAttributeAccessInterface>
{
public:
	TOnlineFriendEOS(FUniqueNetIdEOSRef InNetIdRef)
		: TOnlineUserEOS<BaseClass, IAttributeAccessInterface>(InNetIdRef)
	{
	}

// FOnlineFriend
	/**
	 * @return the current invite status of a friend wrt to user that queried
	 */
	virtual EInviteStatus::Type GetInviteStatus() const override
	{
		return InviteStatus;
	}

	/**
	 * @return presence info for an online friend
	 */
	virtual const FOnlineUserPresence& GetPresence() const override
	{
		return Presence;
	}
//~FOnlineFriend

	void SetInviteStatus(EInviteStatus::Type InStatus)
	{
		InviteStatus = InStatus;
	}

	void SetPresence(FOnlineUserPresenceRef InPresence)
	{
		// Copy the data over since the friend shares it as a const&
		Presence = *InPresence;
	}

protected:
	FOnlineUserPresence Presence;
	EInviteStatus::Type InviteStatus;
};

/**
 * Implementation of FOnlineBlockedPlayer methods that adds in the online user template to complete the interface
 */
template<class BaseClass>
class TOnlineBlockedPlayerEOS :
	public TOnlineUserEOS<BaseClass, IAttributeAccessInterface>
{
public:
	TOnlineBlockedPlayerEOS(FUniqueNetIdEOSRef InNetIdRef)
		: TOnlineUserEOS<BaseClass, IAttributeAccessInterface>(InNetIdRef)
	{
	}
};

/**
 * Implementation of FOnlineRecentPlayer methods that adds in the online user template to complete the interface
 */
template<class BaseClass>
class TOnlineRecentPlayerEOS :
	public TOnlineUserEOS<BaseClass, IAttributeAccessInterface>
{
public:
	TOnlineRecentPlayerEOS(FUniqueNetIdEOSRef InNetIdRef)
		: TOnlineUserEOS<BaseClass, IAttributeAccessInterface>(InNetIdRef)
	{
	}

// FOnlineRecentPlayer
	/**
	 * @return last time the player was seen by the current user
	 */
	virtual FDateTime GetLastSeen() const override
	{
		return LastSeenTime;
	}
//~FOnlineRecentPlayer

	void SetLastSeen(const FDateTime& InLastSeenTime)
	{
		LastSeenTime = InLastSeenTime;
	}

protected:
	FDateTime LastSeenTime;
};

static inline FString MakeStringFromProductUserId(EOS_ProductUserId UserId)
{
	FString StringId;

	char ProductIdString[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
	ProductIdString[0] = '\0';
	int32_t BufferSize = EOS_PRODUCTUSERID_MAX_LENGTH + 1;
	EOS_EResult Result = EOS_ProductUserId_ToString(UserId, ProductIdString, &BufferSize);
	ensure(Result == EOS_EResult::EOS_Success);
	StringId += ProductIdString;

	return StringId;
}

static inline FString MakeStringFromEpicAccountId(EOS_EpicAccountId AccountId)
{
	FString StringId;

	char AccountIdString[EOS_EPICACCOUNTID_MAX_LENGTH + 1];
	AccountIdString[0] = '\0';
	int32_t BufferSize = EOS_EPICACCOUNTID_MAX_LENGTH + 1;
	EOS_EResult Result = EOS_EpicAccountId_ToString(AccountId, AccountIdString, &BufferSize);
	ensure(Result == EOS_EResult::EOS_Success);
	StringId += AccountIdString;

	return StringId;
}

static inline FString MakeNetIdStringFromIds(EOS_EpicAccountId AccountId, EOS_ProductUserId UserId)
{
	FString NetId;

	if (EOS_EpicAccountId_IsValid(AccountId) == EOS_TRUE)
	{
		NetId = MakeStringFromEpicAccountId(AccountId);
	}
	// Only add this when the product user id is valid for more consistent net id string generation
	// across different code paths
	if (EOS_ProductUserId_IsValid(UserId) == EOS_TRUE)
	{
		NetId += EOS_ID_SEPARATOR + MakeStringFromProductUserId(UserId);
	}

	return NetId;
}

/** Used to store a pointer to the EOS callback object without knowing type */
class FCallbackBase
{
public:
	virtual ~FCallbackBase() {}
};

/** Class to handle all callbacks generically using a lambda to process callback results */
template<typename CallbackFuncType, typename CallbackType>
class TEOSCallback :
	public FCallbackBase
{
public:
	TFunction<void(const CallbackType*)> CallbackLambda;

	TEOSCallback()
	{

	}
	virtual ~TEOSCallback() = default;


	CallbackFuncType GetCallbackPtr()
	{
		return &CallbackImpl;
	}

private:
	static void EOS_CALL CallbackImpl(const CallbackType* Data)
	{
		if (EOS_EResult_IsOperationComplete(Data->ResultCode) == EOS_FALSE)
		{
			// Ignore
			return;
		}
		check(IsInGameThread());

		TEOSCallback* CallbackThis = (TEOSCallback*)Data->ClientData;
		check(CallbackThis);

		check(CallbackThis->CallbackLambda);
		CallbackThis->CallbackLambda(Data);

		delete CallbackThis;
	}
};

/** Class to handle all callbacks generically using a lambda to process callback results */
template<typename CallbackFuncType, typename CallbackType>
class TEOSGlobalCallback :
	public FCallbackBase
{
public:
	TFunction<void(const CallbackType*)> CallbackLambda;

	TEOSGlobalCallback() = default;
	virtual ~TEOSGlobalCallback() = default;


	CallbackFuncType GetCallbackPtr()
	{
		return &CallbackImpl;
	}

private:
	static void EOS_CALL CallbackImpl(const CallbackType* Data)
	{
		check(IsInGameThread());

		TEOSGlobalCallback* CallbackThis = (TEOSGlobalCallback*)Data->ClientData;
		check(CallbackThis);

		check(CallbackThis->CallbackLambda);
		CallbackThis->CallbackLambda(Data);
	}
};

#include "eos_sessions_types.h"

/**
 * Implementation of session information
 */
class FOnlineSessionInfoEOS :
	public FOnlineSessionInfo
{
protected:
	/** Hidden on purpose */
	FOnlineSessionInfoEOS& operator=(const FOnlineSessionInfoEOS& Src)
	{
		return *this;
	}

PACKAGE_SCOPE:
	/** Constructor */
	FOnlineSessionInfoEOS();

	FOnlineSessionInfoEOS(const FOnlineSessionInfoEOS& Src)
		: FOnlineSessionInfo(Src)
		, HostAddr(Src.HostAddr)
		, SessionId(Src.SessionId)
		, SessionHandle(Src.SessionHandle)
		, bIsFromClone(true)
	{
	}

	FOnlineSessionInfoEOS(const FString& InHostIp, const FString& InSessionId, EOS_HSessionDetails InSessionHandle);

	/**
	 * Initialize LAN session
	 */
	void InitLAN(FOnlineSubsystemEOS* Subsystem);

	FString EOSAddress;
	/** The ip & port that the host is listening on (valid for LAN/GameServer) */
	TSharedPtr<class FInternetAddr> HostAddr;
	/** Unique Id for this session */
	FUniqueNetIdEOS SessionId;
	/** EOS session handle. Note: this needs to be released by the SDK */
	EOS_HSessionDetails SessionHandle;
	/** Whether we should delete this handle or not */
	bool bIsFromClone;

public:
	virtual ~FOnlineSessionInfoEOS();
	bool operator==(const FOnlineSessionInfoEOS& Other) const
	{
		return false;
	}
	virtual const uint8* GetBytes() const override
	{
		return nullptr;
	}
	virtual int32 GetSize() const override
	{
		return sizeof(uint64) + sizeof(TSharedPtr<class FInternetAddr>);
	}
	virtual bool IsValid() const override
	{
		// LAN case
		return HostAddr.IsValid() && HostAddr->IsValid();
	}
	virtual FString ToString() const override
	{
		return SessionId.ToString();
	}
	virtual FString ToDebugString() const override
	{
		return FString::Printf(TEXT("HostIP: %s SessionId: %s"),
			HostAddr.IsValid() ? *HostAddr->ToString(true) : TEXT("INVALID"),
			*SessionId.ToDebugString());
	}
	virtual const FUniqueNetId& GetSessionId() const override
	{
		return SessionId;
	}
};

#endif
