// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentTypes.h"

class UStaticMesh;
class UStaticMeshComponent;
class FDisplayClusterRender_MeshGeometry;

class DISPLAYCLUSTER_API FDisplayClusterRender_MeshComponentProxyData
{
public:
	// Use channel 1 as default source for chromakey custom markers UV
	FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const UStaticMeshComponent& InMeshComponent, const int InUVChromakeyIndex = 1);
	FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const UStaticMesh& InStaticMesh, const int InUVChromakeyIndex = 1);
	FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const FDisplayClusterRender_MeshGeometry& InMeshGeometry);

	const TArray<uint32>& GetIndexData() const
	{ return IndexData; }

	const TArray<FDisplayClusterMeshVertex>& GetVertexData() const
	{ return VertexData; }

	uint32 GetNumTriangles() const
	{ return NumTriangles; }

	uint32 GetNumVertices() const
	{ return NumVertices; }

	bool IsValid() const
	{
		return NumTriangles > 0 && NumVertices > 0 && IndexData.Num() > 0 && VertexData.Num() > 0;
	}

private:
	void UpdateData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc);

	void NormalizeToScreenSpace();

	// The geometry is created by the 3D artist and is sometimes incorrect. 
	// For example, in the OutputRemap post-process, it is necessary that all UVs be in the range 0..1. 
	// For visual validation, all points outside the 0..1 range are excluded during geometry loading when called function RemoveInvisibleFaces().
	void RemoveInvisibleFaces();
	bool IsFaceVisible(int32 Face);
	bool IsUVVisible(int32 UVIndex);

	void ImplInitializeMesh(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const UStaticMesh& InStaticMesh, const int InUVChromakeyIndex = 1);

private:
	TArray<uint32> IndexData;
	TArray<FDisplayClusterMeshVertex> VertexData;
	uint32 NumTriangles = 0;
	uint32 NumVertices = 0;
};
