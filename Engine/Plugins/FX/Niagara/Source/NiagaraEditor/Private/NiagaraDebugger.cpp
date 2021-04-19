// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugger.h"
#include "Modules/ModuleManager.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "NiagaraDebuggerCommon.h"
#include "ISessionServicesModule.h"

#if WITH_NIAGARA_DEBUGGER

#define LOCTEXT_NAMESPACE "NiagaraDebugger"

DEFINE_LOG_CATEGORY(LogNiagaraDebugger);

FNiagaraDebugger::FNiagaraDebugger()
{
}

FNiagaraDebugger::~FNiagaraDebugger()
{
	if (SessionManager.IsValid())
	{
		SessionManager->OnSelectedSessionChanged().RemoveAll(this);
		SessionManager->OnInstanceSelectionChanged().RemoveAll(this);
	}

	GetMutableDefault<UNiagaraDebugHUDSettings>()->OnChangedDelegate.RemoveAll(this);
	GetMutableDefault<UNiagaraOutliner>()->OnChangedDelegate.RemoveAll(this);
}

void FNiagaraDebugger::Init()
{
	MessageEndpoint = FMessageEndpoint::Builder("SNiagaraDebugger")
		.Handling<FNiagaraDebuggerAcceptConnection>(this, &FNiagaraDebugger::HandleConnectionAcceptedMessage)
		.Handling<FNiagaraDebuggerConnectionClosed>(this, &FNiagaraDebugger::HandleConnectionClosedMessage)
		.Handling<FNiagaraDebuggerOutlinerUpdate>(this, &FNiagaraDebugger::HandleOutlinerUpdateMessage)
		.Handling<FNiagaraSimpleClientInfo>(this, &FNiagaraDebugger::UpdateSimpleClientInfo);

	ISessionServicesModule& SessionServicesModule = FModuleManager::LoadModuleChecked<ISessionServicesModule>("SessionServices");
	SessionManager = SessionServicesModule.GetSessionManager();

	if (SessionManager.IsValid())
	{
		SessionManager->OnSelectedSessionChanged().AddSP(this, &FNiagaraDebugger::SessionManager_OnSessionSelectionChanged);
		SessionManager->OnInstanceSelectionChanged().AddSP(this, &FNiagaraDebugger::SessionManager_OnInstanceSelectionChanged);
	}

	GetMutableDefault<UNiagaraDebugHUDSettings>()->OnChangedDelegate.AddSP(this, &FNiagaraDebugger::UpdateDebugHUDSettings);
	GetMutableDefault<UNiagaraOutliner>()->OnChangedDelegate.AddSP(this, &FNiagaraDebugger::TriggerOutlinerCapture);
}

void FNiagaraDebugger::ExecConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld)
{
	auto SendExecCommand = [&](FNiagaraDebugger::FClientInfo& Client)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Sending console command %s. | Session: %s | Instance: %s |"), Cmd, *Client.SessionId.ToString(), *Client.InstanceId.ToString());
		MessageEndpoint->Send(new FNiagaraDebuggerExecuteConsoleCommand(Cmd, bRequiresWorld), EMessageFlags::Reliable, nullptr, TArrayBuilder<FMessageAddress>().Add(Client.Address), FTimespan::Zero(), FDateTime::MaxValue());
	};

	ForAllConnectedClients(SendExecCommand);
}

void FNiagaraDebugger::UpdateDebugHUDSettings()
{
	//Send the current state as a message to all connected clients.
	if (const UNiagaraDebugHUDSettings* Settings = GetDefault<UNiagaraDebugHUDSettings>())
	{
		auto SendSettingsUpdate = [&](FNiagaraDebugger::FClientInfo& Client)
		{
			//Create the message and copy the current state of the settings into it.
			FNiagaraDebugHUDSettingsData* Message = new FNiagaraDebugHUDSettingsData();
			FNiagaraDebugHUDSettingsData::StaticStruct()->CopyScriptStruct(Message, &Settings->Data);

			UE_LOG(LogNiagaraDebugger, Log, TEXT("Sending updated debug HUD settings. | Session: %s | Instance: %s |"), *Client.SessionId.ToString(), *Client.InstanceId.ToString());
			MessageEndpoint->Send(Message, EMessageFlags::Reliable, nullptr, TArrayBuilder<FMessageAddress>().Add(Client.Address), FTimespan::Zero(), FDateTime::MaxValue());
		};

		ForAllConnectedClients(SendSettingsUpdate);
	}
}

void FNiagaraDebugger::RequestUpdatedClientInfo()
{
	auto RequestUpdate = [&](FNiagaraDebugger::FClientInfo& Client)
	{
		FNiagaraRequestSimpleClientInfoMessage* Message = new FNiagaraRequestSimpleClientInfoMessage();
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Requesting updated simple client info. | Session: %s | Instance: %s |"), *Client.SessionId.ToString(), *Client.InstanceId.ToString());
		MessageEndpoint->Send(Message, EMessageFlags::Reliable, nullptr, TArrayBuilder<FMessageAddress>().Add(Client.Address), FTimespan::Zero(), FDateTime::MaxValue());
	};

	ForAllConnectedClients(RequestUpdate);
}

void FNiagaraDebugger::TriggerOutlinerCapture()
{
	//Send the current settings as a message to all connected clients.
	if (UNiagaraOutliner* Outliner = GetMutableDefault<UNiagaraOutliner>())
	{
		if(Outliner->CaptureSettings.bTriggerCapture)
		{
			auto SendSettingsUpdate = [&](FNiagaraDebugger::FClientInfo& Client)
			{
				//Create the message and copy the current state of the settings into it.
				FNiagaraOutlinerCaptureSettings* Message = new FNiagaraOutlinerCaptureSettings();
				FNiagaraOutlinerCaptureSettings::StaticStruct()->CopyScriptStruct(Message, &Outliner->CaptureSettings);

				UE_LOG(LogNiagaraDebugger, Log, TEXT("Sending updated outliner settings. | Session: %s | Instance: %s |"), *Client.SessionId.ToString(), *Client.InstanceId.ToString());
				MessageEndpoint->Send(Message, EMessageFlags::Reliable, nullptr, TArrayBuilder<FMessageAddress>().Add(Client.Address), FTimespan::Zero(), FDateTime::MaxValue());
			};

			ForAllConnectedClients(SendSettingsUpdate);

			Outliner->CaptureSettings.bTriggerCapture = false;

			//We also need at least minimal debug hud verbosity so we see the countdown timer.
			if (Outliner->CaptureSettings.CaptureDelayFrames > 0)
			{
				if (UNiagaraDebugHUDSettings* Settings = GetMutableDefault<UNiagaraDebugHUDSettings>())
				{
					if (!Settings->Data.bEnabled)
					{
						Settings->Data.bEnabled = true;
						Settings->PostEditChange();
					}
				}
			}
		}
	}
}

void FNiagaraDebugger::SessionManager_OnSessionSelectionChanged(const TSharedPtr<ISessionInfo>& Session)
{
	//Drop all existing and pending connections when the session selection changes.
	TArray<FClientInfo> ToClose;
	if (Session.IsValid())
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Session selection changed. Dropping connections not from this session. | Session: %s (%s)"), *Session->GetSessionId().ToString(), *Session->GetSessionName());
		for (FClientInfo& Client : ConnectedClients)
		{
			if (Client.SessionId != Session->GetSessionId())
			{
				ToClose.Add(Client);
			}
		}
		for (FClientInfo& Client : PendingClients)
		{
			if (Client.SessionId != Session->GetSessionId())
			{
				ToClose.Add(Client);
			}
		}
	}
	else
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Session selection changed. Dropping all connections."));
		ToClose.Append(ConnectedClients);
		ToClose.Append(PendingClients);
	}

	for (FClientInfo& Client : ToClose)
	{
		CloseConnection(Client.SessionId, Client.InstanceId);
	}
}

void FNiagaraDebugger::SessionManager_OnInstanceSelectionChanged(const TSharedPtr<ISessionInstanceInfo>& Instance, bool Selected)
{
	if (MessageEndpoint.IsValid())
	{
		if (Selected)
		{
			const FGuid& SessionId = Instance->GetOwnerSession()->GetSessionId();
			const FGuid& InstanceId = Instance->GetInstanceId();

			int32 FoundActive = FindActiveConnection(SessionId, InstanceId);
			if (FoundActive != INDEX_NONE)
			{
				UE_LOG(LogNiagaraDebugger, Log, TEXT("Session Instance selection callback for existing active connection. Ignored. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *Instance->GetInstanceName());
				return;
			}

			int32 FoundPending = FindPendingConnection(SessionId, InstanceId);
			if (FoundPending != INDEX_NONE)
			{
				UE_LOG(LogNiagaraDebugger, Log, TEXT("Session Instance selection callback for existing pending connection. Ignored. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *Instance->GetInstanceName());
				return;
			}

			FNiagaraDebuggerRequestConnection* ConnectionRequestMessage = new FNiagaraDebuggerRequestConnection(SessionId, InstanceId);
			UE_LOG(LogNiagaraDebugger, Log, TEXT("Establishing connection. | Session: %s | Instance: %s (%s)."), *SessionId.ToString(), *InstanceId.ToString(), *Instance->GetInstanceName());
			MessageEndpoint->Publish(ConnectionRequestMessage);

			FClientInfo& NewPendingConnection = PendingClients.AddDefaulted_GetRef();
			NewPendingConnection.SessionId = SessionId;
			NewPendingConnection.InstanceId = InstanceId;
			NewPendingConnection.StartTime = FPlatformTime::Seconds();
		}
		else
		{
			CloseConnection(Instance->GetOwnerSession()->GetSessionId(), Instance->GetInstanceId());
		}
	}
}

int32 FNiagaraDebugger::FindPendingConnection(FGuid SessionId, FGuid InstanceId)const
{
	return PendingClients.IndexOfByPredicate([&](const FClientInfo& Pending) { return Pending.SessionId == SessionId && Pending.InstanceId == InstanceId; });
}

int32 FNiagaraDebugger::FindActiveConnection(FGuid SessionId, FGuid InstanceId)const
{
	return ConnectedClients.IndexOfByPredicate([&](const FClientInfo& Active) { return Active.SessionId == SessionId && Active.InstanceId == InstanceId; });
}

void FNiagaraDebugger::CloseConnection(FGuid SessionId, FGuid InstanceId)
{
	int32 FoundPending = FindPendingConnection(SessionId, InstanceId);
	int32 FoundActive = FindActiveConnection(SessionId, InstanceId);

	checkf(FoundActive == INDEX_NONE || FoundPending == INDEX_NONE, TEXT("Same client info is on both the pending and active connections lists."));

	if (FoundPending != INDEX_NONE)
	{
		FClientInfo Pending = PendingClients[FoundPending];
		PendingClients.RemoveAtSwap(FoundPending);
		MessageEndpoint->Publish(new FNiagaraDebuggerConnectionClosed(Pending.SessionId, Pending.InstanceId));
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Closing pending connection. | Session: %s | Instance: %s |"), *SessionId.ToString(), *InstanceId.ToString());

		OnConnectionClosedDelegate.Broadcast(Pending);
	}
	if (FoundActive != INDEX_NONE)
	{
		FClientInfo Active = ConnectedClients[FoundActive];
		ConnectedClients.RemoveAtSwap(FoundActive);
		MessageEndpoint->Send(new FNiagaraDebuggerConnectionClosed(Active.SessionId, Active.InstanceId), EMessageFlags::Reliable, nullptr, TArrayBuilder<FMessageAddress>().Add(Active.Address), FTimespan::Zero(), FDateTime::MaxValue());
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Closing active connection. | Session: %s | Instance: %s |"), *SessionId.ToString(), *InstanceId.ToString());

		OnConnectionClosedDelegate.Broadcast(Active);
	}
}

void FNiagaraDebugger::HandleConnectionAcceptedMessage(const FNiagaraDebuggerAcceptConnection& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	int32 FoundActive = FindActiveConnection(Message.SessionId, Message.InstanceId);
	if (FoundActive != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Recieved connection accepted message from an already connected client. Ignored. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		return;
	}

	int32 FoundPending = FindPendingConnection(Message.SessionId, Message.InstanceId);
	if (FoundPending != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Connection accepted. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		PendingClients.RemoveAtSwap(FoundPending);

		FClientInfo& NewConnection = ConnectedClients.AddDefaulted_GetRef();
		NewConnection.Address = Context->GetSender();
		NewConnection.SessionId = Message.SessionId;
		NewConnection.InstanceId = Message.InstanceId;
		NewConnection.StartTime = FPlatformTime::Seconds();

		UpdateDebugHUDSettings();

		OnConnectionMadeDelegate.Broadcast(NewConnection);
	}
	else
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Recieved connection accepted message from a client that is not in our pending list. Ignored. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
	}
}

void FNiagaraDebugger::HandleConnectionClosedMessage(const FNiagaraDebuggerConnectionClosed& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	int32 FoundPending = FindPendingConnection(Message.SessionId, Message.InstanceId);
	int32 FoundActive = FindActiveConnection(Message.SessionId, Message.InstanceId);

	checkf(FoundActive == INDEX_NONE || FoundPending == INDEX_NONE, TEXT("Same client info is on both the pending and active connections lists."));

	if (FoundPending != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Pending connection closed by the client. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		OnConnectionClosedDelegate.Broadcast(PendingClients[FoundPending]);
		PendingClients.RemoveAtSwap(FoundPending);
	}
	if (FoundActive != INDEX_NONE)
	{
		UE_LOG(LogNiagaraDebugger, Log, TEXT("Active connection closed by the client. | Session: %s | Instance: %s |"), *Message.SessionId.ToString(), *Message.InstanceId.ToString());
		OnConnectionClosedDelegate.Broadcast(ConnectedClients[FoundActive]);
		ConnectedClients.RemoveAtSwap(FoundActive);
	}
}

void FNiagaraDebugger::HandleOutlinerUpdateMessage(const FNiagaraDebuggerOutlinerUpdate& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UE_LOG(LogNiagaraDebugger, Log, TEXT("Recieved outliner update. | Sender: %s "), *Context->GetSender().ToString());
	if(UNiagaraOutliner* Outliner = GetOutliner())
	{
		Outliner->UpdateData(Message.OutlinerData);
	}
}

void FNiagaraDebugger::UpdateSimpleClientInfo(const FNiagaraSimpleClientInfo& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UE_LOG(LogNiagaraDebugger, Log, TEXT("Recieved simple client info update. | Sender: %s "), *Context->GetSender().ToString());
	SimpleClientInfo = Message;
	OnSimpleClientInfoChangedDelegate.Broadcast(SimpleClientInfo);
}

#undef LOCTEXT_NAMESPACE


#endif//WITH_NIAGARA_DEBUGGER