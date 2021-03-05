// Copyright Epic Games, Inc. All Rights Reserved.


#include "Parameterization/MeshUVPacking.h"
#include "DynamicSubmesh3.h"
#include "Selections/MeshConnectedComponents.h"

// the generic GeometricObjects version
#include "Parameterization/UVPacking.h"

struct FUVOverlayView : public FUVPacker::IUVMeshView
{
	FDynamicMesh3* Mesh;
	FDynamicMeshUVOverlay* UVOverlay;

	FUVOverlayView(FDynamicMeshUVOverlay* UVOverlay) : UVOverlay(UVOverlay)
	{
		Mesh = UVOverlay->GetParentMesh();
	}

	virtual FIndex3i GetTriangle(int32 TID) const
	{
		return Mesh->GetTriangle(TID);
	}

	virtual FIndex3i GetUVTriangle(int32 TID) const
	{
		return UVOverlay->GetTriangle(TID);
	}

	virtual FVector3d GetVertex(int32 VID) const
	{
		return Mesh->GetVertex(VID);
	}

	virtual FVector2f GetUV(int32 EID) const
	{
		return UVOverlay->GetElement(EID);
	}

	virtual void SetUV(int32 EID, FVector2f UV)
	{
		return UVOverlay->SetElement(EID, UV);
	}
};


FDynamicMeshUVPacker::FDynamicMeshUVPacker(FDynamicMeshUVOverlay* UVOverlayIn)
{
	UVOverlay = UVOverlayIn;
}


bool FDynamicMeshUVPacker::StandardPack()
{
	FUVPacker Packer;
	Packer.bAllowFlips = bAllowFlips;
	Packer.GutterSize = GutterSize;
	Packer.TextureResolution = TextureResolution;

	FUVOverlayView MeshView(UVOverlay);

	FMeshConnectedComponents UVComponents(MeshView.Mesh);
	UVComponents.FindConnectedTriangles([&MeshView](int32 Triangle0, int32 Triangle1) {
		return MeshView.UVOverlay->AreTrianglesConnected(Triangle0, Triangle1);
		});

	return Packer.StandardPack(&MeshView, UVComponents.Num(), [&UVComponents](int Idx, TArray<int32>& Island)
		{
			Island = UVComponents.GetComponent(Idx).Indices;
		});
}



bool FDynamicMeshUVPacker::StackPack()
{
	FUVPacker Packer;
	Packer.bAllowFlips = bAllowFlips;
	Packer.GutterSize = GutterSize;
	Packer.TextureResolution = TextureResolution;

	FUVOverlayView MeshView(UVOverlay);

	FMeshConnectedComponents UVComponents(MeshView.Mesh);
	UVComponents.FindConnectedTriangles([&MeshView](int32 Triangle0, int32 Triangle1) {
		return MeshView.UVOverlay->AreTrianglesConnected(Triangle0, Triangle1);
		});

	return Packer.StackPack(&MeshView, UVComponents.Num(), [&UVComponents](int Idx, TArray<int32>& Island)
		{
			Island = UVComponents.GetComponent(Idx).Indices;
		});
}