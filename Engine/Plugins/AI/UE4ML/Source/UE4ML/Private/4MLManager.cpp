// Copyright Epic Games, Inc. All Rights Reserved.

#include "4MLManager.h"
#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "Engine/Engine.h"
#include "GameFramework/GameModeBase.h"
#include "4MLTypes.h"
#include "4MLAsync.h"
#include "4MLSession.h"
#include "4MLJson.h"
#include "Agents/4MLAgent.h"
#include "Sensors/4MLSensor.h"
#include "UE4MLSettings.h"
#include <string>
#if WITH_EDITORONLY_DATA
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif // WITH_EDITORONLY_DATA

#include "RPCWrapper/MsgPack.h"
#include "RPCWrapper/Server.h"

// engine AI support
#include "AI/NavigationSystemBase.h"


namespace
{
	FRPCServer* RPCServerInstance = nullptr;
	
	EUE4MLServerMode GetServerModeForWorld(UWorld& World)
	{
		EUE4MLServerMode Mode = EUE4MLServerMode::Invalid;
		switch (World.GetNetMode())
		{
		case NM_Standalone:
		case NM_ListenServer:
			Mode = EUE4MLServerMode::Standalone;
			break;
		case NM_DedicatedServer:
			Mode = EUE4MLServerMode::Server;
			break;
		case NM_Client:
			Mode = EUE4MLServerMode::Client;
			break;
		}
		ensure(Mode != EUE4MLServerMode::Invalid);
		return Mode;
	}
}

namespace
{
	struct FManagerBootloader
	{
		FManagerBootloader()
		{
			FCoreDelegates::OnPostEngineInit.AddLambda([this]()
			{
				OnPostEngineInit();
			});
		}

		void OnPostEngineInit()
		{
			// create the manager instance
			TSubclassOf<U4MLManager> SettingsManagerClass = UUE4MLSettings::GetManagerClass().Get();
			UClass* Class = SettingsManagerClass
				? SettingsManagerClass.Get()
				: U4MLManager::StaticClass();

			UE_LOG(LogUE4ML, Log, TEXT("Creating UE4ML manager of class %s"), *GetNameSafe(Class));

			U4MLManager* ManagerInstance = NewObject<U4MLManager>(GEngine, Class);
			check(ManagerInstance);
			ManagerInstance->AddToRoot();

			U4MLManager::OnPostInit.Broadcast();
		}
	};

	static FManagerBootloader Loader;
}

//----------------------------------------------------------------------//
// 
//
// @todo we might want to look for a way to restrict users from creating U4MLManager
// instances manually.
//----------------------------------------------------------------------//
U4MLManager* U4MLManager::ManagerInstance = nullptr;
U4MLManager::FOnGenericEvent U4MLManager::OnPostInit;

U4MLManager::U4MLManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCommonFunctionsAdded = false;
	bTickWorldManually = false;

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		ensure(ManagerInstance == nullptr);

		ManagerInstance = this;
	}
}

void U4MLManager::PostInitProperties()
{
	Super::PostInitProperties();
	
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		Librarian.GatherClasses();

		BindToDelegates();

		// if there's any world present create the RPC server
		UWorld* World = 
#if WITH_EDITOR
			GIsEditor ? GWorld :
#endif // WITH_EDITOR
			(GEngine->GetWorldContexts().Num() > 0 ? GEngine->GetWorldContexts()[0].World() : nullptr);
		
		OnPostWorldInit(World, UWorld::InitializationValues());
	}
}

void U4MLManager::BeginDestroy()
{
	SetSession(nullptr);
	if (ManagerInstance == this)
	{
		//SetManualWorldTickEnabled(false);
		ManagerInstance = nullptr;
	}
	StopServer();
	Super::BeginDestroy();
}

void U4MLManager::StopServer()
{
	if (RPCServerInstance)
	{
		UE_LOG(LogUE4ML, Log, TEXT("Stopping RPC server."));
		RPCServerInstance->stop();
		delete RPCServerInstance;
		RPCServerInstance = nullptr;
	}
	CurrentFunctionMode = EUE4MLServerMode::Invalid;
}

void U4MLManager::StartServer(uint16 Port, EUE4MLServerMode InMode, uint16 ServerThreads)
{
	StopServer();
	
	RequestedFunctionMode = InMode;
	ServerThreads = FMath::Max< uint16>(1, ServerThreads);

	EUE4MLServerMode NewMode = InMode;
	if (InMode == EUE4MLServerMode::Invalid || InMode == EUE4MLServerMode::AutoDetect)
	{
		if (LastActiveWorld)
		{
			NewMode = GetServerModeForWorld(*LastActiveWorld);
		}
		else if (GIsEditor || (GIsClient && GIsServer))
		{
			NewMode = EUE4MLServerMode::Standalone;
		}
		else if (GIsClient)
		{
			NewMode = EUE4MLServerMode::Client;
		}
		else
		{
			NewMode = EUE4MLServerMode::Server;
		}
	}

	UE_LOG(LogUE4ML, Log, TEXT("Starting RPC server on port %d."), Port);
	RPCServerInstance = new FRPCServer(Port);
	CurrentPort = Port;
	check(RPCServerInstance);

	bCommonFunctionsAdded = false;

	CurrentFunctionMode = NewMode;
	switch (NewMode)
	{
	case EUE4MLServerMode::Client:
		ConfigureAsClient(*RPCServerInstance);
		break;
	case EUE4MLServerMode::Server:
		ConfigureAsServer(*RPCServerInstance);
		break;
	default:
		ConfigureAsClient(*RPCServerInstance);
		ConfigureAsServer(*RPCServerInstance);
		break;
	}
	CurrentServerThreads = ServerThreads;
	RPCServerInstance->async_run(ServerThreads);
}

void U4MLManager::ConfigureAsStandalone(FRPCServer& Server)
{
	ConfigureAsServer(Server);
	ConfigureAsClient(Server);
}

void U4MLManager::EnsureAISystemPresence(UWorld& World)
{
	if (World.GetAISystem())
	{
		return;
	}

	UAISystemBase* AISystem = World.CreateAISystem();
	// it's possible the world is configured to not have AI system. Not sure what to do in such a a case
	ensure(AISystem);
}

void U4MLManager::EnsureNavigationSystemPresence(UWorld& World)
{
	if (World.GetNavigationSystem())
	{
		return;
	}
	
	FNavigationSystem::AddNavigationSystemToWorld(World);	
	// it's possible the world is configured to not have a nav system. Not sure what to do in such a a case
	ensure(World.GetNavigationSystem());
}

bool U4MLManager::IsRunning() const
{
	return (RPCServerInstance != nullptr);
}

TStatId U4MLManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(U4MLManager, STATGROUP_Tickables);
}

ETickableTickType U4MLManager::GetTickableTickType() const 
{ 
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Always;
}

bool U4MLManager::IsTickable() const 
{ 
	return (HasAnyFlags(RF_ClassDefaultObject) == false); 
}

void U4MLManager::Tick(float DeltaTime)
{
	if (IsWorldRealTime() || StepsRequested > 0)
	{
		if (Session)
		{
			DeltaTime = IsWorldRealTime() ? DeltaTime : (1.f / WorldFPS);
			Session->Tick(DeltaTime);
		}
		--StepsRequested;
	}
}

U4MLSession* U4MLManager::CreateNewSession()
{
	UClass* Class = UUE4MLSettings::GetSessionClass().Get()
		? UUE4MLSettings::GetSessionClass().Get()
		: U4MLSession::StaticClass();

	U4MLSession* NewSession = F4ML::NewObject<U4MLSession>(this, Class);
	check(NewSession);
	NewSession->SetWorld(LastActiveWorld);
	// some config
	return NewSession;
}

void U4MLManager::SetSession(U4MLSession* NewSession)
{
	if (Session != nullptr && (NewSession == nullptr || Session != NewSession))
	{
		Session->Close();
		// clear the flag to let GC remove the object
		Session->ClearInternalFlags(EInternalObjectFlags::Async);
		Session = nullptr;
	}
	Session = NewSession;
	if (Session)
	{
		// we're going to be using this object in a async manner, so we need to 
		// mark it appropriately. This will make GC ignore this object until we 
		// clear the flag. 
		Session->SetInternalFlags(EInternalObjectFlags::Async);
		Session->Open();
	}

	OnCurrentSessionChanged.Broadcast();
}

void U4MLManager::CloseSession(U4MLSession& InSession)
{
	// @todo temporary implementation, will change with multi-session support
	if (&InSession == Session)
	{
		SetSession(nullptr);
	}
}

U4MLSession& U4MLManager::GetSession()
{
	if (Session == nullptr)
	{
		SetSession(CreateNewSession());
	}
	check(Session);
	return *Session;
}

void U4MLManager::BindToDelegates()
{	
	// Commented out possible other useful delegates
	//	FCoreDelegates::GameStateClassChanged;
	//	FCoreDelegates::ConfigReadyForUse;
	//	FWorldDelegates::OnPostWorldCreation;
	//	FWorldDelegates::OnPreWorldInitialization; 

	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &U4MLManager::OnPostWorldInit);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &U4MLManager::OnWorldCleanup);

	//FGameDelegates
	FGameModeEvents::OnGameModeInitializedEvent().AddUObject(this, &U4MLManager::OnGameModeInitialized);
	// FGameModePreLoginEvent GameModePreLoginEvent;
	FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this, &U4MLManager::OnGameModePostLogin);
	// FGameModeLogoutEvent GameModeLogoutEvent;
	FGameModeEvents::OnGameModeMatchStateSetEvent().AddUObject(this, &U4MLManager::OnGameModeMatchStateSet);

#if WITH_EDITORONLY_DATA
	FEditorDelegates::BeginPIE.AddUObject(this, &U4MLManager::OnBeginPIE);
	FEditorDelegates::EndPIE.AddUObject(this, &U4MLManager::OnEndPIE);
#endif // WITH_EDITORONLY_DATA
}

void U4MLManager::OnPostWorldInit(UWorld* World, const UWorld::InitializationValues)
{
	if (World && World->IsGameWorld())
	{
#if WITH_EDITOR
		// PIE is a special case, we need to see if it's a client-server PIE and 
		// if so we need to filter the incoming world based on the settings
		if (World->WorldType == EWorldType::PIE)
		{
			if (World->HasAnyFlags(RF_WasLoaded))
			{
				const ULevelEditorPlaySettings* PlayInSettings = GetDefault<ULevelEditorPlaySettings>();
				if (PlayInSettings)
				{
					EPlayNetMode PlayNetMode = PIE_Standalone;
					PlayInSettings->GetPlayNetMode(PlayNetMode);

					if (PlayNetMode != PIE_Standalone)
					{
						UE_LOG(LogUE4ML, Log, TEXT("Ignoring %s due to net mode != PIE_Standalone")
							, *World->GetName());
						//UUE4MLSettings::
						return;
					}
				}
				else
				{
					// skipping temp world
					return;
				}
			}

		}		
#endif // WITH_EDITOR

		LastActiveWorld = World;
		if ((RequestedFunctionMode == EUE4MLServerMode::Invalid || RequestedFunctionMode == EUE4MLServerMode::AutoDetect) 
			&& GetServerModeForWorld(*World) != CurrentFunctionMode)
		{
			// restart the RPC server. Note that this will kick all the currently connected agents
			uint16 Port = UUE4MLSettings::GetDefaultRPCServerPort();			
			FParse::Value(FCommandLine::Get(), TEXT("4MLPort="), Port);
			StartServer(Port, GetServerModeForWorld(*World), CurrentServerThreads);
		}

		if (HasSession())
		{
			GetSession().OnPostWorldInit(*World);
		}
	}	
}

void U4MLManager::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	// no need to remove, the World is going away
	if (World && World->IsGameWorld())
	{
		if (World == LastActiveWorld)
		{
			LastActiveWorld = nullptr;
		}

		if (HasSession())
		{
			GetSession().OnWorldCleanup(*World, bSessionEnded, bCleanupResources);
		}
	}
}

void U4MLManager::OnBeginPIE(const bool bIsSimulating)
{

}

void U4MLManager::OnEndPIE(const bool bIsSimulating)
{

}

void U4MLManager::OnGameModeInitialized(AGameModeBase* GameMode)
{
	if (GameMode && HasSession())
	{
		GetSession().OnGameModeInitialized(*GameMode);
	}
}

void U4MLManager::OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (GameMode && NewPlayer && HasSession())
	{
		GetSession().OnGameModePostLogin(GameMode, NewPlayer);
	}
}

void U4MLManager::OnGameModeMatchStateSet(FName MatchState)
{
	if (HasSession())
	{
		GetSession().OnGameModeMatchStateSet(MatchState);
	}
}

void U4MLManager::ResetWorld()
{
	if (LastActiveWorld)
	{
		AGameModeBase* GameMode = LastActiveWorld->GetAuthGameMode<AGameModeBase>();
		if (GameMode)
		{
			GameMode->ResetLevel();
		}
	}

	if (HasSession())
	{
		GetSession().ResetWorld();
	}
}

void U4MLManager::SetManualWorldTickEnabled(bool bEnable)
{
	bTickWorldManually = bEnable;
	if (Session)
	{
		Session->SetManualWorldTickEnabled(bEnable);
	}
}

bool U4MLManager::Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	if (FParse::Command(&Cmd, TEXT("4ml_session_stop")))
	{
		SetSession(nullptr);
		return true;
	}

	return false;
}

namespace F4MLConsoleCommands
{
	struct FHelper
	{
		static void RestartServer(const TArray<FString>& Args, UWorld*)
		{
			U4MLManager& Manager = U4MLManager::Get();
			uint16 Port = UUE4MLSettings::GetDefaultRPCServerPort();
			FParse::Value(FCommandLine::Get(), TEXT("4MLPort="), Port);
			if (Args.Num() > 0)
			{
				Port = uint16(TCString<TCHAR>::Atoi(*Args[0]));
			}
			Manager.StartServer(Port, Manager.CurrentFunctionMode, Manager.CurrentServerThreads);
		}
	};

	FAutoConsoleCommand StopSession(TEXT("4ml.session.stop"), TEXT(""), FConsoleCommandDelegate::CreateLambda([]()
	{
		U4MLManager::Get().SetSession(nullptr);
	}));

	FAutoConsoleCommandWithWorldAndArgs RestartServer(TEXT("4ml.server.restart")
		, TEXT("restarts the UE4ML RPC server, optionally changing the port the server is listening at. Use: 4ml.server.restart [port]")
		, FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(FHelper::RestartServer));
}
