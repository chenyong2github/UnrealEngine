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
class UInterchangeResult;
class UInterchangeSceneNode;
class UInterchangeSkeletalMeshFactoryNode;
class UInterchangeSkeletalMeshLodDataNode;
class UInterchangeSkeletonFactoryNode;
class UInterchangeStaticMeshFactoryNode;
class UInterchangeStaticMeshLodDataNode;
class UInterchangeTexture2DArrayFactoryNode;
class UInterchangeTexture2DArrayNode;
class UInterchangeTextureCubeFactoryNode;
class UInterchangeTextureCubeNode;
class UInterchangeTextureFactoryNode;
class UInterchangeTextureNode;
template<class T> class TSubclassOf;

#define COMMON_CATEGORY "Common"
#define COMMON_MESHES_CATEGORY "Common Meshes"
#define STATIC_MESHES_CATEGORY "Static Meshes"
#define COMMON_SKELETAL_ANIMATIONS_CATEGORY "Common Skeletal Mesh and Animations"
#define SKELETAL_MESHES_CATEGORY "Skeletal Meshes"
#define ANIMATIONS_CATEGORY "Animations"
#define MATERIALS_CATEGORY "Materials"
#define TEXTURES_CATEGORY "Textures"

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

/**
 * This pipeline is the generic pipeline option for all meshes type and should be call before specialized Mesh pipeline (like generic static mesh or skeletal mesh pipelines)
 * All shared import options between mesh type should be added here.
 *
 */
UCLASS(BlueprintType, Experimental)
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
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY)
// 	TEnumAsByte<enum EForceMeshType> ForceAllMeshHasType = EForceMeshType::FMT_None;

	/** If enable, meshes LODs will be imported. Note that it required the advanced bBakeMesh property to be enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY, meta = (editcondition = "bBakeMeshes"))
	bool bImportLods = true;

	/** If enable, meshes will be baked with the scene instance hierarchy transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY)
	bool bBakeMeshes = true;

	/** Specify how vertex colors should be imported */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY)
	TEnumAsByte<enum EInterchangeVertexColorImportOption> VertexColorImportOption;

	/** Specify override color in the case that VertexColorImportOption is set to Override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_MESHES_CATEGORY)
	FColor VertexOverrideColor;
	
	//////	STATIC_MESHES_CATEGORY Properties //////


	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = STATIC_MESHES_CATEGORY)
	bool bImportStaticMeshes = true;

	/** If enable all translated static mesh node will be imported has a one static mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = STATIC_MESHES_CATEGORY)
	bool bCombineStaticMeshes = false;

	
	//////  COMMON_SKELETAL_ANIMATIONS_CATEGORY //////


	/** Skeleton to use for imported asset. When importing a skeletal mesh, leaving this as "None" will create a new skeleton. When importing an animation this MUST be specified to import the asset. */
 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = COMMON_SKELETAL_ANIMATIONS_CATEGORY)
 	TObjectPtr<class USkeleton> Skeleton;

	
	//////	SKELETAL_MESHES_CATEGORY Properties //////

	
	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY)
	bool bImportSkeletalMeshes = true;

	/** If enable all translated skinned mesh node will be imported has a one skeletal mesh, note that it can still create several skeletal mesh for each different skeleton root joint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY)
	bool bCombineSkeletalMeshes = true;

	/** If enable any morph target shape will be imported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY)
	bool bImportMorphTargets = true;

	/** If checked, create new PhysicsAsset if it doesn't have it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY)
	uint32 bCreatePhysicsAsset : 1;

	/** If this is set, use this specified PhysicsAsset. If its not set and bCreatePhysicsAsset is false, the importer will not generate or set any physic asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SKELETAL_MESHES_CATEGORY, meta = (editcondition = "!bCreatePhysicsAsset"))
	TObjectPtr<class UPhysicsAsset> PhysicsAsset;
	
	//////	ANIMATIONS_CATEGORY Properties //////


	/** If enable, import the animation asset find in the sources. */
// 	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ANIMATIONS_CATEGORY)
// 	bool bImportAnimations = true;


	//////	MATERIALS_CATEGORY Properties //////


	/** If enable, import the material asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MATERIALS_CATEGORY)
	bool bImportMaterials = true;


	//////	TEXTURES_CATEGORY Properties //////


	/** If enable, import the material asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TEXTURES_CATEGORY)
	bool bImportTextures = true;

#if WITH_EDITORONLY_DATA
	/** 
	 * If enable, after a new import a test will be run to see if the texture is a normal map
	 * If the texture is a normal map the SRG, CompressionSettings and LODGroup settings will be adjusted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TEXTURES_CATEGORY)
	bool bDetectNormalMapTexture = true;

	/** If enabled, the texture's green channel will be inverted for normal maps. */
	UPROPERTY(EditAnywhere, Category = TEXTURES_CATEGORY)
	bool bFlipNormalMapGreenChannel = false;

	/** Specify the files type that should be imported as long/lat cubemap */
	UPROPERTY(EditAnywhere, Category = TEXTURES_CATEGORY)
	TSet<FString> FileExtensionsToImportAsLongLatCubemap = {"hdr"};
#endif

	// END Pre import pipeline properties
	//////////////////////////////////////////////////////////////////////////

	virtual void PreDialogCleanup(const FName PipelineStackName) override;

protected:

	virtual bool ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;

	virtual bool ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

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
	
	/** Material translated assets nodes */
	TArray<UInterchangeMaterialNode*> MaterialNodes;
	
	/** Material factory assets nodes */
	TArray<UInterchangeMaterialFactoryNode*> MaterialFactoryNodes;

	
	UInterchangeMaterialFactoryNode* CreateMaterialFactoryNode(const UInterchangeMaterialNode* MaterialNode);

	
	/************************************************************************/
	/* Skeletal mesh API BEGIN                                              */

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
	 * This function will create any skeletalmesh we need to create according to the pipeline options
	 */
	bool ExecutePreImportPipelineSkeletalMesh();

	/**
	 * This function will finish creating the physics asset with the skeletalmesh render data
	 */
	void PostImportPhysicsAssetImport(UObject* CreatedAsset, UInterchangeBaseNode* Node);

	/* Skeletal mesh API END                                                */
	/************************************************************************/


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


	/* Meshes utilities, to parse the translated graph and extract the meshes informations. */
	UInterchangePipelineMeshesUtilities* PipelineMeshesUtilities;

	/**
	 * Implement pipeline option bUseSourceNameForAsset
	 */
	void ImplementUseSourceNameForAssetOption();

	/** Specialize for skeletalmesh */
	void ImplementUseSourceNameForAssetOptionSkeletalMesh(const int32 MeshesAndAnimsImportedNodeCount);

	/************************************************************************/
	/* Texture API BEGIN                                              */

	UInterchangeTextureFactoryNode* HandleCreationOfTextureFactoryNode(const UInterchangeTextureNode* TextureNode);

	UInterchangeTextureFactoryNode* CreateTextureFactoryNode(const UInterchangeTextureNode* TextureNode, const TSubclassOf<UInterchangeTextureFactoryNode>& FactorySubclass);

	void PostImportTextureAssetImport(UObject* CreatedAsset, bool bIsAReimport);

	/* Texture API END                                                */
	/************************************************************************/
};


