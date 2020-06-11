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




USimpleDynamicMeshComponent::USimpleDynamicMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	InitializeNewMesh();
}



void USimpleDynamicMeshComponent::InitializeMesh(FMeshDescription* MeshDescription)
{
	FMeshDescriptionToDynamicMesh Converter;
	Mesh->Clear();
	Converter.Convert(MeshDescription, *Mesh);
	if (TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		Converter.CopyTangents(MeshDescription, Mesh.Get(), &Tangents);
	}

	NotifyMeshUpdated();
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



TUniquePtr<FDynamicMesh3> USimpleDynamicMeshComponent::ExtractMesh(bool bNotifyUpdate)
{
	TUniquePtr<FDynamicMesh3> CurMesh = MoveTemp(Mesh);
	InitializeNewMesh();
	if (bNotifyUpdate)
	{
		NotifyMeshUpdated();
	}
	return CurMesh;
}


void USimpleDynamicMeshComponent::InitializeNewMesh()
{
	Mesh = MakeUnique<FDynamicMesh3>();
	// discard any attributes/etc initialized by default
	Mesh->Clear();

	Tangents.SetMesh(Mesh.Get());
}


void USimpleDynamicMeshComponent::ApplyTransform(const FTransform3d& Transform, bool bInvert)
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
	if (bHaveModifiedTopology == false && Converter.HaveMatchingElementCounts(Mesh.Get(), MeshDescription))
	{
		if (ConversionOptions.bUpdatePositions)
		{
			Converter.Update(Mesh.Get(), *MeshDescription, ConversionOptions.bUpdateNormals, ConversionOptions.bUpdateUVs);
		}
		else
		{
			Converter.UpdateAttributes(Mesh.Get(), *MeshDescription, ConversionOptions.bUpdateNormals, ConversionOptions.bUpdateUVs);
		}
	}
	else
	{
		Converter.Convert(Mesh.Get(), *MeshDescription);

		//UE_LOG(LogTemp, Warning, TEXT("MeshDescription has %d instances"), MeshDescription->VertexInstances().Num());
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
		if (bTangentsValid == false && Mesh->HasAttributes())
		{
			FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->PrimaryUV();
			FDynamicMeshNormalOverlay* NormalOverlay = Mesh->Attributes()->PrimaryNormals();
			if (UVOverlay != nullptr && NormalOverlay != nullptr)
			{
				Tangents.ComputeTriVertexTangents(NormalOverlay, UVOverlay, FComputeTangentsOptions());
				bTangentsValid = true;
			}
		}
		return (bTangentsValid) ? &Tangents : nullptr;
	}

	// in this mode we assume the tangents are valid
	check(TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated);
	if (TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		// if you hit this, you did not request ExternallyCalculated tangents before initializing this PreviewMesh
		check(Tangents.GetTangents().Num() > 0)
	}

	return &Tangents;
}



void USimpleDynamicMeshComponent::SetDrawOnTop(bool bSet)
{
	bDrawOnTop = bSet;
	bUseEditorCompositing = bSet;
}


void USimpleDynamicMeshComponent::NotifyMeshUpdated()
{
	// Need to recreate scene proxy to send it over
	MarkRenderStateDirty();
	LocalBounds = Mesh->GetCachedBounds();
	UpdateBounds();

	if (TangentsType != EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		bTangentsValid = false;
	}
}


void USimpleDynamicMeshComponent::FastNotifyColorsUpdated()
{
	FSimpleDynamicMeshSceneProxy* Proxy = GetCurrentSceneProxy();
	if (Proxy != nullptr)
	{
		if (TriangleColorFunc != nullptr &&  Proxy->bUsePerTriangleColor == false )
		{
			Proxy->bUsePerTriangleColor = true;
			Proxy->PerTriangleColorFunc = [this](const FDynamicMesh3* MeshIn, int TriangleID) { return GetTriangleColor(MeshIn, TriangleID); };
		} 
		else if (TriangleColorFunc == nullptr && Proxy->bUsePerTriangleColor == true)
		{
			Proxy->bUsePerTriangleColor = false;
			Proxy->PerTriangleColorFunc = nullptr;
		}

		Proxy->FastUpdateVertices(false, false, true, false);
		//MarkRenderDynamicDataDirty();
	}
	else
	{
		NotifyMeshUpdated();
	}
}



void USimpleDynamicMeshComponent::FastNotifyPositionsUpdated(bool bNormals, bool bColors, bool bUVs)
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->FastUpdateVertices(true, bNormals, bColors, bUVs);
		//MarkRenderDynamicDataDirty();
		MarkRenderTransformDirty();
		LocalBounds = Mesh->GetCachedBounds();
		UpdateBounds();
	}
	else
	{
		NotifyMeshUpdated();
	}
}


void USimpleDynamicMeshComponent::FastNotifyVertexAttributesUpdated(bool bNormals, bool bColors, bool bUVs)
{
	check(bNormals || bColors || bUVs);
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->FastUpdateVertices(false, bNormals, bColors, bUVs);
		//MarkRenderDynamicDataDirty();
		//MarkRenderTransformDirty();
	}
	else
	{
		NotifyMeshUpdated();
	}
}


void USimpleDynamicMeshComponent::FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags UpdatedAttributes)
{
	check(UpdatedAttributes != EMeshRenderAttributeFlags::None);
	if (GetCurrentSceneProxy() != nullptr)
	{
		bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;
		GetCurrentSceneProxy()->FastUpdateVertices(bPositions,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);

		if (bPositions)
		{
			MarkRenderTransformDirty();
			LocalBounds = Mesh->GetCachedBounds();
			UpdateBounds();
		}
	}
	else
	{
		NotifyMeshUpdated();
	}
}


void USimpleDynamicMeshComponent::FastNotifyUVsUpdated()
{
	FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexUVs);
}



void USimpleDynamicMeshComponent::FastNotifySecondaryTrianglesChanged()
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->FastUpdateAllIndexBuffers();
	}
	else
	{
		NotifyMeshUpdated();
	}
}


void USimpleDynamicMeshComponent::FastNotifyTriangleVerticesUpdated(const TArray<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes)
{
	if (GetCurrentSceneProxy() == nullptr)
	{
		NotifyMeshUpdated();
	}
	else if ( ! Decomposition )
	{
		FastNotifyVertexAttributesUpdated(UpdatedAttributes);
	}
	else
	{
		TArray<int32> UpdatedSets;
		for (int32 tid : Triangles)
		{
			int32 SetID = Decomposition->GetGroupForTriangle(tid);
			UpdatedSets.AddUnique(SetID);
		}

		bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;
		GetCurrentSceneProxy()->FastUpdateVertices(UpdatedSets, bPositions,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);

		if (bPositions)
		{
			MarkRenderTransformDirty();
			LocalBounds = Mesh->GetCachedBounds();
			UpdateBounds();
		}
	}
}



void USimpleDynamicMeshComponent::FastNotifyTriangleVerticesUpdated(const TSet<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes)
{
	if (GetCurrentSceneProxy() == nullptr)
	{
		NotifyMeshUpdated();
	}
	else if (!Decomposition)
	{
		FastNotifyVertexAttributesUpdated(UpdatedAttributes);
	}
	else
	{
		TArray<int32> UpdatedSets;
		for (int32 tid : Triangles)
		{
			int32 SetID = Decomposition->GetGroupForTriangle(tid);
			UpdatedSets.AddUnique(SetID);
		}

		int32 TotalTris = 0;
		for (int32 SetID : UpdatedSets)
		{
			TotalTris += Decomposition->GetGroup(SetID).Triangles.Num();
		}

		//UE_LOG(LogTemp, Warning, TEXT("Updating %d groups with %d tris"), UpdatedSets.Num(), TotalTris);

		bool bPositions = (UpdatedAttributes & EMeshRenderAttributeFlags::Positions) != EMeshRenderAttributeFlags::None;

		// calculate bounds while we are updating vertices
		TFuture<void> UpdateBoundsCalc;
		if (bPositions)
		{
			UpdateBoundsCalc = Async(EAsyncExecution::ThreadPool, [&]()
			{
				LocalBounds = Mesh->GetCachedBounds();
			});
		}

		GetCurrentSceneProxy()->FastUpdateVertices(UpdatedSets, bPositions,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexNormals) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexColors) != EMeshRenderAttributeFlags::None,
			(UpdatedAttributes & EMeshRenderAttributeFlags::VertexUVs) != EMeshRenderAttributeFlags::None);

		if (bPositions)
		{
			MarkRenderTransformDirty();
			UpdateBoundsCalc.Wait();
			//LocalBounds = Mesh->GetCachedBounds();
			UpdateBounds();
		}
	}
}




FPrimitiveSceneProxy* USimpleDynamicMeshComponent::CreateSceneProxy()
{
	check(GetCurrentSceneProxy() == nullptr);

	FSimpleDynamicMeshSceneProxy* NewProxy = nullptr;
	if (Mesh->TriangleCount() > 0)
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
	return NewProxy;
}


void USimpleDynamicMeshComponent::NotifyMaterialSetUpdated()
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->UpdatedReferencedMaterials();
	}
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


void USimpleDynamicMeshComponent::ApplyChange(const FMeshVertexChange* Change, bool bRevert)
{
	bool bHavePositions = Change->bHaveVertexPositions;
	bool bHaveColors = Change->bHaveVertexColors && Mesh->HasVertexColors();

	int32 NV = Change->Vertices.Num();
	const TArray<FVector3d>& Positions = (bRevert) ? Change->OldPositions : Change->NewPositions;
	const TArray<FVector3f>& Colors = (bRevert) ? Change->OldColors : Change->NewColors;
	for (int32 k = 0; k < NV; ++k)
	{
		int32 vid = Change->Vertices[k];
		if (Mesh->IsVertex(vid))
		{
			if (bHavePositions)
			{
				Mesh->SetVertex(vid, Positions[k]);
			}
			if (bHaveColors)
			{
				Mesh->SetVertexColor(vid, Colors[k]);
			}
		}
	}

	if (Change->bHaveOverlayNormals && Mesh->HasAttributes() && Mesh->Attributes()->PrimaryNormals() )
	{
		FDynamicMeshNormalOverlay* Overlay = Mesh->Attributes()->PrimaryNormals();
		int32 NumNormals = Change->Normals.Num();
		const TArray<FVector3f>& UseNormals = (bRevert) ? Change->OldNormals : Change->NewNormals;
		for (int32 k = 0; k < NumNormals; ++k)
		{
			int32 elemid = Change->Normals[k];
			if (Overlay->IsElement(elemid))
			{
				Overlay->SetElement(elemid, UseNormals[k]);
			}
		}
	}

	if (bInvalidateProxyOnChange)
	{
		NotifyMeshUpdated();
	}
	OnMeshChanged.Broadcast();
	OnMeshVerticesChanged.Broadcast(this, Change, bRevert);
}




void USimpleDynamicMeshComponent::ApplyChange(const FMeshChange* Change, bool bRevert)
{
	Change->DynamicMeshChange->Apply(Mesh.Get(), bRevert);

	if (bInvalidateProxyOnChange)
	{
		NotifyMeshUpdated();
	}
	OnMeshChanged.Broadcast();
}


void USimpleDynamicMeshComponent::ApplyChange(const FMeshReplacementChange* Change, bool bRevert)
{
	Mesh->Copy(*Change->GetMesh(bRevert));

	if (bInvalidateProxyOnChange)
	{
		NotifyMeshUpdated();
	}
	OnMeshChanged.Broadcast();
}

