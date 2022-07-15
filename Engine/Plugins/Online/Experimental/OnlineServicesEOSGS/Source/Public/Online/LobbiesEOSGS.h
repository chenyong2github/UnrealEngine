// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/LobbiesCommon.h"
#include "OnlineServicesEOSGSTypes.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_lobby_types.h"

namespace UE::Online {

struct FLobbyPrerequisitesEOS;
class FLobbyDataEOS;
class FLobbyDataRegistryEOS;
class FLobbyInviteDataEOS;
class FOnlineServicesEOSGS;
class FLobbySearchEOS;
struct FClientLobbyDataChanges;
struct FClientLobbyMemberDataChanges;

struct FLobbiesJoinLobbyImpl
{
	static constexpr TCHAR Name[] = TEXT("JoinLobbyImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// The local name for the lobby.
		FName LocalName;

		// Local users who will be joining the lobby.
		TArray<FJoinLobbyLocalUserData> LocalUsers;
	};

	struct Result
	{
	};
};

struct FLobbiesJoinLobbyMemberImpl
{
	static constexpr TCHAR Name[] = TEXT("JoinLobbyMemberImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// Initial attributes.
		TMap<FLobbyAttributeId, FLobbyVariant> Attributes;
	};

	struct Result
	{
	};
};

struct FLobbiesLeaveLobbyImpl
{
	static constexpr TCHAR Name[] = TEXT("LeaveLobbyImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;
	};

	struct Result
	{
	};
};

struct FLobbiesDestroyLobbyImpl
{
	static constexpr TCHAR Name[] = TEXT("DestroyLobbyImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;
	};

	struct Result
	{
	};
};

struct FLobbiesInviteLobbyMemberImpl
{
	static constexpr TCHAR Name[] = TEXT("InviteLobbyMemberImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// The target user for the invitation.
		FOnlineAccountIdHandle TargetUserId;
	};

	struct Result
	{
	};
};

struct FLobbiesDeclineLobbyInvitationImpl
{
	static constexpr TCHAR Name[] = TEXT("DeclineLobbyInvitationImpl");

	struct Params
	{
		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// Id of the lobby for which the invitations will be declined.
		FOnlineLobbyIdHandle LobbyId;
	};

	struct Result
	{
	};
};

struct FLobbiesKickLobbyMemberImpl
{
	static constexpr TCHAR Name[] = TEXT("KickLobbyMemberImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// The target user to be kicked.
		FOnlineAccountIdHandle TargetUserId;
	};

	struct Result
	{
	};
};

struct FLobbiesPromoteLobbyMemberImpl
{
	static constexpr TCHAR Name[] = TEXT("PromoteLobbyMemberImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// The target user to be promoted to owner.
		FOnlineAccountIdHandle TargetUserId;
	};

	struct Result
	{
	};
};

struct FLobbiesModifyLobbyDataImpl
{
	static constexpr TCHAR Name[] = TEXT("ModifyLobbyDataImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// The changes to apply to the lobby data.
		TSharedPtr<FClientLobbyDataChanges> Changes;
	};

	// Todo: += operator.
	// Mergeable op must be queued by lobby id.

	struct Result
	{
	};
};

struct FLobbiesModifyLobbyMemberDataImpl
{
	static constexpr TCHAR Name[] = TEXT("ModifyLobbyMemberDataImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// The local user agent which will perform the action.
		FOnlineAccountIdHandle LocalUserId;

		// The changes to apply to the lobby member data.
		TSharedPtr<FClientLobbyMemberDataChanges> Changes;
	};

	// Todo: += operator.
	// Mergeable op must be queued on union of lobby id + lobby member account id.

	struct Result
	{
	};
};

struct FLobbiesProcessLobbyNotificationImpl
{
	static constexpr TCHAR Name[] = TEXT("ProcessLobbyNotificationImpl");

	struct Params
	{
		// The lobby handle data.
		TSharedPtr<FLobbyDataEOS> LobbyData;

		// Joining / mutated members.
		TSet<EOS_ProductUserId> MutatedMembers;

		// Leaving members.
		TMap<EOS_ProductUserId, ELobbyMemberLeaveReason> LeavingMembers;
	};

	// Todo: += operator.
	// Mergeable op must be queued by lobby id.

	struct Result
	{
	};
};

class FLobbiesEOSGS : public FLobbiesCommon
{
public:
	FLobbiesEOSGS(FOnlineServicesEOSGS& InServices);

	virtual void Initialize() override;
	virtual void PreShutdown() override;

	virtual TOnlineAsyncOpHandle<FCreateLobby> CreateLobby(FCreateLobby::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FFindLobbies> FindLobbies(FFindLobbies::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FJoinLobby> JoinLobby(FJoinLobby::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FLeaveLobby> LeaveLobby(FLeaveLobby::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FInviteLobbyMember> InviteLobbyMember(FInviteLobbyMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FDeclineLobbyInvitation> DeclineLobbyInvitation(FDeclineLobbyInvitation::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FKickLobbyMember> KickLobbyMember(FKickLobbyMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FPromoteLobbyMember> PromoteLobbyMember(FPromoteLobbyMember::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbyJoinPolicy> ModifyLobbyJoinPolicy(FModifyLobbyJoinPolicy::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbyAttributes> ModifyLobbyAttributes(FModifyLobbyAttributes::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FModifyLobbyMemberAttributes> ModifyLobbyMemberAttributes(FModifyLobbyMemberAttributes::Params&& Params) override;
	virtual TOnlineResult<FGetJoinedLobbies> GetJoinedLobbies(FGetJoinedLobbies::Params&& Params) override;

private:
	void HandleLobbyUpdated(const EOS_Lobby_LobbyUpdateReceivedCallbackInfo* Data);
	void HandleLobbyMemberUpdated(const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* Data);
	void HandleLobbyMemberStatusReceived(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* Data);
	void HandleLobbyInviteReceived(const EOS_Lobby_LobbyInviteReceivedCallbackInfo* Data);
	void HandleLobbyInviteAccepted(const EOS_Lobby_LobbyInviteAcceptedCallbackInfo* Data);
	void HandleJoinLobbyAccepted(const EOS_Lobby_JoinLobbyAcceptedCallbackInfo* Data);

protected:
#if !UE_BUILD_SHIPPING
	static void CheckMetadata();
#endif

	void RegisterHandlers();
	void UnregisterHandlers();

	void AddActiveLobby(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDataEOS>& LobbyData);
	void RemoveActiveLobby(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDataEOS>& LobbyData);

	// Todo: store list of invites per lobby.
	void AddActiveInvite(const TSharedRef<FLobbyInviteDataEOS>& Invite);
	void RemoveActiveInvite(const TSharedRef<FLobbyInviteDataEOS>& Invite);
	TSharedPtr<FLobbyInviteDataEOS> GetActiveInvite(FOnlineAccountIdHandle TargetUser, FOnlineLobbyIdHandle TargetLobbyId);

	// LobbyData will be fetched from the operation data if not set in Params.
	TFuture<TDefaultErrorResult<FLobbiesJoinLobbyImpl>> JoinLobbyImpl(FLobbiesJoinLobbyImpl::Params&& Params);
	TOnlineAsyncOpHandle<FLobbiesJoinLobbyMemberImpl> JoinLobbyMemberImplOp(FLobbiesJoinLobbyMemberImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesJoinLobbyMemberImpl>> JoinLobbyMemberImpl(FLobbiesJoinLobbyMemberImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesLeaveLobbyImpl>> LeaveLobbyImpl(FLobbiesLeaveLobbyImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesDestroyLobbyImpl>> DestroyLobbyImpl(FLobbiesDestroyLobbyImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesInviteLobbyMemberImpl>> InviteLobbyMemberImpl(FLobbiesInviteLobbyMemberImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesDeclineLobbyInvitationImpl>> DeclineLobbyInvitationImpl(FLobbiesDeclineLobbyInvitationImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesKickLobbyMemberImpl>> KickLobbyMemberImpl(FLobbiesKickLobbyMemberImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesPromoteLobbyMemberImpl>> PromoteLobbyMemberImpl(FLobbiesPromoteLobbyMemberImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesModifyLobbyDataImpl>> ModifyLobbyDataImpl(FLobbiesModifyLobbyDataImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FLobbiesModifyLobbyMemberDataImpl>> ModifyLobbyMemberDataImpl(FLobbiesModifyLobbyMemberDataImpl::Params&& Params);
	TOnlineAsyncOpHandle<FLobbiesProcessLobbyNotificationImpl> ProcessLobbyNotificationImplOp(FLobbiesProcessLobbyNotificationImpl::Params&& Params);

	EOSEventRegistrationPtr OnLobbyUpdatedEOSEventRegistration;
	EOSEventRegistrationPtr OnLobbyMemberUpdatedEOSEventRegistration;
	EOSEventRegistrationPtr OnLobbyMemberStatusReceivedEOSEventRegistration;
	EOSEventRegistrationPtr OnLobbyInviteReceivedEOSEventRegistration;
	EOSEventRegistrationPtr OnLobbyInviteAcceptedEOSEventRegistration;
	EOSEventRegistrationPtr OnJoinLobbyAcceptedEOSEventRegistration;

	TSharedPtr<FLobbyPrerequisitesEOS> LobbyPrerequisites;
	TSharedPtr<FLobbyDataRegistryEOS> LobbyDataRegistry;

	TMap<FOnlineAccountIdHandle, TSet<TSharedRef<FLobbyDataEOS>>> ActiveLobbies;
	TMap<FOnlineAccountIdHandle, TMap<FOnlineLobbyIdHandle, TSharedRef<FLobbyInviteDataEOS>>> ActiveInvites;
	TMap<FOnlineAccountIdHandle, TSharedRef<FLobbySearchEOS>> ActiveSearchResults;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FLobbiesJoinLobbyImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyImpl::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyImpl::Params, LocalName),
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyImpl::Params, LocalUsers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesJoinLobbyImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesJoinLobbyMemberImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyMemberImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyMemberImpl::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbiesJoinLobbyMemberImpl::Params, Attributes)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesJoinLobbyMemberImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesLeaveLobbyImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesLeaveLobbyImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesLeaveLobbyImpl::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesLeaveLobbyImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesDestroyLobbyImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesDestroyLobbyImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesDestroyLobbyImpl::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesDestroyLobbyImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesInviteLobbyMemberImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesInviteLobbyMemberImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesInviteLobbyMemberImpl::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbiesInviteLobbyMemberImpl::Params, TargetUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesInviteLobbyMemberImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesDeclineLobbyInvitationImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesDeclineLobbyInvitationImpl::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbiesDeclineLobbyInvitationImpl::Params, LobbyId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesDeclineLobbyInvitationImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesKickLobbyMemberImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesKickLobbyMemberImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesKickLobbyMemberImpl::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbiesKickLobbyMemberImpl::Params, TargetUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesKickLobbyMemberImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesPromoteLobbyMemberImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesPromoteLobbyMemberImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesPromoteLobbyMemberImpl::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FLobbiesPromoteLobbyMemberImpl::Params, TargetUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesPromoteLobbyMemberImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesModifyLobbyDataImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesModifyLobbyDataImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesModifyLobbyDataImpl::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesModifyLobbyDataImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesModifyLobbyMemberDataImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesModifyLobbyMemberDataImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesModifyLobbyMemberDataImpl::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesModifyLobbyMemberDataImpl::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesProcessLobbyNotificationImpl::Params)
	ONLINE_STRUCT_FIELD(FLobbiesProcessLobbyNotificationImpl::Params, LobbyData),
	ONLINE_STRUCT_FIELD(FLobbiesProcessLobbyNotificationImpl::Params, MutatedMembers),
	ONLINE_STRUCT_FIELD(FLobbiesProcessLobbyNotificationImpl::Params, LeavingMembers)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FLobbiesProcessLobbyNotificationImpl::Result)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
