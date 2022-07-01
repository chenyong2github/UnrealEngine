// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/SessionsCommon.h"
#include "IPAddress.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_sessions_types.h"

namespace UE::Online {

class FOnlineServicesEOSGS;

/** Session Id Registry */

// TODO: Move to Common since it's in Null too
class FOnlineSessionIdString
{
public:
	FString Data;
	FOnlineSessionIdHandle Handle;
};

class FOnlineSessionIdRegistryEOSGS : public IOnlineSessionIdRegistry
{
public:
	static FOnlineSessionIdRegistryEOSGS& Get();

	FOnlineSessionIdHandle Find(const FString& SessionId) const;
	FOnlineSessionIdHandle FindOrAdd(const FString& SessionId);

	// Begin IOnlineSessionIdRegistry
	virtual FString ToLogString(const FOnlineSessionIdHandle& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FOnlineSessionIdHandle& Handle) const override;
	virtual FOnlineSessionIdHandle FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineSessionIdRegistry

	virtual ~FOnlineSessionIdRegistryEOSGS() = default;

private:
	const FOnlineSessionIdString* GetInternal(const FOnlineSessionIdHandle& Handle) const;

	TArray<FOnlineSessionIdString> Ids;
	TMap<FString, FOnlineSessionIdString> StringToId;
};

static FName EOS_SESSIONS_BUCKET_ID = TEXT("EOS_SESSIONS_BUCKET_ID");

struct FSessionSearchHandleEOSGS : FNoncopyable
{
	EOS_HSessionSearch SearchHandle;

	FSessionSearchHandleEOSGS(EOS_HSessionSearch InSearchHandle)
		: SearchHandle(InSearchHandle)
	{
	}

	virtual ~FSessionSearchHandleEOSGS()
	{
		EOS_SessionSearch_Release(SearchHandle);
	}
};

struct FSessionDetailsHandleEOSGS : FNoncopyable
{
	EOS_HSessionDetails SessionDetailsHandle;

	FSessionDetailsHandleEOSGS(EOS_HSessionDetails InSessionDetailsHandle)
		: SessionDetailsHandle(InSessionDetailsHandle)
	{
	}

	virtual ~FSessionDetailsHandleEOSGS()
	{
		EOS_SessionDetails_Release(SessionDetailsHandle);
	}
};

class FSessionEOSGS : public FSession
{
public:
	FSessionEOSGS();
	FSessionEOSGS(const FSession& InSession);
	FSessionEOSGS(const EOS_HSessionDetails& SessionDetailsHandle);

	static const FSessionEOSGS& Cast(const FSession& InSession);

public:
	/** Session details handle */
	TSharedPtr<FSessionDetailsHandleEOSGS> SessionDetailsHandle;
};

struct FUpdateSessionImpl
{
	static constexpr TCHAR Name[] = TEXT("FUpdateSessionImpl");

	struct Params
	{
		/** Handle for the session modification operation */
		EOS_HSessionModification SessionModificationHandle;
	};

	struct Result
	{
	};
};

class ONLINESERVICESEOSGS_API FSessionsEOSGS : public FSessionsCommon
{
public:
	using Super = FSessionsCommon;

	FSessionsEOSGS(FOnlineServicesEOSGS& InOwningSubsystem);
	virtual ~FSessionsEOSGS() = default;

	// TOnlineComponent
	void Initialize() override;

	// ISessions
	virtual TOnlineResult<FGetAllSessions> GetAllSessions(FGetAllSessions::Params&& Params) override;
	virtual TOnlineResult<FGetSessionByName> GetSessionByName(FGetSessionByName::Params&& Params) override;
	virtual TOnlineResult<FGetSessionById> GetSessionById(FGetSessionById::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;

protected:
	void SetPermissionLevel(EOS_HSessionModification& SessionModHandle, const ESessionJoinPolicy& JoinPolicy);
	void SetBucketId(EOS_HSessionModification& SessionModHandle, const FString& NewBucketId);
	void SetMaxPlayers(EOS_HSessionModification& SessionModHandle, const int32& NewMaxPlayers);

	/**
	 * Writes all values in the passed SessionSettings to the SessionModificationHandle
	 */
	void WriteCreateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FSessionSettings& SessionSettings);
	/**
	 * Writes only the new values for all updated settings to the SessionModificationHandle, as well as updating them on the passed SessionSettings
	 */
	void WriteUpdateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, FSessionSettings& SessionSettings, const FSessionSettingsUpdate& NewSettings);
	TFuture<TDefaultErrorResult<FUpdateSessionImpl>> UpdateSessionImpl(FUpdateSessionImpl::Params&& Params);

	void WriteSessionSearchHandle(FSessionSearchHandleEOSGS& SessionSearchHandle, const FFindSessions::Params& Params);

protected:
	FOnlineServicesEOSGS& Services;

	EOS_HSessions SessionsHandle = nullptr;

	TMap<FName, TSharedRef<FSessionEOSGS>> SessionsByName;
	TMap<FOnlineSessionIdHandle, TSharedRef<FSessionEOSGS>> SessionsById;

	TSharedPtr<FSessionSearchHandleEOSGS> CurrentSessionSearchHandleEOSGS;
	TSharedPtr<FFindSessions::Result> CurrentSessionSearch;
	TSharedPtr<TOnlineAsyncOp<FFindSessions>> CurrentSessionSearchAsyncOpHandle;
};

/* UE::Online */ }