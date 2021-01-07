// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericMeshPipeline.generated.h"

class UInterchangeSkeletalMeshNode;

/**
 * This pipeline is the generic pipeline option for all meshes type and should be call before specialized Mesh pipeline (like generic static mesh or skeletal mesh pipelines)
 * All shared import options between mesh type should be added here.
 *
 * UPROPERTY possible meta values:
 * @meta ImportOnly - Boolean, the property is use only when we import not when we re-import. Cannot be mix with ReimportOnly!
 * @meta ReimportOnly - Boolean, the property is use only when we re-import not when we import. Cannot be mix with ImportOnly!
 * @meta MeshType - String, the property is for static or skeletal or both (static | skeletal) mesh type. If not specified it will apply to all mesh type.
 */
UCLASS(BlueprintType)
class INTERCHANGEPIPELINEPLUGIN_API UInterchangeGenericMeshPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////
	// BEGIN Pre import pipeline properties

	/** If enable and there is only one mesh in the imported source file, the filename will be use to name the asset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreImport | Miscellaneous", meta = (ImportOnly = "true", MeshType = "Static | Skeletal"))
	bool bUseFilenameToNameMeshes = true;

	// END Pre import pipeline properties
	//////////////////////////////////////////////////////////////////////////

protected:
	
	virtual bool ExecutePreImportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas) override;

	//virtual bool ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FName& NodeKey, UObject* CreatedAsset) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		//If a blueprint or python derived from this class, it will be execute on the game thread since we cannot currently execute script outside of the game thread, even if this function return true.
		return true;
	}

	//virtual bool ExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer) override;
private:

	struct FPreImportPipelineHelper
	{
	public:
		/** This function initialize the helper with the proper data*/
		void Initialize(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas, UInterchangeGenericMeshPipeline* InPipelineOwner);

		/**
		 * This function will change the meshes node display label and use the source filename to build a new mesh name
		 * @note - If bUseFilenameToNameMeshes is false this function will early exit and not do any name change.
		 * @note - It will rename staticmesh, skeletalmesh and skeleton nodes
		 */
		void ChangeAssetNameToUseFilename();

	private:

		/** This function check all pointer and make sure we can use the pre import pipeline helper functionalities*/
		bool IsValid() const;

		/**
		 * This function refresh the skeletal mesh node array to reflect the BaseNodeContainer content
		 * @note - Any pipeline feature that change the topologies of the graph should call this refresh after changing it.
		 */
		void RefreshMeshNodes();


		UInterchangeBaseNodeContainer* BaseNodeContainer;
		TArray<UInterchangeSourceData*> SourceDatas;
		UInterchangeGenericMeshPipeline* PipelineOwner;

		/** List of all SkeletalMeshNodes, must be refresh if we change the node graph(add/remove/move any node in the graph) */
		TArray<UInterchangeSkeletalMeshNode*> SkeletalMeshNodes;
	};

	/** Cache the PreImportPipeline parameter in this structure to avoid passing parameter to each private functions */
	FPreImportPipelineHelper PreImportPipelineHelper;
};


