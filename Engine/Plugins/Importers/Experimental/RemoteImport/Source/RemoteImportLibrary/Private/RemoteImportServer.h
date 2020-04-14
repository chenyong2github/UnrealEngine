// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MessageEndpoint.h"

struct FPingMessage;
struct FImportFileRequest;
class IMessageContext;


/**
 * Scopes the MessageBus endpoint, i.e. the ability to receive commands from RemoteImport clients.
 */
class FRemoteImportServer
{
public:
	FRemoteImportServer();

	~FRemoteImportServer();

private:
	void OnPingMessage(const FPingMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	void OnImportCommandMessage(const FImportFileRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	void PublishAnchorList();

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	FDelegateHandle OnAnchorChangeDelegateHandle;
};
