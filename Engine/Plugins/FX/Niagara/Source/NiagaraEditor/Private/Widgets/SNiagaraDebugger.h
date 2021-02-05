// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/TargetDeviceId.h"
#include "Interfaces/ITargetDevice.h"
#include "Misc/Optional.h"
#include "Widgets/SCompoundWidget.h"
#include "IPropertyChangeListener.h"

#include "Niagara/Private/NiagaraDebugHud.h"
#include "NiagaraWorldManager.h"
#include "NiagaraDebuggerCommon.h"
#include "Niagara/Private/NiagaraDebugHud.h"

class ISessionManager;
class FMessageEndpoint;
class ISessionInfo;
class ISessionInstanceInfo;

DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraDebugger, Log, All);

class SNiagaraDebugger : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraDebugger) {}
		SLATE_ARGUMENT(TSharedPtr<class FTabManager>, TabManager)
	SLATE_END_ARGS();

	SNiagaraDebugger();
	virtual ~SNiagaraDebugger();

	void Construct(const FArguments& InArgs);
	void FillWindowMenu(FMenuBuilder& MenuBuilder);

	static void RegisterTabSpawner();
	static void UnregisterTabSpawner();
	static TSharedRef<class SDockTab> SpawnNiagaraDebugger(const class FSpawnTabArgs& Args);

	void ExecConsoleCommand(const TCHAR* Cmd, bool bRequiresWorld);
	void UpdateDebugHUDSettings();

private:
	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakePlaybackOptionsMenu();

protected:
	TSharedPtr<FTabManager>			TabManager;

	//////////////////////////////////////////////////////////////////////////
	// Session selection and message handling.
public:
	struct FClientInfo
	{
		FMessageAddress Address;
		FGuid SessionId;
		FGuid InstanceId;

		/** Time this connection began it's current state, pending or connected. Used to timeout pending connections and track uptime of connected clients. */
		double StartTime;

		FORCEINLINE void Clear()
		{
			Address.Invalidate();
			SessionId.Invalidate();
			InstanceId.Invalidate();
			StartTime = 0.0;
		}
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionMade, const FClientInfo&);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionClosed, const FClientInfo&);

	template<typename TAction>
	void ForAllConnectedClients(TAction Func);

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe>& GetMessageEndpoint() { return MessageEndpoint; }

	FOnConnectionMade& GetOnConnectionMade() { return OnConnectionMadeDelegate; }
	FOnConnectionClosed& GetOnConnectionClosed() { return OnConnectionClosedDelegate; }

protected:

	// Handles a connection accepted message and finalizes establishing the connection to a debugger client.
	void HandleConnectionAcceptedMessage(const FNiagaraDebuggerAcceptConnection& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	// Handles a connection closed message can be called from debugged clients if their instance shutsdown etc.
	void HandleConnectionClosedMessage(const FNiagaraDebuggerConnectionClosed& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	/** Callback from session manager when the selected session is changed. */
	void SessionManager_OnSessionSelectionChanged(const TSharedPtr<ISessionInfo>& Session);

	/** Callback from session manager when the selected instance is changed. */
	void SessionManager_OnInstanceSelectionChanged(const TSharedPtr<ISessionInstanceInfo>& Instance, bool Selected);

	/** Removes any active or pending connection to the given client and sends a message informing the client. */
	void CloseConnection(FGuid SessionId, FGuid InstanceId);

	int32 FindPendingConnection(FGuid SessionId, FGuid InstanceId)const;
	int32 FindActiveConnection(FGuid SessionId, FGuid InstanceId)const;

	/** Holds a pointer to the session manager. */
	TSharedPtr<ISessionManager> SessionManager;

	/** Holds the messaging endpoint. */
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	/** Clients that we are actively connected to. */
	TArray<FClientInfo> ConnectedClients;
	
	/** Clients that we are awaiting an connection acceptance message from. */
	TArray<FClientInfo> PendingClients;

	FOnConnectionMade OnConnectionMadeDelegate;
	FOnConnectionClosed OnConnectionClosedDelegate;
	
	// Session selection and message handling END
	//////////////////////////////////////////////////////////////////////////
};

template<typename TAction>
void SNiagaraDebugger::ForAllConnectedClients(TAction Func)
{
	for (SNiagaraDebugger::FClientInfo& Client : ConnectedClients)
	{
		Func(Client);
	}
}
