// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/SessionsLAN.h"

#include "Online/OnlineServicesEOSGSTypes.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_sessions_types.h"
#include "eos_ui_types.h"

namespace UE::Online {

class FOnlineServicesEOSGS;

class FOnlineSessionIdRegistryEOSGS : public TOnlineSessionIdStringRegistry<EOnlineServices::Epic>
{
public:
	static FOnlineSessionIdRegistryEOSGS& Get();
};

static FName EOS_SESSIONS_BUCKET_ID = TEXT("EOS_SESSIONS_BUCKET_ID");

struct FSessionModificationHandleEOSGS : FNoncopyable
{
	EOS_HSessionModification ModificationHandle;

	FSessionModificationHandleEOSGS(EOS_HSessionModification InModificationHandle)
		: ModificationHandle(InModificationHandle)
	{
	}

	~FSessionModificationHandleEOSGS()
	{
		EOS_SessionModification_Release(ModificationHandle);
	}
};

struct FSessionSearchHandleEOSGS : FNoncopyable
{
	EOS_HSessionSearch SearchHandle;

	FSessionSearchHandleEOSGS(EOS_HSessionSearch InSearchHandle)
		: SearchHandle(InSearchHandle)
	{
	}

	~FSessionSearchHandleEOSGS()
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

	~FSessionDetailsHandleEOSGS()
	{
		EOS_SessionDetails_Release(SessionDetailsHandle);
	}
};

class FSessionEOSGS : public FSessionLAN
{
public:
	FSessionEOSGS() = default;
	FSessionEOSGS(const FSessionEOSGS& InSession) = default;
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
		TSharedRef<FSessionModificationHandleEOSGS> SessionModificationHandle;
	};

	struct Result
	{
	};
};

class ONLINESERVICESEOSGS_API FSessionsEOSGS : public FSessionsLAN
{
public:
	friend class FSessionEOSGS;

	using Super = FSessionsLAN;

	FSessionsEOSGS(FOnlineServicesEOSGS& InOwningSubsystem);
	virtual ~FSessionsEOSGS() = default;

	// TOnlineComponent
	void Initialize() override;
	void Shutdown() override;

	// ISessions
	virtual TOnlineAsyncOpHandle<FCreateSession> CreateSession(FCreateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FUpdateSession> UpdateSession(FUpdateSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveSession> LeaveSession(FLeaveSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindSessions> FindSessions(FFindSessions::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinSession> JoinSession(FJoinSession::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FSendSessionInvite> SendSessionInvite(FSendSessionInvite::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRejectSessionInvite> RejectSessionInvite(FRejectSessionInvite::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAddSessionMembers> AddSessionMembers(FAddSessionMembers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FRemoveSessionMembers> RemoveSessionMembers(FRemoveSessionMembers::Params&& Params) override;

protected:
	void RegisterEventHandlers();
	void UnregisterEventHandlers();
	void HandleSessionInviteReceived(const EOS_Sessions_SessionInviteReceivedCallbackInfo* Data);
	void HandleSessionInviteAccepted(const EOS_Sessions_SessionInviteAcceptedCallbackInfo* Data);
	void HandleJoinSessionAccepted(const EOS_Sessions_JoinSessionAcceptedCallbackInfo* Data);

	void SetPermissionLevel(EOS_HSessionModification& SessionModHandle, const ESessionJoinPolicy& JoinPolicy);
	void SetBucketId(EOS_HSessionModification& SessionModHandle, const FString& NewBucketId);
	void SetMaxPlayers(EOS_HSessionModification& SessionModHandle, const uint32& NewMaxPlayers);

	void SetSessionSearchMaxResults(FSessionSearchHandleEOSGS& SessionSearchHandle, uint32 MaxResults);
	void SetSessionSearchParameters(FSessionSearchHandleEOSGS& SessionSearchHandle, TArray<FFindSessionsSearchFilter> Filters);
	void SetSessionSearchSessionId(FSessionSearchHandleEOSGS& SessionSearchHandle, const FOnlineSessionIdHandle& SessionId);
	void SetSessionSearchTargetId(FSessionSearchHandleEOSGS& SessionSearchHandle, const FOnlineAccountIdHandle& TargetUserId);

	/**
	 * Writes all values in the passed SessionSettings to the SessionModificationHandle
	 */
	void WriteCreateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FSessionSettings& SessionSettings);
	/**
	 * Writes only the new values for all updated settings to the SessionModificationHandle
	 */
	void WriteUpdateSessionModificationHandle(EOS_HSessionModification& SessionModificationHandle, const FName& SessionName, const FSessionSettingsUpdate& NewSettings);
	TFuture<TDefaultErrorResult<FUpdateSessionImpl>> UpdateSessionImpl(FUpdateSessionImpl::Params&& Params);

	void WriteSessionSearchHandle(FSessionSearchHandleEOSGS& SessionSearchHandle, const FFindSessions::Params& Params);

	static FOnlineSessionIdHandle CreateSessionId(const FString& SessionId);

	/**
	 * Builds a session invite from an invite id, using the class' Sessions Handle
	 */
	TResult<TSharedRef<const FSessionInvite>, FOnlineError> BuildSessionInvite(FOnlineAccountIdHandle RecipientId, FOnlineAccountIdHandle SenderId, const FString& InInviteId) const;

	/**
	* Builds a session from an invite id, using the class' Sessions Handle
	 */
	TResult<TSharedRef<const FSession>, FOnlineError> BuildSessionFromInvite(const FString& InInviteId) const;

	/**
	 * Builds a session from a UI event id, using the class' Sessions Handle
	 */
	TResult<TSharedRef<const FSession>, FOnlineError> BuildSessionFromUIEvent(const EOS_UI_EventId& UIEventId) const;

private:
	/** Returns true if the named session is found and removed successfully, and false otherwise */
	bool TryRemoveSession(const FName& SessionName);

	// FSessionsLAN
	virtual void AppendSessionToPacket(FNboSerializeToBuffer& Packet, const FSessionLAN& Session) override;
	virtual void ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionLAN& Session) override;

protected:
	EOS_HSessions SessionsHandle = nullptr;

	EOSEventRegistrationPtr OnSessionInviteReceivedEventRegistration;
	EOSEventRegistrationPtr OnSessionInviteAcceptedEventRegistration;
	EOSEventRegistrationPtr OnJoinSessionAcceptedEventRegistration;

	TSharedPtr<FSessionSearchHandleEOSGS> CurrentSessionSearchHandleEOSGS;
};

/* UE::Online */ }