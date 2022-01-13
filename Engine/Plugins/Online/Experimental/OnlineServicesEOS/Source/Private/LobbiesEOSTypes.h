// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/Lobbies.h"
#include "OnlineIdEOS.h"
#include "OnlineServicesEOSTypes.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif

#include "eos_lobby.h"

namespace UE::Online {

class FLobbySchemaRegistry;
class FLobbyDataRegistryEOS;
class FLobbyDetailsInfoEOS;
class FLobbyInviteDataEOSRegistry;
class FLobbySchema;
class FAuthEOS;

inline EOS_ELobbyPermissionLevel TranslateJoinPolicy(ELobbyJoinPolicy JoinPolicy)
{
	switch (JoinPolicy)
	{
	case ELobbyJoinPolicy::PublicAdvertised:	return EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
	case ELobbyJoinPolicy::PublicNotAdvertised:	return EOS_ELobbyPermissionLevel::EOS_LPL_JOINVIAPRESENCE;
	default:									checkNoEntry(); // Intentional fallthrough
	case ELobbyJoinPolicy::InvitationOnly:		return EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
	};
}

using FLobbySearchParameters = FFindLobby::Params;

struct FLobbyDataChanges
{
	TOptional<ELobbyJoinPolicy> JoinPolicy;

	// New or changed attributes.
	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;

	// Attributes to be cleared.
	TSet<FLobbyAttributeId> ClearedAttributes;
};

struct FLobbyMemberDataChanges
{
	// New or changed attributes.
	TMap<FLobbyAttributeId, FLobbyVariant> MutatedAttributes;

	// Attributes to be cleared.
	TSet<FLobbyAttributeId> ClearedAttributes;
};

struct FLobbyPrerequisitesEOS
{
	EOS_HLobby LobbyInterfaceHandle = {};
	TSharedRef<FAuthEOS> AuthInterface;
	TSharedRef<const FLobbySchemaRegistry> SchemaRegistry;
	TSharedRef<const FLobbySchema> ServiceSchema;
};

enum class ELobbyDetailsSource
{
	Active,
	Invite,
	UiEvent,
	Search
};

class FLobbyDetailsEOS final : public TSharedFromThis<FLobbyDetailsEOS>
{
public:
	UE_NONCOPYABLE(FLobbyDetailsEOS);

	static TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> CreateFromLobbyId(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, EOS_LobbyId LobbyId, FOnlineAccountIdHandle MemberHandle);
	static TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> CreateFromInviteId(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, const char* InviteId);
	static TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> CreateFromUiEventId(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, EOS_UI_EventId UiEventId);
	static TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsEOS>> CreateFromSearchResult(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, EOS_HLobbySearch SearchHandle, uint32_t ResultIndex);

	FLobbyDetailsEOS() = delete;
	~FLobbyDetailsEOS();

	bool IsValid() const { return LobbyDetailsHandle != InvalidLobbyDetailsHandle; }
	EOS_HLobbyDetails GetEOSHandle() const { return LobbyDetailsHandle; }
	const TSharedRef<FLobbyDetailsInfoEOS>& GetInfo() const { return LobbyDetailsInfo; }
	ELobbyDetailsSource GetDetailsSource() const { return LobbyDetailsSource; }

	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobby>>> GetLobbyData(FOnlineAccountIdHandle LocalUserId, FOnlineLobbyIdHandle LobbyIdHandle) const;
	TDefaultErrorResultInternal<TSharedPtr<FLobbyMember>> GetLobbyMemberData(FOnlineAccountIdHandle MemberHandle) const;
	TFuture<EOS_EResult> ApplyLobbyDataUpdates(FOnlineAccountIdHandle LocalUserId, FLobbyDataChanges Changes) const;
	TFuture<EOS_EResult> ApplyLobbyMemberDataUpdates(FOnlineAccountIdHandle LocalUserId, FLobbyMemberDataChanges Changes) const;

private:
	template <typename, ESPMode>
	friend class SharedPointerInternals::TIntrusiveReferenceController;
	FLobbyDetailsEOS(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites, const TSharedRef<FLobbyDetailsInfoEOS>& LobbyDetailsInfo, ELobbyDetailsSource LobbyDetailsSource, EOS_HLobbyDetails LobbyDetailsHandle);

	TPair<FLobbyAttributeId, FLobbyVariant> TranslateLobbyAttribute(const EOS_Lobby_Attribute& LobbyAttribute) const;
	TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> GetLobbyAttributes() const;
	TDefaultErrorResultInternal<TMap<FLobbyAttributeId, FLobbyVariant>> GetLobbyMemberAttributes(EOS_ProductUserId TargetMemberProductUserId) const;
	TSharedRef<FLobbyMember> CreateLobbyMember(FOnlineAccountIdHandle MemberHandle, TMap<FLobbyAttributeId, FLobbyVariant> Attributes) const;

	static const EOS_HLobbyDetails InvalidLobbyDetailsHandle;
	TSharedRef<FLobbyPrerequisitesEOS> Prerequisites;
	TSharedRef<FLobbyDetailsInfoEOS> LobbyDetailsInfo;
	EOS_HLobbyDetails LobbyDetailsHandle = InvalidLobbyDetailsHandle;
	ELobbyDetailsSource LobbyDetailsSource;
};

class FLobbyDetailsInfoEOS final
{
public:
	UE_NONCOPYABLE(FLobbyDetailsInfoEOS);
	FLobbyDetailsInfoEOS() = default;

	static TDefaultErrorResultInternal<TSharedPtr<FLobbyDetailsInfoEOS>> Create(EOS_HLobbyDetails LobbyDetailsHandle);

	bool IsValid() const { return LobbyDetailsInfo.IsValid(); }

	EOS_LobbyId GetLobbyIdEOS() const { return LobbyDetailsInfo->LobbyId; }

private:
	struct FEOSLobbyDetailsInfoDeleter
	{
		void operator()(EOS_LobbyDetails_Info* Ptr) const
		{
			EOS_LobbyDetails_Info_Release(Ptr);
		}
	};

	TUniquePtr<EOS_LobbyDetails_Info, FEOSLobbyDetailsInfoDeleter> LobbyDetailsInfo;
};

class FLobbyNotificationPauseHandle
{
public:
	UE_NONCOPYABLE(FLobbyNotificationPauseHandle);
	FLobbyNotificationPauseHandle() = default;
	virtual ~FLobbyNotificationPauseHandle() = default;
};

class FLobbyDataEOS final : public TSharedFromThis<FLobbyDataEOS>
{
public:
	using FUnregisterFn = TFunction<void(FOnlineLobbyIdHandle)>;

	FLobbyDataEOS() = delete;

	static TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> Create(
		FOnlineAccountIdHandle LocalUserId,
		FOnlineLobbyIdHandle LobbyIdHandle,
		const TSharedRef<FLobbyDetailsEOS>& LobbyDetails,
		FUnregisterFn UnregisterFn = FUnregisterFn());

	~FLobbyDataEOS();

	FOnlineLobbyIdHandle GetLobbyIdHandle() const { return LobbyImpl->LobbyId; }
	const TSharedRef<FLobby>& GetLobbyImpl() const { return LobbyImpl; }
	EOS_LobbyId GetLobbyIdEOS() const { return LobbyDetailsInfo->GetLobbyIdEOS(); }
	const FString& GetLobbyId() const { return LobbyId; }
	TSet<FOnlineAccountIdHandle>& GetLocalMembers() { return LocalMembers; }

	// Pause lobby notifications for the target local user.
	TSharedRef<FLobbyNotificationPauseHandle> PauseLobbyNotifications(FOnlineAccountIdHandle LocalUserId);

	// Returns false if there is an existing notification pause for the target local user.
	bool CanNotify(FOnlineAccountIdHandle LocalUserId);

	void SetUserLobbyDetails(FOnlineAccountIdHandle LocalUserId, const TSharedPtr<FLobbyDetailsEOS>& LobbyDetails);
	TSharedPtr<FLobbyDetailsEOS> GetUserLobbyDetails(FOnlineAccountIdHandle LocalUserId) const;

private:
	template <typename, ESPMode>
	friend class SharedPointerInternals::TIntrusiveReferenceController;

	FLobbyDataEOS(
		FOnlineLobbyIdHandle LobbyIdHandle,
		const TSharedRef<FLobby>& LobbyImpl,
		const TSharedRef<FLobbyDetailsInfoEOS>& LobbyDetailsInfo,
		FUnregisterFn UnregisterFn);

	FOnlineLobbyIdHandle LobbyIdHandle;
	TSharedRef<FLobby> LobbyImpl;
	TSharedRef<FLobbyDetailsInfoEOS> LobbyDetailsInfo;
	FUnregisterFn UnregisterFn;
	FString LobbyId;
	TSet<FOnlineAccountIdHandle> LocalMembers;
	TMap<FOnlineAccountIdHandle, TSharedRef<FLobbyNotificationPauseHandle>> NotificationPauses;
	TMap<FOnlineAccountIdHandle, TSharedPtr<FLobbyDetailsEOS>> UserLobbyDetails;
};

// Todo: implement handling for FOnlineIdRegistryRegistry.
class FLobbyDataRegistryEOS : public TSharedFromThis<FLobbyDataRegistryEOS>
{
public:
	FLobbyDataRegistryEOS(const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites);

	TSharedPtr<FLobbyDataEOS> Find(EOS_LobbyId EOSLobbyId) const;
	TSharedPtr<FLobbyDataEOS> Find(FOnlineLobbyIdHandle LobbyIdHandle) const;
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FindOrCreateFromLobbyId(FOnlineAccountIdHandle LocalUserId, EOS_LobbyId LobbyId);
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FindOrCreateFromUiEventId(FOnlineAccountIdHandle LocalUserId, EOS_UI_EventId UiEventId);
	TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyDataEOS>>> FindOrCreateFromLobbyDetails(FOnlineAccountIdHandle LocalUserId, const TSharedRef<FLobbyDetailsEOS>& LobbyDetails);

private:
	void Register(const TSharedRef<FLobbyDataEOS>& LobbyIdHandleData);
	void Unregister(FOnlineLobbyIdHandle LobbyIdHandle);
	FLobbyDataEOS::FUnregisterFn MakeUnregisterFn();

	TSharedRef<FLobbyPrerequisitesEOS> Prerequisites;
	TMap<FString, TWeakPtr<FLobbyDataEOS>> LobbyIdIndex;
	TMap<FOnlineLobbyIdHandle, TWeakPtr<FLobbyDataEOS>> LobbyIdHandleIndex;
	uint32 NextHandleIndex = 1;
};

class FLobbyInviteIdEOS final
{
public:
	FLobbyInviteIdEOS() = default;
	FLobbyInviteIdEOS(const char* InviteIdEOS)
	{
		FPlatformString::Strcpy(Data, MaxLobbyInviteIdSize, InviteIdEOS);
	}

	const char* Get() const { return Data; }

private:
	static const int MaxLobbyInviteIdSize = 256;
	char Data[MaxLobbyInviteIdSize] = {};
};

class FLobbyInviteDataEOS final
{
public:
	FLobbyInviteDataEOS() = delete;

	static TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbyInviteDataEOS>>> CreateFromInviteId(
		const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
		const TSharedRef<FLobbyDataRegistryEOS>& LobbyDataRegistry,
		FOnlineAccountIdHandle LocalUserId,
		const char* InviteIdEOS,
		EOS_ProductUserId Sender);

	TSharedRef<FLobbyDetailsEOS> GetLobbyDetails() const { return LobbyDetails; }
	TSharedRef<FLobbyDataEOS> GetLobbyData() const { return LobbyData; }

	const char* GetInviteIdEOS() const { return InviteIdEOS->Get(); }
	const FString& GetInviteId() const { return InviteId; }

	FOnlineAccountIdHandle GetReceiver() const { return Receiver; }
	FOnlineAccountIdHandle GetSender() const { return Sender; }

private:
	template <typename, ESPMode>
	friend class SharedPointerInternals::TIntrusiveReferenceController;

	FLobbyInviteDataEOS(
		const TSharedRef<FLobbyInviteIdEOS>& InviteIdEOS,
		FOnlineAccountIdHandle Receiver,
		FOnlineAccountIdHandle Sender,
		const TSharedRef<FLobbyDetailsEOS>& LobbyDetails,
		const TSharedRef<FLobbyDataEOS>& LobbyData);

	TSharedRef<FLobbyInviteIdEOS> InviteIdEOS;
	FOnlineAccountIdHandle Receiver;
	FOnlineAccountIdHandle Sender;
	TSharedRef<FLobbyDetailsEOS> LobbyDetails;
	TSharedRef<FLobbyDataEOS> LobbyData;
	FString InviteId;
};

// The lobby search object is meant to hold the lifetimes of lobby search results so that the
// lobby details will be discoverable during a join operation.
class FLobbySearchEOS final
{
public:
	FLobbySearchEOS() = delete;

	static TFuture<TDefaultErrorResultInternal<TSharedPtr<FLobbySearchEOS>>> Create(
		const TSharedRef<FLobbyPrerequisitesEOS>& Prerequisites,
		const TSharedRef<FLobbyDataRegistryEOS>& LobbyRegistry,
		const FLobbySearchParameters& Params);

	TArray<TSharedRef<const FLobby>> GetLobbyResults() const;
	const TArray<TSharedPtr<FLobbyDataEOS>>& GetLobbyData();

private:
	class FSearchHandle
	{
	public:
		UE_NONCOPYABLE(FSearchHandle);
		FSearchHandle() = default;

		~FSearchHandle()
		{
			EOS_LobbySearch_Release(SearchHandle);
		}

		EOS_HLobbySearch& Get()
		{
			return SearchHandle;
		}

	private:
		EOS_HLobbySearch SearchHandle = {};
	};

	template <typename, ESPMode>
	friend class SharedPointerInternals::TIntrusiveReferenceController;
	FLobbySearchEOS(const TSharedRef<FSearchHandle>& SearchHandle, TArray<TSharedPtr<FLobbyDataEOS>>&& Lobbies);

	TSharedRef<FSearchHandle> SearchHandle;
	TArray<TSharedPtr<FLobbyDataEOS>> Lobbies;
};

/* UE::Online */ }
