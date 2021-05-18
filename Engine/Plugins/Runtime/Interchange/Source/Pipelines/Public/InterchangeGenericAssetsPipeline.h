// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericAssetsPipeline.generated.h"

class UInterchangeMaterialFactoryNode;
class UInterchangeMaterialNode;
class UInterchangeMeshNode;
class UInterchangePipelineMeshesUtilities;
class UInterchangeSceneNode;
class UInterchangeSkeletalMeshFactoryNode;
class UInterchangeSkeletalMeshLodDataNode;
class UInterchangeSkeletonFactoryNode;
class UInterchangeTextureCubeFactoryNode;
class UInterchangeTextureCubeNode;
class UInterchangeTextureFactoryNode;
class UInterchangeTextureNode;

#define COMMON_CATEGORY "Common"
#define COMMON_MESHES_CATEGORY "Common Meshes"
#define STATIC_MESHES_CATEGORY "Static Meshes"
#define SKELETAL_MESHES_CATEGORY "Skeletal Meshes"
#define ANIMATIONS_CATEGORY "Animations"
#define MATERIALS_CATEGORY "Materials"
#define TEXTURES_CATEGORY "Textures"

/** Force mesh type, if user want to import all meshes as one type*/
UENUM(BlueprintType)
enum EForceMeshType
{
	/** Will import from the source type, no conversion */
	FMT_None UMETA(DisplayName = "None"),
	/** Will import any mesh to static mesh. */
	FMT_StaticMesh UMETA(DisplayName = "Static Mesh"),
	/** Will import any mesh to skeletal mesh. */
	FMT_SkeletalMesh UMETA(DisplayName = "Skeletal Mesh"),

	FMT_MAX,
};

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
	// BEGIN Pre import pipeline properties, please keep by per category order for properties declaration


	//////	COMMON_CATEGORY Properties //////


	/** If enable and there is only one asset and one source data, we will name the asset like the source data name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_CATEGORY)
	bool bUseSourceNameForAsset = true;


	//////	COMMON_MESHES_CATEGORY Properties //////


	/** Allow to convert mesh to a particular type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY)
	TEnumAsByte<enum EForceMeshType> ForceAllMeshHasType = EForceMeshType::FMT_None;

	/** If enable, meshes LODs will be imported. Note that it required the advanced bBakeMesh property to be enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY, meta = (editcondition = "bBakeMeshes"))
	bool bImportLods = true;

	/** If enable, meshes will be baked with the hierarchy transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = COMMON_MESHES_CATEGORY)
	bool bBakeMeshes = true;

	
	//////	STATIC_MESHES_CATEGORY Properties //////


	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = STATIC_MESHES_CATEGORY)
	bool bImportStaticMeshes = true;

	/** If enable all translated static mesh node will be imported has a one static mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = STATIC_MESHES_CATEGORY)
	bool bCombineStaticMeshes = false;

	
	//////	SKELETAL_MESHES_CATEGORY Properties //////

	
	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY)
	bool bImportSkeletalMeshes = true;

	/** If enable all translated skinned mesh node will be imported has a one skeletal mesh, note that it can still create several skeletal mesh for each different root joint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY)
	bool bCombineSkeletalMeshes = true;

	
	//////	ANIMATIONS_CATEGORY Properties //////


	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ANIMATIONS_CATEGORY)
	bool bImportAnimations = true;


	//////	MATERIALS_CATEGORY Properties //////


	/** If enable, import the material asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MATERIALS_CATEGORY)
	bool bImportMaterials = true;


	//////	TEXTURES_CATEGORY Properties //////


	/** If enable, import the material asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TEXTURES_CATEGORY)
	bool bImportTextures = true;
	

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

	UInterchangeBaseNodeContainer* BaseNodeContainer;
	TArray<const UInterchangeSourceData*> SourceDatas;

	
	/** Texture translated assets nodes */
	TArray<UInterchangeTextureNode*> TextureNodes;

	/** Texture factory assets nodes */
	TArray<UInterchangeTextureFactoryNode*> TextureFactoryNodes;


	/** Texture cube translated assets nodes*/
	TArray<UInterchangeTextureCubeNode*> TextureCubeNodes;

	/** Texture cube factory translated assets nodes */
	TArray<UInterchangeTextureCubeFactoryNode*> TextureCubeFactoryNodes;


	UInterchangeTextureFactoryNode* CreateTexture2DFactoryNode(const UInterchangeTextureNode* TextureNode);

	UInterchangeTextureCubeFactoryNode* CreateTextureCubeFactoryNode(const UInterchangeTextureCubeNode* TextureCubeNode);
	
	/** Material translated assets nodes */
	TArray<UInterchangeMaterialNode*> MaterialNodes;
	
	/** Material factory assets nodes */
	TArray<UInterchangeMaterialFactoryNode*> MaterialFactoryNodes;

	
	UInterchangeMaterialFactoryNode* CreateMaterialFactoryNode(const UInterchangeMaterialNode* MaterialNode);

	
	/** Mesh translated assets nodes */
	TArray<UInterchangeMeshNode*> MeshNodes;
	
	/** Skeleton factory assets nodes */
	TArray<UInterchangeSkeletonFactoryNode*> SkeletonFactoryNodes;

	/** Create a UInterchangeSkeletonFactorynode */
	UInterchangeSkeletonFactoryNode* CreateSkeletonFactoryNode(const FString& RootJointUid);
	
	/** Skeletal mesh factory assets nodes */
	TArray<UInterchangeSkeletalMeshFactoryNode*> SkeletalMeshFactoryNodes;
	
	/** Static mesh factory assets nodes */
	//TArray<UInterchangeStaticMeshFactoryNode*> StaticMeshFactoryNodes;
	
	/**
	 * This function can create a UInterchangeSkeletalMeshFactoryNode
	 * @param MeshUidsPerLodIndex - The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode
	 */
	UInterchangeSkeletalMeshFactoryNode* CreateSkeletalMeshFactoryNode(const FString& RootJointUid, const TMap<int32, TArray<FString>> MeshUidsPerLodIndex);
	
	/** This function can create a UInterchangeSkeletalMeshLodDataNode which represent the LOD data need by the factory to create a lod mesh */
	UInterchangeSkeletalMeshLodDataNode* CreateSkeletalMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID);

	/**
	 * This function add all lod data node to the skeletal mesh.
	 * @param NodeUidsPerLodIndex - The NodeUids can be a UInterchangeSceneNode or a UInterchangeMeshNode. The scene node can bake each instance of the mesh versus the mesh node will import only the modelled mesh.
	 */
	void AddLodDataToSkeletalMesh(const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex);

	/** Translated scene nodes */
	TArray<UInterchangeSceneNode*> SceneNodes;

	/* Meshes utilities, to parse the translated graph and extract the meshes informations. */
	UInterchangePipelineMeshesUtilities* PipelineMeshesUtilities;
};


