// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdRepresentationProcessor.h"
#include "MassCrowdFragments.h"
#include "MassAIMovementFragments.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Translators/MassCharacterMovementTranslators.h"
#include "MassAgentComponent.h"
#include "MassCrowdRepresentationSubsystem.h"

UMassCrowdRepresentationProcessor::UMassCrowdRepresentationProcessor()
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
}

void UMassCrowdRepresentationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	// Override default representation subsystem by the crowd one to support parallelization
	RepresentationSubsystem = UWorld::GetSubsystem<UMassCrowdRepresentationSubsystem>(Owner.GetWorld());
}


void UMassCrowdRepresentationProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();
	EntityQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);

	CharacterMovementEntitiesQuery_Conditional = EntityQuery;
	CharacterMovementEntitiesQuery_Conditional.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(EMassFragmentAccess::ReadWrite);
	CharacterMovementEntitiesQuery_Conditional.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	CharacterMovementEntitiesQuery_Conditional.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
}

void UMassCrowdRepresentationProcessor::SetActorEnabled(const EActorEnabledType EnabledType, AActor& Actor, const int32 EntityIdx, FMassCommandBuffer& CommandBuffer)
{
	Super::SetActorEnabled(EnabledType, Actor, EntityIdx, CommandBuffer);

	const bool bEnabled = EnabledType != EActorEnabledType::Disabled;

	USkeletalMeshComponent* SkeletalMeshComponent = Actor.FindComponentByClass<USkeletalMeshComponent>();
	if (SkeletalMeshComponent)
	{
		// Enable/disable the ticking and visibility of SkeletalMesh and its children
		SkeletalMeshComponent->SetVisibility(bEnabled);
		SkeletalMeshComponent->SetComponentTickEnabled(bEnabled);
		const TArray<USceneComponent*>& AttachedChildren = SkeletalMeshComponent->GetAttachChildren();
		if (AttachedChildren.Num() > 0)
		{
			TInlineComponentArray<USceneComponent*, NumInlinedActorComponents> ComponentStack;

			ComponentStack.Append(AttachedChildren);
			while (ComponentStack.Num() > 0)
			{
				USceneComponent* const CurrentComp = ComponentStack.Pop(/*bAllowShrinking=*/false);
				if (CurrentComp)
				{
					ComponentStack.Append(CurrentComp->GetAttachChildren());
					CurrentComp->SetVisibility(bEnabled);
					if (bEnabled)
					{
						// Re-enable only if it was enabled at startup
						CurrentComp->SetComponentTickEnabled(CurrentComp->PrimaryComponentTick.bStartWithTickEnabled);
					}
					else
					{
						CurrentComp->SetComponentTickEnabled(false);
					}
				}
			}
		}
	}

	// Enable/disable the ticking of CharacterMovementComponent as well
	ACharacter* Character = Cast<ACharacter>(&Actor);
	UCharacterMovementComponent* MovementComp = Character != nullptr ? Character->GetCharacterMovement() : nullptr;
	if (MovementComp != nullptr)
	{
		MovementComp->SetComponentTickEnabled(bEnabled);
	}

	// when we "suspend" the puppet actor we need to let the agent subsystem know by unregistering the agent component
	// associated with the actor. This will result in removing all the puppet-actor-specific fragments which in turn
	// will exclude the owner entity from being processed by puppet-specific processors (usually translators).
	if (UMassAgentComponent* AgentComp = Actor.FindComponentByClass<UMassAgentComponent>())
	{
		AgentComp->PausePuppet(!bEnabled);
	}
}

AActor* UMassCrowdRepresentationProcessor::GetOrSpawnActor(const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, const FTransform& Transform, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, const float Priority)
{
	FTransform RootTransform = Transform;
	
	if (const AActor* DefaultActor = RepresentationSubsystem->GetTemplateActorClass(TemplateActorIndex).GetDefaultObject())
	{
		if (const UCapsuleComponent* CapsuleComp = DefaultActor->FindComponentByClass<UCapsuleComponent>())
		{
			RootTransform.AddToTranslation(FVector(0.0f, 0.0f, CapsuleComp->GetScaledCapsuleHalfHeight()));
		}
	}

	return Super::GetOrSpawnActor(MassAgent, ActorInfo, RootTransform, TemplateActorIndex, SpawnRequestHandle, Priority);
}

void UMassCrowdRepresentationProcessor::TeleportActor(const FTransform& Transform, AActor& Actor, FMassCommandBuffer& CommandBuffer)
{
	FTransform RootTransform = Transform;

	if (const UCapsuleComponent* CapsuleComp = Actor.FindComponentByClass<UCapsuleComponent>())
	{
		const FVector HalfHeight(0.0f, 0.0f, CapsuleComp->GetScaledCapsuleHalfHeight());
		RootTransform.AddToTranslation(HalfHeight);
		const FVector RootLocation = RootTransform.GetLocation();
		const FVector SweepOffset(0.0f, 0.0f, 20.0f);
		const FVector Start = RootLocation + SweepOffset;
		const FVector End = RootLocation - SweepOffset;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(&Actor);
		FHitResult OutHit;
		if (Actor.GetWorld()->SweepSingleByChannel(OutHit, Start, End, Transform.GetRotation(), CapsuleComp->GetCollisionObjectType(), CapsuleComp->GetCollisionShape(), Params))
		{
			RootTransform.SetLocation(OutHit.Location);
		}
	}
	Super::TeleportActor(RootTransform, Actor, CommandBuffer);
}
