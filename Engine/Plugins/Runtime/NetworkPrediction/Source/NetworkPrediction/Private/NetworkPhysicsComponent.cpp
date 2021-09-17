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
#include "Components/PrimitiveComponent.h"

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

	if (AActor* MyActor = GetOwner())
	{
		TInlineComponentArray<UPrimitiveComponent*> FoundComponents;
		MyActor->GetComponents<UPrimitiveComponent>(FoundComponents);
		for (UPrimitiveComponent* FoundComponent : FoundComponents)
		{
			NetworkPhysicsStateArray.Reserve(FoundComponents.Num());

			if (FPhysicsInterface::IsValid(FoundComponent->BodyInstance.ActorHandle))
			{
				FNetworkPhysicsState& NewElement = NetworkPhysicsStateArray.AddDefaulted_GetRef();

				NewElement.Proxy = FoundComponent->BodyInstance.ActorHandle;
				NewElement.OwningActor = GetOwner();

				Manager->RegisterPhysicsProxy(&NewElement);

				Manager->RegisterPhysicsProxyDebugDraw(&NewElement, [this, FoundComponent](const UNetworkPhysicsManager::FDrawDebugParams& P)
				{
					const float Thickness = 2.f;
					
					FBox Box(ForceInit);
					Box += FoundComponent->CalcBounds(FTransform::Identity).GetBox();

					FVector BoxOrigin;
					FVector BoxExtent;
					Box.GetCenterAndExtents(BoxOrigin, BoxExtent);
					const FVector RelativeScale3D = FoundComponent->GetRelativeScale3D();
					BoxExtent *= RelativeScale3D;

					DrawDebugBox(P.DrawWorld, P.Loc + BoxOrigin, BoxExtent, P.Rot, P.Color, false, P.Lifetime, 0, Thickness);
				});
			}
		}
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
			for (FNetworkPhysicsState& Element : NetworkPhysicsStateArray)
			{
				Manager->UnregisterPhysicsProxy(&Element);
			}
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
	DOREPLIFETIME( UNetworkPhysicsComponent, NetworkPhysicsStateArray);
	
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