// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericMeshPipeline.generated.h"

class UInterchangeMeshNode;
class UInterchangePipelineMeshesUtilities;
class UInterchangeSkeletalMeshFactoryNode;
class UInterchangeSkeletalMeshLodDataNode;
class UInterchangeSkeletonFactoryNode;
class UInterchangeStaticMeshFactoryNode;
class UInterchangeStaticMeshLodDataNode;

/** Force mesh type, if user want to import all meshes as one type*/
UENUM(BlueprintType)
enum EInterchangeForceMeshType
{
	/** Will import from the source type, no conversion */
	IFMT_None UMETA(DisplayName = "None"),
	/** Will import any mesh to static mesh. */
	IFMT_StaticMesh UMETA(DisplayName = "Static Mesh"),
	/** Will import any mesh to skeletal mesh. */
	IFMT_SkeletalMesh UMETA(DisplayName = "Skeletal Mesh"),

	IFMT_MAX
};

UENUM(BlueprintType)
enum EInterchangeVertexColorImportOption
{
	/** Import the mesh using the vertex colors from the translated source. */
	IVCIO_Replace UMETA(DisplayName = "Replace"),
	/** Ignore vertex colors from the translated source. In case of a re-import keep the existing mesh vertex colors. */
	IVCIO_Ignore UMETA(DisplayName = "Ignore"),
	/** Override all vertex colors with the specified color. */
	IVCIO_Override UMETA(DisplayName = "Override"),

	IVCIO_MAX
};

UCLASS(BlueprintType, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericMeshPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:

	//////	COMMON_MESHES_CATEGORY Properties //////

	/** Allow to convert mesh to a particular type */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
 	TEnumAsByte<enum EInterchangeForceMeshType> ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_None;

	/** If enable, meshes LODs will be imported. Note that it required the advanced bBakeMesh property to be enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes", meta = (editcondition = "bBakeMeshes"))
	bool bImportLods = true;

	/** If enable, meshes will be baked with the scene instance hierarchy transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
	bool bBakeMeshes = true;

	/** Specify how vertex colors should be imported */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
	TEnumAsByte<enum EInterchangeVertexColorImportOption> VertexColorImportOption = EInterchangeVertexColorImportOption::IVCIO_Replace;

	/** Specify override color in the case that VertexColorImportOption is set to Override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Meshes")
	FColor VertexOverrideColor;
	
	//////	STATIC_MESHES_CATEGORY Properties //////

	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Meshes")
	bool bImportStaticMeshes = true;

	/** If enable all translated static mesh node will be imported has a one static mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Meshes")
	bool bCombineStaticMeshes = false;

	
	//////  COMMON_SKELETAL_ANIMATIONS_CATEGORY //////


	/** Skeleton to use for imported asset. When importing a skeletal mesh, leaving this as "None" will create a new skeleton. When importing an animation this MUST be specified to import the asset. */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common Skeletal Mesh and Animations")
 	TObjectPtr<class USkeleton> Skeleton;

	
	//////	SKELETAL_MESHES_CATEGORY Properties //////

	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bImportSkeletalMeshes = true;

	/** If enable all translated skinned mesh node will be imported has a one skeletal mesh, note that it can still create several skeletal mesh for each different skeleton root joint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bCombineSkeletalMeshes = true;

	/** If enable any morph target shape will be imported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bImportMorphTargets = true;

	/** Enable this option to update Skeleton (of the mesh)'s reference pose. Mesh's reference pose is always updated.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bUpdateSkeletonReferencePose = false;

	/** If checked, meshes nested in bone hierarchies will be imported instead of being converted to bones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bImportMeshesInBoneHierarchy = true;

	/** Enable this option to use frame 0 as reference pose */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bUseT0AsRefPose = false;

	/** If checked, create new PhysicsAsset if it doesn't have it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bCreatePhysicsAsset = true;

	/** If this is set, use this specified PhysicsAsset. If its not set and bCreatePhysicsAsset is false, the importer will not generate or set any physic asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes", meta = (editcondition = "!bCreatePhysicsAsset"))
	TObjectPtr<class UPhysicsAsset> PhysicsAsset;

	virtual void PreDialogCleanup(const FName PipelineStackName) override;

protected:
	virtual void ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;

	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		return true;
	}

private:

	/* Meshes utilities, to parse the translated graph and extract the meshes informations. */
	UInterchangePipelineMeshesUtilities* PipelineMeshesUtilities;


	/************************************************************************/
	/* Skeletal mesh API BEGIN                                              */

	/**
	 * This function will create any skeletalmesh we need to create according to the pipeline options
	 */
	void ExecutePreImportPipelineSkeletalMesh();

	/** Skeleton factory assets nodes */
	TArray<UInterchangeSkeletonFactoryNode*> SkeletonFactoryNodes;

	/** Create a UInterchangeSkeletonFactorynode */
	UInterchangeSkeletonFactoryNode* CreateSkeletonFactoryNode(const FString& RootJointUid);

	/** Skeletal mesh factory assets nodes */
	TArray<UInterchangeSkeletalMeshFactoryNode*> SkeletalMeshFactoryNodes;

	/**
	 * This function can create a UInterchangeSkeletalMeshFactoryNode
	 * @param MeshUidsPerLodIndex - The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode
	 */
	UInterchangeSkeletalMeshFactoryNode* CreateSkeletalMeshFactoryNode(const FString& RootJointUid, const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex);

	/** This function can create a UInterchangeSkeletalMeshLodDataNode which represent the LOD data need by the factory to create a lod mesh */
	UInterchangeSkeletalMeshLodDataNode* CreateSkeletalMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID);

	/**
	 * This function add all lod data node to the skeletal mesh.
	 * @param NodeUidsPerLodIndex - The NodeUids can be a UInterchangeSceneNode or a UInterchangeMeshNode. The scene node can bake each instance of the mesh versus the mesh node will import only the modelled mesh.
	 */
	void AddLodDataToSkeletalMesh(const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex);

	/**
	 * This function will finish creating the skeletalmesh asset
	 */
	void PostImportSkeletalMesh(UObject* CreatedAsset, UInterchangeBaseNode* Node);

	/**
	 * This function will finish creating the physics asset with the skeletalmesh render data
	 */
	void PostImportPhysicsAssetImport(UObject* CreatedAsset, UInterchangeBaseNode* Node);
public:
	
	/** Specialize for skeletalmesh */
	void ImplementUseSourceNameForAssetOptionSkeletalMesh(const int32 MeshesAndAnimsImportedNodeCount, const bool bUseSourceNameForAsset);

private:
	/* Skeletal mesh API END                                                */
	/************************************************************************/


	/************************************************************************/
	/* Static mesh API BEGIN                                              */

	/**
	 * This function will create any skeletalmesh we need to create according to the pipeline options
	 */
	void ExecutePreImportPipelineStaticMesh();

	/** Static mesh factory assets nodes */
	TArray<UInterchangeStaticMeshFactoryNode*> StaticMeshFactoryNodes;

	/**
	 * This function can create a UInterchangeStaticMeshFactoryNode
	 * @param MeshUidsPerLodIndex - The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode
	 */
	UInterchangeStaticMeshFactoryNode* CreateStaticMeshFactoryNode(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex);

	/** This function can create a UInterchangeStaticMeshLodDataNode which represents the LOD data needed by the factory to create a lod mesh */
	UInterchangeStaticMeshLodDataNode* CreateStaticMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID);

	/**
	 * This function add all lod data nodes to the static mesh.
	 * @param NodeUidsPerLodIndex - The NodeUids can be a UInterchangeSceneNode or a UInterchangeMeshNode. The scene node can bake each instance of the mesh versus the mesh node will import only the modelled mesh.
	 */
	void AddLodDataToStaticMesh(UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex);

	/**
	 * Return a reasonable UID and display label for a new mesh factory node.
	 */
	bool MakeMeshFactoryNodeUidAndDisplayLabel(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex, int32 LodIndex, FString& NewMeshUid, FString& DisplayLabel);

	/* Static mesh API END                                                */
	/************************************************************************/

private:

	UInterchangeBaseNodeContainer* BaseNodeContainer = nullptr;
	TArray<const UInterchangeSourceData*> SourceDatas;

};


