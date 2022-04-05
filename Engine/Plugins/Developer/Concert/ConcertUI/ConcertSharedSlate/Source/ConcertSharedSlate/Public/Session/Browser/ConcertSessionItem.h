// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"

/** Signal emitted when a session name text field should enter in edit mode. */
DECLARE_MULTICAST_DELEGATE(FOnBeginEditConcertSessionNameRequest)

/**
 * Item displayed in the session list view.
 */
class CONCERTSHAREDSLATE_API FConcertSessionItem
{
public:
	enum class EType
	{
		None,
		NewSession,      // Editable item to enter a session name and a pick a server.
		RestoreSession,  // Editable item to name the restored session.
		SaveSession,     // Editable item to name the archive.
		ActiveSession,   // Read-only item representing an active session.
		ArchivedSession, // Read-only item representing an archived session.
	};

	bool operator==(const FConcertSessionItem& Other) const
	{
		return Type == Other.Type && ServerAdminEndpointId == Other.ServerAdminEndpointId && SessionId == Other.SessionId;
	}

	FConcertSessionItem MakeCopyAsType(EType NewType) const
	{
		FConcertSessionItem NewItem = *this;
		NewItem.Type = NewType;
		return NewItem;
	}

	EType Type = EType::None;
	FGuid ServerAdminEndpointId;
	FGuid SessionId;
	FString SessionName;
	FString ServerName;
	FString ProjectName;
	FString ProjectVersion;
	EConcertServerFlags ServerFlags = EConcertServerFlags::None;

	FOnBeginEditConcertSessionNameRequest OnBeginEditSessionNameRequest; // Emitted when user press 'F2' or select 'Rename' from context menu.
};
