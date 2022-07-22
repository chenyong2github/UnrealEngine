// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericMeshPipeline.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

void UInterchangeGenericMeshPipeline::AdjustSettingsForContext(EInterchangePipelineContext ImportType, TObjectPtr<UObject> ReimportAsset)
{
	Super::AdjustSettingsForContext(ImportType, ReimportAsset);

	check(!CommonSkeletalMeshesAndAnimationsProperties.IsNull());
	if (ImportType == EInterchangePipelineContext::None)
	{
		//We do not change the setting if we are in editing context
		return;
	}

	const bool bIsReimport = IsReimportContext();

	//Avoid creating physics asset when importing a LOD or the alternate skinning
	if (ImportType == EInterchangePipelineContext::AssetCustomLODImport
		|| ImportType == EInterchangePipelineContext::AssetCustomLODReimport
		|| ImportType == EInterchangePipelineContext::AssetAlternateSkinningImport
		|| ImportType == EInterchangePipelineContext::AssetAlternateSkinningReimport)
	{
		bCreatePhysicsAsset = false;
		PhysicsAsset = nullptr;
	}

	TArray<FString> HideCategories;
	if (ImportType == EInterchangePipelineContext::AssetReimport)
	{
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(ReimportAsset))
		{

			//Set the skeleton to the current asset skeleton
			CommonSkeletalMeshesAndAnimationsProperties->Skeleton = SkeletalMesh->GetSkeleton();
			bImportStaticMeshes = false;
			HideCategories.Add(TEXT("Static Meshes"));
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(ReimportAsset))
		{
			HideCategories.Add(TEXT("Skeletal Meshes"));
			HideCategories.Add(TEXT("Common Skeletal Meshes and Animations"));
		}
		else if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(ReimportAsset))
		{
			HideCategories.Add(TEXT("Static Meshes"));
			HideCategories.Add(TEXT("Skeletal Meshes"));
			HideCategories.Add(TEXT("Common Meshes"));
		}
		else if (ReimportAsset)
		{
			HideCategories.Add(TEXT("Static Meshes"));
			HideCategories.Add(TEXT("Skeletal Meshes"));
			HideCategories.Add(TEXT("Common Meshes"));
			HideCategories.Add(TEXT("Common Skeletal Meshes and Animations"));
		}
	}

	if (UInterchangePipelineBase* OuterMostPipeline = GetMostPipelineOuter())
	{
		for (const FString& HideCategoryName : HideCategories)
		{
			constexpr bool bDoTransientSubPipeline = true;
			HidePropertiesOfCategory(OuterMostPipeline, this, HideCategoryName, bDoTransientSubPipeline);
		}
	}
}

void UInterchangeGenericMeshPipeline::PreDialogCleanup(const FName PipelineStackName)
{
	PhysicsAsset = nullptr;
}

void UInterchangeGenericMeshPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMeshPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}

	PipelineMeshesUtilities = UInterchangePipelineMeshesUtilities::CreateInterchangePipelineMeshesUtilities(BaseNodeContainer);



	//Create skeletalmesh factory nodes
	ExecutePreImportPipelineSkeletalMesh();

	//Create staticmesh factory nodes
	ExecutePreImportPipelineStaticMesh();
}

void UInterchangeGenericMeshPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& FactoryNodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	//We do not use the provided base container since ExecutePreImportPipeline cache it
	//We just make sure the same one is pass in parameter
	if (!InBaseNodeContainer || !ensure(BaseNodeContainer == InBaseNodeContainer) || !CreatedAsset)
	{
		return;
	}

	const UInterchangeFactoryBaseNode* FactoryNode = BaseNodeContainer->GetFactoryNode(FactoryNodeKey);
	if (!FactoryNode)
	{
		return;
	}

	//Set the last content type import
	LastSkeletalMeshImportContentType = SkeletalMeshImportContentType;

	PostImportSkeletalMesh(CreatedAsset, FactoryNode);

	//Finish the physics asset import, it need the skeletal mesh render data to create the physics collision geometry
	PostImportPhysicsAssetImport(CreatedAsset, FactoryNode);
}

void UInterchangeGenericMeshPipeline::SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex)
{
	if (ReimportObjectClass == USkeletalMesh::StaticClass())
	{
		switch (SourceFileIndex)
		{
			case 0:
			{
				//Geo and skinning
				SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::All;
			}
			break;

			case 1:
			{
				//Geo only
				SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::Geometry;
			}
			break;

			case 2:
			{
				//Skinning only
				SkeletalMeshImportContentType = EInterchangeSkeletalMeshContentType::SkinningWeights;
			}
			break;

			default:
			{
				//In case SourceFileIndex == INDEX_NONE //No specified options, we use the last imported content type
				SkeletalMeshImportContentType = LastSkeletalMeshImportContentType;
			}
		};
	}
}

