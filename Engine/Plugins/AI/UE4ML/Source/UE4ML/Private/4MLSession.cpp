// Copyright Epic Games, Inc. All Rights Reserved.

#include "4MLSession.h"
#include "4MLTypes.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameStateBase.h"
#include "Agents/4MLAgent.h"
#include "4MLManager.h"


void U4MLSession::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		bTickWorldManually = (U4MLManager::Get().IsWorldRealTime() == false);
	}
}

void U4MLSession::BeginDestroy()
{
	Super::BeginDestroy();
}

void U4MLSession::SetManualWorldTickEnabled(bool bEnable)
{
	bTickWorldManually = bEnable;
	if (bEnable)
	{
		if (WorldTicker == nullptr && CachedWorld)
		{
			WorldTicker = MakeShareable(new FWorldTicker(CachedWorld));
		}
	}
	else
	{
		WorldTicker = nullptr;
	}
}

void U4MLSession::FWorldTicker::Tick(float DeltaTime) 
{
#if WITH_EDITORONLY_DATA
	if (CachedWorld.IsValid())
	{
		// will get cleared in Session::tick
		GIntraFrameDebuggingGameThread = true;
	}
#endif // WITH_EDITORONLY_DATA
}

U4MLSession::FWorldTicker::~FWorldTicker()
{
#if WITH_EDITORONLY_DATA
	GIntraFrameDebuggingGameThread = false;
#endif // WITH_EDITORONLY_DATA
}

void U4MLSession::SetWorld(UWorld* NewWorld)
{
	if (CachedWorld == NewWorld)
	{
		return;
	}

	WorldTicker = nullptr;

	if (CachedWorld)
	{
		RemoveAvatars(CachedWorld);

		SetGameMode(nullptr);
		CachedWorld->RemoveOnActorSpawnedHandler(ActorSpawnedDelegateHandle);
		ActorSpawnedDelegateHandle.Reset();
		CachedWorld = nullptr;
		
		LastTimestamp = -1.f;
	}

	if (NewWorld)
	{
		CachedWorld = NewWorld;
		LastTimestamp = NewWorld->GetTimeSeconds();
		SetGameMode(NewWorld->GetAuthGameMode());

		if (bTickWorldManually)
		{
			WorldTicker = MakeShareable(new FWorldTicker(NewWorld));
		}

		FindAvatars(*CachedWorld);
		ActorSpawnedDelegateHandle = CachedWorld->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &U4MLSession::OnActorSpawned));
	}
}

void U4MLSession::OnActorSpawned(AActor* InActor)
{
	// check if it's something we need to consider, like a pawn or controller

	// CONCERN: this function is going to be called a lot and we care only about
	// a couple of possible actor classes. Sometimes we don't event care at all. 
	// this should be bound on demand, only when there are unassigned agents @todo

	if (AwaitingAvatar.Num() == 0 || InActor == nullptr)
	{
		return;
	}

	ensure(AvatarToAgent.Find(HashAvatar(*InActor)) == nullptr);

	// @todo extremely wasteful! needs rethinking/reimplementation
	TArray<U4MLAgent*> AwaitingAvatarCopy = AwaitingAvatar;
	AwaitingAvatar.Reset();

	bool bAssigned = false;
	for (U4MLAgent* Agent : AwaitingAvatarCopy)
	{
		if (!bAssigned && Agent->IsSuitableAvatar(*InActor))
		{
			BindAvatar(*Agent, *InActor);
			if (Agent->GetAvatar() == InActor)
			{
				bAssigned = true;
			}
			else
			{
				AwaitingAvatar.Add(Agent);
			}
		}
		else
		{
			AwaitingAvatar.Add(Agent);
		}
	}		
}

void U4MLSession::OnPostWorldInit(UWorld& World)
{
	SetWorld(&World);
}

void U4MLSession::OnWorldCleanup(UWorld& World, bool bSessionEnded, bool bCleanupResources)
{
	if (CachedWorld == &World)
	{
		SetWorld(nullptr);
	}
}

void U4MLSession::OnGameModeInitialized(AGameModeBase& GameModeBase)
{
	SetGameMode(&GameModeBase);
}

void U4MLSession::SetGameMode(AGameModeBase* GameModeBase)
{
	CachedGameMode = GameModeBase;
	AGameMode* AsGameMode = Cast<AGameMode>(GameModeBase);
	if (AsGameMode)
	{
		OnGameModeMatchStateSet(AsGameMode->GetMatchState());
	}
	else
	{
		// a game not utilizing AGameMode's functionality is either a simple game
		// or a very sophisticated one. In the former case we just assume it's 
		// 'ready' from the very start:
		SimulationState = GameModeBase ? E4MLSimState::InProgress : E4MLSimState::Finished;
		// in the latter case we put it on the user to override this logic.
	}

	// game-specific data extraction will come here
}

void U4MLSession::OnGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (ActorSpawnedDelegateHandle.IsValid() == false && CachedWorld)
	{
		FindAvatars(*CachedWorld);
		ActorSpawnedDelegateHandle = CachedWorld->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &U4MLSession::OnActorSpawned));
	}
}

void U4MLSession::OnGameModeMatchStateSet(FName InMatchState)
{
	if (InMatchState == MatchState::EnteringMap)
	{
		SimulationState = E4MLSimState::BootingUp;
	}
	else if (InMatchState == MatchState::WaitingToStart)
	{
		SimulationState = E4MLSimState::BootingUp;

		// no point in binding sooner than this 
		if (ensure(CachedWorld))
		{
			FindAvatars(*CachedWorld);
			if (ActorSpawnedDelegateHandle.IsValid() == false)
			{
				ActorSpawnedDelegateHandle = CachedWorld->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &U4MLSession::OnActorSpawned));
			}
		}
	}
	else if (InMatchState == MatchState::InProgress)
	{
		SimulationState = E4MLSimState::InProgress;
	}
	else if (InMatchState == MatchState::WaitingPostMatch
		|| InMatchState == MatchState::LeavingMap
		|| InMatchState == MatchState::Aborted)
	{
		SimulationState = E4MLSimState::Finished;
	}
}

void U4MLSession::Open()
{
	bActive = true;
}

void U4MLSession::Close()
{
	bActive = false;

	// 'destroy' agents by clearing the asyc flag and letting the GC clean them
	for (U4MLAgent* Agent : Agents)
	{
		Agent->ClearInternalFlags(EInternalObjectFlags::Async);
	}
	Agents.Reset();

	if (CachedWorld && CachedWorld->GetGameInstance())
	{
		CachedWorld->GetGameInstance()->GetOnPawnControllerChanged().RemoveAll(this);
	}
}

void U4MLSession::Tick(float DeltaTime)
{
	LastTimestamp = CachedWorld ? CachedWorld->GetTimeSeconds() : -1.f;

	// @todo for perf reasons we could grab all the agents' senses and tick them
	// by class to keep the cache hot
	for (U4MLAgent* Agent : Agents)
	{
		Agent->Sense(DeltaTime);
	}

	for (U4MLAgent* Agent : Agents)
	{
		//Agent->Think(DeltaTime);
	}

	for (U4MLAgent* Agent : Agents)
	{
		Agent->Act(DeltaTime);
	}

#if WITH_EDITORONLY_DATA
	if (CachedWorld && GIntraFrameDebuggingGameThread)
	{
		GIntraFrameDebuggingGameThread = false;
		// the WorldTicker will clear it to allow next tick
	}
#endif // WITH_EDITORONLY_DATA
}

void U4MLSession::ConfigureAsServer()
{

}

void U4MLSession::ConfigureAsClient()
{

}

void U4MLSession::ResetWorld(F4ML::FAgentID AgentID)
{
	if (CachedGameMode == nullptr)
	{
		return;
	}

	if (AgentID != F4ML::InvalidAgentID)
	{
		if (Agents.IsValidIndex(AgentID) && GetAgent(AgentID)->GetAvatar())
		{
			AController* AvatarController = F4ML::ActorToController(*GetAgent(AgentID)->GetAvatar());
			CachedGameMode->RestartPlayer(AvatarController);
		}
	}
	else
	{
		for (U4MLAgent* Agent : Agents)
		{
			if (Agent->GetAvatar())
			{
				AController* AvatarController = F4ML::ActorToController(*Agent->GetAvatar());
				CachedGameMode->RestartPlayer(AvatarController);
			}
		}
	}
}

bool U4MLSession::IsDone() const
{ 
	return (SimulationState == E4MLSimState::Finished)
		|| (CachedGameMode != nullptr
			&& CachedGameMode->HasMatchEnded());
}

bool U4MLSession::IsReady() const
{
	return SimulationState == E4MLSimState::InProgress 
		&& (CachedGameMode != nullptr)
		&& (CachedGameMode->HasMatchStarted() == true)
		&& (CachedGameMode->HasMatchEnded() == false);
}

F4ML::FAgentID U4MLSession::AddAgent()
{
	FScopeLock Lock(&AgentOpCS);

	// mz@todo support different classes, or [in]config
	U4MLAgent* NewAgent = F4ML::NewObject<U4MLAgent>(this);

	NewAgent->SetAgentID(Agents.Add(NewAgent));
	NewAgent->Configure(NewAgent->GetConfig());

	return NewAgent->GetAgentID();
}

F4ML::FAgentID U4MLSession::AddAgent(const F4MLAgentConfig& InConfig)
{
	FScopeLock Lock(&AgentOpCS);

	UClass* AgentClass = F4MLLibrarian::Get().FindAgentClass(InConfig.AgentClassName);
	U4MLAgent* NewAgent = F4ML::NewObject<U4MLAgent>(this, AgentClass);
	NewAgent->SetAgentID(Agents.Add(NewAgent));

	NewAgent->Configure(InConfig);

	return NewAgent->GetAgentID();
}

F4ML::FAgentID U4MLSession::GetNextAgentID(F4ML::FAgentID ReferenceAgentID) const
{
	if (Agents.Num() == 0)
	{
		return F4ML::InvalidAgentID;
	}
		
	int Index = (ReferenceAgentID != F4ML::InvalidAgentID)
		? (int(ReferenceAgentID) + 1) % Agents.Num()
		: 0;

	for (int Iter = 0; Iter < Agents.Num(); ++Iter)
	{
		if (Agents[Index])
		{
			return F4ML::FAgentID(Index);
		}
		Index = (Index + 1) % Agents.Num();
	}

	return F4ML::InvalidAgentID;
}

U4MLAgent* U4MLSession::GetAgent(F4ML::FAgentID AgentID)
{
	if (Agents.IsValidIndex(AgentID) == false)
	{
		UE_LOG(LogUE4ML, Warning, TEXT("U4MLSession::GetAgent: Invalid AgentID. Failing"));
		return nullptr;
	}

	return Agents[AgentID];
}

void U4MLSession::RemoveAgent(F4ML::FAgentID AgentID)
{
	if (Agents.IsValidIndex(AgentID) == false)
	{
		return;
	}

	// remove from Agents only if it's the last agent in the list since 
	// external code refers to agents by ID which is an index to this array
	// @todo consider switching over to a TMap
	// @todo consider always nulling out, i.e. not removing elements from Agents
	U4MLAgent* Agent = Agents[AgentID];
	if (!ensure(Agent))
	{
		return;
	}
	
	OnBeginAgentRemove.Broadcast(*Agent);

	if (Agents.Num() == AgentID)
	{
		Agents.Pop(/*bAllowShrinking=*/false);
	}
	else
	{
		Agents[AgentID] = nullptr;
	}
	for (auto It = AvatarToAgent.CreateIterator(); It; ++It)
	{
		if (It.Value() == Agent)
		{
			It.RemoveCurrent();
		}
	}
	AwaitingAvatar.RemoveSingleSwap(Agent, /*bAllowShrinking=*/false);
	// there should have been only one agent in AwaitingAvatar
	ensureMsgf(AwaitingAvatar.Find(Agent) == false, TEXT("there should have been only one agent in AwaitingAvatar"));

	Agent->ClearInternalFlags(EInternalObjectFlags::Async);
}

bool U4MLSession::IsAgentReady(F4ML::FAgentID AgentID) const
{
	return Agents.IsValidIndex(AgentID) && Agents[AgentID] && Agents[AgentID]->IsReady();
}

void U4MLSession::FindAvatars(UWorld& World)
{
	// @todo naive implementation for now, subject to optimization in the future
	TArray<U4MLAgent*> AwaitingAvatarCopy = AwaitingAvatar;
	AwaitingAvatar.Reset();

	for (U4MLAgent* Agent : AwaitingAvatarCopy)
	{
		if (ensure(Agent))
		{
			ensure(Agent->GetAvatar() == nullptr); // if not then the avatar has been assigned outside of normal procedure 
			RequestAvatarForAgent(*Agent, &World);
		}
	}
}

void U4MLSession::RemoveAvatars(UWorld* World)
{
	for (U4MLAgent* Agent : Agents)
	{
		if (Agent && Agent->GetAvatar() && (World == nullptr || Agent->GetAvatar()->GetWorld() == World))
		{
			AActor* OldAvatar = Agent->GetAvatar();
			Agent->SetAvatar(nullptr);
			OnAgentAvatarChanged.Broadcast(*Agent, OldAvatar);
		}
	}
}

bool U4MLSession::RequestAvatarForAgent(F4ML::FAgentID& AgentID, UWorld* InWorld)
{
	U4MLAgent* Agent = GetAgent(AgentID);
	return (Agent != nullptr) && RequestAvatarForAgent(*Agent, InWorld);
}

bool U4MLSession::RequestAvatarForAgent(U4MLAgent& Agent, UWorld* InWorld, const bool bForceSearch)
{
	if (Agent.GetAvatar() != nullptr)
	{
		// skipping.
		UE_LOG(LogUE4ML, Verbose, TEXT("U4MLSession::RequestAvatarForAgent called for agent [%s] while it still has an avatar [%s]. Call ClearAvatar first to null-out agent\'s avatar.")
			, Agent.GetAgentID(), *GetNameSafe(Agent.GetAvatar()));
		return false;
	}
	if (bForceSearch == false && AwaitingAvatar.Find(&Agent) != INDEX_NONE)
	{
		// already waiting, skip
		return false;
	}

	// we're adding to awaiting list first to avoid calling RequestAvatarForAgent 
	// couple of times in sequence
	AwaitingAvatar.AddUnique(&Agent);

	InWorld = InWorld ? InWorld : CachedWorld;
	
	if (InWorld == nullptr)
	{
		UE_LOG(LogUE4ML, Warning, TEXT("U4MLSession::RequestAvatarForAgent called with InWorld and CachedWorld both being null. Auto-failure."));
		return false;
	}

	AActor* Avatar = nullptr;
	if (InWorld != nullptr)
	{
		// @todo might want to make special cases for Controllers and 
		UClass* AvatarClass = Agent.GetConfig().AvatarClass;
		if (ensure(AvatarClass))
		{
			for (TActorIterator<AActor> It(InWorld, AvatarClass); It; ++It)
			{
				if (Agent.IsSuitableAvatar(**It)
					// AND not already an avatar
					&& AvatarToAgent.Find(HashAvatar(**It)) == nullptr)
				{
					Avatar = *It;
					break;
				}
			}
		}
	}

	if (Avatar)
	{
		BindAvatar(Agent, *Avatar);
		ensureMsgf(Agent.GetAvatar() == Avatar, TEXT("If we get here and the avatar setting fails it means the above process, leading to avatar selection was flawed"));
	}

	return Agent.GetAvatar() && (Agent.GetAvatar() == Avatar);
}

void U4MLSession::BindAvatar(U4MLAgent& Agent, AActor& Avatar)
{
	AActor* OldAvatar = Agent.GetAvatar();
	ClearAvatar(Agent);

	Agent.SetAvatar(&Avatar);
	AwaitingAvatar.RemoveSingleSwap(&Agent, /*bAllowShrinking=*/false);	
	AvatarToAgent.Add(HashAvatar(Avatar), &Agent);

	OnAgentAvatarChanged.Broadcast(Agent, OldAvatar);
}

void U4MLSession::ClearAvatar(U4MLAgent& Agent)
{
	AActor* OldAvatar = Agent.GetAvatar();
	if (OldAvatar == nullptr)
	{
		// it's possible the previous avatar is already gone. We need to look
		// through the map values
		const uint32* Key = AvatarToAgent.FindKey(&Agent);
		if (Key)
		{
			AvatarToAgent.Remove(*Key);
		}
		return;
	}

	U4MLAgent* BoundAgent = nullptr;
	AvatarToAgent.RemoveAndCopyValue(HashAvatar(*OldAvatar), BoundAgent);
	ensure(BoundAgent == &Agent);
	// @todo what if it causes another RequestAvatarForAgent call
	// should probably ignore the second one
	Agent.SetAvatar(nullptr);
	OnAgentAvatarChanged.Broadcast(Agent, OldAvatar);
}

const U4MLAgent* U4MLSession::FindAgentByAvatar(AActor& Avatar) const
{
	for (U4MLAgent* Agent : Agents)
	{
		if (Agent && Agent->GetAvatar() == &Avatar)
		{
			return Agent;
		}
	}

	return nullptr;
}

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategory.h"

void U4MLSession::DescribeSelfToGameplayDebugger(FGameplayDebuggerCategory& DebuggerCategory) const
{
	if (Agents.Num() > 0)
	{
		bool bInvalidAgents = false;
		FString ActiveAgentIDs;
		for (U4MLAgent* Agent : Agents)
		{
			if (Agent)
			{
				ActiveAgentIDs += FString::Printf(TEXT("{%s}%d,")
					, AwaitingAvatar.Find(Agent) != INDEX_NONE ? TEXT("grey") : TEXT("white")
					, Agent->GetAgentID());
			}
			else
			{
				bInvalidAgents = true;
			}
		}
		
		DebuggerCategory.AddTextLine(FString::Printf(TEXT("{green}Active agents: %s"), *ActiveAgentIDs));

		if (bInvalidAgents || AwaitingAvatar.Find(nullptr) != INDEX_NONE)
		{
			DebuggerCategory.AddTextLine(FString::Printf(TEXT("{red} invalid agents found!")));
		}
	}
}
#endif // WITH_GAMEPLAY_DEBUGGER
