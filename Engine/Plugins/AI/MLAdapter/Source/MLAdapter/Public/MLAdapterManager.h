// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Logging/LogMacros.h"
#include "Tickable.h"
#include "Engine/World.h"
#include "MLAdapterSession.h"
#include "MLAdapterLibrarian.h"
#include "MLAdapterManager.generated.h"

namespace rpc { class server; }
namespace FMLAdapterConsoleCommands { struct FHelper; }
class UMLAdapterSession;
class UMLAdapterActuator;
class UMLAdapterSensor;
class UMLAdapterAgent;
class AGameModeBase;
class APlayerController;
class UWorld;

UENUM()
enum class EMLAdapterServerMode
{
	Invalid,
	Server,
	Client,
	Standalone, // this applies both to Standalone games as well as PIE
	AutoDetect,
};


UCLASS(Transient)
class MLADAPTER_API UMLAdapterManager : public UObject, public FTickableGameObject, public FSelfRegisteringExec
{
	GENERATED_BODY()
public:
	using FRPCFunctionBind = TFunction<void(FRPCServer&/*RPCServer*/)>;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGenericRPCServerDelegate, FRPCServer& /*Server*/);
	DECLARE_MULTICAST_DELEGATE(FOnGenericEvent);
	
	UMLAdapterManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
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
	virtual void StartServer(uint16 Port, EMLAdapterServerMode InMode = EMLAdapterServerMode::AutoDetect, uint16 ServerThreads = 1);
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

	virtual UMLAdapterSession* CreateNewSession();
	virtual void SetSession(UMLAdapterSession* NewSession);
	virtual void CloseSession(UMLAdapterSession& InSession);

	/** Returns current sesison. If one doesn't exist, it gets created. */
	virtual UMLAdapterSession& GetSession();
	bool HasSession() const { return Session && (Session->IsPendingKillOrUnreachable() == false); }

	void RegisterSensorClass(const TSubclassOf<UMLAdapterSensor>& Class) { Librarian.RegisterSensorClass(Class); }
	void RegisterActuatorClass(const TSubclassOf<UMLAdapterActuator>& Class) { Librarian.RegisterActuatorClass(Class); }
	void RegisterAgentClass(const TSubclassOf<UMLAdapterAgent>& Class) { Librarian.RegisterAgentClass(Class); }

	virtual void ResetWorld();
	void SetManualWorldTickEnabled(bool bEnable);

	FOnGenericRPCServerDelegate& GetOnAddClientFunctions() { return OnAddClientFunctions; }
	FOnGenericRPCServerDelegate& GetOnAddServerFunctions() { return OnAddServerFunctions; }

	FORCEINLINE static UMLAdapterManager& Get();
	FORCEINLINE static bool IsReady();

	const FMLAdapterLibrarian& GetLibrarian() const { return Librarian; }

	bool IsWorldRealTime() const { return (bTickWorldManually == false); }

	FOnGenericEvent& GetOnCurrentSessionChanged() { return OnCurrentSessionChanged; }

protected:

	void AddCommonFunctions(FRPCServer& Server);

protected:	
	friend struct FMLAdapterConsoleCommands::FHelper;

	UPROPERTY()
	UMLAdapterSession* Session;

	UPROPERTY()
	UWorld* LastActiveWorld;

	UPROPERTY()
	FMLAdapterLibrarian Librarian;

	FOnGenericRPCServerDelegate OnAddClientFunctions;
	FOnGenericRPCServerDelegate OnAddServerFunctions;

	FOnGenericEvent OnCurrentSessionChanged;

	EMLAdapterServerMode RequestedFunctionMode = EMLAdapterServerMode::Invalid;
	EMLAdapterServerMode CurrentFunctionMode = EMLAdapterServerMode::Invalid;
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

	static UMLAdapterManager* ManagerInstance;
public:
	static FOnGenericEvent OnPostInit;
};


//----------------------------------------------------------------------//
// inlines 
//----------------------------------------------------------------------//
UMLAdapterManager& UMLAdapterManager::Get()
{ 
	// the only way for this check to fail is to call it too soon.
	check(ManagerInstance);  
	return *ManagerInstance; 
}

bool UMLAdapterManager::IsReady()
{ 
	return (ManagerInstance != nullptr); 
}