// Copyright Epic Games, Inc. All Rights Reserved.

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

struct FNiagaraSkelMeshDIFunctionVersion
{
	enum Type
	{
		InitialVersion = 0,
		AddedRandomInfo = 1,
		CleanUpVertexSampling = 2,
		CleanupBoneSampling = 3,
		AddTangentBasisToGetSkinnedVertexData = 4,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

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

FSkeletalMeshSkinningDataHandle::FSkeletalMeshSkinningDataHandle(FSkeletalMeshSkinningDataHandle&& Other)
{
	Usage = Other.Usage;
	SkinningData = Other.SkinningData;
	Other.SkinningData = nullptr;
}

FSkeletalMeshSkinningDataHandle& FSkeletalMeshSkinningDataHandle::operator=(FSkeletalMeshSkinningDataHandle&& Other)
{
	if (this != &Other)
	{
		Usage = Other.Usage;
		SkinningData = Other.SkinningData;
		Other.SkinningData = nullptr;
	}
	return *this;
}

//////////////////////////////////////////////////////////////////////////
void FSkeletalMeshSkinningData::ForceDataRefresh()
{
	FRWScopeLock Lock(RWGuard, SLT_Write);
	bForceDataRefresh = true;
}

void FSkeletalMeshSkinningData::RegisterUser(FSkeletalMeshSkinningDataUsage Usage)
{
	FRWScopeLock Lock(RWGuard, SLT_Write);
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
		if (CurrBoneRefToLocals().Num() == 0 || CurrComponentTransforms().Num() == 0 )
		{
			UpdateBoneTransforms();
		}
		
		//Prime the prev matrices if they're missing.
		if (PrevBoneRefToLocals().Num() != CurrBoneRefToLocals().Num())
		{
			PrevBoneRefToLocals() = CurrBoneRefToLocals();
		}

		if (PrevComponentTransforms().Num() != CurrComponentTransforms().Num())
		{
			PrevComponentTransforms() = CurrComponentTransforms();
		}

		if (Usage.NeedPreSkinnedVerts() && CurrSkinnedPositions(LODIndex).Num() == 0)
		{
			FSkeletalMeshLODRenderData& SkelMeshLODData = SkelComp->SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
			FSkinWeightVertexBuffer* SkinWeightBuffer = SkelComp->GetSkinWeightBuffer(LODIndex);
			USkeletalMeshComponent::ComputeSkinnedPositions(SkelComp, CurrSkinnedPositions(LODIndex), CurrBoneRefToLocals(), SkelMeshLODData, *SkinWeightBuffer);
			USkeletalMeshComponent::ComputeSkinnedTangentBasis(SkelComp, CurrSkinnedTangentBasis(LODIndex), CurrBoneRefToLocals(), SkelMeshLODData, *SkinWeightBuffer);

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
	FRWScopeLock Lock(RWGuard, SLT_Write);
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

bool FSkeletalMeshSkinningData::IsUsed() const
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

void FSkeletalMeshSkinningData::UpdateBoneTransforms()
{
	USkeletalMeshComponent* SkelComp = MeshComp.Get();
	check(SkelComp);

	const TArray<FTransform>& BaseCompSpaceTransforms = SkelComp->GetComponentSpaceTransforms();
	TArray<FMatrix>& CurrBones = CurrBoneRefToLocals();
	TArray<FTransform>& CurrTransforms = CurrComponentTransforms();

	if (USkinnedMeshComponent* MasterComponent = SkelComp->MasterPoseComponent.Get())
	{
		const USkeletalMesh* SkelMesh = SkelComp->SkeletalMesh;
		const TArray<int32>& MasterBoneMap = SkelComp->GetMasterBoneMap();
		const int32 NumBones = MasterBoneMap.Num();

		check(SkelMesh);
		if (NumBones == 0)
		{
			// This case indicates an invalid master pose component (e.g. no skeletal mesh)
			CurrBones.Empty(SkelMesh->RefSkeleton.GetNum());
			CurrBones.AddDefaulted(SkelMesh->RefSkeleton.GetNum());
			CurrTransforms.Empty(SkelMesh->RefSkeleton.GetNum());
			CurrTransforms.AddDefaulted(SkelMesh->RefSkeleton.GetNum());
		}
		else
		{
			CurrBones.SetNumUninitialized(NumBones);
			CurrTransforms.SetNumUninitialized(NumBones);

			const TArray<FTransform>& MasterTransforms = MasterComponent->GetComponentSpaceTransforms();
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				bool bFoundMaster = false;
				FTransform CompSpaceTransform;
				if (MasterBoneMap.IsValidIndex(BoneIndex))
				{
					const int32 MasterIndex = MasterBoneMap[BoneIndex];
					if (MasterIndex != INDEX_NONE && MasterIndex < MasterTransforms.Num())
					{
						CurrTransforms[BoneIndex] = MasterTransforms[MasterIndex];
						CurrBones[BoneIndex] = SkelMesh->RefBasesInvMatrix[BoneIndex] * MasterTransforms[MasterIndex].ToMatrixWithScale();
						bFoundMaster = true;
					}
				}

				if (!bFoundMaster)
				{
					const int32 ParentIndex = SkelMesh->RefSkeleton.GetParentIndex(BoneIndex);

					if (CurrTransforms.IsValidIndex(ParentIndex) && ParentIndex < BoneIndex)
					{
						CurrTransforms[BoneIndex] = CurrTransforms[ParentIndex] * SkelMesh->RefSkeleton.GetRefBonePose()[BoneIndex];
					}
					else
					{
						CurrTransforms[BoneIndex] = SkelMesh->RefSkeleton.GetRefBonePose()[BoneIndex];
					}

					CurrBones[BoneIndex] = CurrTransforms[BoneIndex].ToMatrixWithScale();

				}
			}
		}
	}
	else
	{
		SkelComp->CacheRefToLocalMatrices(CurrBones);
		CurrTransforms = SkelComp->GetComponentSpaceTransforms();
	}
}

bool FSkeletalMeshSkinningData::Tick(float InDeltaSeconds, bool bRequirePreskin)
{
	FRWScopeLock Lock(RWGuard, SLT_Write);

	USkeletalMeshComponent* SkelComp = MeshComp.Get();
	check(SkelComp);
	DeltaSeconds = InDeltaSeconds;
	CurrIndex ^= 1;

	if (BoneMatrixUsers > 0)
	{
		UpdateBoneTransforms();
	}

	//Prime the prev matrices if they're missing.
	if ((PrevBoneRefToLocals().Num() != CurrBoneRefToLocals().Num()) || bForceDataRefresh)
	{
		PrevBoneRefToLocals() = CurrBoneRefToLocals();
	}

	if ((PrevComponentTransforms().Num() != CurrComponentTransforms().Num()) || bForceDataRefresh)
	{
		PrevComponentTransforms() = CurrComponentTransforms();
	}

	if (bRequirePreskin)
	{
		for (int32 LODIndex = 0; LODIndex < LODData.Num(); ++LODIndex)
		{
			FLODData& LOD = LODData[LODIndex];
			if (LOD.PreSkinnedVertsUsers > 0)
			{
				//TODO: If we pass the sections in the usage too, we can probably skin a minimal set of verts just for the used regions.
				FSkeletalMeshLODRenderData& SkelMeshLODData = SkelComp->SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];
				FSkinWeightVertexBuffer* SkinWeightBuffer = SkelComp->GetSkinWeightBuffer(LODIndex);
				USkeletalMeshComponent::ComputeSkinnedPositions(SkelComp, CurrSkinnedPositions(LODIndex), CurrBoneRefToLocals(), SkelMeshLODData, *SkinWeightBuffer);
				USkeletalMeshComponent::ComputeSkinnedTangentBasis(SkelComp, CurrSkinnedTangentBasis(LODIndex), CurrBoneRefToLocals(), SkelMeshLODData, *SkinWeightBuffer);
				//check(CurrSkinnedPositions(LODIndex).Num() == SkelMeshLODData.NumVertices);
				//Prime the previous positions if they're missing
				if (PrevSkinnedPositions(LODIndex).Num() != CurrSkinnedPositions(LODIndex).Num())
				{
					PrevSkinnedPositions(LODIndex) = CurrSkinnedPositions(LODIndex);
				}
			}
		}
	}

	bForceDataRefresh = false;
	return true;
}

//////////////////////////////////////////////////////////////////////////

FSkeletalMeshSkinningDataHandle FNDI_SkeletalMesh_GeneratedData::GetCachedSkinningData(TWeakObjectPtr<USkeletalMeshComponent>& Component, FSkeletalMeshSkinningDataUsage Usage)
{
	check(Component.Get() != nullptr);

	// Attempt to Find data
	{
		FRWScopeLock ReadLock(CachedSkinningDataGuard, SLT_ReadOnly);
		if ( CachedSkinningDataAndUsage* Existing = CachedSkinningData.Find(Component) )
		{
			check(Existing->SkinningData.IsValid());
			ensure(Existing->Usage.NeedBoneMatrices() == Usage.NeedBoneMatrices());
			ensure(Existing->Usage.NeedPreSkinnedVerts() == Usage.NeedPreSkinnedVerts());
			ensure(Existing->Usage.NeedsDataImmediately() == Usage.NeedsDataImmediately());
			ensure(Existing->Usage.GetLODIndex() == Usage.GetLODIndex());

			return FSkeletalMeshSkinningDataHandle(Existing->Usage, Existing->SkinningData);
		}
	}

	// We need to add
	FRWScopeLock WriteLock(CachedSkinningDataGuard, SLT_Write);
	CachedSkinningDataAndUsage& NewData = CachedSkinningData.FindOrAdd(Component);
	NewData.Usage = Usage;
	NewData.SkinningData = MakeShared<FSkeletalMeshSkinningData>(Component);
	return FSkeletalMeshSkinningDataHandle(NewData.Usage, NewData.SkinningData);
}

void FNDI_SkeletalMesh_GeneratedData::TickGeneratedData(ETickingGroup TickGroup, float DeltaSeconds)
{
	check(IsInGameThread());
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSkel_PreSkin);

	FRWScopeLock WriteLock(CachedSkinningDataGuard, SLT_Write);

	// We may want to look at separating out how we manage the ticks here
	//-OPT: Move into different arrays per tick group, manage promotions, demotions, etc, or add ourselves as a subsequent of the component's tick
	TArray<TWeakObjectPtr<USkeletalMeshComponent>, TInlineAllocator<32>> ToRemove;
	TArray<FSkeletalMeshSkinningData*, TInlineAllocator<32>> ToTickBonesOnly;
	TArray<FSkeletalMeshSkinningData*, TInlineAllocator<32>> ToTickPreskin;
	const bool bForceTick = TickGroup == NiagaraLastTickGroup;

	ToTickBonesOnly.Reserve(CachedSkinningData.Num());
	ToTickPreskin.Reserve(CachedSkinningData.Num());

	for (TPair<TWeakObjectPtr<USkeletalMeshComponent>, CachedSkinningDataAndUsage>& Pair : CachedSkinningData)
	{
		if ( TickGroup == NiagaraFirstTickGroup )
		{
			Pair.Value.bHasTicked = false;
		}

		// Should remove?
		TSharedPtr<FSkeletalMeshSkinningData>& SkinningData = Pair.Value.SkinningData;
		USkeletalMeshComponent* Component = Pair.Key.Get();
		if ( (Component == nullptr) || SkinningData.IsUnique() || !SkinningData->IsUsed() )
		{
			ToRemove.Add(Pair.Key);
			continue;
		}

		if ( Pair.Value.bHasTicked == true )
		{
			continue;
		}

		// Has ticked or can be ticked
		if (bForceTick == false)
		{
			const ETickingGroup PrereqTickGroup = FMath::Max(Component->PrimaryComponentTick.TickGroup, Component->PrimaryComponentTick.EndTickGroup);
			if ((PrereqTickGroup > TickGroup) || (Component->PrimaryComponentTick.IsCompletionHandleValid() && !Component->PrimaryComponentTick.GetCompletionHandle()->IsComplete()))
			{
				continue;
			}
		}

		// We are going to tick this one
		Pair.Value.bHasTicked = true;

		FSkeletalMeshSkinningDataUsage Usage = Pair.Value.Usage;
		FSkeletalMeshSkinningData* SkinningDataPtr = SkinningData.Get();
		check(SkinningDataPtr);

		if (Usage.NeedPreSkinnedVerts())
		{
			ToTickPreskin.Add(SkinningDataPtr);
		}
		else
		{
			ToTickBonesOnly.Add(SkinningDataPtr);
		}
	}

	for (TWeakObjectPtr<USkeletalMeshComponent> Key : ToRemove)
	{
		CachedSkinningData.Remove(Key);
	}
	
	// First tick the meshes that don't need pre-skinning. 
	// This prevents additional threading overhead when we don't need to pre-skin.
	for (int i = 0; i < ToTickBonesOnly.Num(); i++)
	{
		ToTickBonesOnly[i]->Tick(DeltaSeconds, false);
	}
		
	// Then tick the remaining meshes requiring pre-skinning in parallel
	if (ToTickPreskin.Num() != 0)
	{
		ParallelFor(ToTickPreskin.Num(), [&](int32 Index)
		{
			ToTickPreskin[Index]->Tick(DeltaSeconds, true);
		});
	}
}

//////////////////////////////////////////////////////////////////////////
// FStaticMeshGpuSpawnBuffer


FSkeletalMeshGpuSpawnStaticBuffers::~FSkeletalMeshGpuSpawnStaticBuffers()
{
	//ValidSections.Empty();
}

void FSkeletalMeshGpuSpawnStaticBuffers::Initialise(FNDISkeletalMesh_InstanceData* InstData, const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData, const FSkeletalMeshSamplingLODBuiltData& MeshSamplingLODBuiltData)
{
	SkeletalMeshSamplingLODBuiltData = nullptr;
	bUseGpuUniformlyDistributedSampling = false;

	LODRenderData = nullptr;
	TriangleCount = 0;
	VertexCount = 0;

	NumFilteredBones = 0;
	NumUnfilteredBones = 0;
	FilteredAndUnfilteredBonesArray.Empty();
	NumFilteredSockets = 0;
	FilteredSocketBoneOffset = 0;

	if (InstData)
	{
		SkeletalMeshSamplingLODBuiltData = &MeshSamplingLODBuiltData;
		bUseGpuUniformlyDistributedSampling = InstData->bIsGpuUniformlyDistributedSampling;

		LODRenderData = &SkeletalMeshLODRenderData;
		TriangleCount = SkeletalMeshLODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->Num() / 3;
		VertexCount = SkeletalMeshLODRenderData.GetNumVertices();

		if (TriangleCount == 0)
		{
			UE_LOG(LogNiagara, Warning, TEXT("FSkeletalMeshGpuSpawnStaticBuffers> Triangle count is invalid %d"), TriangleCount, (InstData && InstData->Mesh) ? *InstData->Mesh->GetFullName() : TEXT("Unknown Mesh"));
		}
		if (VertexCount == 0)
		{
			UE_LOG(LogNiagara, Warning, TEXT("FSkeletalMeshGpuSpawnStaticBuffers> Vertex count is invalid %d"), VertexCount, (InstData && InstData->Mesh) ? *InstData->Mesh->GetFullName() : TEXT("Unknown Mesh"));
		}

		// Copy filtered Bones / Socket data into arrays that the renderer will use to create read buffers
		//-TODO: Exclude setting up these arrays if we don't sample from them
		NumFilteredBones = InstData->NumFilteredBones;
		NumUnfilteredBones = InstData->NumUnfilteredBones;
		ExcludedBoneIndex = InstData->ExcludedBoneIndex;

		FilteredAndUnfilteredBonesArray.Reserve(InstData->FilteredAndUnfilteredBones.Num());
		for (uint16 v : InstData->FilteredAndUnfilteredBones)
		{
			FilteredAndUnfilteredBonesArray.Add(v);
		}

		NumFilteredSockets = InstData->FilteredSocketInfo.Num();
		FilteredSocketBoneOffset = InstData->FilteredSocketBoneOffset;

		// Create triangle / vertex region sampling data
		if (InstData->SamplingRegionIndices.Num() > 0)
		{
			const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();

			// Count required regions
			bSamplingRegionsAllAreaWeighted = true;
			NumSamplingRegionTriangles = 0;
			NumSamplingRegionVertices = 0;

			for (const int32 RegionIndex : InstData->SamplingRegionIndices)
			{
				const FSkeletalMeshSamplingRegionBuiltData& SamplingRegionBuildData = SamplingInfo.GetRegionBuiltData(RegionIndex);
				NumSamplingRegionTriangles += SamplingRegionBuildData.TriangleIndices.Num();
				NumSamplingRegionVertices += SamplingRegionBuildData.Vertices.Num();
				bSamplingRegionsAllAreaWeighted &= SamplingRegionBuildData.AreaWeightedSampler.GetNumEntries() == SamplingRegionBuildData.TriangleIndices.Num();
			}

			// Build buffers
			SampleRegionsProb.Reserve(NumSamplingRegionTriangles);
			SampleRegionsAlias.Reserve(NumSamplingRegionTriangles);
			SampleRegionsTriangleIndicies.Reserve(NumSamplingRegionTriangles);
			SampleRegionsVertices.Reserve(NumSamplingRegionVertices);

			int32 RegionOffset = 0;
			for (const int32 RegionIndex : InstData->SamplingRegionIndices)
			{
				const FSkeletalMeshSamplingRegionBuiltData& SamplingRegionBuildData = SamplingInfo.GetRegionBuiltData(RegionIndex);
				if (bSamplingRegionsAllAreaWeighted)
				{
					for (float v : SamplingRegionBuildData.AreaWeightedSampler.GetProb())
					{
						SampleRegionsProb.Add(v);
					}
					for (int v : SamplingRegionBuildData.AreaWeightedSampler.GetAlias())
					{
						SampleRegionsAlias.Add(v + RegionOffset);
					}
				}
				for (int v : SamplingRegionBuildData.TriangleIndices)
				{
					SampleRegionsTriangleIndicies.Add(v / 3);
				}
				for (int v : SamplingRegionBuildData.Vertices)
				{
					SampleRegionsVertices.Add(v);
				}
				RegionOffset += SamplingRegionBuildData.TriangleIndices.Num();
			}
		}
	}
}

void FSkeletalMeshGpuSpawnStaticBuffers::InitRHI()
{
	// As of today, the UI does not allow to cull specific section of a mesh so this data could be generated on the Mesh. But Section culling might be added later?
	// Also see https://jira.it.epicgames.net/browse/UE-69376 : we would need to know if GPU sampling of the mesh surface is needed or not on the mesh to be able to do that.
	// ALso today we do not know if an interface is create from a CPU or GPU emitter. So always allocate for now. Follow up in https://jira.it.epicgames.net/browse/UE-69375.

	const FMultiSizeIndexContainer& IndexBuffer = LODRenderData->MultiSizeIndexContainer;
	MeshIndexBufferSrv = IndexBuffer.GetIndexBuffer()->GetSRV();
	if (!MeshIndexBufferSrv)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh does not have an SRV for the index buffer, if you are using triangle sampling it will not work."));
	}

	MeshVertexBufferSrv = LODRenderData->StaticVertexBuffers.PositionVertexBuffer.GetSRV();

	MeshTangentBufferSRV = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
	//check(MeshTangentBufferSRV->IsValid()); // not available in this stream

	MeshTexCoordBufferSrv = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
	NumTexCoord = LODRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

	MeshColorBufferSrv = LODRenderData->StaticVertexBuffers.ColorVertexBuffer.GetColorComponentsSRV();

	NumWeights = LODRenderData->SkinWeightVertexBuffer.GetMaxBoneInfluences();

	uint32 SectionCount = LODRenderData->RenderSections.Num();

	if (bUseGpuUniformlyDistributedSampling)
	{
		const FSkeletalMeshAreaWeightedTriangleSampler& triangleSampler = SkeletalMeshSamplingLODBuiltData->AreaWeightedTriangleSampler;
		TArrayView<const float> Prob = triangleSampler.GetProb();
		TArrayView<const int32> Alias = triangleSampler.GetAlias();
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

	// Prepare sampling regions (if we have any)
	if (NumSamplingRegionTriangles > 0)
	{
		FRHIResourceCreateInfo CreateInfo;
		if (bSamplingRegionsAllAreaWeighted)
		{
			CreateInfo.ResourceArray = &SampleRegionsProb;
			SampleRegionsProbBuffer = RHICreateVertexBuffer(SampleRegionsProb.Num() * SampleRegionsProb.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			SampleRegionsProbSRV = RHICreateShaderResourceView(SampleRegionsProbBuffer, sizeof(float), PF_R32_FLOAT);

			CreateInfo.ResourceArray = &SampleRegionsAlias;
			SampleRegionsAliasBuffer = RHICreateVertexBuffer(SampleRegionsAlias.Num() * SampleRegionsAlias.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
			SampleRegionsAliasSRV = RHICreateShaderResourceView(SampleRegionsAliasBuffer, sizeof(float), PF_R32_UINT);
		}
		CreateInfo.ResourceArray = &SampleRegionsTriangleIndicies;
		SampleRegionsTriangleIndicesBuffer = RHICreateVertexBuffer(SampleRegionsTriangleIndicies.Num() * SampleRegionsTriangleIndicies.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
		SampleRegionsTriangleIndicesSRV = RHICreateShaderResourceView(SampleRegionsTriangleIndicesBuffer, sizeof(int32), PF_R32_UINT);

		CreateInfo.ResourceArray = &SampleRegionsVertices;
		SampleRegionsVerticesBuffer = RHICreateVertexBuffer(SampleRegionsVertices.Num() * SampleRegionsVertices.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
		SampleRegionsVerticesSRV = RHICreateShaderResourceView(SampleRegionsVerticesBuffer, sizeof(int32), PF_R32_UINT);
	}

	// Prepare the vertex matrix lookup offset for each of the sections. This is needed because per vertex BlendIndicies are stored relatively to each Section used matrices.
	// And these offset per section need to point to the correct matrix according to each section BoneMap.
	// There is not section selection/culling in the interface so technically we could compute that array in the pipeline.
	{
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

	// Create arrays for filtered bones / sockets
	if ( FilteredAndUnfilteredBonesArray.Num() > 0 )
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.ResourceArray = &FilteredAndUnfilteredBonesArray;

		FilteredAndUnfilteredBonesBuffer = RHICreateVertexBuffer(FilteredAndUnfilteredBonesArray.Num() * FilteredAndUnfilteredBonesArray.GetTypeSize(), BUF_Static | BUF_ShaderResource, CreateInfo);
		FilteredAndUnfilteredBonesSRV = RHICreateShaderResourceView(FilteredAndUnfilteredBonesBuffer, sizeof(uint16), PF_R16_UINT);
	}
}

void FSkeletalMeshGpuSpawnStaticBuffers::ReleaseRHI()
{
	FilteredAndUnfilteredBonesBuffer.SafeRelease();
	FilteredAndUnfilteredBonesSRV.SafeRelease();

	BufferTriangleUniformSamplerProbaRHI.SafeRelease();
	BufferTriangleUniformSamplerProbaSRV.SafeRelease();
	BufferTriangleUniformSamplerAliasRHI.SafeRelease();
	BufferTriangleUniformSamplerAliasSRV.SafeRelease();

	SampleRegionsProbBuffer.SafeRelease();
	SampleRegionsProbSRV.SafeRelease();
	SampleRegionsAliasBuffer.SafeRelease();
	SampleRegionsAliasSRV.SafeRelease();
	SampleRegionsTriangleIndicesBuffer.SafeRelease();
	SampleRegionsTriangleIndicesSRV.SafeRelease();
	SampleRegionsVerticesBuffer.SafeRelease();
	SampleRegionsVerticesSRV.SafeRelease();

	MeshVertexBufferSrv = nullptr;
	MeshIndexBufferSrv = nullptr;
	MeshTangentBufferSRV = nullptr;
	MeshTexCoordBufferSrv = nullptr;
	MeshColorBufferSrv = nullptr;
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

void FSkeletalMeshGpuDynamicBufferProxy::Initialise(const FReferenceSkeleton& RefSkel, const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData, uint32 InSamplingSocketCount)
{
	SectionBoneCount = 0;
	for (const FSkelMeshRenderSection& Section : SkeletalMeshLODRenderData.RenderSections)
	{
		SectionBoneCount += Section.BoneMap.Num();
	}

	SamplingBoneCount = RefSkel.GetNum();
	SamplingSocketCount = InSamplingSocketCount;
}

void FSkeletalMeshGpuDynamicBufferProxy::InitRHI()
{
	for (FSkeletalBuffer& Buffer : RWBufferBones)
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = TEXT("SkeletalMeshGpuDynamicBuffer");
		Buffer.SectionBuffer = RHICreateVertexBuffer(sizeof(FVector4) * 3 * SectionBoneCount, BUF_ShaderResource | BUF_Dynamic, CreateInfo);
		Buffer.SectionSRV = RHICreateShaderResourceView(Buffer.SectionBuffer, sizeof(FVector4), PF_A32B32G32R32F);

		Buffer.SamplingBuffer = RHICreateVertexBuffer(sizeof(FVector4) * 2 * (SamplingBoneCount + SamplingSocketCount), BUF_ShaderResource | BUF_Dynamic, CreateInfo);
		Buffer.SamplingSRV = RHICreateShaderResourceView(Buffer.SamplingBuffer, sizeof(FVector4), PF_A32B32G32R32F);
	}
}

void FSkeletalMeshGpuDynamicBufferProxy::ReleaseRHI()
{
	for (FSkeletalBuffer& Buffer : RWBufferBones)
	{
		Buffer.SectionBuffer.SafeRelease();
		Buffer.SectionSRV.SafeRelease();

		Buffer.SamplingBuffer.SafeRelease();
		Buffer.SamplingSRV.SafeRelease();
	}
}

void FSkeletalMeshGpuDynamicBufferProxy::NewFrame(const FNDISkeletalMesh_InstanceData* InstanceData, int32 LODIndex)
{
	// Grab Skeletal Component / Mesh, we must have a mesh at minimum to set the data
	USkeletalMeshComponent* SkelComp = nullptr;
	USkeletalMesh* SkelMesh = nullptr;
	if (InstanceData != nullptr)
	{
		SkelComp = Cast<USkeletalMeshComponent>(InstanceData->Component.Get());
		if ( SkelComp != nullptr )
		{
			SkelMesh = SkelComp->SkeletalMesh;
		}
		if (SkelMesh == nullptr)
		{
			SkelMesh = InstanceData->MeshSafe.Get();
		}
	}

	if ( SkelMesh == nullptr )
	{
		return;
	}

	static_assert(sizeof(FVector4) == 4 * sizeof(float), "FVector4 should match 4 * floats");

	TArray<FVector4> AllSectionsRefToLocalMatrices;
	TArray<FVector4> BoneSamplingData;

	auto FillBuffers =
		[&](const TArray<FTransform>& BoneTransforms)
		{
			// Fill AllSectionsRefToLocalMatrices
			TIndirectArray<FSkeletalMeshLODRenderData>& LODRenderDataArray = SkelMesh->GetResourceForRendering()->LODRenderData;
			check(0 <= LODIndex && LODIndex < LODRenderDataArray.Num());
			FSkeletalMeshLODRenderData& LODRenderData = LODRenderDataArray[LODIndex];
			TArray<FSkelMeshRenderSection>& Sections = LODRenderData.RenderSections;
			uint32 SectionCount = Sections.Num();

			// Count number of matrices we want before appending all of them according to the per section mapping from BoneMap
			uint32 Float4Count = 0;
			for (const FSkelMeshRenderSection& Section : Sections)
			{
				Float4Count += Section.BoneMap.Num() * 3;
			}
			check(Float4Count == 3 * SectionBoneCount);
			AllSectionsRefToLocalMatrices.AddUninitialized(Float4Count);

			Float4Count = 0;
			for (const FSkelMeshRenderSection& Section : Sections)
			{
				const uint32 MatrixCount = Section.BoneMap.Num();
				for (uint32 m=0; m < MatrixCount; ++m)
				{
					const int32 BoneIndex = Section.BoneMap[m];
					const FTransform& BoneTransform = BoneTransforms[BoneIndex];
					const FMatrix BoneMatrix = SkelMesh->RefBasesInvMatrix.IsValidIndex(BoneIndex) ? SkelMesh->RefBasesInvMatrix[BoneIndex] * BoneTransform.ToMatrixWithScale() : BoneTransform.ToMatrixWithScale();
					BoneMatrix.To3x4MatrixTranspose(&AllSectionsRefToLocalMatrices[Float4Count].X);
					Float4Count += 3;
				}
			}

			// Fill BoneSamplingData
			BoneSamplingData.Reserve((SamplingBoneCount + SamplingSocketCount) * 2);
			check(BoneTransforms.Num() == SamplingBoneCount);

			for (const FTransform& BoneTransform : BoneTransforms)
			{
				const FQuat Rotation = BoneTransform.GetRotation();
				BoneSamplingData.Add(BoneTransform.GetLocation());
				BoneSamplingData.Add(FVector4(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
			}

			// Append sockets
			for (const FTransform& SocketTransform : InstanceData->GetFilteredSocketsCurrBuffer())
			{
				const FQuat Rotation = SocketTransform.GetRotation();
				BoneSamplingData.Add(SocketTransform.GetLocation());
				BoneSamplingData.Add(FVector4(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
			}
		};

	// If we have a component pull transforms from component otherwise grab from skel mesh
	if (SkelComp)
	{
		if (USkinnedMeshComponent* MasterComponent = SkelComp->MasterPoseComponent.Get())
		{
			const TArray<int32>& MasterBoneMap = SkelComp->GetMasterBoneMap();
			const int32 NumBones = MasterBoneMap.Num();

			TArray<FTransform> TempBoneTransforms;
			TempBoneTransforms.Reserve(SamplingBoneCount);

			if (NumBones == 0)
			{
				// This case indicates an invalid master pose component (e.g. no skeletal mesh)
				TempBoneTransforms.AddDefaulted(SamplingBoneCount);
			}
			else
			{
				const TArray<FTransform>& MasterTransforms = MasterComponent->GetComponentSpaceTransforms();
				for (int32 BoneIndex=0; BoneIndex < NumBones; ++BoneIndex)
				{
					if (MasterBoneMap.IsValidIndex(BoneIndex))
					{
						const int32 MasterIndex = MasterBoneMap[BoneIndex];
						if (MasterIndex != INDEX_NONE && MasterIndex < MasterTransforms.Num())
						{
							TempBoneTransforms.Add(MasterTransforms[MasterIndex]);
							continue;
						}
					}

					const int32 ParentIndex = SkelMesh->RefSkeleton.GetParentIndex(BoneIndex);
					FTransform BoneTransform =SkelMesh->RefSkeleton.GetRefBonePose()[BoneIndex];
					if (TempBoneTransforms.IsValidIndex(ParentIndex))
					{
						BoneTransform = BoneTransform * TempBoneTransforms[ParentIndex];
					}
					TempBoneTransforms.Add(BoneTransform);
				}
			}
			FillBuffers(TempBoneTransforms);
		}
		else
		{
			const TArray<FTransform>& ComponentTransforms = SkelComp->GetComponentSpaceTransforms();
			FillBuffers(ComponentTransforms);
		}
	}
	else
	{
		//-TODO: Opt and combine with MaterPoseComponent
		TArray<FTransform> TempBoneTransforms;
		TempBoneTransforms.Reserve(SamplingBoneCount);

		const TArray<FTransform>& RefTransforms = SkelMesh->RefSkeleton.GetRefBonePose();
		for (int32 i=0; i < RefTransforms.Num(); ++i)
		{
			FTransform BoneTransform = RefTransforms[i];
			const int32 ParentIndex = SkelMesh->RefSkeleton.GetParentIndex(i);
			if (TempBoneTransforms.IsValidIndex(ParentIndex))
			{
				BoneTransform = BoneTransform * TempBoneTransforms[ParentIndex];
			}
			TempBoneTransforms.Add(BoneTransform);
		}

		FillBuffers(TempBoneTransforms);
	}

	FSkeletalMeshGpuDynamicBufferProxy* ThisProxy = this;
	ENQUEUE_RENDER_COMMAND(UpdateSpawnInfoForSkinnedMesh)(
		[ThisProxy, AllSectionsRefToLocalMatrices = MoveTemp(AllSectionsRefToLocalMatrices), BoneSamplingData = MoveTemp(BoneSamplingData)](FRHICommandListImmediate& RHICmdList) mutable
		{
			ThisProxy->CurrentBoneBufferId = (ThisProxy->CurrentBoneBufferId + 1) % BufferBoneCount;
			ThisProxy->bPrevBoneGpuBufferValid = ThisProxy->bBoneGpuBufferValid;
			ThisProxy->bBoneGpuBufferValid = true;

			// Copy bone remap data matrices
			{
				const uint32 NumBytes = AllSectionsRefToLocalMatrices.Num() * sizeof(FVector4);
				void* DstData = RHILockVertexBuffer(ThisProxy->GetRWBufferBone().SectionBuffer, 0, NumBytes, RLM_WriteOnly);
				FMemory::Memcpy(DstData, AllSectionsRefToLocalMatrices.GetData(), NumBytes);
				RHIUnlockVertexBuffer(ThisProxy->GetRWBufferBone().SectionBuffer);
			}

			// Copy bone sampling data
			{
				const uint32 NumBytes = BoneSamplingData.Num() * sizeof(FVector4);
				FVector4* DstData = reinterpret_cast<FVector4*>(RHILockVertexBuffer(ThisProxy->GetRWBufferBone().SamplingBuffer, 0, NumBytes, RLM_WriteOnly));
				FMemory::Memcpy(DstData, BoneSamplingData.GetData(), NumBytes);
				RHIUnlockVertexBuffer(ThisProxy->GetRWBufferBone().SamplingBuffer);
			}
		}
	);
}

//////////////////////////////////////////////////////////////////////////
//FNiagaraDataInterfaceParametersCS_SkeletalMesh
struct FNDISkeletalMeshParametersName
{
	FString MeshIndexBufferName;
	FString MeshVertexBufferName;
	FString MeshSkinWeightBufferName;
	FString MeshSkinWeightLookupBufferName;
	FString MeshCurrBonesBufferName;
	FString MeshPrevBonesBufferName;
	FString MeshCurrSamplingBonesBufferName;
	FString MeshPrevSamplingBonesBufferName;
	FString MeshTangentBufferName;
	FString MeshTexCoordBufferName;
	FString MeshColorBufferName;
	FString MeshTriangleSamplerProbaBufferName;
	FString MeshTriangleSamplerAliasBufferName;
	FString MeshNumSamplingRegionTrianglesName;
	FString MeshNumSamplingRegionVerticesName;
	FString MeshSamplingRegionsProbaBufferName;
	FString MeshSamplingRegionsAliasBufferName;
	FString MeshSampleRegionsTriangleIndicesName;
	FString MeshSampleRegionsVerticesName;
	FString MeshTriangleMatricesOffsetBufferName;
	FString MeshTriangleCountName;
	FString MeshVertexCountName;
	FString MeshWeightStrideName;
	FString MeshSkinWeightIndexSizeName;
	FString MeshNumTexCoordName;
	FString MeshNumWeightsName;
	FString NumBonesName;
	FString NumFilteredBonesName;
	FString NumUnfilteredBonesName;
	FString RandomMaxBoneName;
	FString ExcludeBoneIndexName;
	FString FilteredAndUnfilteredBonesName;
	FString NumFilteredSocketsName;
	FString FilteredSocketBoneOffsetName;
	FString InstanceTransformName;
	FString InstancePrevTransformName;
	FString InstanceRotationName;
	FString InstancePrevRotationName;
	FString InstanceInvDeltaTimeName;
	FString EnabledFeaturesName;
};

static void GetNiagaraDataInterfaceParametersName(FNDISkeletalMeshParametersName& Names, const FString& Suffix)
{
	Names.MeshIndexBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshIndexBufferName + Suffix;
	Names.MeshVertexBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshVertexBufferName + Suffix;
	Names.MeshSkinWeightBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightBufferName + Suffix;
	Names.MeshSkinWeightLookupBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightLookupBufferName + Suffix;
	Names.MeshCurrBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshCurrBonesBufferName + Suffix;
	Names.MeshPrevBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshPrevBonesBufferName + Suffix;
	Names.MeshCurrSamplingBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshCurrSamplingBonesBufferName + Suffix;
	Names.MeshPrevSamplingBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshPrevSamplingBonesBufferName + Suffix;
	Names.MeshTangentBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTangentBufferName + Suffix;
	Names.MeshTexCoordBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTexCoordBufferName + Suffix;
	Names.MeshColorBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshColorBufferName + Suffix;
	Names.MeshTriangleSamplerProbaBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerProbaBufferName + Suffix;
	Names.MeshTriangleSamplerAliasBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerAliasBufferName + Suffix;
	Names.MeshNumSamplingRegionTrianglesName = UNiagaraDataInterfaceSkeletalMesh::MeshNumSamplingRegionTrianglesName + Suffix;
	Names.MeshNumSamplingRegionVerticesName = UNiagaraDataInterfaceSkeletalMesh::MeshNumSamplingRegionVerticesName + Suffix;
	Names.MeshSamplingRegionsProbaBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshSamplingRegionsProbaBufferName + Suffix;
	Names.MeshSamplingRegionsAliasBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshSamplingRegionsAliasBufferName + Suffix;
	Names.MeshSampleRegionsTriangleIndicesName = UNiagaraDataInterfaceSkeletalMesh::MeshSampleRegionsTriangleIndicesName + Suffix;
	Names.MeshSampleRegionsVerticesName = UNiagaraDataInterfaceSkeletalMesh::MeshSampleRegionsVerticesName + Suffix;
	Names.MeshTriangleMatricesOffsetBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleMatricesOffsetBufferName + Suffix;
	Names.MeshTriangleCountName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleCountName + Suffix;
	Names.MeshVertexCountName = UNiagaraDataInterfaceSkeletalMesh::MeshVertexCountName + Suffix;
	Names.MeshWeightStrideName = UNiagaraDataInterfaceSkeletalMesh::MeshWeightStrideName + Suffix;
	Names.MeshSkinWeightIndexSizeName = UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightIndexSizeName + Suffix;
	Names.MeshNumTexCoordName = UNiagaraDataInterfaceSkeletalMesh::MeshNumTexCoordName + Suffix;
	Names.MeshNumWeightsName = UNiagaraDataInterfaceSkeletalMesh::MeshNumWeightsName + Suffix;
	Names.NumBonesName = UNiagaraDataInterfaceSkeletalMesh::NumBonesName + Suffix;
	Names.NumFilteredBonesName = UNiagaraDataInterfaceSkeletalMesh::NumFilteredBonesName + Suffix;
	Names.NumUnfilteredBonesName = UNiagaraDataInterfaceSkeletalMesh::NumUnfilteredBonesName + Suffix;
	Names.RandomMaxBoneName = UNiagaraDataInterfaceSkeletalMesh::RandomMaxBoneName + Suffix;
	Names.ExcludeBoneIndexName = UNiagaraDataInterfaceSkeletalMesh::ExcludeBoneIndexName + Suffix;
	Names.FilteredAndUnfilteredBonesName = UNiagaraDataInterfaceSkeletalMesh::FilteredAndUnfilteredBonesName + Suffix;
	Names.NumFilteredSocketsName = UNiagaraDataInterfaceSkeletalMesh::NumFilteredSocketsName + Suffix;
	Names.FilteredSocketBoneOffsetName = UNiagaraDataInterfaceSkeletalMesh::FilteredSocketBoneOffsetName + Suffix;
	Names.InstanceTransformName = UNiagaraDataInterfaceSkeletalMesh::InstanceTransformName + Suffix;
	Names.InstancePrevTransformName = UNiagaraDataInterfaceSkeletalMesh::InstancePrevTransformName + Suffix;
	Names.InstanceRotationName = UNiagaraDataInterfaceSkeletalMesh::InstanceRotationName + Suffix;
	Names.InstancePrevRotationName = UNiagaraDataInterfaceSkeletalMesh::InstancePrevRotationName + Suffix;
	Names.InstanceInvDeltaTimeName = UNiagaraDataInterfaceSkeletalMesh::InstanceInvDeltaTimeName + Suffix;
	Names.EnabledFeaturesName = UNiagaraDataInterfaceSkeletalMesh::EnabledFeaturesName + Suffix;
}

struct FNiagaraDataInterfaceParametersCS_SkeletalMesh : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_SkeletalMesh, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDISkeletalMeshParametersName ParamNames;
		GetNiagaraDataInterfaceParametersName(ParamNames, ParameterInfo.DataInterfaceHLSLSymbol);

		MeshIndexBuffer.Bind(ParameterMap, *ParamNames.MeshIndexBufferName);
		MeshVertexBuffer.Bind(ParameterMap, *ParamNames.MeshVertexBufferName);
		MeshSkinWeightBuffer.Bind(ParameterMap, *ParamNames.MeshSkinWeightBufferName);
		MeshSkinWeightLookupBuffer.Bind(ParameterMap, *ParamNames.MeshSkinWeightLookupBufferName);
		MeshCurrBonesBuffer.Bind(ParameterMap, *ParamNames.MeshCurrBonesBufferName);
		MeshPrevBonesBuffer.Bind(ParameterMap, *ParamNames.MeshPrevBonesBufferName);
		MeshCurrSamplingBonesBuffer.Bind(ParameterMap, *ParamNames.MeshCurrSamplingBonesBufferName);
		MeshPrevSamplingBonesBuffer.Bind(ParameterMap, *ParamNames.MeshPrevSamplingBonesBufferName);
		MeshTangentBuffer.Bind(ParameterMap, *ParamNames.MeshTangentBufferName);
		MeshTexCoordBuffer.Bind(ParameterMap, *ParamNames.MeshTexCoordBufferName);
		MeshColorBuffer.Bind(ParameterMap, *ParamNames.MeshColorBufferName);
		MeshTriangleSamplerProbaBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleSamplerProbaBufferName);
		MeshTriangleSamplerAliasBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleSamplerAliasBufferName);
		MeshNumSamplingRegionTriangles.Bind(ParameterMap, *ParamNames.MeshNumSamplingRegionTrianglesName);
		MeshNumSamplingRegionVertices.Bind(ParameterMap, *ParamNames.MeshNumSamplingRegionVerticesName);
		MeshSamplingRegionsProbaBuffer.Bind(ParameterMap, *ParamNames.MeshSamplingRegionsProbaBufferName);
		MeshSamplingRegionsAliasBuffer.Bind(ParameterMap, *ParamNames.MeshSamplingRegionsAliasBufferName);
		MeshSampleRegionsTriangleIndices.Bind(ParameterMap, *ParamNames.MeshSampleRegionsTriangleIndicesName);
		MeshSampleRegionsVertices.Bind(ParameterMap, *ParamNames.MeshSampleRegionsVerticesName);
		MeshTriangleMatricesOffsetBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleMatricesOffsetBufferName);
		MeshTriangleCount.Bind(ParameterMap, *ParamNames.MeshTriangleCountName);
		MeshVertexCount.Bind(ParameterMap, *ParamNames.MeshVertexCountName);
		MeshWeightStride.Bind(ParameterMap, *ParamNames.MeshWeightStrideName);
		MeshSkinWeightIndexSize.Bind(ParameterMap, *ParamNames.MeshSkinWeightIndexSizeName);
		MeshNumTexCoord.Bind(ParameterMap, *ParamNames.MeshNumTexCoordName);
		MeshNumWeights.Bind(ParameterMap, *ParamNames.MeshNumWeightsName);
		NumBones.Bind(ParameterMap, *ParamNames.NumBonesName);
		NumFilteredBones.Bind(ParameterMap, *ParamNames.NumFilteredBonesName);
		NumUnfilteredBones.Bind(ParameterMap, *ParamNames.NumUnfilteredBonesName);
		RandomMaxBone.Bind(ParameterMap, *ParamNames.RandomMaxBoneName);
		ExcludeBoneIndex.Bind(ParameterMap, *ParamNames.ExcludeBoneIndexName);
		FilteredAndUnfilteredBones.Bind(ParameterMap, *ParamNames.FilteredAndUnfilteredBonesName);
		NumFilteredSockets.Bind(ParameterMap, *ParamNames.NumFilteredSocketsName);
		FilteredSocketBoneOffset.Bind(ParameterMap, *ParamNames.FilteredSocketBoneOffsetName);
		InstanceTransform.Bind(ParameterMap, *ParamNames.InstanceTransformName);
		InstancePrevTransform.Bind(ParameterMap, *ParamNames.InstancePrevTransformName);
		InstanceRotation.Bind(ParameterMap, *ParamNames.InstanceRotationName);
		InstancePrevRotation.Bind(ParameterMap, *ParamNames.InstancePrevRotationName);
		InstanceInvDeltaTime.Bind(ParameterMap, *ParamNames.InstanceInvDeltaTimeName);
		EnabledFeatures.Bind(ParameterMap, *ParamNames.EnabledFeaturesName);
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxySkeletalMesh* InterfaceProxy = static_cast<FNiagaraDataInterfaceProxySkeletalMesh*>(Context.DataInterface);
		FNiagaraDataInterfaceProxySkeletalMeshData* InstanceData = InterfaceProxy->SystemInstancesToData.Find(Context.SystemInstance);
		if (InstanceData && InstanceData->StaticBuffers)
		{
			FSkeletalMeshGpuSpawnStaticBuffers* StaticBuffers = InstanceData->StaticBuffers;

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, StaticBuffers->GetBufferPositionSRV());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, StaticBuffers->GetBufferIndexSRV());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, StaticBuffers->GetBufferTangentSRV());

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumTexCoord, StaticBuffers->GetNumTexCoord());
			if (StaticBuffers->GetNumTexCoord() > 0)
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, StaticBuffers->GetBufferTexCoordSRV());
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			}
			if (StaticBuffers->GetBufferColorSRV())
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, StaticBuffers->GetBufferColorSRV());
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			}
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshTriangleCount, StaticBuffers->GetTriangleCount());
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshVertexCount, StaticBuffers->GetVertexCount());

			// Set triangle sampling buffer
			if (InstanceData->bIsGpuUniformlyDistributedSampling)
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, StaticBuffers->GetBufferTriangleUniformSamplerProbaSRV().GetReference());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, StaticBuffers->GetBufferTriangleUniformSamplerAliasSRV().GetReference());
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			}

			// Set triangle sampling region buffer
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumSamplingRegionTriangles, StaticBuffers->GetNumSamplingRegionTriangles());
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumSamplingRegionVertices, StaticBuffers->GetNumSamplingRegionVertices());
			if (StaticBuffers->IsSamplingRegionsAllAreaWeighted() && StaticBuffers->GetNumSamplingRegionTriangles() > 0)
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSamplingRegionsProbaBuffer, StaticBuffers->GetSampleRegionsProbSRV().GetReference());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSamplingRegionsAliasBuffer, StaticBuffers->GetSampleRegionsAliasSRV().GetReference());
			}
			else
			{
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSamplingRegionsProbaBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSamplingRegionsAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			}
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSampleRegionsTriangleIndices, StaticBuffers->GetNumSamplingRegionTriangles() > 0 ? StaticBuffers->GetSampleRegionsTriangleIndicesSRV().GetReference() : FNiagaraRenderer::GetDummyUIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSampleRegionsVertices, StaticBuffers->GetNumSamplingRegionVertices() > 0 ? StaticBuffers->GetSampleRegionsVerticesSRV().GetReference() : FNiagaraRenderer::GetDummyUIntBuffer());

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightBuffer, InstanceData->MeshSkinWeightBuffer->GetSRV());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightLookupBuffer, InstanceData->MeshSkinWeightLookupBuffer->GetSRV());

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshWeightStride, InstanceData->MeshWeightStrideByte/4);

			uint32 EnabledFeaturesBits = 0;
			EnabledFeaturesBits |= InstanceData->bIsGpuUniformlyDistributedSampling ? 1 : 0;
			EnabledFeaturesBits |= StaticBuffers->IsSamplingRegionsAllAreaWeighted() ? 2 : 0;
			EnabledFeaturesBits |= (InstanceData->bUnlimitedBoneInfluences ? 4 : 0);

			FSkeletalMeshGpuDynamicBufferProxy* DynamicBuffers = InstanceData->DynamicBuffer;
			check(DynamicBuffers);
			if(DynamicBuffers->DoesBoneDataExist())
			{
				SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumWeights, StaticBuffers->GetNumWeights());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, DynamicBuffers->GetRWBufferBone().SectionSRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, DynamicBuffers->GetRWBufferPrevBone().SectionSRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrSamplingBonesBuffer, DynamicBuffers->GetRWBufferBone().SamplingSRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevSamplingBonesBuffer, DynamicBuffers->GetRWBufferPrevBone().SamplingSRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, StaticBuffers->GetBufferTriangleMatricesOffsetSRV());
			}
			// Bind dummy data for validation purposes only.  Code will not execute due to "EnabledFeatures" bits but validation can not determine that.
			else
			{
				SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumWeights, 0);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			}

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumBones, DynamicBuffers->GetNumBones());

			FRHIShaderResourceView* FilteredAndUnfilteredBonesSRV = StaticBuffers->GetNumFilteredBones() > 0 ? StaticBuffers->GetFilteredAndUnfilteredBonesSRV() : FNiagaraRenderer::GetDummyUIntBuffer();
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumFilteredBones, StaticBuffers->GetNumFilteredBones());
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumUnfilteredBones, StaticBuffers->GetNumUnfilteredBones());
			SetShaderValue(RHICmdList, ComputeShaderRHI, RandomMaxBone, StaticBuffers->GetExcludedBoneIndex() >= 0 ? DynamicBuffers->GetNumBones() - 2 : DynamicBuffers->GetNumBones() - 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ExcludeBoneIndex, StaticBuffers->GetExcludedBoneIndex());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FilteredAndUnfilteredBones, FilteredAndUnfilteredBonesSRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumFilteredSockets, StaticBuffers->GetNumFilteredSockets());
			SetShaderValue(RHICmdList, ComputeShaderRHI, FilteredSocketBoneOffset, StaticBuffers->GetFilteredSocketBoneOffset());

			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, InstanceData->Transform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, InstanceData->PrevTransform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceRotation, InstanceData->Transform.ToQuat());
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevRotation, InstanceData->PrevTransform.ToQuat());
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 1.0f / InstanceData->DeltaSeconds);

			SetShaderValue(RHICmdList, ComputeShaderRHI, EnabledFeatures, EnabledFeaturesBits);
		}
		else
		{
			// Bind dummy buffers
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, FNiagaraRenderer::GetDummyFloatBuffer());

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumTexCoord, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshTriangleCount, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshVertexCount, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer());

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumSamplingRegionTriangles, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumSamplingRegionVertices, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSamplingRegionsProbaBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSamplingRegionsAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSampleRegionsTriangleIndices, FNiagaraRenderer::GetDummyUIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSampleRegionsVertices, FNiagaraRenderer::GetDummyUIntBuffer());

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightLookupBuffer, FNiagaraRenderer::GetDummyUIntBuffer());

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshWeightStride, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshSkinWeightIndexSize, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumWeights, 0);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, FNiagaraRenderer::GetDummyUIntBuffer());

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumBones, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumFilteredBones, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumUnfilteredBones, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RandomMaxBone, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, ExcludeBoneIndex, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, FilteredAndUnfilteredBones, FNiagaraRenderer::GetDummyUIntBuffer());
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumFilteredSockets, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, FilteredSocketBoneOffset, 0);

			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceRotation, FQuat::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevRotation, FQuat::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 0.0f);

			SetShaderValue(RHICmdList, ComputeShaderRHI, EnabledFeatures, 0);
		}
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, MeshIndexBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshVertexBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSkinWeightBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSkinWeightLookupBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshCurrBonesBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshPrevBonesBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshCurrSamplingBonesBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshPrevSamplingBonesBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTangentBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTexCoordBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshColorBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTriangleSamplerProbaBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTriangleSamplerAliasBuffer);
	LAYOUT_FIELD(FShaderParameter, MeshNumSamplingRegionTriangles);
	LAYOUT_FIELD(FShaderParameter, MeshNumSamplingRegionVertices);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSamplingRegionsProbaBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSamplingRegionsAliasBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSampleRegionsTriangleIndices);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSampleRegionsVertices);
	LAYOUT_FIELD(FShaderResourceParameter, MeshTriangleMatricesOffsetBuffer);
	LAYOUT_FIELD(FShaderParameter, MeshTriangleCount);
	LAYOUT_FIELD(FShaderParameter, MeshVertexCount);
	LAYOUT_FIELD(FShaderParameter, MeshWeightStride);
	LAYOUT_FIELD(FShaderParameter, MeshSkinWeightIndexSize);
	LAYOUT_FIELD(FShaderParameter, MeshNumTexCoord);
	LAYOUT_FIELD(FShaderParameter, MeshNumWeights);
	LAYOUT_FIELD(FShaderParameter, NumBones);
	LAYOUT_FIELD(FShaderParameter, NumFilteredBones);
	LAYOUT_FIELD(FShaderParameter, NumUnfilteredBones);
	LAYOUT_FIELD(FShaderParameter, RandomMaxBone);
	LAYOUT_FIELD(FShaderParameter, ExcludeBoneIndex);
	LAYOUT_FIELD(FShaderResourceParameter, FilteredAndUnfilteredBones);
	LAYOUT_FIELD(FShaderParameter, NumFilteredSockets);
	LAYOUT_FIELD(FShaderParameter, FilteredSocketBoneOffset);
	LAYOUT_FIELD(FShaderParameter, InstanceTransform);
	LAYOUT_FIELD(FShaderParameter, InstancePrevTransform);
	LAYOUT_FIELD(FShaderParameter, InstanceRotation);
	LAYOUT_FIELD(FShaderParameter, InstancePrevRotation);
	LAYOUT_FIELD(FShaderParameter, InstanceInvDeltaTime);
	LAYOUT_FIELD(FShaderParameter, EnabledFeatures);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_SkeletalMesh);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceSkeletalMesh, FNiagaraDataInterfaceParametersCS_SkeletalMesh);

//////////////////////////////////////////////////////////////////////////

void FNiagaraDataInterfaceProxySkeletalMesh::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNiagaraDISkeletalMeshPassedDataToRT* SourceData = static_cast<FNiagaraDISkeletalMeshPassedDataToRT*>(PerInstanceData);

	FNiagaraDataInterfaceProxySkeletalMeshData& Data = SystemInstancesToData.FindOrAdd(Instance);

	Data.bIsGpuUniformlyDistributedSampling = SourceData->bIsGpuUniformlyDistributedSampling;
	Data.bUnlimitedBoneInfluences = SourceData->bUnlimitedBoneInfluences;
	Data.DeltaSeconds = SourceData->DeltaSeconds;
	Data.DynamicBuffer = SourceData->DynamicBuffer;
	Data.MeshWeightStrideByte = SourceData->MeshWeightStrideByte;
	Data.MeshSkinWeightIndexSizeByte = SourceData->MeshSkinWeightIndexSizeByte;
	Data.PrevTransform = SourceData->PrevTransform;
	Data.StaticBuffers = SourceData->StaticBuffers;
	Data.Transform = SourceData->Transform;

	Data.MeshSkinWeightBuffer = SourceData->MeshSkinWeightBuffer;
	Data.MeshSkinWeightLookupBuffer = SourceData->MeshSkinWeightLookupBuffer;
}

//////////////////////////////////////////////////////////////////////////
//FNDISkeletalMesh_InstanceData

void UNiagaraDataInterfaceSkeletalMesh::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNiagaraDISkeletalMeshPassedDataToRT* Data = static_cast<FNiagaraDISkeletalMeshPassedDataToRT*>(DataForRenderThread);
	FNDISkeletalMesh_InstanceData* SourceData = static_cast<FNDISkeletalMesh_InstanceData*>(PerInstanceData);

	Data->bIsGpuUniformlyDistributedSampling = SourceData->bIsGpuUniformlyDistributedSampling;
	Data->bUnlimitedBoneInfluences = SourceData->bUnlimitedBoneInfluences;
	Data->DeltaSeconds = SourceData->DeltaSeconds;
	Data->DynamicBuffer = SourceData->MeshGpuSpawnDynamicBuffers;
	Data->MeshWeightStrideByte = SourceData->MeshWeightStrideByte;
	Data->MeshSkinWeightIndexSizeByte = SourceData->MeshSkinWeightIndexSizeByte;
	Data->PrevTransform = SourceData->PrevTransform;
	Data->StaticBuffers = SourceData->MeshGpuSpawnStaticBuffers;
	Data->Transform = SourceData->Transform;

	Data->MeshSkinWeightBuffer = SourceData->MeshSkinWeightBuffer;
	Data->MeshSkinWeightLookupBuffer = SourceData->MeshSkinWeightLookupBuffer;
}

USkeletalMesh* UNiagaraDataInterfaceSkeletalMesh::GetSkeletalMesh(UNiagaraComponent* OwningComponent, TWeakObjectPtr<USceneComponent>& SceneComponent, USkeletalMeshComponent*& FoundSkelComp, FNDISkeletalMesh_InstanceData* InstData)
{
	USkeletalMesh* Mesh = nullptr;
	if (MeshUserParameter.Parameter.IsValid() && InstData)
	{
		FNiagaraSystemInstance* SystemInstance = OwningComponent->GetSystemInstance();
		if (UObject* UserParamObject = InstData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), MeshUserParameter.Parameter))
		{
			InstData->CachedUserParam = UserParamObject;
			if (USkeletalMeshComponent* UserSkelMeshComp = Cast<USkeletalMeshComponent>(UserParamObject))
			{
				FoundSkelComp = UserSkelMeshComp;
				Mesh = FoundSkelComp->SkeletalMesh;
			}
			else if (ASkeletalMeshActor* UserSkelMeshActor = Cast<ASkeletalMeshActor>(UserParamObject))
			{
				FoundSkelComp = UserSkelMeshActor->GetSkeletalMeshComponent();
				Mesh = FoundSkelComp->SkeletalMesh;
			}
			else if (AActor* Actor = Cast<AActor>(UserParamObject))
			{
				for (UActorComponent* ActorComp : Actor->GetComponents())
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
			else
			{
				//We have a valid, non-null UObject parameter type but it is not a type we can use to get a skeletal mesh from. 
				UE_LOG(LogNiagara, Warning, TEXT("SkeletalMesh data interface using object parameter with invalid type. Skeletal Mesh Data Interfaces can only get a valid mesh from SkeletalMeshComponents, SkeletalMeshActors or Actors."));
				UE_LOG(LogNiagara, Warning, TEXT("Invalid Parameter : %s"), *UserParamObject->GetFullName());
				UE_LOG(LogNiagara, Warning, TEXT("Niagara Component : %s"), *OwningComponent->GetFullName());
				UE_LOG(LogNiagara, Warning, TEXT("System : %s"), *OwningComponent->GetAsset()->GetFullName());
			}
		}
		else
		{
			//WARNING - We have a valid user param but the object set is null.
		}
	}
	else if (SourceComponent)
	{
		Mesh = SourceComponent->SkeletalMesh;
		FoundSkelComp = SourceComponent;
	}
	else if (Source)
	{
		ASkeletalMeshActor* MeshActor = Cast<ASkeletalMeshActor>(Source);
		USkeletalMeshComponent* SourceComp = nullptr;
		if (MeshActor != nullptr)
		{
			SourceComp = MeshActor->GetSkeletalMeshComponent();
		}
		else
		{
			SourceComp = Source->FindComponentByClass<USkeletalMeshComponent>();
		}

		if (SourceComp)
		{
			Mesh = SourceComp->SkeletalMesh;
			FoundSkelComp = SourceComp;
		}
		else
		{
			SceneComponent = Source->GetRootComponent();
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
				for (UActorComponent* ActorComp : Owner->GetComponents())
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

#if WITH_EDITORONLY_DATA
	if (!Mesh && PreviewMesh)
	{
		Mesh = PreviewMesh;
	}
#endif

	return Mesh;
}


bool FNDISkeletalMesh_InstanceData::Init(UNiagaraDataInterfaceSkeletalMesh* Interface, FNiagaraSystemInstance* SystemInstance)
{
	check(Interface);
	check(SystemInstance);

	// Initialize members
	Component = nullptr;
	CachedAttachParent = nullptr;
	Mesh = nullptr;
	MeshSafe = nullptr;
	Transform = FMatrix::Identity;
	TransformInverseTransposed = FMatrix::Identity;
	PrevTransform = FMatrix::Identity;
	DeltaSeconds = SystemInstance->GetComponent()->GetWorld()->GetDeltaSeconds();
	ChangeId = Interface->ChangeId;
	bIsGpuUniformlyDistributedSampling = false;
	bUnlimitedBoneInfluences = false;
	MeshWeightStrideByte = 0;
	MeshSkinWeightIndexSizeByte = 0;
	MeshGpuSpawnStaticBuffers = nullptr;
	MeshGpuSpawnDynamicBuffers = nullptr;
	bAllowCPUMeshDataAccess = false;

	// Get skel mesh and confirm have valid data
	USkeletalMeshComponent* NewSkelComp = nullptr;
	Mesh = Interface->GetSkeletalMesh(SystemInstance->GetComponent(), Component, NewSkelComp, this);
	MeshSafe = Mesh;

	if (!Component.IsValid())
	{
		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface has no valid component. Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

	Transform = Component->GetComponentToWorld().ToMatrixWithScale();
	TransformInverseTransposed = Transform.Inverse().GetTransposed();
	PrevTransform = Transform;
	
	CachedAttachParent = Component->GetAttachParent();

#if WITH_EDITOR
	if (MeshSafe.IsValid())
	{
		MeshSafe->GetOnMeshChanged().AddUObject(SystemInstance->GetComponent(), &UNiagaraComponent::ReinitializeSystem);
	}
#endif

	// Get the first LOD level we can guarantee to be in memory
	//-TODO: Support skeletal mesh streaming
	const FSkeletalMeshRenderData* SkelMeshRenderData = Mesh->GetResourceForRendering();
	check(SkelMeshRenderData != nullptr);
	const int32 FirstInlineLODLevel = SkelMeshRenderData->LODRenderData.Num() - SkelMeshRenderData->NumInlinedLODs;

	//Setup where to spawn from
	SamplingRegionIndices.Empty();
	bool bAllRegionsAreAreaWeighting = true;
	int32 LODIndex = INDEX_NONE;
	if (Mesh == nullptr)
	{
		// Just say we're sampling LOD 0, even though there are no LODs
		LODIndex = 0;
	}
	else if (Interface->SamplingRegions.Num() == 0)
	{
		//If we have no regions, sample the whole mesh at the specified LOD.
		LODIndex = Interface->WholeMeshLOD;
		if (LODIndex == INDEX_NONE)
		{
			LODIndex = Mesh->GetLODNum() - 1;
		}
		else
		{
			LODIndex = FMath::Clamp(LODIndex, FirstInlineLODLevel, Mesh->GetLODNum() - 1);
		}
	}
	else
	{
		//Sampling from regions. Gather the indices of the regions we'll sample from.
		const FSkeletalMeshSamplingInfo& SamplingInfo = Mesh->GetSamplingInfo();
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

				// Ensure we aren't using a region from a streaming / culled LOD level
				if (LODIndex < FirstInlineLODLevel)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use regions on LODs levels that are either streamed or cooked out. This is currently unsupported.\nInterface: %s\nMesh: %s\nRegion: %s"),
						*Interface->GetFullName(),
						*Mesh->GetFullName(),
						*RegionName.ToString());

					return false;
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
	const ENDISkeletalMesh_SkinningMode SkinningMode = Interface->SkinningMode;
	FSkeletalMeshSkinningDataUsage Usage(
		LODIndex,
		SkinningMode == ENDISkeletalMesh_SkinningMode::SkinOnTheFly || SkinningMode == ENDISkeletalMesh_SkinningMode::PreSkin,
		SkinningMode == ENDISkeletalMesh_SkinningMode::PreSkin,
		bNeedDataImmediately);

	if (NewSkelComp)
	{
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


	if (Mesh)
	{
		bAllowCPUMeshDataAccess = true; // Assume accessibility until proven otherwise below
		FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
		FSkeletalMeshLODRenderData* LODData = GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
		check(LODData);
		check(SkinWeightBuffer);

		// Check for the validity of the Mesh's cpu data.
		if (Mesh->GetLODInfo(LODIndex)->bAllowCPUAccess)
		{
			const bool LODDataNumVerticesCorrect = LODData->GetNumVertices() > 0;
			const bool LODDataPositonNumVerticesCorrect = LODData->StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() > 0;
			const bool SkinWeightBufferNumVerticesCorrect = SkinWeightBuffer->GetNumVertices() > 0;
			const bool bIndexBufferValid = LODData->MultiSizeIndexContainer.IsIndexBufferValid();
			const bool bIndexBufferFound = bIndexBufferValid && (LODData->MultiSizeIndexContainer.GetIndexBuffer() != nullptr);
			const bool bIndexBufferNumCorrect = bIndexBufferFound && (LODData->MultiSizeIndexContainer.GetIndexBuffer()->Num() > 0);

			bAllowCPUMeshDataAccess = LODDataNumVerticesCorrect &&
				LODDataPositonNumVerticesCorrect &&
				SkinWeightBufferNumVerticesCorrect &&
				bIndexBufferValid &&
				bIndexBufferFound &&
				bIndexBufferNumCorrect;
		}
		else
		{
			bAllowCPUMeshDataAccess = false;
		}

		// Generate excluded root bone index (if any)
		FReferenceSkeleton& RefSkel = Mesh->RefSkeleton;
		ExcludedBoneIndex = INDEX_NONE;
		if (Interface->bExcludeBone && !Interface->ExcludeBoneName.IsNone())
		{
			ExcludedBoneIndex = RefSkel.FindBoneIndex(Interface->ExcludeBoneName);
			if (ExcludedBoneIndex == INDEX_NONE)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface '%s' is missing bone '%s' this is ok but may not exclude what you want Mesh '%s' Component '%s'"), *Interface->GetFullName(), *Interface->ExcludeBoneName.ToString(), *Mesh->GetFullName(), *Component->GetFullName());
			}
		}

		// Gather filtered bones information
		if (Interface->FilteredBones.Num() > 0)
		{
			if (RefSkel.GetNum() > TNumericLimits<uint16>::Max())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface '%s' requires more bones '%d' than we currently support '%d' Mesh '%s' Component '%s'"), *Interface->GetFullName(), RefSkel.GetNum(), TNumericLimits<uint16>::Max(), *Mesh->GetFullName(), *Component->GetFullName());
				return false;
			}

			//-TODO: If the DI does not use unfiltered bones we can skip adding them here...
			TArray<FName, TInlineAllocator<16>> MissingBones;

			FilteredAndUnfilteredBones.Reserve(RefSkel.GetNum());

			// Append filtered bones to the array first
			for (const FName& BoneName : Interface->FilteredBones)
			{
				const int32 Bone = RefSkel.FindBoneIndex(BoneName);
				if (Bone == INDEX_NONE)
				{
					MissingBones.Add(BoneName);
				}
				else
				{
					ensure(Bone <= TNumericLimits<uint16>::Max());
					FilteredAndUnfilteredBones.Add(Bone);
					++NumFilteredBones;
				}
			}

			// Append unfiltered bones to the array
			for (int32 i = 0; i < RefSkel.GetNum(); ++i)
			{
				// Don't append excluded bone
				if (i == ExcludedBoneIndex)
				{
					continue;
				}

				bool bExists = false;
				for (int32 j = 0; j < NumFilteredBones; ++j)
				{
					if (FilteredAndUnfilteredBones[j] == i)
					{
						bExists = true;
						break;
					}
				}
				if (!bExists)
				{
					FilteredAndUnfilteredBones.Add(i);
					++NumUnfilteredBones;
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
		}
		else
		{
			// Note: We do not allocate space in the array as that wastes memory, we handle this special case when reading from unfiltered
			NumUnfilteredBones = RefSkel.GetNum();
		}

		// Gather filtered socket information
		{
			TArray<FName>& FilteredSockets = Interface->FilteredSockets;
			FilteredSocketInfo.SetNum(FilteredSockets.Num());

			//-TODO: We may need to handle skinning mode changes here
			if (NewSkelComp != nullptr)
			{
				for (int32 i = 0; i < FilteredSocketInfo.Num(); ++i)
				{
					NewSkelComp->GetSocketInfoByName(FilteredSockets[i], FilteredSocketInfo[i].Transform, FilteredSocketInfo[i].BoneIdx);
				}
			}
			else
			{
				for (int32 i = 0; i < FilteredSocketInfo.Num(); ++i)
				{
					FilteredSocketInfo[i].Transform = FTransform(Mesh->GetComposedRefPoseMatrix(FilteredSockets[i]));
					FilteredSocketInfo[i].BoneIdx = INDEX_NONE;
				}
			}

			FilteredSocketBoneOffset = Mesh->RefSkeleton.GetNum();

			FilteredSocketTransformsIndex = 0;
			FilteredSocketTransforms[0].Reset(FilteredSockets.Num());
			FilteredSocketTransforms[0].AddDefaulted(FilteredSockets.Num());
			UpdateFilteredSocketTransforms();
			for (int32 i = 1; i < FilteredSocketTransforms.Num(); ++i)
			{
				FilteredSocketTransforms[i].Reset(FilteredSockets.Num());
				FilteredSocketTransforms[i].Append(FilteredSocketTransforms[0]);
			}

			TArray<FName, TInlineAllocator<16>> MissingSockets;
			for (FName SocketName : FilteredSockets)
			{
				if (Mesh->FindSocket(SocketName) == nullptr)
				{
					MissingSockets.Add(SocketName);
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
		}

		//-TODO: We should find out if this DI is connected to a GPU emitter or not rather than a blanket accross the system
		if (SystemInstance->HasGPUEmitters())
		{
			GPUSkinBoneInfluenceType BoneInfluenceType = SkinWeightBuffer->GetBoneInfluenceType();
			bUnlimitedBoneInfluences = (BoneInfluenceType == GPUSkinBoneInfluenceType::UnlimitedBoneInfluence);
			MeshWeightStrideByte = SkinWeightBuffer->GetConstantInfluencesVertexStride();
			MeshSkinWeightIndexSizeByte = SkinWeightBuffer->GetBoneIndexByteSize();
			MeshSkinWeightBuffer = SkinWeightBuffer->GetDataVertexBuffer();
			//check(MeshSkinWeightBufferSrv->IsValid()); // not available in this stream
			MeshSkinWeightLookupBuffer = SkinWeightBuffer->GetLookupVertexBuffer();

			FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex);
			bIsGpuUniformlyDistributedSampling = LODInfo->bSupportUniformlyDistributedSampling && bAllRegionsAreAreaWeighting;

			if (Mesh->HasActiveClothingAssets())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh %s has cloth asset on it: spawning from it might not work properly."), *Mesh->GetName());
			}
			if (LODData->GetVertexBufferMaxBoneInfluences() > MAX_INFLUENCES_PER_STREAM)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh %s has bones extra influence: spawning from it might not work properly."), *Mesh->GetName());
			}

			const FSkeletalMeshSamplingInfo& SamplingInfo = Mesh->GetSamplingInfo();
			MeshGpuSpawnStaticBuffers = new FSkeletalMeshGpuSpawnStaticBuffers();
			MeshGpuSpawnStaticBuffers->Initialise(this, *LODData, SamplingInfo.GetBuiltData().WholeMeshBuiltData[LODIndex]);
			BeginInitResource(MeshGpuSpawnStaticBuffers);

			MeshGpuSpawnDynamicBuffers = new FSkeletalMeshGpuDynamicBufferProxy();
			MeshGpuSpawnDynamicBuffers->Initialise(RefSkel, *LODData, FilteredSocketInfo.Num());
			BeginInitResource(MeshGpuSpawnDynamicBuffers);
		}
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

	//Detect and reset on any attachment change.
	if (CachedAttachParent.IsValid() && Comp->GetAttachParent() != CachedAttachParent.Get())
	{
		return true;
	}
	
	if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Comp))
	{
		if (!SkelComp->SkeletalMesh)//TODO: Handle clearing the mesh gracefully.
		{
			return true;
		}

		//If the user ptr has been changed to look at a new mesh component. TODO: Handle more gracefully.
		if (Interface->MeshUserParameter.Parameter.IsValid())
		{
			UObject* NewUserParam = UserParamBinding.GetValue();
			if (CachedUserParam != NewUserParam)
			{
				return true;
			}
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

		if (Component.IsValid())
		{
			PrevTransform = Transform;
			Transform = Component->GetComponentToWorld().ToMatrixWithScale();
			TransformInverseTransposed = Transform.Inverse().GetTransposed();
		}
		else
		{
			PrevTransform = FMatrix::Identity;
			Transform = FMatrix::Identity;
			TransformInverseTransposed = FMatrix::Identity;
		}

		// Cache socket transforms to avoid potentially calculating them multiple times during the VM
		FilteredSocketTransformsIndex = (FilteredSocketTransformsIndex + 1) % FilteredSocketTransforms.Num();
		UpdateFilteredSocketTransforms();

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

void FNDISkeletalMesh_InstanceData::UpdateFilteredSocketTransforms()
{
	USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component.Get());
	TArray<FTransform>& WriteBuffer = GetFilteredSocketsWriteBuffer();

	for (int32 i = 0; i < FilteredSocketInfo.Num(); ++i)
	{
		const FCachedSocketInfo& SocketInfo = FilteredSocketInfo[i];
		const FTransform& BoneTransform = SocketInfo.BoneIdx != INDEX_NONE ? SkelComp->GetBoneTransform(SocketInfo.BoneIdx, FTransform::Identity) : FTransform::Identity;
		WriteBuffer[i] = SocketInfo.Transform * BoneTransform;
	}
}

bool FNDISkeletalMesh_InstanceData::HasColorData()
{
	FSkinWeightVertexBuffer* SkinWeightBuffer;
	FSkeletalMeshLODRenderData* LODData = GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	return LODData && LODData->StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() != 0;
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
#if WITH_EDITORONLY_DATA
	, PreviewMesh(nullptr)
#endif
	, Source(nullptr)
	, SkinningMode(ENDISkeletalMesh_SkinningMode::SkinOnTheFly)
	, WholeMeshLOD(INDEX_NONE)
	, ChangeId(0)
{

	FNiagaraTypeDefinition Def(UObject::StaticClass());
	MeshUserParameter.Parameter.SetType(Def);

	static const FName NAME_Root("root");
	ExcludeBoneName = NAME_Root;
	bExcludeBone = false;

	Proxy.Reset(new FNiagaraDataInterfaceProxySkeletalMesh());
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
	ChangeId++;
}

#endif //WITH_EDITOR

void UNiagaraDataInterfaceSkeletalMesh::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	GetTriangleSamplingFunctions(OutFunctions);
	GetVertexSamplingFunctions(OutFunctions);
	GetSkeletonSamplingFunctions(OutFunctions);

#if WITH_EDITORONLY_DATA
	for (FNiagaraFunctionSignature& Function : OutFunctions)
	{
		Function.FunctionVersion = FNiagaraSkelMeshDIFunctionVersion::LatestVersion;
	}
#endif
}

void UNiagaraDataInterfaceSkeletalMesh::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	FNDISkeletalMesh_InstanceData* InstData = (FNDISkeletalMesh_InstanceData*)InstanceData;
	USkeletalMeshComponent* SkelComp = InstData != nullptr ? Cast<USkeletalMeshComponent>(InstData->Component.Get()) : nullptr;
	
	if (!InstData)
	{
		OutFunc = FVMExternalFunction();
		return;
	}

	// Bind skeleton sampling function
	BindSkeletonSamplingFunction(BindingInfo, InstData, OutFunc);
	if (OutFunc.IsBound())
	{
		return;
	}

	// Bind triangle sampling function
	BindTriangleSamplingFunction(BindingInfo, InstData, OutFunc);
	if (OutFunc.IsBound())
	{
		if (!InstData->bAllowCPUMeshDataAccess)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use triangle sampling but CPU access or the data is invalid. Interface: %s"), *GetFullName());
		}
		return;
	}

	// Bind vertex sampling function
	BindVertexSamplingFunction(BindingInfo, InstData, OutFunc);
	if (OutFunc.IsBound())
	{
		if (!InstData->bAllowCPUMeshDataAccess)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use vertex sampling but CPU access or the data is invalid. Interface: %s"), *GetFullName());
		}
		return;
	}
}


bool UNiagaraDataInterfaceSkeletalMesh::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceSkeletalMesh* OtherTyped = CastChecked<UNiagaraDataInterfaceSkeletalMesh>(Destination);
	OtherTyped->Source = Source;
	OtherTyped->MeshUserParameter = MeshUserParameter;
	OtherTyped->SkinningMode = SkinningMode;
	OtherTyped->SamplingRegions = SamplingRegions;
	OtherTyped->WholeMeshLOD = WholeMeshLOD;
	OtherTyped->FilteredBones = FilteredBones;
	OtherTyped->FilteredSockets = FilteredSockets;
	OtherTyped->bExcludeBone = bExcludeBone;
	OtherTyped->ExcludeBoneName = ExcludeBoneName;
#if WITH_EDITORONLY_DATA
	OtherTyped->PreviewMesh = PreviewMesh;
#endif
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
#if WITH_EDITORONLY_DATA
		OtherTyped->PreviewMesh == PreviewMesh &&
#endif
		OtherTyped->MeshUserParameter == MeshUserParameter &&
		OtherTyped->SkinningMode == SkinningMode &&
		OtherTyped->SamplingRegions == SamplingRegions &&
		OtherTyped->WholeMeshLOD == WholeMeshLOD &&
		OtherTyped->FilteredBones == FilteredBones &&
		OtherTyped->FilteredSockets == FilteredSockets &&
		OtherTyped->bExcludeBone == bExcludeBone &&
		OtherTyped->ExcludeBoneName == ExcludeBoneName;
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
				//check(ThisProxy->Contains(InstanceID));
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
void UNiagaraDataInterfaceSkeletalMesh::GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component, TArray<FNiagaraDataInterfaceError>& OutErrors,
	TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo)
{
	if (Asset == nullptr)
	{
		return;
	}

	bool bHasCPUAccessWarning = false;
	bool bHasNoMeshAssignedError = false;
	
	// Collect Errors
#if WITH_EDITORONLY_DATA
	TWeakObjectPtr<USceneComponent> SceneComponent;
	USkeletalMeshComponent* SkelMeshComponent = nullptr;
	USkeletalMesh* SkelMesh = GetSkeletalMesh(Component, SceneComponent, SkelMeshComponent);
	if (SkelMesh != nullptr)
	{
		bool bHasCPUAccess = true;
		for (const FSkeletalMeshLODInfo& LODInfo : SkelMesh->GetLODInfoArray())
		{
			if (!LODInfo.bAllowCPUAccess)
			{
				bHasCPUAccess = false;
				break;
			}
		}

		// Check for the possibility that this mesh won't behave properly because of no CPU access
		if (!bHasCPUAccess)
		{			
			// Filter through all the relevant CPU scripts
			TArray<UNiagaraScript*> Scripts;
			Scripts.Add(Asset->GetSystemSpawnScript());
			Scripts.Add(Asset->GetSystemUpdateScript());
			for (auto&& EmitterHandle : Asset->GetEmitterHandles())
			{
				TArray<UNiagaraScript*> OutScripts;
				EmitterHandle.GetInstance()->GetScripts(OutScripts, false);
				Scripts.Append(OutScripts.FilterByPredicate(
					[&EmitterHandle](const UNiagaraScript* Script)
					{
						if (EmitterHandle.GetInstance()->SimTarget == ENiagaraSimTarget::GPUComputeSim)
						{
							// Ignore the spawn and update scripts
							if (Script->Usage == ENiagaraScriptUsage::ParticleSpawnScript ||
								Script->Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated ||
								Script->Usage == ENiagaraScriptUsage::ParticleUpdateScript)
							{
								return false;
							}
						}
						return Script->Usage != ENiagaraScriptUsage::ParticleGPUComputeScript;						
					}
				));
			}

			// Now check if any CPU script uses functions that require CPU access
			//TODO: This isn't complete enough. It doesn't guarantee that the DI used by these functions are THIS DI.
			// Finding that information out is currently non-trivial so just pop a warning with the possibility of false
			// positives
			TArray<FNiagaraFunctionSignature> CPUFunctions;
			GetTriangleSamplingFunctions(CPUFunctions);
			GetVertexSamplingFunctions(CPUFunctions);

			bHasCPUAccessWarning = [this, &Scripts, &CPUFunctions]()
			{
				for (const auto Script : Scripts)
				{
					for (const auto& DIInfo : Script->GetVMExecutableData().DataInterfaceInfo)
					{
						if (DIInfo.GetDefaultDataInterface()->GetClass() == GetClass())
						{
							for (const auto& Func : DIInfo.RegisteredFunctions)
							{
								auto Filter = [&Func](const FNiagaraFunctionSignature& CPUSig)
								{
									return CPUSig.Name == Func.Name;
								};
								if (CPUFunctions.FindByPredicate(Filter))
								{
									return true;
								}
							}
						}
					}
				}
				return false;
			}();			
		}
	}
	else
	{
		bHasNoMeshAssignedError = true;
	}

	// Report Errors/Warnings
	if (bHasCPUAccessWarning)
	{
		FNiagaraDataInterfaceFeedback CPUAccessNotAllowedWarning(FText::Format(LOCTEXT("CPUAccessNotAllowedError", "This mesh may need CPU access in order to be used properly. ({0})"), FText::FromString(SkelMesh->GetName())),
			LOCTEXT("CPUAccessNotAllowedErrorSummary", "CPU access error"),
			FNiagaraDataInterfaceFix::CreateLambda([=]()
				{
					SkelMesh->Modify();
					for (FSkeletalMeshLODInfo& LODInfo : SkelMesh->GetLODInfoArray())
					{
						LODInfo.bAllowCPUAccess = true;
					}
					return true;
				}));

		OutWarnings.Add(CPUAccessNotAllowedWarning);
	}
#endif

	if (Source == nullptr && bHasNoMeshAssignedError)
	{
		FNiagaraDataInterfaceError NoMeshAssignedError(LOCTEXT("NoMeshAssignedError", "This Data Interface must be assigned a skeletal mesh to operate."),
			LOCTEXT("NoMeshAssignedErrorSummary", "No mesh assigned error"),
			FNiagaraDataInterfaceFix());

		OutErrors.Add(NoMeshAssignedError);
	}
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

const FString UNiagaraDataInterfaceSkeletalMesh::MeshIndexBufferName(TEXT("MeshIndexBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshVertexBufferName(TEXT("MeshVertexBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightBufferName(TEXT("MeshSkinWeightBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightLookupBufferName(TEXT("MeshSkinWeightLookupBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshCurrBonesBufferName(TEXT("MeshCurrBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshPrevBonesBufferName(TEXT("MeshPrevBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshCurrSamplingBonesBufferName(TEXT("MeshCurrSamplingBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshPrevSamplingBonesBufferName(TEXT("MeshPrevSamplingBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTangentBufferName(TEXT("MeshTangentBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTexCoordBufferName(TEXT("MeshTexCoordBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshColorBufferName(TEXT("MeshColorBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerProbaBufferName(TEXT("MeshTriangleSamplerProbaBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerAliasBufferName(TEXT("MeshTriangleSamplerAliasBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshNumSamplingRegionTrianglesName(TEXT("MeshNumSamplingRegionTriangles_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshNumSamplingRegionVerticesName(TEXT("MeshNumSamplingRegionVertices_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSamplingRegionsProbaBufferName(TEXT("MeshSamplingRegionsProbaBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSamplingRegionsAliasBufferName(TEXT("MeshSamplingRegionsAliasBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSampleRegionsTriangleIndicesName(TEXT("MeshSampleRegionsTriangleIndices_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSampleRegionsVerticesName(TEXT("MeshSampleRegionsVertices_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleMatricesOffsetBufferName(TEXT("MeshTriangleMatricesOffsetBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleCountName(TEXT("MeshTriangleCount_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshVertexCountName(TEXT("MeshVertexCount_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshWeightStrideName(TEXT("MeshWeightStride_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightIndexSizeName(TEXT("MeshSkinWeightIndexSize_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshNumTexCoordName(TEXT("MeshNumTexCoord_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshNumWeightsName(TEXT("MeshNumWeights_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumBonesName(TEXT("NumBones_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumFilteredBonesName(TEXT("NumFilteredBones_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumUnfilteredBonesName(TEXT("NumUnfilteredBones_"));
const FString UNiagaraDataInterfaceSkeletalMesh::RandomMaxBoneName(TEXT("RandomMaxBone_"));
const FString UNiagaraDataInterfaceSkeletalMesh::ExcludeBoneIndexName(TEXT("ExcludeBoneIndex_"));
const FString UNiagaraDataInterfaceSkeletalMesh::FilteredAndUnfilteredBonesName(TEXT("FilteredAndUnfilteredBones_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumFilteredSocketsName(TEXT("NumFilteredSockets_"));
const FString UNiagaraDataInterfaceSkeletalMesh::FilteredSocketBoneOffsetName(TEXT("FilteredSocketBoneOffset_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstanceTransformName(TEXT("InstanceTransform_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstancePrevTransformName(TEXT("InstancePrevTransform_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstanceRotationName(TEXT("InstanceRotation_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstancePrevRotationName(TEXT("InstancePrevRotation_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstanceInvDeltaTimeName(TEXT("InstanceInvDeltaTime_"));
const FString UNiagaraDataInterfaceSkeletalMesh::EnabledFeaturesName(TEXT("EnabledFeatures_"));

void UNiagaraDataInterfaceSkeletalMesh::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/FX/Niagara/Private/NiagaraDataInterfaceSkeletalMesh.ush\"\n");
}

bool UNiagaraDataInterfaceSkeletalMesh::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
		return false;

	FSHAHash Hash = GetShaderFileHash((TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceSkeletalMesh.ush")), EShaderPlatform::SP_PCD3D_SM5);
	InVisitor->UpdateString(TEXT("NiagaraDataInterfaceSkeletalMeshHLSLSource"), Hash.ToString());
	return true;
}

bool UNiagaraDataInterfaceSkeletalMesh::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDISkeletalMeshParametersName ParamNames;
	GetNiagaraDataInterfaceParametersName(ParamNames, ParamInfo.DataInterfaceHLSLSymbol);
	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("MeshTriCoordinateStructName"), TEXT("MeshTriCoordinate")},
		{TEXT("MeshTriangleCount"), ParamNames.MeshTriangleCountName},
		{TEXT("MeshVertexCount"), ParamNames.MeshVertexCountName},
		{TEXT("GetDISkelMeshContextName"), TEXT("DISKELMESH_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Triangle Sampling
	if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomTriCoordName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out {MeshTriCoordinateStructName} OutCoord) { {GetDISkelMeshContextName} DISKelMesh_RandomTriCoord(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, OutCoord.Tri, OutCoord.BaryCoord); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidTriCoordName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, out bool IsValid) { {GetDISkelMeshContextName} IsValid = InCoord.Tri < DIContext.MeshTriangleCount; }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, out float3 OutPosition, out float3 OutVelocity, out float3 OutNormal, out float3 OutBinormal, out float3 OutTangent) { {GetDISkelMeshContextName} DISKelMesh_GetSkinnedTriangleDataWS(DIContext, InCoord.Tri, InCoord.BaryCoord, OutPosition, OutVelocity, OutNormal, OutBinormal, OutTangent); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataWSInterpName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, in float InInterp, out float3 OutPosition, out float3 OutVelocity, out float3 OutNormal, out float3 OutBinormal, out float3 OutTangent) { {GetDISkelMeshContextName} DISKelMesh_GetSkinnedTriangleDataInterpolatedWS(DIContext, InCoord.Tri, InCoord.BaryCoord, InInterp, OutPosition, OutVelocity, OutNormal, OutBinormal, OutTangent); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, out float3 OutPosition, out float3 OutVelocity, out float3 OutNormal, out float3 OutBinormal, out float3 OutTangent) { {GetDISkelMeshContextName} DISKelMesh_GetSkinnedTriangleData(DIContext, InCoord.Tri, InCoord.BaryCoord, OutPosition, OutVelocity, OutNormal, OutBinormal, OutTangent); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedTriangleDataInterpName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, in float InInterp, out float3 OutPosition, out float3 OutVelocity, out float3 OutNormal, out float3 OutBinormal, out float3 OutTangent) { {GetDISkelMeshContextName} DISKelMesh_GetSkinnedTriangleDataInterpolated(DIContext, InCoord.Tri, InCoord.BaryCoord, InInterp, OutPosition, OutVelocity, OutNormal, OutBinormal, OutTangent); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriUVName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, in int InUVSet, out float2 OutUV) { {GetDISkelMeshContextName} DISKelMesh_GetTriUV(DIContext, InCoord.Tri, InCoord.BaryCoord, InUVSet, OutUV); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriColorName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, out float4 OutColor) { {GetDISkelMeshContextName} DISkelMesh_GetTriColor(DIContext, InCoord.Tri, InCoord.BaryCoord, OutColor); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int TriangleIndex, out int OutVertexIndex0, out int OutVertexIndex1, out int OutVertexIndex2) { {GetDISkelMeshContextName} DISkelMesh_GetTriVertices(DIContext, TriangleIndex, OutVertexIndex0, OutVertexIndex1, OutVertexIndex2); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomTriangleName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out {MeshTriCoordinateStructName} OutCoord) { {GetDISkelMeshContextName} DISKelMesh_RandomTriangle(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, OutCoord.Tri, OutCoord.BaryCoord); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriangleCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISKelMesh_GetTriangleCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomFilteredTriangleName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out {MeshTriCoordinateStructName} OutCoord) { {GetDISkelMeshContextName} DISKelMesh_RandomFilteredTriangle(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, OutCoord.Tri, OutCoord.BaryCoord); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredTriangleCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISKelMesh_GetFilteredTriangleCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredTriangleAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (int FilteredIndex, out {MeshTriCoordinateStructName} OutCoord) { {GetDISkelMeshContextName} DISKelMesh_GetFilteredTriangleAt(DIContext, FilteredIndex, OutCoord.Tri, OutCoord.BaryCoord); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Bone Sampling
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int InBone, out float3 OutPosition, out float4 OutRotation, out float3 OutVelocity) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedBone(DIContext, InBone, OutPosition, OutRotation, OutVelocity); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataInterpolatedName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int InBone, in float Interp, out float3 OutPosition, out float4 OutRotation, out float3 OutVelocity) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedBoneInterpolated(DIContext, InBone, Interp, OutPosition, OutRotation, OutVelocity); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int InBone, out float3 OutPosition, out float4 OutRotation, out float3 OutVelocity) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedBoneWS(DIContext, InBone, OutPosition, OutRotation, OutVelocity); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedBoneDataWSInterpolatedName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int InBone, in float Interp, out float3 OutPosition, out float4 OutRotation, out float3 OutVelocity) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedBoneInterpolatedWS(DIContext, InBone, Interp, OutPosition, OutRotation, OutVelocity); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vertex Sampling
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out float3 OutPosition, out float3 OutVelocity, out float3 OutNormal, out float3 OutBinormal, out float3 OutTangent) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedVertex(DIContext, Vertex, OutPosition, OutVelocity, OutNormal, OutBinormal, OutTangent); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out float3 OutPosition, out float3 OutVelocity, out float3 OutNormal, out float3 OutBinormal, out float3 OutTangent) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedVertexWS(DIContext, Vertex, OutPosition, OutVelocity, OutNormal, OutBinormal, OutTangent); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetVertexColorName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out float4 OutColor) { {GetDISkelMeshContextName} DISkelMesh_GetVertexColor(DIContext, Vertex, OutColor); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetVertexUVName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, in int UVSet, out float2 OutUV) { {GetDISkelMeshContextName} DISkelMesh_GetVertexUV(DIContext, Vertex, UVSet, OutUV); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidVertexName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out bool IsValid) { {GetDISkelMeshContextName} DISkelMesh_IsValidVertex(DIContext, Vertex, IsValid); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomVertexName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(NiagaraRandInfo InRandomInfo, out int OutVertex) { {GetDISkelMeshContextName} DISkelMesh_GetRandomVertex(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, OutVertex); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetVertexCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int VertexCount) { {GetDISkelMeshContextName} DISkelMesh_GetVertexCount(DIContext, VertexCount); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidFilteredVertexName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int FilteredIndex, out bool IsValid) { {GetDISkelMeshContextName} DISkelMesh_IsValidFilteredVertex(DIContext, FilteredIndex, IsValid); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomFilteredVertexName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(NiagaraRandInfo InRandomInfo, out int OutVertex) { {GetDISkelMeshContextName} DISkelMesh_GetRandomFilteredVertex(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, OutVertex); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredVertexCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int VertexCount) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredVertexCount(DIContext, VertexCount); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredVertexAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int FilteredIndex, out int VertexIndex) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredVertexAt(DIContext, FilteredIndex, VertexIndex); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Filtered Bone
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int BoneIndex, out bool IsValid) { {GetDISkelMeshContextName} DISkelMesh_IsValidBone(DIContext, BoneIndex, IsValid); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_RandomBone(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetBoneCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetBoneCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredBoneCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredBoneCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredBoneAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int BoneIndex, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredBoneAt(DIContext, BoneIndex, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomFilteredBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_RandomFilteredBone(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetUnfilteredBoneCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetUnfilteredBoneCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetUnfilteredBoneAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int BoneIndex, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_GetUnfilteredBoneAt(DIContext, BoneIndex, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomUnfilteredBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_RandomUnfilteredBone(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Filtered Socket
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredSocketCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredSocketCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredSocketBoneAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int SocketIndex, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredSocketBoneAt(DIContext, SocketIndex, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	} 
	//else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredSocketTransformName)
	//{
	//	// TODO: This just returns the Identity transform.
	//	// TODO: Make this work on the GPU?
	//	static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int SocketIndex, in int bShouldApplyTransform, out float3 Translation, out float4 Rotation, out float3 Scale) { Translation = float3(0.0, 0.0, 0.0); Rotation = float4(0.0, 0.0, 0.0, 1.0); Scale = float3(1.0, 1.0, 1.0); }");
	//	OutHLSL += FString::Format(FormatSample, ArgsSample);
	//} 
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomFilteredSocketName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out int SocketBone) { {GetDISkelMeshContextName} DISkelMesh_RandomFilteredSocket(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, SocketBone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Misc bone functions
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomFilteredSocketOrBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_RandomFilteredSocketOrBone(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredSocketOrBoneCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetFilteredSocketOrBoneAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int FilteredIndex, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_GetFilteredSocketOrBoneAt(DIContext, FilteredIndex, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Unsupported functionality
	else
	{
		// This function is not support
		return false;
	}

	OutHLSL += TEXT("\n");
	return true;
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceSkeletalMesh::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bWasChanged = false;

	// Early out for version matching
	if (FunctionSignature.FunctionVersion == FNiagaraSkelMeshDIFunctionVersion::LatestVersion)
	{
		return bWasChanged;
	}

	// Renamed some functions and added Random Info to Various functions for consistency
	if (FunctionSignature.FunctionVersion < FNiagaraSkelMeshDIFunctionVersion::AddedRandomInfo)
	{
		static const TPair<FName, FName> FunctionRenames[] =
		{
			MakeTuple(FName("IsValidBone"), FSkeletalMeshInterfaceHelper::IsValidBoneName),
			MakeTuple(FName("RandomSpecificBone"), FSkeletalMeshInterfaceHelper::RandomFilteredBoneName),
			MakeTuple(FName("GetSpecificBoneCount"), FSkeletalMeshInterfaceHelper::GetFilteredBoneCountName),
			MakeTuple(FName("GetSpecificBone"), FSkeletalMeshInterfaceHelper::GetFilteredBoneAtName),
			MakeTuple(FName("RandomSpecificSocketBone"), FSkeletalMeshInterfaceHelper::RandomFilteredSocketName),
			MakeTuple(FName("GetSpecificSocketCount"), FSkeletalMeshInterfaceHelper::GetFilteredSocketCountName),
			MakeTuple(FName("GetSpecificSocketTransform"), FSkeletalMeshInterfaceHelper::GetFilteredSocketTransformName),
			MakeTuple(FName("GetSpecificSocketBone"), FSkeletalMeshInterfaceHelper::GetFilteredSocketBoneAtName),
			MakeTuple(FName("RandomFilteredSocketBone"), FSkeletalMeshInterfaceHelper::RandomFilteredSocketName),
		};

		for (const auto& RenamePair : FunctionRenames)
		{
			if (FunctionSignature.Name == RenamePair.Key)
			{
				FunctionSignature.Name = RenamePair.Value;
				bWasChanged = true;
				break;
			}
		}

		if (FunctionSignature.Name == FSkeletalMeshInterfaceHelper::RandomTriCoordName)
		{
			if (FunctionSignature.Inputs.Num() == 1)
			{
				FunctionSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
				bWasChanged = true;
			}
		}
		else if (FunctionSignature.Name == FSkeletalMeshInterfaceHelper::RandomFilteredBoneName)
		{
			FunctionSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
			bWasChanged = true;
		}
		else if (FunctionSignature.Name == FSkeletalMeshInterfaceHelper::RandomFilteredSocketName)
		{
			FunctionSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
			bWasChanged = true;
		}
		else if (FunctionSignature.Name == FSkeletalMeshInterfaceHelper::RandomVertexName)
		{
			FunctionSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraRandInfo::StaticStruct()), TEXT("RandomInfo")));
			bWasChanged = true;
		}
	}

	// Vertex sampling clean up
	if (FunctionSignature.FunctionVersion < FNiagaraSkelMeshDIFunctionVersion::CleanUpVertexSampling)
	{
		static const TPair<FName, FName> FunctionRenames[] =
		{
			MakeTuple(FName("IsValidVertex"), FSkeletalMeshInterfaceHelper::IsValidVertexName),
			MakeTuple(FName("RandomVertex"), FSkeletalMeshInterfaceHelper::RandomFilteredVertexName),
		};

		for (const auto& RenamePair : FunctionRenames)
		{
			if (FunctionSignature.Name == RenamePair.Key)
			{
				FunctionSignature.Name = RenamePair.Value;
				bWasChanged = true;
				break;
			}
		}
	}

	// Clean up CleanupBoneSampling
	if (FunctionSignature.FunctionVersion < FNiagaraSkelMeshDIFunctionVersion::CleanupBoneSampling)
	{
		static const TPair<FName, FName> FunctionRenames[] =
		{
			MakeTuple(FName("GetFilteredSocketBone"), FSkeletalMeshInterfaceHelper::GetFilteredSocketBoneAtName),
		};

		for (const auto& RenamePair : FunctionRenames)
		{
			if (FunctionSignature.Name == RenamePair.Key)
			{
				FunctionSignature.Name = RenamePair.Value;
				bWasChanged = true;
				break;
			}
		}
	}

	// Added tangent basis to GetSkinnedVertexData
	if (FunctionSignature.FunctionVersion < FNiagaraSkelMeshDIFunctionVersion::AddTangentBasisToGetSkinnedVertexData)
	{
		if ((FunctionSignature.Name == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataName) ||
			(FunctionSignature.Name == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName))
		{
			if ( ensure(FunctionSignature.Outputs.Num() == 2) )
			{
				FunctionSignature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Normal")));
				FunctionSignature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Binormal")));
				FunctionSignature.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
				bWasChanged = true;
			}
		}
	}

	// Set latest version
	FunctionSignature.FunctionVersion = FNiagaraSkelMeshDIFunctionVersion::LatestVersion;

	return bWasChanged;
}
#endif

void UNiagaraDataInterfaceSkeletalMesh::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DISKELMESH_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

void UNiagaraDataInterfaceSkeletalMesh::SetSourceComponentFromBlueprints(USkeletalMeshComponent* ComponentToUse)
{
	// NOTE: When ChangeId changes the next tick will be skipped and a reset of the per-instance data will be initiated. 
	ChangeId++;
	SourceComponent = ComponentToUse;
	Source = ComponentToUse->GetOwner();
}

ETickingGroup UNiagaraDataInterfaceSkeletalMesh::CalculateTickGroup(void* PerInstanceData) const
{
	FNDISkeletalMesh_InstanceData* InstData = static_cast<FNDISkeletalMesh_InstanceData*>(PerInstanceData);
	USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	if ( Component )
	{
		return ETickingGroup(FMath::Max(Component->PrimaryComponentTick.TickGroup, Component->PrimaryComponentTick.EndTickGroup) + 1);
	}
	return NiagaraFirstTickGroup;
}

//UNiagaraDataInterfaceSkeletalMesh END
//////////////////////////////////////////////////////////////////////////

template<>
void FSkeletalMeshAccessorHelper::Init<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOff>(FNDISkeletalMesh_InstanceData* InstData)
{
	Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	Mesh = InstData->Mesh;
	LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	IndexBuffer = LODData ? LODData->MultiSizeIndexContainer.GetIndexBuffer() : nullptr;
	SkinningData = InstData->SkinningData.SkinningData.Get();
	Usage = InstData->SkinningData.Usage;

	if (InstData->Mesh)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		SamplingRegion = &SamplingInfo.GetRegion(InstData->SamplingRegionIndices[0]);
		SamplingRegionBuiltData = &SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[0]);
	}
	else
	{
		SamplingRegion = nullptr;
		SamplingRegionBuiltData = nullptr;
	}
	
	if (SkinningData != nullptr)
	{
		SkinningData->EnterRead();
	}
}

template<>
void FSkeletalMeshAccessorHelper::Init<TNDISkelMesh_FilterModeSingle, TNDISkelMesh_AreaWeightingOn>(FNDISkeletalMesh_InstanceData* InstData)
{
	Comp = Cast<USkeletalMeshComponent>(InstData->Component.Get());
	Mesh = InstData->Mesh;
	LODData = InstData->GetLODRenderDataAndSkinWeights(SkinWeightBuffer);
	IndexBuffer = LODData ? LODData->MultiSizeIndexContainer.GetIndexBuffer() : nullptr;
	SkinningData = InstData->SkinningData.SkinningData.Get();
	Usage = InstData->SkinningData.Usage;

	if (InstData->Mesh)
	{
		const FSkeletalMeshSamplingInfo& SamplingInfo = InstData->Mesh->GetSamplingInfo();
		SamplingRegion = &SamplingInfo.GetRegion(InstData->SamplingRegionIndices[0]);
		SamplingRegionBuiltData = &SamplingInfo.GetRegionBuiltData(InstData->SamplingRegionIndices[0]);
	}
	else
	{
		SamplingRegion = nullptr;
		SamplingRegionBuiltData = nullptr;
	}

	if (SkinningData != nullptr)
	{
		SkinningData->EnterRead();
	}
}

#undef LOCTEXT_NAMESPACE