// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DisplayClusterLog.h"

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

private:
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
void FDisplayClusterRender_MeshGeometry::CreatePassthrough()
{
	Vertices.Empty();
	Vertices.Add(FVector(2.6f, 1, 0));
	Vertices.Add(FVector(2.6f, 2, 0));
	Vertices.Add(FVector(5.5f, 2, 0));
	Vertices.Add(FVector(5.5f, 1, 0));

	UV.Empty();
	UV.Add(FVector2D(0, 0));
	UV.Add(FVector2D(0, 1));
	UV.Add(FVector2D(1, 1));
	UV.Add(FVector2D(1, 0));

	Triangles.Empty();
	Triangles.Add(0);
	Triangles.Add(1);
	Triangles.Add(2);

	Triangles.Add(3);
	Triangles.Add(0);
	Triangles.Add(2);
}

//*************************************************************************
//* FDisplayCluster_MeshGeometryLoaderOBJ
//*************************************************************************
bool FDisplayCluster_MeshGeometryLoaderOBJ::Load(const FString& FullPathFileName)
{
	return CreateFromFile(FullPathFileName);
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

			int LineIdx = 0;
			// Parse each line from config
			for (FString Line : data)
			{
				LineIdx++;
				Line.TrimStartAndEndInline();
				if (!ParseLine(Line))
				{
					UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshGeometryLoaderOBJ: Invalid line %d: '%s'"), LineIdx , *Line);
					bResult = false;
				}
			}

			if (!bResult)
			{
				UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshGeometryLoaderOBJ: Can't load mesh geometry from file '%s'"), *FullPathFileName);
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
	int32 Count = Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values);
	if (Count > 2)
	{
		const float U = FCString::Atof(*Data[1]);
		const float V = FCString::Atof(*Data[2]);
		const float W = (Count > 3)?FCString::Atof(*Data[3]) : 0;

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
		if (InVertexIndex < 0 || InVertexIndex >= InVertex.Num())
	{
			UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshGeometryLoaderOBJ: broken vertex index. Line: '%s'"), *Line);
			Target.Vertices.Add(FVector(0,0,0));
	}
		else
	{
			Target.Vertices.Add(InVertex[InVertexIndex]);
	}

		const int32 InUVIndex = FCString::Atoi(*Data[1]) - 1;
		if (InUVIndex < 0 || InUVIndex >= InUV.Num())
	{
			UE_LOG(LogDisplayClusterRender, Error, TEXT("MeshGeometryLoaderOBJ: broken uv index. Line: '%s'"), *Line);
			Target.UV.Add(FVector2D(0, 0));
}
		else
		{
			Target.UV.Add(FVector2D(InUV[InUVIndex].X, InUV[InUVIndex].Y));
	}

		return true;
}

	return false;
}
