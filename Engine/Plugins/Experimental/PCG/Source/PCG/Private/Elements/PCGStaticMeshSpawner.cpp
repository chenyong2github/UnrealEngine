// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGStaticMeshSpawner.h"

#include "PCGHelpers.h"
#include "Helpers/PCGActorHelpers.h"
#include "Data/PCGPointData.h"

#include "Components/InstancedStaticMeshComponent.h"
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

bool FPCGStaticMeshSpawnerElement::ExecuteInternal(FPCGContext* Context) const
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
		// Todo: we could likely pre-load these meshes asynchronously in the settings
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
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		AActor* TargetActor = SpatialData->TargetActor;

		if (!TargetActor)
		{
			PCGE_LOG(Error, "Invalid target actor");
			continue;
		}

		// First, create target instance transforms
		const UPCGPointData* PointData = SpatialData->ToPointData(Context);

		if (!PointData)
		{
			PCGE_LOG(Error, "Unable to get point data from input");
			continue;
		}

		// Assign points to entries
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::SelectEntries);
			const TArray<FPCGPoint>& Points = PointData->GetPoints();

			for (const FPCGPoint& Point : Points)
			{
				if (Point.Density <= 0.0f)
				{
					continue;
				}

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

				UInstancedStaticMeshComponent* ISMC = UPCGActorHelpers::GetOrCreateISMC(TargetActor, Context->SourceComponent, Instances.Mesh);

				// TODO: add scaling
				// TODO: document these arguments
				ISMC->NumCustomDataFloats = 0;
				ISMC->AddInstances(Instances.Instances, false, true);
				ISMC->UpdateBounds();

				PCGE_LOG(Verbose, "Added %d instances of %s on actor %s", Instances.Instances.Num(), *Instances.Mesh->GetFName().ToString(), *TargetActor->GetFName().ToString());
			}
		}
	}

	return true;
}
