// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/LobbiesCommon.h"
#include "OnlineServicesEOSTypes.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_lobby_types.h"

namespace UE::Online {

class FAuthEOS;
struct FLobbyPrerequisitesEOS;
class FLobbyDataEOS;
class FLobbyDataRegistryEOS;
class FLobbyDetailsEOS;
class FLobbyInviteDataEOS;
class FOnlineServicesEOS;
class FLobbySearchEOS;
struct FClientLobbyDataChanges;
struct FClientLobbyMemberDataChanges;
	
class FLobbiesEOS : public FLobbiesCommon
{
public:
	FLobbiesEOS(FOnlineServicesEOS& InServices);

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
	void RegisterHandlers();
	void UnregisterHandlers();

	void AddActiveLobby(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDataEOS>& LobbyData);
	void RemoveActiveLobby(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDataEOS>& LobbyData);

	// Todo: store list of invites per lobby.
	void AddActiveInvite(const TSharedRef<FLobbyInviteDataEOS>& Invite);
	void RemoveActiveInvite(const TSharedRef<FLobbyInviteDataEOS>& Invite);
	TSharedPtr<FLobbyInviteDataEOS> GetActiveInvite(FOnlineAccountIdHandle TargetUser, FOnlineLobbyIdHandle TargetLobbyId);

	struct FJoinLobbyImpl
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

	// LobbyData will be fetched from the operation data if not set in Params.
	TFuture<TDefaultErrorResult<FJoinLobbyImpl>> JoinLobbyImpl(FJoinLobbyImpl::Params&& Params);

	struct FJoinLobbyMemberImpl
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

	TOnlineAsyncOpHandle<FJoinLobbyMemberImpl> JoinLobbyMemberImplOp(FJoinLobbyMemberImpl::Params&& Params);
	TFuture<TDefaultErrorResult<FJoinLobbyMemberImpl>> JoinLobbyMemberImpl(FJoinLobbyMemberImpl::Params&& Params);

	struct FLeaveLobbyImpl
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

	TFuture<TDefaultErrorResult<FLeaveLobbyImpl>> LeaveLobbyImpl(FLeaveLobbyImpl::Params&& Params);

	struct FDestroyLobbyImpl
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

	TFuture<TDefaultErrorResult<FDestroyLobbyImpl>> DestroyLobbyImpl(FDestroyLobbyImpl::Params&& Params);

	struct FInviteLobbyMemberImpl
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

	TFuture<TDefaultErrorResult<FInviteLobbyMemberImpl>> InviteLobbyMemberImpl(FInviteLobbyMemberImpl::Params&& Params);

	struct FDeclineLobbyInvitationImpl
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

	TFuture<TDefaultErrorResult<FDeclineLobbyInvitationImpl>> DeclineLobbyInvitationImpl(FDeclineLobbyInvitationImpl::Params&& Params);

	struct FKickLobbyMemberImpl
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

	TFuture<TDefaultErrorResult<FKickLobbyMemberImpl>> KickLobbyMemberImpl(FKickLobbyMemberImpl::Params&& Params);

	struct FPromoteLobbyMemberImpl
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

	TFuture<TDefaultErrorResult<FPromoteLobbyMemberImpl>> PromoteLobbyMemberImpl(FPromoteLobbyMemberImpl::Params&& Params);

	struct FModifyLobbyDataImpl
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

	TFuture<TDefaultErrorResult<FModifyLobbyDataImpl>> ModifyLobbyDataImpl(FModifyLobbyDataImpl::Params&& Params);

	struct FModifyLobbyMemberDataImpl
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

	TFuture<TDefaultErrorResult<FModifyLobbyMemberDataImpl>> ModifyLobbyMemberDataImpl(FModifyLobbyMemberDataImpl::Params&& Params);

	struct FProcessLobbyNotificationImpl
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

	TOnlineAsyncOpHandle<FProcessLobbyNotificationImpl> ProcessLobbyNotificationImplOp(FProcessLobbyNotificationImpl::Params&& Params);

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

/* UE::Online */ }
