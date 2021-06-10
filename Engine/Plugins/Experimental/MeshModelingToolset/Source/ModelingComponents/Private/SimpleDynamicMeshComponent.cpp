// Copyright Epic Games, Inc. All Rights Reserved. 

#include "SimpleDynamicMeshComponent.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "VertexFactory.h"
#include "MaterialShared.h"
#include "Engine/CollisionProfile.h"
#include "Materials/Material.h"
#include "LocalVertexFactory.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "StaticMeshResources.h"
#include "StaticMeshAttributes.h"
#include "Async/Async.h"

#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "DynamicMeshChangeTracker.h"
#include "MeshTransforms.h"

// default proxy for this component
#include "SimpleDynamicMeshSceneProxy.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

namespace
{
	// probably should be something defined for the whole tool framework...
#if WITH_EDITOR
	static EAsyncExecution SimpleDynamicMeshComponentAsyncExecTarget = EAsyncExecution::LargeThreadPool;
#else
	static EAsyncExecution SimpleDynamicMeshComponentAsyncExecTarget = EAsyncExecution::ThreadPool;
#endif
}




USimpleDynamicMeshComponent::USimpleDynamicMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	MeshObject = CreateDefaultSubobject<UDynamicMesh>(TEXT("DynamicMesh"));
	//MeshObject->SetFlags(RF_Transactional);

	MeshObjectChangedHandle = MeshObject->OnMeshChanged().AddUObject(this, &USimpleDynamicMeshComponent::OnMeshObjectChanged);

	Tangents.SetMesh(GetMesh());
}


void USimpleDynamicMeshComponent::PostLoad()
{
	Super::PostLoad();

	check(MeshObject != nullptr);
	MeshObjectChangedHandle = MeshObject->OnMeshChanged().AddUObject(this, &USimpleDynamicMeshComponent::OnMeshObjectChanged);

	Tangents.SetMesh(GetMesh());

	ResetProxy();
}


FDynamicMesh3* USimpleDynamicMeshComponent::GetRenderMesh()
{
	if (RenderMeshPostProcessor && RenderMesh)
	{
		return RenderMesh.Get();
	}
	else
	{
		return GetMesh();
	}
}

const FDynamicMesh3* USimpleDynamicMeshComponent::GetRenderMesh() const
{
	if (RenderMeshPostProcessor && RenderMesh)
	{
		return RenderMesh.Get();
	}
	else
	{
		return GetMesh();
	}
}

void USimpleDynamicMeshComponent::InitializeMesh(const FMeshDescription* MeshDescription)
{
	FMeshDescriptionToDynamicMesh Converter;
	MeshObject->Reset();
	Converter.Convert(MeshDescription, *GetMesh());
	if (TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		Converter.CopyTangents(MeshDescription, GetMesh(), &Tangents);
	}

	NotifyMeshUpdated();
}

void USimpleDynamicMeshComponent::SetRenderMeshPostProcessor(TUniquePtr<IRenderMeshPostProcessor> Processor)
{
	RenderMeshPostProcessor = MoveTemp(Processor);
	if (RenderMeshPostProcessor)
	{
		if (!RenderMesh)
		{
			RenderMesh = MakeUnique<FDynamicMesh3>(*GetMesh());
		}
	}
	else
	{
		// No post processor, no render mesh
		RenderMesh = nullptr;
	}
}

void USimpleDynamicMeshComponent::UpdateTangents(const FMeshTangentsf* ExternalTangents, bool bFastUpdateIfPossible)
{
	Tangents.CopyTriVertexTangents(*ExternalTangents);
	if (bFastUpdateIfPossible)
	{
		FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexNormals);
	}
	else
	{
		NotifyMeshUpdated();
	}
}

void USimpleDynamicMeshComponent::UpdateTangents(const FMeshTangentsd* ExternalTangents, bool bFastUpdateIfPossible)
{
	Tangents.CopyTriVertexTangents(*ExternalTangents);
	if (bFastUpdateIfPossible)
	{
		FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexNormals);
	}
	else
	{
		NotifyMeshUpdated();
	}
}



void USimpleDynamicMeshComponent::ApplyTransform(const UE::Geometry::FTransform3d& Transform, bool bInvert)
{
	if (bInvert)
	{
		MeshTransforms::ApplyTransformInverse(*GetMesh(), Transform);
	}
	else
	{
		MeshTransforms::ApplyTransform(*GetMesh(), Transform);
	}

	NotifyMeshUpdated();
}


void USimpleDynamicMeshComponent::Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions)
{
	FDynamicMeshToMeshDescription Converter(ConversionOptions);
	if (!bHaveModifiedTopology)
	{
		Converter.UpdateUsingConversionOptions(GetMesh(), *MeshDescription);
	}
	else
	{
		Converter.Convert(GetMesh(), *MeshDescription);
	}
}






const FMeshTangentsf* USimpleDynamicMeshComponent::GetTangents()
{
	if (TangentsType == EDynamicMeshTangentCalcType::NoTangents)
	{
		return nullptr;
	}
	
	if (TangentsType == EDynamicMeshTangentCalcType::AutoCalculated)
	{
		if (bTangentsValid == false && GetMesh()->HasAttributes())
		{
			FDynamicMeshUVOverlay* UVOverlay = GetMesh()->Attributes()->PrimaryUV();
			FDynamicMeshNormalOverlay* NormalOverlay = GetMesh()->Attributes()->PrimaryNormals();
			if (UVOverlay != nullptr && NormalOverlay != nullptr)
			{
				Tangents.ComputeTriVertexTangents(NormalOverlay, UVOverlay, FComputeTangentsOptions());
				bTangentsValid = true;
			}
		}
		return (bTangentsValid) ? &Tangents : nullptr;
	}

	// in this mode we assume the tangents are valid
	ensure(TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated);
	if (TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		// if you hit this, you did not request ExternallyCalculated tangents before initializing this PreviewMesh
		ensure(Tangents.GetTangents().Num() > 0);
	}

	return &Tangents;
}



void USimpleDynamicMeshComponent::UpdateLocalBounds()
{
	LocalBounds = GetMesh()->GetBounds(true);
	if (LocalBounds.MaxDim() <= 0)
	{
		LocalBounds = FAxisAlignedBox3d(FVector3d::Zero(), FMathf::ZeroTolerance);
	}
}

FSimpleDynamicMeshSceneProxy* USimpleDynamicMeshComponent::GetCurrentSceneProxy() 
{ 
	if (bProxyValid)
	{
		return (FSimpleDynamicMeshSceneProxy*)SceneProxy;
	}
	return nullptr;
}


void USimpleDynamicMeshComponent::ResetProxy()
{
	bProxyValid = false;

	// Need to recreate scene proxy to send it over
	MarkRenderStateDirty();
	UpdateLocalBounds();
	UpdateBounds();

	if (TangentsType != EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		bTangentsValid = false;
	}

	// this seems speculative, ie we may not actually have a mesh update, but we currently ResetProxy() in lots
	// of places where that is what it means
	GetDynamicMesh()->PostRealtimeUpdate();
}

void USimpleDynamicMeshComponent::NotifyMeshUpdated()
{
	if (RenderMeshPostProcessor)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
	}

	ResetProxy();
}


void USimpleDynamicMeshComponent::FastNotifyColorsUpdated()
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (Proxy)
	{
		if (HasTriangleColorFunction() && Proxy->bUsePerTriangleColor == false )
		{
			Proxy->bUsePerTriangleColor = true;
			Proxy->PerTriangleColorFunc = [this](const FDynamicMesh3* MeshIn, int TriangleID) { return GetTriangleColor(MeshIn, TriangleID); };
		} 
		else if ( !HasTriangleColorFunction() && Proxy->bUsePerTriangleColor == true)
		{
			Proxy->bUsePerTriangleColor = false;
			Proxy->PerTriangleColorFunc = nullptr;
		}

		Proxy->FastUpdateVertices(false, false, true, false);
		//MarkRenderDynamicDataDirty();
	}
	else
	{
		ResetProxy();
	}
}



void USimpleDynamicMeshComponent::FastNotifyPositionsUpdated(bool bNormals, bool bColors, bool bUVs)
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (Proxy)
	{
		// calculate bounds while we are updating vertices
		TFuture<void> UpdateBoundsCalc;
		UpdateBoundsCalc = Async(SimpleDynamicMeshComponentAsyncExecTarget, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastPositionsUpdate_AsyncBoundsUpdate);
			UpdateLocalBounds();
		});

		GetCurrentSceneProxy()->FastUpdateVertices(true, bNormals, bColors, bUVs);

		//MarkRenderDynamicDataDirty();
		MarkRenderTransformDirty();
		UpdateBoundsCalc.Wait();
		UpdateBounds();

		GetDynamicMesh()->PostRealtimeUpdate();
	}
	else
	{
		ResetProxy();
	}
}


void USimpleDynamicMeshComponent::FastNotifyVertexAttributesUpdated(bool bNormals, bool bColors, bool bUVs)
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (Proxy && ensure(bNormals || bColors || bUVs) )
	{
		GetCurrentSceneProxy()->FastUpdateVertices(false, bNormals, bColors, bUVs);
		//MarkRenderDynamicDataDirty();
		//MarkRenderTransformDirty();

		GetDynamicMesh()->PostRealtimeUpdate();
	}
	else
	{
		ResetProxy();
	}
}


void USimpleDynamicMeshComponent::FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags UpdatedAttributes)
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (Proxy && ensure(UpdatedAttributes != EMeshRenderAttributeFlags::None))
	{
		bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;

		// calculate bounds while we are updating vertices
		TFuture<void> UpdateBoundsCalc;
		if (bPositions)
		{
			UpdateBoundsCalc = Async(SimpleDynamicMeshComponentAsyncExecTarget, [this]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexAttribUpdate_AsyncBoundsUpdate);
				UpdateLocalBounds();
			});
		}

		GetCurrentSceneProxy()->FastUpdateVertices(bPositions,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);

		if (bPositions)
		{
			MarkRenderTransformDirty();
			UpdateBoundsCalc.Wait();
			UpdateBounds();
		}

		GetDynamicMesh()->PostRealtimeUpdate();
	}
	else
	{
		ResetProxy();
	}
}


void USimpleDynamicMeshComponent::FastNotifyUVsUpdated()
{
	FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexUVs);
}



void USimpleDynamicMeshComponent::FastNotifySecondaryTrianglesChanged()
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (Proxy)
	{
		GetCurrentSceneProxy()->FastUpdateAllIndexBuffers();
		GetDynamicMesh()->PostRealtimeUpdate();
	}
	else
	{
		ResetProxy();
	}
}


void USimpleDynamicMeshComponent::FastNotifyTriangleVerticesUpdated(const TArray<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes)
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	bool bUpdateSecondarySort = (SecondaryTriFilterFunc) &&
		((UpdatedAttributes & EMeshRenderAttributeFlags::SecondaryIndexBuffers) != EMeshRenderAttributeFlags::None);

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (!Proxy)
	{
		ResetProxy();
	}
	else if ( ! Decomposition )
	{
		FastNotifyVertexAttributesUpdated(UpdatedAttributes);
		if (bUpdateSecondarySort)
		{
			Proxy->FastUpdateAllIndexBuffers();
		}
		GetDynamicMesh()->PostRealtimeUpdate();
	}
	else
	{
		// compute list of sets to update
		TArray<int32> UpdatedSets;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_FindSets);
			for (int32 tid : Triangles)
			{
				int32 SetID = Decomposition->GetGroupForTriangle(tid);
				UpdatedSets.AddUnique(SetID);
			}
		}

		bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;

		// calculate bounds while we are updating vertices
		TFuture<void> UpdateBoundsCalc;
		if (bPositions)
		{
			UpdateBoundsCalc = Async(SimpleDynamicMeshComponentAsyncExecTarget, [this]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_AsyncBoundsUpdate);
				UpdateLocalBounds();
			});
		}

		// update the render buffers
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_ApplyUpdate);
			Proxy->FastUpdateVertices(UpdatedSets, bPositions,
				(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
				(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
				(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);
		}

		if (bUpdateSecondarySort)
		{
			Proxy->FastUpdateIndexBuffers(UpdatedSets);
		}

		if (bPositions)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_FinalPositionsUpdate);
			MarkRenderTransformDirty();
			UpdateBoundsCalc.Wait();
			UpdateBounds();
		}

		GetDynamicMesh()->PostRealtimeUpdate();
	}
}




void USimpleDynamicMeshComponent::FastNotifyTriangleVerticesUpdated(const TSet<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes)
{
	// should not be using fast paths if we have to run mesh postprocessor
	if (ensure(!RenderMeshPostProcessor) == false)
	{
		RenderMeshPostProcessor->ProcessMesh(*GetMesh(), *RenderMesh);
		ResetProxy();
		return;
	}

	bool bUpdateSecondarySort = (SecondaryTriFilterFunc) &&
		((UpdatedAttributes & EMeshRenderAttributeFlags::SecondaryIndexBuffers) != EMeshRenderAttributeFlags::None);

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (!Proxy)
	{
		ResetProxy();
	}
	else if (!Decomposition)
	{
		FastNotifyVertexAttributesUpdated(UpdatedAttributes);
		if (bUpdateSecondarySort)
		{
			Proxy->FastUpdateAllIndexBuffers();
		}
		GetDynamicMesh()->PostRealtimeUpdate();
	}
	else
	{
		// compute list of sets to update
		TArray<int32> UpdatedSets;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_FindSets);
			for (int32 tid : Triangles)
			{
				int32 SetID = Decomposition->GetGroupForTriangle(tid);
				UpdatedSets.AddUnique(SetID);
			}
		}

		bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;

		// calculate bounds while we are updating vertices
		TFuture<void> UpdateBoundsCalc;
		if (bPositions)
		{
			UpdateBoundsCalc = Async(SimpleDynamicMeshComponentAsyncExecTarget, [this]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_AsyncBoundsUpdate);
				UpdateLocalBounds();
			});
		}

		// update the render buffers
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_ApplyUpdate);
			Proxy->FastUpdateVertices(UpdatedSets, bPositions,
				(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
				(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
				(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_UpdateIndexBuffers);
			if (bUpdateSecondarySort)
			{
				Proxy->FastUpdateIndexBuffers(UpdatedSets);
			}
		}

		// finish up, have to wait for background bounds recalculation here
		if (bPositions)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_FinalPositionsUpdate);
			MarkRenderTransformDirty();
			UpdateBoundsCalc.Wait();
			UpdateBounds();
		}

		GetDynamicMesh()->PostRealtimeUpdate();
	}
}



/**
 * Compute the combined bounding-box of the Triangles array in parallel, by computing
 * partial boxes for subsets of this array, and then combining those boxes.
 * TODO: this should move to a pulbic utility function, and possibly the block-based ParallelFor
 * should be refactored out into something more general, as this pattern is useful in many places...
 */
static FAxisAlignedBox3d ParallelComputeROIBounds(const FDynamicMesh3& Mesh, const TArray<int32>& Triangles)
{
	FAxisAlignedBox3d FinalBounds = FAxisAlignedBox3d::Empty();
	FCriticalSection FinalBoundsLock;
	int32 N = Triangles.Num();
	constexpr int32 BlockSize = 4096;
	int32 Blocks = (N / BlockSize) + 1;
	ParallelFor(Blocks, [&](int bi)
	{
		FAxisAlignedBox3d BlockBounds = FAxisAlignedBox3d::Empty();
		for (int32 k = 0; k < BlockSize; ++k)
		{
			int32 i = bi * BlockSize + k;
			if (i < N)
			{
				int32 tid = Triangles[i];
				const FIndex3i& TriV = Mesh.GetTriangleRef(tid);
				BlockBounds.Contain(Mesh.GetVertexRef(TriV.A));
				BlockBounds.Contain(Mesh.GetVertexRef(TriV.B));
				BlockBounds.Contain(Mesh.GetVertexRef(TriV.C));
			}
		}
		FinalBoundsLock.Lock();
		FinalBounds.Contain(BlockBounds);
		FinalBoundsLock.Unlock();
	});
	return FinalBounds;
}



TFuture<bool> USimpleDynamicMeshComponent::FastNotifyTriangleVerticesUpdated_TryPrecompute(
	const TArray<int32>& Triangles,
	TArray<int32>& UpdateSetsOut,
	FAxisAlignedBox3d& BoundsOut)
{
	if ((!!RenderMeshPostProcessor) || (GetCurrentSceneProxy() == nullptr) || (!Decomposition))
	{
		// is there a simpler way to do this? cannot seem to just make a TFuture<bool>...
		return Async(SimpleDynamicMeshComponentAsyncExecTarget, []() { return false; });
	}

	return Async(SimpleDynamicMeshComponentAsyncExecTarget, [this, &Triangles, &UpdateSetsOut, &BoundsOut]()
	{
		TFuture<void> ComputeBounds = Async(SimpleDynamicMeshComponentAsyncExecTarget, [this, &BoundsOut, &Triangles]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdatePrecomp_CalcBounds);
			BoundsOut = ParallelComputeROIBounds(*GetMesh(), Triangles);
		});

		TFuture<void> ComputeSets = Async(SimpleDynamicMeshComponentAsyncExecTarget, [this, &UpdateSetsOut, &Triangles]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdatePrecomp_FindSets);
			int32 NumBuffers = Decomposition->Num();
			TArray<std::atomic<bool>> BufferFlags;
			BufferFlags.SetNum(NumBuffers);
			for (int32 k = 0; k < NumBuffers; ++k)
			{
				BufferFlags[k] = false;
			}
			ParallelFor(Triangles.Num(), [&](int32 k)
			{
				int32 SetID = Decomposition->GetGroupForTriangle(Triangles[k]);
				BufferFlags[SetID] = true;
			});
			UpdateSetsOut.Reset();
			for (int32 k = 0; k < NumBuffers; ++k)
			{
				if (BufferFlags[k])
				{
					UpdateSetsOut.Add(k);
				}
			}

		});

		ComputeSets.Wait();
		ComputeBounds.Wait();

		return true;
	});
}


void USimpleDynamicMeshComponent::FastNotifyTriangleVerticesUpdated_ApplyPrecompute(
	const TArray<int32>& Triangles,
	EMeshRenderAttributeFlags UpdatedAttributes, 
	TFuture<bool>& Precompute, 
	const TArray<int32>& UpdateSets, 
	const FAxisAlignedBox3d& UpdateSetBounds)
{
	Precompute.Wait();

	bool bPrecomputeOK = Precompute.Get();
	if (bPrecomputeOK == false || GetCurrentSceneProxy() == nullptr )
	{
		FastNotifyTriangleVerticesUpdated(Triangles, UpdatedAttributes);
		return;
	}

	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;
	bool bUpdateSecondarySort = (SecondaryTriFilterFunc) &&
		((UpdatedAttributes & EMeshRenderAttributeFlags::SecondaryIndexBuffers) != EMeshRenderAttributeFlags::None);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_ApplyUpdate);
		Proxy->FastUpdateVertices(UpdateSets, bPositions,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_UpdateIndexBuffers);
		if (bUpdateSecondarySort)
		{
			Proxy->FastUpdateIndexBuffers(UpdateSets);
		}
	}

	if (bPositions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SimpleDynamicMeshComponent_FastVertexUpdate_FinalPositionsUpdate);
		MarkRenderTransformDirty();
		LocalBounds.Contain(UpdateSetBounds);
		UpdateBounds();
	}

	GetDynamicMesh()->PostRealtimeUpdate();
}





FPrimitiveSceneProxy* USimpleDynamicMeshComponent::CreateSceneProxy()
{
	// if this is not always the case, we have made incorrect assumptions
	ensure(GetCurrentSceneProxy() == nullptr);

	FSimpleDynamicMeshSceneProxy* NewProxy = nullptr;
	if (GetMesh()->TriangleCount() > 0)
	{
		NewProxy = new FSimpleDynamicMeshSceneProxy(this);

		if (TriangleColorFunc)
		{
			NewProxy->bUsePerTriangleColor = true;
			NewProxy->PerTriangleColorFunc = [this](const FDynamicMesh3* MeshIn, int TriangleID) { return GetTriangleColor(MeshIn, TriangleID); };
		}

		if (SecondaryTriFilterFunc)
		{
			NewProxy->bUseSecondaryTriBuffers = true;
			NewProxy->SecondaryTriFilterFunc = [this](const FDynamicMesh3* MeshIn, int32 TriangleID) 
			{ 
				return (SecondaryTriFilterFunc) ? SecondaryTriFilterFunc(MeshIn, TriangleID) : false;
			};
		}

		if (Decomposition)
		{
			NewProxy->InitializeFromDecomposition(Decomposition);
		}
		else
		{
			NewProxy->Initialize();
		}
	}

	bProxyValid = true;
	return NewProxy;
}



void USimpleDynamicMeshComponent::NotifyMaterialSetUpdated()
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->UpdatedReferencedMaterials();
	}
}


void USimpleDynamicMeshComponent::SetTriangleColorFunction(
	TUniqueFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFuncIn,
	EDynamicMeshComponentRenderUpdateMode UpdateMode)
{
	TriangleColorFunc = MoveTemp(TriangleColorFuncIn);

	if (UpdateMode == EDynamicMeshComponentRenderUpdateMode::FastUpdate)
	{
		FastNotifyColorsUpdated();
	}
	else if (UpdateMode == EDynamicMeshComponentRenderUpdateMode::FullUpdate)
	{
		NotifyMeshUpdated();
	}
}

void USimpleDynamicMeshComponent::ClearTriangleColorFunction(EDynamicMeshComponentRenderUpdateMode UpdateMode)
{
	if (TriangleColorFunc)
	{
		TriangleColorFunc = nullptr;

		if (UpdateMode == EDynamicMeshComponentRenderUpdateMode::FastUpdate)
		{
			FastNotifyColorsUpdated();
		}
		else if (UpdateMode == EDynamicMeshComponentRenderUpdateMode::FullUpdate)
		{
			NotifyMeshUpdated();
		}
	}
}

bool USimpleDynamicMeshComponent::HasTriangleColorFunction()
{
	return !!TriangleColorFunc;
}




void USimpleDynamicMeshComponent::EnableSecondaryTriangleBuffers(TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFuncIn)
{
	SecondaryTriFilterFunc = MoveTemp(SecondaryTriFilterFuncIn);
	NotifyMeshUpdated();
}

void USimpleDynamicMeshComponent::DisableSecondaryTriangleBuffers()
{
	SecondaryTriFilterFunc = nullptr;
	NotifyMeshUpdated();
}


void USimpleDynamicMeshComponent::SetExternalDecomposition(TUniquePtr<FMeshRenderDecomposition> DecompositionIn)
{
	Decomposition = MoveTemp(DecompositionIn);
	NotifyMeshUpdated();
}



FColor USimpleDynamicMeshComponent::GetTriangleColor(const FDynamicMesh3* MeshIn, int TriangleID)
{
	if (TriangleColorFunc)
	{
		return TriangleColorFunc(MeshIn, TriangleID);
	}
	else
	{
		return (TriangleID % 2 == 0) ? FColor::Red : FColor::White;
	}
}



FBoxSphereBounds USimpleDynamicMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// can get a tighter box by calculating in world space, but we care more about performance
	FBox LocalBoundingBox = (FBox)LocalBounds;
	FBoxSphereBounds Ret(LocalBoundingBox.TransformBy(LocalToWorld));
	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;
	return Ret;
}




void USimpleDynamicMeshComponent::SetInvalidateProxyOnChangeEnabled(bool bEnabled)
{
	bInvalidateProxyOnChange = bEnabled;
}


void USimpleDynamicMeshComponent::ApplyChange(const FMeshVertexChange* Change, bool bRevert)
{
	// will fire UDynamicMesh::MeshChangedEvent, which will call OnMeshObjectChanged() below to invalidate proxy, fire change events, etc
	MeshObject->ApplyChange(Change, bRevert);
}

void USimpleDynamicMeshComponent::ApplyChange(const FMeshChange* Change, bool bRevert)
{
	// will fire UDynamicMesh::MeshChangedEvent, which will call OnMeshObjectChanged() below to invalidate proxy, fire change events, etc
	MeshObject->ApplyChange(Change, bRevert);
}

void USimpleDynamicMeshComponent::ApplyChange(const FMeshReplacementChange* Change, bool bRevert)
{
	// will fire UDynamicMesh::MeshChangedEvent, which will call OnMeshObjectChanged() below to invalidate proxy, fire change events, etc
	MeshObject->ApplyChange(Change, bRevert);
}

void USimpleDynamicMeshComponent::OnMeshObjectChanged(UDynamicMesh* ChangedMeshObject, FDynamicMeshChangeInfo ChangeInfo)
{
	bool bIsFChange = (
		ChangeInfo.Type == EDynamicMeshChangeType::MeshChange
		|| ChangeInfo.Type == EDynamicMeshChangeType::MeshVertexChange
		|| ChangeInfo.Type == EDynamicMeshChangeType::MeshReplacementChange);

	if (bIsFChange)
	{
		if (bInvalidateProxyOnChange)
		{
			NotifyMeshUpdated();
		}

		OnMeshChanged.Broadcast();

		if (ChangeInfo.Type == EDynamicMeshChangeType::MeshVertexChange)
		{
			OnMeshVerticesChanged.Broadcast(this, ChangeInfo.VertexChange, ChangeInfo.bIsRevertChange);
		}
	}
	else
	{
		NotifyMeshUpdated();
		OnMeshChanged.Broadcast();
	}
}


void USimpleDynamicMeshComponent::SetDynamicMesh(UDynamicMesh* NewMesh)
{
	if (ensure(NewMesh) == false)
	{
		return;
	}

	if (ensure(MeshObject))
	{
		MeshObject->OnMeshChanged().Remove(MeshObjectChangedHandle);
	}

	MeshObject = NewMesh;
	MeshObjectChangedHandle = MeshObject->OnMeshChanged().AddUObject(this, &USimpleDynamicMeshComponent::OnMeshObjectChanged);

	NotifyMeshUpdated();
	OnMeshChanged.Broadcast();
}



void USimpleDynamicMeshComponent::OnChildAttached(USceneComponent* ChildComponent)
{
	Super::OnChildAttached(ChildComponent);
	OnChildAttachmentModified.Broadcast(ChildComponent, true);
}
void USimpleDynamicMeshComponent::OnChildDetached(USceneComponent* ChildComponent)
{
	Super::OnChildDetached(ChildComponent);
	OnChildAttachmentModified.Broadcast(ChildComponent, false);
}
