// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkeletalRenderCPUSkin.cpp: CPU skinned skeletal mesh rendering code.
=============================================================================*/

#include "SkeletalRenderCPUSkin.h"
#include "EngineStats.h"
#include "Components/SkeletalMeshComponent.h"
#include "SceneManagement.h"
#include "SkeletalRender.h"
#include "Animation/MorphTarget.h"
#include "GPUSkinVertexFactory.h"

struct FMorphTargetDelta;

template<typename VertexType>
static void SkinVertices(FFinalSkinVertex* DestVertex, FMatrix* ReferenceToLocal, int32 LODIndex, FSkeletalMeshLODRenderData& LOD, FSkinWeightVertexBuffer& WeightBuffer, TArray<FActiveMorphTarget>& ActiveMorphTargets, TArray<float>& MorphTargetWeights, const TMap<int32, FClothSimulData>& ClothSimulUpdateData, float ClothBlendWeight, const FMatrix& WorldToLocal);

#define INFLUENCE_0		0
#define INFLUENCE_1		1
#define INFLUENCE_2		2
#define INFLUENCE_3		3
#define INFLUENCE_4		4
#define INFLUENCE_5		5
#define INFLUENCE_6		6
#define INFLUENCE_7		7

/** 
 * Modify the vertex buffer to store bone weights in the UV coordinates for rendering 
 * @param DestVertex - already filled out vertex buffer from SkinVertices
 * @param LOD - LOD model corresponding to DestVertex 
 * @param BonesOfInterest - array of bones we want to display
 */
static void CalculateBoneWeights(FFinalSkinVertex* DestVertex, FSkeletalMeshLODRenderData& LOD, FSkinWeightVertexBuffer& WeightBuffer, TArray<int32> InBonesOfInterest);

/**
* Modify the vertex buffer to store morph target weights in the UV coordinates for rendering
* @param DestVertex - already filled out vertex buffer from SkinVertices
* @param LOD - LOD model corresponding to DestVertex
* @param MorphTargetOfInterest - array of morphtargets to draw
*/
static void CalculateMorphTargetWeights(FFinalSkinVertex* DestVertex, FSkeletalMeshLODRenderData& LOD, int LODIndex, TArray<UMorphTarget*> InMorphTargetOfInterest);

/*-----------------------------------------------------------------------------
	FSkeletalMeshObjectCPUSkin
-----------------------------------------------------------------------------*/


FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectCPUSkin(USkinnedMeshComponent* InMeshComponent, FSkeletalMeshRenderData* InSkelMeshRenderData, ERHIFeatureLevel::Type InFeatureLevel)
	: FSkeletalMeshObject(InMeshComponent, InSkelMeshRenderData, InFeatureLevel)
,	DynamicData(NULL)
,	CachedVertexLOD(INDEX_NONE)
,	bRenderOverlayMaterial(false)
{
	// create LODs to match the base mesh
	for( int32 LODIndex=0;LODIndex < InSkelMeshRenderData->LODRenderData.Num();LODIndex++ )
	{
		new(LODs) FSkeletalMeshObjectLOD(InFeatureLevel, InSkelMeshRenderData,LODIndex);
	}

	InitResources(InMeshComponent);
}


FSkeletalMeshObjectCPUSkin::~FSkeletalMeshObjectCPUSkin()
{
	delete DynamicData;
}


void FSkeletalMeshObjectCPUSkin::InitResources(USkinnedMeshComponent* InMeshComponent)
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			FSkelMeshComponentLODInfo* CompLODInfo = nullptr;
			if (InMeshComponent->LODInfo.IsValidIndex(LODIndex))
			{
				CompLODInfo = &InMeshComponent->LODInfo[LODIndex];
			}

			SkelLOD.InitResources(CompLODInfo);
		}
	}
}


void FSkeletalMeshObjectCPUSkin::ReleaseResources()
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];

		// Skip LODs that have their render data stripped
		if (SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
			SkelLOD.ReleaseResources();
		}
	}
}


void FSkeletalMeshObjectCPUSkin::EnableOverlayRendering(bool bEnabled, const TArray<int32>* InBonesOfInterest, const TArray<UMorphTarget*>* InMorphTargetOfInterest)
{
	bRenderOverlayMaterial = bEnabled;
	
	BonesOfInterest.Reset();
	MorphTargetOfInterest.Reset();

	if (InBonesOfInterest)
	{
		BonesOfInterest.Append(*InBonesOfInterest);
	}
	else if (InMorphTargetOfInterest)
	{
		MorphTargetOfInterest.Append(*InMorphTargetOfInterest);
	}
}

void FSkeletalMeshObjectCPUSkin::Update(int32 LODIndex,USkinnedMeshComponent* InMeshComponent,const TArray<FActiveMorphTarget>& ActiveMorphTargets, const TArray<float>& MorphTargetWeights, bool bUpdatePreviousBoneTransform)
{
	if (InMeshComponent)
	{
		// create the new dynamic data for use by the rendering thread
		// this data is only deleted when another update is sent
		FDynamicSkelMeshObjectDataCPUSkin* NewDynamicData = new FDynamicSkelMeshObjectDataCPUSkin(InMeshComponent,SkeletalMeshRenderData,LODIndex,ActiveMorphTargets, MorphTargetWeights);

		// We prepare the next frame but still have the value from the last one
		uint32 FrameNumberToPrepare = GFrameNumber + 1;
		uint32 RevisionNumber = 0;

		if (InMeshComponent->SceneProxy)
		{
			// We allow caching of per-frame, per-scene data
			FrameNumberToPrepare = InMeshComponent->SceneProxy->GetScene().GetFrameNumber() + 1;
			RevisionNumber = InMeshComponent->GetBoneTransformRevisionNumber();
		}

		// queue a call to update this data
		ENQUEUE_UNIQUE_RENDER_COMMAND_FOURPARAMETER(
			SkelMeshObjectUpdateDataCommand,
			FSkeletalMeshObjectCPUSkin*, MeshObject, this,
			uint32, FrameNumberToPrepare, FrameNumberToPrepare,
			uint32, RevisionNumber, RevisionNumber, 
			FDynamicSkelMeshObjectDataCPUSkin*, NewDynamicData, NewDynamicData,
		{
			FScopeCycleCounter Context(MeshObject->GetStatId());
			MeshObject->UpdateDynamicData_RenderThread(RHICmdList, NewDynamicData, FrameNumberToPrepare, RevisionNumber);
		}
		);
	}
}

void FSkeletalMeshObjectCPUSkin::UpdateDynamicData_RenderThread(FRHICommandListImmediate& RHICmdList, FDynamicSkelMeshObjectDataCPUSkin* InDynamicData, uint32 FrameNumberToPrepare, uint32 RevisionNumber)
{
	// we should be done with the old data at this point
	delete DynamicData;
	// update with new data
	DynamicData = InDynamicData;	
	check(DynamicData);

	// update vertices using the new data
	CacheVertices(DynamicData->LODIndex,true);
}

void FSkeletalMeshObjectCPUSkin::CacheVertices(int32 LODIndex, bool bForce) const
{
	SCOPE_CYCLE_COUNTER( STAT_CPUSkinUpdateRTTime);

	// Source skel mesh and static lod model
	FSkeletalMeshLODRenderData& LOD = SkeletalMeshRenderData->LODRenderData[LODIndex];

	// Get the destination mesh LOD.
	const FSkeletalMeshObjectLOD& MeshLOD = LODs[LODIndex];

	// only recache if lod changed
	if ( (LODIndex != CachedVertexLOD || bForce) &&
		DynamicData && 
		MeshLOD.StaticMeshVertexBuffer.IsValid() )
	{
		const FSkelMeshObjectLODInfo& MeshLODInfo = LODInfo[LODIndex];

		// bone matrices
		FMatrix* ReferenceToLocal = DynamicData->ReferenceToLocal.GetData();

		int32 CachedFinalVerticesNum = LOD.GetNumVertices();
		CachedFinalVertices.Empty(CachedFinalVerticesNum);
		CachedFinalVertices.AddUninitialized(CachedFinalVerticesNum);

		// final cached verts
		FFinalSkinVertex* DestVertex = CachedFinalVertices.GetData();

		if (DestVertex)
		{
			check(GIsEditor || LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess());
			SCOPE_CYCLE_COUNTER(STAT_SkinningTime);
			if (LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs())
			{
				// do actual skinning
				SkinVertices< TGPUSkinVertexFloat32Uvs<1> >( DestVertex, ReferenceToLocal, DynamicData->LODIndex, LOD, *MeshLOD.MeshObjectWeightBuffer, DynamicData->ActiveMorphTargets, DynamicData->MorphTargetWeights, DynamicData->ClothSimulUpdateData, DynamicData->ClothBlendWeight, DynamicData->WorldToLocal);
			}
			else
			{
				// do actual skinning
				SkinVertices< TGPUSkinVertexFloat16Uvs<1> >( DestVertex, ReferenceToLocal, DynamicData->LODIndex, LOD, *MeshLOD.MeshObjectWeightBuffer, DynamicData->ActiveMorphTargets, DynamicData->MorphTargetWeights, DynamicData->ClothSimulUpdateData, DynamicData->ClothBlendWeight, DynamicData->WorldToLocal);
			}

			if (bRenderOverlayMaterial)
			{
				if (MorphTargetOfInterest.Num() > 0)
				{
					//Transfer morph target weights we're interested in to the UV channels
					CalculateMorphTargetWeights(DestVertex, LOD, LODIndex, MorphTargetOfInterest);
				}
				else // default is bones of interest
					// this can go if no morphtarget is selected but enabled to render
					// but that doesn't matter since it will only draw empty overlay
				{
					//Transfer bone weights we're interested in to the UV channels
					CalculateBoneWeights(DestVertex, LOD, *MeshLOD.MeshObjectWeightBuffer, BonesOfInterest);
				}
			}
		}

		// set lod level currently cached
		CachedVertexLOD = LODIndex;

		check(LOD.GetNumVertices() == CachedFinalVertices.Num());

		for (int i = 0; i < CachedFinalVertices.Num(); i++)
		{
			MeshLOD.PositionVertexBuffer.VertexPosition(i) = CachedFinalVertices[i].Position;
			MeshLOD.StaticMeshVertexBuffer.SetVertexTangents(i, CachedFinalVertices[i].TangentX.ToFVector(), CachedFinalVertices[i].GetTangentY(), CachedFinalVertices[i].TangentZ.ToFVector());
			MeshLOD.StaticMeshVertexBuffer.SetVertexUV(i, 0, FVector2D(CachedFinalVertices[i].U, CachedFinalVertices[i].V));
		}

		BeginUpdateResourceRHI(&MeshLOD.PositionVertexBuffer);
		BeginUpdateResourceRHI(&MeshLOD.StaticMeshVertexBuffer);

		const FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectLOD* MeshLODptr = &MeshLOD;
		FLocalVertexFactory* VertexFactoryPtr = &MeshLOD.VertexFactory;
		ENQUEUE_RENDER_COMMAND(UpdateSkeletalMeshCPUSkinVertexFactory)(
			[VertexFactoryPtr, MeshLODptr](FRHICommandListImmediate& RHICmdList)
		{
			FLocalVertexFactory::FDataType Data;

			MeshLODptr->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactoryPtr, Data);
			MeshLODptr->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactoryPtr, Data);
			MeshLODptr->StaticMeshVertexBuffer.BindTexCoordVertexBuffer(VertexFactoryPtr, Data, 0);
			MeshLODptr->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactoryPtr, Data, 0);
			MeshLODptr->MeshObjectColorBuffer->BindColorVertexBuffer(VertexFactoryPtr, Data);

			VertexFactoryPtr->SetData(Data);
			VertexFactoryPtr->InitResource();
		});
	}
}

const FVertexFactory* FSkeletalMeshObjectCPUSkin::GetSkinVertexFactory(const FSceneView* View, int32 LODIndex,int32 /*ChunkIdx*/) const
{
	check( LODs.IsValidIndex(LODIndex) );
	return &LODs[LODIndex].VertexFactory;
}

/** 
 * Init rendering resources for this LOD 
 */
void FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectLOD::InitResources(FSkelMeshComponentLODInfo* CompLODInfo)
{
	check(SkelMeshRenderData);
	check(SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex));

	// If we have a skin weight override buffer (and it's the right size) use it
	FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
	if (CompLODInfo)
	{
		if (CompLODInfo->OverrideSkinWeights &&
			CompLODInfo->OverrideSkinWeights->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
		{
			check(LODData.SkinWeightVertexBuffer.HasExtraBoneInfluences() == CompLODInfo->OverrideSkinWeights->HasExtraBoneInfluences());
			MeshObjectWeightBuffer = CompLODInfo->OverrideSkinWeights;
		}
		else
		{
			MeshObjectWeightBuffer = &LODData.SkinWeightVertexBuffer;
		}

		if (CompLODInfo->OverrideVertexColors &&
			CompLODInfo->OverrideVertexColors->GetNumVertices() == LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices())
		{
			MeshObjectColorBuffer = CompLODInfo->OverrideVertexColors;
		}
		else
		{
			MeshObjectColorBuffer = &LODData.StaticVertexBuffers.ColorVertexBuffer;
		}
	}

	const FStaticMeshVertexBuffer& SrcVertexBuf = LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
	PositionVertexBuffer.Init(LODData.StaticVertexBuffers.PositionVertexBuffer);
	StaticMeshVertexBuffer.Init(SrcVertexBuf.GetNumVertices(), 1);

	for (uint32 i = 0; i < SrcVertexBuf.GetNumVertices(); i++)
	{
		StaticMeshVertexBuffer.SetVertexTangents(i, SrcVertexBuf.VertexTangentX(i), SrcVertexBuf.VertexTangentY(i), SrcVertexBuf.VertexTangentZ(i));
		StaticMeshVertexBuffer.SetVertexUV(i, 0, SrcVertexBuf.GetVertexUV(i, 0));
	}

	BeginInitResource(&PositionVertexBuffer);
	BeginInitResource(&StaticMeshVertexBuffer);

	FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectLOD* Self = this;
	FLocalVertexFactory* VertexFactoryPtr = &VertexFactory;
	// update vertex factory components and sync it
	ENQUEUE_RENDER_COMMAND(InitSkeletalMeshCPUSkinVertexFactory)(
		[VertexFactoryPtr, Self](FRHICommandListImmediate& RHICmdList)
		{
			FLocalVertexFactory::FDataType Data;

			Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactoryPtr, Data);
			Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactoryPtr, Data);
			Self->StaticMeshVertexBuffer.BindTexCoordVertexBuffer(VertexFactoryPtr, Data, 0);
			Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactoryPtr, Data, 0);
			Self->MeshObjectColorBuffer->BindColorVertexBuffer(VertexFactoryPtr, Data);

			VertexFactoryPtr->SetData(Data);
			VertexFactoryPtr->InitResource();
		});

	bResourcesInitialized = true;
}

/** 
 * Release rendering resources for this LOD 
 */
void FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectLOD::ReleaseResources()
{	
	BeginReleaseResource(&VertexFactory);
	BeginReleaseResource(&PositionVertexBuffer);
	BeginReleaseResource(&StaticMeshVertexBuffer);

	bResourcesInitialized = false;
}

TArray<FTransform>* FSkeletalMeshObjectCPUSkin::GetComponentSpaceTransforms() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if(DynamicData)
	{
		return &(DynamicData->MeshComponentSpaceTransforms);
	}
	else
#endif
	{
		return NULL;
	}
}

const TArray<FMatrix>& FSkeletalMeshObjectCPUSkin::GetReferenceToLocalMatrices() const
{
	return DynamicData->ReferenceToLocal;
}

void FSkeletalMeshObjectCPUSkin::DrawVertexElements(FPrimitiveDrawInterface* PDI, const FMatrix& ToWorldSpace, bool bDrawNormals, bool bDrawTangents, bool bDrawBinormals) const
{
	uint32 NumIndices = CachedFinalVertices.Num();

	FMatrix LocalToWorldInverseTranspose = ToWorldSpace.InverseFast().GetTransposed();

	for (uint32 i = 0; i < NumIndices; i++)
	{
		FFinalSkinVertex& Vert = CachedFinalVertices[i];

		const FVector WorldPos = ToWorldSpace.TransformPosition( Vert.Position );

		const FVector Normal = Vert.TangentZ.ToFVector();
		const FVector Tangent = Vert.TangentX.ToFVector();
		const FVector Binormal = FVector(Normal) ^ FVector(Tangent);

		const float Len = 1.0f;

		if( bDrawNormals )
		{
			PDI->DrawLine( WorldPos, WorldPos+LocalToWorldInverseTranspose.TransformVector( (FVector)(Normal) ).GetSafeNormal() * Len, FLinearColor( 0.0f, 1.0f, 0.0f), SDPG_World );
		}

		if( bDrawTangents )
		{
			PDI->DrawLine( WorldPos, WorldPos+LocalToWorldInverseTranspose.TransformVector( Tangent ).GetSafeNormal() * Len, FLinearColor( 1.0f, 0.0f, 0.0f), SDPG_World );
		}

		if( bDrawBinormals )
		{
			PDI->DrawLine( WorldPos, WorldPos+LocalToWorldInverseTranspose.TransformVector( Binormal ).GetSafeNormal() * Len, FLinearColor( 0.0f, 0.0f, 1.0f), SDPG_World );
		}
	}
}

/*-----------------------------------------------------------------------------
FDynamicSkelMeshObjectDataCPUSkin
-----------------------------------------------------------------------------*/

FDynamicSkelMeshObjectDataCPUSkin::FDynamicSkelMeshObjectDataCPUSkin(
	USkinnedMeshComponent* InMeshComponent,
	FSkeletalMeshRenderData* InSkelMeshRenderData,
	int32 InLODIndex,
	const TArray<FActiveMorphTarget>& InActiveMorphTargets,
	const TArray<float>& InMorphTargetWeights
	)
:	LODIndex(InLODIndex)
,	ActiveMorphTargets(InActiveMorphTargets)
,	MorphTargetWeights(InMorphTargetWeights)
,	ClothBlendWeight(0.0f)
{
	UpdateRefToLocalMatrices( ReferenceToLocal, InMeshComponent, InSkelMeshRenderData, LODIndex );

	// Update the clothing simulation mesh positions and normals
	UpdateClothSimulationData(InMeshComponent);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	MeshComponentSpaceTransforms = InMeshComponent->GetComponentSpaceTransforms();
#endif
}

/*-----------------------------------------------------------------------------
	FSkeletalMeshObjectCPUSkin - morph target blending implementation
-----------------------------------------------------------------------------*/

/** Struct used to hold temporary info during morph target blending */
struct FMorphTargetInfo
{
	/** Info about morphtarget to blend */
	FActiveMorphTarget			ActiveMorphTarget;
	/** Index of next delta to try applying. This prevents us looking at every delta for every vertex. */
	int32						NextDeltaIndex;
	/** Array of deltas to apply to mesh, sorted based on the index of the base mesh vert that they affect. */
	FMorphTargetDelta*			Deltas;
	/** How many deltas are in array */
	int32						NumDeltas;
};

/**
 *	Init set of info structs to hold temporary state while blending morph targets in.
 * @return							number of active morphs that are valid
 */
static uint32 InitEvalInfos(const TArray<FActiveMorphTarget>& ActiveMorphTargets, const TArray<float>& MorphTargetWeights, int32 LODIndex, TArray<FMorphTargetInfo>& OutEvalInfos)
{
	uint32 NumValidMorphTargets=0;

	for( int32 MorphIdx=0; MorphIdx < ActiveMorphTargets.Num(); MorphIdx++ )
	{
		FMorphTargetInfo NewInfo;

		const FActiveMorphTarget& ActiveMorphTarget = ActiveMorphTargets[MorphIdx];
		const float ActiveMorphAbsVertexWeight = FMath::Abs(MorphTargetWeights[ActiveMorphTarget.WeightIndex]);

		if( ActiveMorphTarget.MorphTarget != NULL &&
			ActiveMorphAbsVertexWeight >= MinMorphTargetBlendWeight &&
			ActiveMorphAbsVertexWeight <= MaxMorphTargetBlendWeight &&
			ActiveMorphTarget.MorphTarget->HasDataForLOD(LODIndex) )
		{
			// start at the first vertex since they affect base mesh verts in ascending order
			NewInfo.ActiveMorphTarget = ActiveMorphTarget;
			NewInfo.NextDeltaIndex = 0;
			NewInfo.Deltas = ActiveMorphTarget.MorphTarget->GetMorphTargetDelta(LODIndex, NewInfo.NumDeltas);

			NumValidMorphTargets++;
		}
		else
		{
			// invalidate the indices for any invalid morph models
			NewInfo.ActiveMorphTarget = FActiveMorphTarget();
			NewInfo.NextDeltaIndex = INDEX_NONE;
			NewInfo.Deltas = nullptr;
			NewInfo.NumDeltas = 0;
		}			

		OutEvalInfos.Add(NewInfo);
	}
	return NumValidMorphTargets;
}

/** Release any state for the morphs being evaluated */
void TermEvalInfos(TArray<FMorphTargetInfo>& EvalInfos)
{
	EvalInfos.Empty();
}

/** 
* Derive the tanget/binormal using the new normal and the base tangent vectors for a vertex 
*/
template<typename VertexType>
FORCEINLINE void RebuildTangentBasis( VertexType& DestVertex )
{
	// derive the new tangent by orthonormalizing the new normal against
	// the base tangent vector (assuming these are normalized)
	FVector Tangent = DestVertex.TangentX.ToFVector();
	FVector Normal = DestVertex.TangentZ.ToFVector();
	Tangent = Tangent - ((Tangent | Normal) * Normal);
	Tangent.Normalize();
	DestVertex.TangentX = Tangent;
}

/**
* Applies the vertex deltas to a vertex.
*/
template<typename VertexType>
FORCEINLINE void ApplyMorphBlend( VertexType& DestVertex, const FMorphTargetDelta& SrcMorph, float Weight )
{
	// Add position offset 
	DestVertex.Position += SrcMorph.PositionDelta * Weight;

	// Save W before = operator. That overwrites W to be 127.
	int8 W = DestVertex.TangentZ.Vector.W;

	FVector TanZ = DestVertex.TangentZ.ToFVector();

	// add normal offset. can only apply normal deltas up to a weight of 1
	DestVertex.TangentZ = FVector(TanZ + SrcMorph.TangentZDelta * FMath::Min(Weight,1.0f)).GetUnsafeNormal();
	// Recover W
	DestVertex.TangentZ.Vector.W = W;
} 

/**
* Blends the source vertex with all the active morph targets.
*/
template<typename VertexType>
FORCEINLINE void UpdateMorphedVertex( VertexType& MorphedVertex, const VertexType& SrcVertex, int32 CurBaseVertIdx, int32 LODIndex, TArray<FMorphTargetInfo>& EvalInfos, const TArray<float>& MorphWeights )
{
	MorphedVertex = SrcVertex;

	// iterate over all active morphs
	for( int32 MorphIdx=0; MorphIdx < EvalInfos.Num(); MorphIdx++ )
	{
		FMorphTargetInfo& Info = EvalInfos[MorphIdx];

		// if the next delta to use matches the current vertex, apply it
		if( Info.NextDeltaIndex != INDEX_NONE &&
			Info.NextDeltaIndex < Info.NumDeltas &&
			Info.Deltas[Info.NextDeltaIndex].SourceIdx == CurBaseVertIdx )
		{
			ApplyMorphBlend( MorphedVertex, Info.Deltas[Info.NextDeltaIndex], MorphWeights[Info.ActiveMorphTarget.WeightIndex] );

			// Update 'next delta to use'
			Info.NextDeltaIndex += 1;
		}
	}

	// rebuild orthonormal tangents
	RebuildTangentBasis( MorphedVertex );
}



/*-----------------------------------------------------------------------------
	FSkeletalMeshObjectCPUSkin - optimized skinning code
-----------------------------------------------------------------------------*/

MSVC_PRAGMA(warning(push))
MSVC_PRAGMA(warning(disable : 4730)) //mixing _m64 and floating point expressions may result in incorrect code

const VectorRegister		VECTOR_0001				= DECLARE_VECTOR_REGISTER(0.f, 0.f, 0.f, 1.f);

#define FIXED_VERTEX_INDEX 0xFFFF

template<bool bExtraBoneInfluences, int32 MaxSectionBoneInfluences, typename VertexType>
static void SkinVertexSection(
	FFinalSkinVertex*& DestVertex,
	TArray<FMorphTargetInfo>& MorphEvalInfos,
	const TArray<float>& MorphWeights,
	const FSkelMeshRenderSection& Section,
	const FSkeletalMeshLODRenderData &LOD,
	FSkinWeightVertexBuffer& WeightBuffer,
	int32 VertexBufferBaseIndex, 
	uint32 NumValidMorphs, 
	int32 &CurBaseVertIdx, 
	int32 LODIndex, 
	const FMatrix* RESTRICT ReferenceToLocal, 
	const FClothSimulData* ClothSimData, 
	float ClothBlendWeight, 
	const FMatrix& WorldToLocal)
{
	// VertexCopy for morph. Need to allocate right struct
	// To avoid re-allocation, create 2 statics, and assign right struct
	VertexType  VertexCopy;

	// Prefetch all bone indices
	const FBoneIndexType* BoneMap = Section.BoneMap.GetData();
	FPlatformMisc::Prefetch( BoneMap );
	FPlatformMisc::Prefetch( BoneMap, PLATFORM_CACHE_LINE_SIZE );

	const bool bLODUsesCloth = LOD.HasClothData() && ClothSimData != nullptr && ClothBlendWeight > 0.0f;
	const int32 NumSoftVertices = Section.GetNumVertices();
	if (NumSoftVertices > 0)
	{
		INC_DWORD_STAT_BY(STAT_CPUSkinVertices,NumSoftVertices);
		for(int32 VertexIndex = VertexBufferBaseIndex;VertexIndex < NumSoftVertices;VertexIndex++,DestVertex++)
		{
			const int32 VertexBufferIndex = Section.GetVertexBufferIndex() + VertexIndex;

			VertexType SrcSoftVertex;
			const FVector& VertexPosition = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexBufferIndex);
			FPlatformMisc::Prefetch(&VertexPosition, PLATFORM_CACHE_LINE_SIZE);	// Prefetch next vertices
			
			SrcSoftVertex.Position = VertexPosition;
			SrcSoftVertex.TangentX = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexBufferIndex);
			SrcSoftVertex.TangentZ = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexBufferIndex);
			for (uint32 j = 0; j < VertexType::NumTexCoords; j++)
			{
				SrcSoftVertex.UVs[j] = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV_Typed<VertexType::StaticMeshVertexUVType>(VertexBufferIndex, j);
			}

			VertexType* MorphedVertex = &SrcSoftVertex;

			const TSkinWeightInfo<bExtraBoneInfluences>* SrcWeights = WeightBuffer.GetSkinWeightPtr<bExtraBoneInfluences>(VertexBufferIndex);

			if( NumValidMorphs ) 
			{
				MorphedVertex = &VertexCopy;
				UpdateMorphedVertex<VertexType>( *MorphedVertex, SrcSoftVertex, CurBaseVertIdx, LODIndex, MorphEvalInfos, MorphWeights);
			}

			const FMeshToMeshVertData* ClothVertData = nullptr;
			if (bLODUsesCloth)
			{
				ClothVertData = &Section.ClothMappingData[VertexIndex];
				FPlatformMisc::Prefetch(ClothVertData, PLATFORM_CACHE_LINE_SIZE);	// Prefetch next cloth vertex
			}

			const uint8* RESTRICT BoneIndices = SrcWeights->InfluenceBones;
			const uint8* RESTRICT BoneWeights = SrcWeights->InfluenceWeights;

			static VectorRegister	SrcNormals[3];
			VectorRegister			DstNormals[3];
			SrcNormals[0] = VectorLoadFloat3_W1( &MorphedVertex->Position);
			SrcNormals[1] = Unpack3( &MorphedVertex->TangentX.Vector.Packed );
			SrcNormals[2] = Unpack4( &MorphedVertex->TangentZ.Vector.Packed );
			VectorRegister Weights = VectorMultiply( VectorLoadByte4(BoneWeights), VECTOR_INV_255 );
			VectorRegister ExtraWeights;
			if (MaxSectionBoneInfluences > 4)
			{
				ExtraWeights = VectorMultiply( VectorLoadByte4(&BoneWeights[MAX_INFLUENCES_PER_STREAM]), VECTOR_INV_255 );
			}
			VectorResetFloatRegisters(); // Need to call this to be able to use regular floating point registers again after Unpack and VectorLoadByte4.

			const FMatrix BoneMatrix0 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_0]]];
			VectorRegister Weight0 = VectorReplicate( Weights, INFLUENCE_0 );
			VectorRegister M00	= VectorMultiply( VectorLoadAligned( &BoneMatrix0.M[0][0] ), Weight0 );
			VectorRegister M10	= VectorMultiply( VectorLoadAligned( &BoneMatrix0.M[1][0] ), Weight0 );
			VectorRegister M20	= VectorMultiply( VectorLoadAligned( &BoneMatrix0.M[2][0] ), Weight0 );
			VectorRegister M30	= VectorMultiply( VectorLoadAligned( &BoneMatrix0.M[3][0] ), Weight0 );

			if (MaxSectionBoneInfluences > 1 )
			{
				const FMatrix BoneMatrix1 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_1]]];
				VectorRegister Weight1 = VectorReplicate( Weights, INFLUENCE_1 );
				M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix1.M[0][0] ), Weight1, M00 );
				M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix1.M[1][0] ), Weight1, M10 );
				M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix1.M[2][0] ), Weight1, M20 );
				M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix1.M[3][0] ), Weight1, M30 );

				if (MaxSectionBoneInfluences > 2 )
				{
					const FMatrix BoneMatrix2 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_2]]];
					VectorRegister Weight2 = VectorReplicate( Weights, INFLUENCE_2 );
					M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix2.M[0][0] ), Weight2, M00 );
					M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix2.M[1][0] ), Weight2, M10 );
					M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix2.M[2][0] ), Weight2, M20 );
					M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix2.M[3][0] ), Weight2, M30 );

					if (MaxSectionBoneInfluences > 3 )
					{
						const FMatrix BoneMatrix3 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_3]]];
						VectorRegister Weight3 = VectorReplicate( Weights, INFLUENCE_3 );
						M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix3.M[0][0] ), Weight3, M00 );
						M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix3.M[1][0] ), Weight3, M10 );
						M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix3.M[2][0] ), Weight3, M20 );
						M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix3.M[3][0] ), Weight3, M30 );
					}

					if (MaxSectionBoneInfluences > 4)
					{
						const FMatrix BoneMatrix4 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_4]]];
						VectorRegister Weight4 = VectorReplicate( ExtraWeights, INFLUENCE_4 - INFLUENCE_4 );
						M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix4.M[0][0] ), Weight4, M00 );
						M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix4.M[1][0] ), Weight4, M10 );
						M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix4.M[2][0] ), Weight4, M20 );
						M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix4.M[3][0] ), Weight4, M30 );

						if (MaxSectionBoneInfluences > 5)
						{
							const FMatrix BoneMatrix5 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_5]]];
							VectorRegister Weight5 = VectorReplicate( ExtraWeights, INFLUENCE_5 - INFLUENCE_4 );
							M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix5.M[0][0] ), Weight5, M00 );
							M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix5.M[1][0] ), Weight5, M10 );
							M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix5.M[2][0] ), Weight5, M20 );
							M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix5.M[3][0] ), Weight5, M30 );

							if (MaxSectionBoneInfluences > 6)
							{
								const FMatrix BoneMatrix6 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_6]]];
								VectorRegister Weight6 = VectorReplicate( ExtraWeights, INFLUENCE_6 - INFLUENCE_4 );
								M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix6.M[0][0] ), Weight6, M00 );
								M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix6.M[1][0] ), Weight6, M10 );
								M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix6.M[2][0] ), Weight6, M20 );
								M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix6.M[3][0] ), Weight6, M30 );

								if (MaxSectionBoneInfluences > 7)
								{
									const FMatrix BoneMatrix7 = ReferenceToLocal[BoneMap[BoneIndices[INFLUENCE_7]]];
									VectorRegister Weight7 = VectorReplicate( ExtraWeights, INFLUENCE_7 - INFLUENCE_4 );
									M00	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix7.M[0][0] ), Weight7, M00 );
									M10	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix7.M[1][0] ), Weight7, M10 );
									M20	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix7.M[2][0] ), Weight7, M20 );
									M30	= VectorMultiplyAdd( VectorLoadAligned( &BoneMatrix7.M[3][0] ), Weight7, M30 );
								}
							}
						}
					}
				}
			}

			VectorRegister N_xxxx = VectorReplicate( SrcNormals[0], 0 );
			VectorRegister N_yyyy = VectorReplicate( SrcNormals[0], 1 );
			VectorRegister N_zzzz = VectorReplicate( SrcNormals[0], 2 );
			DstNormals[0] = VectorMultiplyAdd( N_xxxx, M00, VectorMultiplyAdd( N_yyyy, M10, VectorMultiplyAdd( N_zzzz, M20, M30 ) ) );

			DstNormals[1] = VectorZero();
			N_xxxx = VectorReplicate( SrcNormals[1], 0 );
			N_yyyy = VectorReplicate( SrcNormals[1], 1 );
			N_zzzz = VectorReplicate( SrcNormals[1], 2 );
			DstNormals[1] = VectorNormalize(VectorMultiplyAdd( N_xxxx, M00, VectorMultiplyAdd( N_yyyy, M10, VectorMultiply( N_zzzz, M20 ) ) ));

			N_xxxx = VectorReplicate( SrcNormals[2], 0 );
			N_yyyy = VectorReplicate( SrcNormals[2], 1 );
			N_zzzz = VectorReplicate( SrcNormals[2], 2 );
			DstNormals[2] = VectorZero();
			DstNormals[2] = VectorNormalize(VectorMultiplyAdd( N_xxxx, M00, VectorMultiplyAdd( N_yyyy, M10, VectorMultiply( N_zzzz, M20 ) ) ));


			// carry over the W component (sign of basis determinant) 
			DstNormals[2] = VectorMultiplyAdd( VECTOR_0001, SrcNormals[2], DstNormals[2] );

			// Write to 16-byte aligned memory:
			VectorStore( DstNormals[0], &DestVertex->Position );
			Pack3( DstNormals[1], &DestVertex->TangentX.Vector.Packed );
			Pack4( DstNormals[2], &DestVertex->TangentZ.Vector.Packed );
			VectorResetFloatRegisters(); // Need to call this to be able to use regular floating point registers again after Pack().

			// Apply cloth. This code has been adapted from GpuSkinVertexFactory.usf
			if (ClothVertData != nullptr && ClothVertData->SourceMeshVertIndices[3] < FIXED_VERTEX_INDEX)
			{
				struct ClothCPU
				{
					FORCEINLINE static FVector GetClothSimulPosition(const FClothSimulData& InClothSimData, int32 InIndex)
					{
						if (InClothSimData.Positions.IsValidIndex(InIndex))
						{
							return FVector(InClothSimData.Transform.TransformPosition(InClothSimData.Positions[InIndex]));
						}

						return FVector::ZeroVector;
					}

					FORCEINLINE static FVector GetClothSimulNormal(const FClothSimulData& InClothSimData, int32 InIndex)
					{
						if (InClothSimData.Normals.IsValidIndex(InIndex))
						{
							return FVector(InClothSimData.Transform.TransformVector(InClothSimData.Normals[InIndex]));
						}

						return FVector(0, 0, 1);
					}

					FORCEINLINE static FVector ClothingPosition(const FMeshToMeshVertData& InClothVertData, const FClothSimulData& InClothSimData)
					{
						return    InClothVertData.PositionBaryCoordsAndDist.X * (GetClothSimulPosition(InClothSimData, InClothVertData.SourceMeshVertIndices[0]) + GetClothSimulNormal(InClothSimData, InClothVertData.SourceMeshVertIndices[0]) * InClothVertData.PositionBaryCoordsAndDist.W)
								+ InClothVertData.PositionBaryCoordsAndDist.Y * (GetClothSimulPosition(InClothSimData, InClothVertData.SourceMeshVertIndices[1]) + GetClothSimulNormal(InClothSimData, InClothVertData.SourceMeshVertIndices[1]) * InClothVertData.PositionBaryCoordsAndDist.W)
								+ InClothVertData.PositionBaryCoordsAndDist.Z * (GetClothSimulPosition(InClothSimData, InClothVertData.SourceMeshVertIndices[2]) + GetClothSimulNormal(InClothSimData, InClothVertData.SourceMeshVertIndices[2]) * InClothVertData.PositionBaryCoordsAndDist.W);
					}

					FORCEINLINE static void ClothingTangents(const FMeshToMeshVertData& InClothVertData, const FClothSimulData& InClothSimData, const FVector& InSimulatedPosition, const FMatrix& InWorldToLocal, FVector& OutTangentX, FVector& OutTangentZ)
					{
						FVector A = GetClothSimulPosition(InClothSimData, InClothVertData.SourceMeshVertIndices[0]);
						FVector B = GetClothSimulPosition(InClothSimData, InClothVertData.SourceMeshVertIndices[1]);
						FVector C = GetClothSimulPosition(InClothSimData, InClothVertData.SourceMeshVertIndices[2]);

						FVector NA = GetClothSimulNormal(InClothSimData, InClothVertData.SourceMeshVertIndices[0]);
						FVector NB = GetClothSimulNormal(InClothSimData, InClothVertData.SourceMeshVertIndices[1]);
						FVector NC = GetClothSimulNormal(InClothSimData, InClothVertData.SourceMeshVertIndices[2]);

						FVector NormalPosition = InClothVertData.NormalBaryCoordsAndDist.X*(A + NA*InClothVertData.NormalBaryCoordsAndDist.W)
												+ InClothVertData.NormalBaryCoordsAndDist.Y*(B + NB*InClothVertData.NormalBaryCoordsAndDist.W)
												+ InClothVertData.NormalBaryCoordsAndDist.Z*(C + NC*InClothVertData.NormalBaryCoordsAndDist.W);

						FVector TangentPosition = InClothVertData.TangentBaryCoordsAndDist.X*(A + NA*InClothVertData.TangentBaryCoordsAndDist.W)
												+ InClothVertData.TangentBaryCoordsAndDist.Y*(B + NB*InClothVertData.TangentBaryCoordsAndDist.W)
												+ InClothVertData.TangentBaryCoordsAndDist.Z*(C + NC*InClothVertData.TangentBaryCoordsAndDist.W);

						OutTangentX = (TangentPosition - InSimulatedPosition).GetUnsafeNormal();
						OutTangentZ = (NormalPosition - InSimulatedPosition).GetUnsafeNormal();

						// cloth data are all in world space so need to change into local space
						OutTangentX = InWorldToLocal.TransformVector(OutTangentX);
						OutTangentZ = InWorldToLocal.TransformVector(OutTangentZ);
					}
				};

				// build sim position (in world space)
				FVector SimulatedPositionWorld = ClothCPU::ClothingPosition(*ClothVertData, *ClothSimData);

				// transform back to local space
				FVector SimulatedPosition = WorldToLocal.TransformPosition(SimulatedPositionWorld);

				// Lerp between skinned and simulated position
				DestVertex->Position = FMath::Lerp(DestVertex->Position, SimulatedPosition, ClothBlendWeight);

				// recompute tangent & normal
				FVector TangentX;
				FVector TangentZ;
				ClothCPU::ClothingTangents(*ClothVertData, *ClothSimData, SimulatedPositionWorld, WorldToLocal, TangentX, TangentZ);

				// Lerp between skinned and simulated tangents
				FVector SkinnedTangentX = DestVertex->TangentX.ToFVector();
				FVector4 SkinnedTangentZ = DestVertex->TangentZ.ToFVector4();
				DestVertex->TangentX = (TangentX * ClothBlendWeight) + (SkinnedTangentX * (1.0f - ClothBlendWeight));
				DestVertex->TangentZ = FVector4((TangentZ * ClothBlendWeight) + (SkinnedTangentZ * (1.0f - ClothBlendWeight)), SkinnedTangentZ.W);
			}

			// Copy UVs.
			FVector2D UVs = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Section.GetVertexBufferIndex() + VertexIndex, 0);
			DestVertex->U = UVs.X;
			DestVertex->V = UVs.Y;

			CurBaseVertIdx++;
		}
	}
}

template< bool bExtraBoneInfluences, typename VertexType>
static void SkinVertexSection( 
	FFinalSkinVertex*& DestVertex, 
	TArray<FMorphTargetInfo>& MorphEvalInfos, 
	const TArray<float>& MorphWeights, 
	const FSkelMeshRenderSection& Section, 
	const FSkeletalMeshLODRenderData &LOD,
	FSkinWeightVertexBuffer& WeightBuffer,
	int32 VertexBufferBaseIndex, 
	uint32 NumValidMorphs, 
	int32 &CurBaseVertIdx, 
	int32 LODIndex, 
	const FMatrix* RESTRICT ReferenceToLocal, 
	const FClothSimulData* ClothSimData, 
	float ClothBlendWeight, 
	const FMatrix& WorldToLocal)
{
	switch (Section.MaxBoneInfluences)
	{
		case 1: SkinVertexSection<bExtraBoneInfluences, 1, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 2: SkinVertexSection<bExtraBoneInfluences, 2, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 3: SkinVertexSection<bExtraBoneInfluences, 3, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 4: SkinVertexSection<bExtraBoneInfluences, 4, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 5: SkinVertexSection<bExtraBoneInfluences, 5, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 6: SkinVertexSection<bExtraBoneInfluences, 6, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 7: SkinVertexSection<bExtraBoneInfluences, 7, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		case 8: SkinVertexSection<bExtraBoneInfluences, 8, VertexType>(DestVertex, MorphEvalInfos, MorphWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal); break;
		default: check(0);
	}
}

template<typename VertexType>
static void SkinVertices(
	FFinalSkinVertex* DestVertex, 
	FMatrix* ReferenceToLocal, 
	int32 LODIndex, 
	FSkeletalMeshLODRenderData& LOD,
	FSkinWeightVertexBuffer& WeightBuffer,
	TArray<FActiveMorphTarget>& ActiveMorphTargets, 
	TArray<float>& MorphTargetWeights, 
	const TMap<int32, FClothSimulData>& ClothSimulUpdateData, 
	float ClothBlendWeight, 
	const FMatrix& WorldToLocal)
{
	uint32 StatusRegister = VectorGetControlRegister();
	VectorSetControlRegister( StatusRegister | VECTOR_ROUND_TOWARD_ZERO );

	// Create array to track state during morph blending
	TArray<FMorphTargetInfo> MorphEvalInfos;
	uint32 NumValidMorphs = InitEvalInfos(ActiveMorphTargets, MorphTargetWeights, LODIndex, MorphEvalInfos);

	const uint32 MaxGPUSkinBones = FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
	check(MaxGPUSkinBones <= FGPUBaseSkinVertexFactory::GHardwareMaxGPUSkinBones);

	// Prefetch all matrices
	for ( uint32 MatrixIndex=0; MatrixIndex < MaxGPUSkinBones; MatrixIndex+=2 )
	{
		FPlatformMisc::Prefetch( ReferenceToLocal + MatrixIndex );
	}

	int32 CurBaseVertIdx = 0;

	int32 VertexBufferBaseIndex=0;

	for(int32 SectionIndex= 0;SectionIndex< LOD.RenderSections.Num();SectionIndex++)
	{
		FSkelMeshRenderSection& Section = LOD.RenderSections[SectionIndex];

		const FClothSimulData* ClothSimData = ClothSimulUpdateData.Find(Section.CorrespondClothAssetIndex);

		if (LOD.DoesVertexBufferHaveExtraBoneInfluences())
		{
			SkinVertexSection<true, VertexType>(DestVertex, MorphEvalInfos, MorphTargetWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal);
		}
		else
		{
			SkinVertexSection<false, VertexType>(DestVertex, MorphEvalInfos, MorphTargetWeights, Section, LOD, WeightBuffer, VertexBufferBaseIndex, NumValidMorphs, CurBaseVertIdx, LODIndex, ReferenceToLocal, ClothSimData, ClothBlendWeight, WorldToLocal);
		}
	}

	VectorSetControlRegister( StatusRegister );
}

/**
 * Convert FPackedNormal to 0-1 FVector4
 */
FVector4 GetTangetToColor(FPackedNormal Tangent)
{
	VectorRegister VectorToUnpack = Tangent.GetVectorRegister();
	// Write to FVector and return it.
	FVector4 UnpackedVector;
	VectorStoreAligned( VectorToUnpack, &UnpackedVector );

	FVector4 Src = UnpackedVector;
	Src = Src + FVector4(1.f, 1.f, 1.f, 1.f);
	Src = Src/2.f;
	return Src;
}

/** 
 * Modify the vertex buffer to store bone weights in the UV coordinates for rendering 
 * @param DestVertex - already filled out vertex buffer from SkinVertices
 * @param LOD - LOD model corresponding to DestVertex 
 * @param BonesOfInterest - array of bones we want to display
 */
template <bool bExtraBoneInfluencesT>
static FORCEINLINE void CalculateSectionBoneWeights(FFinalSkinVertex*& DestVertex, FSkinWeightVertexBuffer& SkinWeightVertexBuffer, FSkelMeshRenderSection& Section, const TArray<int32>& BonesOfInterest)
{
	const float INV255 = 1.f/255.f;

	int32 VertexBufferBaseIndex = 0;

	//array of bone mapping
	FBoneIndexType* BoneMap = Section.BoneMap.GetData();

	for(int32 VertexIndex = VertexBufferBaseIndex;VertexIndex < Section.GetNumVertices();VertexIndex++,DestVertex++)
	{
		const int32 VertexBufferIndex = Section.GetVertexBufferIndex() + VertexIndex;
		auto* SrcWeight = SkinWeightVertexBuffer.GetSkinWeightPtr<bExtraBoneInfluencesT>(VertexBufferIndex);

		//Zero out the UV coords
		DestVertex->U = 0.0f;
		DestVertex->V = 0.0f;

		const uint8* RESTRICT BoneIndices = SrcWeight->InfluenceBones;
		const uint8* RESTRICT BoneWeights = SrcWeight->InfluenceWeights;

		for (int32 i = 0; i < TSkinWeightInfo<bExtraBoneInfluencesT>::NumInfluences; i++)
		{
			if (BonesOfInterest.Contains(BoneMap[BoneIndices[i]]))
			{
				DestVertex->U += BoneWeights[i] * INV255; 
				DestVertex->V += BoneWeights[i] * INV255;
			}
		}
	}
}

/** 
 * Modify the vertex buffer to store bone weights in the UV coordinates for rendering 
 * @param DestVertex - already filled out vertex buffer from SkinVertices
 * @param LOD - LOD render data corresponding to DestVertex 
 * @param WeightBuffer - skin weights
 * @param BonesOfInterest - array of bones we want to display
 */
static void CalculateBoneWeights(FFinalSkinVertex* DestVertex, FSkeletalMeshLODRenderData& LOD, FSkinWeightVertexBuffer& WeightBuffer, TArray<int32> InBonesOfInterest)
{
	const float INV255 = 1.f/255.f;

	int32 VertexBufferBaseIndex = 0;

	for(int32 SectionIndex= 0;SectionIndex< LOD.RenderSections.Num();SectionIndex++)
	{
		FSkelMeshRenderSection& Section = LOD.RenderSections[SectionIndex];

		if (WeightBuffer.HasExtraBoneInfluences())
		{
			CalculateSectionBoneWeights<true>(DestVertex, WeightBuffer, Section, InBonesOfInterest);
		}
		else
		{
			CalculateSectionBoneWeights<false>(DestVertex, WeightBuffer, Section, InBonesOfInterest);
		}
	}
}

static void CalculateMorphTargetWeights(FFinalSkinVertex* DestVertex, FSkeletalMeshLODRenderData& LOD, int LODIndex, TArray<UMorphTarget*> InMorphTargetOfInterest)
{
	const FFinalSkinVertex* EndVert = DestVertex + LOD.GetNumVertices();

	for (FFinalSkinVertex* ClearVert = DestVertex; ClearVert != EndVert; ++ClearVert)
	{
		ClearVert->U = 0.f;
		ClearVert->V = 0.f;
	}

	for (const UMorphTarget* Morphtarget : InMorphTargetOfInterest)
	{
		const FMorphTargetLODModel& MTLOD = Morphtarget->MorphLODModels[LODIndex];
		for (int32 MorphVertexIndex = 0; MorphVertexIndex < MTLOD.Vertices.Num(); ++MorphVertexIndex)
		{
			FFinalSkinVertex* SetVert = DestVertex + MTLOD.Vertices[MorphVertexIndex].SourceIdx;
			SetVert->U = 1.0f;
			SetVert->V = 1.0f;
		}
	}
}

bool FDynamicSkelMeshObjectDataCPUSkin::UpdateClothSimulationData(USkinnedMeshComponent* InMeshComponent)
{
	USkeletalMeshComponent* SimMeshComponent = Cast<USkeletalMeshComponent>(InMeshComponent);

	if (InMeshComponent->MasterPoseComponent.IsValid() && (SimMeshComponent && SimMeshComponent->IsClothBoundToMasterComponent()))
	{
		USkeletalMeshComponent* SrcComponent = SimMeshComponent;

		// if I have master, override sim component
		SimMeshComponent = Cast<USkeletalMeshComponent>(InMeshComponent->MasterPoseComponent.Get());

		// IF we don't have sim component that is skeletalmeshcomponent, just ignore
		if (!SimMeshComponent)
		{
			return false;
		}

		WorldToLocal = SrcComponent->GetRenderMatrix().InverseFast();
		ClothBlendWeight = SrcComponent->ClothBlendWeight;
		SimMeshComponent->GetUpdateClothSimulationData(ClothSimulUpdateData, SrcComponent);

		return true;
	}

	if (SimMeshComponent)
	{
		WorldToLocal = SimMeshComponent->GetRenderMatrix().InverseFast();
		ClothBlendWeight = SimMeshComponent->ClothBlendWeight;
		SimMeshComponent->GetUpdateClothSimulationData(ClothSimulUpdateData);
		return true;
	}
	return false;
}


MSVC_PRAGMA(warning(pop))
