// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "MLAdapterTypes.h"
#include "Tickable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MLAdapterSession.generated.h"


class AGameModeBase;
class UMLAdapterAgent;
struct FMLAdapterAgentConfig;
class APawn;
class AController;
class APlayerController;
class UGameInstance;


enum class EMLAdapterSimState : uint8
{
	BootingUp,
	InProgress,
	Finished,
};


UCLASS()
class MLADAPTER_API UMLAdapterSession : public UObject
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAgentAvatarChangedDelegate, UMLAdapterAgent& /*Agent*/, AActor* /*OldAvatar*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeginAgentRemove, UMLAdapterAgent& /*Agent*/);

	virtual UWorld* GetWorld() const override { return CachedWorld; }
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;

	// @todo this needs further consideration.

	UGameInstance* GetGameInstance() const { return CachedWorld ? CachedWorld->GetGameInstance() : nullptr; }
	/** NewWorld might be null */
	virtual void SetWorld(UWorld* NewWorld);

	virtual void OnActorSpawned(AActor* InActor);
	virtual void OnPostWorldInit(UWorld& World);
	virtual void OnWorldCleanup(UWorld& World, bool bSessionEnded, bool bCleanupResources);
	virtual void OnGameModeInitialized(AGameModeBase& GameModeBase);
	virtual void OnGameModeMatchStateSet(FName InMatchState);
	virtual void OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);

	virtual void Open();
	virtual void Close();

	void Tick(float DeltaTime);

	/** This is where Session can add Authority-side-specific functions by calling 
	 *	UMLAdapterManager::Get().AddServerFunctionBind */
	virtual void ConfigureAsServer();
	/** This is where Session can add Client-side-specific functions by calling 
	 *	UMLAdapterManager::Get().AddClientFunctionBind. "Client" in this context means 
	 *	UnrealEngine game client, not RPC client */
	virtual void ConfigureAsClient();

	/** using FMLAdapter::InvalidAgentID for AgentID will reset all agents */
	virtual void ResetWorld(FMLAdapter::FAgentID AgentID = FMLAdapter::InvalidAgentID);

	bool IsDone() const;
	bool IsReady() const;

	float GetTimestamp() const { return LastTimestamp; }

	void SetManualWorldTickEnabled(bool bEnable);

	FOnAgentAvatarChangedDelegate GetOnAgentAvatarChanged() { return OnAgentAvatarChanged; }
	FOnBeginAgentRemove GetOnBeginAgentRemove() { return OnBeginAgentRemove; }

	//----------------------------------------------------------------------//
	// Agent/Avatar management 
	//----------------------------------------------------------------------//
	FMLAdapter::FAgentID AddAgent();
	FMLAdapter::FAgentID AddAgent(const FMLAdapterAgentConfig& InConfig);

	/** @return next valid agent ID. Note that the return value might be equal to 
	 *	ReferenceAgentID if there's only one agent. Will be FMLAdapter::InvalidAgentId 
	 *	if no agents registerd */
	FMLAdapter::FAgentID GetNextAgentID(FMLAdapter::FAgentID ReferenceAgentID) const;
	UMLAdapterAgent* GetAgent(FMLAdapter::FAgentID AgentID);

	void RemoveAgent(FMLAdapter::FAgentID AgentID);

	bool IsAgentReady(FMLAdapter::FAgentID AgentID) const;

	/** Finds avatar in given World for every avatar-less agent in AwaitingAvatar */
	void FindAvatars(UWorld& World);

	/** Processes Agents and removes all agent avatars belonging to World. 
	 *	If World is null the function will remove all avatars */
	void RemoveAvatars(UWorld* World);

	/** Finds a suitable avatar in InWorld (or CachedWorld, if InWorld is null) 
	 *	for given agent, as specified by FMLAdapterAgentConfig.AvatarClass
	 *	and confirmed by Agent->IsSuitableAvatar call. If no suitable avatar is 
	 *	found this agent will be added to "waiting list" (AwaitingAvatar)
	 *	@param bForceSearch if true will ignore whether the Agent is already waiting in 
	 *		AwaitingAvatar and will perform the search right away. Note that the Agent 
	 *		might still end up in AwaitingAvatar if there's no suitable avatars available 
	 *	@return true if an avatar has been assigned. False otherwise.*/
	virtual bool RequestAvatarForAgent(UMLAdapterAgent& Agent, UWorld* InWorld = nullptr, const bool bForceSearch = false);

	bool RequestAvatarForAgent(FMLAdapter::FAgentID& AgentID, UWorld* InWorld = nullptr);

	void BindAvatar(UMLAdapterAgent& Agent, AActor& Avatar);
	void ClearAvatar(UMLAdapterAgent& Agent);

	int32 GetAgentsCount() const { return Agents.Num(); }

	const UMLAdapterAgent* FindAgentByAvatar(AActor& Avatar) const;

	//----------------------------------------------------------------------//
	// debug 
	//----------------------------------------------------------------------//
#if WITH_GAMEPLAY_DEBUGGER
	void DescribeSelfToGameplayDebugger(class FGameplayDebuggerCategory& DebuggerCategory) const;
#endif // WITH_GAMEPLAY_DEBUGGER

protected:
	virtual void SetGameMode(AGameModeBase* GameModeBase);

	FORCEINLINE static uint32 HashAvatar(const AActor& Avatar)
	{
		return Avatar.GetUniqueID();
	}

	UPROPERTY()
	AGameModeBase* CachedGameMode;

	UPROPERTY()
	UWorld* CachedWorld;

	/** @see HashAvatar */
	UPROPERTY()
	TMap<uint32, UMLAdapterAgent*> AvatarToAgent;

	UPROPERTY()
	TArray<UMLAdapterAgent*> Agents;

	UPROPERTY()
	TArray<UMLAdapterAgent*> AwaitingAvatar;

	FOnAgentAvatarChangedDelegate OnAgentAvatarChanged;
	FOnBeginAgentRemove OnBeginAgentRemove;

	FDelegateHandle ActorSpawnedDelegateHandle;
	
	EMLAdapterSimState SimulationState;

	float LastTimestamp = -1.f;

	bool bActive = false;
	bool bTickWorldManually = false;

	mutable FCriticalSection AgentOpCS;

	struct FWorldTicker : public FTickableGameObject
	{
		TWeakObjectPtr<UWorld> CachedWorld;
		FWorldTicker(UWorld* InWorld) : CachedWorld(InWorld) {}
		virtual ~FWorldTicker();
		virtual void Tick(float DeltaTime) override;
		virtual UWorld* GetTickableGameObjectWorld() const override { return CachedWorld.Get(); }
		virtual TStatId GetStatId() const { return TStatId(); }
	};

	TSharedPtr<FWorldTicker> WorldTicker;
};
