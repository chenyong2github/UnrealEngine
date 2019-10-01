// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshTrackerTypes.h"
#include "MeshBlockSelectorInterface.h"
#include "Components/SceneComponent.h"
#include "MeshTrackerComponent.generated.h"

/**
	The MeshTrackerComponent class manages requests for environmental mesh data, processes the results and provides
	them to the calling system. The calling system is able request environmental mesh data within a specified area.
	Various other search criteria can be set via this class's public properties.  Mesh data requests are processed
	on a separate thread.  Once a mesh data request has been processed the calling system will be notified via an
	FOnMeshTrackerUpdated broadcast.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAP_API UMeshTrackerComponent : public USceneComponent, public IMagicLeapMeshBlockSelectorInterface
{
	GENERATED_BODY()

public:
	UMeshTrackerComponent();
	/** Destroys the FMagicLeapMeshTrackerImpl instance.*/
	~UMeshTrackerComponent();

	/**
		Sets the procedural mesh component that will store and display the environmental mesh results.
		@param InMRMeshPtr The procedural mesh component to store the query result in.
	*/
	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void ConnectMRMesh(class UMRMeshComponent* InMRMeshPtr);

	/**
		Unlinks the current procedural mesh component from the mesh tracking system.
		@param InMRMeshPtr The procedural mesh component to unlink from the mesh tracking system.
	*/
	UFUNCTION(BlueprintCallable, Category = "Mesh Reconstruction")
	void DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr);

	/**
		Sets the interface to be used for selecting blocks to mesh.
		@param Selector pointer to the object which implements IMagicLeapMeshBlockSelectorInterface.
	*/
	UFUNCTION(BlueprintCallable, Category = "Meshing|MagicLeap")
	void ConnectBlockSelector(TScriptInterface<IMagicLeapMeshBlockSelectorInterface> Selector);

	/** 
	 * Disconnects the previously connected IMagicLeapMeshBlockSelectorInterface.
	 * The default implementation is used this case - all new and updated blocks are meshed with the MeshTrackerComponent'd LevelOfDetail.
	 */
	UFUNCTION(BlueprintCallable, Category = "Meshing|MagicLeap")
	void DisconnectBlockSelector();

	/**
	 * Gets the number of mesh bricks in the pending queue.
	 */
	UFUNCTION(BlueprintCallable, Category = "Meshing|MagicLeap")
	int32 GetNumQueuedBlockUpdates();

	/** IMagicLeapMeshBlockSelectorInterface */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Meshing|MagicLeap")
	void SelectMeshBlocks(const FMagicLeapTrackingMeshInfo& NewMeshInfo, TArray<FMagicLeapMeshBlockRequest>& RequestedMesh);
	virtual void SelectMeshBlocks_Implementation(const FMagicLeapTrackingMeshInfo& NewMeshInfo, TArray<FMagicLeapMeshBlockRequest>& RequestedMesh) override;

	/**
		Delegate used by OnMeshUpdated().
		@param ID The ID of the mesh section in the Block that was updated.
		@param Vertices List of all vertices in the updated mesh section.
		@param Triangles List of all triangles in the updated mesh section.
		@param Normals List of the normals of all triangles in the updated mesh section.
		@param Confidence List of the confidence values per vertex in the updated mesh section. This can be used to determine if the user needs to scan more.
	*/
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnMeshTrackerUpdated, FGuid, ID, const TArray<FVector>&, Vertices, const TArray<int32>&, Triangles, const TArray<FVector>&, Normals, const TArray<float>&, Confidence);

	/** Activated whenever new information about this mesh tracker is detected. */
	UPROPERTY(BlueprintAssignable)
	FOnMeshTrackerUpdated OnMeshTrackerUpdated;

	/** Set to true to start scanning the world for meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	bool ScanWorld = true;

	/** The type of mesh to query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	EMagicLeapMeshType MeshType = EMagicLeapMeshType::Triangles;

	/** Bounding box for the mesh scan. The mesh will be scanned for only within this box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	class UBoxComponent* BoundingVolume;

	/** Meshing LOD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	EMagicLeapMeshLOD LevelOfDetail = EMagicLeapMeshLOD::Medium;

	/** The perimeter (in Unreal Units) of gaps to be filled. 0 means do not fill. A good value is 300cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	float PerimeterOfGapsToFill = 300.0f;

	/** If true, the system will planarize the returned mesh i.e. planar regions will be smoothed out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	bool Planarize = false;

	/** Any section that is disconnected from the main mesh and has an area (in Unreal Units squared) 
	    less than this value will be removed. 
		0 means do not remove disconnected sections. A good value is 50cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	float DisconnectedSectionArea = 50.0f;

	/** If true, the system will generate normals for the triangle vertices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	bool RequestNormals = true;

	/**
		If true, the system will generate the mesh confidence values for the triangle vertices.
		These confidence values can be used to determine if the user needs to scan more.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	bool RequestVertexConfidence = false;

	/**
	* Vertex Colors can be unused, or filled with several types of information.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	EMagicLeapMeshVertexColorMode VertexColorMode = EMagicLeapMeshVertexColorMode::None;

	/** Colors through which we cycle when setting vertex color by block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	TArray<FColor> BlockVertexColors;

	/** Color mapped to confidence value of zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	FLinearColor VertexColorFromConfidenceZero;

	/** Color mapped to confidence value of one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	FLinearColor VertexColorFromConfidenceOne;

	/**
		If true, overlapping area between two mesh blocks will be removed.
		This field is only valid when the MeshType is Blocks.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	bool RemoveOverlappingTriangles = false;

	/** MRMeshComponent can render and provide collision based on the Mesh data. */
	UPROPERTY(transient)
	class UMRMeshComponent* MRMesh;

	/** Specifies the number of MR Mesh bricks to create per frame. This is to
	    amortize the cost of brick creation across frames rather than creating
		all N bricks delivered in response to each meshing query.
		If the value is <=0 then all meshes will be pushed to MR Mesh as they
		are provided by the system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Meshing|MagicLeap")
	int32 BricksPerFrame = 1;

	/** Polls for and handles the results of the environmental mesh queries. */
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	/** Unlinks the current procedural mesh component from the mesh tracking system. */
	virtual void BeginDestroy() override;

	/** Destroys the interface object to the mesh tracking api*/
	virtual void FinishDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& e) override;
#endif

private:
	void RequestMeshInfo();
	bool GetMeshInfoResult();
	void RequestMesh();
	bool GetMeshResult();

	class FMagicLeapMeshTrackerImpl *Impl;

#if WITH_EDITOR
private:
	void PrePIEEnded(bool bWasSimulatingInEditor);
#endif
};
