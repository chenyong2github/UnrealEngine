// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "../Private/MuCO/CustomizableObjectSystemPrivate.h"
#include "MuR/Mesh.h"


namespace UnrealConversionUtils
{
	/*
	 * Shared methods between mutable instance update and viewport mesh generation
	 * These are the result of stripping up parts of the reference code to be able to share it on the two
	 * current pipelines (instance update and USkeletal mesh generation for a mesh viewport)
	 */
	
	/**
	 * Assigns a new render section for each of the surfaces found on the mutable mesh and sets a default material for
	 * each of them (same default material)
	 * @param MeshLODIndex - The index of the LOD found on the OutSkeletalMesh to be targeting.
	 * @param InMutableMesh - The Mutable mesh to get the surface count from. Determines the amount materials and also
	 * render sections to be added to the OutSkeletalMesh.
	 * @param OutSkeletalMesh - The skeletal mesh whose render data is being updated (adding render sections)
	 */
	CUSTOMIZABLEOBJECT_API void BuildSkeletalMeshElementDataAtLOD(const int32 MeshLODIndex, mu::MeshPtrConst InMutableMesh,
										   USkeletalMesh* OutSkeletalMesh);
	
	
	/**
	 * Builds the reference skeleton by adding bones to the reference skeleton modifier provided. When exiting the scope
	 * of this method those bones get generated on the reference skeleton. It also updates an array with the used bones.
	 * @param OutMutSkeletonData - Object map used later when remaking bones.
	 * @param InSourceReferenceSkeleton - The reference skeleton from the source object. Is used to get the skeleton bone info
	 * @param InUsedBones - Array with the same count as the bones present on the skeleton and determines what bones are used and what not.
	 * @param InRefSkeleton - The reference skeleton to be updated with new bone data
	 * @param InSkeleton - Skeleton to be updated as consequence of the destruction of the FReferenceSkeletonModifier
	 */
	CUSTOMIZABLEOBJECT_API void BuildRefSkeleton(
		FInstanceUpdateData::FSkeletonData* OutMutSkeletonData,
		const FReferenceSkeleton& InSourceReferenceSkeleton,
		const TArray<bool>& InUsedBones,
		FReferenceSkeleton& InRefSkeleton,
		const USkeleton* InSkeleton );

	
	/**
	 * Prepares the render sections found on the InSkeletalMesh and sets them up accordingly what the InMutableMesh requires
	 * @param InMutableMesh - Mutable mesh to be used as reference for the section data update on the Skeletal Mesh
	 * @param OutSkeletalMesh - The Skeletal mesh whose sections are ought to be updated
	 * @param MeshLODIndex - The Mesh lod to be working with
	 * @param NumBoneInfluences - The amount of bones a pixel can be affected by
	 * @param InBoneMap - The bones to be set as part of the sections.
	 */
	CUSTOMIZABLEOBJECT_API void SetupRenderSections(
		const mu::MeshPtrConst InMutableMesh,
		const USkeletalMesh* OutSkeletalMesh,
		const int32 MeshLODIndex,
		const int32 NumBoneInfluences,
		const TArray<uint16>& InBoneMap);


	/** Performs a copy of the data found on the vertex buffers on the mutable mesh to the buffers of the skeletal mesh
	 * @param OutSkeletalMesh - Skeletal Mesh to be updated with new buffer data
	 * @param NumVerticesLODModel - The amount of vertices on the LODModel.
	 * @param NumBoneInfluences - The amount of bones that can influence a vertex
	 * @param BoneIndexBuffer - The buffer containing the indices for the bones.
	 * @param InMutableMeshVertexBuffers - The mutable buffers to be reading data from
	 * @param MeshLODIndex - The LOD index we are working with.
	 * @param InBoneIndexFormat - The format of the bone indices on the mutable mesh.
	 */
	CUSTOMIZABLEOBJECT_API void CopyMutableVertexBuffers(
		USkeletalMesh* OutSkeletalMesh,
		const int32 NumVerticesLODModel,
		const int32 NumBoneInfluences,
		const int32 BoneIndexBuffer,
		const mu::FMeshBufferSet& InMutableMeshVertexBuffers,
		const int32 MeshLODIndex,
		const mu::MESH_BUFFER_FORMAT& InBoneIndexFormat);

	
	/**
	 *Performs a copy of the data found on the index buffers on the mutable mesh to the buffers of the skeletal mesh
	 * @param InMutableMesh - The mutable mesh whose index buffers you want to work with
	 * @param OutLODModel - The LOD model to be updated.
	 * @return True if the operation could be performed successfully, false if not.
	 */
	CUSTOMIZABLEOBJECT_API bool CopyMutableIndexBuffers(mu::MeshPtrConst InMutableMesh,
	                                                    FSkeletalMeshLODRenderData& OutLODModel);
	
}
