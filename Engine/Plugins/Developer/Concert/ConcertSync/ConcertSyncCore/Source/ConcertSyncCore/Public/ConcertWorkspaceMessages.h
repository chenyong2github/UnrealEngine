// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertSyncSessionTypes.h"
#include "ConcertWorkspaceMessages.generated.h"

USTRUCT()
struct FConcertWorkspaceSyncEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumRemainingSyncEvents = 0;
};

USTRUCT()
struct FConcertWorkspaceSyncEndpointEvent : public FConcertWorkspaceSyncEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	FConcertSyncEndpointIdAndData Endpoint;
};

USTRUCT()
struct FConcertWorkspaceSyncActivityEvent : public FConcertWorkspaceSyncEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	FConcertSessionSerializedPayload Activity;
};

USTRUCT()
struct FConcertWorkspaceSyncLockEvent : public FConcertWorkspaceSyncEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, FGuid> LockedResources;
};

USTRUCT()
struct FConcertWorkspaceSyncRequestedEvent
{
	GENERATED_BODY()

	/** The ID of the first activity to sync */
	UPROPERTY()
	int64 FirstActivityIdToSync = 1;

	/** The ID of the last activity to sync (ignored if bEnableLiveSync is true) */
	UPROPERTY()
	int64 LastActivityIdToSync = MAX_int64;

	/** True if the server workspace should be live-synced to this client as new activity is added, or false if syncing should only happen in response to these sync request events */
	UPROPERTY()
	bool bEnableLiveSync = true;
};

USTRUCT()
struct FConcertWorkspaceSyncCompletedEvent
{
	GENERATED_BODY()
};

USTRUCT()
struct FConcertPackageUpdateEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FConcertPackage Package;
};

USTRUCT()
struct FConcertPackageRejectedEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FName PackageName;
};

UENUM()
enum class EConcertResourceLockType : uint8
{
	None,
	Lock,
	Unlock,
};

USTRUCT()
struct FConcertResourceLockEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ClientId;

	UPROPERTY()
	TArray<FName> ResourceNames;

	UPROPERTY()
	EConcertResourceLockType LockType;
};

USTRUCT()
struct FConcertResourceLockRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ClientId;

	UPROPERTY()
	TArray<FName> ResourceNames;

	UPROPERTY()
	EConcertResourceLockType LockType;
};

USTRUCT()
struct FConcertResourceLockResponse
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, FGuid> FailedResources;

	UPROPERTY()
	EConcertResourceLockType LockType;
};

UENUM()
enum class EConcertPlaySessionEventType : uint8
{
	BeginPlay,
	SwitchPlay,
	EndPlay,
};

USTRUCT()
struct FConcertPlaySessionEvent
{
	GENERATED_BODY()

	UPROPERTY()
	EConcertPlaySessionEventType EventType;

	UPROPERTY()
	FGuid PlayEndpointId;

	UPROPERTY()
	FName PlayPackageName;

	UPROPERTY()
	bool bIsSimulating = false;
};
