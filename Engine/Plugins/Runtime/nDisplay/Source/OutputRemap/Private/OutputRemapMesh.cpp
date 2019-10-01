// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OutputRemapMesh.h"
#include "OutputRemapLog.h"
#include "OutputRemapHelpers.h"

#include "Stats/Stats.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

#include "RendererInterface.h"
#include "RenderResource.h"
#include "SceneTypes.h"

#include "CommonRenderResources.h"


static int VarOutputRemapReloadChangedExtFilesFrame = 0;
static TAutoConsoleVariable<int32> CVarOutputRemapReloadChangedExtFiles(
	TEXT("nDisplay.reload.output_remap"),
	(int) 0,
	TEXT("Changed external files reload period for output remap:\n")
	TEXT("0 : disable\n"),
	ECVF_RenderThreadSafe
);


namespace ObjMeshStrings
{
	static constexpr auto Vertex = TEXT("v ");
	static constexpr auto UV = TEXT("vt ");
	static constexpr auto Face = TEXT("f ");

	namespace delims
	{
		static constexpr auto Values = TEXT(" ");
		static constexpr auto Face = TEXT("/");
	}
};


FOutputRemapMesh::FOutputRemapMesh(const FString& InExtFileName)
	: NumVertices(0)
	, NumIndices(0)
	, NumTriangles(0)
	, bIsValid(false)
	, FileName(InExtFileName)
	, bIsExtFilesReloadEnabled(false)
	, bForceExtFilesReload(false)
{
	CreateMesh();
}

FOutputRemapMesh::~FOutputRemapMesh()
{
	ReleaseMesh();
}

void FOutputRemapMesh::DrawMesh(FRHICommandList& RHICmdList)
{
	check(IsInRenderingThread());

	if (bIsValid)
	{
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, 0, 0, NumVertices, 0, NumTriangles, 1);

		//!support mesh reload
		ReloadMesh();
	}
	else
	{
		//! Add error logs
	}
}

bool FOutputRemapMesh::FMeshData::CreateFromFile(const FString& FullPathFileName)
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

bool FOutputRemapMesh::FMeshData::ParseLine(const FString& Line)
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

bool FOutputRemapMesh::FMeshData::ExtractVertex(const FString& Line)
{
	TArray<FString> Data;

	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values) == 4)
	{
		float X = FCString::Atof(*Data[1]);
		float Y = FCString::Atof(*Data[2]);
		float Z = FCString::Atof(*Data[3]);

		InVertex.Add(FVector(X, Y, Z));

		return true;
	}

	return false;
}

bool FOutputRemapMesh::FMeshData::ExtractUV(const FString& Line)
{
	TArray<FString> Data;

	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values) == 4)
	{
		float U = FCString::Atof(*Data[1]);
		float V = FCString::Atof(*Data[2]);
		float W = FCString::Atof(*Data[3]);

		InUV.Add(FVector(U, V, W));

		return true;
	}

	return false;
}

bool FOutputRemapMesh::FMeshData::ExtractFace(const FString& Line)
{
	TArray<FString> Data;
	Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values);

	if (Data.Num() > 3)
	{
		TArray<int> FaceIndices;
		for (int i = 1; i < Data.Num(); ++i)
		{
			if (ExtractFaceVertex(Data[i]))
			{				
				const int IdxC = Vertices.Num() - 1;

				if (FaceIndices.Num() > 2)
				{
					const int IdxA = FaceIndices[0];
					const int IdxB = FaceIndices.Last();

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

		Indices.Append(FaceIndices);

		return true;
	}

	return false;
}

bool FOutputRemapMesh::FMeshData::ExtractFaceVertex(const FString& Line)
{
	TArray<FString> Data;
	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Face) > 1)
	{
		const int InVertexIndex = FCString::Atoi(*Data[0]) - 1;
		const int InUVIndex = FCString::Atoi(*Data[1]) - 1;

		FMeshData::FVertexData VertexData(InVertex[InVertexIndex], InUV[InUVIndex]);
		Vertices.Add(VertexData);
		
		return true;
	}

	return false;
}

bool FOutputRemapMesh::FMeshData::CreatePassthrough()
{
	Vertices.Add(FMeshData::FVertexData(FVector(2.6f, 1, 0), FVector(0, 1, 0)));
	Vertices.Add(FMeshData::FVertexData(FVector(2.6f, 2, 0), FVector(0, 0, 0)));
	Vertices.Add(FMeshData::FVertexData(FVector(5.5f, 2, 0), FVector(1, 0, 0)));
	Vertices.Add(FMeshData::FVertexData(FVector(5.5f, 1, 0), FVector(1, 1, 0)));

	for (const auto& It : Vertices)
	{
		InVertex.Add(It.Pos);
	}

	Indices.Add(0);
	Indices.Add(1);
	Indices.Add(2);

	Indices.Add(3);
	Indices.Add(0);
	Indices.Add(2);

	return true;
}

void FOutputRemapMesh::FMeshData::Normalize()
{
	FBox AABBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
	
	for (const FVector& It: InVertex)
	{
		AABBox.Min.X = FMath::Min(AABBox.Min.X, It.X);
		AABBox.Min.Y = FMath::Min(AABBox.Min.Y, It.Y);
		AABBox.Min.Z = FMath::Min(AABBox.Min.Z, It.Z);

		AABBox.Max.X = FMath::Max(AABBox.Max.X, It.X);
		AABBox.Max.Y = FMath::Max(AABBox.Max.Y, It.Y);
		AABBox.Max.Z = FMath::Max(AABBox.Max.Z, It.Z);
	}

	//Normalize
	FVector Size(
		(AABBox.Max.X - AABBox.Min.X),
		(AABBox.Max.Y - AABBox.Min.Y),
		(AABBox.Max.Z - AABBox.Min.Z)
	);

	// Detect axis aligned plane
	const bool bHelperSwapYZ = fabs(Size.Y) > fabs(Size.Z);
	
	if (Size.X == 0) { Size.X = 1; }
	if (Size.Y == 0) { Size.Y = 1; }
	if (Size.Z == 0) { Size.Z = 1; }

	FVector Scale(1/Size.X, 1 / Size.Y, 1 / Size.Z);
	
	for (auto& It : Vertices)
	{
		float X = (It.Pos.X - AABBox.Min.X) * Scale.X;
		float Y = (It.Pos.Y - AABBox.Min.Y) * Scale.Y;
		float Z = (It.Pos.Z - AABBox.Min.Z) * Scale.Z;

		if (bHelperSwapYZ)
		{
			It.Pos = FVector(X, Y, Z);
		}
		else
		{
			It.Pos = FVector(X, Z, Y);
		}

		// Apply axis wrap
		It.UV.Y = 1 - It.UV.Y;
	}
}

void FOutputRemapMesh::FMeshData::RemoveInvisibleFaces()
{
	const int FacesNum = Indices.Num() / 3;
	for (int Face = 0; Face < FacesNum; ++Face)
	{
		if (!IsFaceVisible(Face))
		{
			Indices.RemoveAt(Face * 3, 3, false);
		}
	}

	Indices.Shrink();
}

bool FOutputRemapMesh::FMeshData::IsFaceVisible(int Face)
{
	const int idx0 = Indices[Face * 3 + 0];
	const int idx1 = Indices[Face * 3 + 1];
	const int idx2 = Indices[Face * 3 + 2];

	return IsUVVisible(idx0) && IsUVVisible(idx1) && IsUVVisible(idx2);
}

bool FOutputRemapMesh::FMeshData::IsUVVisible(int UVIndex)
{
	if (Vertices[UVIndex].UV.X < 0 || Vertices[UVIndex].UV.X > 1)
	{
		return false;
	}

	if (Vertices[UVIndex].UV.Y < 0 || Vertices[UVIndex].UV.Y > 1)
	{
		return false;
	}

	return true;
}

void FOutputRemapMesh::ReleaseMesh()
{
	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();
	NumVertices = 0;
	NumIndices = 0;
	NumTriangles = 0;
	bIsValid = false;
	bIsExtFilesReloadEnabled = false;
	bForceExtFilesReload = false;
}

void FOutputRemapMesh::CreateMesh()
{
	FMeshData* Mesh = new FMeshData();

	if (FileName == "Passthrough")
	{
		// Auto-generate test mesh
		bIsValid = Mesh->CreatePassthrough();
	}
	else
	{
		FString FullPathFileName = DisplayClusterHelpers::config::GetFullPath(FileName);
		bIsValid = Mesh->CreateFromFile(FullPathFileName);

		FileLastAccessDateTime = IFileManager::Get().GetAccessTimeStamp(*FullPathFileName);
		bIsExtFilesReloadEnabled = true;
	}

	if (bIsValid)
	{
		// Normalize mesh for output screen 0..1
		Mesh->Normalize();
		// Remove faces with UV out of 0..1 range
		Mesh->RemoveInvisibleFaces();
		// Build runtime mesh in RHI
		BuildMesh(Mesh); // Initialize mesh RHI, structure released latter on renderthread
	}
	else
	{
		delete Mesh;
		Mesh = nullptr;
	}
}

void FOutputRemapMesh::ReloadMesh()
{
	if (bIsExtFilesReloadEnabled)
	{
		if (!bForceExtFilesReload)
		{
			// Detect auto reload by frames
			const int FramePeriod = CVarOutputRemapReloadChangedExtFiles.GetValueOnAnyThread();
			if (FramePeriod > 0)
			{
				if (VarOutputRemapReloadChangedExtFilesFrame++ > FramePeriod)
				{
					VarOutputRemapReloadChangedExtFilesFrame = 0;
					bForceExtFilesReload = true; 
				}
			}
		}

		if(bForceExtFilesReload)
		{
			// Check for file data is modified
			const FString FullPathFileName = DisplayClusterHelpers::config::GetFullPath(FileName);
			const FDateTime CurrentDateTime = IFileManager::Get().GetAccessTimeStamp(*FullPathFileName);

			if (CurrentDateTime != FileLastAccessDateTime)
			{
				ReleaseMesh();
				CreateMesh();
			}
		}
	}

	bForceExtFilesReload = false;
}

static void BuildMesh_RenderThread(FOutputRemapMesh& Dst, const FOutputRemapMesh::FMeshData* Mesh)
{
	Dst.NumVertices  = Mesh->Vertices.Num();
	Dst.NumTriangles = Mesh->Indices.Num() / 3;
	Dst.NumIndices   = Mesh->Indices.Num();

	check(Dst.NumVertices > 2 && Dst.NumTriangles > 0);

	FRHIResourceCreateInfo CreateInfo;
	Dst.VertexBufferRHI = RHICreateVertexBuffer(sizeof(FFilterVertex) * Dst.NumVertices, BUF_Static, CreateInfo);
	void* VoidPtr = RHILockVertexBuffer(Dst.VertexBufferRHI, 0, sizeof(FFilterVertex) * Dst.NumVertices, RLM_WriteOnly);
	FFilterVertex* pVertices = reinterpret_cast<FFilterVertex*>(VoidPtr);

	Dst.IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), sizeof(uint16) * Dst.NumIndices, BUF_Static, CreateInfo);
	void* VoidPtr2 = RHILockIndexBuffer(Dst.IndexBufferRHI, 0, sizeof(uint16) * Dst.NumIndices, RLM_WriteOnly);
	uint16* pIndices = reinterpret_cast<uint16*>(VoidPtr2);
	
	// Copy vertices
	int DataIndex = 0;
	for (const FOutputRemapMesh::FMeshData::FVertexData& It : Mesh->Vertices)
	{
		FFilterVertex& Vertex = pVertices[DataIndex++];

		Vertex.Position.X = It.Pos.X;
		Vertex.Position.Y = It.Pos.Y;
		Vertex.Position.Z = 1.0f;
		Vertex.Position.W = 1.0f;

		Vertex.UV.X = It.UV.X;
		Vertex.UV.Y = It.UV.Y;
	}
	
	// Copy face indices
	DataIndex = 0;
	for (const auto& It : Mesh->Indices)
	{
		pIndices[DataIndex++] = (uint16)It;
	}
	
	RHIUnlockVertexBuffer(Dst.VertexBufferRHI);
	RHIUnlockIndexBuffer(Dst.IndexBufferRHI);

	// Now free temporary mesh data
	delete Mesh;
	Mesh = nullptr;
}

void FOutputRemapMesh::BuildMesh(const FMeshData* Mesh)
{
	if (Mesh)
	{
		if (IsInRenderingThread())
		{
			BuildMesh_RenderThread(*this, Mesh);
		}
		else
		{
			FOutputRemapMesh* Self = this;
			ENQUEUE_RENDER_COMMAND(SetupOutputRemapMeshCmd)([Self, Mesh](FRHICommandListImmediate& RHICmdList)
			{
				BuildMesh_RenderThread(*Self, Mesh);
			});
		}
	}
}
