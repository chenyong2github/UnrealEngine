// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericAssetsPipeline.h"

#include "Animation/Skeleton.h"
#include "CoreMinimal.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeStaticMeshLodDataNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

UInterchangeGenericAssetsPipeline::UInterchangeGenericAssetsPipeline()
{
	TexturePipeline = CreateDefaultSubobject<UInterchangeGenericTexturePipeline>("TexturePipeline");
	MaterialPipeline = CreateDefaultSubobject<UInterchangeGenericMaterialPipeline>("MaterialPipeline");
	CommonMeshesProperties = CreateDefaultSubobject<UInterchangeGenericCommonMeshesProperties>("CommonMeshesProperties");
	CommonSkeletalMeshesAndAnimationsProperties = CreateDefaultSubobject<UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties>("CommonSkeletalMeshesAndAnimationsProperties");
	MeshPipeline = CreateDefaultSubobject<UInterchangeGenericMeshPipeline>("MeshPipeline");
	MeshPipeline->CommonMeshesProperties = CommonMeshesProperties;
	MeshPipeline->CommonSkeletalMeshesAndAnimationsProperties = CommonSkeletalMeshesAndAnimationsProperties;
	AnimationPipeline = CreateDefaultSubobject<UInterchangeGenericAnimationPipeline>("AnimationPipeline");
	AnimationPipeline->CommonSkeletalMeshesAndAnimationsProperties = CommonSkeletalMeshesAndAnimationsProperties;
}

void UInterchangeGenericAssetsPipeline::PreDialogCleanup(const FName PipelineStackName)
{
	check(!CommonSkeletalMeshesAndAnimationsProperties.IsNull())
	//We always clean the pipeline skeleton when showing the dialog
	CommonSkeletalMeshesAndAnimationsProperties->Skeleton = nullptr;

	if (TexturePipeline)
	{
		TexturePipeline->PreDialogCleanup(PipelineStackName);
	}

	if (MaterialPipeline)
	{
		MaterialPipeline->PreDialogCleanup(PipelineStackName);
	}
	
	if (MeshPipeline)
	{
		MeshPipeline->PreDialogCleanup(PipelineStackName);
	}

	if (AnimationPipeline)
	{
		AnimationPipeline->PreDialogCleanup(PipelineStackName);
	}
	
	SaveSettings(PipelineStackName);
}

bool UInterchangeGenericAssetsPipeline::IsSettingsAreValid() const
{
	if (TexturePipeline && !TexturePipeline->IsSettingsAreValid())
	{
		return false;
	}

	if (MaterialPipeline && !MaterialPipeline->IsSettingsAreValid())
	{
		return false;
	}

	if (CommonMeshesProperties && !CommonMeshesProperties->IsSettingsAreValid())
	{
		return false;
	}

	if (CommonSkeletalMeshesAndAnimationsProperties && !CommonSkeletalMeshesAndAnimationsProperties->IsSettingsAreValid())
	{
		return false;
	}

	if (MeshPipeline && !MeshPipeline->IsSettingsAreValid())
	{
		return false;
	}

	if (AnimationPipeline && !AnimationPipeline->IsSettingsAreValid())
	{
		return false;
	}

	return Super::IsSettingsAreValid();
}

void UInterchangeGenericAssetsPipeline::SetupReimportData(TObjectPtr<UObject> ReimportObject)
{
	if (TexturePipeline)
	{
		TexturePipeline->SetupReimportData(ReimportObject);
	}

	if (MaterialPipeline)
	{
		MaterialPipeline->SetupReimportData(ReimportObject);
	}

	if (MeshPipeline)
	{
		MeshPipeline->SetupReimportData(ReimportObject);
	}

	if (AnimationPipeline)
	{
		AnimationPipeline->SetupReimportData(ReimportObject);
	}
}

void UInterchangeGenericAssetsPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	check(!CommonSkeletalMeshesAndAnimationsProperties.IsNull());

	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericAssetsPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	//////////////////////////////////////////////////////////////////////////
	//Make sure all options go together
	
	//When we import only animation we need to prevent material and physic asset to be created
	if (CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations)
	{
		MaterialPipeline->MaterialImport = EInterchangeMaterialImportOption::DoNotImport;
		MeshPipeline->bImportStaticMeshes = false;
		MeshPipeline->bCreatePhysicsAsset = false;
		MeshPipeline->PhysicsAsset = nullptr;
		TexturePipeline->bImportTextures = false;
	}

	//////////////////////////////////////////////////////////////////////////


	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}

	if (TexturePipeline)
	{
		TexturePipeline->ScriptedExecutePreImportPipeline(InBaseNodeContainer, InSourceDatas);
	}

	if (MaterialPipeline)
	{
		MaterialPipeline->ScriptedExecutePreImportPipeline(InBaseNodeContainer, InSourceDatas);
	}

	if (MeshPipeline)
	{
		MeshPipeline->ScriptedExecutePreImportPipeline(InBaseNodeContainer, InSourceDatas);
	}

	if (AnimationPipeline)
	{
		AnimationPipeline->ScriptedExecutePreImportPipeline(InBaseNodeContainer, InSourceDatas);
	}

	ImplementUseSourceNameForAssetOption();
	//Make sure all factory nodes have the specified strategy
	BaseNodeContainer->IterateNodes([ReimportStrategyClosure = ReimportStrategy](const FString& NodeUid, UInterchangeBaseNode* Node)
		{
			if (Node->GetNodeContainerType() == EInterchangeNodeContainerType::FactoryData)
			{
				Node->SetReimportStrategyFlags(ReimportStrategyClosure);
			}
		});
}

void UInterchangeGenericAssetsPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	if (TexturePipeline)
	{
		TexturePipeline->ScriptedExecutePostImportPipeline(InBaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	}

	if (MaterialPipeline)
	{
		MaterialPipeline->ScriptedExecutePostImportPipeline(InBaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	}

	if (MeshPipeline)
	{
		MeshPipeline->ScriptedExecutePostImportPipeline(InBaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	}

	if (AnimationPipeline)
	{
		AnimationPipeline->ScriptedExecutePostImportPipeline(InBaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	}
}

void UInterchangeGenericAssetsPipeline::SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex)
{
	if (TexturePipeline)
	{
		TexturePipeline->ScriptedSetReimportSourceIndex(ReimportObjectClass, SourceFileIndex);
	}

	if (MaterialPipeline)
	{
		MaterialPipeline->ScriptedSetReimportSourceIndex(ReimportObjectClass, SourceFileIndex);
	}

	if (MeshPipeline)
	{
		MeshPipeline->ScriptedSetReimportSourceIndex(ReimportObjectClass, SourceFileIndex);
	}

	if (AnimationPipeline)
	{
		AnimationPipeline->ScriptedSetReimportSourceIndex(ReimportObjectClass, SourceFileIndex);
	}
}

void UInterchangeGenericAssetsPipeline::ImplementUseSourceNameForAssetOption()
{
	if (bUseSourceNameForAsset)
	{
		const UClass* SkeletalMeshFactoryNodeClass = UInterchangeSkeletalMeshFactoryNode::StaticClass();
		TArray<FString> SkeletalMeshNodeUids;
		BaseNodeContainer->GetNodes(SkeletalMeshFactoryNodeClass, SkeletalMeshNodeUids);

		const UClass* StaticMeshFactoryNodeClass = UInterchangeStaticMeshFactoryNode::StaticClass();
		TArray<FString> StaticMeshNodeUids;
		BaseNodeContainer->GetNodes(StaticMeshFactoryNodeClass, StaticMeshNodeUids);

		const UClass* AnimSequenceFactoryNodeClass = UInterchangeAnimSequenceFactoryNode::StaticClass();
		TArray<FString> AnimSequenceNodeUids;
		BaseNodeContainer->GetNodes(AnimSequenceFactoryNodeClass, AnimSequenceNodeUids);

		//If we import only one mesh, we want to rename the mesh using the file name.
		const int32 MeshesImportedNodeCount = SkeletalMeshNodeUids.Num() + StaticMeshNodeUids.Num();

		//SkeletalMesh
		MeshPipeline->ImplementUseSourceNameForAssetOptionSkeletalMesh(MeshesImportedNodeCount, bUseSourceNameForAsset);

		//StaticMesh
		if (MeshesImportedNodeCount == 1 && StaticMeshNodeUids.Num() > 0)
		{
			UInterchangeStaticMeshFactoryNode* StaticMeshNode = Cast<UInterchangeStaticMeshFactoryNode>(BaseNodeContainer->GetNode(StaticMeshNodeUids[0]));
			const FString DisplayLabelName = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename());
			StaticMeshNode->SetDisplayLabel(DisplayLabelName);
		}

		//Animation, simply look if we import only 1 animation before applying the option to animation
		if (AnimSequenceNodeUids.Num() == 1)
		{
			UInterchangeAnimSequenceFactoryNode* AnimSequenceNode = Cast<UInterchangeAnimSequenceFactoryNode>(BaseNodeContainer->GetNode(AnimSequenceNodeUids[0]));
			const FString DisplayLabelName = FPaths::GetBaseFilename(SourceDatas[0]->GetFilename()) + TEXT("_Anim");
			AnimSequenceNode->SetDisplayLabel(DisplayLabelName);
		}
	}

}
