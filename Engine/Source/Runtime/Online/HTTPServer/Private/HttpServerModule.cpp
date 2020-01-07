// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpServerModule.h"
#include "HttpListener.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogHttpServerModule); 

// FHttpServerModule 
IMPLEMENT_MODULE(FHttpServerModule, HTTPServer);

FHttpServerModule* FHttpServerModule::Singleton = nullptr;

void FHttpServerModule::StartupModule()
{
	Singleton = this;
	bInitialized = true;
}

void FHttpServerModule::ShutdownModule()
{
	bInitialized = false;

	// stop all listeners
	StopAllListeners();

	// destroy all listeners
	Listeners.Empty();
}

void FHttpServerModule::StartAllListeners()
{
	bHttpListenersEnabled = true;

	UE_LOG(LogHttpServerModule, Log,
		TEXT("Starting all listeners..."));

	for (const auto& Listener : Listeners)
	{
		if (!Listener.Value->IsListening())
		{
			Listener.Value->StartListening();
		}
	}
	UE_LOG(LogHttpServerModule, Log,
		TEXT("All listeners started"));
}

void FHttpServerModule::StopAllListeners()
{
	UE_LOG(LogHttpServerModule, Log, 
		TEXT("Stopping all listeners..."));

	for (const auto& Listener : Listeners)
	{
		if (Listener.Value->IsListening())
		{
			Listener.Value->StopListening();
		}
	}

	UE_LOG(LogHttpServerModule, Log,
		TEXT("All listeners stopped"));

}

bool FHttpServerModule::HasPendingListeners() const 
{
	for (const auto& Listener : Listeners)
	{
		if (Listener.Value->HasPendingConnections())
		{
			return true;
		}
	}
	return false;
}

FHttpServerModule& FHttpServerModule::Get()
{
	if (nullptr == Singleton)
	{
		check(IsInGameThread());
		FModuleManager::LoadModuleChecked<FHttpServerModule>("HTTPServer");
	}
	check(Singleton);
	return *Singleton;
}

TSharedPtr<IHttpRouter> FHttpServerModule::GetHttpRouter(uint32 Port)
{
	check(bInitialized);

	// We may already be listening on this port
	TUniquePtr<FHttpListener>* ExistingListener = Listeners.Find(Port);
	if (ExistingListener)
	{
		return ExistingListener->Get()->GetRouter();
	}

	// Otherwise create a new one
	TUniquePtr<FHttpListener> NewListener = MakeUnique<FHttpListener>(Port);

    // Try to start this listener now
	if (bHttpListenersEnabled)
	{
		NewListener->StartListening();
	}
	const auto& NewListenerRef = Listeners.Add(Port, MoveTemp(NewListener));
	return NewListenerRef->GetRouter();
}

bool FHttpServerModule::Tick(float DeltaTime)
{
	check(Singleton == this);
	check(bInitialized);

	if (bHttpListenersEnabled)
	{
		for (const auto& Listener : Listeners)
		{
			Listener.Value->Tick(DeltaTime);
		}
	}
	return true;
}
