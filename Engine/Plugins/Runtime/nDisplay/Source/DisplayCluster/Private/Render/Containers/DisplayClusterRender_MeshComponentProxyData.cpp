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

	uint32 UVBaseIndex = 0;
	uint32 UVChromakeyIndex = (InUVChromakeyIndex < SrcMeshResource.GetNumTexCoords()) ? InUVChromakeyIndex : 0;

	IndexBuffer.GetCopy(IndexData);

	VertexData.AddZeroed(VertexPosition.GetNumVertices());
	for (int32 VertexIdx = 0; VertexIdx < VertexData.Num(); VertexIdx++)
	{
		VertexData[VertexIdx].Position = VertexPosition.VertexPosition(VertexIdx);
		VertexData[VertexIdx].UV = VertexBuffer.GetVertexUV(VertexIdx, UVBaseIndex);
		VertexData[VertexIdx].UV_Chromakey = VertexBuffer.GetVertexUV(VertexIdx, UVChromakeyIndex);
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

	VertexData.AddZeroed(InMeshGeometry.Vertices.Num());
	for (int32 VertexIdx = 0; VertexIdx < VertexData.Num(); VertexIdx++)
	{
		VertexData[VertexIdx].Position = InMeshGeometry.Vertices[VertexIdx];
		VertexData[VertexIdx].UV = InMeshGeometry.UV[VertexIdx];
		VertexData[VertexIdx].UV_Chromakey = bUseChromakeyUV ? InMeshGeometry.ChromakeyUV[VertexIdx] : InMeshGeometry.UV[VertexIdx];
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
	if (IndexData.Num() > 0 && VertexData.Num() > 0)
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

		NumTriangles = IndexData.Num() / 3;
		NumVertices = VertexData.Num();
	}
	else
	{
		IndexData.Empty();
		VertexData.Empty();
		NumTriangles = 0;
		NumVertices = 0;
	}

	if (!IsValid())
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshComponentProxyData::UpdateData() Invalid mesh - ignored"));
	}
}

enum class EGeometryPlane2DAxis : uint8
{
	XY,
	XZ,
	YZ,
	Undefined
};

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

	bool bIsValidMeshAxis = true;
	// Support axis rules: Z=UP(screen Y), Y=RIGHT(screen X), X=NotUsed
	EGeometryPlane2DAxis GeometryPlane2DAxis = EGeometryPlane2DAxis::Undefined;
	if (FMath::Abs(Size.Y) <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshComponentProxyData::NormalizeToScreenSpace(): The Y axis is used in screen space as 'x' and the distance cannot be zero."));
		bIsValidMeshAxis = false;
	}
	if (FMath::Abs(Size.Z) <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshComponentProxyData::NormalizeToScreenSpace(): The Z axis is used in screen space as 'y' and the distance cannot be zero."));
		bIsValidMeshAxis = false;
	}
	if (bIsValidMeshAxis)
	{
		// Checking for strange aspect ratio
		FVector ScreenSize(FMath::Max(Size.Z, Size.Y), FMath::Min(Size.Z, Size.Y), 0);
		double AspectRatio = ScreenSize.X / ScreenSize.Y;
		if (AspectRatio > 10)
		{
			// just warning, experimental
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("MeshComponentProxyData::NormalizeToScreenSpace(): Aspect ratio is to big '%d'- <%d,%d>"), AspectRatio, ScreenSize.X, ScreenSize.Y);
		}
	}

	if (!bIsValidMeshAxis)
	{
		// Dont use invalid mesh
		IndexData.Empty();
		VertexData.Empty();
		NumTriangles = 0;
		NumVertices = 0;

		UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshComponentProxyData::NormalizeToScreenSpace() Invalid mesh. Expected mesh axis: Z=UP(screen Y), Y=RIGHT(screen X), X=NotUsed"));
		return;
	}

	FVector Scale(0, 1.f / Size.Y, 1.f / Size.Z);
	for (FDisplayClusterMeshVertex& MeshVertexIt : VertexData)
	{
		FVector4& Vertex = MeshVertexIt.Position;
		
		const float X = (Vertex.Y - AABBox.Min.Y) * Scale.Y;
		const float Y = (Vertex.Z - AABBox.Min.Z) * Scale.Z;

		Vertex = FVector4(X, Y, 0);
	}
}

void FDisplayClusterRender_MeshComponentProxyData::RemoveInvisibleFaces()
{
	TArray<uint32> VisibleIndexData;
	int32 RemovedFacesNum = 0;
	const int32 FacesNum = IndexData.Num() / 3;
	for (int32 Face = 0; Face < FacesNum; ++Face)
	{
		if (IsFaceVisible(Face))
		{
			const uint32 FaceIdx0 = IndexData[Face * 3 + 0];
			const uint32 FaceIdx1 = IndexData[Face * 3 + 1];
			const uint32 FaceIdx2 = IndexData[Face * 3 + 2];

			VisibleIndexData.Add(FaceIdx0);
			VisibleIndexData.Add(FaceIdx1);
			VisibleIndexData.Add(FaceIdx2);
		}
		else
		{
			RemovedFacesNum++;
		}
	}

	IndexData.Empty();
	IndexData.Append(VisibleIndexData);

	if (IndexData.Num() == 0)
	{
		VertexData.Empty();
	}

	if (RemovedFacesNum)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshComponentProxyData::RemoveInvisibleFaces() Removed %d/%d faces"), RemovedFacesNum, FacesNum);
	}
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
