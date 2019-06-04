// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraRenderer.h"
#include "NiagaraScript.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "NiagaraWorldManager.h"
#include "Async/ParallelFor.h"
#include "NiagaraStats.h"
#include "Templates/AlignmentTemplates.h"
#include "NDISkeletalMeshCommon.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSkeletalMesh"

DECLARE_CYCLE_STAT(TEXT("PreSkin"), STAT_NiagaraSkel_PreSkin, STATGROUP_Niagara);

//////////////////////////////////////////////////////////////////////////

FSkeletalMeshSamplingRegionAreaWeightedSampler::FSkeletalMeshSamplingRegionAreaWeightedSampler()
	: Owner(nullptr)
{
}

void FSkeletalMeshSamplingRegionAreaWeightedSampler::Init(FNDISkeletalMesh_InstanceData* InOwner)
{
	Owner = InOwner;
	Initialize();
}

float FSkeletalMeshSamplingRegionAreaWeightedSampler::GetWeights(TArray<float>& OutWeights)
{
	check(Owner && Owner->Mesh);
	check(Owner->Mesh->IsValidLODIndex(Owner->GetLODIndex()));

	float Total = 0.0f;
	int32 NumUsedRegions = Owner->SamplingRegionIndices.Num();
	if (NumUsedRegions <= 1)
	{
		//Use 0 or 1 Sampling region. Only need additional area weighting between regions if we're sampling from multiple.
		return 0.0f;
	}
	
	const FSkeletalMeshSamplingInfo& SamplingInfo = Owner->Mesh->GetSamplingInfo();
	OutWeights.Empty(NumUsedRegions);
	for (int32 i = 0; i < NumUsedRegions; ++i)
	{
		int32 RegionIdx = Owner->SamplingRegionIndices[i];
		const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(RegionIdx);
		float T = SamplingInfo.GetRegionBuiltData(RegionIdx).AreaWeightedSampler.GetTotalWeight();
		OutWeights.Add(T);
		Total += T;
	}
	return Total;
}

//////////////////////////////////////////////////////////////////////////
FSkeletalMeshSkinningDataHandle::FSkeletalMeshSkinningDataHandle()
	: SkinningData(nullptr)
{
}

FSkeletalMeshSkinningDataHandle::FSkeletalMeshSkinningDataHandle(FSkeletalMeshSkinningDataUsage InUsage, TSharedPtr<FSkeletalMeshSkinningData> InSkinningData)
	: Usage(InUsage)
	, SkinningData(InSkinningData)
{
	if (FSkeletalMeshSkinningData* SkinData = SkinningData.Get())
	{
		SkinData->RegisterUser(Usage);
	}
}

FSkeletalMeshSkinningDataHandle::~FSkeletalMeshSkinningDataHandle()
{
	if (FSkeletalMeshSkinningData* SkinData = SkinningData.Get())
	{
		SkinData->UnregisterUser(Usage);
	}
}

//////////////////////////////////////////////////////////////////////////
void FSkeletalMeshSkinningData::ForceDataRefresh()
{
	FScopeLock Lock(&CriticalSection);
	bForceDataRefresh = true;
}

void FSkeletalMeshSkinningData::RegisterUser(FSkeletalMeshSkinningDataUsage Usage)
{
	FScopeLock Lock(&CriticalSection);
	USkeletalMeshComponent* SkelComp = MeshComp.Get();

	int32 LODIndex = Usage.GetLODIndex();
	check(LODIndex != INDEX_NONE);
	check(SkelComp);

	LODData.SetNum(SkelComp->SkeletalMesh->GetLODInfoArray().Num());

	if (Usage.NeedBoneMatrices())
	{
		++BoneMatrixUsers;
	}

	FLODData& LOD = LODData[LODIndex];
	if (Usage.NeedPreSkinnedVerts())
	{
		++LOD.PreSkinnedVertsUsers;
	}

	if (Usage.NeedsDataImmediately())
	{
		check(IsInGameThread());
		if (CurrBoneRefToLocals().Num() == 0)
		{
			SkelComp->CacheRefToLocalMatrices(CurrBoneRefToLocals()); 
		}
		
		//Prime the prev matrices if they're missing.
		if (PrevBoneRefToLocals().Num() != CurrBoneRefToLocals().Num())
		{
			PrevBoneRefToLocals() = CurrBoneRefToLocals();
		}

		if (Usage.NeedPreSkinnedVerts() && CurrSkinnedPositions(LODIndex).Num() == 0)
		{
			FSkeletalMeshLODRenderData& SkelMeshLODData = SkelComp->SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
			FSkinWeightVertexBuffer* SkinWeightBuffer = SkelComp->GetSkinWeightBuffer(LODIndex);
			USkeletalMeshComponent::ComputeSkinnedPositions(SkelComp, CurrSkinnedPositions(LODIndex), CurrBoneRefToLocals(), SkelMeshLODData, *SkinWeightBuffer);

			//Prime the previous positions if they're missing
			if (PrevSkinnedPositions(LODIndex).Num() != CurrSkinnedPositions(LODIndex).Num())
			{
				PrevSkinnedPositions(LODIndex) = CurrSkinnedPositions(LODIndex);
			}
		}
	}
}

void FSkeletalMeshSkinningData::UnregisterUser(FSkeletalMeshSkinningDataUsage Usage)
{
	FScopeLock Lock(&CriticalSection);
	check(LODData.IsValidIndex(Usage.GetLODIndex()));

	if (Usage.NeedBoneMatrices())
	{
		--BoneMatrixUsers;
	}

	FLODData& LOD = LODData[Usage.GetLODIndex()];
	if (Usage.NeedPreSkinnedVerts())
	{
		--LOD.PreSkinnedVertsUsers;
	}
}

bool FSkeletalMeshSkinningData::IsUsed()const
{
	if (BoneMatrixUsers > 0)
	{
		return true;
	}

	for (const FLODData& LOD : LODData)
	{
		if (LOD.PreSkinnedVertsUsers > 0)
		{
			return true;
		}
	}

	return false;
}

bool FSkeletalMeshSkinningData::Tick(float InDeltaSeconds)
{
	USkeletalMeshComponent* SkelComp = MeshComp.Get();
	check(SkelComp);
	DeltaSeconds = InDeltaSeconds;
	CurrIndex ^= 1;

	if (BoneMatrixUsers > 0)
	{
		SkelComp->CacheRefToLocalMatrices(CurrBoneRefToLocals());
	}

	//Prime the prev matrices if they're missing.
	if (PrevBoneRefToLocals().Num() != CurrBoneRefToLocals().Num() || bForceDataRefresh)
	{
		PrevBoneRefToLocals() = CurrBoneRefToLocals();
	}

	for (int32 LODIndex = 0; LODIndex < LODData.Num(); ++LODIndex)
	{
		FLODData& LOD = LODData[LODIndex];
		if (LOD.PreSkinnedVertsUsers > 0)
		{
			//TODO: If we pass the sections in the usage too, we can probably skin a minimal set of verts just for the used regions.
			FSkeletalMeshLODRenderData& SkelMeshLODData = SkelComp->SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
			FSkinWeightVertexBuffer* SkinWeightBuffer = SkelComp->GetSkinWeightBuffer(LODIndex);
			USkeletalMeshComponent::ComputeSkinnedPositions(SkelComp, CurrSkinnedPositions(LODIndex), CurrBoneRefToLocals(), SkelMeshLODData, *SkinWeightBuffer);
			//check(CurrSkinnedPositions(LODIndex).Num() == SkelMeshLODData.NumVertices);
			//Prime the previous positions if they're missing
			if (PrevSkinnedPositions(LODIndex).Num() != CurrSkinnedPositions(LODIndex).Num())
			{
				PrevSkinnedPositions(LODIndex) = CurrSkinnedPositions(LODIndex);
			}
		}
	}
	
	bForceDataRefresh = false;
	return true;
}

//////////////////////////////////////////////////////////////////////////

FSkeletalMeshSkinningDataHandle FNDI_SkeletalMesh_GeneratedData::GetCachedSkinningData(TWeakObjectPtr<USkeletalMeshComponent>& InComponent, FSkeletalMeshSkinningDataUsage Usage)
{
	FScopeLock Lock(&CriticalSection);
	
	USkeletalMeshComponent* Component = InComponent.Get();
	check(Component);
	TSharedPtr<FSkeletalMeshSkinningData> SkinningData = nullptr;

	if (TSharedPtr<FSkeletalMeshSkinningData>* Existing = CachedSkinningData.Find(Component))
	{
		check(Existing->IsValid());//We shouldn't be able to have an invalid ptr here.
		SkinningData = *Existing;
	}
	else
	{
		SkinningData = MakeShared<FSkeletalMeshSkinningData>(InComponent);
		CachedSkinningData.Add(Component) = SkinningData;
	}

	return FSkeletalMeshSkinningDataHandle(Usage, SkinningData);
}

void FNDI_SkeletalMesh_GeneratedData::TickGeneratedData(float DeltaSeconds)
{
	check(IsInGameThread());
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_PreSkin);

	//Tick skinning data.
	{
		TArray<TWeakObjectPtr<USkeletalMeshComponent>, TInlineAllocator<64>> ToRemove;
		TArray<FSkeletalMeshSkinningData*> ToTick;
		ToTick.Reserve(CachedSkinningData.Num());
		for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, TSharedPtr<FSkeletalMeshSkinningData>>& Pair : CachedSkinningData)
		{
			TSharedPtr<FSkeletalMeshSkinningData>& Ptr = Pair.Value;
			FSkeletalMeshSkinningData* SkinData = Ptr.Get();
			USkeletalMeshComponent* Component = Pair.Key.Get();
			check(SkinData);
			if (Ptr.IsUnique() || !Component || !Ptr->IsUsed())
			{
				ToRemove.Add(Pair.Key);//Remove unused skin data or for those with GCd components as we go.
			}
			else
			{
				ToTick.Add(SkinData);
			}
		}

		for (TWeakObjectPtr<USkeletalMeshComponent> Key : ToRemove)
		{
			CachedSkinningData.Remove(Key);
		}

		ParallelFor(ToTick.Num(), [&](int32 Index)
		{
			ToTick[Index]->Tick(DeltaSeconds);
		});
	}
}

//////////////////////////////////////////////////////////////////////////
// FStaticMeshGpuSpawnBuffer


FSkeletalMeshGpuSpawnStaticBuffers::~FSkeletalMeshGpuSpawnStaticBuffers()
{
	//ValidSections.Empty();
}

void FSkeletalMeshGpuSpawnStaticBuffers::Initialise(const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData,
	bool bIsGpuUniformlyDistributedSampling, const FSkeletalMeshSamplingLODBuiltData& MeshSamplingLODBuiltData)
{
	SkeletalMeshSamplingLODBuiltData = &MeshSamplingLODBuiltData;
	bUseGpuUniformlyDistributedSampling = bIsGpuUniformlyDistributedSampling;

	LODRenderData = &SkeletalMeshLODRenderData;
	TriangleCount = SkeletalMeshLODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->Num() / 3;
	check(TriangleCount > 0);
}

void FSkeletalMeshGpuSpawnStaticBuffers::InitRHI()
{
	// As of today, the UI does not allow to cull specific section of a mesh so this data could be generated on the Mesh. But Section culling might be added later?
	// Also see https://jira.it.epicgames.net/browse/UE-69376 : we would need to know if GPU sampling of the mesh surface is needed or not on the mesh to be able to do that.
	// ALso today we do not know if an interface is create from a CPU or GPU emitter. So always allocate for now. Follow up in https://jira.it.epicgames.net/browse/UE-69375.

	const FMultiSizeIndexContainer& IndexBuffer = LODRenderData->MultiSizeIndexContainer;
	MeshIndexBufferSrv = IndexBuffer.GetIndexBuffer()->GetSRV();
	MeshVertexBufferSrv = LODRenderData->StaticVertexBuffers.PositionVertexBuffer.GetSRV();

	MeshTangentBufferSRV = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
	//check(MeshTangentBufferSRV->IsValid()); // not available in this stream

	MeshTexCoordBufferSrv = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
	NumTexCoord = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

	uint32 VertexCount = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
	uint32 SectionCount = LODRenderData->RenderSections.Num();

	if (bUseGpuUniformlyDistributedSampling)
	{
		const FSkeletalMeshAreaWeightedTriangleSampler& triangleSampler = SkeletalMeshSamplingLODBuiltData->AreaWeightedTriangleSampler;
		const TArray<float>& Prob = triangleSampler.GetProb();
		const TArray<int32>& Alias = triangleSampler.GetAlias();
		check(TriangleCount == triangleSampler.GetNumEntries());

		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;
		uint32 SizeByte = TriangleCount * sizeof(float);
		BufferTriangleUniformSamplerProbaRHI = RHICreateAndLockVertexBuffer(SizeByte, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
		FMemory::Memcpy(BufferData, Prob.GetData(), SizeByte);
		RHIUnlockVertexBuffer(BufferTriangleUniformSamplerProbaRHI);
		BufferTriangleUniformSamplerProbaSRV = RHICreateShaderResourceView(BufferTriangleUniformSamplerProbaRHI, sizeof(float), PF_R32_FLOAT);

		BufferTriangleUniformSamplerAliasRHI = RHICreateAndLockVertexBuffer(SizeByte, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
		FMemory::Memcpy(BufferData, Alias.GetData(), SizeByte);
		RHIUnlockVertexBuffer(BufferTriangleUniformSamplerAliasRHI);
		BufferTriangleUniformSamplerAliasSRV = RHICreateShaderResourceView(BufferTriangleUniformSamplerAliasRHI, sizeof(uint32), PF_R32_UINT);
	}

	// Prepare the vertex matrix lookup offset for each of the sections. This is needed because per vertex BlendIndicies are stored relatively to each Section used matrices.
	// And these offset per section need to point to the correct matrix according to each section BoneMap.
	// There is not section selection/culling in the interface so technically we could compute that array in the pipeline.
	FRHIResourceCreateInfo CreateInfo;
	void* BufferData = nullptr;
	BufferTriangleMatricesOffsetRHI = RHICreateAndLockVertexBuffer(VertexCount * sizeof(uint32), BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
	uint32* MatricesOffsets = (uint32*)BufferData;
	uint32 AccumulatedMatrixOffset = 0;
	for (uint32 s = 0; s < SectionCount; ++s)
	{
		const FSkelMeshRenderSection& Section = LODRenderData->RenderSections[s];
		const uint32 SectionBaseVertexIndex = Section.BaseVertexIndex;
		const uint32 SectionNumVertices = Section.NumVertices;
		for (uint32 SectionVertex = 0; SectionVertex < SectionNumVertices; ++SectionVertex)
		{
			MatricesOffsets[SectionBaseVertexIndex + SectionVertex] = AccumulatedMatrixOffset;
		}
		AccumulatedMatrixOffset += Section.BoneMap.Num();
	}
	RHIUnlockVertexBuffer(BufferTriangleMatricesOffsetRHI);
	BufferTriangleMatricesOffsetSRV = RHICreateShaderResourceView(BufferTriangleMatricesOffsetRHI, sizeof(uint32), PF_R32_UINT);
}

void FSkeletalMeshGpuSpawnStaticBuffers::ReleaseRHI()
{
	BufferTriangleUniformSamplerProbaRHI.SafeRelease();
	BufferTriangleUniformSamplerProbaSRV.SafeRelease();
	BufferTriangleUniformSamplerAliasRHI.SafeRelease();
	BufferTriangleUniformSamplerAliasSRV.SafeRelease();
}

//////////////////////////////////////////////////////////////////////////
//FSkeletalMeshGpuSpawnProxy

FSkeletalMeshGpuDynamicBufferProxy::FSkeletalMeshGpuDynamicBufferProxy()
{
	//
}

FSkeletalMeshGpuDynamicBufferProxy::~FSkeletalMeshGpuDynamicBufferProxy()
{
	//
}

void FSkeletalMeshGpuDynamicBufferProxy::Initialise(const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData)
{
	BoneCount = 0;
	for (const FSkelMeshRenderSection& section : SkeletalMeshLODRenderData.RenderSections)
	{
		BoneCount += section.BoneMap.Num();
	}
}

void FSkeletalMeshGpuDynamicBufferProxy::InitRHI()
{
	for (FSkeletalBuffer& Buffer : RWBufferBones)
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = TEXT("SkeletalMeshGpuDynamicBuffer");
		Buffer.Buffer = RHICreateVertexBuffer(sizeof(FVector4) * 3 * BoneCount, BUF_ShaderResource | BUF_Dynamic, CreateInfo);
		Buffer.SRV = RHICreateShaderResourceView(Buffer.Buffer, sizeof(FVector4), PF_A32B32G32R32F);
	}
}

void FSkeletalMeshGpuDynamicBufferProxy::ReleaseRHI()
{
	for (FSkeletalBuffer& Buffer : RWBufferBones)
	{
		Buffer.Buffer.SafeRelease();
		Buffer.SRV.SafeRelease();
	}
}

void FSkeletalMeshGpuDynamicBufferProxy::NewFrame(const FNDISkeletalMesh_InstanceData* InstanceData, int32 LODIndex)
{
	USkeletalMeshComponent* SkelComp = InstanceData != nullptr ? Cast<USkeletalMeshComponent>(InstanceData->Component.Get()) : nullptr;
	if (SkelComp)
	{
		TArray<FMatrix> RefToLocalMatrices;
		SkelComp->CacheRefToLocalMatrices(RefToLocalMatrices);

		TIndirectArray<FSkeletalMeshLODRenderData>& LODRenderDataArray = SkelComp->SkeletalMesh->GetResourceForRendering()->LODRenderData;
		check(0 <= LODIndex  && LODIndex < LODRenderDataArray.Num());
		FSkeletalMeshLODRenderData& LODRenderData = LODRenderDataArray[LODIndex];
		TArray<FSkelMeshRenderSection>& Sections = LODRenderData.RenderSections;
		uint32 SectionCount = Sections.Num();

		TArray<FVector4> AllSectionsRefToLocalMatrices;
		static_assert(sizeof(FVector4) == 4*sizeof(float), "FVector4 should match 4 * floats");

		// Count number of matrices we want before appending all of them according to the per section mapping from BoneMap
		uint32 Float4Count = 0;
		for ( const FSkelMeshRenderSection& section : Sections )
		{
			Float4Count += section.BoneMap.Num() * 3;
		}
		check(Float4Count == 3 * BoneCount);
		AllSectionsRefToLocalMatrices.AddUninitialized(Float4Count);

		Float4Count = 0;
		for ( const FSkelMeshRenderSection& section : Sections )
		{
			const uint32 MatrixCount = section.BoneMap.Num();
			for (uint32 m = 0; m < MatrixCount; ++m)
			{
				RefToLocalMatrices[section.BoneMap[m]].To3x4MatrixTranspose(&AllSectionsRefToLocalMatrices[Float4Count].X);
				Float4Count += 3;
			}
		}

		FSkeletalMeshGpuDynamicBufferProxy* ThisProxy = this;
		ENQUEUE_RENDER_COMMAND(UpdateSpawnInfoForSkinnedMesh)(
			[ThisProxy, AllSectionsRefToLocalMatrices = MoveTemp(AllSectionsRefToLocalMatrices)](FRHICommandListImmediate& RHICmdList) mutable
		{
			ThisProxy->CurrentBoneBufferId = (ThisProxy->CurrentBoneBufferId + 1) % BufferBoneCount;
			ThisProxy->bPrevBoneGpuBufferValid = ThisProxy->bBoneGpuBufferValid;
			ThisProxy->bBoneGpuBufferValid = true;

			const uint32 NumBytes = AllSectionsRefToLocalMatrices.Num() * sizeof(FVector4);

			void* DstData = RHILockVertexBuffer(ThisProxy->GetRWBufferBone().Buffer, 0, NumBytes, RLM_WriteOnly);
			FMemory::Memcpy(DstData, AllSectionsRefToLocalMatrices.GetData(), NumBytes);
			RHIUnlockVertexBuffer(ThisProxy->GetRWBufferBone().Buffer);
		});
	}
}

//////////////////////////////////////////////////////////////////////////
//FNiagaraDataInterfaceParametersCS_SkeletalMesh

struct FNDISkeletalMeshParametersName
{
	FString MeshIndexBufferName;
	FString MeshVertexBufferName;
	FString MeshSkinWeightBufferName;
	FString MeshCurrBonesBufferName;
	FString MeshPrevBonesBufferName;
	FString MeshTangentBufferName;
	FString MeshTexCoordBufferName;
	FString MeshTriangleSamplerProbaBufferName;
	FString MeshTriangleSamplerAliasBufferName;
	FString MeshTriangleMatricesOffsetBufferName;
	FString MeshTriangleCountName;
	FString MeshWeightStrideByteName;
	FString InstanceTransformName;
	FString InstancePrevTransformName;
	FString InstanceInvDeltaTimeName;
	FString EnabledFeaturesName;
	FString InputWeightStrideName;
	FString NumTexCoordName;
};
static void GetNiagaraDataInterfaceParametersName(FNDISkeletalMeshParametersName& Names, const FString& Suffix)
{
	Names.MeshIndexBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshIndexBufferName + Suffix;
	Names.MeshVertexBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshVertexBufferName + Suffix;
	Names.MeshSkinWeightBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightBufferName + Suffix;
	Names.MeshCurrBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshCurrBonesBufferName + Suffix;
	Names.MeshPrevBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshPrevBonesBufferName + Suffix;
	Names.MeshTangentBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTangentBufferName + Suffix;
	Names.MeshTexCoordBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTexCoordBufferName + Suffix;
	Names.MeshTriangleSamplerProbaBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerProbaBufferName + Suffix;
	Names.MeshTriangleSamplerAliasBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerAliasBufferName + Suffix;
	Names.MeshTriangleMatricesOffsetBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleMatricesOffsetBufferName + Suffix;
	Names.MeshTriangleCountName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleCountName + Suffix;
	Names.MeshWeightStrideByteName = UNiagaraDataInterfaceSkeletalMesh::MeshWeightStrideByteName + Suffix;
	Names.InstanceTransformName = UNiagaraDataInterfaceSkeletalMesh::InstanceTransformName + Suffix;
	Names.InstancePrevTransformName = UNiagaraDataInterfaceSkeletalMesh::InstancePrevTransformName + Suffix;
	Names.InstanceInvDeltaTimeName = UNiagaraDataInterfaceSkeletalMesh::InstanceInvDeltaTimeName + Suffix;
	Names.EnabledFeaturesName = UNiagaraDataInterfaceSkeletalMesh::EnabledFeaturesName + Suffix;
	Names.InputWeightStrideName = UNiagaraDataInterfaceSkeletalMesh::InputWeightStrideName + Suffix;
	Names.NumTexCoordName = UNiagaraDataInterfaceSkeletalMesh::NumTexCoordName + Suffix;
}

struct FNiagaraDataInterfaceParametersCS_SkeletalMesh : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		FNDISkeletalMeshParametersName ParamNames;
		GetNiagaraDataInterfaceParametersName(ParamNames, ParamRef.ParameterInfo.DataInterfaceHLSLSymbol);

		MeshIndexBuffer.Bind(ParameterMap, *ParamNames.MeshIndexBufferName);
		MeshVertexBuffer.Bind(ParameterMap, *ParamNames.MeshVertexBufferName);
		MeshSkinWeightBuffer.Bind(ParameterMap, *ParamNames.MeshSkinWeightBufferName);
		MeshCurrBonesBuffer.Bind(ParameterMap, *ParamNames.MeshCurrBonesBufferName);
		MeshPrevBonesBuffer.Bind(ParameterMap, *ParamNames.MeshPrevBonesBufferName);
		MeshTangentBuffer.Bind(ParameterMap, *ParamNames.MeshTangentBufferName);
		MeshTexCoordBuffer.Bind(ParameterMap, *ParamNames.MeshTexCoordBufferName);
		MeshTriangleSamplerProbaBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleSamplerProbaBufferName);
		MeshTriangleSamplerAliasBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleSamplerAliasBufferName);
		MeshTriangleMatricesOffsetBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleMatricesOffsetBufferName);
		MeshTriangleCount.Bind(ParameterMap, *ParamNames.MeshTriangleCountName);
		MeshWeightStrideByte.Bind(ParameterMap, *ParamNames.MeshWeightStrideByteName);
		InstanceTransform.Bind(ParameterMap, *ParamNames.InstanceTransformName);
		InstancePrevTransform.Bind(ParameterMap, *ParamNames.InstancePrevTransformName);
		InstanceInvDeltaTime.Bind(ParameterMap, *ParamNames.InstanceInvDeltaTimeName);
		EnabledFeatures.Bind(ParameterMap, *ParamNames.EnabledFeaturesName);
		InputWeightStride.Bind(ParameterMap, *ParamNames.InputWeightStrideName);
		NumTexCoord.Bind(ParameterMap, *ParamNames.NumTexCoordName);

		if (!MeshIndexBuffer.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_StaticMesh Texture %s. Was it optimized out?"), *ParamNames.MeshIndexBufferName)
		}
		if (!MeshVertexBuffer.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_StaticMesh Sampler %s. Was it optimized out?"), *ParamNames.MeshVertexBufferName)
		}
		if (!MeshSkinWeightBuffer.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_StaticMesh Sampler %s. Was it optimized out?"), *ParamNames.MeshSkinWeightBufferName)
		}
		if (!MeshCurrBonesBuffer.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_StaticMesh Sampler %s. Was it optimized out?"), *ParamNames.MeshCurrBonesBufferName)
		}
		if (!MeshTangentBuffer.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_StaticMesh Sampler %s. Was it optimized out?"), *ParamNames.MeshTangentBufferName)
		}
		if (!MeshTriangleMatricesOffsetBuffer.IsBound())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Binding failed for FNiagaraDataInterfaceParametersCS_StaticMesh Sampler %s. Was it optimized out?"), *ParamNames.MeshTriangleMatricesOffsetBufferName)
		}
	}

	virtual void Serialize(FArchive& Ar)override
	{
		Ar << MeshIndexBuffer;
		Ar << MeshVertexBuffer;
		Ar << MeshSkinWeightBuffer;
		Ar << MeshCurrBonesBuffer;
		Ar << MeshPrevBonesBuffer;
		Ar << MeshTangentBuffer;
		Ar << MeshTexCoordBuffer;
		Ar << MeshTriangleSamplerProbaBuffer;
		Ar << MeshTriangleSamplerAliasBuffer;
		Ar << MeshTriangleMatricesOffsetBuffer;
		Ar << MeshTriangleCount;
		Ar << MeshWeightStrideByte;
		Ar << InstanceTransform;
		Ar << InstancePrevTransform;
		Ar << InstanceInvDeltaTime;
		Ar << EnabledFeatures;
		Ar << InputWeightStride;
		Ar << NumTexCoord;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FComputeShaderRHIParamRef ComputeShaderRHI = Context.Shader->GetComputeShader();
		FNiagaraDataInterfaceProxySkeletalMesh* InterfaceProxy = static_cast<FNiagaraDataInterfaceProxySkeletalMesh*>(Context.DataInterface);
		FNiagaraDataInterfaceProxySkeletalMeshData* InstanceData = InterfaceProxy->SystemInstancesToData.Find(Context.SystemInstance);
		if (InstanceData && InstanceData->StaticBuffers)
		{
			FSkeletalMeshGpuSpawnStaticBuffers* StaticBuffers = InstanceData->StaticBuffers;

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, StaticBuffers->GetBufferPositionSRV());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, StaticBuffers->GetBufferIndexSRV());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, StaticBuffers->GetBufferTangentSRV());

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumTexCoord, StaticBuffers->GetNumTexCoord());
			if (StaticBuffers->GetNumTexCoord() > 0)
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, StaticBuffers->GetBufferTexCoordSRV());
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			}
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshTriangleCount, StaticBuffers->GetTriangleCount());
			if (InstanceData->bIsGpuUniformlyDistributedSampling)
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, StaticBuffers->GetBufferTriangleUniformSamplerProbaSRV().GetReference());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, StaticBuffers->GetBufferTriangleUniformSamplerAliasSRV().GetReference());
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			}

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightBuffer, InstanceData->MeshSkinWeightBufferSrv);

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshWeightStrideByte, InstanceData->MeshWeightStrideByte);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, InstanceData->Transform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, InstanceData->PrevTransform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 1.0f / InstanceData->DeltaSeconds);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InputWeightStride, InstanceData->MeshWeightStrideByte/4);

			uint32 EnabledFeaturesBits = InstanceData->bIsGpuUniformlyDistributedSampling ? 1 : 0;

			FSkeletalMeshGpuDynamicBufferProxy* DynamicBuffers = InstanceData->DynamicBuffer;
			check(DynamicBuffers);
			if(DynamicBuffers->DoesBoneDataExist())
			{
				EnabledFeaturesBits |= 2; // Enable the skinning feature
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, DynamicBuffers->GetRWBufferBone().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, DynamicBuffers->GetRWBufferPrevBone().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, StaticBuffers->GetBufferTriangleMatricesOffsetSRV());
			}
			// Bind dummy data for validation purposes only.  Code will not execute due to "EnabledFeatures" bits but validation can not determine that.
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			}

			SetShaderValue(RHICmdList, ComputeShaderRHI, EnabledFeatures, EnabledFeaturesBits);
		}
		else
		{
			// Bind dummy buffers
			ensure(!InstanceData || InstanceData->StaticBuffers);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumTexCoord, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshTriangleCount, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshWeightStrideByte, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 0.0f);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InputWeightStride, 0);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, EnabledFeatures, 0);
		}
	}


private:

	FShaderResourceParameter MeshIndexBuffer;
	FShaderResourceParameter MeshVertexBuffer;
	FShaderResourceParameter MeshSkinWeightBuffer;
	FShaderResourceParameter MeshCurrBonesBuffer;
	FShaderResourceParameter MeshPrevBonesBuffer;
	FShaderResourceParameter MeshTangentBuffer;
	FShaderResourceParameter MeshTexCoordBuffer;
	FShaderResourceParameter MeshTriangleSamplerProbaBuffer;
	FShaderResourceParameter MeshTriangleSamplerAliasBuffer;
	FShaderResourceParameter MeshTriangleMatricesOffsetBuffer;
	FShaderParameter MeshTriangleCount;
	FShaderParameter MeshWeightStrideByte;
	FShaderParameter InstanceTransform;
	FShaderParameter InstancePrevTransform;
	FShaderParameter InstanceInvDeltaTime;
	FShaderParameter EnabledFeatures;
	FShaderParameter InputWeightStride;
	FShaderParameter NumTexCoord;
};

//////////////////////////////////////////////////////////////////////////

void FNiagaraDataInterfaceProxySkeletalMesh::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FGuid& Instance)
{
	FNiagaraDISkeletalMeshPassedDataToRT* SourceData = static_cast<FNiagaraDISkeletalMeshPassedDataToRT*>(PerInstanceData);

	FNiagaraDataInterfaceProxySkeletalMeshData& Data = SystemInstancesToData.FindOrAdd(Instance);

	Data.bIsGpuUniformlyDistributedSampling = SourceData->bIsGpuUniformlyDistributedSampling;
	Data.DeltaSeconds = SourceData->DeltaSeconds;
	Data.DynamicBuffer = SourceData->DynamicBuffer;
	Data.MeshWeightStrideByte = SourceData->MeshWeightStrideByte;
	Data.PrevTransform = SourceData->PrevTransform;
	Data.StaticBuffers = SourceData->StaticBuffers;
	Data.Transform = SourceData->Transform;

	// @todo-threadsafety race here. Need to hold a ref to this buffer on the RT
	Data.MeshSkinWeightBufferSrv = SourceData->MeshSkinWeightBufferSrv;
}

//////////////////////////////////////////////////////////////////////////
//FNDISkeletalMesh_InstanceData

void UNiagaraDataInterfaceSkeletalMesh::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FGuid& SystemInstance)
{
	FNiagaraDISkeletalMeshPassedDataToRT* Data = static_cast<FNiagaraDISkeletalMeshPassedDataToRT*>(DataForRenderThread);
	FNDISkeletalMesh_InstanceData* SourceData = static_cast<FNDISkeletalMesh_InstanceData*>(PerInstanceData);

	Data->bIsGpuUniformlyDistributedSampling = SourceData->bIsGpuUniformlyDistributedSampling;
	Data->DeltaSeconds = SourceData->DeltaSeconds;
	Data->DynamicBuffer = SourceData->MeshGpuSpawnDynamicBuffers;
	Data->MeshWeightStrideByte = SourceData->MeshWeightStrideByte;
	Data->PrevTransform = SourceData->PrevTransform;
	Data->StaticBuffers = SourceData->MeshGpuSpawnStaticBuffers;
	Data->Transform = SourceData->Transform;

	// @todo-threadsafety race here. Need to hold a ref to this buffer on the RT
	Data->MeshSkinWeightBufferSrv = SourceData->MeshSkinWeightBufferSrv;
}

USkeletalMesh* UNiagaraDataInterfaceSkeletalMesh::GetSkeletalMeshHelper(UNiagaraDataInterfaceSkeletalMesh* Interface, UNiagaraComponent* OwningComponent, TWeakObjectPtr<USceneComponent>& SceneComponent, USkeletalMeshComponent*& FoundSkelComp)
{
	USkeletalMesh* Mesh = nullptr;
	if (Interface->SourceComponent)
	{
		Mesh = Interface->SourceComponent->SkeletalMesh;
		FoundSkelComp = Interface->SourceComponent;
	}
	else if (Interface->Source)
	{
		ASkeletalMeshActor* MeshActor = Cast<ASkeletalMeshActor>(Interface->Source);
		USkeletalMeshComponent* SourceComp = nullptr;
		if (MeshActor != nullptr)
		{
			SourceComp = MeshActor->GetSkeletalMeshComponent();
		}
		else
		{
			SourceComp = Interface->Source->FindComponentByClass<USkeletalMeshComponent>();
		}

		if (SourceComp)
		{
			Mesh = SourceComp->SkeletalMesh;
			FoundSkelComp = SourceComp;
		}
		else
		{
			SceneComponent = Interface->Source->GetRootComponent();
		}
	}
	else
	{
		if (UNiagaraComponent* SimComp = OwningComponent)
		{
			if (USkeletalMeshComponent* ParentComp = Cast<USkeletalMeshComponent>(SimComp->GetAttachParent()))
			{
				FoundSkelComp = ParentComp;
				Mesh = ParentComp->SkeletalMesh;
			}
			else if (USkeletalMeshComponent* OuterComp = SimComp->GetTypedOuter<USkeletalMeshComponent>())
			{
				FoundSkelComp = OuterComp;
				Mesh = OuterComp->SkeletalMesh;
			}
			else if (AActor* Owner = SimComp->GetAttachmentRootActor())
			{
				TArray<UActorComponent*> SourceComps = Owner->GetComponentsByClass(USkeletalMeshComponent::StaticClass());
				for (UActorComponent* ActorComp : SourceComps)
				{
					USkeletalMeshComponent* SourceComp = Cast<USkeletalMeshComponent>(ActorComp);
					if (SourceComp)
					{
						USkeletalMesh* PossibleMesh = SourceComp->SkeletalMesh;
						if (PossibleMesh != nullptr/* && PossibleMesh->bAllowCPUAccess*/)
						{
							Mesh = PossibleMesh;
							FoundSkelComp = SourceComp;
							break;
						}
					}
				}
			}

			if (!SceneComponent.IsValid())
			{
				SceneComponent = SimComp;
			}
		}
	}

	if (FoundSkelComp)
	{
		SceneComponent = FoundSkelComp;
	}
	
	if (!Mesh && Interface->DefaultMesh)
	{
		Mesh = Interface->DefaultMesh;
	}

	return Mesh;
}


bool FNDISkeletalMesh_InstanceData::Init(UNiagaraDataInterfaceSkeletalMesh* Interface, FNiagaraSystemInstance* SystemInstance)
{
	check(SystemInstance);
	ChangeId = Interface->ChangeId;
	USkeletalMesh* PrevMesh = Mesh;
	Component = nullptr;
	Mesh = nullptr;
	Transform = FMatrix::Identity;
	TransformInverseTransposed = FMatrix::Identity;
	PrevTransform = FMatrix::Identity;
	PrevTransformInverseTransposed = FMatrix::Identity;
	DeltaSeconds = 0.0f;

	USkeletalMeshComponent* NewSkelComp = nullptr;
	Mesh = UNiagaraDataInterfaceSkeletalMesh::GetSkeletalMeshHelper(Interface, SystemInstance->GetComponent(), Component, NewSkelComp);
	
	MeshSafe = Mesh;

	if (Component.IsValid() && Mesh)
	{
		PrevTransform = Transform;
		PrevTransformInverseTransposed = TransformInverseTransposed;
		Transform = Component->GetComponentToWorld().ToMatrixWithScale();
		TransformInverseTransposed = Transform.InverseFast().GetTransposed();
	}

	if (!Mesh)
	{
		/*USceneComponent* Comp = Component.Get();
		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface has no valid mesh. Failed InitPerInstanceData!\nInterface: %s\nComponent: %s\nActor: %s\n")
			, *Interface->GetFullName()
			, Comp ? *Component->GetFullName() : TEXT("Null Component!")
			, Comp ? *Comp->GetOwner()->GetFullName() : TEXT("NA"));*/
		return false;
	}

#if WITH_EDITOR
	MeshSafe->GetOnMeshChanged().AddUObject(SystemInstance->GetComponent(), &UNiagaraComponent::ReinitializeSystem);
#endif


// 	if (!Mesh->bAllowCPUAccess)
// 	{
// 		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface using a mesh that does not allow CPU access. Failed InitPerInstanceData - Mesh: %s"), *Mesh->GetFullName());
// 		return false;
// 	}

	if (!Component.IsValid())
	{
		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface has no valid component. Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

	//Setup where to spawn from
	SamplingRegionIndices.Empty();
	bool bAllRegionsAreAreaWeighting = true;
	const FSkeletalMeshSamplingInfo& SamplingInfo = Mesh->GetSamplingInfo();
	int32 LODIndex = INDEX_NONE;
	if (Interface->SamplingRegions.Num() == 0)
	{
		LODIndex = Interface->WholeMeshLOD;
		//If we have no regions, sample the whole mesh at the specified LOD.
		if (LODIndex == INDEX_NONE)
		{
			LODIndex = Mesh->GetLODNum() - 1;
		}
		else
		{
			LODIndex = FMath::Clamp(Interface->WholeMeshLOD, 0, Mesh->GetLODNum() - 1);
		}

		if (!Mesh->GetLODInfo(LODIndex)->bAllowCPUAccess && (Interface->bUseTriangleSampling || Interface->bUseVertexSampling))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to spawn from a whole mesh that does not allow CPU Access.\nInterface: %s\nMesh: %s\nLOD: %d"),
				*Interface->GetFullName(),
				*Mesh->GetFullName(),
				LODIndex);

			return false;
		}
	}
	else
	{
		//Sampling from regions. Gather the indices of the regions we'll sample from.
		for (FName RegionName : Interface->SamplingRegions)
		{
			int32 RegionIdx = SamplingInfo.IndexOfRegion(RegionName);
			if (RegionIdx != INDEX_NONE)
			{
				const FSkeletalMeshSamplingRegion& Region = SamplingInfo.GetRegion(RegionIdx);
				const FSkeletalMeshSamplingRegionBuiltData& RegionBuiltData = SamplingInfo.GetRegionBuiltData(RegionIdx);
				int32 RegionLODIndex = Region.LODIndex;
				if (RegionLODIndex == INDEX_NONE)
				{
					RegionLODIndex = Mesh->GetLODInfoArray().Num() - 1;
				}
				else
				{
					RegionLODIndex = FMath::Clamp(RegionLODIndex, 0, Mesh->GetLODInfoArray().Num() - 1);
				}

				if (LODIndex == INDEX_NONE)
				{
					LODIndex = RegionLODIndex;
				}

				//ensure we don't try to use two regions from different LODs.
				if (LODIndex != RegionLODIndex)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use regions on different LODs of the mesh. This is currently unsupported.\nInterface: %s\nMesh: %s\nRegion: %s"),
						*Interface->GetFullName(),
						*Mesh->GetFullName(),
						*RegionName.ToString());

					return false;
				}

				if (RegionBuiltData.TriangleIndices.Num() > 0)
				{
					SamplingRegionIndices.Add(RegionIdx);
					bAllRegionsAreAreaWeighting &= Region.bSupportUniformlyDistributedSampling;
				}
				else
				{
					UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use a region with no associated triangles.\nLOD: %d\nInterface: %s\nMesh: %s\nRegion: %s"),
						LODIndex,
						*Interface->GetFullName(),
						*Mesh->GetFullName(),
						*RegionName.ToString());

					return false;
				}
			}
			else
			{
				UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use a region on a mesh that does not provide this region.\nInterface: %s\nMesh: %s\nRegion: %s"),
					*Interface->GetFullName(),
					*Mesh->GetFullName(),
					*RegionName.ToString());

				return false;
			}
		}
	}

	// TODO: This change is temporary to work around a crash that happens when you change the
	// source mesh on a system which is running in the level from the details panel.
	// bool bNeedDataImmediately = SystemInstance->IsSolo();
	bool bNeedDataImmediately = true;
		
	//Grab a handle to the skinning data if we have a component to skin.
	ENDISkeletalMesh_SkinningMode SkinningMode = (Interface->bUseTriangleSampling || Interface->bUseVertexSampling) ? Interface->SkinningMode : ENDISkeletalMesh_SkinningMode::None;
	FSkeletalMeshSkinningDataUsage Usage(
		LODIndex,
		SkinningMode == ENDISkeletalMesh_SkinningMode::SkinOnTheFly || SkinningMode == ENDISkeletalMesh_SkinningMode::PreSkin || Interface->bUseSkeletonSampling,
		SkinningMode == ENDISkeletalMesh_SkinningMode::PreSkin,
		bNeedDataImmediately);

	if (NewSkelComp)
	{
		SkinningMode = Interface->SkinningMode;
		TWeakObjectPtr<USkeletalMeshComponent> SkelWeakCompPtr = NewSkelComp;
		FNDI_SkeletalMesh_GeneratedData& GeneratedData = SystemInstance->GetWorldManager()->GetSkeletalMeshGeneratedData();
		SkinningData = GeneratedData.GetCachedSkinningData(SkelWeakCompPtr, Usage);
	}
	else
	{
		SkinningData = FSkeletalMeshSkinningDataHandle(Usage, nullptr);
	}

	//Init area weighting sampler for Sampling regions.
	if (SamplingRegionIndices.Num() > 1 && bAllRegionsAreAreaWeighting)
	{
		//We are sampling from multiple area weighted regions so setup the inter-region weighting sampler.
		SamplingRegionAreaWeightedSampler.Init(this);
	}

	FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
	FSkeletalMeshLODRenderData& LODData = GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	//Check for the validity of the Mesh's cpu data.
	if ( Interface->bUseTriangleSampling || Interface->bUseVertexSampling )
	{
		bool LODDataNumVerticesCorrect = LODData.GetNumVertices() > 0;
		bool LODDataPositonNumVerticesCorrect = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() > 0;
		bool bSkinWeightBuffer = SkinWeightBuffer != nullptr;
		bool SkinWeightBufferNumVerticesCorrect = bSkinWeightBuffer && (SkinWeightBuffer->GetNumVertices() > 0);
		bool bIndexBufferValid = LODData.MultiSizeIndexContainer.IsIndexBufferValid();
		bool bIndexBufferFound = bIndexBufferValid && (LODData.MultiSizeIndexContainer.GetIndexBuffer() != nullptr);
		bool bIndexBufferNumCorrect = bIndexBufferFound && (LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num() > 0);

		bool bMeshCPUDataValid = LODDataNumVerticesCorrect &&
			LODDataPositonNumVerticesCorrect &&
			bSkinWeightBuffer &&
			SkinWeightBufferNumVerticesCorrect &&
			bIndexBufferValid &&
			bIndexBufferFound &&
			bIndexBufferNumCorrect;

		if (!bMeshCPUDataValid)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to sample from a mesh with missing CPU vertex or index data.\nInterface: %s\nMesh: %s\nLOD: %d\n"
				"LODDataNumVerticesCorrect: %d  LODDataPositonNumVerticesCorrect : %d  bSkinWeightBuffer : %d  SkinWeightBufferNumVerticesCorrect : %d bIndexBufferValid : %d  bIndexBufferFound : %d  bIndexBufferNumCorrect : %d"),
				*Interface->GetFullName(),
				*Mesh->GetFullName(),
				LODIndex,
				LODDataNumVerticesCorrect ? 1 : 0,
				LODDataPositonNumVerticesCorrect ? 1 : 0,
				bSkinWeightBuffer ? 1 : 0,
				SkinWeightBufferNumVerticesCorrect ? 1 : 0,
				bIndexBufferValid ? 1 : 0,
				bIndexBufferFound ? 1 : 0,
				bIndexBufferNumCorrect ? 1 : 0
			);

			return false;
		}
	}

	FReferenceSkeleton& RefSkel = Mesh->RefSkeleton;
	SpecificBones.SetNumUninitialized(Interface->SpecificBones.Num());
	TArray<FName, TInlineAllocator<16>> MissingBones;
	for (int32 BoneIdx = 0; BoneIdx < SpecificBones.Num(); ++BoneIdx)
	{
		FName BoneName = Interface->SpecificBones[BoneIdx];
		int32 Bone = RefSkel.FindBoneIndex(BoneName);
		if (Bone == INDEX_NONE)
		{
			MissingBones.Add(BoneName);
			SpecificBones[BoneIdx] = 0;
		}
		else
		{
			SpecificBones[BoneIdx] = Bone;
		}
	}

	if (MissingBones.Num() > 0)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to sample from bones that don't exist in it's skeleton.\nMesh: %s\nBones: "), *Mesh->GetName());
		for (FName BoneName : MissingBones)
		{
			UE_LOG(LogNiagara, Warning, TEXT("%s\n"), *BoneName.ToString());
		}
	}

	SpecificSockets.SetNumUninitialized(Interface->SpecificSockets.Num());
	SpecificSocketBones.SetNumUninitialized(Interface->SpecificSockets.Num());
	TArray<FName, TInlineAllocator<16>> MissingSockets;
	for (int32 SocketIdx = 0; SocketIdx < SpecificSockets.Num(); ++SocketIdx)
	{
		FName SocketName = Interface->SpecificSockets[SocketIdx];
		int32 SocketIndex = INDEX_NONE;
		USkeletalMeshSocket* Socket = Mesh->FindSocketAndIndex(SocketName, SocketIndex);
		if (SocketIndex == INDEX_NONE)
		{
			MissingSockets.Add(SocketName);
			SpecificSockets[SocketIdx] = 0;
			SpecificSocketBones[SocketIdx] = 0;
		}
		else
		{
			check(Socket);
			SpecificSockets[SocketIdx] = SocketIndex;
			SpecificSocketBones[SocketIdx] = RefSkel.FindBoneIndex(Socket->BoneName);
		}
	}

	if (MissingSockets.Num() > 0)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to sample from sockets that don't exist in it's skeleton.\nMesh: %s\nSockets: "), *Mesh->GetName());
		for (FName SocketName : MissingSockets)
		{
			UE_LOG(LogNiagara, Warning, TEXT("%s\n"), *SocketName.ToString());
		}
	}

	{
		MeshWeightStrideByte = SkinWeightBuffer->GetStride();
		MeshSkinWeightBufferSrv = SkinWeightBuffer->GetSRV();
		//check(MeshSkinWeightBufferSrv->IsValid()); // not available in this stream

		FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
		bIsGpuUniformlyDistributedSampling = LODInfo->bSupportUniformlyDistributedSampling && bAllRegionsAreAreaWeighting;

		if (Mesh->HasActiveClothingAssets())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh %s has cloth asset on it: spawning from it might not work properly."), *Mesh->GetName());
		}
		if (LODData.DoesVertexBufferHaveExtraBoneInfluences())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh %s has bones extra influence: spawning from it might not work properly."), *Mesh->GetName());
		}

		MeshGpuSpawnStaticBuffers = new FSkeletalMeshGpuSpawnStaticBuffers();
		MeshGpuSpawnStaticBuffers->Initialise(LODData, bIsGpuUniformlyDistributedSampling, SamplingInfo.GetBuiltData().WholeMeshBuiltData[LODIndex]);
		BeginInitResource(MeshGpuSpawnStaticBuffers);

		MeshGpuSpawnDynamicBuffers = new FSkeletalMeshGpuDynamicBufferProxy();
		MeshGpuSpawnDynamicBuffers->Initialise(LODData);
		BeginInitResource(MeshGpuSpawnDynamicBuffers);
	}

	return true;
}

bool FNDISkeletalMesh_InstanceData::ResetRequired(UNiagaraDataInterfaceSkeletalMesh* Interface)const
{
	USceneComponent* Comp = Component.Get();
	if (!Comp)
	{
		//The component we were bound to is no longer valid so we have to trigger a reset.
		return true;
	}
		
	if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Comp))
	{
		if (!SkelComp->SkeletalMesh)
		{
			return true;
		}
		
		// Handle the case where they've procedurally swapped out the skeletal mesh from
		// the one we previously cached data for.
		if (SkelComp->SkeletalMesh != Mesh && Mesh != nullptr && SkelComp->SkeletalMesh != nullptr)
		{
			if (SkinningData.SkinningData.IsValid())
			{
				SkinningData.SkinningData.Get()->ForceDataRefresh();
			}
			return true;
		}
	}
	else
	{
		if (!Interface->DefaultMesh)
		{
			return true;
		}
	}

	if (Interface->ChangeId != ChangeId)
	{
		return true;
	}

	
	return false;
}

bool FNDISkeletalMesh_InstanceData::Tick(UNiagaraDataInterfaceSkeletalMesh* Interface, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	if (ResetRequired(Interface))
	{
		return true;
	}
	else
	{
		DeltaSeconds = InDeltaSeconds;
		if (Component.IsValid() && Mesh)
		{
			PrevTransform = Transform;
			PrevTransformInverseTransposed = TransformInverseTransposed;
			Transform = Component->GetComponentToWorld().ToMatrixWithScale();
			TransformInverseTransposed = Transform.InverseFast().GetTransposed();
		}
		else
		{
			PrevTransform = FMatrix::Identity;
			PrevTransformInverseTransposed = FMatrix::Identity;
			Transform = FMatrix::Identity;
			TransformInverseTransposed = FMatrix::Identity;
		}

		if (MeshGpuSpawnDynamicBuffers)
		{
			USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(Component.Get());
			const USkinnedMeshComponent* BaseComp = nullptr;
			if (Comp)
			{
				BaseComp = Comp->GetBaseComponent();
			}

			MeshGpuSpawnDynamicBuffers->NewFrame(this, GetLODIndex());
		}

		return false;
	}
}

bool FNDISkeletalMesh_InstanceData::HasColorData()
{
	check(Mesh);
	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData& LODData = GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	return LODData.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() != 0;
}

void FNDISkeletalMesh_InstanceData::Release()
{
	if (MeshGpuSpawnStaticBuffers)
	{
		BeginReleaseResource(MeshGpuSpawnStaticBuffers);
		ENQUEUE_RENDER_COMMAND(DeleteResource)(
			[ParamPointerToRelease = MeshGpuSpawnStaticBuffers](FRHICommandListImmediate& RHICmdList)
			{
				delete ParamPointerToRelease;
			});
		MeshGpuSpawnStaticBuffers = nullptr;
	}
	if (MeshGpuSpawnDynamicBuffers)
	{
		BeginReleaseResource(MeshGpuSpawnDynamicBuffers);
		ENQUEUE_RENDER_COMMAND(DeleteResource)(
			[ParamPointerToRelease = MeshGpuSpawnDynamicBuffers](FRHICommandListImmediate& RHICmdList)
			{
				delete ParamPointerToRelease;
			});
		MeshGpuSpawnDynamicBuffers = nullptr;
	}
}

//Instance Data END
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
// UNiagaraDataInterfaceSkeletalMesh

UNiagaraDataInterfaceSkeletalMesh::UNiagaraDataInterfaceSkeletalMesh(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultMesh(nullptr)
	, Source(nullptr)
	, SkinningMode(ENDISkeletalMesh_SkinningMode::SkinOnTheFly)
	, WholeMeshLOD(INDEX_NONE)
	, bUseTriangleSampling(true)
	, bUseVertexSampling(true)
	, bUseSkeletonSampling(true)
	, ChangeId(0)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxySkeletalMesh, ESPMode::ThreadSafe>();
}


void UNiagaraDataInterfaceSkeletalMesh::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);

		//Still some issues with using custom structs. Convert node for example throws a wobbler. TODO after GDC.
		FNiagaraTypeRegistry::Register(FMeshTriCoordinate::StaticStruct(), true, true, false);
	}
}

#if WITH_EDITOR
void UNiagaraDataInterfaceSkeletalMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// If the change comes from an interaction (and not just a generic change) reset the usage flags.
	// todo : this and the usage binding need to be done in the a precompilation parsing (or whever the script is compiled).
	if (PropertyChangedEvent.Property)
	{
		bUseTriangleSampling = false;
		bUseVertexSampling = false;
		bUseSkeletonSampling = false;
	}
	ChangeId++;
}

#endif //WITH_EDITOR


void UNiagaraDataInterfaceSkeletalMesh::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	GetTriangleSamplingFunctions(OutFunctions);
	GetVertexSamplingFunctions(OutFunctions);
	GetSkeletonSamplingFunctions(OutFunctions);
}

void UNiagaraDataInterfaceSkeletalMesh::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
	USkeletalMeshComponent* SkelComp = InstData != nullptr ? Cast<USkeletalMeshComponent>(InstData->Component.Get()) : nullptr;
	
	if (!InstData || !InstData->Mesh)
	{
		OutFunc = FVMExternalFunction();
		return;
	}

	BindTriangleSamplingFunction(BindingInfo, InstData, OutFunc);

	if (OutFunc.IsBound())
	{
#if WITH_EDITOR
		if (!bUseTriangleSampling)
		{
			bUseTriangleSampling = true;
			MarkPackageDirty();
		}
#endif // WITH_EDITOR
		return;
	}

	BindVertexSamplingFunction(BindingInfo, InstData, OutFunc);

	if (OutFunc.IsBound())
	{
#if WITH_EDITOR
		if (!bUseVertexSampling)
		{
			bUseVertexSampling = true;
			MarkPackageDirty();
		}
#endif // WITH_EDITOR
		return;
	}

	BindSkeletonSamplingFunction(BindingInfo, InstData, OutFunc);

#if WITH_EDITOR
	if (OutFunc.IsBound())
	{
		if (!bUseSkeletonSampling)
		{
			bUseSkeletonSampling = true;
			MarkPackageDirty();
		}
	}
#endif // WITH_EDITOR
}


bool UNiagaraDataInterfaceSkeletalMesh::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceSkeletalMesh* OtherTyped = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Destination);
	OtherTyped->Source = Source;
	OtherTyped->DefaultMesh = DefaultMesh;
	OtherTyped->SkinningMode = SkinningMode;
	OtherTyped->SamplingRegions = SamplingRegions;
	OtherTyped->WholeMeshLOD = WholeMeshLOD;
	OtherTyped->SpecificBones = SpecificBones;
	OtherTyped->SpecificSockets = SpecificSockets;
	OtherTyped->bUseTriangleSampling = bUseTriangleSampling;
	OtherTyped->bUseVertexSampling = bUseVertexSampling;
	OtherTyped->bUseSkeletonSampling = bUseSkeletonSampling;
	return true;
}

bool UNiagaraDataInterfaceSkeletalMesh::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceSkeletalMesh* OtherTyped = CastChecked<const UNiagaraDataInterfaceSkeletalMesh>(Other);
	return OtherTyped->Source == Source &&
		OtherTyped->DefaultMesh == DefaultMesh &&
		OtherTyped->SkinningMode == SkinningMode &&
		OtherTyped->SamplingRegions == SamplingRegions &&
		OtherTyped->WholeMeshLOD == WholeMeshLOD &&
		OtherTyped->SpecificBones == SpecificBones &&
		OtherTyped->SpecificSockets == SpecificSockets;
}

bool UNiagaraDataInterfaceSkeletalMesh::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDISkeletalMesh_InstanceData* Inst = new (PerInstanceData) FNDISkeletalMesh_InstanceData();
	check(IsAligned(PerInstanceData, 16));
	return Inst->Init(this, SystemInstance);
}

void UNiagaraDataInterfaceSkeletalMesh::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDISkeletalMesh_InstanceData* Inst = (FNDISkeletalMesh_InstanceData*)PerInstanceData;

#if WITH_EDITOR
	if(Inst->MeshSafe.IsValid())
	{
		Inst->MeshSafe.Get()->GetOnMeshChanged().RemoveAll(SystemInstance->GetComponent());
	}
#endif

	Inst->Release();
	Inst->~FNDISkeletalMesh_InstanceData();

	{
		// @todo this races
		FNiagaraDataInterfaceProxySkeletalMesh* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxySkeletalMesh>();
		ENQUEUE_RENDER_COMMAND(FNiagaraDestroySkeletalMeshInstanceData) (
			[ThisProxy, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& CmdList)
			{
				ThisProxy->SystemInstancesToData.Remove(InstanceID);
			}
		);
	}
}

bool UNiagaraDataInterfaceSkeletalMesh::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDISkeletalMesh_InstanceData* Inst = (FNDISkeletalMesh_InstanceData*)PerInstanceData;
	return Inst->Tick(this, SystemInstance, InDeltaSeconds);
}

#if WITH_EDITOR	
TArray<FNiagaraDataInterfaceError> UNiagaraDataInterfaceSkeletalMesh::GetErrors()
{
	TArray<FNiagaraDataInterfaceError> Errors;
	bool bHasCPUAccessError= false;
	bool bHasNoMeshAssignedError = false;
	
	// Collect Errors
	if (DefaultMesh != nullptr && (bUseTriangleSampling || bUseVertexSampling))
	{
		for (auto info : DefaultMesh->GetLODInfoArray())
		{
			if (!info.bAllowCPUAccess)
				bHasCPUAccessError = true;
		}
	}
	else
	{
		bHasNoMeshAssignedError = true;
	}

	// Report Errors
	if (Source == nullptr && bHasCPUAccessError)
	{
		FNiagaraDataInterfaceError CPUAccessNotAllowedError(FText::Format(LOCTEXT("CPUAccessNotAllowedError", "This mesh needs CPU access in order to be used properly.({0})"), FText::FromString(DefaultMesh->GetName())),
			LOCTEXT("CPUAccessNotAllowedErrorSummary", "CPU access error"),
			FNiagaraDataInterfaceFix::CreateLambda([=]()
		{
			DefaultMesh->Modify();
			for (int i = 0; i < DefaultMesh->GetLODInfoArray().Num(); i++)
			{
				FSkeletalMeshLODInfo* info = &DefaultMesh->GetLODInfoArray()[i];
				DefaultMesh->Modify();
				info->bAllowCPUAccess = true;
			}
			return true;
		}));

		Errors.Add(CPUAccessNotAllowedError);
	}

	if (Source == nullptr && bHasNoMeshAssignedError)
	{
		FNiagaraDataInterfaceError NoMeshAssignedError(LOCTEXT("NoMeshAssignedError", "This Data Interface must be assigned a skeletal mesh to operate."),
			LOCTEXT("NoMeshAssignedErrorSummary", "No mesh assigned error"),
			FNiagaraDataInterfaceFix());

		Errors.Add(NoMeshAssignedError);
	}

	return Errors;
}

//Deprecated functions we check for and advise on updates in ValidateFunction
static const FName GetTriPositionName_DEPRECATED("GetTriPosition");
static const FName GetTriPositionWSName_DEPRECATED("GetTriPositionWS");
static const FName GetTriNormalName_DEPRECATED("GetTriNormal");
static const FName GetTriNormalWSName_DEPRECATED("GetTriNormalWS");
static const FName GetTriPositionVelocityAndNormalName_DEPRECATED("GetTriPositionVelocityAndNormal");
static const FName GetTriPositionVelocityAndNormalWSName_DEPRECATED("GetTriPositionVelocityAndNormalWS");
static const FName GetTriPositionVelocityAndNormalBinormalTangentName_DEPRECATED("GetTriPositionVelocityAndNormalBinormalTangent");
static const FName GetTriPositionVelocityAndNormalBinormalTangentWSName_DEPRECATED("GetTriPositionVelocityAndNormalBinormalTangentWS");

void UNiagaraDataInterfaceSkeletalMesh::ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors)
{
	TArray<FNiagaraFunctionSignature> DIFuncs;
	GetFunctions(DIFuncs);

	if (!DIFuncs.Contains(Function))
	{
		TArray<FNiagaraFunctionSignature> SkinnedDataDeprecatedFunctions;

		{
			FNiagaraFunctionSignature Sig;
			Sig.Name = GetTriPositionName_DEPRECATED;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			SkinnedDataDeprecatedFunctions.Add(Sig);
		}

		{
			FNiagaraFunctionSignature Sig;
			Sig.Name = GetTriPositionWSName_DEPRECATED;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			SkinnedDataDeprecatedFunctions.Add(Sig);
		}

		{
			FNiagaraFunctionSignature Sig;
			Sig.Name = GetTriPositionVelocityAndNormalName_DEPRECATED;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			SkinnedDataDeprecatedFunctions.Add(Sig);
		}

		{
			FNiagaraFunctionSignature Sig;
			Sig.Name = GetTriPositionVelocityAndNormalWSName_DEPRECATED;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			SkinnedDataDeprecatedFunctions.Add(Sig);
		}

		{
			FNiagaraFunctionSignature Sig;
			Sig.Name = GetTriPositionVelocityAndNormalBinormalTangentName_DEPRECATED;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			SkinnedDataDeprecatedFunctions.Add(Sig);
		}

		{
			FNiagaraFunctionSignature Sig;
			Sig.Name = GetTriPositionVelocityAndNormalBinormalTangentWSName_DEPRECATED;
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("SkeletalMesh")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FMeshTriCoordinate::StaticStruct()), TEXT("Coord")));
			Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("UV Set")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
			Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("UV")));
			Sig.bMemberFunction = true;
			Sig.bRequiresContext = false;
			SkinnedDataDeprecatedFunctions.Add(Sig);
		}

		if (SkinnedDataDeprecatedFunctions.Contains(Function))
		{
			OutValidationErrors.Add(FText::Format(LOCTEXT("SkinnedDataFunctionDeprecationMsgFmt", "Skeletal Mesh DI Function {0} has been deprecated. Use GetSinnedTriangleData or GetSkinnedTriangleDataWS instead.\n"), FText::FromString(Function.GetName())));
		}
		else
		{
			Super::ValidateFunction(Function, OutValidationErrors);
		}
	}
}

#endif

const FString UNiagaraDataInterfaceSkeletalMesh::MeshIndexBufferName(TEXT("IndexBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshVertexBufferName(TEXT("VertexBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightBufferName(TEXT("VertexSkinWeightBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshCurrBonesBufferName(TEXT("MeshCurrBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshPrevBonesBufferName(TEXT("MeshPrevBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTangentBufferName(TEXT("MeshTangentBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTexCoordBufferName(TEXT("TexCoordBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerProbaBufferName(TEXT("MeshTriangleSamplerProbaBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerAliasBufferName(TEXT("MeshTriangleSamplerAliasBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleMatricesOffsetBufferName(TEXT("MeshTriangleMatricesOffsetBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstanceTransformName(TEXT("InstanceTransform_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstancePrevTransformName(TEXT("InstancePrevTransform_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstanceInvDeltaTimeName(TEXT("InstanceInvDeltaTime_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshWeightStrideByteName(TEXT("MeshWeightStrideByte_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleCountName(TEXT("MeshTriangleCount_"));
const FString UNiagaraDataInterfaceSkeletalMesh::EnabledFeaturesName(TEXT("EnabledFeatures_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InputWeightStrideName(TEXT("InputWeightStride_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumTexCoordName(TEXT("NumTexCoordName_"));

bool UNiagaraDataInterfaceSkeletalMesh::GetFunctionHLSL(const FName&  DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) 
{
	FNDISkeletalMeshParametersName ParamNames;
	GetNiagaraDataInterfaceParametersName(ParamNames, ParamInfo.DataInterfaceHLSLSymbol);
	FString MeshTriCoordinateStructName = "MeshTriCoordinate";

	static const TCHAR* FormatCommonFunctions = TEXT(R"(
		void {InstanceFunctionName}_GetIndicesAndWeights(uint VertexIndex, out int4 BlendIndices, out float4 BlendWeights)
		{
			uint PackedBlendIndices = {MeshSkinWeightBufferName}[VertexIndex * ({InputWeightStrideName})    ];
			uint PackedBlendWeights = {MeshSkinWeightBufferName}[VertexIndex * ({InputWeightStrideName}) + 1];
			BlendIndices.x = PackedBlendIndices & 0xff;
			BlendIndices.y = PackedBlendIndices >> 8 & 0xff;
			BlendIndices.z = PackedBlendIndices >> 16 & 0xff;
			BlendIndices.w = PackedBlendIndices >> 24 & 0xff;
			BlendWeights.x = float(PackedBlendWeights & 0xff) / 255.0f;
			BlendWeights.y = float(PackedBlendWeights >> 8 & 0xff) / 255.0f;
			BlendWeights.z = float(PackedBlendWeights >> 16 & 0xff) / 255.0f;
			BlendWeights.w = float(PackedBlendWeights >> 24 & 0xff) / 255.0f;
		}

		float3x4 {InstanceFunctionName}_GetPrevBoneMatrix(uint Bone)
		{
			return float3x4({MeshPrevBonesBufferName}[Bone * 3], {MeshPrevBonesBufferName}[Bone * 3 + 1], {MeshPrevBonesBufferName}[Bone * 3 + 2]);
		}

		float3x4 {InstanceFunctionName}_GetPrevSkinningMatrix(uint VertexIndex, int4 BlendIndices, float4 BlendWeights)
		{
			// Get the matrix offset for each vertex because BlendIndices are stored relatively to each section start vertex.
			uint MatrixOffset = {MeshTriangleMatricesOffsetBufferName}[VertexIndex];

			float3x4 Result;
			Result  = {InstanceFunctionName}_GetPrevBoneMatrix(MatrixOffset + BlendIndices.x) * BlendWeights.x;
			Result += {InstanceFunctionName}_GetPrevBoneMatrix(MatrixOffset + BlendIndices.y) * BlendWeights.y;
			Result += {InstanceFunctionName}_GetPrevBoneMatrix(MatrixOffset + BlendIndices.z) * BlendWeights.z;
			Result += {InstanceFunctionName}_GetPrevBoneMatrix(MatrixOffset + BlendIndices.w) * BlendWeights.w;
			return Result;
		}

		float3x4 {InstanceFunctionName}_GetCurrBoneMatrix(uint Bone)
		{
			return float3x4({MeshCurrBonesBufferName}[Bone * 3], {MeshCurrBonesBufferName}[Bone * 3 + 1], {MeshCurrBonesBufferName}[Bone * 3 + 2]);
		}

		float3x4 {InstanceFunctionName}_GetCurrSkinningMatrix(uint VertexIndex, int4 BlendIndices, float4 BlendWeights)
		{
			// Get the matrix offset for each vertex because BlendIndices are stored relatively to each section start vertex.
			uint MatrixOffset = {MeshTriangleMatricesOffsetBufferName}[VertexIndex];

			float3x4 Result;
			Result  = {InstanceFunctionName}_GetCurrBoneMatrix(MatrixOffset + BlendIndices.x) * BlendWeights.x;
			Result += {InstanceFunctionName}_GetCurrBoneMatrix(MatrixOffset + BlendIndices.y) * BlendWeights.y;
			Result += {InstanceFunctionName}_GetCurrBoneMatrix(MatrixOffset + BlendIndices.z) * BlendWeights.z;
			Result += {InstanceFunctionName}_GetCurrBoneMatrix(MatrixOffset + BlendIndices.w) * BlendWeights.w;
			return Result;
		}
	)");

	static const TCHAR* FormatSampleSkinnedTriangleDataWSHeader = TEXT(R"(
		void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float3 Out_Position, out float3 Out_Velocity, out float3 Out_Normal, out float3 Out_Binormal, out float3 Out_Tangent)
		{
			const float In_Interp = 1.0f;
		)");

	static const TCHAR* FormatSampleSkinnedTriangleDataWSInterpolatedHeader = TEXT(R"(
		void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, float In_Interp, out float3 Out_Position, out float3 Out_Velocity, out float3 Out_Normal, out float3 Out_Binormal, out float3 Out_Tangent)
		{
		)");

	static const TCHAR* FormatSampleSkinnedTriangleDataWSPart0 = TEXT(R"(
			const bool SkinningEnabled = {EnabledFeaturesName} & 0x0002;

			uint TriangleIndex = In_Coord.Tri * 3;
			uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ];
			uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1];
			uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2];
			
			// I could not find a R32G32B32f format to create an SRV on that buffer. So float load it is for now...
			float3 Vertex0 = float3({MeshVertexBufferName}[VertexIndex0*3], {MeshVertexBufferName}[VertexIndex0*3+1], {MeshVertexBufferName}[VertexIndex0*3+2]);
			float3 Vertex1 = float3({MeshVertexBufferName}[VertexIndex1*3], {MeshVertexBufferName}[VertexIndex1*3+1], {MeshVertexBufferName}[VertexIndex1*3+2]);
			float3 Vertex2 = float3({MeshVertexBufferName}[VertexIndex2*3], {MeshVertexBufferName}[VertexIndex2*3+1], {MeshVertexBufferName}[VertexIndex2*3+2]);
			float3 PrevVertex0 = Vertex0;
			float3 PrevVertex1 = Vertex1;
			float3 PrevVertex2 = Vertex2;

			float3 TangentX0 = TangentBias({MeshTangentBufferName}[VertexIndex0*2  ].xyz);
			float4 TangentZ0 = TangentBias({MeshTangentBufferName}[VertexIndex0*2+1].xyzw);
			float3 TangentX1 = TangentBias({MeshTangentBufferName}[VertexIndex1*2  ].xyz);
			float4 TangentZ1 = TangentBias({MeshTangentBufferName}[VertexIndex1*2+1].xyzw);
			float3 TangentX2 = TangentBias({MeshTangentBufferName}[VertexIndex2*2  ].xyz);
			float4 TangentZ2 = TangentBias({MeshTangentBufferName}[VertexIndex2*2+1].xyzw);

			if(SkinningEnabled)
			{
				int4 BlendIndices0;
				int4 BlendIndices1;
				int4 BlendIndices2;
				float4 BlendWeights0;
				float4 BlendWeights1;
				float4 BlendWeights2;

				{InstanceFunctionName}_GetIndicesAndWeights(VertexIndex0, BlendIndices0, BlendWeights0);
				{InstanceFunctionName}_GetIndicesAndWeights(VertexIndex1, BlendIndices1, BlendWeights1);
				{InstanceFunctionName}_GetIndicesAndWeights(VertexIndex2, BlendIndices2, BlendWeights2);

				float3x4 PrevBoneMatrix0 = {InstanceFunctionName}_GetPrevSkinningMatrix(VertexIndex0, BlendIndices0, BlendWeights0);
				float3x4 PrevBoneMatrix1 = {InstanceFunctionName}_GetPrevSkinningMatrix(VertexIndex1, BlendIndices1, BlendWeights1);
				float3x4 PrevBoneMatrix2 = {InstanceFunctionName}_GetPrevSkinningMatrix(VertexIndex2, BlendIndices2, BlendWeights2);
				PrevVertex0 = mul( PrevBoneMatrix0, float4(Vertex0, 1.0f) ).xyz;
				PrevVertex1 = mul( PrevBoneMatrix1, float4(Vertex1, 1.0f) ).xyz;
				PrevVertex2 = mul( PrevBoneMatrix2, float4(Vertex2, 1.0f) ).xyz;

				float3x4 CurrBoneMatrix0 = {InstanceFunctionName}_GetCurrSkinningMatrix(VertexIndex0, BlendIndices0, BlendWeights0);
				float3x4 CurrBoneMatrix1 = {InstanceFunctionName}_GetCurrSkinningMatrix(VertexIndex1, BlendIndices1, BlendWeights1);
				float3x4 CurrBoneMatrix2 = {InstanceFunctionName}_GetCurrSkinningMatrix(VertexIndex2, BlendIndices2, BlendWeights2);
				Vertex0 = mul( CurrBoneMatrix0, float4(Vertex0, 1.0f) ).xyz;
				Vertex1 = mul( CurrBoneMatrix1, float4(Vertex1, 1.0f) ).xyz;
				Vertex2 = mul( CurrBoneMatrix2, float4(Vertex2, 1.0f) ).xyz;

				// Not using InverseTranspose of matrices so assuming uniform scaling only (same as SkinCache)
				TangentX0.xyz = mul( CurrBoneMatrix0, float4(TangentX0.xyz, 0.0f) ).xyz;
				TangentZ0.xyz = mul( CurrBoneMatrix0, float4(TangentZ0.xyz, 0.0f) ).xyz;
				TangentX1.xyz = mul( CurrBoneMatrix1, float4(TangentX1.xyz, 0.0f) ).xyz;
				TangentZ1.xyz = mul( CurrBoneMatrix1, float4(TangentZ1.xyz, 0.0f) ).xyz;
				TangentX2.xyz = mul( CurrBoneMatrix2, float4(TangentX2.xyz, 0.0f) ).xyz;
				TangentZ2.xyz = mul( CurrBoneMatrix2, float4(TangentZ2.xyz, 0.0f) ).xyz;
			}

			// Evaluate current and previous world position
			float3 WSPos = Vertex0 * In_Coord.BaryCoord.x + Vertex1 * In_Coord.BaryCoord.y + Vertex2 * In_Coord.BaryCoord.z;
			WSPos = mul(float4(WSPos,1.0), {InstanceTransformName}).xyz;
			float3 PrevWSPos = PrevVertex0 * In_Coord.BaryCoord.x + PrevVertex1 * In_Coord.BaryCoord.y + PrevVertex2 * In_Coord.BaryCoord.z;
			PrevWSPos = mul(float4(PrevWSPos,1.0), {InstancePrevTransformName}).xyz;

			// Not using InverseTranspose of matrices so assuming uniform scaling only (same as SkinCache)
			float3 Binormal0 = cross(TangentZ0.xyz, TangentX0.xyz) * TangentZ0.w;
			float3 Binormal1 = cross(TangentZ1.xyz, TangentX1.xyz) * TangentZ1.w;
			float3 Binormal2 = cross(TangentZ2.xyz, TangentX2.xyz) * TangentZ2.w;
			float3 Normal   = TangentZ0.xyz * In_Coord.BaryCoord.x + TangentZ1.xyz * In_Coord.BaryCoord.y + TangentZ2.xyz * In_Coord.BaryCoord.z; // Normal is TangentZ
			float3 Tangent  = TangentX0.xyz * In_Coord.BaryCoord.x + TangentX1.xyz * In_Coord.BaryCoord.y + TangentX2.xyz * In_Coord.BaryCoord.z;
			float3 Binormal = Binormal0.xyz * In_Coord.BaryCoord.x + Binormal1.xyz * In_Coord.BaryCoord.y + Binormal2.xyz * In_Coord.BaryCoord.z;
			float3 NormalWorld   = mul(float4(Normal  , 0.0), {InstanceTransformName}).xyz;
			float3 TangentWorld  = mul(float4(Tangent , 0.0), {InstanceTransformName}).xyz;
			float3 BinormalWorld = mul(float4(Binormal, 0.0), {InstanceTransformName}).xyz;
			
			Out_Position = lerp(PrevWSPos, WSPos, float3(In_Interp,In_Interp,In_Interp));
			Out_Velocity = (WSPos - PrevWSPos) * {InstanceInvDeltaTimeName};				// Velocity is unafected by spawn interpolation. That would require another set of previous data.
			Out_Normal   = normalize(NormalWorld);
			Out_Tangent  = normalize(TangentWorld);
			Out_Binormal = normalize(BinormalWorld);
		}
		)");


	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), InstanceFunctionName},
		{TEXT("MeshTriCoordinateStructName"), MeshTriCoordinateStructName},
		{TEXT("MeshIndexBufferName"), ParamNames.MeshIndexBufferName},
		{TEXT("MeshVertexBufferName"), ParamNames.MeshVertexBufferName},
		{TEXT("MeshSkinWeightBufferName"), ParamNames.MeshSkinWeightBufferName},
		{TEXT("MeshCurrBonesBufferName"), ParamNames.MeshCurrBonesBufferName},
		{TEXT("MeshPrevBonesBufferName"), ParamNames.MeshPrevBonesBufferName},
		{TEXT("MeshTangentBufferName"), ParamNames.MeshTangentBufferName},
		{TEXT("MeshTexCoordBufferName"), ParamNames.MeshTexCoordBufferName},
		{TEXT("MeshTriangleSamplerProbaBufferName"), ParamNames.MeshTriangleSamplerProbaBufferName},
		{TEXT("MeshTriangleSamplerAliasBufferName"), ParamNames.MeshTriangleSamplerAliasBufferName},
		{TEXT("MeshTriangleMatricesOffsetBufferName"), ParamNames.MeshTriangleMatricesOffsetBufferName},
		{TEXT("MeshTriangleCountName"), ParamNames.MeshTriangleCountName},
		{TEXT("InstanceTransformName"), ParamNames.InstanceTransformName},
		{TEXT("InstancePrevTransformName"), ParamNames.InstancePrevTransformName},
		{TEXT("InstanceInvDeltaTimeName"), ParamNames.InstanceInvDeltaTimeName},
		{TEXT("EnabledFeaturesName"), ParamNames.EnabledFeaturesName},
		{TEXT("InputWeightStrideName"), ParamNames.InputWeightStrideName},
		{TEXT("NumTexCoordName"), ParamNames.NumTexCoordName},
	};

	if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::RandomTriCoordName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out {MeshTriCoordinateStructName} Out_Coord)
			{
				const bool UniformTriangleSamplingEnable = {EnabledFeaturesName} & 0x0001;

				float RandT0 = NiagaraInternalNoise(1, 2, 3);
				[branch]
				if (!UniformTriangleSamplingEnable)
				{
					// Uniform triangle id selection
					Out_Coord.Tri = min(uint(RandT0*float({MeshTriangleCountName})), {MeshTriangleCountName}-1); // avoid % by using mul/min to Tri = MeshTriangleCountName
				}
				else
				{
					// Uniform area weighted position selection (using alias method from Alias method from FWeightedRandomSampler)
					uint TriangleIndex = min(uint(RandT0*float({MeshTriangleCountName})), {MeshTriangleCountName}-1);
					float TriangleProbability = {MeshTriangleSamplerProbaBufferName}[TriangleIndex];

					// Alias check
					float RandT1 = NiagaraInternalNoise(1, 2, 3);
					if( RandT1 > TriangleProbability )
					{
						TriangleIndex = {MeshTriangleSamplerAliasBufferName}[TriangleIndex];
					}
					Out_Coord.Tri = TriangleIndex;
				}

				float r0 = NiagaraInternalNoise(1, 2, 3);
				float r1 = NiagaraInternalNoise(1, 2, 3);
				float sqrt0 = sqrt(r0);
				float sqrt1 = sqrt(r1);
				Out_Coord.BaryCoord = float3(1.0f - sqrt0, sqrt0 * (1.0 - r1), r1 * sqrt0);
			//	Out_Coord.BaryCoord = float3(1.0f, 0.0f, 0.0f);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName)
	{
		OutHLSL += FString::Format(FormatCommonFunctions, ArgsSample);
		OutHLSL += FString::Format(FormatSampleSkinnedTriangleDataWSHeader, ArgsSample);
		OutHLSL += FString::Format(FormatSampleSkinnedTriangleDataWSPart0, ArgsSample);
	}
	else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName)
	{
		OutHLSL += FString::Format(FormatCommonFunctions, ArgsSample);
		OutHLSL += FString::Format(FormatSampleSkinnedTriangleDataWSInterpolatedHeader, ArgsSample);
		OutHLSL += FString::Format(FormatSampleSkinnedTriangleDataWSPart0, ArgsSample);
	}
	else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetTriColorName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, out float4 Out_Color)
				{
					Out_Color = 0.0f;
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetTriUVName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in {MeshTriCoordinateStructName} In_Coord, in int In_UVSet, out float2 Out_UV)
			{
				if({NumTexCoordName}>0)
				{
					uint TriangleIndex = In_Coord.Tri * 3;
					uint VertexIndex0 = {MeshIndexBufferName}[TriangleIndex  ];
					uint VertexIndex1 = {MeshIndexBufferName}[TriangleIndex+1];
					uint VertexIndex2 = {MeshIndexBufferName}[TriangleIndex+2];

					uint stride = {NumTexCoordName};
					uint SelectedUVSet = clamp(In_UVSet, 0, {NumTexCoordName}-1);
					float2 UV0 = {MeshTexCoordBufferName}[VertexIndex0 * stride + SelectedUVSet];
					float2 UV1 = {MeshTexCoordBufferName}[VertexIndex1 * stride + SelectedUVSet];
					float2 UV2 = {MeshTexCoordBufferName}[VertexIndex2 * stride + SelectedUVSet];

					Out_UV = UV0 * In_Coord.BaryCoord.x + UV1 * In_Coord.BaryCoord.y + UV2 * In_Coord.BaryCoord.z;
				}
				else	
				{
					Out_UV = 0.0f;
				}
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::IsValidTriCoordName)
	//{
	//	// unimplemented();
	//}
	//else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName)
	//{
	//	// unimplemented();
	//}
	//else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName)
	//{
	//	// unimplemented();
	//}
	//else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetTriangleCountName)
	//{
	//	// unimplemented();
	//}
	//else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetTriangleAtName)
	//{
	//	// unimplemented();
	//}
	//else if (DefinitionFunctionName == FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName)
	//{
	//}
	else
	{
		// This function is not support
		return false;
	}

	OutHLSL += TEXT("\n");
	return true;

}
void UNiagaraDataInterfaceSkeletalMesh::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FNDISkeletalMeshParametersName ParamNames;
	GetNiagaraDataInterfaceParametersName(ParamNames, ParamInfo.DataInterfaceHLSLSymbol);

	OutHLSL += TEXT("Buffer<uint> ") + ParamNames.MeshIndexBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float> ") + ParamNames.MeshVertexBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<uint> ") + ParamNames.MeshSkinWeightBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float4> ") + ParamNames.MeshCurrBonesBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float4> ") + ParamNames.MeshPrevBonesBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float4> ") + ParamNames.MeshTangentBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float2> ") + ParamNames.MeshTexCoordBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<float> ") + ParamNames.MeshTriangleSamplerProbaBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<uint> ") + ParamNames.MeshTriangleSamplerAliasBufferName + TEXT(";\n");
	OutHLSL += TEXT("Buffer<uint> ") + ParamNames.MeshTriangleMatricesOffsetBufferName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.MeshTriangleCountName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstanceTransformName + TEXT(";\n");
	OutHLSL += TEXT("float4x4 ") + ParamNames.InstancePrevTransformName + TEXT(";\n");
	OutHLSL += TEXT("float ") + ParamNames.InstanceInvDeltaTimeName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.EnabledFeaturesName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.InputWeightStrideName + TEXT(";\n");
	OutHLSL += TEXT("uint ") + ParamNames.NumTexCoordName + TEXT(";\n");
}
FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceSkeletalMesh::ConstructComputeParameters() const
{
	return new FNiagaraDataInterfaceParametersCS_SkeletalMesh();
}


void UNiagaraDataInterfaceSkeletalMesh::SetSourceComponentFromBlueprints(USkeletalMeshComponent* ComponentToUse)
{
	// NOTE: When ChangeId changes the next tick will be skipped and a reset of the per-instance data will be initiated. 
	ChangeId++;
	SourceComponent = ComponentToUse;
	Source = ComponentToUse->GetOwner();
}

//UNiagaraDataInterfaceSkeletalMesh END
//////////////////////////////////////////////////////////////////////////

template<>
void FSkeletalMeshAccessorHelper::Init<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::None>>
	(FNDISkeletalMesh_InstanceData* InstData)
{
	Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	Mesh = InstData->Mesh;
	LODData = &InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	IndexBuffer = LODData->MultiSizeIndexContainer.GetIndexBuffer();
	SkinningData = InstData->SkinningData.SkinningData.Get();
	Usage = InstData->SkinningData.Usage;

	if (Comp)
	{
		const USkinnedMeshComponent* BaseComp = Comp->GetBaseComponent();
		BoneComponentSpaceTransforms = &BaseComp->GetComponentSpaceTransforms();
		PrevBoneComponentSpaceTransforms = &BaseComp->GetPreviousComponentTransformsArray();
	}

	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	SamplingRegion = &SamplingInfo.GetRegion(InstData->SamplingRegionIndices[0]);
	SamplingRegionBuiltData = &SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[0]);
}

template<>
void FSkeletalMeshAccessorHelper::Init<
	TIntegralConstant<ENDISkeletalMesh_FilterMode, ENDISkeletalMesh_FilterMode::SingleRegion>,
	TIntegralConstant<ENDISkelMesh_AreaWeightingMode, ENDISkelMesh_AreaWeightingMode::AreaWeighted>>
	(FNDISkeletalMesh_InstanceData* InstData)
{
	Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	Mesh = InstData->Mesh;
	LODData = &InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	IndexBuffer = LODData->MultiSizeIndexContainer.GetIndexBuffer();
	SkinningData = InstData->SkinningData.SkinningData.Get();
	Usage = InstData->SkinningData.Usage;

	if (Comp)
	{
		const USkinnedMeshComponent* BaseComp = Comp->GetBaseComponent();
		BoneComponentSpaceTransforms = &BaseComp->GetComponentSpaceTransforms();
		PrevBoneComponentSpaceTransforms = &BaseComp->GetPreviousComponentTransformsArray();
	}

	const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
	SamplingRegion = &SamplingInfo.GetRegion(InstData->SamplingRegionIndices[0]);
	SamplingRegionBuiltData = &SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[0]);
}


#undef LOCTEXT_NAMESPACE
