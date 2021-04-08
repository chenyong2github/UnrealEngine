// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// These are to help readability of template specializations
using TNDISkelMesh_FilterModeNone = TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>;
using TNDISkelMesh_FilterModeSingle = TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>;
using TNDISkelMesh_FilterModeMulti = TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>;

using TNDISkelMesh_AreaWeightingOff = TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>;
using TNDISkelMesh_AreaWeightingOn = TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>;

using TNDISkelMesh_SkinningModeInvalid = TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::Invalid>;
using TNDISkelMesh_SkinningModeNone = TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::None>;
using TNDISkelMesh_SkinningModeOnTheFly = TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::SkinOnTheFly>;
using TNDISkelMesh_SkinningModePreSkin = TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::PreSkin>;

//////////////////////////////////////////////////////////////////////////
// Helper classes for reducing duplicate code when accessing vertex positions. 

struct FSkeletalMeshAccessorHelper
{
	FSkeletalMeshAccessorHelper()
	{
	}
	~FSkeletalMeshAccessorHelper()
	{
		if (SkinningData != nullptr)
		{
			SkinningData->ExitRead();
		}
	}

	FSkeletalMeshAccessorHelper(const FSkeletalMeshAccessorHelper&) = delete;
	FSkeletalMeshAccessorHelper(FSkeletalMeshAccessorHelper&&) = delete;
	FSkeletalMeshAccessorHelper operator=(const FSkeletalMeshAccessorHelper&) = delete;
	FSkeletalMeshAccessorHelper operator=(FSkeletalMeshAccessorHelper&&) = delete;

	template<typename FilterMode, typename AreaWeightingMode>
	FORCEINLINE void Init(FNDISkeletalMesh_InstanceData* InstData)
	{
		Comp = Cast<USkeletalMeshComponent>(InstData->SceneComponent.Get());
		Mesh = InstData->SkeletalMesh.Get();
		LODData = InstData->CachedLODData;
		SkinWeightBuffer = InstData->GetSkinWeights();
		IndexBuffer = LODData ? LODData->MultiSizeIndexContainer.GetIndexBuffer() : nullptr;
		SkinningData = InstData->SkinningData.SkinningData.Get();
		Usage = InstData->SkinningData.Usage;

		if (SkinningData != nullptr)
		{
			SkinningData->EnterRead();
		}
	}

	FORCEINLINE bool AreBonesAccessible() const
	{
		return Mesh != nullptr;
	}

	FORCEINLINE bool IsLODAccessible() const
	{
		return LODData != nullptr;
	}

	USkeletalMeshComponent* Comp = nullptr;
	USkeletalMesh* Mesh = nullptr;
	TWeakObjectPtr<USkeletalMesh> MeshSafe;
	const FSkeletalMeshLODRenderData* LODData = nullptr;
	const FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = nullptr;
	const FSkeletalMeshSamplingRegion* SamplingRegion = nullptr;
	const FSkeletalMeshSamplingRegionBuiltData* SamplingRegionBuiltData = nullptr;
	FSkeletalMeshSkinningData* SkinningData = nullptr;
	FSkeletalMeshSkinningDataUsage Usage;
};

template<>
void FSkeletalMeshAccessorHelper::Init<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOff>(FNDISkeletalMesh_InstanceData* InstData);

template<>
void FSkeletalMeshAccessorHelper::Init<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOn>(FNDISkeletalMesh_InstanceData* InstData);

//////////////////////////////////////////////////////////////////////////

template<typename SkinningMode>
struct FSkinnedPositionAccessorHelper
{
	FORCEINLINE int32 GetBoneCount(FSkeletalMeshAccessorHelper& Accessor, bool RequiresPrevious) const = delete;
	FORCEINLINE void GetTriangleIndices(int32 Tri, int32& Idx0, int32& Idx1, int32& Idx2) = delete;
	FORCEINLINE void GetSkinnedTrianglePositions(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2, FVector& OutPrev0, FVector& OutPrev1, FVector& OutPrev2, int32& Idx0, int32& Idx1, int32& Idx2) = delete;
	FORCEINLINE void GetSkinnedTrianglePositions(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2, int32& Idx0, int32& Idx1, int32& Idx2) = delete;
	FORCEINLINE FVector GetSkinnedVertexPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex) = delete;
	FORCEINLINE FVector GetSkinnedVertexPreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex) = delete;
	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ) = delete;
	FORCEINLINE void GetSkinnedPreviousTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ) = delete;
	FORCEINLINE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
	FORCEINLINE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
};

template<>
struct FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModeInvalid>
{
	FORCEINLINE int32 GetBoneCount(FSkeletalMeshAccessorHelper& Accessor, bool RequiresPrevious) const
	{
		if (const USkeletalMesh* Mesh = Accessor.Mesh)
		{
			return Mesh->GetRefSkeleton().GetNum();
		}

		return 0;
	}

	FORCEINLINE void GetTriangleIndices(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, int32& Idx0, int32& Idx1, int32& Idx2)
	{
		Idx0 = Idx1 = Idx2 = -1;
	}

	FORCEINLINE void GetSkinnedTrianglePositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = OutPos1 = OutPos2 = FVector::ZeroVector;
	}

	FORCEINLINE void GetSkinnedTrianglePreviousPositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = OutPos1 = OutPos2 = FVector::ZeroVector;
	}

	FORCEINLINE FVector GetSkinnedVertexPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return FVector::ZeroVector;
	}

	FORCEINLINE FVector GetSkinnedVertexPreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return FVector::ZeroVector;
	}

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		OutTangentX = FVector(1.0f, 0.0f, 0.0f);
		OutTangentY = FVector(0.0f, 1.0f, 0.0f);
		OutTangentZ = FVector(0.0f, 0.0f, 1.0f);
	}

	FORCEINLINE void GetSkinnedPreviousTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		OutTangentX = FVector(1.0f, 0.0f, 0.0f);
		OutTangentY = FVector(0.0f, 1.0f, 0.0f);
		OutTangentZ = FVector(0.0f, 0.0f, 1.0f);
	}

	// The bone accessor functions are valid if a mesh is present, so don't stub them entirely
	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		const int32 NumRealBones = Accessor.Mesh->GetRefSkeleton().GetRawBoneNum();
		if (BoneIndex < NumRealBones)
		{
			return Accessor.Mesh->GetComposedRefPoseMatrix(BoneIndex).GetOrigin();
		}

		const FTransform& RefTransform = Accessor.Mesh->GetRefSkeleton().GetRefBonePose()[BoneIndex];
		return RefTransform.GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return GetSkinnedBonePosition(Accessor, BoneIndex);
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		const int32 NumRealBones = Accessor.Mesh->GetRefSkeleton().GetRawBoneNum();
		if (BoneIndex < NumRealBones)
		{
			return Accessor.Mesh->GetComposedRefPoseMatrix(BoneIndex).GetMatrixWithoutScale().ToQuat();
		}

		const FTransform& RefTransform = Accessor.Mesh->GetRefSkeleton().GetRefBonePose()[BoneIndex];
		return RefTransform.GetRotation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return GetSkinnedBoneRotation(Accessor, BoneIndex);
	}
};

template<>
struct FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModeNone>
{
	FORCEINLINE int32 GetBoneCount(FSkeletalMeshAccessorHelper& Accessor, bool RequiresPrevious) const
	{
		if (const USkeletalMesh* Mesh = Accessor.Mesh)
		{
			return Mesh->GetRefSkeleton().GetNum();
		}

		return 0;
	}

	FORCEINLINE void GetTriangleIndices(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, int32& Idx0, int32& Idx1, int32& Idx2)
	{
		const int32 BaseIndex = Tri * 3;
		check(BaseIndex + 2 < Accessor.IndexBuffer->Num());
		Idx0 = Accessor.IndexBuffer->Get(BaseIndex);
		Idx1 = Accessor.IndexBuffer->Get(BaseIndex + 1);
		Idx2 = Accessor.IndexBuffer->Get(BaseIndex + 2);
	}

	FORCEINLINE void GetSkinnedTrianglePositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, Idx0);
		OutPos1 = GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, Idx1);
		OutPos2 = GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, Idx2);
	}

	FORCEINLINE void GetSkinnedTrianglePreviousPositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, Idx0);
		OutPos1 = GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, Idx1);
		OutPos2 = GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, Idx2);
	}

	FORCEINLINE FVector GetSkinnedVertexPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, VertexIndex);
	}

	FORCEINLINE FVector GetSkinnedVertexPreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return GetSkeletalMeshRefVertLocation(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, VertexIndex);
	}

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		GetSkeletalMeshRefTangentBasis(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, VertexIndex, OutTangentX, OutTangentY, OutTangentZ);
	}

	FORCEINLINE void GetSkinnedPreviousTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		GetSkeletalMeshRefTangentBasis(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, VertexIndex, OutTangentX, OutTangentY, OutTangentZ);
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		const int32 NumRealBones = Accessor.Mesh->GetRefSkeleton().GetRawBoneNum();
		if (BoneIndex < NumRealBones)
		{
			return Accessor.Mesh->GetComposedRefPoseMatrix(BoneIndex).GetOrigin();
		}

		const FTransform& RefTransform = Accessor.Mesh->GetRefSkeleton().GetRefBonePose()[BoneIndex];
		return RefTransform.GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return GetSkinnedBonePosition(Accessor, BoneIndex);
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		const int32 NumRealBones = Accessor.Mesh->GetRefSkeleton().GetRawBoneNum();
		if (BoneIndex < NumRealBones)
		{
			return Accessor.Mesh->GetComposedRefPoseMatrix(BoneIndex).GetMatrixWithoutScale().ToQuat();
		}

		const FTransform& RefTransform = Accessor.Mesh->GetRefSkeleton().GetRefBonePose()[BoneIndex];
		return RefTransform.GetRotation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return GetSkinnedBoneRotation(Accessor, BoneIndex);
	}
};

template<>
struct FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModeOnTheFly>
{
	FORCEINLINE int32 GetBoneCount(FSkeletalMeshAccessorHelper& Accessor, bool RequiresPrevious) const
	{
		if (const FSkeletalMeshSkinningData* SkinningData = Accessor.SkinningData)
		{
			return SkinningData->GetBoneCount(RequiresPrevious);
		}

		return 0;
	}

	FORCEINLINE void GetTriangleIndices(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, int32& Idx0, int32& Idx1, int32& Idx2)
	{
		const int32 BaseIndex = Tri * 3;
		check(BaseIndex + 2 < Accessor.IndexBuffer->Num());
		Idx0 = Accessor.IndexBuffer->Get(BaseIndex);
		Idx1 = Accessor.IndexBuffer->Get(BaseIndex + 1);
		Idx2 = Accessor.IndexBuffer->Get(BaseIndex + 2);
	}

	FORCEINLINE void GetSkinnedTrianglePositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, Idx0, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->CurrBoneRefToLocals());
		OutPos1 = USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, Idx1, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->CurrBoneRefToLocals());
		OutPos2 = USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, Idx2, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->CurrBoneRefToLocals());
	}

	FORCEINLINE void GetSkinnedTrianglePreviousPositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, Idx0, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->PrevBoneRefToLocals());
		OutPos1 = USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, Idx1, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->PrevBoneRefToLocals());
		OutPos2 = USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, Idx2, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->PrevBoneRefToLocals());
	}

	FORCEINLINE FVector GetSkinnedVertexPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, VertexIndex, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->CurrBoneRefToLocals());
	}

	FORCEINLINE FVector GetSkinnedVertexPreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return USkeletalMeshComponent::GetSkinnedVertexPosition(Accessor.Comp, VertexIndex, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->PrevBoneRefToLocals());
	}

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		USkeletalMeshComponent::GetSkinnedTangentBasis(Accessor.Comp, VertexIndex, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->CurrBoneRefToLocals(), OutTangentX, OutTangentY, OutTangentZ);
	}

	FORCEINLINE void GetSkinnedPreviousTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		USkeletalMeshComponent::GetSkinnedTangentBasis(Accessor.Comp, VertexIndex, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->PrevBoneRefToLocals(), OutTangentX, OutTangentY, OutTangentZ);
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->CurrComponentTransforms()[BoneIndex].GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->PrevComponentTransforms()[BoneIndex].GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->CurrComponentTransforms()[BoneIndex].GetRotation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->PrevComponentTransforms()[BoneIndex].GetRotation();
	}
};

template<>
struct FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModePreSkin>
{
	FORCEINLINE int32 GetBoneCount(FSkeletalMeshAccessorHelper& Accessor, bool RequiresPrevious) const
	{
		if (const FSkeletalMeshSkinningData* SkinningData = Accessor.SkinningData)
		{
			return SkinningData->GetBoneCount(RequiresPrevious);
		}

		return 0;
	}

	FORCEINLINE void GetTriangleIndices(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, int32& Idx0, int32& Idx1, int32& Idx2)
	{
		const int32 BaseIndex = Tri * 3;
		check(BaseIndex + 2 < Accessor.IndexBuffer->Num());
		Idx0 = Accessor.IndexBuffer->Get(BaseIndex);
		Idx1 = Accessor.IndexBuffer->Get(BaseIndex + 1);
		Idx2 = Accessor.IndexBuffer->Get(BaseIndex + 2);
	}

	FORCEINLINE void GetSkinnedTrianglePositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = Accessor.SkinningData->GetPosition(Accessor.Usage.GetLODIndex(), Idx0);
		OutPos1 = Accessor.SkinningData->GetPosition(Accessor.Usage.GetLODIndex(), Idx1);
		OutPos2 = Accessor.SkinningData->GetPosition(Accessor.Usage.GetLODIndex(), Idx2);
	}

	FORCEINLINE void GetSkinnedTrianglePreviousPositions(FSkeletalMeshAccessorHelper& Accessor, int32 Idx0, int32 Idx1, int32 Idx2, FVector& OutPos0, FVector& OutPos1, FVector& OutPos2)
	{
		OutPos0 = Accessor.SkinningData->GetPreviousPosition(Accessor.Usage.GetLODIndex(), Idx0);
		OutPos1 = Accessor.SkinningData->GetPreviousPosition(Accessor.Usage.GetLODIndex(), Idx1);
		OutPos2 = Accessor.SkinningData->GetPreviousPosition(Accessor.Usage.GetLODIndex(), Idx2);
	}

	FORCEINLINE FVector GetSkinnedVertexPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return Accessor.SkinningData->GetPosition(Accessor.Usage.GetLODIndex(), VertexIndex);
	}

	FORCEINLINE FVector GetSkinnedVertexPreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex)
	{
		return Accessor.SkinningData->GetPreviousPosition(Accessor.Usage.GetLODIndex(), VertexIndex);
	}

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		Accessor.SkinningData->GetTangentBasis(Accessor.Usage.GetLODIndex(), VertexIndex, OutTangentX, OutTangentY, OutTangentZ);
	}

	FORCEINLINE void GetSkinnedPreviousTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentY, FVector& OutTangentZ)
	{
		Accessor.SkinningData->GetPreviousTangentBasis(Accessor.Usage.GetLODIndex(), VertexIndex, OutTangentX, OutTangentY, OutTangentZ);
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->CurrComponentTransforms()[BoneIndex].GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->PrevComponentTransforms()[BoneIndex].GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->CurrComponentTransforms()[BoneIndex].GetRotation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return Accessor.SkinningData->PrevComponentTransforms()[BoneIndex].GetRotation();
	}
};

//////////////////////////////////////////////////////////////////////////
// Helpers for accessing misc vertex data
template<bool bUseFullPrecisionUVs>
struct FSkelMeshVertexAccessor
{
	FORCEINLINE FVector2D GetVertexUV(const FSkeletalMeshLODRenderData* LODData, int32 VertexIdx, int32 UVChannel)const
	{
		check(LODData);
		if (bUseFullPrecisionUVs)
		{
			return LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::HighPrecision>(VertexIdx, UVChannel);
		}
		else
		{
			return LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::Default>(VertexIdx, UVChannel);
		}
	}

	FORCEINLINE FLinearColor GetVertexColor(FSkeletalMeshLODRenderData* LODData, int32 VertexIdx)const
	{
		check(LODData);
		return LODData->StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIdx);
	}
};

struct FSkelMeshVertexAccessorNoop
{
	FORCEINLINE FVector2D GetVertexUV(const FSkeletalMeshLODRenderData* LODData, int32 VertexIdx, int32 UVChannel)const
	{
		return FVector2D(0.0f, 0.0f);
	}

	FORCEINLINE FLinearColor GetVertexColor(const FSkeletalMeshLODRenderData* LODData, int32 VertexIdx)const
	{
		return FLinearColor::White;
	}
};

//////////////////////////////////////////////////////////////////////////
//Function Binders.

//External function binder choosing between template specializations based on if we're area weighting or not.
template<typename NextBinder>
struct TAreaWeightingModeBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		check(InstData);

		UNiagaraDataInterfaceSkeletalMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Interface);

		bool bAreaWeighting = false;
		if (InstData->bAllowCPUMeshDataAccess)
		{
			if (InstData->SamplingRegionIndices.Num() > 1)
			{
				bAreaWeighting = InstData->SamplingRegionAreaWeightedSampler.IsValid();
			}
			else if (InstData->SamplingRegionIndices.Num() == 1)
			{
				const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->SkeletalMesh->GetSamplingInfo();
				const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[0]);
				bAreaWeighting = Region.bSupportUniformlyDistributedSampling;
			}
			else
			{
				int32 LODIndex = InstData->GetLODIndex();
				bAreaWeighting = InstData->SkeletalMesh->GetLODInfo(LODIndex)->bSupportUniformlyDistributedSampling;
			}
		}

		if (bAreaWeighting)
		{
			NextBinder::template Bind<ParamTypes..., TNDISkelMesh_AreaWeightingOn>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., TNDISkelMesh_AreaWeightingOff>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

//External function binder choosing between template specializations based on filtering methods
template<typename NextBinder>
struct TFilterModeBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		check(InstData);

		if (InstData->SamplingRegionIndices.Num() == 1)
		{
			NextBinder::template Bind<ParamTypes..., TNDISkelMesh_FilterModeSingle>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (InstData->SamplingRegionIndices.Num() > 1)
		{
			NextBinder::template Bind<ParamTypes..., TNDISkelMesh_FilterModeMulti>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., TNDISkelMesh_FilterModeNone>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

//External function binder choosing between template specializations based vertex data format
template<typename NextBinder>
struct TVertexAccessorBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		check(InstData);

		if (InstData->bAllowCPUMeshDataAccess)
		{
			const FSkeletalMeshLODRenderData* LODData = InstData->CachedLODData;
			if (LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
			{
				NextBinder::template Bind<ParamTypes..., FSkelMeshVertexAccessor<true>>(Interface, BindingInfo, InstanceData, OutFunc);
			}
			else
			{
				NextBinder::template Bind<ParamTypes..., FSkelMeshVertexAccessor<false>>(Interface, BindingInfo, InstanceData, OutFunc);
			}
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., FSkelMeshVertexAccessorNoop>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

//External function binder choosing between template specializations based on skinning mode
template<typename NextBinder>
struct TSkinningModeBinder
{
	template<typename... ParamTypes>
	static void BindIgnoreCPUAccess(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		UNiagaraDataInterfaceSkeletalMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Interface);
		USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(InstData->SceneComponent.Get());
		if (MeshInterface->SkinningMode == ENDISkeletalMesh_SkinningMode::None || !Component) // Can't skin if we have no component.
		{
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModeNone>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (MeshInterface->SkinningMode == ENDISkeletalMesh_SkinningMode::SkinOnTheFly)
		{
			check(Component);
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModeOnTheFly>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (MeshInterface->SkinningMode == ENDISkeletalMesh_SkinningMode::PreSkin)
		{
			check(Component);
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModePreSkin>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			checkf(false, TEXT("Invalid skinning mode in %s"), *Interface->GetPathName());
		}
	}

	template<typename... ParamTypes>
	static void BindCheckCPUAccess(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		if (!InstData->bAllowCPUMeshDataAccess) // No-op when we can't access the mesh on CPU
		{
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TNDISkelMesh_SkinningModeInvalid>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			BindIgnoreCPUAccess(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};
