// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Base class for common skinned mesh assets.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/StreamableRenderAsset.h"
#include "Interfaces/Interface_AsyncCompilation.h"
#include "ReferenceSkeleton.h"
#include "PerPlatformProperties.h"
#include "SkeletalMeshTypes.h"
#include "SkinnedAsset.generated.h"

class USkeleton;
class UMorphTarget;
class UPhysicsAsset;
class ITargetPlatform;
struct FSkeletalMaterial;
struct FSkeletalMeshLODInfo;

UCLASS(hidecategories = Object, config = Engine, editinlinenew, abstract)
class ENGINE_API USkinnedAsset : public UStreamableRenderAsset, public IInterface_AsyncCompilation
{
	GENERATED_BODY()

public:
	USkinnedAsset(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{}

	virtual ~USkinnedAsset() {}

	/** Return the reference skeleton. */
	virtual struct FReferenceSkeleton& GetRefSkeleton()
	PURE_VIRTUAL(USkinnedAsset::GetRefSkeleton, static FReferenceSkeleton Dummy; return Dummy;);
	virtual const struct FReferenceSkeleton& GetRefSkeleton() const
	PURE_VIRTUAL(USkinnedAsset::GetRefSkeleton, static const FReferenceSkeleton Dummy; return Dummy;);

	/** Return the LOD information for the specified LOD index. */
	virtual FSkeletalMeshLODInfo* GetLODInfo(int32 Index)
	PURE_VIRTUAL(USkinnedAsset::GetLODInfo, return nullptr;);
	virtual const FSkeletalMeshLODInfo* GetLODInfo(int32 Index) const
	PURE_VIRTUAL(USkinnedAsset::GetLODInfo, return nullptr;);

	/** Return if the material index is valid. */
	virtual bool IsValidMaterialIndex(int32 Index) const
	{ return GetMaterials().IsValidIndex(Index); }

	/** Return the number of materials of this mesh. */
	virtual int32 GetNumMaterials() const
	{ return GetMaterials().Num(); }

	/** Return the physics asset whose shapes will be used for shadowing. */
	virtual UPhysicsAsset* GetShadowPhysicsAsset() const
	PURE_VIRTUAL(USkinnedAsset::GetShadowPhysicsAsset, return nullptr;);

	/** Return the component orientation of a bone or socket. */
	virtual FMatrix GetComposedRefPoseMatrix(int32 InBoneIndex) const
	PURE_VIRTUAL(USkinnedAsset::GetShadowPhysicsAsset, return FMatrix::Identity;);

	/**
	 * Returns the UV channel data for a given material index. Used by the texture streamer.
	 * This data applies to all lod-section using the same material.
	 *
	 * @param MaterialIndex		the material index for which to get the data for.
	 * @return the data, or null if none exists.
	 */
	virtual const FMeshUVChannelInfo* GetUVChannelData(int32 MaterialIndex) const
	PURE_VIRTUAL(USkinnedAsset::GetUVChannelData, return nullptr;);

	/** Return whether ray tracing is supported on this mesh. */
	virtual bool GetSupportRayTracing() const
	PURE_VIRTUAL(USkinnedAsset::GetSupportRayTracing, return false;);

	/** Return the minimum ray tracing LOD of this mesh. */
	virtual int32 GetRayTracingMinLOD() const
	PURE_VIRTUAL(USkinnedAsset::GetRayTracingMinLOD, return 0;);

	/** Return the reference skeleton precomputed bases. */
	virtual TArray<FMatrix44f>& GetRefBasesInvMatrix()
	PURE_VIRTUAL(USkinnedAsset::GetRefBasesInvMatrix, static TArray<FMatrix44f> Dummy; return Dummy;);
	virtual const TArray<FMatrix44f>& GetRefBasesInvMatrix() const
	PURE_VIRTUAL(USkinnedAsset::GetRefBasesInvMatrix, static const TArray<FMatrix44f> Dummy; return Dummy;);

	/** Return the whole array of LOD info. */
	virtual TArray<FSkeletalMeshLODInfo>& GetLODInfoArray()
	PURE_VIRTUAL(USkinnedAsset::GetLODInfoArray, static TArray<FSkeletalMeshLODInfo> Dummy; return Dummy;);
	virtual const TArray<FSkeletalMeshLODInfo>& GetLODInfoArray() const
	PURE_VIRTUAL(USkinnedAsset::GetLODInfoArray, static const TArray<FSkeletalMeshLODInfo> Dummy; return Dummy;);

	/** Get the data to use for rendering. */
	virtual FORCEINLINE class FSkeletalMeshRenderData* GetResourceForRendering() const
	PURE_VIRTUAL(USkinnedAsset::GetResourceForRendering, return nullptr;);

	virtual int32 GetDefaultMinLod() const
	PURE_VIRTUAL(USkinnedAsset::GetDefaultMinLod, return 0;);

	virtual const FPerPlatformInt& GetMinLod() const
	PURE_VIRTUAL(USkinnedAsset::GetMinLod, static const FPerPlatformInt Dummy;  return Dummy;);

	/** Check the QualitLevel property is enabled for MinLod. */
	virtual bool IsMinLodQualityLevelEnable() const { return false; }

	virtual UPhysicsAsset* GetPhysicsAsset() const
	PURE_VIRTUAL(USkinnedAsset::GetPhysicsAsset, return nullptr;);

	virtual TArray<FSkeletalMaterial>& GetMaterials()
	PURE_VIRTUAL(USkinnedAsset::GetMaterials, static TArray<FSkeletalMaterial> Dummy; return Dummy;);
	virtual const TArray<FSkeletalMaterial>& GetMaterials() const
	PURE_VIRTUAL(USkinnedAsset::GetMaterials, static const TArray<FSkeletalMaterial> Dummy; return Dummy;);

	virtual int32 GetLODNum() const
	PURE_VIRTUAL(USkinnedAsset::GetLODNum, return 0;);

	virtual bool IsMaterialUsed(int32 MaterialIndex) const
	PURE_VIRTUAL(USkinnedAsset::IsMaterialUsed, return false;);

	virtual FBoxSphereBounds GetBounds() const
	PURE_VIRTUAL(USkinnedAsset::GetBounds, return FBoxSphereBounds(););

	/**
	 * Returns the "active" socket list - all sockets from this mesh plus all non-duplicates from the skeleton
	 * Const ref return value as this cannot be modified externally
	 */
	virtual TArray<class USkeletalMeshSocket*> GetActiveSocketList() const
	PURE_VIRTUAL(USkinnedAsset::GetActiveSocketList, static TArray<class USkeletalMeshSocket*> Dummy; return Dummy;);

	virtual USkeleton* GetSkeleton()
	PURE_VIRTUAL(USkinnedAsset::GetSkeleton, return nullptr;);
	virtual const USkeleton* GetSkeleton() const
	PURE_VIRTUAL(USkinnedAsset::GetSkeleton, return nullptr;);
	virtual void SetSkeleton(USkeleton* InSkeleton)
	PURE_VIRTUAL(USkinnedAsset::SetSkeleton,);

	/** Return true if given index's LOD is valid */
	virtual bool IsValidLODIndex(int32 Index) const
	PURE_VIRTUAL(USkinnedAsset::IsValidLODIndex, return false;);

	virtual int32 GetMinLodIdx(bool bForceLowestLODIdx = false) const
	PURE_VIRTUAL(USkinnedAsset::GetMinLodIdx, return 0;);

	/** Return the morph targets. */
	virtual TArray<TObjectPtr<UMorphTarget>>& GetMorphTargets()
	{ static TArray<TObjectPtr<UMorphTarget>> Dummy; return Dummy; }
	virtual const TArray<UMorphTarget*>& GetMorphTargets() const
	{ static const TArray<TObjectPtr<UMorphTarget>> Dummy; return Dummy; }

	/**Find a named MorphTarget from the morph targets */
	virtual UMorphTarget* FindMorphTarget(FName MorphTargetName) const
	{ return nullptr; }

	/** True if this mesh LOD needs to keep it's data on CPU. USkinnedAsset interface. */
	virtual bool NeedCPUData(int32 LODIndex) const
	PURE_VIRTUAL(USkinnedAsset::NeedCPUData, return false;);

	/** Return whether or not the mesh has vertex colors. */
	virtual bool GetHasVertexColors() const
	PURE_VIRTUAL(USkinnedAsset::GetHasVertexColors, return false;);

	virtual int32 GetPlatformMinLODIdx(const ITargetPlatform* TargetPlatform) const
	PURE_VIRTUAL(USkinnedAsset::GetPlatformMinLODIdx, return 0;);

	virtual const FPerPlatformBool& GetDisableBelowMinLodStripping() const
	PURE_VIRTUAL(USkinnedAsset::GetDisableBelowMinLodStripping, static const FPerPlatformBool Dummy; return Dummy;);

	virtual void SetSkinWeightProfilesData(int32 LODIndex, struct FSkinWeightProfilesData& SkinWeightProfilesData) {}

	/** Computes flags for building vertex buffers. */
	virtual uint32 GetVertexBufferFlags() const
	{ return GetHasVertexColors() ? ESkeletalMeshVertexFlags::HasVertexColors : ESkeletalMeshVertexFlags::None; }

	/**
	* UObject Interface
	* This will return detail info about this specific object. (e.g. AudioComponent will return the name of the cue,
	* ParticleSystemComponent will return the name of the ParticleSystem)  The idea here is that in many places
	* you have a component of interest but what you really want is some characteristic that you can use to track
	* down where it came from.
	*/
	virtual FString GetDetailedInfoInternal() const override
	{ return GetPathName(nullptr); }

#if WITH_EDITOR
	/** IInterface_AsyncCompilation begin*/
	virtual bool IsCompiling() const override { return false; }
	/** IInterface_AsyncCompilation end*/

	virtual FString BuildDerivedDataKey(const ITargetPlatform* TargetPlatform)
	PURE_VIRTUAL(USkinnedAsset::BuildDerivedDataKey, return TEXT(""););

	/* Return true if this asset was never build since its creation. */
	virtual bool IsInitialBuildDone() const
	PURE_VIRTUAL(USkinnedAsset::IsInitialBuildDone, return false;);

	/* Build a LOD model before creating its render data. */
	virtual void BuildLODModel(const ITargetPlatform* TargetPlatform, int32 LODIndex) {}

	/** Get whether this mesh should use LOD streaming for the given platform. */
	virtual bool GetEnableLODStreaming(const ITargetPlatform* TargetPlatform) const
	PURE_VIRTUAL(USkinnedAsset::GetEnableLODStreaming, return false;);

	/** Get the maximum number of LODs that can be streamed. */
	virtual int32 GetMaxNumStreamedLODs(const ITargetPlatform* TargetPlatform) const
	PURE_VIRTUAL(USkinnedAsset::GetMaxNumStreamedLODs, return 0;);

	virtual int32 GetMaxNumOptionalLODs(const ITargetPlatform* TargetPlatform) const
	PURE_VIRTUAL(USkinnedAsset::GetMaxNumOptionalLODs, return 0;);
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	virtual bool GetUseLegacyMeshDerivedDataKey() const	{ return false; }

	/** Get the source mesh data. */
	virtual class FSkeletalMeshModel* GetImportedModel() const
	PURE_VIRTUAL(USkinnedAsset::GetImportedModel, return nullptr;);
#endif // WITH_EDITORONLY_DATA
};

