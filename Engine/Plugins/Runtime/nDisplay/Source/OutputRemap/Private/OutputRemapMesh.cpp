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
	(int) 0, // Disabled by default
	TEXT("Changed external files reload period for output remap:\n")
	TEXT("0 : disable\n"),
	ECVF_RenderThreadSafe
);



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

bool FOutputRemapMesh::FMeshData::CreateFromFile(const FString& FullPathFileName)
{	
	//@load from obj file at runtime:
	if (FPaths::FileExists(FullPathFileName))
	{
		TArray<FString> data;
		if (FFileHelper::LoadANSITextFileToStrings(*FullPathFileName, nullptr, data) == true)
		{
			bool bResult = true;

			// Parse each line from config
			for (auto line : data)
			{
				line.TrimStartAndEndInline();
				if (!ParseLine(line))
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

	if (Line.StartsWith(ObjMeshStrings::UV))
	{
		return ExtractUV(Line);
	}

	if (Line.StartsWith(ObjMeshStrings::Face))
	{
		return ExtractFace(Line);
	}

	//skip
	return true;
}

bool FOutputRemapMesh::FMeshData::ExtractVertex(const FString& Line)
{
	TArray<FString> Data;
	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values)==4)
	{
		float X = FCString::Atof(*Data[1]);
		float Y = FCString::Atof(*Data[2]);
		float Z = FCString::Atof(*Data[3]);
		InVertex.Add(FVector(X, Y, Z));
		return true;
	}
	//! handle error
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
	//! handle error
	return false;
}
bool FOutputRemapMesh::FMeshData::ExtractFace(const FString& Line)
{
	TArray<FString> Data;
	Line.ParseIntoArray(Data, ObjMeshStrings::delims::Values);

	if (Data.Num() > 3)
	{
		int IndexBase = Vertex.Num();
		if (ExtractFaceVertex(Data[1])
			&& ExtractFaceVertex(Data[2])
			&& ExtractFaceVertex(Data[3])
			)
		{
			// Triangle
			Index.Add(IndexBase + 0);
			Index.Add(IndexBase + 1);
			Index.Add(IndexBase + 2);

			if (Data.Num() > 4)
			{
				// Quad face
				if (!ExtractFaceVertex(Data[4]))
				{
					//! handle error
					return false;
				}

				Index.Add(IndexBase + 3);
				Index.Add(IndexBase + 0);
				Index.Add(IndexBase + 2);
			}
			return true;
		}
	}
	//! handle error
	return false;
}
bool FOutputRemapMesh::FMeshData::ExtractFaceVertex(const FString& Line)
{
	TArray<FString> Data;
	if (Line.ParseIntoArray(Data, ObjMeshStrings::delims::Face) > 1)
	{
		int InVertexIndex = FCString::Atoi(*Data[0]) - 1;
		int InUVIndex = FCString::Atoi(*Data[1]) - 1;

		Vertex.Add(InVertex[InVertexIndex]);
		UV.Add(InUV[InUVIndex]);
		return true;
	}

	//! handle error
	return false;
}

bool FOutputRemapMesh::FMeshData::CreatePassthrough()
{
	//@create dummy Passthrough mesh (test purpose)
	
	Vertex.Add(FVector(2.6f, 0, 1));
	Vertex.Add(FVector(2.6f, 0, 2));
	Vertex.Add(FVector(5.5f, 0, 2));
	Vertex.Add(FVector(5.5f, 0, 1));

	UV.Add(FVector(0, 1, 0));
	UV.Add(FVector(0, 0, 0));
	UV.Add(FVector(1, 0, 0));
	UV.Add(FVector(1, 1, 0));

	Index.Add(0);
	Index.Add(1);
	Index.Add(2);

	Index.Add(3);
	Index.Add(0);
	Index.Add(2);

	return true;
}
void FOutputRemapMesh::FMeshData::Normalize()
{
	FBox AABBox(FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
	
	for (const auto& Pts: Vertex)
	{
		AABBox.Min.X = FMath::Min(AABBox.Min.X, Pts.X);
		AABBox.Min.Y = FMath::Min(AABBox.Min.Y, Pts.Y);
		AABBox.Min.Z = FMath::Min(AABBox.Min.Z, Pts.Z);

		AABBox.Max.X = FMath::Max(AABBox.Max.X, Pts.X);
		AABBox.Max.Y = FMath::Max(AABBox.Max.Y, Pts.Y);
		AABBox.Max.Z = FMath::Max(AABBox.Max.Z, Pts.Z);
	}

	//Normalize
	FVector Size(
		(AABBox.Max.X - AABBox.Min.X),
		(AABBox.Max.Y - AABBox.Min.Y),
		(AABBox.Max.Z - AABBox.Min.Z)
	);

	if (Size.X == 0) { Size.X = 1; }
	if (Size.Y == 0) { Size.Y = 1; }
	if (Size.Z == 0) { Size.Z = 1; }

	FVector Scale(1/Size.X, 1 / Size.Y, 1 / Size.Z);
	
	for (auto& Pts : Vertex)
	{
		float X = (Pts.X - AABBox.Min.X) * Scale.X;
		float Y = (Pts.Y - AABBox.Min.Y) * Scale.Y;
		float Z = (Pts.Z - AABBox.Min.Z) * Scale.Z;

		Pts = FVector(X,Y,Z);
	}
}
void FOutputRemapMesh::FMeshData::RemoveInvisibleFaces()
{
	int FacesNum = Index.Num() / 3;
	for (int Face = 0; Face < FacesNum; Face++)
	{
		if (!IsVisibleFace(Face))
		{
			Index.RemoveAt(Face * 3, 3, true);
			Face--;
		}
	}
}
bool FOutputRemapMesh::FMeshData::IsVisibleFace(int Face)
{
	int idx0 = Index[Face * 3 + 0];
	int idx1 = Index[Face * 3 + 1];
	int idx2 = Index[Face * 3 + 2];

	return IsVisibleUV(idx0) && IsVisibleUV(idx1) && IsVisibleUV(idx2);
}

bool FOutputRemapMesh::FMeshData::IsVisibleUV(int UVIndex)
{
	if (UV[UVIndex].X < 0 || UV[UVIndex].X > 1)
		return false;

	if (UV[UVIndex].Y < 0 || UV[UVIndex].Y > 1)
		return false;

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
		//autogenerate test mesh:
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
			FString FullPathFileName = DisplayClusterHelpers::config::GetFullPath(FileName);
			FDateTime CurrentDateTime = IFileManager::Get().GetAccessTimeStamp(*FullPathFileName);
			if (CurrentDateTime != FileLastAccessDateTime)
			{
				//! Add log. Reload file *FullPathFileName
				ReleaseMesh();
				CreateMesh();
			}
		}
	}
	bForceExtFilesReload = false;
}


FOutputRemapMesh::FOutputRemapMesh(const FString& InExtFileName)	
	: bIsValid(false)
	, FileName(InExtFileName)
	, bIsExtFilesReloadEnabled(false)
	, bForceExtFilesReload(false)
	, NumVertices(0)
	, NumIndices(0)
	, NumTriangles(0)
{
	CreateMesh();
}

FOutputRemapMesh::~FOutputRemapMesh()
{
	ReleaseMesh();
}

static void BuildMesh_RenderThread(FOutputRemapMesh& Dst, const FOutputRemapMesh::FMeshData* Mesh)
{
	Dst.NumVertices = Mesh->Vertex.Num();
	Dst.NumTriangles = Mesh->Index.Num() / 3;
	Dst.NumIndices = Mesh->Index.Num();

	check(Dst.NumVertices > 2 && Dst.NumTriangles > 0);


	FRHIResourceCreateInfo CreateInfo;
	Dst.VertexBufferRHI = RHICreateVertexBuffer(sizeof(FFilterVertex) * Dst.NumVertices, BUF_Static, CreateInfo);
	void* VoidPtr = RHILockVertexBuffer(Dst.VertexBufferRHI, 0, sizeof(FFilterVertex) * Dst.NumVertices, RLM_WriteOnly);
	FFilterVertex* pVertices = reinterpret_cast<FFilterVertex*>(VoidPtr);

	Dst.IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), sizeof(uint16) * Dst.NumIndices, BUF_Static, CreateInfo);
	void* VoidPtr2 = RHILockIndexBuffer(Dst.IndexBufferRHI, 0, sizeof(uint16) * Dst.NumIndices, RLM_WriteOnly);
	uint16* pIndices = reinterpret_cast<uint16*>(VoidPtr2);

	//Copy Vertex
	int DataIndex = 0;
	for (auto& It : Mesh->Vertex)
	{
		FFilterVertex& Vertex = pVertices[DataIndex++];
		// Remap from to NDC space [0 1] -> [-1 1]
		Vertex.Position.X = It.X;
		Vertex.Position.Y = 1-It.Z;
		Vertex.Position.Z = 1.0f;
		Vertex.Position.W = 1.0f;
	}

	//Copy UV
	DataIndex = 0;
	for (auto& It : Mesh->UV)
	{
		FFilterVertex& Vertex = pVertices[DataIndex++];		
		Vertex.UV.X = It.X;
		Vertex.UV.Y = It.Y;
	}

	//Copy faces Index
	DataIndex = 0;
	for (auto& It : Mesh->Index)
	{
		pIndices[DataIndex++] = (uint16)It;
	}
	
	RHIUnlockVertexBuffer(Dst.VertexBufferRHI);
	RHIUnlockIndexBuffer(Dst.IndexBufferRHI);

	delete Mesh; // Now free tempropary mesh data
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
