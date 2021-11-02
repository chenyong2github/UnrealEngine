// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangePipelineMeshesUtilities.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeMeshNode;
class UInterchangeSceneNode;

/*
* This container exist only because UPROPERTY cannot support nested container. See FInterchangeMeshInstance
*/
USTRUCT(BlueprintType, Experimental)
struct FInterchangeLodSceneNodeContainer
{
	GENERATED_BODY()

	/**
	* Each scene node here represent a mesh scene node. Only if we represent a lod group we can have more then 1 mesh scene node for a specific lod index.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesInstance")
	TArray<TObjectPtr<UInterchangeSceneNode>> SceneNodes;
};

/*
* A mesh instance is a description of a translated scene node that point on a translated mesh asset.
* A mesh instance pointing on a lod group can have many lods and many scene mesh nodes per lod index.
* A mesh instance pointing on a mesh node will have only the lod 0 and will point on one scene mesh node.
*/
USTRUCT(BlueprintType, Experimental)
struct FInterchangeMeshInstance
{
	GENERATED_BODY()

	FInterchangeMeshInstance()
	{
		LodGroupNode = nullptr;
		bReferenceSkinnedMesh = false;
		bReferenceBlendShape = false;
	}
	/**
	* This ID represent either 1: a lod group scene node uid or 2: a mesh scene node uid.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesInstance")
	FString MeshInstanceUid;

	/**
	* If this mesh instance represent a LodGroup this member will not be null, but will be null if the mesh instance do not represent a lod group
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesInstance")
	TObjectPtr<UInterchangeSceneNode> LodGroupNode;

	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesInstance")
	bool bReferenceSkinnedMesh;

	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesInstance")
	bool bReferenceBlendShape;

	/**
	* Each scene node here represent a mesh scene node. Only if we represent a lod group we can have more then 1 mesh scene node for a specific lod index.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesInstance")
	TMap<int32, FInterchangeLodSceneNodeContainer> SceneNodePerLodIndex;

	/**
	* All mesh geometry referenced by this MeshInstance.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesGeometry")
	TArray<FString> ReferencingMeshGeometryUids;
};

/*
* A mesh geometry is a description of a translated mesh asset node that define a geometry.
*/
USTRUCT(BlueprintType, Experimental)
struct FInterchangeMeshGeometry
{
	GENERATED_BODY()

	FInterchangeMeshGeometry()
	{
		MeshNode = nullptr;
	}

	/**
	* Represent the unique id of the UInterchangeMeshNode represent by this structure.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesGeometry")
	FString MeshUid;

	/**
	* The UInterchangeMeshNode pointer represent by this structure.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesGeometry")
	TObjectPtr<UInterchangeMeshNode> MeshNode = nullptr;

	/**
	* All mesh instance referencing this UInterchangeMeshNode pointer.
	*/
	UPROPERTY(EditAnywhere, Category = "Interchange | Pipeline | MeshesGeometry")
	TArray<FString> ReferencingMeshInstanceUids;
};


/**/
UCLASS(BlueprintType, Experimental)
class INTERCHANGEPIPELINES_API UInterchangePipelineMeshesUtilities : public UObject
{
	GENERATED_BODY()
public:
	/**
	* Create an instance of UInterchangePipelineMeshesUtilities.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	static UInterchangePipelineMeshesUtilities* CreateInterchangePipelineMeshesUtilities(UInterchangeBaseNodeContainer* BaseNodeContainer);

	/**
	* Get all mesh instance unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllMeshInstanceUids(TArray<FString>& MeshInstanceUids) const;

	/**
	* Iterate all mesh instance.
	*/
	void IterateAllMeshInstance(TFunctionRef<void(const FInterchangeMeshInstance&)> IterationLambda) const;

	/**
	* Get all skinned mesh instance unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllSkinnedMeshInstance(TArray<FString>& MeshInstanceUids) const;

	/**
	* Iterate all skinned mesh instance.
	*/
	void IterateAllSkinnedMeshInstance(TFunctionRef<void(const FInterchangeMeshInstance&)> IterationLambda) const;

	/**
	* Get all static mesh instance unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllStaticMeshInstance(TArray<FString>& MeshInstanceUids) const;

	/**
	* Iterate all static mesh instance.
	*/
	void IterateAllStaticMeshInstance(TFunctionRef<void(const FInterchangeMeshInstance&)> IterationLambda) const;

	/**
	* Get all mesh geometry unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllMeshGeometry(TArray<FString>& MeshGeometryUids) const;
		
	/**
	* Iterate all mesh geometry.
	*/
	void IterateAllMeshGeometry(TFunctionRef<void(const FInterchangeMeshGeometry&)> IterationLambda) const;

	/**
	* Get all skinned mesh geometry unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllSkinnedMeshGeometry(TArray<FString>& MeshGeometryUids) const;

	/**
	* Iterate all skinned mesh geometry.
	*/
	void IterateAllSkinnedMeshGeometry(TFunctionRef<void(const FInterchangeMeshGeometry&)> IterationLambda) const;

	/**
	* Get all static mesh geometry unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllStaticMeshGeometry(TArray<FString>& MeshGeometryUids) const;

	/**
	* Iterate all static mesh geometry.
	*/
	void IterateAllStaticMeshGeometry(TFunctionRef<void(const FInterchangeMeshGeometry&)> IterationLambda) const;

	/**
	* Get all not instanced mesh geometry unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllMeshGeometryNotInstanced(TArray<FString>& MeshGeometryUids) const;

	/**
	* Iterate all mesh geometry not instanced.
	*/
	void IterateAllMeshGeometryNotIntanced(TFunctionRef<void(const FInterchangeMeshGeometry&)> IterationLambda) const;

	/**
	* Return true if there is an existing FInterchangeMeshInstance matching the MeshInstanceUid key.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	bool IsValidMeshInstanceUid(const FString& MeshInstanceUid) const;

	/**
	* Get the instanced mesh from the unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	const FInterchangeMeshInstance& GetMeshInstanceByUid(const FString& MeshInstanceUid) const;

	/**
	* Return true if there is an existing FInterchangeMeshGeometry matching the MeshInstanceUid key.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	bool IsValidMeshGeometryUid(const FString& MeshGeometryUid) const;

	/**
	* Get the geometry mesh from the unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	const FInterchangeMeshGeometry& GetMeshGeometryByUid(const FString& MeshGeometryUid) const;

	/**
	* Get all instanced mesh uids using the mesh geometry unique ids.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	void GetAllMeshInstanceUidsUsingMeshGeometryUid(const FString& MeshGeometryUid, TArray<FString>& MeshInstanceUids) const;

	/**
	* Iterate all instanced mesh uids using the mesh geometry unique ids.
	*/
	void IterateAllMeshInstanceUsingMeshGeometry(const FString& MeshGeometryUid, TFunctionRef<void(const FInterchangeMeshInstance&)> IterationLambda) const;

	/**
	* Return a list of skinned FInterchangeMeshInstance uid that can be combined together.
	* We cannot create a skinned mesh with multiple skeleton root node, This function return combined MeshInstance per skeleton roots
	*/
	void GetCombinedSkinnedMeshInstances(TMap<FString, TArray<FString>>& OutMeshInstanceUidsPerSkeletonRootUid) const;
	
	/**
	* Return a list of skinned FInterchangeMeshGeometry uid that can be combined together.
	* We cannot create a skinned mesh with multiple skeleton root node, This function return combined FInterchangeMeshGeometry per skeleton roots
	*/
	void GetCombinedSkinnedMeshGeometries(TMap<FString, TArray<FString>>& OutMeshGeometryUidsPerSkeletonRootUid) const;

	/**
	* Return the skeleton root node Uid, this is the uid for a UInterchangeSceneNode that has a "Joint" specialized type.
	* Return an empty string if the MeshInstanceUid parameter point on nothing.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	FString GetMeshInstanceSkeletonRootUid(const FString& MeshInstanceUid) const;

	FString GetMeshInstanceSkeletonRootUid(const FInterchangeMeshInstance& MeshInstance) const;

	/**
	* Return the skeleton root node Uid, this is the uid for a UInterchangeSceneNode that has a "Joint" specialized type.
	* Return an empty string if the MeshGeometryUid parameter point on nothing.
	*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Pipeline | Meshes")
	FString GetMeshGeometrySkeletonRootUid(const FString& MeshGeometryUid) const;

	FString GetMeshGeometrySkeletonRootUid(const FInterchangeMeshGeometry& MeshGeometry) const;

protected:
	TMap<FString, FInterchangeMeshGeometry> MeshGeometriesPerMeshUid;
	TMap<FString, FInterchangeMeshInstance> MeshInstancesPerMeshInstanceUid;
	TMap<FString, FString> SkeletonRootUidPerMeshUid;
};
