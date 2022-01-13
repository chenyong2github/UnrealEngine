// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGStaticMeshSpawner.h"

#include "PCGComponent.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"

FPCGElementPtr UPCGStaticMeshSpawnerSettings::CreateElement() const
{
	return MakeShared<FPCGStaticMeshSpawnerElement>();
}

struct FWeightedMeshAndInstances
{
	int Weight = 0;
	UStaticMesh* Mesh = nullptr;
	TArray<FTransform> Instances;
};

bool FPCGStaticMeshSpawnerElement::Execute(FPCGContextPtr Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute);
	// TODO : time-sliced implementation
	const UPCGStaticMeshSpawnerSettings* Settings = Context->GetInputSettings<UPCGStaticMeshSpawnerSettings>();
	check(Settings);

	// TODO : see if we could do async mesh loading here
	TArray<FWeightedMeshAndInstances> WeightedEntries;
	int TotalWeight = 0;
	for (const FPCGStaticMeshSpawnerEntry& Entry : Settings->Meshes)
	{
		if (Entry.Weight <= 0)
			continue;

		TotalWeight += Entry.Weight;

		FWeightedMeshAndInstances& WeightedEntry = WeightedEntries.Emplace_GetRef();
		WeightedEntry.Weight = TotalWeight;
		WeightedEntry.Mesh = Entry.Mesh.LoadSynchronous();
	}

	if (TotalWeight <= 0)
	{
		return true;
	}

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();

	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialData)
		{
			// Data type mismatch
			continue;
		}

		AActor* TargetActor = SpatialData->TargetActor;

		if (!TargetActor)
		{
			// No target actor
			continue;
		}

		// First, create target instance transforms
		const UPCGPointData* PointData = SpatialData->ToPointData();

		if (!PointData)
		{
			continue;
		}

		// Assign points to entries
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::SelectEntries);
			const TArray<FPCGPoint>& Points = PointData->GetPoints();

			for (const FPCGPoint& Point : Points)
			{
				FRandomStream RandomSource(PCGHelpers::ComputeSeed(Point.Seed, Settings->Seed));
				int RandomWeightedPick = RandomSource.RandRange(0, TotalWeight - 1);

				int RandomPick = 0;
				while (RandomPick < WeightedEntries.Num() && WeightedEntries[RandomPick].Weight <= RandomWeightedPick)
				{
					++RandomPick;
				}

				if (RandomPick < WeightedEntries.Num())
				{
					WeightedEntries[RandomPick].Instances.Emplace(Point.Transform);
				}
			}
		}

		// Populate the (H)ISM from the previously prepared entries
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::PopulateISMs);

			// Second, add the instances
			for (FWeightedMeshAndInstances& Instances : WeightedEntries)
			{
				if (!Instances.Mesh || Instances.Instances.Num() == 0)
				{
					continue;
				}

				UInstancedStaticMeshComponent* ISMC = GetOrCreateISMC(TargetActor, Context->SourceComponent, Instances.Mesh);

				// TODO: add scaling
				// TODO: document these arguments
				ISMC->AddInstances(Instances.Instances, false, true);
				ISMC->UpdateBounds();
			}
		}
	}

	return true;
}

UInstancedStaticMeshComponent* FPCGStaticMeshSpawnerElement::GetOrCreateISMC(AActor* InTargetActor, const UPCGComponent* InSourceComponent, UStaticMesh* InMesh) const
{
	check(InTargetActor != nullptr && InMesh != nullptr);

	TArray<UInstancedStaticMeshComponent*> ISMCs;
	InTargetActor->GetComponents<UInstancedStaticMeshComponent>(ISMCs);

	for (UInstancedStaticMeshComponent* ISMC : ISMCs)
	{
		if (ISMC->GetStaticMesh() == InMesh && (!InSourceComponent || ISMC->ComponentTags.Contains(InSourceComponent->GetFName())))
		{
			return ISMC;
		}
	}

	InTargetActor->Modify();

	// Otherwise, create a new component
	// TODO: use static mesh component if there's only one instance
	// TODO: add hism/ism switch or better yet, use a template component
	UInstancedStaticMeshComponent* ISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(InTargetActor);
	ISMC->SetStaticMesh(InMesh);
	// TODO: add way to do material overrides, maybe on a finer basis as well
	ISMC->RegisterComponent();
	InTargetActor->AddInstanceComponent(ISMC);
	ISMC->SetMobility(EComponentMobility::Static);
	// TODO: add option for collision, or use a template
	ISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISMC->AttachToComponent(InTargetActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

	if (InSourceComponent)
	{
		ISMC->ComponentTags.Add(InSourceComponent->GetFName());
		ISMC->ComponentTags.Add(PCGHelpers::DefaultPCGTag);
	}

	return ISMC;
}
