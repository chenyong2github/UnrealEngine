// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UVGenerationDataprepOperation.h"

#include "UVGenerationFlattenMapping.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "MeshDescriptionOperations.h"
#include "OverlappingCorners.h"

void UUVGenerationFlattenMappingOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	TSet<UStaticMesh*> StaticMeshes;

	for (UObject* Object : InContext.Objects)
	{
		if (AActor* Actor = Cast<AActor>(Object))
		{
			if (Actor->IsPendingKillOrUnreachable())
			{
				continue;
			}

			TInlineComponentArray<UStaticMeshComponent*> ComponentArray;
			Actor->GetComponents<UStaticMeshComponent>(ComponentArray);
			for (UStaticMeshComponent* MeshComponent : ComponentArray)
			{
				if (UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
				{
					StaticMeshes.Add(StaticMesh);
				}
			}
		}
		else if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Object))
		{
			if (UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
			{
				StaticMeshes.Add(StaticMesh);
			}
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object))
		{
			StaticMeshes.Add(StaticMesh);
		}
	}

	for (UStaticMesh* StaticMesh : StaticMeshes)
	{
		for (int32 LodIndex = 0; LodIndex < StaticMesh->GetNumSourceModels(); ++LodIndex)
		{
			if (!StaticMesh->IsMeshDescriptionValid(LodIndex))
			{
				continue;
			}

			FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(LodIndex).BuildSettings;
			FMeshDescription& MeshDescription = *StaticMesh->GetMeshDescription(LodIndex);

			UUVGenerationFlattenMapping::GenerateUVs(MeshDescription, UVChannel, BuildSettings.bRemoveDegenerates, AngleThreshold, AreaWeight);
		}
	}
}