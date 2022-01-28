// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/LobbiesCommonTypes.h"

namespace UE::Online {

TSharedPtr<FLobbySchema> FLobbySchema::Create(FLobbySchemaConfig LobbySchemaConfig)
{
	TSharedRef<FLobbySchema> Schema = MakeShared<FLobbySchema>();
	Schema->SchemaId = LobbySchemaConfig.SchemaName;

	// Todo: for real validation and init
	if (Schema->SchemaId == FLobbySchemaId())
	{
		return TSharedPtr<FLobbySchema>();
	}
	else
	{
		return Schema;
	}
}

TSharedRef<FLobbyServiceAttributeChanges> FLobbySchema::TranslateLobbyAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyServiceAttributeChanges> Changes = MakeShared<FLobbyServiceAttributeChanges>();
	Changes->MutatedAttributes = ClientAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ClientAttributeChanges.ClearedAttributes;
	return Changes;
}

TSharedRef<FLobbyClientAttributeChanges> FLobbySchema::TranslateLobbyAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyClientAttributeChanges> Changes = MakeShared<FLobbyClientAttributeChanges>();
	Changes->MutatedAttributes = ServiceAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ServiceAttributeChanges.ClearedAttributes;
	return Changes;
}

TSharedRef<FLobbyServiceAttributeChanges> FLobbySchema::TranslateLobbyMemberAttributes(const TMap<FLobbyAttributeId, FLobbyVariant>& LobbyAttributes, const FLobbyClientAttributeChanges& ClientAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyServiceAttributeChanges> Changes = MakeShared<FLobbyServiceAttributeChanges>();
	Changes->MutatedAttributes = ClientAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ClientAttributeChanges.ClearedAttributes;
	return Changes;
}

TSharedRef<FLobbyClientAttributeChanges> FLobbySchema::TranslateLobbyMemberAttributes(const FLobbyServiceAttributeChanges& ServiceAttributeChanges) const
{
	// Todo: implementation.

	TSharedRef<FLobbyClientAttributeChanges> Changes = MakeShared<FLobbyClientAttributeChanges>();
	Changes->MutatedAttributes = ServiceAttributeChanges.MutatedAttributes;
	Changes->ClearedAttributes = ServiceAttributeChanges.ClearedAttributes;
	return Changes;
}

bool FLobbySchemaRegistry::Initialize(TArray<FLobbySchemaConfig> LobbySchemaConfigs)
{
	bool bSuccess = true;

	for (FLobbySchemaConfig& LobbySchemaConfig : LobbySchemaConfigs)
	{
		bSuccess &= RegisterSchema(MoveTemp(LobbySchemaConfig));
	}

	return bSuccess;
}

TSharedPtr<FLobbySchema> FLobbySchemaRegistry::FindSchema(FLobbySchemaId SchemaId)
{
	TSharedRef<FLobbySchema>* Schema = RegisteredSchemas.Find(SchemaId);
	return Schema ? *Schema : TSharedPtr<FLobbySchema>();
}

bool FLobbySchemaRegistry::RegisterSchema(FLobbySchemaConfig LobbySchemaConfig)
{
	// Todo: handle schema hierarchy.
	// Todo: handle schema parsing.

	FLobbySchemaId SchemaName = LobbySchemaConfig.SchemaName;

	if (TSharedPtr<FLobbySchema> Schema = FLobbySchema::Create(MoveTemp(LobbySchemaConfig)))
	{
		RegisteredSchemas.Emplace(SchemaName, Schema.ToSharedRef());
		return true;
	}
	else
	{
		return false;
	}
}


FClientLobbyData::FClientLobbyData(FOnlineLobbyIdHandle LobbyId)
	: PublicData(MakeShared<FLobby>())
{
	PublicData->LobbyId = LobbyId;
}

TSharedPtr<const FClientLobbyMemberSnapshot> FClientLobbyData::GetMemberData(FOnlineAccountIdHandle MemberAccountId) const
{
	const TSharedRef<FClientLobbyMemberSnapshot>* FoundMemberData = MemberDataStorage.Find(MemberAccountId);
	return FoundMemberData ? *FoundMemberData : TSharedPtr<const FClientLobbyMemberSnapshot>();
}

FApplyLobbyUpdateResult FClientLobbyData::ApplyLobbyUpdateFromServiceSnapshot(
	FClientLobbySnapshot&& LobbySnapshot,
	TMap<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>>&& LobbyMemberSnapshots,
	TMap<FOnlineAccountIdHandle, ELobbyMemberLeaveReason>&& LeaveReasons,
	FLobbyEvents* LobbyEvents)
{
	FApplyLobbyUpdateResult Result;
	const bool bDispatchNotifications = LobbyEvents != nullptr && !LocalMembers.IsEmpty();

	// Notify if schema changed.
	// Schema change notification must be processed first as the schema affects how the client processes attributes.
	if (LobbySnapshot.SchemaName != PublicData->SchemaName)
	{
		PublicData->SchemaName = LobbySnapshot.SchemaName;
		if (bDispatchNotifications)
		{
			LobbyEvents->OnLobbySchemaChanged.Broadcast(FLobbySchemaChanged{PublicData});
		}
	}

	// Handle lobby attribute changes.
	TSet<FLobbyAttributeId> LobbyAttributeChanges = ApplyAttributeUpdateFromSnapshot(MoveTemp(LobbySnapshot.Attributes), PublicData->Attributes);
	if (bDispatchNotifications && !LobbyAttributeChanges.IsEmpty())
	{
		LobbyEvents->OnLobbyAttributesChanged.Broadcast(FLobbyAttributesChanged{PublicData, MoveTemp(LobbyAttributeChanges)});
	}

	// Process members who left.
	{
		// Build list of leaving members.
		TArray<TPair<TSharedRef<FClientLobbyMemberSnapshot>, ELobbyMemberLeaveReason>> LeavingMembers;
		for (TPair<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>>& MemberData : MemberDataStorage)
		{
			// Check if member exists in new snapshot.
			if (LobbySnapshot.Members.Find(MemberData.Key) == nullptr)
			{
				// Member has left - check if they gave an explicit reason.
				ELobbyMemberLeaveReason ResovedReason = ELobbyMemberLeaveReason::Disconnected;
				if (const ELobbyMemberLeaveReason* MemberLeaveReason = LeaveReasons.Find(MemberData.Key))
				{
					ResovedReason = *MemberLeaveReason;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[FClientLobbyData::ApplyLobbyUpdateFromSnapshot] Member left lobby without giving reason: Lobby: %s, Member: %s"),
						*ToLogString(PublicData->LobbyId), *ToLogString(MemberData.Key));
				}

				LeavingMembers.Emplace(MemberData.Value, ResovedReason);
			}
		}

		// Remove member data and notify.
		for (const TPair<TSharedRef<FClientLobbyMemberSnapshot>, ELobbyMemberLeaveReason>& MemberData : LeavingMembers)
		{
			MemberDataStorage.Remove(MemberData.Key->AccountId);
			PublicData->Members.Remove(MemberData.Key->AccountId);

			if (MemberData.Key->bIsLocalMember)
			{
				LocalMembers.Remove(MemberData.Key->AccountId);
				Result.LeavingLocalMembers.Add(MemberData.Key->AccountId);
			}

			if (bDispatchNotifications)
			{
				LobbyEvents->OnLobbyMemberLeft.Broadcast(FLobbyMemberLeft{PublicData, MemberData.Key, MemberData.Value});
			}
		}
	}

	// Process member joins and attribute changes.
	// Joining members are expected to have a member data snapshot.
	for (TPair<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>>& MemberSnapshot : LobbyMemberSnapshots)
	{
		if (LobbySnapshot.Members.Find(MemberSnapshot.Key) == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FClientLobbyData::ApplyLobbyUpdateFromSnapshot] Member update ignored for unknown member: Lobby: %s, Member: %s"),
				*ToLogString(PublicData->LobbyId), *ToLogString(MemberSnapshot.Key));
		}
		else
		{
			// Check whether this member is already in the lobby.
			if (TSharedRef<FClientLobbyMemberSnapshot>* MemberData = MemberDataStorage.Find(MemberSnapshot.Key))
			{
				// Member is already in the lobby. Check if their attributes changed.
				TSet<FLobbyAttributeId> MemberAttributeChanges = ApplyAttributeUpdateFromSnapshot(MoveTemp(MemberSnapshot.Value->Attributes), (*MemberData)->Attributes);
				if (bDispatchNotifications && !MemberAttributeChanges.IsEmpty())
				{
					LobbyEvents->OnLobbyMemberAttributesChanged.Broadcast(FLobbyMemberAttributesChanged{PublicData, (*MemberData), MemberAttributeChanges});
				}
			}
			else
			{
				// Member is not in the lobby, add them.
				MemberDataStorage.Add(MemberSnapshot.Key, MemberSnapshot.Value);
				PublicData->Members.Add(MemberSnapshot.Key, MoveTemp(MemberSnapshot.Value));
				if (bDispatchNotifications)
				{
					LobbyEvents->OnLobbyMemberJoined.Broadcast(FLobbyMemberJoined{PublicData, MemberSnapshot.Value});
				}
			}
		}
	}

	// Process lobby state.
	// The lobby owner notification must be processed after member updates as the new owner may not
	// have existed in the previous snapshot.
	if (PublicData->OwnerAccountId != LobbySnapshot.OwnerAccountId)
	{
		if (TSharedRef<FClientLobbyMemberSnapshot>* MemberData = MemberDataStorage.Find(LobbySnapshot.OwnerAccountId))
		{
			PublicData->OwnerAccountId = LobbySnapshot.OwnerAccountId;
			if (bDispatchNotifications)
			{
				LobbyEvents->OnLobbyLeaderChanged.Broadcast(FLobbyLeaderChanged{PublicData, (*MemberData)});
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FClientLobbyData::ApplyLobbyUpdateFromSnapshot] Lobby owner chage failed - lobby member data not found: Lobby: %s, Member: %s"),
				*ToLogString(PublicData->LobbyId), *ToLogString(LobbySnapshot.OwnerAccountId));
		}
	}

	// Set remaining lobby data.
	PublicData->MaxMembers = LobbySnapshot.MaxMembers;
	PublicData->JoinPolicy = LobbySnapshot.JoinPolicy;

	// If no local members remain in the lobby process lobby removal.
	if (bDispatchNotifications && LocalMembers.IsEmpty())
	{
		TArray<TSharedRef<FClientLobbyMemberSnapshot>> LeavingMembers;
		MemberDataStorage.GenerateValueArray(LeavingMembers);

		for (TSharedRef<FClientLobbyMemberSnapshot>& LeavingMember : LeavingMembers)
		{
			MemberDataStorage.Remove(LeavingMember->AccountId);
			PublicData->Members.Remove(LeavingMember->AccountId);
			LobbyEvents->OnLobbyMemberLeft.Broadcast(FLobbyMemberLeft{PublicData, LeavingMember, ELobbyMemberLeaveReason::Left});
		}

		LobbyEvents->OnLobbyLeft.Broadcast(FLobbyLeft{PublicData});
	}

	return Result;
}

FApplyLobbyUpdateResult FClientLobbyData::ApplyLobbyUpdateFromLocalChanges(FClientLobbyDataChanges&& Changes, FLobbyEvents& LobbyEvents)
{
	FApplyLobbyUpdateResult Result;
	const bool bDispatchLobbyNotifications = !LocalMembers.IsEmpty();

	// Process lobby changes.
	{
		if (Changes.LocalName)
		{
			PublicData->LocalName = *Changes.LocalName;
		}

		// Join policy.
		if (Changes.JoinPolicy)
		{
			PublicData->JoinPolicy = *Changes.JoinPolicy;
		}

		// Lobby owner.
		if (Changes.OwnerAccountId && *Changes.OwnerAccountId != PublicData->OwnerAccountId)
		{
			if (TSharedRef<FClientLobbyMemberSnapshot>* MemberData = MemberDataStorage.Find(*Changes.OwnerAccountId))
			{
				PublicData->OwnerAccountId = *Changes.OwnerAccountId;
				if (bDispatchLobbyNotifications)
				{
					LobbyEvents.OnLobbyLeaderChanged.Broadcast(FLobbyLeaderChanged{PublicData, (*MemberData)});
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FClientLobbyData::ApplyLobbyUpdateFromLocalChanges] Lobby owner chage failed - lobby member data not found: Lobby: %s, Member: %s"),
					*ToLogString(PublicData->LobbyId), *ToLogString(*Changes.OwnerAccountId));
			}
		}

		// Lobby schema.
		if (Changes.LobbySchema && *Changes.LobbySchema != PublicData->SchemaName)
		{
			PublicData->SchemaName = *Changes.LobbySchema;
			if (bDispatchLobbyNotifications)
			{
				LobbyEvents.OnLobbySchemaChanged.Broadcast(FLobbySchemaChanged{PublicData});
			}
		}

		// Apply lobby attribute changes.
		if (!Changes.MutatedAttributes.IsEmpty() || !Changes.ClearedAttributes.IsEmpty())
		{
			TSet<FLobbyAttributeId> ChangedLobbyAttributes = ApplyAttributeUpdateFromChanges(
				MoveTemp(Changes.MutatedAttributes),
				MoveTemp(Changes.ClearedAttributes),
				PublicData->Attributes);

			if (bDispatchLobbyNotifications && !ChangedLobbyAttributes.IsEmpty())
			{
				LobbyEvents.OnLobbyAttributesChanged.Broadcast(FLobbyAttributesChanged{PublicData, ChangedLobbyAttributes});
			}
		}
	}

	// Apply member changes.
	{
		// Adding local members will make the lobby active. When the lobby moves from inactive to
		// active join events must be generated for the lobby and each of its existing members.
		if (!Changes.MutatedMembers.IsEmpty())
		{
			// When adding members for the first time, notify for lobby joined then notify lobby member joined for each existing member.
			if (LocalMembers.IsEmpty())
			{
				LobbyEvents.OnLobbyJoined.Broadcast(FLobbyJoined{PublicData});

				for (TPair<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberSnapshot>>& MemberData : MemberDataStorage)
				{
					// The lobby creator will already be in the list during lobby creation due to applying a lobby snapshot.
					if (Changes.MutatedMembers.Find(MemberData.Key) == nullptr)
					{
						LobbyEvents.OnLobbyMemberJoined.Broadcast(FLobbyMemberJoined{PublicData, MemberData.Value});
					}
				}
			}

			// Create or modify state for each mutated member and notify.
			// The lobby creator will already be in the list during lobby creation due to applying a lobby snapshot.
			for (TPair<FOnlineAccountIdHandle, TSharedRef<FClientLobbyMemberDataChanges>>& MutatedMemberPair : Changes.MutatedMembers)
			{
				if (TSharedRef<FClientLobbyMemberSnapshot>* MemberDataPtr = MemberDataStorage.Find(MutatedMemberPair.Key))
				{
					const bool bIsJoinedLocalMember = LocalMembers.Find(MutatedMemberPair.Key) != nullptr;

					TSharedRef<FClientLobbyMemberSnapshot>& MemberData = *MemberDataPtr;
					TSet<FLobbyAttributeId> ChangedAttributes = ApplyAttributeUpdateFromChanges(
						MoveTemp(MutatedMemberPair.Value->MutatedAttributes),
						MoveTemp(MutatedMemberPair.Value->ClearedAttributes),
						MemberData->Attributes);

					if (bIsJoinedLocalMember && !ChangedAttributes.IsEmpty())
					{
						LobbyEvents.OnLobbyMemberAttributesChanged.Broadcast(FLobbyMemberAttributesChanged{PublicData, MemberData, ChangedAttributes});
					}

					if (!bIsJoinedLocalMember)
					{
						MemberData->bIsLocalMember = true;
						LocalMembers.Add(MemberData->AccountId);
						LobbyEvents.OnLobbyMemberJoined.Broadcast(FLobbyMemberJoined{PublicData, MemberData});
					}
				}
				else
				{
					TSharedRef<FClientLobbyMemberSnapshot> JoiningMember = MakeShared<FClientLobbyMemberSnapshot>();
					JoiningMember->bIsLocalMember = true;
					JoiningMember->AccountId = MutatedMemberPair.Key;
					JoiningMember->Attributes = MoveTemp(MutatedMemberPair.Value->MutatedAttributes);

					// todo:
					//NewMember->PlatformAccountId;
					//NewMember->PlatformDisplayName;

					MemberDataStorage.Add(JoiningMember->AccountId, JoiningMember);
					PublicData->Members.Add(JoiningMember->AccountId, JoiningMember);
					LocalMembers.Add(JoiningMember->AccountId);
					LobbyEvents.OnLobbyMemberJoined.Broadcast(FLobbyMemberJoined{PublicData, JoiningMember});
				}
			}
		}

		// Handle leaving members.
		// When the last local member leaves, leave events will be generated for each of the
		// remaining lobby members and for the lobby itself.
		if (!Changes.LeavingMembers.IsEmpty())
		{
			for (TPair<FOnlineAccountIdHandle, ELobbyMemberLeaveReason>& LeavingMemberPair : Changes.LeavingMembers)
			{
				// Member data is expected to be added through changes before being mutated.
				TSharedRef<FClientLobbyMemberSnapshot>* MemberDataPtr = MemberDataStorage.Find(LeavingMemberPair.Key);
				if (ensure(MemberDataPtr))
				{
					TSharedRef<FClientLobbyMemberSnapshot> MemberData = *MemberDataPtr;
					if (MemberData->bIsLocalMember)
					{
						Result.LeavingLocalMembers.Add(LeavingMemberPair.Key);
						LocalMembers.Remove(LeavingMemberPair.Key);
					}

					MemberDataStorage.Remove(LeavingMemberPair.Key);
					PublicData->Members.Remove(LeavingMemberPair.Key);
					LobbyEvents.OnLobbyMemberLeft.Broadcast(FLobbyMemberLeft{PublicData, MemberData, LeavingMemberPair.Value});
				}
			}

			// If no local members remain in the lobby process lobby removal.
			if (LocalMembers.IsEmpty())
			{
				TArray<TSharedRef<FClientLobbyMemberSnapshot>> LeavingMembers;
				MemberDataStorage.GenerateValueArray(LeavingMembers);

				for (TSharedRef<FClientLobbyMemberSnapshot>& LeavingMember : LeavingMembers)
				{
					MemberDataStorage.Remove(LeavingMember->AccountId);
					PublicData->Members.Remove(LeavingMember->AccountId);
					LobbyEvents.OnLobbyMemberLeft.Broadcast(FLobbyMemberLeft{PublicData, LeavingMember, ELobbyMemberLeaveReason::Left});
				}

				LobbyEvents.OnLobbyLeft.Broadcast(FLobbyLeft{PublicData});
			}
		}
	}

	return Result;
}

TSet<FLobbyAttributeId> FClientLobbyData::ApplyAttributeUpdateFromSnapshot(
	TMap<FLobbyAttributeId, FLobbyVariant>&& AttributeSnapshot,
	TMap<FLobbyAttributeId, FLobbyVariant>& ExistingAttributes)
{
	TSet<FLobbyAttributeId> Result;

	// Clear the attributes.
	{
		// Determine which attributes were removed.
		for (const TPair<FLobbyAttributeId, FLobbyVariant>& Attribute : ExistingAttributes)
		{
			if (!AttributeSnapshot.Contains(Attribute.Key))
			{
				Result.Add(Attribute.Key);
			}
		}

		// Remove them.
		for (const FLobbyAttributeId& AttributeId : Result)
		{
			ExistingAttributes.Remove(AttributeId);
		}
	}

	// Apply attribute additions and mutations.
	{
		for (TPair<FLobbyAttributeId, FLobbyVariant>& Attribute : AttributeSnapshot)
		{
			if (FLobbyVariant* ExistingAttributeData = ExistingAttributes.Find(Attribute.Key))
			{
				if (*ExistingAttributeData != Attribute.Value)
				{
					Result.Add(MoveTemp(Attribute.Key));
					*ExistingAttributeData = MoveTemp(Attribute.Value);
				}
			}
			else
			{
				Result.Add(Attribute.Key);
				ExistingAttributes.Add(MoveTemp(Attribute));
			}
		}
	}

	return Result;
}

TSet<FLobbyAttributeId> FClientLobbyData::ApplyAttributeUpdateFromChanges(
	TMap<FLobbyAttributeId, FLobbyVariant>&& MutatedAttributes,
	TSet<FLobbyAttributeId>&& ClearedAttributes,
	TMap<FLobbyAttributeId, FLobbyVariant>& ExistingAttributes)
{
	TSet<FLobbyAttributeId> Result;

	// Apply attribute additions and mutations.
	{
		for (TPair<FLobbyAttributeId, FLobbyVariant>& Attribute : MutatedAttributes)
		{
			if (FLobbyVariant* ExistingAttributeData = ExistingAttributes.Find(Attribute.Key))
			{
				if (*ExistingAttributeData != Attribute.Value)
				{
					Result.Add(MoveTemp(Attribute.Key));
					*ExistingAttributeData = MoveTemp(Attribute.Value);
				}
			}
			else
			{
				Result.Add(Attribute.Key);
				ExistingAttributes.Add(MoveTemp(Attribute));
			}
		}
	}

	// Clear the attributes.
	{
		for (FLobbyAttributeId ClearedAttributeId : ClearedAttributes)
		{
			if (ExistingAttributes.Remove(ClearedAttributeId) > 0)
			{
				Result.Add(ClearedAttributeId);
			}
		}
	}

	return Result;
}

/* UE::Online */ }
