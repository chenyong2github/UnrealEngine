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
	UpdateBounds();
	CurrentProxy = nullptr;

	if (TangentsType != EDynamicMeshTangentCalcType::ExternallyCalculated)
	{
		bTangentsValid = false;
	}
}


void USimpleDynamicMeshComponent::FastNotifyColorsUpdated()
{
	if (CurrentProxy != nullptr)
	{
		CurrentProxy->FastUpdateColors();
	}
	else
	{
		NotifyMeshUpdated();
	}
}



void USimpleDynamicMeshComponent::FastNotifyPositionsUpdated()
{
	if (CurrentProxy != nullptr)
	{
		bTangentsValid = false;
		CurrentProxy->FastUpdatePositions(false);
	}
	else
	{
		NotifyMeshUpdated();
	}
}




FPrimitiveSceneProxy* USimpleDynamicMeshComponent::CreateSceneProxy()
{
	CurrentProxy = nullptr;
	if (Mesh->TriangleCount() > 0)
	{
		CurrentProxy = new FSimpleDynamicMeshSceneProxy(this);

		if (TriangleColorFunc)
		{
			CurrentProxy->bUsePerTriangleColor = true;
			CurrentProxy->PerTriangleColorFunc = [this](int TriangleID) { return GetTriangleColor(TriangleID); };
		}

		CurrentProxy->Initialize();
	}
	return CurrentProxy;
}

int32 USimpleDynamicMeshComponent::GetNumMaterials() const
{
	return 1;
}


FColor USimpleDynamicMeshComponent::GetTriangleColor(int TriangleID)
{
	if (TriangleColorFunc)
	{
		return TriangleColorFunc(TriangleID);
	}
	else
	{
		return (TriangleID % 2 == 0) ? FColor::Red : FColor::White;
	}
}



FBoxSphereBounds USimpleDynamicMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Bounds are tighter if the box is generated from pre-transformed vertices.
	FBox BoundingBox(ForceInit);
	for ( FVector3d Vertex : Mesh->VerticesItr() ) 
	{
		BoundingBox += LocalToWorld.TransformPosition(Vertex);
	}

	FBoxSphereBounds NewBounds;
	NewBounds.BoxExtent = BoundingBox.GetExtent();
	NewBounds.Origin = BoundingBox.GetCenter();
	NewBounds.SphereRadius = NewBounds.BoxExtent.Size();

	return NewBounds;
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