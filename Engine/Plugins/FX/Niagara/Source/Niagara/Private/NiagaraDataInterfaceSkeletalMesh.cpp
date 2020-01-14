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

bool FSkeletalMeshSkinningData::Tick(float InDeltaSeconds, bool bRequirePreskin)
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
	if (!InstData)
    {
		SkeletalMeshSamplingLODBuiltData = nullptr;
		bUseGpuUniformlyDistributedSampling = false;

		LODRenderData = nullptr;
		TriangleCount = 0;
		VertexCount = 0;

		NumSpecificBones = 0;
		SpecificBonesArray.Empty();
		NumSpecificSockets = 0;
		SpecificSocketBoneOffset = 0;
		return;
	}
	else
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

		// Copy Specific Bones / Socket data into arrays that the renderer will use to create read buffers
		//-TODO: Exclude setting up these arrays if we don't sample from them
		NumSpecificBones = InstData->SpecificBones.Num();
		if (NumSpecificBones > 0)
		{
			SpecificBonesArray.Reserve(NumSpecificBones);
			for (int32 v : InstData->SpecificBones)
			{
				check(v <= 65535);
				SpecificBonesArray.Add(v);
			}
		}

		NumSpecificSockets = InstData->SpecificSockets.Num();
		SpecificSocketBoneOffset = InstData->SpecificSocketBoneOffset;
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

	NumWeights = LODRenderData->SkinWeightVertexBuffer.HasExtraBoneInfluences() ? 8 : 4;

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

	// Create arrays for specific bones / sockets
	if ( NumSpecificBones > 0 )
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.ResourceArray = &SpecificBonesArray;

		SpecificBonesBuffer = RHICreateVertexBuffer(NumSpecificBones * sizeof(uint16), BUF_Static | BUF_ShaderResource, CreateInfo);
		SpecificBonesSRV = RHICreateShaderResourceView(SpecificBonesBuffer, sizeof(uint16), PF_R16_UINT);
	}
}

void FSkeletalMeshGpuSpawnStaticBuffers::ReleaseRHI()
{
	SpecificBonesBuffer.SafeRelease();
	SpecificBonesSRV.SafeRelease();

	BufferTriangleUniformSamplerProbaRHI.SafeRelease();
	BufferTriangleUniformSamplerProbaSRV.SafeRelease();
	BufferTriangleUniformSamplerAliasRHI.SafeRelease();
	BufferTriangleUniformSamplerAliasSRV.SafeRelease();

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
		for ( const FSkelMeshRenderSection& Section : Sections )
		{
			Float4Count += Section.BoneMap.Num() * 3;
		}
		check(Float4Count == 3 * SectionBoneCount);
		AllSectionsRefToLocalMatrices.AddUninitialized(Float4Count);

		Float4Count = 0;
		for ( const FSkelMeshRenderSection& Section : Sections )
		{
			const uint32 MatrixCount = Section.BoneMap.Num();
			for (uint32 m = 0; m < MatrixCount; ++m)
			{
				RefToLocalMatrices[Section.BoneMap[m]].To3x4MatrixTranspose(&AllSectionsRefToLocalMatrices[Float4Count].X);
				Float4Count += 3;
			}
		}

		// Generate information for bone sampling
		TArray<FVector4> BoneSamplingData;
		{
			const TArray<FTransform>& ComponentTransforms = SkelComp->GetComponentSpaceTransforms();
			check(ComponentTransforms.Num() == SamplingBoneCount);

			BoneSamplingData.Reserve((SamplingBoneCount + SamplingSocketCount) * 2);

			// Append bones
			for (const FTransform& BoneTransform : ComponentTransforms)
			{
				const FQuat Rotation = BoneTransform.GetRotation();
				BoneSamplingData.Add(BoneTransform.GetLocation());
				BoneSamplingData.Add(FVector4(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
			}

			// Append sockets
			for (const FTransform& SocketTransform : InstanceData->GetSpecificSocketsCurrBuffer())
			{
				const FQuat Rotation = SocketTransform.GetRotation();
				BoneSamplingData.Add(SocketTransform.GetLocation());
				BoneSamplingData.Add(FVector4(Rotation.X, Rotation.Y, Rotation.Z, Rotation.W));
			}
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
	FString MeshCurrSamplingBonesBufferName;
	FString MeshPrevSamplingBonesBufferName;
	FString MeshTangentBufferName;
	FString MeshTexCoordBufferName;
	FString MeshColorBufferName;
	FString MeshTriangleSamplerProbaBufferName;
	FString MeshTriangleSamplerAliasBufferName;
	FString MeshTriangleMatricesOffsetBufferName;
	FString MeshTriangleCountName;
	FString MeshVertexCountName;
	FString MeshWeightStrideName;
	FString MeshNumTexCoordName;
	FString MeshNumWeightsName;
	FString NumSpecificBonesName;
	FString SpecificBonesName;
	FString NumSpecificSocketsName;
	FString SpecificSocketBoneOffsetName;
	FString InstanceTransformName;
	FString InstancePrevTransformName;
	FString InstanceInvDeltaTimeName;
	FString EnabledFeaturesName;
};

static void GetNiagaraDataInterfaceParametersName(FNDISkeletalMeshParametersName& Names, const FString& Suffix)
{
	Names.MeshIndexBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshIndexBufferName + Suffix;
	Names.MeshVertexBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshVertexBufferName + Suffix;
	Names.MeshSkinWeightBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightBufferName + Suffix;
	Names.MeshCurrBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshCurrBonesBufferName + Suffix;
	Names.MeshPrevBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshPrevBonesBufferName + Suffix;
	Names.MeshCurrSamplingBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshCurrSamplingBonesBufferName + Suffix;
	Names.MeshPrevSamplingBonesBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshPrevSamplingBonesBufferName + Suffix;
	Names.MeshTangentBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTangentBufferName + Suffix;
	Names.MeshTexCoordBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTexCoordBufferName + Suffix;
	Names.MeshColorBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshColorBufferName + Suffix;
	Names.MeshTriangleSamplerProbaBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerProbaBufferName + Suffix;
	Names.MeshTriangleSamplerAliasBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerAliasBufferName + Suffix;
	Names.MeshTriangleMatricesOffsetBufferName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleMatricesOffsetBufferName + Suffix;
	Names.MeshTriangleCountName = UNiagaraDataInterfaceSkeletalMesh::MeshTriangleCountName + Suffix;
	Names.MeshVertexCountName = UNiagaraDataInterfaceSkeletalMesh::MeshVertexCountName + Suffix;
	Names.MeshWeightStrideName = UNiagaraDataInterfaceSkeletalMesh::MeshWeightStrideName + Suffix;
	Names.MeshNumTexCoordName = UNiagaraDataInterfaceSkeletalMesh::MeshNumTexCoordName + Suffix;
	Names.MeshNumWeightsName = UNiagaraDataInterfaceSkeletalMesh::MeshNumWeightsName + Suffix;
	Names.NumSpecificBonesName = UNiagaraDataInterfaceSkeletalMesh::NumSpecificBonesName + Suffix;
	Names.SpecificBonesName = UNiagaraDataInterfaceSkeletalMesh::SpecificBonesName + Suffix;
	Names.NumSpecificSocketsName = UNiagaraDataInterfaceSkeletalMesh::NumSpecificSocketsName + Suffix;
	Names.SpecificSocketBoneOffsetName = UNiagaraDataInterfaceSkeletalMesh::SpecificSocketBoneOffsetName + Suffix;
	Names.InstanceTransformName = UNiagaraDataInterfaceSkeletalMesh::InstanceTransformName + Suffix;
	Names.InstancePrevTransformName = UNiagaraDataInterfaceSkeletalMesh::InstancePrevTransformName + Suffix;
	Names.InstanceInvDeltaTimeName = UNiagaraDataInterfaceSkeletalMesh::InstanceInvDeltaTimeName + Suffix;
	Names.EnabledFeaturesName = UNiagaraDataInterfaceSkeletalMesh::EnabledFeaturesName + Suffix;
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
		MeshCurrSamplingBonesBuffer.Bind(ParameterMap, *ParamNames.MeshCurrSamplingBonesBufferName);
		MeshPrevSamplingBonesBuffer.Bind(ParameterMap, *ParamNames.MeshPrevSamplingBonesBufferName);
		MeshTangentBuffer.Bind(ParameterMap, *ParamNames.MeshTangentBufferName);
		MeshTexCoordBuffer.Bind(ParameterMap, *ParamNames.MeshTexCoordBufferName);
		MeshColorBuffer.Bind(ParameterMap, *ParamNames.MeshColorBufferName);
		MeshTriangleSamplerProbaBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleSamplerProbaBufferName);
		MeshTriangleSamplerAliasBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleSamplerAliasBufferName);
		MeshTriangleMatricesOffsetBuffer.Bind(ParameterMap, *ParamNames.MeshTriangleMatricesOffsetBufferName);
		MeshTriangleCount.Bind(ParameterMap, *ParamNames.MeshTriangleCountName);
		MeshVertexCount.Bind(ParameterMap, *ParamNames.MeshVertexCountName);
		MeshWeightStride.Bind(ParameterMap, *ParamNames.MeshWeightStrideName);
		MeshNumTexCoord.Bind(ParameterMap, *ParamNames.MeshNumTexCoordName);
		MeshNumWeights.Bind(ParameterMap, *ParamNames.MeshNumWeightsName);
		NumSpecificBones.Bind(ParameterMap, *ParamNames.NumSpecificBonesName);
		SpecificBones.Bind(ParameterMap, *ParamNames.SpecificBonesName);
		NumSpecificSockets.Bind(ParameterMap, *ParamNames.NumSpecificSocketsName);
		SpecificSocketBoneOffset.Bind(ParameterMap, *ParamNames.SpecificSocketBoneOffsetName);
		InstanceTransform.Bind(ParameterMap, *ParamNames.InstanceTransformName);
		InstancePrevTransform.Bind(ParameterMap, *ParamNames.InstancePrevTransformName);
		InstanceInvDeltaTime.Bind(ParameterMap, *ParamNames.InstanceInvDeltaTimeName);
		EnabledFeatures.Bind(ParameterMap, *ParamNames.EnabledFeaturesName);
	}

	virtual void Serialize(FArchive& Ar)override
	{
		Ar << MeshIndexBuffer;
		Ar << MeshVertexBuffer;
		Ar << MeshSkinWeightBuffer;
		Ar << MeshCurrBonesBuffer;
		Ar << MeshPrevBonesBuffer;
		Ar << MeshCurrSamplingBonesBuffer;
		Ar << MeshPrevSamplingBonesBuffer;
		Ar << MeshTangentBuffer;
		Ar << MeshTexCoordBuffer;
		Ar << MeshColorBuffer;
		Ar << MeshTriangleSamplerProbaBuffer;
		Ar << MeshTriangleSamplerAliasBuffer;
		Ar << MeshTriangleMatricesOffsetBuffer;
		Ar << MeshTriangleCount;
		Ar << MeshVertexCount;
		Ar << MeshWeightStride;
		Ar << MeshNumTexCoord;
		Ar << MeshNumWeights;
		Ar << NumSpecificBones;
		Ar << SpecificBones;
		Ar << NumSpecificSockets;
		Ar << SpecificSocketBoneOffset;
		Ar << InstanceTransform;
		Ar << InstancePrevTransform;
		Ar << InstanceInvDeltaTime;
		Ar << EnabledFeatures;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();
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
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			}
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, StaticBuffers->GetBufferColorSRV());
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshTriangleCount, StaticBuffers->GetTriangleCount());
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshVertexCount, StaticBuffers->GetVertexCount());
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

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshWeightStride, InstanceData->MeshWeightStrideByte/4);

			uint32 EnabledFeaturesBits = InstanceData->bIsGpuUniformlyDistributedSampling ? 1 : 0;

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
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			}

			FRHIShaderResourceView* SpecificBonesSRV = StaticBuffers->GetNumSpecificBones() > 0 ? StaticBuffers->GetSpecificBonesSRV() : FNiagaraRenderer::GetDummyUIntBuffer().SRV.GetReference();
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumSpecificBones, StaticBuffers->GetNumSpecificBones());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, SpecificBones, SpecificBonesSRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumSpecificSockets, StaticBuffers->GetNumSpecificSockets());
			SetShaderValue(RHICmdList, ComputeShaderRHI, SpecificSocketBoneOffset, StaticBuffers->GetSpecificSocketBoneOffset());

			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, InstanceData->Transform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, InstanceData->PrevTransform);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 1.0f / InstanceData->DeltaSeconds);

			SetShaderValue(RHICmdList, ComputeShaderRHI, EnabledFeatures, EnabledFeaturesBits);
		}
		else
		{
			// Bind dummy buffers
			ensure(!InstanceData || InstanceData->StaticBuffers);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshVertexBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshIndexBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTangentBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumTexCoord, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTexCoordBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshColorBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshTriangleCount, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshVertexCount, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerProbaBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleSamplerAliasBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSkinWeightBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshWeightStride, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MeshNumWeights, 0);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshCurrSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshPrevSamplingBonesBuffer, FNiagaraRenderer::GetDummyFloat4Buffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshTriangleMatricesOffsetBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, NumSpecificBones, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, SpecificBones, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumSpecificSockets, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, SpecificSocketBoneOffset, 0);

			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstancePrevTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InstanceInvDeltaTime, 0.0f);

			SetShaderValue(RHICmdList, ComputeShaderRHI, EnabledFeatures, 0);
		}
	}


private:

	FShaderResourceParameter MeshIndexBuffer;
	FShaderResourceParameter MeshVertexBuffer;
	FShaderResourceParameter MeshSkinWeightBuffer;
	FShaderResourceParameter MeshCurrBonesBuffer;
	FShaderResourceParameter MeshPrevBonesBuffer;
	FShaderResourceParameter MeshCurrSamplingBonesBuffer;
	FShaderResourceParameter MeshPrevSamplingBonesBuffer;
	FShaderResourceParameter MeshTangentBuffer;
	FShaderResourceParameter MeshTexCoordBuffer;
	FShaderResourceParameter MeshColorBuffer;
	FShaderResourceParameter MeshTriangleSamplerProbaBuffer;
	FShaderResourceParameter MeshTriangleSamplerAliasBuffer;
	FShaderResourceParameter MeshTriangleMatricesOffsetBuffer;
	FShaderParameter MeshTriangleCount;
	FShaderParameter MeshVertexCount;
	FShaderParameter MeshWeightStride;
	FShaderParameter MeshNumTexCoord;
	FShaderParameter MeshNumWeights;
	FShaderParameter NumSpecificBones;
	FShaderResourceParameter SpecificBones;
	FShaderParameter NumSpecificSockets;
	FShaderParameter SpecificSocketBoneOffset;
	FShaderParameter InstanceTransform;
	FShaderParameter InstancePrevTransform;
	FShaderParameter InstanceInvDeltaTime;
	FShaderParameter EnabledFeatures;
};

//////////////////////////////////////////////////////////////////////////

void FNiagaraDataInterfaceProxySkeletalMesh::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
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

void UNiagaraDataInterfaceSkeletalMesh::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
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
	if (!Mesh && DefaultMesh)
	{
		Mesh = DefaultMesh;
	}
#endif

	return Mesh;
}


bool FNDISkeletalMesh_InstanceData::Init(UNiagaraDataInterfaceSkeletalMesh* Interface, FNiagaraSystemInstance* SystemInstance)
{
	check(SystemInstance);

	// Initialize members
	Component = nullptr;
	Mesh = nullptr;
	MeshSafe = nullptr;
	Transform = FMatrix::Identity;
	TransformInverseTransposed = FMatrix::Identity;
	PrevTransform = FMatrix::Identity;
	PrevTransformInverseTransposed = FMatrix::Identity;
	DeltaSeconds = SystemInstance->GetComponent()->GetWorld()->GetDeltaSeconds();
	ChangeId = Interface->ChangeId;
	bIsGpuUniformlyDistributedSampling = false;
	MeshWeightStrideByte = 0;
	MeshGpuSpawnStaticBuffers = nullptr;
	MeshGpuSpawnDynamicBuffers = nullptr;

	bAllowCPUMeshDataAccess = true;

	// Get skel mesh and confirm have valid data
	USkeletalMeshComponent* NewSkelComp = nullptr;
	Mesh = Interface->GetSkeletalMesh(SystemInstance->GetComponent(), Component, NewSkelComp, this);
	MeshSafe = Mesh;

	if (!Mesh)
	{
		/*USceneComponent* Comp = Component.Get();
		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface has no valid mesh. Failed InitPerInstanceData!\nInterface: %s\nComponent: %s\nActor: %s\n")
			, *Interface->GetFullName()
			, Comp ? *Component->GetFullName() : TEXT("Null Component!")
			, Comp ? *Comp->GetOwner()->GetFullName() : TEXT("NA"));*/
		return false;
	}

	if (!Component.IsValid())
	{
		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface has no valid component. Failed InitPerInstanceData - %s"), *Interface->GetFullName());
		return false;
	}

	Transform = Component->GetComponentToWorld().ToMatrixWithScale();
	TransformInverseTransposed = Transform.InverseFast().GetTransposed();
	PrevTransform = Transform;
	PrevTransformInverseTransposed = TransformInverseTransposed;

#if WITH_EDITOR
	MeshSafe->GetOnMeshChanged().AddUObject(SystemInstance->GetComponent(), &UNiagaraComponent::ReinitializeSystem);
#endif

// 	if (!Mesh->bAllowCPUAccess)
// 	{
// 		UE_LOG(LogNiagara, Log, TEXT("SkeletalMesh data interface using a mesh that does not allow CPU access. Failed InitPerInstanceData - Mesh: %s"), *Mesh->GetFullName());
// 		return false;
// 	}

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

	FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
	FSkeletalMeshLODRenderData& LODData = GetLODRenderDataAndSkinWeights(SkinWeightBuffer);

	// Check for the validity of the Mesh's cpu data.
	{
		if ( Mesh->GetLODInfo(LODIndex)->bAllowCPUAccess )
		{
			const bool LODDataNumVerticesCorrect = LODData.GetNumVertices() > 0;
			const bool LODDataPositonNumVerticesCorrect = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() > 0;
			const bool bSkinWeightBuffer = SkinWeightBuffer != nullptr;
			const bool SkinWeightBufferNumVerticesCorrect = bSkinWeightBuffer && (SkinWeightBuffer->GetNumVertices() > 0);
			const bool bIndexBufferValid = LODData.MultiSizeIndexContainer.IsIndexBufferValid();
			const bool bIndexBufferFound = bIndexBufferValid && (LODData.MultiSizeIndexContainer.GetIndexBuffer() != nullptr);
			const bool bIndexBufferNumCorrect = bIndexBufferFound && (LODData.MultiSizeIndexContainer.GetIndexBuffer()->Num() > 0);

			bAllowCPUMeshDataAccess = LODDataNumVerticesCorrect &&
				LODDataPositonNumVerticesCorrect &&
				bSkinWeightBuffer &&
				SkinWeightBufferNumVerticesCorrect &&
				bIndexBufferValid &&
				bIndexBufferFound &&
				bIndexBufferNumCorrect;
		}
		else
		{
			bAllowCPUMeshDataAccess = false;
		}
	}

	// Gather specific bones information
	FReferenceSkeleton& RefSkel = Mesh->RefSkeleton;
	{
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
	}

	// Gather specific socket information
	{
		SpecificSockets = Interface->SpecificSockets;
		SpecificSocketBoneOffset = Mesh->RefSkeleton.GetNum();

		SpecificSocketTransformsIndex = 0;
		SpecificSocketTransforms[0].Reset(SpecificSockets.Num());
		SpecificSocketTransforms[0].AddDefaulted(SpecificSockets.Num());
		UpdateSpecificSocketTransforms();
		for (int32 i=1; i < SpecificSocketTransforms.Num(); ++i)
		{
			SpecificSocketTransforms[i].Reset(SpecificSockets.Num());
			SpecificSocketTransforms[i].Append(SpecificSocketTransforms[0]);
		}

		TArray<FName, TInlineAllocator<16>> MissingSockets;
		for (FName SocketName : SpecificSockets)
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
	if ( SystemInstance->HasGPUEmitters() )
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
		MeshGpuSpawnStaticBuffers->Initialise(this, LODData, SamplingInfo.GetBuiltData().WholeMeshBuiltData[LODIndex]);
		BeginInitResource(MeshGpuSpawnStaticBuffers);

		MeshGpuSpawnDynamicBuffers = new FSkeletalMeshGpuDynamicBufferProxy();
		MeshGpuSpawnDynamicBuffers->Initialise(RefSkel, LODData, SpecificSockets.Num());
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
	else
	{
#if WITH_EDITORONLY_DATA
		if (!Interface->DefaultMesh)
		{
			return true;
		}
#endif
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

		// Cache socket transforms to avoid potentially calculating them multiple times during the VM
		SpecificSocketTransformsIndex = (SpecificSocketTransformsIndex + 1) % SpecificSocketTransforms.Num();
		UpdateSpecificSocketTransforms();

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

void FNDISkeletalMesh_InstanceData::UpdateSpecificSocketTransforms()
{
	USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component.Get());
	TArray<FTransform>& WriteBuffer = GetSpecificSocketsWriteBuffer();

	//-TODO: We may need to handle skinning mode changes here
	if (SkelComp != nullptr)
	{
		for (int32 i = 0; i < SpecificSockets.Num(); ++i)
		{
			WriteBuffer[i] = SkelComp->GetSocketTransform(SpecificSockets[i], RTS_Component);
		}
	}
	else if ( Mesh != nullptr )
	{
		for (int32 i = 0; i < SpecificSockets.Num(); ++i)
		{
			WriteBuffer[i] = FTransform(Mesh->GetComposedRefPoseMatrix(SpecificSockets[i]));
		}
	}
	else
	{
		for (int32 i = 0; i < SpecificSockets.Num(); ++i)
		{
			WriteBuffer[i] = FTransform::Identity;
		}
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
#if WITH_EDITORONLY_DATA
	, DefaultMesh(nullptr)
#endif
	, Source(nullptr)
	, SkinningMode(ENDISkeletalMesh_SkinningMode::SkinOnTheFly)
	, WholeMeshLOD(INDEX_NONE)
#if WITH_EDITORONLY_DATA
	, bRequiresCPUAccess(false)
#endif
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

#if WITH_EDITORONLY_DATA
	// If the change comes from an interaction (and not just a generic change) reset the usage flags.
	// todo : this and the usage binding need to be done in the a precompilation parsing (or whever the script is compiled).
	if (PropertyChangedEvent.Property)
	{
		bRequiresCPUAccess = false;
	}
#endif
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
			OutFunc.Unbind();
			return;
		}

#if WITH_EDITOR
		bRequiresCPUAccess = true;
#endif // WITH_EDITOR
		return;
	}

	// Bind vertex sampling function
	BindVertexSamplingFunction(BindingInfo, InstData, OutFunc);
	if (OutFunc.IsBound())
	{
		if (!InstData->bAllowCPUMeshDataAccess)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Skeletal Mesh Data Interface is trying to use vertex sampling but CPU access or the data is invalid. Interface: %s"), *GetFullName());
			OutFunc.Unbind();
			return;
		}

#if WITH_EDITOR
		bRequiresCPUAccess = true;
#endif // WITH_EDITOR
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
	OtherTyped->SpecificBones = SpecificBones;
	OtherTyped->SpecificSockets = SpecificSockets;
#if WITH_EDITORONLY_DATA
	OtherTyped->DefaultMesh = DefaultMesh;
	OtherTyped->bRequiresCPUAccess = bRequiresCPUAccess;
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
		OtherTyped->DefaultMesh == DefaultMesh &&
#endif
		OtherTyped->MeshUserParameter == MeshUserParameter &&
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
	bool bHasCPUAccessError = false;
	bool bHasNoMeshAssignedError = false;
	
	// Collect Errors
#if WITH_EDITORONLY_DATA
	if (DefaultMesh != nullptr)
	{
		if (bRequiresCPUAccess)
		{
			for (const FSkeletalMeshLODInfo& LODInfo : DefaultMesh->GetLODInfoArray())
			{
				if (!LODInfo.bAllowCPUAccess)
				{
					bHasCPUAccessError = true;
					break;
				}
			}
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
			for (FSkeletalMeshLODInfo& LODInfo : DefaultMesh->GetLODInfoArray())
			{
				LODInfo.bAllowCPUAccess = true;
			}
			return true;
		}));

		Errors.Add(CPUAccessNotAllowedError);
	}
#endif

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

const FString UNiagaraDataInterfaceSkeletalMesh::MeshIndexBufferName(TEXT("MeshIndexBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshVertexBufferName(TEXT("MeshVertexBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshSkinWeightBufferName(TEXT("MeshSkinWeightBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshCurrBonesBufferName(TEXT("MeshCurrBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshPrevBonesBufferName(TEXT("MeshPrevBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshCurrSamplingBonesBufferName(TEXT("MeshCurrSamplingBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshPrevSamplingBonesBufferName(TEXT("MeshPrevSamplingBonesBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTangentBufferName(TEXT("MeshTangentBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTexCoordBufferName(TEXT("MeshTexCoordBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshColorBufferName(TEXT("MeshColorBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerProbaBufferName(TEXT("MeshTriangleSamplerProbaBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleSamplerAliasBufferName(TEXT("MeshTriangleSamplerAliasBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleMatricesOffsetBufferName(TEXT("MeshTriangleMatricesOffsetBuffer_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshTriangleCountName(TEXT("MeshTriangleCount_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshVertexCountName(TEXT("MeshVertexCount_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshWeightStrideName(TEXT("MeshWeightStride_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshNumTexCoordName(TEXT("MeshNumTexCoord_"));
const FString UNiagaraDataInterfaceSkeletalMesh::MeshNumWeightsName(TEXT("MeshNumWeights_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumSpecificBonesName(TEXT("NumSpecificBones_"));
const FString UNiagaraDataInterfaceSkeletalMesh::SpecificBonesName(TEXT("SpecificBones_"));
const FString UNiagaraDataInterfaceSkeletalMesh::NumSpecificSocketsName(TEXT("NumSpecificSockets_"));
const FString UNiagaraDataInterfaceSkeletalMesh::SpecificSocketBoneOffsetName(TEXT("SpecificSocketBoneOffset_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstanceTransformName(TEXT("InstanceTransform_"));
const FString UNiagaraDataInterfaceSkeletalMesh::InstancePrevTransformName(TEXT("InstancePrevTransform_"));
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

	// Triangle Sampling
	if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomTriCoordName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (NiagaraRandInfo InRandomInfo, out {MeshTriCoordinateStructName} OutCoord) { {GetDISkelMeshContextName} DISKelMesh_RandomTriCoord(DIContext, InRandomInfo.Seed1, InRandomInfo.Seed2, InRandomInfo.Seed3, OutCoord.Tri, OutCoord.BaryCoord); }");
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
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidTriCoordName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in {MeshTriCoordinateStructName} InCoord, out bool IsValid) { {GetDISkelMeshContextName} IsValid = InCoord.Tri < DIContext.MeshTriangleCount; }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriCoordVerticesName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int TriangleIndex, out int OutVertexIndex0, out int OutVertexIndex1, out int OutVertexIndex2) { {GetDISkelMeshContextName} DISkelMesh_GetTriVertices(DIContext, TriangleIndex, OutVertexIndex0, OutVertexIndex1, OutVertexIndex2); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
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
	// Vertex Sampling
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomVertexName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName}(out int OutVertex) { {GetDISkelMeshContextName} DISkelMesh_GetRandomVertex(DIContext, OutVertex); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidVertexName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out bool IsValid) { {GetDISkelMeshContextName} DISkelMesh_IsValidVertex(DIContext, Vertex, IsValid); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out float3 OutPosition, out float3 OutVelocity) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedVertex(DIContext, Vertex, OutPosition, OutVelocity); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSkinnedVertexDataWSName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int Vertex, out float3 OutPosition, out float3 OutVelocity) { {GetDISkelMeshContextName} DISkelMesh_GetSkinnedVertexWS(DIContext, Vertex, OutPosition, OutVelocity); }");
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
	// Specific Bone
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSpecificBoneCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetSpecificBoneCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSpecificBoneAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int BoneIndex, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_GetSpecificBoneAt(DIContext, BoneIndex, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomSpecificBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Bone) { {GetDISkelMeshContextName} DISkelMesh_RandomSpecificBone(DIContext, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	// Specific Socket
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSpecificSocketCountName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int Count) { {GetDISkelMeshContextName} DISkelMesh_GetSpecificSocketCount(DIContext, Count); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSpecificSocketBoneAtName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int SocketIndex, out int Bone) { {GetDISkelMeshContextName} DISkelMesh_GetSpecificSocketBoneAt(DIContext, SocketIndex, Bone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	} 
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetSpecificSocketTransformName)
	{
		// TODO: This just returns the Identity transform.
		// TODO: Make this work on the GPU?
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (in int SocketIndex, in int bShouldApplyTransform, out float3 Translation, out float4 Rotation, out float4 Scale) { Translation = float3(0.0, 0.0, 0.0); Rotation = float4(0.0, 0.0, 0.0, 1.0); Scale = float3(1.0, 1.0, 1.0); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	} 
	else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::RandomSpecificSocketBoneName)
	{
		static const TCHAR* FormatSample = TEXT("void {InstanceFunctionName} (out int SocketBone) { {GetDISkelMeshContextName} DISkelMesh_RandomSpecificSocketBone(DIContext, SocketBone); }");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
	}
	// Unsupported functionality
	//else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetVertexCountName)				// void GetFilteredVertexCount(out int VertexCount)
	//else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetVertexAtName)					// void GetFilteredVertex(in int FilterdIndex, out int VertexIndex)
	//else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriangleCountName)			// void GetFilteredTriangleCount(out int OutTriangleCount)
	//else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::GetTriangleAtName)				// void GetFilteredTriangle(in int Triangle, out {MeshTriCoordinateStructName} OutCoord)
	//else if (FunctionInfo.DefinitionName == FSkeletalMeshInterfaceHelper::IsValidBoneName)					// void IsValidBoneName(in int BoneIndex, out bool IsValid)
	else
	{
		// This function is not support
		return false;
	}

	OutHLSL += TEXT("\n");
	return true;
}

void UNiagaraDataInterfaceSkeletalMesh::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DISKELMESH_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
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
