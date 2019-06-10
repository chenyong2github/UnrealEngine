// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"

#include "ConcertTransportMessages.h"
#include "ConcertMessageData.h"
#include "ConcertMessages.generated.h"

/** Connection status for Concert client sessions */
UENUM()
enum class EConcertConnectionStatus : uint8
{
	/** Currently establishing connection to the server session */
	Connecting,
	/** Connection established and alive */
	Connected,
	/** Currently severing connection to the server session gracefully */
	Disconnecting,
	/** Disconnected */
	Disconnected,
};

/** Connection Result for Concert client session */
UENUM()
enum class EConcertConnectionResult : uint8
{
	/** Server has accepted connection */
	ConnectionAccepted,
	/** Server has refused the connection session messages beside other connection request are ignored */
	ConnectionRefused,
	/** Server already accepted connection */
	AlreadyConnected
};

/** Status for Concert session clients */
UENUM()
enum class EConcertClientStatus : uint8
{
	/** Client connected */
	Connected,
	/** Client disconnected */
	Disconnected,
	/** Client state updated */
	Updated,
};

/** Response codes for a session custom request */
UENUM()
enum class EConcertSessionResponseCode : uint8
{
	/** The request data was valid. A response was generated. */
	Success,
	/** The request data was valid, but the request failed. A response was generated. */
	Failed,
	/** The request data was invalid. No response was generated. */
	InvalidRequest,
};

USTRUCT()
struct FConcertAdmin_DiscoverServersEvent : public FConcertEndpointDiscoveryEvent
{
	GENERATED_BODY()

	/** The required role of the server (eg, MultiUser, DisasterRecovery, etc) */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString RequiredRole;

	/** The required version of the server (eg, 4.22, 4.23, etc) */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString RequiredVersion;
};

USTRUCT()
struct FConcertAdmin_ServerDiscoveredEvent : public FConcertEndpointDiscoveryEvent
{
	GENERATED_BODY()

	/** Server designated name */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString ServerName;

	/** Basic information about the server instance */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertInstanceInfo InstanceInfo;

	/** Contains information on the server settings */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	EConcertSeverFlags ServerFlags;
};

USTRUCT()
struct FConcertAdmin_GetAllSessionsRequest : public FConcertRequestData
{
	GENERATED_BODY()
};

USTRUCT()
struct FConcertAdmin_GetAllSessionsResponse : public FConcertResponseData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionInfo> LiveSessions;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionInfo> ArchivedSessions;
};

USTRUCT()
struct FConcertAdmin_GetLiveSessionsRequest : public FConcertRequestData
{
	GENERATED_BODY()
};

USTRUCT()
struct FConcertAdmin_GetArchivedSessionsRequest : public FConcertRequestData
{
	GENERATED_BODY()
};

USTRUCT()
struct FConcertAdmin_GetSessionsResponse : public FConcertResponseData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionInfo> Sessions;
};

USTRUCT()
struct FConcertAdmin_CreateSessionRequest : public FConcertRequestData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FString SessionName;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertClientInfo OwnerClientInfo;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionSettings SessionSettings;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionVersionInfo VersionInfo;
};

USTRUCT()
struct FConcertAdmin_FindSessionRequest : public FConcertRequestData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionId;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertClientInfo OwnerClientInfo;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionSettings SessionSettings;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionVersionInfo VersionInfo;
};

USTRUCT()
struct FConcertAdmin_RestoreSessionRequest : public FConcertRequestData
{
	GENERATED_BODY()

	/** The ID of the session to restore (must be an archived session). */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionId;

	/** The name of the restored session to create. */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FString SessionName;

	/** Information about the owner of the restored session. */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertClientInfo OwnerClientInfo;

	/** Settings to apply to the restored session. */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionSettings SessionSettings;

	/** Version information of the client requesting the restore. */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionVersionInfo VersionInfo;

	/** The filter controlling which activities from the session should be restored. */
	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionFilter SessionFilter;
};

USTRUCT()
struct FConcertAdmin_SessionInfoResponse : public FConcertResponseData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionInfo SessionInfo; // TODO: Split session Id out of session info
};

/** Create an archived copy of a live session. */
USTRUCT()
struct FConcertAdmin_ArchiveSessionRequest : public FConcertRequestData
{
	GENERATED_BODY()

	/** The ID of the session to archive (must be a live session). */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid SessionId;

	/** The override for the archive. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString ArchiveNameOverride;

	/** The caller user name. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString UserName;

	/** The caller device name. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString DeviceName;

	/** The filter controlling which activities from the session should be archived. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FConcertSessionFilter SessionFilter;
};

USTRUCT()
struct FConcertAdmin_ArchiveSessionResponse : public FConcertResponseData
{
	GENERATED_BODY()

	/** The ID of the session that was requested to be archived. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid SessionId;

	/** The name of the session that was requested to be archived. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString SessionName;

	/** The ID of the new archived session (on success). */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid ArchiveId;

	/** The name of the new archived session (on success). */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString ArchiveName;
};

/** Rename a session. */
USTRUCT()
struct FConcertAdmin_RenameSessionRequest : public FConcertRequestData
{
	GENERATED_BODY()

	/** The ID of the session to rename. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid SessionId;

	/** The new session name. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString NewName;

	// For now only the user name and device name of the client is used to id him as the owner of a session
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString UserName;

	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString DeviceName;
};

USTRUCT()
struct FConcertAdmin_RenameSessionResponse : public FConcertResponseData
{
	GENERATED_BODY()

	/** The ID of the session that was requested to be renamed. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid SessionId;

	/** The old session name (if the session exist). */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString OldName;
};


/** Delete a live session. */
USTRUCT()
struct FConcertAdmin_DeleteSessionRequest : public FConcertRequestData
{
	GENERATED_BODY()

	/** The ID of the session to delete. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid SessionId;

	//For now only the user name and device name of the client is used to id him as the owner of a session
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString UserName;

	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString DeviceName;
};

USTRUCT()
struct FConcertAdmin_DeleteSessionResponse : public FConcertResponseData
{
	GENERATED_BODY()

	/** The ID of the session that was requested to be deleted. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FGuid SessionId;

	/** The name of the session that was was requested to be deleted. */
	UPROPERTY(VisibleAnywhere, Category = "Concert Message")
	FString SessionName;
};

USTRUCT()
struct FConcertAdmin_GetSessionClientsRequest : public FConcertRequestData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionId;
};

USTRUCT()
struct FConcertAdmin_GetSessionClientsResponse : public FConcertResponseData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionClientInfo> SessionClients;
};

USTRUCT()
struct FConcertAdmin_GetSessionActivitiesRequest : public FConcertRequestData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionId;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	int64 FromActivityId = 1;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	int64 ActivityCount = 1024;
};

USTRUCT()
struct FConcertAdmin_GetSessionActivitiesResponse : public FConcertResponseData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionSerializedPayload> Activities;
};

USTRUCT()
struct FConcertSession_DiscoverAndJoinSessionEvent : public FConcertEndpointDiscoveryEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionServerEndpointId;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertClientInfo ClientInfo;
};

USTRUCT()
struct FConcertSession_JoinSessionResultEvent : public FConcertEndpointDiscoveryEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionServerEndpointId;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	EConcertConnectionResult ConnectionResult;

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionClientInfo> SessionClients;
};

USTRUCT()
struct FConcertSession_LeaveSessionEvent : public FConcertEventData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FGuid SessionServerEndpointId;
};

USTRUCT()
struct FConcertSession_UpdateClientInfoEvent : public FConcertEventData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FConcertSessionClientInfo SessionClient;
};

USTRUCT()
struct FConcertSession_ClientListUpdatedEvent : public FConcertEventData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	TArray<FConcertSessionClientInfo> SessionClients;
};

USTRUCT()
struct FConcertSession_SessionRenamedEvent : public FConcertEventData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Concert Message")
	FString NewName;
};

USTRUCT()
struct FConcertSession_CustomEvent : public FConcertEventData
{
	GENERATED_BODY()

	virtual bool IsSafeToHandle() const override
	{
		return !(GIsSavingPackage || IsGarbageCollecting());
	}

	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	FGuid SourceEndpointId;

	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	TArray<FGuid> DestinationEndpointIds;

	/** The serialized payload that we're hosting. */
	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	FConcertSessionSerializedPayload SerializedPayload;
};

USTRUCT()
struct FConcertSession_CustomRequest : public FConcertRequestData
{
	GENERATED_BODY()

	virtual bool IsSafeToHandle() const override
	{
		return !(GIsSavingPackage || IsGarbageCollecting());
	}

	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	FGuid SourceEndpointId;

	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	FGuid DestinationEndpointId;

	/** The serialized payload that we're hosting. */
	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	FConcertSessionSerializedPayload SerializedPayload;
};

USTRUCT()
struct FConcertSession_CustomResponse : public FConcertResponseData
{
	GENERATED_BODY()

	virtual bool IsSafeToHandle() const override
	{
		return !(GIsSavingPackage || IsGarbageCollecting());
	}

	/** Set the internal Concert response code from the custom response code from the request handler */
	void SetResponseCode(const EConcertSessionResponseCode InResponseCode)
	{
		switch (InResponseCode)
		{
		case EConcertSessionResponseCode::Success:
			ResponseCode = EConcertResponseCode::Success;
			break;
		case EConcertSessionResponseCode::Failed:
			ResponseCode = EConcertResponseCode::Failed;
			break;
		case EConcertSessionResponseCode::InvalidRequest:
			ResponseCode = EConcertResponseCode::InvalidRequest;
			break;
		default:
			checkf(false, TEXT("Unknown EConcertSessionResponseCode!"));
			break;
		}
	}

	/** The serialized payload that we're hosting. */
	UPROPERTY(VisibleAnywhere, Category="Concert Custom Message")
	FConcertSessionSerializedPayload SerializedPayload;
};
