// Copyright Epic Games, Inc. All Rights Reserved.

#include "Drawing/MeshElementsVisualizer.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/MeshWireframeComponent.h"
#include "ToolSetupUtil.h"
#include "Async/Async.h"




/**
 * IMeshWireframeSource implementation for a FDynamicMesh3
 */
class FDynamicMeshWireframeSource : public IMeshWireframeSource
{
public:
	const FDynamicMesh3* Mesh = nullptr;

	virtual bool IsValid() const { return Mesh != nullptr; }

	virtual FBoxSphereBounds GetBounds() const 
	{ 
		FAxisAlignedBox3d Bounds = Mesh->GetBounds();
		return FBoxSphereBounds((FBox)Bounds);
	}

	virtual FVector GetVertex(int32 Index) const
	{
		return (FVector)Mesh->GetVertex(Index);
	}

	virtual int32 GetEdgeCount() const
	{
		return Mesh->EdgeCount();
	}

	virtual int32 GetMaxEdgeIndex() const
	{
		return Mesh->MaxEdgeID();
	}

	virtual bool IsEdge(int32 Index) const
	{
		return Mesh->IsEdge(Index);
	}

	virtual void GetEdge(int32 EdgeIndex, int32& VertIndexAOut, int32& VertIndexBOut, EMeshEdgeType& TypeOut) const
	{
		FIndex2i EdgeV = Mesh->GetEdgeV(EdgeIndex);
		VertIndexAOut = EdgeV.A;
		VertIndexBOut = EdgeV.B;
		int32 EdgeType = (int32)EMeshEdgeType::Regular;
		if (Mesh->IsBoundaryEdge(EdgeIndex))
		{
			EdgeType |= (int32)EMeshEdgeType::MeshBoundary;
		}
		if (Mesh->HasAttributes())
		{
			bool bIsUVSeam = false, bIsNormalSeam = false;
			if (Mesh->Attributes()->IsSeamEdge(EdgeIndex, bIsUVSeam, bIsNormalSeam))
			{
				if (bIsUVSeam)
				{
					EdgeType |= (int32)EMeshEdgeType::UVSeam;
				}
				if (bIsNormalSeam)
				{
					EdgeType |= (int32)EMeshEdgeType::NormalSeam;
				}
			}
		}
		TypeOut = (EMeshEdgeType)EdgeType;
	}
};


class FDynamicMeshWireframeSourceProvider : public IMeshWireframeSourceProvider
{
public:
	TUniqueFunction<const FDynamicMesh3* (void)> MeshAccessFunction;

	FDynamicMeshWireframeSourceProvider(TUniqueFunction<const FDynamicMesh3* (void)>&& MeshAccessFuncIn)
	{
		MeshAccessFunction = MoveTemp(MeshAccessFuncIn);
	}

	virtual void AccessMesh(TFunctionRef<void(const IMeshWireframeSource&)> ProcessingFunc) override
	{
		const FDynamicMesh3* Mesh = MeshAccessFunction();
		check(Mesh);
		FDynamicMeshWireframeSource WireSource;
		WireSource.Mesh = Mesh;
		ProcessingFunc(WireSource);
	}
};




void UMeshElementsVisualizer::OnCreated()
{
	Settings = NewObject<UMeshElementsVisualizerProperties>(this);
	Settings->WatchProperty(Settings->bVisible, [this](bool) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->bShowWireframe, [this](bool) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->bShowBorders, [this](bool) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->bShowUVSeams, [this](bool) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->bShowNormalSeams, [this](bool) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->ThicknessScale, [this](float) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->DepthBias, [this](float) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->WireframeColor, [this](FColor) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->BoundaryEdgeColor, [this](FColor) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->UVSeamColor, [this](FColor) { bSettingsModified = true; });
	Settings->WatchProperty(Settings->NormalSeamColor, [this](FColor) { bSettingsModified = true; });
	bSettingsModified = false;

	WireframeComponent = NewObject<UMeshWireframeComponent>(GetActor());
	WireframeComponent->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(nullptr));
	WireframeComponent->SetupAttachment(GetActor()->GetRootComponent());	
	WireframeComponent->RegisterComponent();
}


void UMeshElementsVisualizer::SetMeshAccessFunction(TUniqueFunction<const FDynamicMesh3* (void)>&& MeshAccessFunctionIn)
{
	const FDynamicMesh3* Mesh = MeshAccessFunctionIn();
	FAxisAlignedBox3d Bounds = Mesh->GetBounds();
	float DepthBiasDim = Bounds.DiagonalLength() * 0.01;
	WireframeComponent->LineDepthBiasSizeScale = DepthBiasDim;

	WireframeSourceProvider = MakeShared<FDynamicMeshWireframeSourceProvider>(MoveTemp(MeshAccessFunctionIn));

	WireframeComponent->SetWireframeSourceProvider(WireframeSourceProvider);
}




void UMeshElementsVisualizer::OnTick(float DeltaTime)
{
	if (bSettingsModified)
	{
		UpdateVisibility();
		bSettingsModified = false;
	}
}


void UMeshElementsVisualizer::UpdateVisibility()
{
	if (Settings->bVisible == false)
	{
		WireframeComponent->SetVisibility(false);
		return;
	}

	WireframeComponent->SetVisibility(true);

	WireframeComponent->LineDepthBias = Settings->DepthBias;
	WireframeComponent->ThicknessScale = Settings->ThicknessScale;

	WireframeComponent->bEnableWireframe = Settings->bShowWireframe;
	WireframeComponent->bEnableBoundaryEdges = Settings->bShowBorders;
	WireframeComponent->bEnableUVSeams = Settings->bShowUVSeams;
	WireframeComponent->bEnableNormalSeams = Settings->bShowNormalSeams;

	WireframeComponent->WireframeColor = Settings->WireframeColor;
	WireframeComponent->BoundaryEdgeColor = Settings->BoundaryEdgeColor;
	WireframeComponent->UVSeamColor = Settings->UVSeamColor;
	WireframeComponent->NormalSeamColor = Settings->NormalSeamColor;

	WireframeComponent->MarkRenderStateDirty();
}


void UMeshElementsVisualizer::NotifyMeshChanged()
{
	WireframeComponent->MarkRenderStateDirty();
}