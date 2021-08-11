// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionSceneProxy.h"

#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "RHIDefinitions.h"
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
#include "GeometryCollection/GeometryCollectionHitProxy.h"
#endif
#if INTEL_ISPC
#if USING_CODE_ANALYSIS
    MSVC_PRAGMA( warning( push ) )
    MSVC_PRAGMA( warning( disable : ALL_CODE_ANALYSIS_WARNINGS ) )
#endif    // USING_CODE_ANALYSIS

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonportable-include-path"
#endif

#include "GeometryCollectionSceneProxy.ispc.generated.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if USING_CODE_ANALYSIS
    MSVC_PRAGMA( warning( pop ) )
#endif    // USING_CODE_ANALYSIS
#endif

static int32 GParallelGeometryCollectionBatchSize = 1024;
static TAutoConsoleVariable<int32> CVarParallelGeometryCollectionBatchSize(
	TEXT("r.ParallelGeometryCollectionBatchSize"),
	GParallelGeometryCollectionBatchSize,
	TEXT("The number of vertices per thread dispatch in a single collection. \n"),
	ECVF_Default
);

int32 GGeometryCollectionTripleBufferUploads = 1;
FAutoConsoleVariableRef CVarGeometryCollectionTripleBufferUploads(
	TEXT("r.GeometryCollectionTripleBufferUploads"),
	GGeometryCollectionTripleBufferUploads,
	TEXT("Whether to triple buffer geometry collection uploads, which allows Lock_NoOverwrite uploads which are much faster on the GPU with large amounts of data."),
	ECVF_Default
);

DEFINE_LOG_CATEGORY_STATIC(FGeometryCollectionSceneProxyLogging, Log, All);

FGeometryCollectionSceneProxy::FGeometryCollectionSceneProxy(UGeometryCollectionComponent* Component)
	: FPrimitiveSceneProxy(Component)
	, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	, NumVertices(0)
	, NumIndices(0)
	, VertexFactory(GetScene().GetFeatureLevel())
	, bSupportsManualVertexFetch(VertexFactory.SupportsManualVertexFetch(GetScene().GetFeatureLevel()))
	, bSupportsTripleBufferVertexUpload(!IsMetalPlatform(GetScene().GetShaderPlatform()) && !IsVulkanPlatform(GetScene().GetShaderPlatform()))
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	, SubSections()
	, SubSectionHitProxies()
	, SubSectionHitProxyIndexMap()
	, bUsesSubSections(false)
#endif
	, DynamicData(nullptr)
	, ConstantData(nullptr)
	, bShowBoneColors(Component->GetShowBoneColors())
	, bEnableBoneSelection(Component->GetEnableBoneSelection())
	, BoneSelectionMaterialID(Component->GetBoneSelectedMaterialID())
	, TransformVertexBuffersContainsOriginalMesh(false)
{
	Materials.Empty();
	const int32 NumMaterials = Component->GetNumMaterials();
	for (int MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
	{
		Materials.Push(Component->GetMaterial(MaterialIndex));

		if (Materials[MaterialIndex] == nullptr || !Materials[MaterialIndex]->CheckMaterialUsage_Concurrent(MATUSAGE_GeometryCollections))
		{
			Materials[MaterialIndex] = UMaterial::GetDefaultMaterial(MD_Surface);
		}
	}

	// Make sure the vertex color material has the usage flag for rendering geometry collections
	if (GEngine->VertexColorMaterial)
	{
		GEngine->VertexColorMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_GeometryCollections);
	}

	// #todo(dmp): We create the sections before we set the constant data because we need to make sure these
	// are set before the hit proxies are created via CreateHitProxies.  Ideally, all data is passed in
	// here when we create proxies, and they are thrown away if underlying geometry changes.
	TManagedArray<FGeometryCollectionSection>& InputSections = Component->GetRestCollection()->GetGeometryCollection()->Sections;

	const int32 NumSections = InputSections.Num();
	Sections.Reset(NumSections);

	for (int SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
	{
		FGeometryCollectionSection& Section = InputSections[SectionIndex];
		
		if (Section.NumTriangles > 0)
		{
			Sections.Add(Section);
		}
	}

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	// Init HitProxy array with the maximum number of subsections
	SubSectionHitProxies.SetNumZeroed(Sections.Num() * Component->GetTransformArray().Num());
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION

	// #todo(dmp): This flag means that when motion blur is turned on, it will always render geometry collections into the
	// velocity buffer.  Note that the way around this is to loop through the global matrices and test whether they have
	// changed from the prev to curr frame, but this is expensive.  We should revisit this if the draw calls for velocity
	// rendering become a problem. One solution could be to use internal solver sleeping state to drive motion blur.
	bAlwaysHasVelocity = true;

	// Build pre-skinned bounds from the rest collection, never needs to change as this is the bounds before
	// any movement, or skinning ever happens to the component so it is logically immutable.
	const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>            
										Collection     = Component->RestCollection->GetGeometryCollection();
	const TManagedArray<FBox>&			BoundingBoxes  = Collection->BoundingBox;
	const TManagedArray<FTransform>&	Transform      = Collection->Transform;
	const TManagedArray<int32>&			Parent         = Collection->Parent;
	const TManagedArray<int32>&			TransformIndex = Collection->TransformIndex;

	const int32 NumBoxes = BoundingBoxes.Num();
	PreSkinnedBounds = Component->Bounds;

	if(NumBoxes > 0)
	{
		TArray<FMatrix> TmpGlobalMatrices;
		GeometryCollectionAlgo::GlobalMatrices(Transform, Parent, TmpGlobalMatrices);

		FBox PreSkinnedBoundsTemp(ForceInit);
		bool bBoundsInit = false;
		for(int32 BoxIdx = 0; BoxIdx < NumBoxes; ++BoxIdx)
		{
			const int32 TIndex = TransformIndex[BoxIdx];
			if(Collection->IsGeometry(TIndex))
			{
				if(!bBoundsInit)
				{
					PreSkinnedBoundsTemp = BoundingBoxes[BoxIdx].TransformBy(TmpGlobalMatrices[TIndex]);
					bBoundsInit = true;
				}
				else
				{
					PreSkinnedBoundsTemp += BoundingBoxes[BoxIdx].TransformBy(TmpGlobalMatrices[TIndex]);
				}
			}
		}

		PreSkinnedBounds = FBoxSphereBounds(PreSkinnedBoundsTemp);
	}
}

FGeometryCollectionSceneProxy::~FGeometryCollectionSceneProxy()
{
	ReleaseResources();

	if (DynamicData != nullptr)
	{
		delete DynamicData;
	}

	if (ConstantData != nullptr)
	{
		delete ConstantData;
	}
}

void FGeometryCollectionSceneProxy::InitResources()
{
	check(ConstantData);
	check(IsInRenderingThread());
	
	NumVertices = ConstantData->Vertices.Num();
	NumIndices = ConstantData->Indices.Num()*3;	

	// taken from this, and expanded here to accomodate modifications for
	// GeometryCollection vertex factory data (transform and bonemap)
	// VertexBuffers.InitWithDummyData(&VertexFactory, GetRequiredVertexCount());

	// get vertex factory data
	FGeometryCollectionVertexFactory::FDataType Data;
	
	// Init buffers
	VertexBuffers.PositionVertexBuffer.Init(NumVertices);
	VertexBuffers.StaticMeshVertexBuffer.Init(NumVertices, 1);
	VertexBuffers.ColorVertexBuffer.Init(NumVertices);

	// Init resources
	VertexBuffers.PositionVertexBuffer.InitResource();
	VertexBuffers.StaticMeshVertexBuffer.InitResource();
	VertexBuffers.ColorVertexBuffer.InitResource();

	// Bind buffers
	VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, Data);
	VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, Data);
	VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&VertexFactory, Data);	
	VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&VertexFactory, Data, 0);
	VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, Data);

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	if (bEnableBoneSelection)
	{
		HitProxyIdBuffer.Init(NumVertices);
		HitProxyIdBuffer.InitResource();
	}
#endif

	IndexBuffer.NumIndices = GetRequiredIndexCount();
	IndexBuffer.InitResource();
		
	OriginalMeshIndexBuffer.NumIndices = GetRequiredIndexCount();
	OriginalMeshIndexBuffer.InitResource();	
	
	// If using manual vertex fetch, then we will setup the GPU point transform implementation
	if (bSupportsManualVertexFetch)
	{
		BoneMapBuffer.NumVertices = NumVertices;

		TransformBuffers.AddDefaulted(1);
		PrevTransformBuffers.AddDefaulted(1);

		TransformBuffers[0].NumTransforms = ConstantData->NumTransforms;
		PrevTransformBuffers[0].NumTransforms = ConstantData->NumTransforms;
		TransformBuffers[0].InitResource();
		PrevTransformBuffers[0].InitResource();

		BoneMapBuffer.InitResource();

		Data.InstanceBoneMapSRV = BoneMapBuffer.VertexBufferSRV;			
		Data.InstanceTransformSRV = TransformBuffers[0].VertexBufferSRV;
		Data.InstancePrevTransformSRV = PrevTransformBuffers[0].VertexBufferSRV;
	}

	// 
	// from InitOrUpdateResource(VertexFactory);
	//

	// also make sure to do the binding to the vertex factory
	VertexFactory.SetData(Data);

	if (!VertexFactory.IsInitialized())
	{
		VertexFactory.InitResource();
	}
	else
	{
		VertexFactory.UpdateRHI();
	}
}

void FGeometryCollectionSceneProxy::ReleaseResources()
{
	VertexBuffers.PositionVertexBuffer.ReleaseResource();
	VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();

	OriginalMeshIndexBuffer.ReleaseResource();

	if (bSupportsManualVertexFetch)
	{
		BoneMapBuffer.ReleaseResource();

		for (int32 i = 0; i < TransformBuffers.Num(); i++)
		{
			TransformBuffers[i].ReleaseResource();
			PrevTransformBuffers[i].ReleaseResource();
		}
		TransformBuffers.Reset();
	}

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	HitProxyIdBuffer.ReleaseResource();
#endif

	VertexFactory.ReleaseResource();
}

void FGeometryCollectionSceneProxy::BuildGeometry( const FGeometryCollectionConstantData* ConstantDataIn, TArray<FDynamicMeshVertex>& OutVertices, TArray<int32>& OutIndices, TArray<int32> &OutOriginalMeshIndices)
{
	OutVertices.SetNumUninitialized(ConstantDataIn->Vertices.Num());
	ParallelFor(ConstantData->Vertices.Num(), [&](int32 PointIdx)
	{
		OutVertices[PointIdx] =
			FDynamicMeshVertex(
				ConstantDataIn->Vertices[PointIdx],
				ConstantDataIn->UVs[PointIdx],
				bShowBoneColors||bEnableBoneSelection ?
				ConstantDataIn->BoneColors[PointIdx].ToFColor(true) :
				ConstantDataIn->Colors[PointIdx].ToFColor(true)
			);
		OutVertices[PointIdx].SetTangents(ConstantDataIn->TangentU[PointIdx], ConstantDataIn->TangentV[PointIdx], ConstantDataIn->Normals[PointIdx]);
	});

	check(ConstantDataIn->Indices.Num() * 3 == NumIndices);

	OutIndices.SetNumUninitialized(NumIndices);
	ParallelFor (ConstantDataIn->Indices.Num(), [&](int32 IndexIdx)
	{
		OutIndices[IndexIdx * 3 ]    = ConstantDataIn->Indices[IndexIdx].X;
		OutIndices[IndexIdx * 3 + 1] = ConstantDataIn->Indices[IndexIdx].Y;
		OutIndices[IndexIdx * 3 + 2] = ConstantDataIn->Indices[IndexIdx].Z;
	});
	
	OutOriginalMeshIndices.SetNumUninitialized(ConstantDataIn->OriginalMeshIndices.Num() * 3);
	ParallelFor(ConstantDataIn->OriginalMeshIndices.Num(), [&](int32 IndexIdx)
	{
		OutOriginalMeshIndices[IndexIdx * 3] = ConstantDataIn->OriginalMeshIndices[IndexIdx].X;
		OutOriginalMeshIndices[IndexIdx * 3 + 1] = ConstantDataIn->OriginalMeshIndices[IndexIdx].Y;
		OutOriginalMeshIndices[IndexIdx * 3 + 2] = ConstantDataIn->OriginalMeshIndices[IndexIdx].Z;
	});
}

void FGeometryCollectionSceneProxy::SetConstantData_RenderThread(FGeometryCollectionConstantData* NewConstantData, bool ForceInit)
{
	check(IsInRenderingThread());
	check(NewConstantData);

	if (ConstantData)
	{
		delete ConstantData;
		ConstantData = nullptr;
	}
	ConstantData = NewConstantData;

	if (ConstantData->Vertices.Num() != VertexBuffers.PositionVertexBuffer.GetNumVertices() || ForceInit)
	{
		ReleaseResources();
		InitResources();
	}

	TArray<int32> Indices;
	TArray<int32> OriginalMeshIndices;
	TArray<FDynamicMeshVertex> Vertices;
	BuildGeometry(ConstantData, Vertices, Indices, OriginalMeshIndices);
	check(Vertices.Num() == GetRequiredVertexCount());
	check(Indices.Num() == GetRequiredIndexCount());

	if (GetRequiredVertexCount())
	{
		ParallelFor(Vertices.Num(), [&](int32 i)
		{
			const FDynamicMeshVertex& Vertex = Vertices[i];

			VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector());
			VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TextureCoordinate[0]);
			VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
			if (bEnableBoneSelection)
			{
				// One proxy per bone
				const int32 ProxyIndex = ConstantData->BoneMap[i];
				HitProxyIdBuffer.VertexColor(i) = PerBoneHitProxies[ProxyIndex]->Id.GetColor();
			}
#endif
		});

		{
			auto& VertexBuffer = VertexBuffers.PositionVertexBuffer;
			void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
		}

		{
			auto& VertexBuffer = VertexBuffers.ColorVertexBuffer;
			void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
		}

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
		if (bEnableBoneSelection)
		{
			auto& VertexBuffer = HitProxyIdBuffer;
			void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
		}
#endif

		{
			auto& VertexBuffer = VertexBuffers.StaticMeshVertexBuffer;
			void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTangentSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), VertexBuffer.GetTangentSize());
			RHIUnlockVertexBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
		}

		{
			auto& VertexBuffer = VertexBuffers.StaticMeshVertexBuffer;
			void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTexCoordSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), VertexBuffer.GetTexCoordSize());
			RHIUnlockVertexBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
		}

		{
			void* IndexBufferData = RHILockIndexBuffer(IndexBuffer.IndexBufferRHI, 0, Indices.Num() * sizeof(int32), RLM_WriteOnly);
			FMemory::Memcpy(IndexBufferData, &Indices[0], Indices.Num() * sizeof(int32));
			RHIUnlockIndexBuffer(IndexBuffer.IndexBufferRHI);
		}

		{							
			void* OriginalMeshIndexBufferData = RHILockIndexBuffer(OriginalMeshIndexBuffer.IndexBufferRHI, 0, OriginalMeshIndices.Num() * sizeof(int32), RLM_WriteOnly);
			FMemory::Memcpy(OriginalMeshIndexBufferData, &OriginalMeshIndices[0], OriginalMeshIndices.Num() * sizeof(int32));
			RHIUnlockIndexBuffer(OriginalMeshIndexBuffer.IndexBufferRHI);
		}

		// If we are using the GeometryCollection vertex factory, populate the vertex buffer for bone map
		if (bSupportsManualVertexFetch)
		{
			void* BoneMapBufferData = RHILockVertexBuffer(BoneMapBuffer.VertexBufferRHI, 0, Vertices.Num() * sizeof(int32), RLM_WriteOnly);								
			FMemory::Memcpy(BoneMapBufferData, &ConstantData->BoneMap[0], ConstantData->BoneMap.Num() * sizeof(int32));
			RHIUnlockVertexBuffer(BoneMapBuffer.VertexBufferRHI);
		}

		// Update mesh sections
		check(Sections.Num() == ConstantData->Sections.Num());
		// #todo(dmp): We should restructure the component/SceneProxy usage to avoid this messy stuff.  We need to know the sections
		// when we create the sceneproxy for the hit proxy to work, but then we are updating the sections here with potentially differing
		// vertex counts due to hiding geometry.  Ideally, the SceneProxy is treated as const and recreated whenever the geometry
		// changes rather than this.  SetConstantData_RenderThread should be done in the constructor for the sceneproxy, most likely
		int i = 0;
		for (FGeometryCollectionSection Section : ConstantData->Sections)
		{
			if (Section.NumTriangles > 0)
			{
				FGeometryCollectionSection &NewSection = Sections[i++];
				NewSection.MaterialID = Section.MaterialID;
				NewSection.FirstIndex = Section.FirstIndex;
				NewSection.NumTriangles = Section.NumTriangles;
				NewSection.MinVertexIndex = Section.MinVertexIndex;
				NewSection.MaxVertexIndex = Section.MaxVertexIndex;				
			}
		}

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
		// Recreate or release subsections as needed
		if (bUsesSubSections)
		{
			InitializeSubSections_RenderThread();
		}
		else
		{
			ReleaseSubSections_RenderThread();
		}
	}
	else
	{
		ReleaseSubSections_RenderThread();
#endif
	}
}

void FGeometryCollectionSceneProxy::SetDynamicData_RenderThread(FGeometryCollectionDynamicData* NewDynamicData)
{
	check(IsInRenderingThread());
	if (GetRequiredVertexCount())
	{
		if (DynamicData)
		{
			delete DynamicData;
			DynamicData = nullptr;
		}
		DynamicData = NewDynamicData;
		
		check(VertexBuffers.PositionVertexBuffer.GetNumVertices() == (uint32)ConstantData->Vertices.Num());

		if (bSupportsManualVertexFetch)
		{
			const bool bLocalGeometryCollectionTripleBufferUploads = (GGeometryCollectionTripleBufferUploads != 0) && bSupportsTripleBufferVertexUpload;

			if (bLocalGeometryCollectionTripleBufferUploads && TransformBuffers.Num() == 1)
			{
				TransformBuffers.AddDefaulted(2);
				PrevTransformBuffers.AddDefaulted(2);

				for (int32 i = 1; i < 3; i++)
				{
					TransformBuffers[i].NumTransforms = ConstantData->NumTransforms;
					PrevTransformBuffers[i].NumTransforms = ConstantData->NumTransforms;
					TransformBuffers[i].InitResource();
					PrevTransformBuffers[i].InitResource();
				}
			}

			const EResourceLockMode LockMode = bLocalGeometryCollectionTripleBufferUploads ? RLM_WriteOnly_NoOverwrite : RLM_WriteOnly;

			// Copy the transform data over to the vertex buffer	
			if (DynamicData->IsDynamic)
			{
				CycleTransformBuffers(bLocalGeometryCollectionTripleBufferUploads);
				FGeometryCollectionTransformBuffer& TransformBuffer = GetCurrentTransformBuffer();
				FGeometryCollectionTransformBuffer& PrevTransformBuffer = GetCurrentPrevTransformBuffer();
				VertexFactory.SetInstanceTransformSRV(TransformBuffer.VertexBufferSRV);
				VertexFactory.SetInstancePrevTransformSRV(PrevTransformBuffer.VertexBufferSRV);

				check(TransformBuffer.NumTransforms == DynamicData->Transforms.Num());
				check(PrevTransformBuffer.NumTransforms == DynamicData->PrevTransforms.Num());

				void* VertexBufferData = RHILockVertexBuffer(TransformBuffer.VertexBufferRHI, 0, DynamicData->Transforms.Num() * sizeof(FMatrix), LockMode);
				FMemory::Memcpy(VertexBufferData, DynamicData->Transforms.GetData(), DynamicData->Transforms.Num() * sizeof(FMatrix));
				RHIUnlockVertexBuffer(TransformBuffer.VertexBufferRHI);

				void* PrevVertexBufferData = RHILockVertexBuffer(PrevTransformBuffer.VertexBufferRHI, 0, DynamicData->PrevTransforms.Num() * sizeof(FMatrix), LockMode);
				FMemory::Memcpy(PrevVertexBufferData, DynamicData->PrevTransforms.GetData(), DynamicData->PrevTransforms.Num() * sizeof(FMatrix));
				RHIUnlockVertexBuffer(PrevTransformBuffer.VertexBufferRHI);

				TransformVertexBuffersContainsOriginalMesh = false;
			}
			else if (!TransformVertexBuffersContainsOriginalMesh)
			{		
				CycleTransformBuffers(bLocalGeometryCollectionTripleBufferUploads);
				FGeometryCollectionTransformBuffer& TransformBuffer = GetCurrentTransformBuffer();
				FGeometryCollectionTransformBuffer& PrevTransformBuffer = GetCurrentPrevTransformBuffer();
				VertexFactory.SetInstanceTransformSRV(TransformBuffer.VertexBufferSRV);
				VertexFactory.SetInstancePrevTransformSRV(PrevTransformBuffer.VertexBufferSRV);

				// if we are rendering the base mesh geometry, then use rest transforms rather than the simulated one for both current and previous transforms
				void* VertexBufferData = RHILockVertexBuffer(TransformBuffer.VertexBufferRHI, 0, ConstantData->RestTransforms.Num() * sizeof(FMatrix), LockMode);
				FMemory::Memcpy(VertexBufferData, ConstantData->RestTransforms.GetData(), ConstantData->RestTransforms.Num() * sizeof(FMatrix));
				RHIUnlockVertexBuffer(TransformBuffer.VertexBufferRHI);

				void* PrevVertexBufferData = RHILockVertexBuffer(PrevTransformBuffer.VertexBufferRHI, 0, ConstantData->RestTransforms.Num() * sizeof(FMatrix), LockMode);
				FMemory::Memcpy(PrevVertexBufferData, ConstantData->RestTransforms.GetData(), ConstantData->RestTransforms.Num() * sizeof(FMatrix));
				RHIUnlockVertexBuffer(PrevTransformBuffer.VertexBufferRHI);

				TransformVertexBuffersContainsOriginalMesh = true;
			}
		}
		else
		{
			auto& VertexBuffer = VertexBuffers.PositionVertexBuffer;
			void* VertexBufferData = RHILockVertexBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);

			bool bParallelGeometryCollection = true;
			int32 TotalVertices = ConstantData->Vertices.Num();
			int32 ParallelGeometryCollectionBatchSize = CVarParallelGeometryCollectionBatchSize.GetValueOnRenderThread();

			int32 NumBatches = (TotalVertices / ParallelGeometryCollectionBatchSize);

			if (TotalVertices != ParallelGeometryCollectionBatchSize)
			{
				NumBatches++;
			}

			// Batch too small, don't bother with parallel
			if (ParallelGeometryCollectionBatchSize > TotalVertices)
			{
				bParallelGeometryCollection = false;
				ParallelGeometryCollectionBatchSize = TotalVertices;
			}

			auto GeometryCollectionBatch([&](int32 BatchNum)
			{
				int32 IndexOffset = ParallelGeometryCollectionBatchSize * BatchNum;
				int32 ThisBatchSize = ParallelGeometryCollectionBatchSize;

				// Check for final batch
				if (IndexOffset + ParallelGeometryCollectionBatchSize > NumVertices)
				{
					ThisBatchSize = TotalVertices - IndexOffset;
				}

				if (ThisBatchSize > 0)
				{
					const FMatrix* RESTRICT BoneTransformsPtr = DynamicData->IsDynamic ? DynamicData->Transforms.GetData() : ConstantData->RestTransforms.GetData();
	#if INTEL_ISPC
					uint8* VertexBufferOffset = (uint8*)VertexBufferData + (IndexOffset * VertexBuffer.GetStride());
					ispc::SetDynamicData_RenderThread(
						(ispc::FVector*)VertexBufferOffset, 
						ThisBatchSize, 
						VertexBuffer.GetStride(), 
						&ConstantData->BoneMap[IndexOffset], 
						(ispc::FMatrix*)BoneTransformsPtr,
						(ispc::FVector*)&ConstantData->Vertices[IndexOffset]);
	#else
					for (int32 i = IndexOffset; i < IndexOffset + ThisBatchSize; i++)
					{
						FVector Transformed = DynamicData->Transforms[ConstantData->BoneMap[i]].TransformPosition(ConstantData->Vertices[i]);
						FMemory::Memcpy((uint8*)VertexBufferData + (i * VertexBuffer.GetStride()), &Transformed, sizeof(FVector));
					}
	#endif
				}
			});

			ParallelFor(NumBatches, GeometryCollectionBatch, !bParallelGeometryCollection);

			RHIUnlockVertexBuffer(VertexBuffer.VertexBufferRHI);
		}
	}
}

FMaterialRenderProxy* FGeometryCollectionSceneProxy::GetMaterial(FMeshElementCollector& Collector, int32 MaterialIndex) const
{
	// material for wireframe
	/*
	never used
	auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
		GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy(IsSelected()) : nullptr,
		FLinearColor(0, 0.5f, 1.f)
	);
	Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
	*/

	// material for colored bones

	FMaterialRenderProxy* MaterialProxy = nullptr;

	if (bShowBoneColors && GEngine->VertexColorMaterial)
	{
		UMaterial* VertexColorVisualizationMaterial = GEngine->VertexColorMaterial;
		auto VertexColorVisualizationMaterialInstance = new FColoredMaterialRenderProxy(
			VertexColorVisualizationMaterial->GetRenderProxy(),
			GetSelectionColor(FLinearColor::White, false, false)
		);
		Collector.RegisterOneFrameMaterialProxy(VertexColorVisualizationMaterialInstance);
		MaterialProxy = VertexColorVisualizationMaterialInstance;
	}
	else
	{
		MaterialProxy = Materials[MaterialIndex]->GetRenderProxy();
	}

	if (MaterialProxy == nullptr)
	{
		MaterialProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
	}

	return MaterialProxy;
}

void FGeometryCollectionSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GeometryCollectionSceneProxy_GetDynamicMeshElements);
	if (GetRequiredVertexCount())
	{
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;		

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if ((VisibilityMap & (1 << ViewIndex)) == 0) { continue; }

			// Render Batches
						
			// render original mesh if it isn't dynamic and there is an unfractured mesh			
			// #todo(dmp): refactor this to share more code later
			if (!DynamicData->IsDynamic)			
			{
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
				const TArray<FGeometryCollectionSection>& SectionArray = bUsesSubSections && SubSections.Num() ? SubSections: ConstantData->OriginalMeshSections;
				UE_LOG(FGeometryCollectionSceneProxyLogging, VeryVerbose, TEXT("GetDynamicMeshElements, bUseSubSections=%d, NumSections=%d for %p."), bUsesSubSections, SectionArray.Num(), this);

#else  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
				const TArray<FGeometryCollectionSection>& SectionArray = ConstantData->OriginalMeshSections;
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION #else

				//Grab the material proxies we'll be using for each section
				TArray<FMaterialRenderProxy*, TInlineAllocator<32>> MaterialProxies;
				for (int SectionIndex = 0; SectionIndex < SectionArray.Num(); SectionIndex++)
				{
					const FGeometryCollectionSection& Section = SectionArray[SectionIndex];
					FMaterialRenderProxy* MaterialProxy = GetMaterial(Collector, Section.MaterialID);
					MaterialProxies.Add(MaterialProxy);
				}

				for (int SectionIndex = 0; SectionIndex < SectionArray.Num(); SectionIndex++)
				{
					const FGeometryCollectionSection& Section = SectionArray[SectionIndex];

					// Draw the mesh.
					FMeshBatch& Mesh = Collector.AllocateMesh();
					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.IndexBuffer = &OriginalMeshIndexBuffer;
					Mesh.bWireframe = bWireframe;
					Mesh.VertexFactory = &VertexFactory;
					Mesh.MaterialRenderProxy = MaterialProxies[SectionIndex];

					/*
					bool bHasPrecomputedVolumetricLightmap;
					FMatrix PreviousLocalToWorld;
					int32 SingleCaptureIndex;
					bool bOutputVelocity;
					GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);
					BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
					*/

					BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
					BatchElement.FirstIndex = Section.FirstIndex;
					BatchElement.NumPrimitives = Section.NumTriangles;
					BatchElement.MinVertexIndex = Section.MinVertexIndex;
					BatchElement.MaxVertexIndex = Section.MaxVertexIndex;
					Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
					Mesh.Type = PT_TriangleList;
					Mesh.DepthPriorityGroup = SDPG_World;
					Mesh.bCanApplyViewModeOverrides = true;
#if WITH_EDITOR
					if (GIsEditor)
					{
						Mesh.BatchHitProxyId = Section.HitProxy ? Section.HitProxy->Id : FHitProxyId();
					}
#endif // WITH_EDITOR
					Collector.AddMesh(ViewIndex, Mesh);
				}
			}
			else
			{
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
				const TArray<FGeometryCollectionSection>& SectionArray = bUsesSubSections && SubSections.Num() ? SubSections: Sections;
				UE_LOG(FGeometryCollectionSceneProxyLogging, VeryVerbose, TEXT("GetDynamicMeshElements, bUseSubSections=%d, NumSections=%d for %p."), bUsesSubSections, SectionArray.Num(), this);

#else  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
				const TArray<FGeometryCollectionSection>& SectionArray = Sections;
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION #else

				//Grab the material proxies we'll be using for each section
				TArray<FMaterialRenderProxy*, TInlineAllocator<32>> MaterialProxies;
				for (int SectionIndex = 0; SectionIndex < SectionArray.Num(); SectionIndex++)
				{
					const FGeometryCollectionSection& Section = SectionArray[SectionIndex];

					FMaterialRenderProxy* MaterialProxy = GetMaterial(Collector, Section.MaterialID);
					MaterialProxies.Add(MaterialProxy);
				}
				for (int SectionIndex = 0; SectionIndex < SectionArray.Num(); SectionIndex++)
				{
					const FGeometryCollectionSection& Section = SectionArray[SectionIndex];

					// Draw the mesh.
					FMeshBatch& Mesh = Collector.AllocateMesh();
					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.IndexBuffer = &IndexBuffer;
					Mesh.bWireframe = bWireframe;
					Mesh.VertexFactory = &VertexFactory;
					Mesh.MaterialRenderProxy = MaterialProxies[SectionIndex];
					BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
					BatchElement.FirstIndex = Section.FirstIndex;
					BatchElement.NumPrimitives = Section.NumTriangles;
					BatchElement.MinVertexIndex = Section.MinVertexIndex;
					BatchElement.MaxVertexIndex = Section.MaxVertexIndex;
					Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
					Mesh.Type = PT_TriangleList;
					Mesh.DepthPriorityGroup = SDPG_World;
					Mesh.bCanApplyViewModeOverrides = true;
#if WITH_EDITOR
					if (GIsEditor)
					{
						Mesh.BatchHitProxyId = Section.HitProxy ? Section.HitProxy->Id : FHitProxyId();
					}
#endif // WITH_EDITOR
					Collector.AddMesh(ViewIndex, Mesh);
				}
			}

			// Highlight selected bone using specialized material - when rendering bones as colors we don't need to run this code as the
			// bone selection is already contained in the rendered colors
			// #note: This renders the geometry again but with the bone selection material.  Ideally we'd have one render pass and one
			// material.
			if (bShowBoneColors||bEnableBoneSelection)
			{
				FMaterialRenderProxy* MaterialRenderProxy = Materials[BoneSelectionMaterialID]->GetRenderProxy();

				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];
				BatchElement.IndexBuffer = &IndexBuffer;
				Mesh.bWireframe = bWireframe;
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MaterialRenderProxy;
				BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = GetRequiredIndexCount() / 3;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = GetRequiredVertexCount();
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(ViewIndex, Mesh);
			}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
#endif
		}
	}
}

FPrimitiveViewRelevance FGeometryCollectionSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bDynamicRelevance = true;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);

	// #todo(dmp): why does this crash?
	// Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

	return Result;

}

#if WITH_EDITOR
HHitProxy* FGeometryCollectionSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies)
{
	// In order to be able to click on static meshes when they're batched up, we need to have catch all default
	// hit proxy to return.
	HHitProxy* DefaultHitProxy = FPrimitiveSceneProxy::CreateHitProxies(Component, OutHitProxies);

	// @todo FractureTools - Reconcile with subsection hit proxies.  Subsection is a draw call per hit proxy but is not suitable per-vertex as written
	if (bEnableBoneSelection)
	{
		UGeometryCollectionComponent* GeometryCollectionComp = CastChecked<UGeometryCollectionComponent>(Component);
		int32 NumTransforms = GeometryCollectionComp->GetTransformArray().Num();
		PerBoneHitProxies.Empty();
		for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
		{
			HGeometryCollectionBone* HitProxy = new HGeometryCollectionBone(GeometryCollectionComp, TransformIndex);
			PerBoneHitProxies.Add(HitProxy);
		}

		OutHitProxies.Append(PerBoneHitProxies);
	}
	else if (Component->GetOwner())
	{
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
		const int32 NumTransforms = (Sections.Num() > 0) ? SubSectionHitProxies.Num() / Sections.Num(): 0;
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION

		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			// Create HitProxy for regular material based sections, and update existing section
			FGeometryCollectionSection& Section = Sections[SectionIndex];

			const int32 MaterialID = Section.MaterialID;
			HActor* const HitProxy = new HActor(Component->GetOwner(), Component, SectionIndex, MaterialID);

			OutHitProxies.Add(HitProxy);
			Section.HitProxy = HitProxy;

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
			// Create HitProxy per transform index using the same material Id than the current sections
			// All combinations of material id/transform index are populated,
			// since it can't be assumed that any of them won't be needed.
			const int32 SectionOffset = SectionIndex * NumTransforms;

			for (int32 TransformIndex = 0; TransformIndex < NumTransforms; ++TransformIndex)
			{
				static const int32 SubSectionIndex = INDEX_NONE;  // The index will get updated later for existing subsections
				
				HGeometryCollection* const SubSectionHitProxy = new HGeometryCollection(Component->GetOwner(), Component, SubSectionIndex, MaterialID, TransformIndex);

				OutHitProxies.Add(SubSectionHitProxy);
				SubSectionHitProxies[SectionOffset + TransformIndex] = SubSectionHitProxy;
			}
		}

		// Update existing subsections and any HitProxy's section index that is currently being used
		for (int32 SubSectionIndex = 0; SubSectionIndex < SubSections.Num(); ++SubSectionIndex)
		{
			HGeometryCollection* const SubSectionHitProxy = SubSectionHitProxies[SubSectionHitProxyIndexMap[SubSectionIndex]];

			SubSections[SubSectionIndex].HitProxy = SubSectionHitProxy;
			SubSectionHitProxy->SectionIndex = SubSectionIndex;
#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION
		}
	}

	return DefaultHitProxy;
}
#endif // WITH_EDITOR

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
void FGeometryCollectionSceneProxy::UseSubSections(bool bInUsesSubSections, bool bForceInit)
{
	if (!bForceInit)
	{
		bUsesSubSections = bInUsesSubSections;
	}
	else if (bInUsesSubSections)
	{
		FGeometryCollectionSceneProxy* GeometryCollectionSceneProxy = this;
		ENQUEUE_RENDER_COMMAND(InitializeSubSections)(
			[GeometryCollectionSceneProxy](FRHICommandListImmediate& RHICmdList)
			{
				if (GeometryCollectionSceneProxy)
				{
					GeometryCollectionSceneProxy->InitializeSubSections_RenderThread();
					GeometryCollectionSceneProxy->bUsesSubSections = true;
					UE_LOG(FGeometryCollectionSceneProxyLogging, Verbose, TEXT("UseSubSections, %d SubSections initialized for %p."), GeometryCollectionSceneProxy->SubSections.Num(), GeometryCollectionSceneProxy);
				}
			});
	}
	else
	{
		FGeometryCollectionSceneProxy* GeometryCollectionSceneProxy = this;
		ENQUEUE_RENDER_COMMAND(ReleaseSubSections)(
			[GeometryCollectionSceneProxy](FRHICommandListImmediate& RHICmdList)
			{
				if (GeometryCollectionSceneProxy)
				{
					GeometryCollectionSceneProxy->ReleaseSubSections_RenderThread();
					GeometryCollectionSceneProxy->bUsesSubSections = false;
					UE_LOG(FGeometryCollectionSceneProxyLogging, Verbose, TEXT("UseSubSections, SubSections released for %p."), GeometryCollectionSceneProxy);
				}
			});
	}
}

void FGeometryCollectionSceneProxy::InitializeSubSections_RenderThread()
{
	// Exit now if there isn't any data
	if (!ConstantData)
	{
		SubSections.Empty();
		SubSectionHitProxyIndexMap.Empty();
		return;
	}

	// Retrieve the correct arrays depending on the dynamic state
	const bool bIsDynamic = DynamicData && DynamicData->IsDynamic;
	const TArray<FGeometryCollectionSection>& SectionArray = bIsDynamic ? Sections: ConstantData->OriginalMeshSections;
	const TArray<FIntVector>& IndexArray = bIsDynamic ? ConstantData->Indices: ConstantData->OriginalMeshIndices;
	const TArray<int32>& BoneMap = ConstantData->BoneMap;

	// Reserve sub sections array with a minimum of one transform per section
	SubSections.Empty(SectionArray.Num());
	SubSectionHitProxyIndexMap.Empty(SectionArray.Num());

	// Lambda that adds a new subsection and update the HitProxy section index
	auto AddSubSection = [this, IndexArray](int32 HitProxyIndex, const FGeometryCollectionSection& Section, int32 FirstFaceIndex, int32 EndFaceIndex)
	{
		// Find the matching HitProxy for this transform/section
		HGeometryCollection* const SubSectionHitProxy = SubSectionHitProxies[HitProxyIndex];

		// Add the subsection
		FGeometryCollectionSection SubSection;
		SubSection.MaterialID = Section.MaterialID;
		SubSection.FirstIndex = FirstFaceIndex * 3;
		SubSection.NumTriangles = EndFaceIndex - FirstFaceIndex;
		{
			// Find out new min/max vertex indices
			check(SubSection.NumTriangles > 0);
			SubSection.MinVertexIndex = TNumericLimits<int32>::Max();
			SubSection.MaxVertexIndex = TNumericLimits<int32>::Min();
			for (int32 FaceIndex = FirstFaceIndex; FaceIndex < EndFaceIndex; ++FaceIndex)
			{
				SubSection.MinVertexIndex = FMath::Min(SubSection.MinVertexIndex, IndexArray[FaceIndex].GetMin());
				SubSection.MaxVertexIndex = FMath::Max(SubSection.MaxVertexIndex, IndexArray[FaceIndex].GetMax());
			}
			check(SubSection.MinVertexIndex >= Section.MinVertexIndex && SubSection.MinVertexIndex <= Section.MaxVertexIndex)
			check(SubSection.MaxVertexIndex >= Section.MinVertexIndex && SubSection.MaxVertexIndex <= Section.MaxVertexIndex)
		}
		SubSection.HitProxy = SubSectionHitProxy;
		const int32 SubSectionIndex = SubSections.Add(SubSection);

		// Keep the HitProxy index in a map in case this section's HitProxy pointer ever needs to be updated (e.g. after CreateHitProxies is called)
		SubSectionHitProxyIndexMap.Add(SubSectionIndex, HitProxyIndex);

		// Update HitProxy with this subsection index
		if (SubSectionHitProxy)
		{
			SubSectionHitProxy->SectionIndex = SubSectionIndex;
		}
	};

	// Create subsections per transform
	const int32 NumTransforms = (SectionArray.Num() > 0) ? SubSectionHitProxies.Num() / SectionArray.Num(): 0;

	for (int32 SectionIndex = 0; SectionIndex < SectionArray.Num(); ++SectionIndex)
	{
		const int32 SectionOffset = SectionIndex * NumTransforms;

		const FGeometryCollectionSection& Section = SectionArray[SectionIndex];
		check(Section.NumTriangles > 0);  // Sections are not created with zero triangles

		const int32 FirstFaceIndex = Section.FirstIndex / 3;
		const int32 EndFaceIndex = FirstFaceIndex + Section.NumTriangles;

		int32 TransformIndex = BoneMap[IndexArray[FirstFaceIndex][0]];  // Assumes one transform per triangle
		int32 FaceIndex = FirstFaceIndex;

		for (int32 NextFaceIndex = FaceIndex + 1; NextFaceIndex < EndFaceIndex; ++NextFaceIndex)
		{
			const int32 NextTransformIndex = BoneMap[IndexArray[NextFaceIndex][0]];  // Assumes one transform per triangle
			if (TransformIndex != NextTransformIndex)
			{
				// Add the current subsection
				AddSubSection(SectionOffset + TransformIndex, Section, FaceIndex, NextFaceIndex);

				// Update variables for the next subsection
				TransformIndex = NextTransformIndex;
				FaceIndex = NextFaceIndex;
			}
		}

		// Add the last remaining subsection
		AddSubSection(SectionOffset + TransformIndex, Section, FaceIndex, EndFaceIndex);
	}
}

void FGeometryCollectionSceneProxy::ReleaseSubSections_RenderThread()
{
	SubSections.Reset();
	SubSectionHitProxyIndexMap.Reset();
}

#endif  // #if GEOMETRYCOLLECTION_EDITOR_SELECTION

void FGeometryCollectionSceneProxy::GetPreSkinnedLocalBounds(FBoxSphereBounds& OutBounds) const
{
	OutBounds = PreSkinnedBounds;
}
