// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericScenesPipeline.generated.h"

class UInterchangeActorFactoryNode;
class UInterchangeSceneNode;
class UInterchangeSceneVariantSetsNode;
class UInterchangeSceneImportAssetFactoryNode;

UCLASS(BlueprintType, editinlinenew)
class INTERCHANGEPIPELINES_API UInterchangeGenericLevelPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	/* Allow user to choose the re-import strategy when importing into level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actors", meta = (AdjustPipelineAndRefreshDetailOnChange = "True"))
	EReimportStrategyFlags ReimportStrategy = EReimportStrategyFlags::ApplyNoProperties;

protected:

	/** BEGIN UInterchangePipelineBase overrides */
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		return true;
	}
	/** END UInterchangePipelineBase overrides */

	/**
	 * PreImport step called for each translated SceneNode.
	 */
	virtual void ExecuteSceneNodePreImport(const FTransform& GlobalOffsetTransform, const UInterchangeSceneNode* SceneNode);

	/**
	 * PreImport step called for each translated SceneVariantSetNode.
	 */
	virtual void ExecuteSceneVariantSetNodePreImport(const UInterchangeSceneVariantSetsNode& SceneVariantSetNode);

	/**
	 * Return a new Actor Factory Node to be used for the given SceneNode.
	 */
	virtual UInterchangeActorFactoryNode* CreateActorFactoryNode(const UInterchangeSceneNode* SceneNode, const UInterchangeBaseNode* TranslatedAssetNode) const;

	/**
	 * Use to set up the given factory node's attributes after its initialization.
	 */
	virtual void SetUpFactoryNode(UInterchangeActorFactoryNode* ActorFactoryNode, const UInterchangeSceneNode* SceneNode, const UInterchangeBaseNode* TranslatedAssetNode) const;
	
protected:
	UInterchangeBaseNodeContainer* BaseNodeContainer = nullptr;
#if WITH_EDITORONLY_DATA
	/*
	 * Factory node created by the pipeline to later create the SceneImportAsset
	 * This factory node must be unique and depends on all the other factory nodes
	 * and its factory must be called after all factories
	 * Note that this factory node is not created at runtime. Therefore, the reimport
	 * of a level will not work at runtime
	 */
	UInterchangeSceneImportAssetFactoryNode* SceneImportFactoryNode = nullptr;
#endif
};


