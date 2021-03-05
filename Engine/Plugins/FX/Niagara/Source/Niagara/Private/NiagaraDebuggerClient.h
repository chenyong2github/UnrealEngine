// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Controller class running on game clients that handles the passing of messages to a connected Niagara debugger.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraDebuggerCommon.h"
#include "IMessageContext.h"

class FMessageEndpoint;

#if WITH_NIAGARA_DEBUGGER

DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraDebuggerClient, Log, All);

class FNiagaraDebuggerClient
{
public:

	static FNiagaraDebuggerClient* Get();

	FNiagaraDebuggerClient();
	~FNiagaraDebuggerClient();

	bool Tick(float DeltaSeconds);

	void UpdateClientInfo();

private:

	void HandleConnectionRequestMessage(const FNiagaraDebuggerRequestConnection& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleConnectionClosedMessage(const FNiagaraDebuggerConnectionClosed& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleExecConsoleCommandMessage(const FNiagaraDebuggerExecuteConsoleCommand& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleDebugHUDSettingsMessage(const FNiagaraDebugHUDSettingsData& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleRequestSimpleClientInfoMessage(const FNiagaraRequestSimpleClientInfoMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleOutlinerSettingsMessage(const FNiagaraOutlinerSettings& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	/** Closes any currently active connection. */
	void CloseConnection();

	/** Handle any cleanup needed whether we close the connection or the client does. */
	void OnConnectionClosed();

	void ExecuteConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld);

	bool UpdateOutliner(float DeltaSeconds);

	/** Holds the session and instance identifier. */
	FGuid SessionId;
	FGuid InstanceId;
	FString InstanceName;

	/** Holds the messaging endpoint. */
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	/** The address of the connected debugger, if any. */
	FMessageAddress Connection;

	FNiagaraOutlinerSettings OutlinerSettings;
	FDelegateHandle TickerHandle;

	float OutlinerCountdown = 0.0f;
};

#endif