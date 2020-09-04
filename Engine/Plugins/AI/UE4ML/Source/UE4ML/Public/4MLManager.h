// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Logging/LogMacros.h"
#include "Tickable.h"
#include "Engine/World.h"
#include "4MLSession.h"
#include "4MLLibrarian.h"
#include "4MLManager.generated.h"

namespace rpc { class server; }
namespace F4MLConsoleCommands { struct FHelper; }
class U4MLSession;
class U4MLActuator;
class U4MLSensor;
class U4MLAgent;
class AGameModeBase;
class APlayerController;
class UWorld;

UENUM()
enum class EUE4MLServerMode
{
	Invalid,
	Server,
	Client,
	Standalone, // this applies both to Standalone games as well as PIE
	AutoDetect,
};


UCLASS(Transient)
class UE4ML_API U4MLManager : public UObject, public FTickableGameObject, public FSelfRegisteringExec
{
	GENERATED_BODY()
public:
	using FRPCFunctionBind = TFunction<void(FRPCServer&/*RPCServer*/)>;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGenericRPCServerDelegate, FRPCServer& /*Server*/);
	DECLARE_MULTICAST_DELEGATE(FOnGenericEvent);
	
	U4MLManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginDestroy() override;
	virtual void PostInitProperties() override;

	// FTickableGameObject begin
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	// FTickableGameObject end

	// FExec begin
	virtual bool Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	// FExec end

	virtual void BindToDelegates();

	virtual void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues);
	virtual void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	virtual void OnBeginPIE(const bool bIsSimulating);
	virtual void OnEndPIE(const bool bIsSimulating);
	virtual void OnGameModeInitialized(AGameModeBase* GameMode);
	virtual void OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);
	/** @note that this might not get called at all if the project's game mode 
	 *	doesn't extend AGameMode*/
	virtual void OnGameModeMatchStateSet(FName MatchState);

	/** If a server is already running it will be shut down before the new instance gets created
	 *	@param ServerThreads best set at the number of external clients that are going to be connecting */
	virtual void StartServer(uint16 Port, EUE4MLServerMode InMode = EUE4MLServerMode::AutoDetect, uint16 ServerThreads = 1);
	virtual void StopServer();
	virtual bool IsRunning() const;

	virtual void ConfigureAsServer(FRPCServer& Server);
	/** "Client" in this context means UE4 game client, not RPC client */
	virtual void ConfigureAsClient(FRPCServer& Server);
	/** Essentially calls both the server and client versions */
	virtual void ConfigureAsStandalone(FRPCServer& Server);

	/** if given World doesn't have an AI system this call results in creating one */
	virtual void EnsureAISystemPresence(UWorld& World);

	/** if given World doesn't have a Navigation system instance this call results in creating one */
	virtual void EnsureNavigationSystemPresence(UWorld& World);

	virtual U4MLSession* CreateNewSession();
	virtual void SetSession(U4MLSession* NewSession);
	virtual void CloseSession(U4MLSession& InSession);

	/** Returns current sesison. If one doesn't exist, it gets created. */
	virtual U4MLSession& GetSession();
	bool HasSession() const { return Session && (Session->IsPendingKillOrUnreachable() == false); }

	void RegisterSensorClass(const TSubclassOf<U4MLSensor>& Class) { Librarian.RegisterSensorClass(Class); }
	void RegisterActuatorClass(const TSubclassOf<U4MLActuator>& Class) { Librarian.RegisterActuatorClass(Class); }
	void RegisterAgentClass(const TSubclassOf<U4MLAgent>& Class) { Librarian.RegisterAgentClass(Class); }

	virtual void ResetWorld();
	void SetManualWorldTickEnabled(bool bEnable);

	FOnGenericRPCServerDelegate& GetOnAddClientFunctions() { return OnAddClientFunctions; }
	FOnGenericRPCServerDelegate& GetOnAddServerFunctions() { return OnAddServerFunctions; }

	FORCEINLINE static U4MLManager& Get();
	FORCEINLINE static bool IsReady();

	const F4MLLibrarian& GetLibrarian() const { return Librarian; }

	bool IsWorldRealTime() const { return (bTickWorldManually == false); }

	FOnGenericEvent& GetOnCurrentSessionChanged() { return OnCurrentSessionChanged; }

protected:

	void AddCommonFunctions(FRPCServer& Server);

protected:	
	friend struct F4MLConsoleCommands::FHelper;

	UPROPERTY()
	U4MLSession* Session;

	UPROPERTY()
	UWorld* LastActiveWorld;

	UPROPERTY()
	F4MLLibrarian Librarian;

	FOnGenericRPCServerDelegate OnAddClientFunctions;
	FOnGenericRPCServerDelegate OnAddServerFunctions;

	FOnGenericEvent OnCurrentSessionChanged;

	EUE4MLServerMode RequestedFunctionMode = EUE4MLServerMode::Invalid;
	EUE4MLServerMode CurrentFunctionMode = EUE4MLServerMode::Invalid;
	uint16 CurrentPort = 0;
	uint16 CurrentServerThreads = 1;

	TArray<uint8> Data;

	float WorldFPS = 20.f;

	uint32 bCommonFunctionsAdded : 1;
	uint32 bTickWorldManually : 1;
	
	/** is the manager is in 'manual ticking mode' (where external client is 
	 *	responsible for progressing the world sim by calling 'request_world_tick' 
	 *	function) the simulation will progress by StepsRequested ticks before pausing */
	int32 StepsRequested = 0;

	static U4MLManager* ManagerInstance;
public:
	static FOnGenericEvent OnPostInit;
};


//----------------------------------------------------------------------//
// inlines 
//----------------------------------------------------------------------//
U4MLManager& U4MLManager::Get()
{ 
	// the only way for this check to fail is to call it too soon.
	check(ManagerInstance);  
	return *ManagerInstance; 
}

bool U4MLManager::IsReady()
{ 
	return (ManagerInstance != nullptr); 
}