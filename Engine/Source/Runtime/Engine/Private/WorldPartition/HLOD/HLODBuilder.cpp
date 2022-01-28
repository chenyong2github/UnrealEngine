// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "AssetCompilingManager.h"

DEFINE_LOG_CATEGORY(LogHLODBuilder);


UHLODBuilder::UHLODBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UHLODBuilderSettings::UHLODBuilderSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

UHLODBuilderSettings* UHLODBuilder::CreateSettings(UHLODLayer* InHLODLayer) const
{
	return NewObject<UHLODBuilderSettings>(InHLODLayer);
}

bool UHLODBuilder::RequiresCompiledAssets() const
{
	return true;
}

bool UHLODBuilder::RequiresWarmup() const
{
	return true;
}

TArray<UPrimitiveComponent*> UHLODBuilder::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) const
{
	return TArray<UPrimitiveComponent*>();
}

void UHLODBuilder::Build(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<AActor*>& InSubActors)
{
	TArray<UPrimitiveComponent*> SubComponents = GatherPrimitiveComponents(InSubActors);
	if (SubComponents.IsEmpty())
	{
		return;
	}

	TArray<UPrimitiveComponent*> HLODPrimitives = CreateComponents(InHLODActor, InHLODLayer, SubComponents);
	HLODPrimitives.RemoveSwap(nullptr);

	for (UPrimitiveComponent* HLODPrimitive : HLODPrimitives)
	{
		// Disable collisions
		HLODPrimitive->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		HLODPrimitive->SetGenerateOverlapEvents(false);
		HLODPrimitive->SetCanEverAffectNavigation(false);
		HLODPrimitive->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
		HLODPrimitive->SetCanEverAffectNavigation(false);
		HLODPrimitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Enable optimizations
		HLODPrimitive->bComputeFastLocalBounds = true;
		HLODPrimitive->bComputeBoundsOnceForGame = true;

		if (InHLODLayer->GetLayerType() != EHLODLayerType::Instancing)
		{
			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(HLODPrimitive))
			{
				if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
				{
					// Set up ray tracing far fields
					StaticMeshComponent->bRayTracingFarField = StaticMesh->bSupportRayTracing;

					if (HLODPrimitive->GetPackage() == StaticMesh->GetPackage())
					{
						// Disable collisions on owned static mesh
						if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
						{							
							BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
							BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
						}
						
						// Rename owned static mesh
						StaticMesh->Rename(*MakeUniqueObjectName(StaticMesh->GetOuter(), StaticMesh->GetClass(), *FString::Printf(TEXT("StaticMesh_%s"), *InHLODLayer->GetName())).ToString());
					}
				}
			}
		}
	}

	if (!HLODPrimitives.IsEmpty())
	{
		InHLODActor->Modify();
		InHLODActor->SetHLODPrimitives(HLODPrimitives);
	}
}

TArray<UPrimitiveComponent*> UHLODBuilder::GatherPrimitiveComponents(const TArray<AActor*>& InActors)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;

	auto GatherPrimitivesFromActor = [&PrimitiveComponents](const AActor* Actor, const AActor* ParentActor = nullptr)
	{
		const TCHAR* Padding = ParentActor ? TEXT("    ") : TEXT("");
		UE_LOG(LogHLODBuilder, Verbose, TEXT("%s* Adding components from actor %s"), Padding, *Actor->GetName());
		for (UActorComponent* SubComponent : Actor->GetComponents())
		{
			if (SubComponent && SubComponent->IsHLODRelevant())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(SubComponent))
				{
					PrimitiveComponents.Add(PrimitiveComponent);

					if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(PrimitiveComponent))
					{
						UE_LOG(LogHLODBuilder, Verbose, TEXT("%s    * %s [%d instances]"), Padding, *ISMC->GetStaticMesh()->GetName(), ISMC->GetInstanceCount());
					}
					else if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(PrimitiveComponent))
					{
						UE_LOG(LogHLODBuilder, Verbose, TEXT("%s    * %s"), Padding, *SMC->GetStaticMesh()->GetName());
					}
				}
				else
				{
					UE_LOG(LogHLODBuilder, Warning, TEXT("Component \"%s\" is marked as HLOD-relevant but this type of component currently unsupported."), *SubComponent->GetFullName());
				}
			}
		}
	};

	TSet<AActor*> UnderlyingActors;

	for (AActor* Actor : InActors)
	{
		if (!Actor->IsHLODRelevant())
		{
			continue;
		}

		// Gather primitives from the Actor
		GatherPrimitivesFromActor(Actor);
	}

	return PrimitiveComponents;
}

#endif // WITH_EDITOR
