// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSource.h"

#include "HAL/ThreadSafeBool.h"
#include "IMessageContext.h"
#include "LiveLinkRole.h"
#include "MessageEndpoint.h"

class ILiveLinkClient;
struct FLiveLinkPongMessage;
struct FLiveLinkSubjectDataMessage;
struct FLiveLinkSubjectFrameMessage;
struct FLiveLinkHeartbeatMessage;
struct FLiveLinkClearSubject;

class LIVELINK_API FLiveLinkMessageBusSource : public ILiveLinkSource
{
public:

	FLiveLinkMessageBusSource(const FText& InSourceType, const FText& InSourceMachineName, const FMessageAddress& InConnectionAddress)
		: ConnectionAddress(InConnectionAddress)
		, SourceType(InSourceType)
		, SourceMachineName(InSourceMachineName)
		, ConnectionLastActive(0.0)
		, bIsValid(false)
		, bInvalidSubjectNameErrorReported(false)
		, bInvalidRoleErrorReported(false)
	{}

	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual void Update() override;

	virtual bool IsSourceValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override { return SourceType; }
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override;

private:
	//~ Message bus message handlers
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	void HandleSubjectData(const FLiveLinkSubjectDataMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleSubjectFrame(const FLiveLinkSubjectFrameMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	void HandleHeartbeat(const FLiveLinkHeartbeatMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleClearSubject(const FLiveLinkClearSubject& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void InternalHandleMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	//~ End Message bus message handlers

	// Threadsafe update of the last active time
	FORCEINLINE void UpdateConnectionLastActive();

	void SendConnectMessage();

	ILiveLinkClient* Client;

	// Our identifier in LiveLink
	FGuid SourceGuid;

	// List of the roles available when the bus was opened
	TArray<TWeakObjectPtr<ULiveLinkRole>> RoleInstances;

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	FMessageAddress ConnectionAddress;

	FText SourceType;
	FText SourceMachineName;

	// Time we last received anything 
	double ConnectionLastActive;

	// Current Validity of Source
	FThreadSafeBool bIsValid;

	// Critical section to allow for threadsafe updating of the connection time
	FCriticalSection ConnectionLastActiveSection;

	// Flag to prevent log spamming. Only log an error once.
	FThreadSafeBool bInvalidSubjectNameErrorReported;
	FThreadSafeBool bInvalidRoleErrorReported;
};