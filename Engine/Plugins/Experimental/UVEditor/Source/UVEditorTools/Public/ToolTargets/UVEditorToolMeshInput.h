// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h" // FDynamicMeshUVOverlay
#include "ToolTargets/ToolTarget.h"
#include "VectorTypes.h"

#include "GeometryBase.h"

#include "UVEditorToolMeshInput.generated.h"

class UMaterialInterface;
class UMeshOpPreviewWithBackgroundCompute;
class UMeshElementsVisualizer;
PREDECLARE_GEOMETRY(class FDynamicMesh3);

/**
 * A package of the needed information for an asset being operated on by a
 * UV editor tool. It includes a UV unwrap mesh, a mesh with the UV layer applied,
 * and background-op-compatible previews for each. It also has convenience methods
 * for updating all of the represenations from just one of them, using a "fast update"
 * code path when possible.
 * 
 * This tool target is a bit different from usual in that it is not created
 * by a tool target manager, and therefore doesn't have an accompanying factory.
 * Instead, it is created by the mode, because the mode has access to the worlds
 * in which the previews need to be created.
 * 
 * It's arguable whether this should even inherit from UToolTarget.
 */
UCLASS()
class UVEDITORTOOLS_API UUVEditorToolMeshInput : public UToolTarget
{
	GENERATED_BODY()

public:
	/** 
	 * Mesh representing the unwrapped UV layer. If the UnwrapPreview is changed via background
	 * ops, then this mesh can be used to restart an operation as parameters change. Once a change
	 * is completed, this mesh should be updated (i.e., it is the "canonical" unwrap mesh, though
	 * the final UV layer truth is in the UV's of AppliedCanonical).
	 */
	TSharedPtr<UE::Geometry::FDynamicMesh3> UnwrapCanonical;

	/**
	 * Preview of the unwrapped UV layer, suitable for being manipulated by background ops.
	 */
	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> UnwrapPreview = nullptr;

	// Note that both UnwrapCanonical and UnwrapPreview, besides having vert positions that represent
	// a UV layer, also have a primary UV overlay that represents the same layer, to make it possible
	// to someday texture the unwrap. This UV overlay will differ from the UV overlays in AppliedCanonical
	// and AppliedPreview only in the parent pointers of its elements, since there will not be any elements 
	// pointing back to the same vertex. The element id's and the triangles will be the same.

	/** 
	 * A 3d mesh with the UV layer applied. This is the canonical result that will be baked back
	 * on the application of changes. It can also be used to reset background ops that may operate
	 * on AppliedPreview.
	 */
	TSharedPtr<UE::Geometry::FDynamicMesh3> AppliedCanonical;

	/**
	 * 3d preview of the asset with the UV layer updated, suitable for use with background ops. 
	 */
	UPROPERTY()
	TObjectPtr<UMeshOpPreviewWithBackgroundCompute> AppliedPreview;

	/**
	 * Optional: a wireframe to track the mesh in unwrap preview. If set, it gets updated whenever the
	 * class updates the unwrap preview, and it is destroyed on Shutdown().
	 * TODO: We should have a fast path for updating the wireframe...
	 */
	UPROPERTY()
	TObjectPtr<UMeshElementsVisualizer> WireframeDisplay = nullptr;

	// Additional needed information
	TObjectPtr<UObject> OriginalAsset = nullptr;
	int32 UVLayerIndex = 0;

	// Mappings used for generating and baking back the unwrap.
	TFunction<FVector3d(const FVector2f&)> UVToVertPosition;
	TFunction<FVector2f(const FVector3d&)> VertPositionToUV;

	bool InitializeMeshes(UToolTarget* Target, int32 UVLayerIndex,
		UWorld* UnwrapWorld, UWorld* LivePreviewWorld,
		UMaterialInterface* WorkingMaterialIn,
		TFunction<FVector3d(const FVector2f&)> UVToVertPositionFuncIn,
		TFunction<FVector2f(const FVector3d&)> VertPositionToUVFuncIn);

	void Shutdown();

	// Notes above the below convenience functions:
	// 1. If BOTH ChangedVids/ChagnedElementIDs and ChangedTids are null, a complete update will be performed. If either
	//   is present, then only those vids/elements/tids will be updated.
	// 2. Generally ChangedVids/ChangedElementIDs is allowed to have new elements, since it is natural to split UVs.
	//   However, ChangedTids must not have new Tids, because the tids form our correspondence between the unwrap mesh and
	//   the original layer.
	// 3. If updating the preview objects, note that the functions do not try to cancel any active computations, so an active
	//   computation could reset things once it completes.

	/**
	 * Updates UnwrapPreview UV Overlay from UnwrapPreview vert positions. Issues a NotifyDeferredEditCompleted
	 * for both positions and UVs.
	 */
	void UpdateUnwrapPreviewOverlay(const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates UnwrapCanonical UV Overlay from UnwrapCanonical vert positions. Issues a NotifyDeferredEditCompleted
	 * for both positions and UVs.
	 */
	void UpdateUnwrapCanonicalOverlay(const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates the AppliedPreview from UnwrapPreview, without updating the non-preview meshes. Useful for updates during
	 * a drag, etc, when we only care about updating the visible items.
	 * Assumes that the overlay in UnwrapPreview is already updated.
	 */
	void UpdateAppliedPreviewFromUnwrapPreview(const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates only the UnwrapPreview from AppliedPreview, without updating the non-preview meshes. Useful for updates during
	 * a drag, etc, when we only care about updating the visible items.
	 */
	void UpdateUnwrapPreviewFromAppliedPreview(
		const TArray<int32>* ChangedElementIDs = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates the non-preview meshes from their preview counterparts. Useful, for instance, after the completion of a drag
	 * to update the canonical objects.
	 */
	void UpdateCanonicalFromPreviews(const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates the other meshes using the mesh in UnwrapPreview. Assumes that the overlay in UnwrapPreview is updated.
	 */
	void UpdateAllFromUnwrapPreview(const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates the other meshes using the mesh in UnwrapCanonical. Assumes that overlay in UnwrapCanonical is updated.
	 */
	void UpdateAllFromUnwrapCanonical(const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

	/**
	 * Updates the other meshes using the UV overlay in the live preview.
	 */
	void UpdateAllFromAppliedPreview(const TArray<int32>* ChangedElementIDs = nullptr, const TArray<int32>* ChangedTids = nullptr);

	// UToolTarget
	virtual bool IsValid() const override;
protected:

	void UpdateAppliedOverlays(const UE::Geometry::FDynamicMeshUVOverlay& SourceOverlay,
		const TArray<int32>* ChangedVids, const TArray<int32>* ChangedTids);
	void UpdateOtherUnwrap(const UE::Geometry::FDynamicMesh3& SourceUnwrapMesh,
		UE::Geometry::FDynamicMesh3& DestUnwrapMesh, const TArray<int32>* ChangedVids, const TArray<int32>* ChangedTids);
};