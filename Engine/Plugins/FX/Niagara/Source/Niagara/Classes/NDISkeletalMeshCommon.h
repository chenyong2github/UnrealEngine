// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

//////////////////////////////////////////////////////////////////////////
// Helper classes for reducing duplicate code when accessing vertex positions. 

struct FSkeletalMeshAccessorHelper
{
	template<typename FilterMode, typename AreaWeightingMode>
	FORCEINLINE void Init(FNDISkeletalMesh_InstanceData* InstData)
	{
		Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
		Mesh = InstData->Mesh;
		LODData = &InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
		IndexBuffer = LODData->MultiSizeIndexContainer.GetIndexBuffer();
		SkinningData = InstData->SkinningData.SkinningData.Get();
		Usage = InstData->SkinningData.Usage;
	}

	USkeletalMeshComponent* Comp = nullptr;
	USkeletalMesh* Mesh = nullptr;
	TWeakObjectPtr<USkeletalMesh> MeshSafe;
	FSkeletalMeshLODRenderData* LODData = nullptr;
	FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
	FRawStaticIndexBuffer16or32Interface* IndexBuffer = nullptr;
	const FSkeletalMeshSamplingRegion* SamplingRegion = nullptr;
	const FSkeletalMeshSamplingRegionBuiltData* SamplingRegionBuiltData = nullptr;
	FSkeletalMeshSkinningData* SkinningData = nullptr;
	FSkeletalMeshSkinningDataUsage Usage;
};

template<>
void FSkeletalMeshAccessorHelper::Init<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FNDISkeletalMesh_InstanceData* InstData);

template<>
void FSkeletalMeshAccessorHelper::Init<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FNDISkeletalMesh_InstanceData* InstData);

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
	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutNormal, FVector& OutBinormal) = delete;
	FORCEINLINE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
	FORCEINLINE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex) = delete;
};

template<>
struct FSkinnedPositionAccessorHelper<TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::None>>
{
	FORCEINLINE int32 GetBoneCount(FSkeletalMeshAccessorHelper& Accessor, bool RequiresPrevious) const
	{
		if (const USkeletalMesh* Mesh = Accessor.Mesh)
		{
			return Mesh->RefSkeleton.GetNum();
		}

		return 0;
	}

	FORCEINLINE void GetTriangleIndices(FSkeletalMeshAccessorHelper& Accessor, int32 Tri, int32& Idx0, int32& Idx1, int32& Idx2)
	{
		const int32 BaseIndex = Tri * 3;
		checkSlow(BaseIndex + 2 < Accessor.IndexBuffer->Num());
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

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentZ)
	{
		GetSkeletalMeshRefTangentBasis(Accessor.Mesh, *Accessor.LODData, *Accessor.SkinWeightBuffer, VertexIndex, OutTangentX, OutTangentZ);
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		const int32 NumRealBones = Accessor.Mesh->RefSkeleton.GetRawBoneNum();
		if (BoneIndex < NumRealBones)
		{
			return Accessor.Mesh->GetComposedRefPoseMatrix(BoneIndex).GetOrigin();
		}

		const FTransform& RefTransform = Accessor.Mesh->RefSkeleton.GetRefBonePose()[BoneIndex];
		return RefTransform.GetLocation();
	}

	FORCEINLINE_DEBUGGABLE FVector GetSkinnedBonePreviousPosition(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return GetSkinnedBonePosition(Accessor, BoneIndex);
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBoneRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		const int32 NumRealBones = Accessor.Mesh->RefSkeleton.GetRawBoneNum();
		if (BoneIndex < NumRealBones)
		{
			return Accessor.Mesh->GetComposedRefPoseMatrix(BoneIndex).ToQuat();
		}

		const FTransform& RefTransform = Accessor.Mesh->RefSkeleton.GetRefBonePose()[BoneIndex];
		return RefTransform.GetRotation();
	}

	FORCEINLINE_DEBUGGABLE FQuat GetSkinnedBonePreviousRotation(FSkeletalMeshAccessorHelper& Accessor, int32 BoneIndex)
	{
		return GetSkinnedBoneRotation(Accessor, BoneIndex);
	}
};

template<>
struct FSkinnedPositionAccessorHelper<TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::SkinOnTheFly>>
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
		checkSlow(BaseIndex + 2 < Accessor.IndexBuffer->Num());
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

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentZ)
	{
		USkeletalMeshComponent::GetSkinnedTangentBasis(Accessor.Comp, VertexIndex, *Accessor.LODData, *Accessor.SkinWeightBuffer, Accessor.SkinningData->PrevBoneRefToLocals(), OutTangentX, OutTangentZ);
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
struct FSkinnedPositionAccessorHelper<TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::PreSkin>>
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
		checkSlow(BaseIndex + 2 < Accessor.IndexBuffer->Num());
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

	FORCEINLINE void GetSkinnedTangentBasis(FSkeletalMeshAccessorHelper& Accessor, int32 VertexIndex, FVector& OutTangentX, FVector& OutTangentZ)
	{
		Accessor.SkinningData->GetTangentBasis(Accessor.Usage.GetLODIndex(), VertexIndex, OutTangentX, OutTangentZ);
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
// Helper for accessing misc vertex data
template<bool bUseFullPrecisionUVs>
struct FSkelMeshVertexAccessor
{
	FORCEINLINE FVector2D GetVertexUV(FSkeletalMeshLODRenderData& LODData, int32 VertexIdx, int32 UVChannel)const
	{
		if (bUseFullPrecisionUVs)
		{
			return LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::HighPrecision>(VertexIdx, UVChannel);
		}
		else
		{
			return LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV_Typed<EStaticMeshVertexUVType::Default>(VertexIdx, UVChannel);
		}
	}

	FORCEINLINE FLinearColor GetVertexColor(FSkeletalMeshLODRenderData& LODData, int32 VertexIdx)const
	{
		return LODData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIdx);
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
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();

		bool bAreaWeighting = false;
		if (InstData->SamplingRegionIndices.Num() > 1)
		{
			bAreaWeighting = InstData->SamplingRegionAreaWeightedSampler.IsValid();
		}
		else if (InstData->SamplingRegionIndices.Num() == 1)
		{
			const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(InstData->SamplingRegionIndices[0]);
			bAreaWeighting = Region.bSupportUniformlyDistributedSampling;
		}
		else
		{
			int32 LODIndex = InstData->GetLODIndex();
			bAreaWeighting = InstData->Mesh->GetLODInfo(LODIndex)->bSupportUniformlyDistributedSampling;
		}

		if (bAreaWeighting)
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>(Interface, BindingInfo, InstanceData, OutFunc);
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
		UNiagaraDataInterfaceSkeletalMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Interface);

		if (InstData->SamplingRegionIndices.Num() == 1)
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (InstData->SamplingRegionIndices.Num() > 1)
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::MultiRegion>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::None>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

//External function binder choosing between template specializations based vetrex data format
template<typename NextBinder>
struct TVertexAccessorBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		UNiagaraDataInterfaceSkeletalMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Interface);
		USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(InstData->Component.Get());
		FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
		FSkeletalMeshLODRenderData& LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

		if (LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
		{
			NextBinder::template Bind<ParamTypes..., FSkelMeshVertexAccessor<true>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			NextBinder::template Bind<ParamTypes..., FSkelMeshVertexAccessor<false>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
	}
};

//External function binder choosing between template specializations based on skinning mode
template<typename NextBinder>
struct TSkinningModeBinder
{
	template<typename... ParamTypes>
	static void Bind(UNiagaraDataInterface* Interface, const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
	{
		FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
		UNiagaraDataInterfaceSkeletalMesh* MeshInterface = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Interface);
		USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(InstData->Component.Get());
		if (MeshInterface->SkinningMode == ENDISkeletalMesh_SkinningMode::None || !Component)//Can't skin if we have no component.
		{
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::None>>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (MeshInterface->SkinningMode == ENDISkeletalMesh_SkinningMode::SkinOnTheFly)
		{
			check(Component);
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::SkinOnTheFly>>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else if (MeshInterface->SkinningMode == ENDISkeletalMesh_SkinningMode::PreSkin)
		{
			check(Component);
			NextBinder::template Bind<ParamTypes..., FSkinnedPositionAccessorHelper<TIntegralConstant<ENDISkeletalMesh_SkinningMode, ENDISkeletalMesh_SkinningMode::PreSkin>>>(Interface, BindingInfo, InstanceData, OutFunc);
		}
		else
		{
			checkf(false, TEXT("Invalid skinning mode in %s"), *Interface->GetPathName());
		}
	}
};