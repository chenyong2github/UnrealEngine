// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshComponentProxyData.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#include "Misc/DisplayClusterLog.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

//*************************************************************************
//* FDisaplyClusterMeshComponentProxyData
//*************************************************************************
void FDisplayClusterRender_MeshComponentProxyData::ImplInitializeMesh(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const UStaticMesh& InStaticMesh, const int InUVChromakeyIndex)
{
	if (InStaticMesh.bAllowCPUAccess == false)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("If packaging this project, static mesh '%s' requires its AllowCPUAccess flag to be enabled."), *InStaticMesh.GetName());
#if !WITH_EDITOR
		// Can't access to cooked data from CPU without this flag
		return;
#endif
	}

	const FStaticMeshLODResources& SrcMeshResource = InStaticMesh.GetLODForExport(0);

	const FPositionVertexBuffer& VertexPosition = SrcMeshResource.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& VertexBuffer = SrcMeshResource.VertexBuffers.StaticMeshVertexBuffer;
	const FRawStaticIndexBuffer& IndexBuffer = SrcMeshResource.IndexBuffer;

	NumTriangles = IndexBuffer.GetNumIndices() / 3; // Now by default no triangle strip supported
	NumVertices = VertexBuffer.GetNumVertices();

	uint32 UVBaseIndex = 0;
	uint32 UVChromakeyIndex = (InUVChromakeyIndex < SrcMeshResource.GetNumTexCoords()) ? InUVChromakeyIndex : 0;

	IndexBuffer.GetCopy(IndexData);

	VertexData.AddZeroed(NumVertices);
	for (uint32 i = 0; i < NumVertices; i++)
	{
		VertexData[i].Position = VertexPosition.VertexPosition(i);
		VertexData[i].UV = VertexBuffer.GetVertexUV(i, UVBaseIndex);
		VertexData[i].UV_Chromakey = VertexBuffer.GetVertexUV(i, UVChromakeyIndex);
	}

	UpdateData(InDataFunc);
}

FDisplayClusterRender_MeshComponentProxyData::FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const UStaticMeshComponent& InMeshComponent, const int InUVChromakeyIndex)
{
	check(IsInGameThread());

	UStaticMesh* StaticMesh = InMeshComponent.GetStaticMesh();
	if (StaticMesh)
	{
		ImplInitializeMesh(InDataFunc, *StaticMesh, InUVChromakeyIndex);
	}
}

FDisplayClusterRender_MeshComponentProxyData::FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const UStaticMesh& InStaticMesh, const int InUVChromakeyIndex)
{
	check(IsInGameThread());

	ImplInitializeMesh(InDataFunc, InStaticMesh, InUVChromakeyIndex);
}

FDisplayClusterRender_MeshComponentProxyData::FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc, const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
{
	bool bUseChromakeyUV = InMeshGeometry.ChromakeyUV.Num() > 0;

	NumTriangles = InMeshGeometry.Triangles.Num() / 3; // No triangle strip supported by default
	NumVertices = InMeshGeometry.Vertices.Num();

	VertexData.AddZeroed(NumVertices);
	for (uint32 i = 0; i < NumVertices; i++)
	{
		VertexData[i].Position = InMeshGeometry.Vertices[i];
		VertexData[i].UV = InMeshGeometry.UV[i];

		// Use channel 1 as default source for chromakey custom markers UV
		VertexData[i].UV_Chromakey = bUseChromakeyUV ? InMeshGeometry.ChromakeyUV[i] : InMeshGeometry.UV[i];
	}

	int Idx = 0;
	IndexData.AddZeroed(InMeshGeometry.Triangles.Num());
	for (const int32& It : InMeshGeometry.Triangles)
	{
		IndexData[Idx++] = (uint32)It;
	}

	UpdateData(InDataFunc);
}

void FDisplayClusterRender_MeshComponentProxyData::UpdateData(const FDisplayClusterRender_MeshComponentProxyDataFunc InDataFunc)
{
	if (IsValid())
	{
		switch (InDataFunc)
		{
		case FDisplayClusterRender_MeshComponentProxyDataFunc::OutputRemapScreenSpace:
			// Output remap require normalize:
			NormalizeToScreenSpace();
			RemoveInvisibleFaces();
			break;

		default:
		case FDisplayClusterRender_MeshComponentProxyDataFunc::None:
			break;
		}
	}
}

void FDisplayClusterRender_MeshComponentProxyData::NormalizeToScreenSpace()
{
	FBox AABBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));

	for (const FDisplayClusterMeshVertex& MeshVertexIt : VertexData)
	{
		const FVector4& Vertex = MeshVertexIt.Position;

		AABBox.Min.X = FMath::Min(AABBox.Min.X, Vertex.X);
		AABBox.Min.Y = FMath::Min(AABBox.Min.Y, Vertex.Y);
		AABBox.Min.Z = FMath::Min(AABBox.Min.Z, Vertex.Z);

		AABBox.Max.X = FMath::Max(AABBox.Max.X, Vertex.X);
		AABBox.Max.Y = FMath::Max(AABBox.Max.Y, Vertex.Y);
		AABBox.Max.Z = FMath::Max(AABBox.Max.Z, Vertex.Z);
	}

	//Normalize
	FVector Size(
		(AABBox.Max.X - AABBox.Min.X),
		(AABBox.Max.Y - AABBox.Min.Y),
		(AABBox.Max.Z - AABBox.Min.Z)
	);

	// Detect axis aligned plane
	const bool bHelperSwapYZ = fabs(Size.Y) > fabs(Size.Z);

	Size.X = (Size.X == 0 ? 1 : Size.X);
	Size.Y = (Size.Y == 0 ? 1 : Size.Y);
	Size.Z = (Size.Z == 0 ? 1 : Size.Z);

	FVector Scale(1.f / Size.X, 1.f / Size.Y, 1.f / Size.Z);

	for (FDisplayClusterMeshVertex& MeshVertexIt : VertexData)
	{
		FVector4& Vertex = MeshVertexIt.Position;

		const float X = (Vertex.X - AABBox.Min.X) * Scale.X;
		const float Y = (Vertex.Y - AABBox.Min.Y) * Scale.Y;
		const float Z = (Vertex.Z - AABBox.Min.Z) * Scale.Z;

		Vertex = (bHelperSwapYZ ? FVector4(Y, X, Z, 1) : FVector4(Z, X, Y, 1));
	}
}

void FDisplayClusterRender_MeshComponentProxyData::RemoveInvisibleFaces()
{
	const int32 FacesNum = IndexData.Num() / 3;
	for (int32 Face = 0; Face < FacesNum; ++Face)
	{
		const bool bFaceExist = (Face * 3) < IndexData.Num();

		if (bFaceExist && !IsFaceVisible(Face))
		{
			IndexData.RemoveAt(Face * 3, 3, false);
		}
	}

	IndexData.Shrink();
}

bool FDisplayClusterRender_MeshComponentProxyData::IsFaceVisible(int32 Face)
{
	const int32 FaceIdx0 = IndexData[Face * 3 + 0];
	const int32 FaceIdx1 = IndexData[Face * 3 + 1];
	const int32 FaceIdx2 = IndexData[Face * 3 + 2];

	return IsUVVisible(FaceIdx0) && IsUVVisible(FaceIdx1) && IsUVVisible(FaceIdx2);
}

bool FDisplayClusterRender_MeshComponentProxyData::IsUVVisible(int32 UVIndex)
{
	return (
		VertexData[UVIndex].UV.X >= 0.f && VertexData[UVIndex].UV.X <= 1.f &&
		VertexData[UVIndex].UV.Y >= 0.f && VertexData[UVIndex].UV.Y <= 1.f);
}
