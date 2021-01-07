// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericMeshPipeline.h"

#include "CoreMinimal.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletalMeshNode.h"
#include "InterchangeSkeletonNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

bool UInterchangeGenericMeshPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas)
{
	PreImportPipelineHelper.Initialize(BaseNodeContainer, SourceDatas, this);

	//Make sure all node graph changes, merges, etc have been done before calling this feature since the rename is done only if there is one imported asset.
	PreImportPipelineHelper.ChangeAssetNameToUseFilename();
	
	return true;
}

void UInterchangeGenericMeshPipeline::FPreImportPipelineHelper::Initialize(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas, UInterchangeGenericMeshPipeline* InPipelineOwner)
{
	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas = InSourceDatas;
	PipelineOwner = InPipelineOwner;

	RefreshMeshNodes();
}

bool UInterchangeGenericMeshPipeline::FPreImportPipelineHelper::IsValid() const 
{
	//Check all the initialized data
	if (!BaseNodeContainer)
	{
		return false;
	}
	
	for (const UInterchangeSourceData* SourceData : SourceDatas)
	{
		if (!SourceData)
		{
			return false;
		}
	}
	
	if (!PipelineOwner)
	{
		return false;
	}

	return true;
}

void UInterchangeGenericMeshPipeline::FPreImportPipelineHelper::RefreshMeshNodes()
{
	if (!IsValid())
	{
		return;
	}
	//Get all node we want this pipeline interact with.
	BaseNodeContainer->IterateNodes([this](const FString& NodeUID, UInterchangeBaseNode* Node)
	{
		if (UInterchangeSkeletalMeshNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshNode>(Node))
		{
			SkeletalMeshNodes.Add(SkeletalMeshNode);
		}
	});
}

void UInterchangeGenericMeshPipeline::FPreImportPipelineHelper::ChangeAssetNameToUseFilename()
{
	if (!IsValid())
	{
		return;
	}

	//Is the feature enable or not
	if (!PipelineOwner->bUseFilenameToNameMeshes)
	{
		return;
	}

	if (SourceDatas.Num() != 1 || !(SourceDatas[0]))
	{
		return;
	}

	int32 MeshCount = 0;
	UInterchangeBaseNode* NodeToRename = nullptr;
	//Add all enable mesh count here, The Use filename has name feature work only if there is one asset to import
	//Start by the skeletalmesh
	for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < SkeletalMeshNodes.Num(); ++SkeletalMeshIndex)
	{
		if (SkeletalMeshNodes[SkeletalMeshIndex]->IsEnabled())
		{
			MeshCount++;
			if (MeshCount == 1)
			{
				NodeToRename = SkeletalMeshNodes[SkeletalMeshIndex];
			}
			else
			{
				NodeToRename = nullptr;
			}
		}
	}
	//Iterate staticmesh

	if (!NodeToRename)
	{
		return;
	}

	//If the NodeToRename pointer is valid we should have only one mesh count
	if (!ensure(MeshCount == 1))
	{
		return;
	}

	FString SourceFilename = SourceDatas[0]->GetFilename();
	FString OverrideMeshName = FPaths::GetBaseFilename(SourceFilename);
	NodeToRename->SetDisplayLabel(OverrideMeshName);

	//For skeletalmesh we need to rename also the skeleton asset node
	if (UInterchangeSkeletalMeshNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshNode>(NodeToRename))
	{
		TArray<FString> LodDataUniqueIds;
		SkeletalMeshNode->GetLodDataUniqueIds(LodDataUniqueIds);
		if (LodDataUniqueIds.Num() > 0)
		{
			//Get the LOD 0 node
			if (UInterchangeSkeletalMeshLodDataNode* SkeletalMeshLodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(BaseNodeContainer->GetNode(LodDataUniqueIds[0])))
			{
				FString SkeletonNodeId;
				if (SkeletalMeshLodDataNode->GetCustomSkeletonID(SkeletonNodeId))
				{
					if (UInterchangeSkeletonNode* SkeletonNode = Cast<UInterchangeSkeletonNode>(BaseNodeContainer->GetNode(SkeletonNodeId)))
					{
						SkeletonNode->SetDisplayLabel(OverrideMeshName + TEXT("_Skeleton"));
					}
				}
			}
		}
	}
}