// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"


class FOutputRemapMesh
{
public:
	struct FMeshData
	{
		struct FVertexData
		{
			FVector Pos;
			FVector UV;
			FVertexData(const FVector &InPos, const FVector &InUV)
				: Pos(InPos), UV(InUV)
			{ }
		};

		TArray<FVertexData> Vertices;
		TArray<int>         Indices;

		bool CreateFromFile(const FString& FullPathFileName);
		bool CreatePassthrough();

		void Normalize();
		void RemoveInvisibleFaces();

	private:
		// Mesh post-op's
		bool IsFaceVisible(int Face);
		bool IsUVVisible(int UVIndex);

		// Obj file parser:
		bool ParseLine(const FString& Line);
		bool ExtractVertex(const FString& Line);
		bool ExtractUV(const FString& Line);
		bool ExtractFace(const FString& Line);
		bool ExtractFaceVertex(const FString& Line);
		
		TArray<FVector> InVertex;
		TArray<FVector> InUV;
	};

public:
	FOutputRemapMesh(const FString& ExtFileName);
	~FOutputRemapMesh();
	
public:
	void DrawMesh(FRHICommandList& RHICmdList);

	const FString& GetFileName() const
	{ return FileName; }

	bool IsValid() const
	{ return bIsValid; }

	void ReloadChangedExtFiles()
	{ bForceExtFilesReload = true; }

private:
	void BuildMesh(const FMeshData* Mesh);
	void CreateMesh();
	void ReleaseMesh();
	void ReloadMesh();

public:
	FVertexBufferRHIRef VertexBufferRHI;
	FIndexBufferRHIRef  IndexBufferRHI;

	uint32  NumVertices;
	uint32  NumIndices;
	uint32  NumTriangles;

private:
	bool       bIsValid;

	FString    FileName;
	FDateTime  FileLastAccessDateTime;
	bool       bIsExtFilesReloadEnabled;
	bool       bForceExtFilesReload;
};
