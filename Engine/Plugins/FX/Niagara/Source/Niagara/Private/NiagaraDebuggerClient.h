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
	FNiagaraDebuggerClient();
	~FNiagaraDebuggerClient();

private:

	void HandleConnectionRequestMessage(const FNiagaraDebuggerRequestConnection& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleConnectionClosedMessage(const FNiagaraDebuggerConnectionClosed& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleExecConsoleCommandMessage(const FNiagaraDebuggerExecuteConsoleCommand& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);
	void HandleDebugHUDSettingsMessage(const FNiagaraDebugHUDSettingsData& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	/** Closes any currently active connection. */
	void CloseConnection();

	/** Holds the session and instance identifier. */
	FGuid SessionId;
	FGuid InstanceId;
	FString InstanceName;

	/** Holds the messaging endpoint. */
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	/** The address of the connected debugger, if any. */
	FMessageAddress Connection;

	void ExecuteConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld);
};

// 
// /** Tracks all info about an individual Niagara System Instance. */
// struct FNiagaraSceneOutliner_InstanceInfo
// {
// 	/** Name of the system asset for this instance. */
// 	UPROPERTY(EditAnywhere, Category="Instance")
// 	FName SystemName;
// 
// 	/** Name of the component for this instance. */
// 	UPROPERTY(EditAnywhere, Category = "Instance")
// 	FName ComponentName;
// 
// 	/** Name of the system asset for this instance. */
// 	UPROPERTY(EditAnywhere, Category = "Instance")
// 	FNiagaraScalabilityState ScalabilityState;
// 
// 	UPROPERTY(EditAnywhere, Category = "Instance")
// 	int32 ActiveEmitters;
// 	
// 	UPROPERTY(EditAnywhere, Category = "Instance")
// 	int32 TotalParticles;
// 	
// 	UPROPERTY(EditAnywhere, Category = "Instance")
// 	float LastRenderTime;
// };

#endif