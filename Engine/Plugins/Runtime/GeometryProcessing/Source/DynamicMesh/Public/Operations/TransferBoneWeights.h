// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryTypes.h"
#include "UObject/NameTypes.h"


// Forward declarations
class FProgressCancel;

namespace UE::AnimationCore { class FBoneWeights; }

namespace UE::Geometry
{
	template<typename RealType> class TTransformSRT3;
	typedef TTransformSRT3<double> FTransformSRT3d;

	class FDynamicMesh3;
	template<typename MeshType> class TMeshAABBTree3;
	typedef TMeshAABBTree3<FDynamicMesh3> FDynamicMeshAABBTree3;
}


namespace UE
{
namespace Geometry
{
/**
 * Transfer bone weights from one mesh (source) to another (target). Uses the dynamic mesh bone attributes to reindex 
 * the bone indices of the transferred weights from the source to the target skeletons. If both meshes have identical 
 * bone name attributes, then reindexing is skipped.
 * 
 * During the reindexing, if a weighted bone in the source skeleton is not present in the target skeleton, then the
 * weight is not transferred (skipped), and an error is printed to the console. For best results, the target skeleton
 * should be a superset of all the bones that are indexed by the transferred weights.
 * 
 * 
 * Example usage:
 * 
 * FDynamicMesh SourceMesh = ...; // Mesh we transferring weights from. Must have bone attributes.
 * FDynamicMesh TargetMesh = ...; // Mesh we are transferring weights to.
 *
 * FTransferBoneWeights TransferBoneWeights(&SourceMesh, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
 * 
 * // Optionally, transform the target mesh. This is useful when you want to align the two meshes in world space.
 * FTransformSRT3d InToWorld = ...; 
 * 
 * // When transferring weights from a dynamic mesh with bone attributes to a dynamic mesh without bone attributes,
 * // first copy over the bone attributes from the source to the target.
 * if (!TargetMesh.HasAttributes() || !TargetMesh.Attributes()->HasBones())
 * {
 *     TargetMesh.EnableAttributes();
 *     TargetMesh.Attributes()->CopyBoneAttributes(*SourceMesh.Attributes());
 * }
 * 
 * if (TransferBoneWeights.Validate() == EOperationValidationResult::Ok) 
 * {
 *      TransferBoneWeights.Compute(TargetMesh, InToWorld, FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
 * }
 */

class DYNAMICMESH_API FTransferBoneWeights
{
public:

	enum class ETransferBoneWeightsMethod : uint8
	{
        // For every vertex on the target mesh, find the closest point on the surface of the source mesh.
        // This is usually a point on a triangle where the bone weights are interpolated via barycentric coordinates.
		ClosestPointOnSurface = 0
	};

	//
	// Optional Inputs
	//
	
    /** Set this to be able to cancel the running operation. */
	FProgressCancel* Progress = nullptr;

	/** Enable/disable multi-threading. */
	bool bUseParallel = true;
	
	/** The transfer method to compute the bone weights. */
	ETransferBoneWeightsMethod TransferMethod = ETransferBoneWeightsMethod::ClosestPointOnSurface;

	/** 
     * Completely ignore the source and target mesh bone attributes when transferring weights from one dynamic mesh to another.
     * This skips re-indexing and simply copies skin weights over. Use with caution.
	 */
	bool bIgnoreBoneAttributes = false;

protected:
		
	/** Source mesh we are transfering weights from. */
	const FDynamicMesh3* SourceMesh;
	
	/** The name of the source mesh skinning profile name. */
	FName SourceProfileName;

	/** 
	 * The caller can optionally specify the source mesh BVH in case this operator is run on multiple target meshes 
	 * while the source mesh remains the same. Otherwise BVH tree will be computed.
	 */
	const FDynamicMeshAABBTree3* SourceBVH = nullptr;

	/** If the caller doesn't pass BVH for the source mesh then we compute one. */
	TUniquePtr<FDynamicMeshAABBTree3> InternalSourceBVH;

public:
	
	/**
	 * @param InSourceMesh The mesh we are transferring weights from 
	 * @param InSourceProfileName The profile name of the skin weight attribute we are transferring weights from.
	 * @param SourceBVH Optional source mesh BVH. If not provided, one will be computed internally. 
	 * 
	 * @note Assumes that the InSourceMesh has bone attributes, use bIgnoreBoneAttributes flag to ignore the bone 
	 * 		 attributes and skip re-indexing.
	 */
	FTransferBoneWeights(const FDynamicMesh3* InSourceMesh, 
					     const FName& InSourceProfileName,
					     const FDynamicMeshAABBTree3* SourceBVH = nullptr); 
	
	virtual ~FTransferBoneWeights();

	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot.
	 */
	virtual EOperationValidationResult Validate();

	/**
     * Transfer the bone weights from the source mesh to the given target mesh and store the result in the skin weight  
     * attribute with the given profile name.
	 * 
	 * @param InOutTargetMesh Target mesh we are transfering weights into
	 * @param InToWorld Transform applied to the input target mesh
     * @param InTargetProfileName Skin weight profile name we are writing into. If the profile with that name exists,  
     *       					  then the data will be overwritten, otherwise a new attribute will be created.
     * 
     * @return true if the algorithm succeeds, false if it failed or was canceled by the user.
	 * 
	 * @note Assumes that the InOutTargetMesh has bone attributes, use bIgnoreBoneAttributes flag to ignore the bone 
	 * 		 attributes and skip re-indexing.
	 */
	virtual bool Compute(FDynamicMesh3& InOutTargetMesh, const FTransformSRT3d& InToWorld, const FName& InTargetProfileName);

	/**
     * Compute a single bone weight for a given point.
     *
	 * @param InPoint Point for which we are computing a bone weight
	 * @param InToWorld Transform applied to the point
     * @param OutWeights Bone weight computed for the input transformed point
	 * @param TargetBoneToIndex Optional map from the bone names to the bone indices of the target skeleton. 
	 * 							If null, the bone indices of the skinning weights will not be re-indexed after the transfer.
	 * 
     * @return true if the algorithm succeeds, false if it failed or was canceled by the user.
	 */
	virtual bool Compute(const FVector3d& InPoint, const FTransformSRT3d& InToWorld, UE::AnimationCore::FBoneWeights& OutWeights, const TMap<FName, uint16>* TargetBoneToIndex = nullptr);

protected:
	
    /** @return if true, abort the computation. */
	virtual bool Cancelled();
};

} // end namespace UE::Geometry
} // end namespace UE
