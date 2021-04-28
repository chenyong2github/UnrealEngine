// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericAssetsPipeline.generated.h"

class UInterchangeTextureNode;
class UInterchangeTextureFactoryNode;
class UInterchangeMaterialNode;
class UInterchangeMaterialFactoryNode;
class UInterchangeMeshNode;
class UInterchangeSceneNode;

/**
 * This pipeline is the generic pipeline option for all meshes type and should be call before specialized Mesh pipeline (like generic static mesh or skeletal mesh pipelines)
 * All shared import options between mesh type should be added here.
 *
 */
UCLASS(BlueprintType)
class INTERCHANGEPIPELINES_API UInterchangeGenericAssetsPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////
	// BEGIN Pre import pipeline properties

	/** If enable and there is only one mesh in the translated nodes, the Source data name (filename) will be use to name the asset */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Miscellaneous")
	//bool bUseSourceDataToNameMeshes = true;

	// END Pre import pipeline properties
	//////////////////////////////////////////////////////////////////////////

protected:

	virtual bool ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;

	//virtual bool ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FName& NodeKey, UObject* CreatedAsset) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		//If a blueprint or python derived from this class, it will be execute on the game thread since we cannot currently execute script outside of the game thread, even if this function return true.
		return true;
	}

	//virtual bool ExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer) override;
private:

	TObjectPtr<UInterchangeBaseNodeContainer> BaseNodeContainer;
	TArray<const UInterchangeSourceData*> SourceDatas;

	
	/** Texture translated assets nodes */
	TArray<TObjectPtr<UInterchangeTextureNode>> TextureNodes;

	/** Texture factory assets nodes */
	TArray<TObjectPtr<UInterchangeTextureFactoryNode>> TextureFactoryNodes;

	TObjectPtr<UInterchangeTextureFactoryNode> CreateTexture2DFactoryNode(const TObjectPtr<UInterchangeTextureNode> TextureNode);

	
	/** Material translated assets nodes */
	TArray<TObjectPtr<UInterchangeMaterialNode>> MaterialNodes;
	
	/** Material factory assets nodes */
	TArray<TObjectPtr<UInterchangeMaterialFactoryNode>> MaterialFactoryNodes;
	
	TObjectPtr<UInterchangeMaterialFactoryNode> CreateMaterialFactoryNode(const TObjectPtr<UInterchangeMaterialNode> MaterialNode);

	
	/** Mesh translated assets nodes */
	TArray<TObjectPtr<UInterchangeMeshNode>> MeshNodes;
	
	/** Skeletal mesh factory assets nodes */
	//TArray<TObjectPtr<UInterchangeSkeletalMeshFactoryNode>> SkeletalMeshFactoryNodes;
	
	/** Static mesh factory assets nodes */
	//TArray<TObjectPtr<UInterchangeStaticMeshFactoryNode>> StaticMeshFactoryNodes;
	
	/** This function can create a UInterchangeSkeletalMeshFactoryNode or a UInterchangeStaticMeshFactoryNode */
	TObjectPtr<UInterchangeBaseNode> CreateMeshFactoryNode(const TObjectPtr<UInterchangeMeshNode> MeshNode);

	/** Mesh translated scene nodes */
	TArray<TObjectPtr<UInterchangeSceneNode>> SceneNodes;
};


