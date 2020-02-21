// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RemoteImportMessages.generated.h"

/**
 * Server handle Ping messages by answering a Pong message.
 * Useful to test client/server connectivity
 */
USTRUCT()
struct FPingMessage
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 1;
};


/**
 * Server should send this message on client's Ping messages
 */
USTRUCT()
struct FPongMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FString Acknowledgment;

	FPongMessage(const FString& Msg={})
		: Acknowledgment(FString(TEXT("[ack]:")) + Msg)
	{}
};


/**
 * Servers should send this message to notify an internal state change.
 * When anchors are added/removed, the server should notify clients.
 */
USTRUCT()
struct FServerStateMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FString ServerGUID;

	UPROPERTY()
	TArray<FString> Anchors;
};

/**
 * Client requests inherit from this message
 */
USTRUCT()
struct FRequestMessage
{
	GENERATED_BODY()

	UPROPERTY()
	int32 RequestID = -1;
};

/**
 * Server responses inherit from this message
 */
USTRUCT()
struct FResponseMessage
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SourceRequestID;

	FResponseMessage(const FRequestMessage* SourceRequest=nullptr)
		: SourceRequestID(SourceRequest ? SourceRequest->RequestID : -1)
	{
	}
};


/**
 * Client request to import a file in the designated anchor destination
 */
USTRUCT()
struct FImportFileRequest : public FRequestMessage
{
	GENERATED_BODY()

	UPROPERTY()
	FString File;

	UPROPERTY()
	FString Destination;
};


/**
 * Server response to an import file request
 */
USTRUCT()
struct FImportFileResponse : public FResponseMessage
{
	GENERATED_BODY()

	FImportFileResponse(const FImportFileRequest* Request=nullptr)
		: FResponseMessage(Request)
	{}
};

