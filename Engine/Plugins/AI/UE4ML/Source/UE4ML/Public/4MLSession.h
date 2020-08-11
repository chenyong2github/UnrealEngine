// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "4MLTypes.h"
#include "Tickable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "4MLSession.generated.h"


class AGameModeBase;
class U4MLAgent;
struct F4MLAgentConfig;
class APawn;
class AController;
class APlayerController;
class UGameInstance;


enum class E4MLSimState : uint8
{
	BootingUp,
	InProgress,
	Finished,
};


UCLASS()
class UE4ML_API U4MLSession : public UObject
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAgentAvatarChangedDelegate, U4MLAgent& /*Agent*/, AActor* /*OldAvatar*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeginAgentRemove, U4MLAgent& /*Agent*/);

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
	 *	U4MLManager::Get().AddServerFunctionBind */
	virtual void ConfigureAsServer();
	/** This is where Session can add Client-side-specific functions by calling 
	 *	U4MLManager::Get().AddClientFunctionBind. "Client" in this context means 
	 *	UE4 game client, not RPC client */
	virtual void ConfigureAsClient();

	/** using F4ML::InvalidAgentID for AgentID will reset all agents */
	virtual void ResetWorld(F4ML::FAgentID AgentID = F4ML::InvalidAgentID);

	bool IsDone() const;
	bool IsReady() const;

	float GetTimestamp() const { return LastTimestamp; }

	void SetManualWorldTickEnabled(bool bEnable);

	FOnAgentAvatarChangedDelegate GetOnAgentAvatarChanged() { return OnAgentAvatarChanged; }
	FOnBeginAgentRemove GetOnBeginAgentRemove() { return OnBeginAgentRemove; }

	//----------------------------------------------------------------------//
	// Agent/Avatar management 
	//----------------------------------------------------------------------//
	F4ML::FAgentID AddAgent();
	F4ML::FAgentID AddAgent(const F4MLAgentConfig& InConfig);

	/** @return next valid agent ID. Note that the return value might be equal to 
	 *	ReferenceAgentID if there's only one agent. Will be F4ML::InvalidAgentId 
	 *	if no agents registerd */
	F4ML::FAgentID GetNextAgentID(F4ML::FAgentID ReferenceAgentID) const;
	U4MLAgent* GetAgent(F4ML::FAgentID AgentID);

	void RemoveAgent(F4ML::FAgentID AgentID);

	bool IsAgentReady(F4ML::FAgentID AgentID) const;

	/** Finds avatar in given World for every avatar-less agent in AwaitingAvatar */
	void FindAvatars(UWorld& World);

	/** Processes Agents and removes all agent avatars belonging to World. 
	 *	If World is null the function will remove all avatars */
	void RemoveAvatars(UWorld* World);

	/** Finds a suitable avatar in InWorld (or CachedWorld, if InWorld is null) 
	 *	for given agent, as specified by F4MLAgentConfig.AvatarClass
	 *	and confirmed by Agent->IsSuitableAvatar call. If no suitable avatar is 
	 *	found this agent will be added to "waiting list" (AwaitingAvatar)
	 *	@param bForceSearch if true will ignore whether the Agent is already waiting in 
	 *		AwaitingAvatar and will perform the search right away. Note that the Agent 
	 *		might still end up in AwaitingAvatar if there's no suitable avatars available 
	 *	@return true if an avatar has been assigned. False otherwise.*/
	virtual bool RequestAvatarForAgent(U4MLAgent& Agent, UWorld* InWorld = nullptr, const bool bForceSearch = false);

	bool RequestAvatarForAgent(F4ML::FAgentID& AgentID, UWorld* InWorld = nullptr);

	void BindAvatar(U4MLAgent& Agent, AActor& Avatar);
	void ClearAvatar(U4MLAgent& Agent);

	int32 GetAgentsCount() const { return Agents.Num(); }

	const U4MLAgent* FindAgentByAvatar(AActor& Avatar) const;

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
	TMap<uint32, U4MLAgent*> AvatarToAgent;

	UPROPERTY()
	TArray<U4MLAgent*> Agents;

	UPROPERTY()
	TArray<U4MLAgent*> AwaitingAvatar;

	FOnAgentAvatarChangedDelegate OnAgentAvatarChanged;
	FOnBeginAgentRemove OnBeginAgentRemove;

	FDelegateHandle ActorSpawnedDelegateHandle;
	
	E4MLSimState SimulationState;

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
