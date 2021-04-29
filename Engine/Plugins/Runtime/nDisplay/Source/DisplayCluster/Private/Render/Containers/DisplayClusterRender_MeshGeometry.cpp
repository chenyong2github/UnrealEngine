// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"


namespace ObjMeshStrings
{
	static constexpr auto Vertex = TEXT("v ");
	static constexpr auto UV     = TEXT("vt ");
	static constexpr auto Face   = TEXT("f ");

	namespace delims
	{
		static constexpr auto Values = TEXT(" ");
		static constexpr auto Face = TEXT("/");
	}
};


class FDisplayCluster_MeshGeometryLoaderOBJ
{
public:
	FDisplayCluster_MeshGeometryLoaderOBJ(FDisplayClusterRender_MeshGeometry& InTarget)
		: Target(InTarget)
	{ }

	bool Load(const FString& FullPathFileName);

private:
	bool CreateFromFile(const FString& FullPathFileName);

	void Normalize();

	// The geometry is created by the 3D artist and is sometimes incorrect. 
	// For example, in the OutputRemap post-process, it is necessary that all UVs be in the range 0..1. 
	// For visual validation, all points outside the 0..1 range are excluded during geometry loading when called function RemoveInvisibleFaces().
	void RemoveInvisibleFaces();

private:
	// Mesh post-op's
	bool IsFaceVisible(int32 Face);
	bool IsUVVisible(int32 UVIndex);

	// Obj file parser:
	bool ParseLine(const FString& Line);
	bool ExtractVertex(const FString& Line);
	bool ExtractUV(const FString& Line);
	bool ExtractFace(const FString& Line);
	bool ExtractFaceVertex(const FString& Line);

private:
	FDisplayClusterRender_MeshGeometry& Target;
	TArray<FVector> InVertex;
	TArray<FVector> InUV;
};


//*************************************************************************
//* FDisplayClusterRender_MeshGeometry
//*************************************************************************
FDisplayClusterRender_MeshGeometry::FDisplayClusterRender_MeshGeometry(const FDisplayClusterRender_MeshGeometry& In)
	: Vertices(In.Vertices)
	, Normal(In.Normal)
	, UV(In.UV)
	, ChromakeyUV(In.ChromakeyUV)
	, Triangles(In.Triangles)
{ }


FDisplayClusterRender_MeshGeometry::FDisplayClusterRender_MeshGeometry(EDisplayClusterRender_MeshGeometryCreateType CreateType)
{
	switch (CreateType)
	{
	case EDisplayClusterRender_MeshGeometryCreateType::Passthrough:
		CreatePassthrough();
		break;
	}
}

// Load geometry from OBJ file
bool FDisplayClusterRender_MeshGeometry::LoadFromFile(const FString& FullPathFileName, EDisplayClusterRender_MeshGeometryFormat Format)
{
	switch (Format)
	{
	case EDisplayClusterRender_MeshGeometryFormat::OBJ:
	{
		FDisplayCluster_MeshGeometryLoaderOBJ LoaderOBJ(*this);
		return LoaderOBJ.Load(FullPathFileName);
	}
	default:
		break;
	}

	return false;
}

// Test purpose: create square geometry
bool FDisplayClusterRender_MeshGeometry::CreatePassthrough()
{
	Vertices.Empty();
	Vertices.Add(FVector(2.6f, 1, 0));
	Vertices.Add(FVector(2.6f, 2, 0));
	Vertices.Add(FVector(5.5f, 2, 0));
	Vertices.Add(FVector(5.5f, 1, 0));

	UV.Empty();
	UV.Add(FVector2D(0, 1));
	UV.Add(FVector2D(0, 0));
	UV.Add(FVector2D(1, 0));
	UV.Add(FVector2D(1, 1));

	Triangles.Empty();
	Triangles.Add(0);
	Triangles.Add(1);
	Triangles.Add(2);

	Triangles.Add(3);
	Triangles.Add(0);
	Triangles.Add(2);

	return true;
}

//*************************************************************************
//* FDisplayCluster_MeshGeometryLoaderOBJ
//*************************************************************************
bool FDisplayCluster_MeshGeometryLoaderOBJ::Load(const FString& FullPathFileName)
{
	if (CreateFromFile(FullPathFileName))
	{
		// Now used only for OutputRemap postprocess
		// OutputRemap postprocess helpers:
		
		// Normalize mesh for output screen 0..1
		Normalize();

		// Remove faces with UV out of 0..1 range
		RemoveInvisibleFaces();
		return true;
	}

	return false;
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::CreateFromFile(const FString& FullPathFileName)
{	
	// Load from obj file at runtime:
	if (FPaths::FileExists(FullPathFileName))
	{
		TArray<FString> data;
		if (FFileHelper::LoadANSITextFileToStrings(*FullPathFileName, nullptr, data) == true)
		{
			bool bResult = true;

			// Parse each line from config
			for (FString Line : data)
			{
				Line.TrimStartAndEndInline();
				if (!ParseLine(Line))
				{
					// Handle error
					bResult = false;
				}
			}

			return bResult;
		}
	}

	return false;
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::ParseLine(const FString& Line)
{
	if (Line.StartsWith(ObjMeshStrings::Vertex))
	{
		return ExtractVertex(Line);
	}
	else if (Line.StartsWith(ObjMeshStrings::UV))
	{
		return ExtractUV(Line);
	}
	else if (Line.StartsWith(ObjMeshStrings::Face))
	{
		return ExtractFace(Line);
	}

	return true;
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::ExtractVertex(const FString& Line)
{
	TArray<FString> Data;

	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values) == 4)
	{
		const float X = FCString::Atof(*Data[1]);
		const float Y = FCString::Atof(*Data[2]);
		const float Z = FCString::Atof(*Data[3]);

		InVertex.Add(FVector(X, Y, Z));

		return true;
	}

	return false;
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::ExtractUV(const FString& Line)
{
	TArray<FString> Data;

	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values) == 4)
	{
		const float U = FCString::Atof(*Data[1]);
		const float V = FCString::Atof(*Data[2]);
		const float W = FCString::Atof(*Data[3]);

		InUV.Add(FVector(U, V, W));

		return true;
	}

	return false;
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::ExtractFace(const FString& Line)
{
	TArray<FString> Data;
	Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values);

	if (Data.Num() > 3)
	{
		TArray<int32> FaceIndices;
		for (int32 i = 1; i < Data.Num(); ++i)
		{
			if (ExtractFaceVertex(Data[i]))
			{
				const int32 IdxC = Target.Vertices.Num() - 1;

				if (FaceIndices.Num() > 2)
				{
					const int32 IdxA = FaceIndices[0];
					const int32 IdxB = FaceIndices.Last();

					FaceIndices.Add(IdxA);
					FaceIndices.Add(IdxB);
					FaceIndices.Add(IdxC);
				}
				else
				{
					FaceIndices.Add(IdxC);
				}
			}
			else
			{
				return false;
			}
		}

		Target.Triangles.Append(FaceIndices);

		return true;
	}

	return false;
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::ExtractFaceVertex(const FString& Line)
{
	TArray<FString> Data;
	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Face) > 1)
	{
		const int32 InVertexIndex = FCString::Atoi(*Data[0]) - 1;
		const int32 InUVIndex = FCString::Atoi(*Data[1]) - 1;

		Target.Vertices.Add(InVertex[InVertexIndex]);
		Target.UV.Add(FVector2D(InUV[InUVIndex].X, InUV[InUVIndex].Y));
		
		return true;
	}

	return false;
}

void FDisplayCluster_MeshGeometryLoaderOBJ::Normalize()
{
	FBox AABBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
	
	for (const FVector& Vertex: InVertex)
	{
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
	
	for (FVector& VertexIt : Target.Vertices)
	{
		const float X = (VertexIt.X - AABBox.Min.X) * Scale.X;
		const float Y = (VertexIt.Y - AABBox.Min.Y) * Scale.Y;
		const float Z = (VertexIt.Z - AABBox.Min.Z) * Scale.Z;

		VertexIt = (bHelperSwapYZ ? FVector(X, Y, Z) : FVector(X, Z, Y));
	}

	for (FVector2D& UVIt : Target.UV)
	{
		// Apply axis wrap
		UVIt.Y = 1 - UVIt.Y;
	}
}

void FDisplayCluster_MeshGeometryLoaderOBJ::RemoveInvisibleFaces()
{
	const int32 FacesNum = Target.Triangles.Num() / 3;
	for (int32 Face = 0; Face < FacesNum; ++Face)
	{
		const bool bFaceExist = (Face * 3) < Target.Triangles.Num();

		if (bFaceExist && !IsFaceVisible(Face))
		{
			Target.Triangles.RemoveAt(Face * 3, 3, false);
		}
	}

	Target.Triangles.Shrink();
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::IsFaceVisible(int32 Face)
{
	const int32 FaceIdx0 = Target.Triangles[Face * 3 + 0];
	const int32 FaceIdx1 = Target.Triangles[Face * 3 + 1];
	const int32 FaceIdx2 = Target.Triangles[Face * 3 + 2];

	return IsUVVisible(FaceIdx0) && IsUVVisible(FaceIdx1) && IsUVVisible(FaceIdx2);
}

bool FDisplayCluster_MeshGeometryLoaderOBJ::IsUVVisible(int32 UVIndex)
{
	return (
		Target.UV[UVIndex].X >= 0.f && Target.UV[UVIndex].X <= 1.f &&
		Target.UV[UVIndex].Y >= 0.f && Target.UV[UVIndex].Y <= 1.f);
}
