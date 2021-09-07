// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsSimple.h"
#include "Engine/World.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"
#include "RewindData.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Chaos/SimCallbackObject.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "TimerManager.h"
#include "NetworkPredictionDebug.h"
#include "NetworkPredictionCVars.h"
#include "Async/NetworkPredictionAsyncModelDef.h"
#include "Async/NetworkPredictionAsyncWorldManager.h"
#include "Async/NetworkPredictionAsyncID.h"
#include "Async/NetworkPredictionAsyncModelDefRegistry.h"
#include "Async/NetworkPredictionAsyncProxyImpl.h"

namespace UE_NP
{
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(SimpleSpeed, 500.f, "np2.SimpleSpeed", "");
}

struct FSimpletSimulation
{
	static void Tick_Internal(FNetworkPredictionAsyncTickContext& Context, UE_NP::FNetworkPredictionAsyncID ID, FSimpleInputCmd& InputCmd, FSimpleLocalState& LocalState, FSimpleNetState& NetState)
	{
		UWorld* World = Context.World;
		const float DeltaSeconds = Context.DeltaTimeSeconds;
		const int32 SimulationFrame = Context.SimulationFrame;

		if (!ensureAlways(LocalState.Proxy))
		{
			return;
		}

		auto* PT = LocalState.Proxy->GetPhysicsThreadAPI();
		if (!PT)
		{
			return;
		}

		if (!InputCmd.bLegit)
		{
			// THis is just debug warning trying to make sure we never submit uninitialized input cmds into the system.
			UE_LOG(LogNetworkPrediction, Warning, TEXT("Illegitimate InputCmd has seeped in!!! SimFrame: %d. LocalFrame: %d"), SimulationFrame, Context.LocalStorageFrame);
		}

		if (InputCmd.bButtonPressed)
		{
			//UE_LOG(LogTemp, Warning, TEXT("0x%X [%d]"), (int64)
			//UE_LOG(LogTemp, Warning, TEXT("0x%X [%s][%d][%d][%d] ID: %d. [%s][%s] %s"), (int64)World, World->GetNetMode() == NM_Client ? TEXT("C") : TEXT("S"), SimulationFrame, Context.LocalStorageFrame, SimulationFrame - Context.LocalStorageFrame, (int32)ID, *InputCmd.ToString(), *NetState.ToString(), Context.bIsResim ? TEXT("RESIM") : TEXT(" "));
			NetState.ButtonPressedCounter++;
		}

		NetState.Counter++;

		if (InputCmd.MovementDir.SizeSquared() < 0.1f)
		{
			PT->SetV(Chaos::FVec3(0,0,0));
		}
		else
		{
			//PT->AddForce(InputCmd.MovementDir * UE_NP::SimpleSpeed());
			PT->SetV(InputCmd.MovementDir * UE_NP::SimpleSpeed());
		}
	}
};


struct FSimpleAsyncModelDef : public UE_NP::FNetworkPredictionAsyncModelDef
{
	NP_ASYNC_MODEL_BODY()

	using InputCmdType = FSimpleInputCmd;
	using NetStateType = FSimpleNetState;
	using LocalStateType = FSimpleLocalState;
	using SimulationTickType = FSimpletSimulation;

	static const TCHAR* GetName() { return TEXT("SimpleSimulation"); }
	static constexpr int32 GetSortPriority() { return 0; }
};

NP_ASYNC_MODEL_REGISTER(FSimpleAsyncModelDef);


UPhysicsSimpleComponent::UPhysicsSimpleComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPhysicsSimpleComponent::InitializeComponent()
{
	Super::InitializeComponent();
#if WITH_CHAOS
	UWorld* World = GetWorld();	
	checkSlow(World);
	UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>();
	if (!Manager)
	{
		return;
	}
	
	FSimpleLocalState LocalState;
	LocalState.Proxy = this->GetManagedProxy();
	if (LocalState.Proxy)
	{
		if (ensure(NetworkPredictionProxy.RegisterProxy(GetWorld())))
		{
			NetworkPredictionProxy.RegisterSim<FSimpleAsyncModelDef>(MoveTemp(LocalState), FSimpleNetState(), &PendingInputCmd, &SimpleState);
		}
	}
	else
	{
		UE_LOG(LogNetworkPhysics, Warning, TEXT("No valid physics body found on %s"), *GetName());
	}
#endif
}

void UPhysicsSimpleComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	NetworkPredictionProxy.OnPreReplication();
}

void UPhysicsSimpleComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
	NetworkPredictionProxy.UnregisterProxy();
}

void UPhysicsSimpleComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	APlayerController* PC = GetOwnerPC();

	// RegisterController when owning PC changes, or at least once (With null controller) on server
	if (CachedPC != PC || (!bHasRegisteredController && GetOwnerRole() == ROLE_Authority))
	{
		NetworkPredictionProxy.RegisterController(PC);
		bHasRegisteredController = true;
		CachedPC = PC;
	}

	if ((PC && PC->IsLocalController()) || (GetOwnerRole() == ROLE_Authority))
	{
		// Broadcast out a delegate. The user will write to PendingInputCmd
		OnGenerateLocalInputCmd.Broadcast();
		PendingInputCmd.bLegit = true;
		//PendingInputCmd.Counter++;
	}
}

void UPhysicsSimpleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UPhysicsSimpleComponent, NetworkPredictionProxy);
}

APlayerController* UPhysicsSimpleComponent::GetOwnerPC() const
{
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(PawnOwner->GetController());
	}

	return nullptr;
}

void UPhysicsSimpleComponent::SetCounter(int32 NewValue)
{
	NetworkPredictionProxy.ModifyNetState<FSimpleAsyncModelDef>([NewValue](FSimpleNetState& NetState)
	{
		NetState.ButtonPressedCounter += NewValue;
		NetState.Counter += NewValue;
	});
}


FAutoConsoleCommandWithWorldAndArgs ForcePhysicsSimpleCorrectionCmd(TEXT("np.PhysicsSimple.ForceRandCorrection2"), TEXT(""),
FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* InWorld) 
{
	for (TObjectIterator<UWorld> It; It; ++It)
	{
		UWorld* World = *It;
		if (World->GetNetMode() != NM_Client && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
		{
			const int32 NewRand = FMath::RandHelper(1024);

			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				if (UPhysicsSimpleComponent* PhysComp = ActorIt->FindComponentByClass<UPhysicsSimpleComponent>())
				{
					PhysComp->SetCounter(NewRand);
				}
			}
		}
	}
}));