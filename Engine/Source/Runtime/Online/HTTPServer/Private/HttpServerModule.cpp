// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

void FHttpServerModule::StopAllListeners()
{
	for (const auto& Listener : Listeners)
	{
		if (Listener.Value->IsListening())
		{
			UE_LOG(LogHttpServerModule, Log,
				TEXT("Stopping listener on Port: %u"), Listener.Key);

			Listener.Value->StopListening();
		}
	}
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
	if (!bInitialized)
	{
		return nullptr;
	}

	// We may already be listening on this port
	TUniquePtr<FHttpListener>* ExistingListener = Listeners.Find(Port);
	if (ExistingListener)
	{
		return ExistingListener->Get()->GetRouter();
	}
	else
	{
		// Otherwise create a new one
		TUniquePtr<FHttpListener> NewListener = MakeUnique<FHttpListener>(Port);
		if (NewListener->StartListening())
		{
			const auto& NewListenerRef = Listeners.Add(Port, MoveTemp(NewListener));
			return NewListenerRef->GetRouter();
		}
	}
	return nullptr;
}

bool FHttpServerModule::Tick(float DeltaTime)
{
	check(Singleton == this);

	for (const auto& Listener : Listeners)
	{
		check(Listener.Value);
		Listener.Value->Tick(DeltaTime);
	}
	return true;
}
