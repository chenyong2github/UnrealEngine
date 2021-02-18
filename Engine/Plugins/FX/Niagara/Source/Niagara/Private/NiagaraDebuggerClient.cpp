// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebuggerClient.h"

#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "IMessageContext.h"
#include "NiagaraWorldManager.h"
#include "NiagaraDebugHud.h"

#if WITH_NIAGARA_DEBUGGER

DEFINE_LOG_CATEGORY(LogNiagaraDebuggerClient);

FNiagaraDebuggerClient::FNiagaraDebuggerClient()
{
	MessageEndpoint = FMessageEndpoint::Builder("FNiagaraDebuggerClient")
		.Handling<FNiagaraDebuggerRequestConnection>(this, &FNiagaraDebuggerClient::HandleConnectionRequestMessage)
		.Handling<FNiagaraDebuggerConnectionClosed>(this, &FNiagaraDebuggerClient::HandleConnectionClosedMessage)
		.Handling<FNiagaraDebuggerExecuteConsoleCommand>(this, &FNiagaraDebuggerClient::HandleExecConsoleCommandMessage)
 		.Handling<FNiagaraDebugHUDSettingsData>(this, &FNiagaraDebuggerClient::HandleDebugHUDSettingsMessage);

 	if (MessageEndpoint.IsValid())
 	{
		MessageEndpoint->Subscribe<FNiagaraDebuggerRequestConnection>();
		MessageEndpoint->Subscribe<FNiagaraDebuggerConnectionClosed>();
 	}

	SessionId = FApp::GetSessionId();
	InstanceId = FApp::GetInstanceId();
	InstanceName = FApp::GetInstanceName();
	UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Niagara Debugger Client Initialized | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);
}

FNiagaraDebuggerClient::~FNiagaraDebuggerClient()
{
	CloseConnection();
}

void FNiagaraDebuggerClient::HandleConnectionRequestMessage(const FNiagaraDebuggerRequestConnection& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	if (MessageEndpoint.IsValid() && Message.SessionId == SessionId && Message.InstanceId == InstanceId)
	{
		if (Connection.IsValid())
		{
			UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Connection Request Recieved but we already have a connected debugger. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);
		}
		else
		{
			UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Connection Request Recieved and Accepted. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);
			//Accept the connection and inform the debugger we have done so with an accepted message.
			Connection = Context->GetSender();
			MessageEndpoint->Send(new FNiagaraDebuggerAcceptConnection(SessionId, InstanceId), Connection);
		}
	}
}

void FNiagaraDebuggerClient::HandleConnectionClosedMessage(const FNiagaraDebuggerConnectionClosed& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	if (MessageEndpoint.IsValid() && Message.SessionId == SessionId && Message.InstanceId == InstanceId)
	{
		if (Connection == Context->GetSender())
		{
			UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Connection Closed. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);
			Connection.Invalidate();
		}
		else
		{
			UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Recieved connection closed message for unconnected debugger. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);
		}
	}
}

void FNiagaraDebuggerClient::HandleExecConsoleCommandMessage(const FNiagaraDebuggerExecuteConsoleCommand& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	if (ensure(Context->GetSender() == Connection))
	{
		UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Executing console command. %s | Session: %s | Instance: %s (%s)."), *Message.Command, *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);
		ExecuteConsoleCommand(*Message.Command, Message.bRequiresWorld);
	}
}

void FNiagaraDebuggerClient::HandleDebugHUDSettingsMessage(const FNiagaraDebugHUDSettingsData& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	if (ensure(Context->GetSender() == Connection))
	{
		UE_LOG(LogNiagaraDebuggerClient, Log, TEXT("Received updated DebugHUD settings. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *InstanceName);

		//Pass along the new settings.
		auto ApplySettingsToWorldMan = [&Message](FNiagaraWorldManager& WorldMan)
		{
			WorldMan.GetNiagaraDebugHud()->UpdateSettings(Message);

			//TODO: Move these to just take direct from the debug hud per worldman?
			//Possbly move the debug hud itself to the debugger client rather than having one per world man and they all share global state.
			WorldMan.SetDebugPlaybackMode(Message.PlaybackMode);
			WorldMan.SetDebugPlaybackRate(Message.bPlaybackRateEnabled ? Message.PlaybackRate : 1.0f);
		};

		FNiagaraWorldManager::ForAllWorldManagers(ApplySettingsToWorldMan);
			
		//TODO: Move usage to come direct from settings struct instead of this CVar.
		ExecuteConsoleCommand(*FString::Printf(TEXT("fx.Niagara.Debug.GlobalLoopTime %.3f"), Message.bLoopTimeEnabled && Message.PlaybackMode == ENiagaraDebugPlaybackMode::Loop ? Message.LoopTime : 0.0f), true);
	}
}

void FNiagaraDebuggerClient::CloseConnection()
{
	if (MessageEndpoint.IsValid() && Connection.IsValid())
	{
		MessageEndpoint->Send(new FNiagaraDebuggerConnectionClosed(SessionId, InstanceId), Connection);
		Connection.Invalidate();
	}
}

void FNiagaraDebuggerClient::ExecuteConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld)
{
	if (bRequiresWorld)
	{
		for (TObjectIterator<UWorld> WorldIt; WorldIt; ++WorldIt)
		{
			UWorld* World = *WorldIt;
			if ((World != nullptr) &&
				(World->PersistentLevel != nullptr) &&
				(World->PersistentLevel->OwningWorld == World) &&
				((World->GetNetMode() == ENetMode::NM_Client) || (World->GetNetMode() == ENetMode::NM_Standalone)))
			{
				GEngine->Exec(*WorldIt, Cmd);
			}
		}
	}
	else
	{
		GEngine->Exec(nullptr, Cmd);
	}
}

#endif//WITH_NIAGARA_DEBUGGER