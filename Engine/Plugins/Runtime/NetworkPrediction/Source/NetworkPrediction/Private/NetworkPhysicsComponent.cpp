// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPhysicsComponent.h"
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

UNetworkPhysicsComponent::UNetworkPhysicsComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);
}

void UNetworkPhysicsComponent::InitializeComponent()
{
#if WITH_CHAOS
	Super::InitializeComponent();

	UWorld* World = GetWorld();	
	checkSlow(World);
	UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>();
	if (!Manager)
	{
		return;
	}

	// Test is component is valid for Network Physics. Needs a valid physics ActorHandle
	auto ValidComponent = [](UActorComponent* Component)
	{
		bool bValid = false;
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
		{
			bValid = FPhysicsInterface::IsValid(PrimComp->BodyInstance.ActorHandle);
		}
		return bValid;
	};

	auto SelectComponent = [&ValidComponent](const TArray<UActorComponent*>& Components)
	{
		UPrimitiveComponent* Pc = nullptr;
		for (UActorComponent* Ac : Components)
		{
			if (ValidComponent(Ac))
			{
				Pc = (UPrimitiveComponent*)Ac;
				break;
			}

		}
		return Pc;
	};

	UPrimitiveComponent* PrimitiveComponent = nullptr;
	if (AActor* MyActor = GetOwner())
	{
		// Explicitly tagged component
		if (ManagedComponentTag != NAME_None)
		{
			if (UPrimitiveComponent* FoundComponent = SelectComponent(MyActor->GetComponentsByTag(UPrimitiveComponent::StaticClass(), ManagedComponentTag)))
			{
				PrimitiveComponent = FoundComponent;
			}
			else
			{
				UE_LOG(LogNetworkPhysics, Warning, TEXT("Actor %s: could not find a valid Primitive Component with Tag %s"), *MyActor->GetPathName(), *ManagedComponentTag.ToString());
			}
		}

		// Root component
		if (!PrimitiveComponent && ValidComponent(MyActor->GetRootComponent()))
		{
			PrimitiveComponent = CastChecked<UPrimitiveComponent>(MyActor->GetRootComponent());
		}

		// Any other valid primitive component?
		if (!PrimitiveComponent)
		{
			if (UPrimitiveComponent* FoundComponent = SelectComponent(MyActor->K2_GetComponentsByClass(UPrimitiveComponent::StaticClass())))
			{
				PrimitiveComponent = FoundComponent;
			}
		}
	}

	if (ensureMsgf(PrimitiveComponent, TEXT("No PrimitiveComponent found on %s"), *GetPathName()))
	{
		NetworkPhysicsState.Proxy = PrimitiveComponent->BodyInstance.ActorHandle;
		NetworkPhysicsState.OwningActor = GetOwner();
		ensure(NetworkPhysicsState.OwningActor);

		Manager->RegisterPhysicsProxy(&NetworkPhysicsState);

		Manager->RegisterPhysicsProxyDebugDraw(&NetworkPhysicsState, [this](const UNetworkPhysicsManager::FDrawDebugParams& P)
			{
				AActor* Actor = this->GetOwner();
				FBox LocalSpaceBox = Actor->CalculateComponentsBoundingBoxInLocalSpace();
				const float Thickness = 2.f;

				FVector ActorOrigin;
				FVector ActorExtent;
				LocalSpaceBox.GetCenterAndExtents(ActorOrigin, ActorExtent);
				ActorExtent *= Actor->GetActorScale3D();
				DrawDebugBox(P.DrawWorld, P.Loc, ActorExtent, P.Rot, P.Color, false, P.Lifetime, 0, Thickness);
			});
	}
#endif
}

void UNetworkPhysicsComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
#if WITH_CHAOS
	if (UWorld* World = GetWorld())
	{
		if (UNetworkPhysicsManager* Manager = World->GetSubsystem<UNetworkPhysicsManager>())
		{
			Manager->UnregisterPhysicsProxy(&NetworkPhysicsState);
		}
	}
#endif
}

APlayerController* UNetworkPhysicsComponent::GetOwnerPC() const
{
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(PawnOwner->GetController());
	}

	return nullptr;
}

void UNetworkPhysicsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UNetworkPhysicsComponent, NetworkPhysicsState);
}

// ========================================================================================
// ========================================================================================
// ========================================================================================

#if 0

void UNetworkPhysicsComponent::ProcessInputs_External(FMockManagedState& State, int32 PhysicsStep, int32 LocalFrameOffset)
{
	if (bRecording)
	{
		if (CurrentInputCmdStream)
		{
			CurrentInputCmdStream->Add(State.InputCmd);
		}
	}
	else if (CurrentInputCmdStream && CurrentInputCmdStream->IsValidIndex(PlaybackIdx))
	{
		State.InputCmd = (*CurrentInputCmdStream)[PlaybackIdx++ % CurrentInputCmdStream->Num()];		
	}
}

void UNetworkPhysicsComponent::StartRecording(TArray<FMockPhysInputCmd>* Stream)
{
	if (bRecording)
	{
		return;
	}

	CurrentInputCmdStream = Stream;
	bRecording = true;
}

void UNetworkPhysicsComponent::StopRecording()
{
	if (CurrentInputCmdStream)
	{
		UE_LOG(LogTemp, Log, TEXT("Recorded %d Inputs."), CurrentInputCmdStream->Num());
	}

	bRecording = false;
	CurrentInputCmdStream = nullptr;
}

void UNetworkPhysicsComponent::StartPlayback(TArray<FMockPhysInputCmd>* Stream)
{
	CurrentInputCmdStream = Stream;
	if (CurrentInputCmdStream && CurrentInputCmdStream->Num() > 0)
	{
		PlaybackIdx = 0;
	}
	else
	{
		PlaybackIdx = INDEX_NONE;
	}
}


#endif