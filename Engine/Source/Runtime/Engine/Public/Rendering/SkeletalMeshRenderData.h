// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIDefinitions.h"
#include "Containers/IndirectArray.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

struct FMeshUVChannelInfo;
class USkeletalMesh;
struct FSkeletalMaterial;
class UMorphTarget;
struct FResourceSizeEx;

class FSkeletalMeshRenderData
{
public:
	/** Per-LOD render data. */
	TIndirectArray<FSkeletalMeshLODRenderData> LODRenderData;

	/** True if rhi resources are initialized */
	bool bReadyForStreaming;

	/** Const after serialization. */
	uint8 NumInlinedLODs;

	/** Const after serialization. */
	uint8 NumOptionalLODs;

	/** [RenderThread] Index of the most detailed valid LOD. */
	uint8 CurrentFirstLODIdx;

	/** [GameThread/RenderThread] Future value of CurrentFirstLODIdx. */
	uint8 PendingFirstLODIdx;

#if WITH_EDITORONLY_DATA
	/** UV data used for streaming accuracy debug view modes. In sync for rendering thread */
	TArray<FMeshUVChannelInfo> UVChannelDataPerMaterial;
#endif

	FSkeletalMeshRenderData();

#if WITH_EDITOR
	void Cache(const ITargetPlatform* TargetPlatform, USkeletalMesh* Owner);

	void SyncUVChannelData(const TArray<FSkeletalMaterial>& ObjectData);
#endif

	/** Serialize to/from the specified archive.. */
	void Serialize(FArchive& Ar, USkeletalMesh* Owner);

	/** Initializes rendering resources. */
	void InitResources(bool bNeedsVertexColors, TArray<UMorphTarget*>& InMorphTargets, USkeletalMesh* Owner);

	/** Releases rendering resources. */
	ENGINE_API void ReleaseResources();

	/** Return the resource size */
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);

	/** Returns true if this resource must be skinned on the CPU for the given feature level. */
	ENGINE_API bool RequiresCPUSkinning(ERHIFeatureLevel::Type FeatureLevel) const;

	/** Returns the number of bone influences per vertex. */
	uint32 GetNumBoneInfluences() const;

	/**
	* Computes the maximum number of bones per section used to render this mesh.
	*/
	ENGINE_API int32 GetMaxBonesPerSection() const;

	/** Return first valid LOD index starting at MinLODIdx. */
	ENGINE_API int32 GetFirstValidLODIdx(int32 MinLODIdx) const;

	/** Return the current first LODIdx that can be used. */
	FORCEINLINE int32 GetCurrentFirstLODIdx(int32 MinLODIdx) const
	{
		return GetFirstValidLODIdx(FMath::Max<int32>(CurrentFirstLODIdx, MinLODIdx));
	}

private:
	/** True if the resource has been initialized. */
	bool bInitialized = false;
};