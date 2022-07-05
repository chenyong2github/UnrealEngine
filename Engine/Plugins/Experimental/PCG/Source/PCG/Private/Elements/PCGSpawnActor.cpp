// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSpawnActor.h"

#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "PCGManagedResource.h"

#include "Data/PCGPointData.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UPCGNode* UPCGSpawnActorSettings::CreateNode() const
{
	return NewObject<UPCGSpawnActorNode>();
}

FPCGElementPtr UPCGSpawnActorSettings::CreateElement() const
{
	return MakeShared<FPCGSpawnActorElement>();
}

UPCGGraph* UPCGSpawnActorSettings::GetSubgraph() const
{
	if(!TemplateActorClass || TemplateActorClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}

	TArray<UActorComponent*> PCGComponents;
	UPCGActorHelpers::GetActorClassDefaultComponents(TemplateActorClass, PCGComponents, UPCGComponent::StaticClass());

	if (PCGComponents.IsEmpty())
	{
		return nullptr;
	}

	for (UActorComponent* Component : PCGComponents)
	{
		if (UPCGComponent* PCGComponent = Cast<UPCGComponent>(Component))
		{
			if (PCGComponent->GetGraph() && PCGComponent->bActivated)
			{
				return PCGComponent->GetGraph();
			}
		}
	}

	return nullptr;
}

#if WITH_EDITOR
bool UPCGSpawnActorSettings::IsStructuralProperty(const FName& InPropertyName) const
{
	return InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGSpawnActorSettings, TemplateActorClass) || 
		InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGSpawnActorSettings, Option) ||
		Super::IsStructuralProperty(InPropertyName);
}
#endif // WITH_EDITOR

TObjectPtr<UPCGGraph> UPCGSpawnActorNode::GetSubgraph() const
{
	TObjectPtr<UPCGSpawnActorSettings> Settings = Cast<UPCGSpawnActorSettings>(DefaultSettings);
	return (Settings && Settings->Option != EPCGSpawnActorOption::NoMerging) ? Settings->GetSubgraph() : nullptr;
}

bool FPCGSpawnActorElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCSpawnActorElement::Execute);

	const UPCGSpawnActorSettings* Settings = Context->GetInputSettings<UPCGSpawnActorSettings>();
	check(Settings);

	// TODO: Temporary override to verify that soft path can also be overridden
	// Should probably be handled somewhere else (helpers?).
	TSubclassOf<AActor> TemplateActorClass = Settings->TemplateActorClass;

	if (UPCGParamData* PCGParams = Context->InputData.GetParams())
	{
		FString TemplateActorClassPath = TemplateActorClass ? TemplateActorClass->GetPathName() : "";
		FString OverriddenTemplateActorClassPath = PCGSettingsHelpers::GetValue(GET_MEMBER_NAME_CHECKED(UPCGSpawnActorSettings, TemplateActorClass), TemplateActorClassPath, PCGParams);

		if (OverriddenTemplateActorClassPath != TemplateActorClassPath)
		{
			UClass* OverriddenTemplateActorClass = FSoftClassPath(OverriddenTemplateActorClassPath).ResolveClass();
			if (OverriddenTemplateActorClass && OverriddenTemplateActorClass->IsChildOf<AActor>())
			{
				TemplateActorClass = OverriddenTemplateActorClass;
			}
		}
	}

	// Early out
	if(!TemplateActorClass || TemplateActorClass->HasAnyClassFlags(CLASS_Abstract))
	{
		PCGE_LOG(Error, "Invalid template actor class (%s)", TemplateActorClass ? *TemplateActorClass->GetFName().ToString() : TEXT("None"));
		return true;
	}

	bool bShouldPassThroughInputs = (Settings->Option != EPCGSpawnActorOption::NoMerging && Settings->GetSubgraph() != nullptr);

	// Pass-through exclusions & settings
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialData)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		AActor* TargetActor = SpatialData->TargetActor;

		if (!TargetActor)
		{
			PCGE_LOG(Error, "Invalid target actor");
			continue;
		}

		const bool bHasAuthority = !Context->SourceComponent || (Context->SourceComponent->GetOwner() && Context->SourceComponent->GetOwner()->HasAuthority());
		const bool bSpawnedActorsRequireAuthority = TemplateActorClass->GetDefaultObject<AActor>()->GetIsReplicated();

		// First, create target instance transforms
		const UPCGPointData* PointData = SpatialData->ToPointData(Context);

		if (!PointData)
		{
			PCGE_LOG(Error, "Unable to get point data from input");
			continue;
		}

		const TArray<FPCGPoint>& Points = PointData->GetPoints();

		if (Points.IsEmpty())
		{
			PCGE_LOG(Verbose, "Skipped - no points");
			continue;
		}

		// Spawn actors/populate ISM
		{
			UInstancedStaticMeshComponent* ISMC = nullptr;
			TArray<FTransform> Instances;

			// If we are collapsing actors, we need to get the mesh & prep the ISMC
			if (Settings->Option == EPCGSpawnActorOption::CollapseActors)
			{
				TArray<UActorComponent*> Components;
				UPCGActorHelpers::GetActorClassDefaultComponents(TemplateActorClass, Components, UStaticMeshComponent::StaticClass());
				UStaticMeshComponent* FirstSMC = nullptr;
				UStaticMesh* Mesh = nullptr;

				for (UActorComponent* Component : Components)
				{
					if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Component))
					{
						FirstSMC = SMC;
						Mesh = SMC->GetStaticMesh();
						if (Mesh)
						{
							break;
						}
					}
				}

				if (Mesh)
				{
					FPCGISMCBuilderParameters Params;
					Params.Mesh = Mesh;

					ISMC = UPCGActorHelpers::GetOrCreateISMC(TargetActor, Context->SourceComponent, Params);
					UEngine::CopyPropertiesForUnrelatedObjects(FirstSMC, ISMC);
				}
				else
				{
					PCGE_LOG(Error, "No supported mesh found");
				}
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSpawnActorElement::ExecuteInternal::SpawnActors);
				if (Settings->Option == EPCGSpawnActorOption::CollapseActors && ISMC)
				{
					for (const FPCGPoint& Point : Points)
					{
						Instances.Add(Point.Transform);
					}
				}
				else if (Settings->Option != EPCGSpawnActorOption::CollapseActors && (bHasAuthority || !bSpawnedActorsRequireAuthority))
				{
					FActorSpawnParameters SpawnParams;
					SpawnParams.Owner = TargetActor;
					SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

					UPCGManagedActors* ManagedActors = NewObject<UPCGManagedActors>(Context->SourceComponent);

					for (const FPCGPoint& Point : Points)
					{
						AActor* GeneratedActor = TargetActor->GetWorld()->SpawnActor(TemplateActorClass, &Point.Transform, SpawnParams);
						GeneratedActor->Tags.Add(PCGHelpers::DefaultPCGActorTag);
						GeneratedActor->AttachToActor(TargetActor, FAttachmentTransformRules::KeepWorldTransform);

						ManagedActors->GeneratedActors.Add(GeneratedActor);

						// If the actor spawned has a PCG component, either generate it or mark it as generated if we pass through its inputs
						TArray<UPCGComponent*> PCGComponents;
						GeneratedActor->GetComponents<UPCGComponent>(PCGComponents);

						for (UPCGComponent* PCGComponent : PCGComponents)
						{
							if (!bShouldPassThroughInputs)
							{
								PCGComponent->Generate();
							}
							else
							{
								PCGComponent->bActivated = false;
							}
						}
					}

					Context->SourceComponent->AddToManagedResources(ManagedActors);

					PCGE_LOG(Verbose, "Generated %d actors", Points.Num());
				}
			}

			// Finalize
			if (ISMC && Instances.Num() > 0)
			{
				ISMC->NumCustomDataFloats = 0;
				ISMC->AddInstances(Instances, false, true);
				ISMC->UpdateBounds();

				PCGE_LOG(Verbose, "Added %d ISM instances", Instances.Num());
			}
		}

		// Finally, pass through the input if we're merging the PCG here
		if (bShouldPassThroughInputs)
		{
			Outputs.Add(Input);
		}
	}

	return true;
}

#if WITH_EDITOR
EPCGSettingsType UPCGSpawnActorSettings::GetType() const
{
	return GetSubgraph() ? EPCGSettingsType::Subgraph : EPCGSettingsType::Spawner;
}
#endif // WITH_EDITOR