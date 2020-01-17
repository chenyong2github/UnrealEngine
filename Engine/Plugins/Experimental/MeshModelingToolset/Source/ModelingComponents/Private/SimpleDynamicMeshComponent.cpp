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

#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "DynamicMeshChangeTracker.h"



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
	Converter.bPrintDebugMessages = true;
	Mesh->Clear();
	Converter.Convert(MeshDescription, *Mesh);
	if (TangentsType == EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		Converter.CopyTangents(MeshDescription, Mesh.Get(), Tangents);
	}

	NotifyMeshUpdated();
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


void USimpleDynamicMeshComponent::Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions)
{
	if (bHaveModifiedTopology == false && Mesh.Get()->VertexCount() == MeshDescription->Vertices().Num())
	{
		FDynamicMeshToMeshDescription Converter(ConversionOptions);
		Converter.Update(Mesh.Get(), *MeshDescription);
	}
	else
	{
		FDynamicMeshToMeshDescription Converter(ConversionOptions);
		Converter.Convert(Mesh.Get(), *MeshDescription);

		//UE_LOG(LogTemp, Warning, TEXT("MeshDescription has %d instances"), MeshDescription->VertexInstances().Num());
	}
}






FMeshTangentsf* USimpleDynamicMeshComponent::GetTangents()
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
				Tangents.ComputePerTriangleTangents(NormalOverlay, UVOverlay);
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

		Proxy->FastUpdateVertices(false, false, true);
		//MarkRenderDynamicDataDirty();
	}
	else
	{
		NotifyMeshUpdated();
	}
}



void USimpleDynamicMeshComponent::FastNotifyPositionsUpdated()
{
	if (GetCurrentSceneProxy() != nullptr)
	{
		GetCurrentSceneProxy()->FastUpdateVertices(true, false, false);
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

		NewProxy->Initialize();
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
	int NV = Change->Vertices.Num();
	const TArray<FVector3d>& Positions = (bRevert) ? Change->OldPositions : Change->NewPositions;
	
	for (int k = 0; k < NV; ++k)
	{
		int vid = Change->Vertices[k];
		Mesh->SetVertex(vid, Positions[k]);
	}

	NotifyMeshUpdated();
	OnMeshChanged.Broadcast();
}




void USimpleDynamicMeshComponent::ApplyChange(const FMeshChange* Change, bool bRevert)
{
	Change->DynamicMeshChange->Apply(Mesh.Get(), bRevert);

	NotifyMeshUpdated();
	OnMeshChanged.Broadcast();
}


void USimpleDynamicMeshComponent::ApplyChange(const FMeshReplacementChange* Change, bool bRevert)
{
	Mesh->Copy(*Change->GetMesh(bRevert));

	NotifyMeshUpdated();
	OnMeshChanged.Broadcast();
}

