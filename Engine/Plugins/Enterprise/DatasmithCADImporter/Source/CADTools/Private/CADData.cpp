// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CADData.h"

#include "CADOptions.h"

#include "Misc/FileHelper.h"


namespace CADLibrary
{
uint32 BuildFastColorHash(uint32 ColorId, uint8 Alpha)
{
	if (Alpha == 0)
	{
		Alpha = 1;
	}
	return ColorId | Alpha << 24;
}

void UnhashFastColorHash(uint32 ColorHash, uint32& ColorId, uint8& Alpha)
{
	ColorId = ColorHash & 0x00ffffff;
	Alpha = (uint8)((ColorHash & 0xff000000) >> 24);
}

int32 BuildColorHash(const FColor& Color)
{
	FString HashString = FString::Printf(TEXT("%02x%02x%02x%02x"), Color.R, Color.G, Color.B, Color.A);
	return FGenericPlatformMath::Abs((int32)GetTypeHash(HashString));
}

int32 BuildMaterialHash(const FCADMaterial& Material)
{
	FString Hash;
	if (!Material.MaterialName.IsEmpty())
	{
		Hash += Material.MaterialName;  // we add material name because it could be used by the end user so two material with same parameters but different name are different.
	}
	Hash += FString::Printf(TEXT("%02x%02x%02x "), Material.Diffuse.R, Material.Diffuse.G, Material.Diffuse.B);
	Hash += FString::Printf(TEXT("%02x%02x%02x "), Material.Ambient.R, Material.Ambient.G, Material.Ambient.B);
	Hash += FString::Printf(TEXT("%02x%02x%02x "), Material.Specular.R, Material.Specular.G, Material.Specular.B);
	Hash += FString::Printf(TEXT("%02x%02x%02x"), (int)(Material.Shininess * 255.0), (int)(Material.Transparency * 255.0), (int)(Material.Reflexion * 255.0));

	if (!Material.TextureName.IsEmpty())
	{
		Hash += Material.TextureName;
	}
	return FMath::Abs((int32)GetTypeHash(Hash));
}


FBodyMesh::FBodyMesh(uint32 InBodyID, int32 FaceNum)
	: TriangleCount(0)
	, BodyID(InBodyID)
{
	FaceTessellationSet.Reserve(FaceNum);
}


FCADMeshFile::FCADMeshFile(FString& InFileName, TMap< uint32, FBodyMesh* >& InBodyUuidToCTBodyMap)
	: BodyUuidToBodyMap(InBodyUuidToCTBodyMap)
{
	uint32 NbBody = 0;

	TArray<uint8> MeshData;
	FFileHelper::LoadFileToArray(MeshData, *InFileName);

	ReadTessellationSetFromFile(MeshData, NbBody, CTBodySet);

	for(FBodyMesh& Body : CTBodySet)
	{
		BodyUuidToBodyMap.Add(Body.GetBodyUuid(), &Body);
	}
}

enum ETessellationContent
{
	HasVertex = 1 << 1,
	HasNormal = 1 << 2,
	HasIndex = 1 << 3,
	HasTexCoord = 1 << 4,

	Default = HasVertex | HasNormal | HasIndex | HasTexCoord
};

void WriteTessellationInRawData(FTessellationData& Tessellation, TArray<uint8>& GlobalRawData)
{
	int32 INFO[LastLine];

	int32 Types = ETessellationContent::Default;
	uint32 VertexSize = Tessellation.VertexArray.Num();
	uint32 NormalSize = Tessellation.NormalArray.Num();
	uint32 IndexSize = Tessellation.IndexArray.Num();
	uint32 TexCoordSize = Tessellation.TexCoordArray.Num();

	uint32 TessellationRawDataSize = VertexSize + NormalSize + IndexSize + TexCoordSize + sizeof(INFO); // VertexCount, VertexType, NormalCount, NormalType, IndexCount, IndexType, TexCoordType;

	INFO[LineType] = Types;
	INFO[LineRawSize] = TessellationRawDataSize;
	INFO[LineBodyId] = Tessellation.BodyId;
	INFO[LineBodyUuId] = Tessellation.BodyUuId;
	INFO[LineBodyFaceNum] = Tessellation.BodyFaceNum;
	INFO[LineMaterialId] = Tessellation.MaterialId;
	INFO[LineMaterialHash] = Tessellation.MaterialHash;
	INFO[LineVertexCount] = Tessellation.VertexCount;
	INFO[LineVertexType] = Tessellation.SizeOfVertexType;
	INFO[LineNormalCount] = Tessellation.NormalCount;
	INFO[LineNormalType] = Tessellation.SizeOfNormalType;
	INFO[LineIndexCount] = Tessellation.IndexCount;
	INFO[LineIndexType] = Tessellation.SizeOfIndexType;
	INFO[LineTexCoordCount] = Tessellation.TexCoordCount;
	INFO[LineTexCoordType] = Tessellation.SizeOfTexCoordType;

	GlobalRawData.Append((uint8*)INFO, sizeof(INFO));
	GlobalRawData.Append(Tessellation.VertexArray);
	GlobalRawData.Append(Tessellation.NormalArray);
	GlobalRawData.Append(Tessellation.IndexArray);
	GlobalRawData.Append(Tessellation.TexCoordArray);
}

bool ReadTessellationSetFromFile(TArray<uint8>& RawData, uint32& OutNbBodies, TArray<FBodyMesh>& CTBodySet)
{
	int32 RawDataOffset = 0;
	memcpy(&OutNbBodies, RawData.GetData() + RawDataOffset, sizeof(uint32));
	RawDataOffset += sizeof(uint32);

	CTBodySet.Reserve(OutNbBodies);

	FBodyMesh* CurrentBody = nullptr;

	int32 CurrentBodyId = -1;
	int32 CurrentBodyIndex = -1;
	uint32 BodyHeader[LastLine];
	const uint32 BodyHeaderByteSize = sizeof(BodyHeader);

	while (RawDataOffset + int32(BodyHeaderByteSize) <= RawData.Num())
	{
		memcpy(BodyHeader, RawData.GetData() + RawDataOffset, BodyHeaderByteSize);

		int32 BlocByteSize  = BodyHeader[LineRawSize];
		if (RawDataOffset + BlocByteSize > RawData.Num())
		{
			return false;
		}

		RawDataOffset += BodyHeaderByteSize;

		int32 Types = BodyHeader[LineType];
		int32 RawSize = BodyHeader[LineRawSize];

		if (Types != ETessellationContent::Default)
		{
			return false;
		}

		if (BodyHeader[LineBodyId] != CurrentBodyId)
		{
			CurrentBodyId = BodyHeader[LineBodyId];
			CTBodySet.Emplace(BodyHeader[LineBodyId], BodyHeader[LineBodyFaceNum]);
			CurrentBodyIndex++;

			CurrentBody = &CTBodySet[CurrentBodyIndex];
			CurrentBody->SetBodyId(BodyHeader[LineBodyId]);
			CurrentBody->SetBodyUuid(BodyHeader[LineBodyUuId]);
			CurrentBody->GetTessellationSet().Reserve(BodyHeader[LineBodyFaceNum]);
		}

		if (CurrentBody == nullptr)
		{
			return false;
		}

		FTessellationData& Tessellation = CurrentBody->GetTessellationSet().Emplace_GetRef();

		Tessellation.BodyId = BodyHeader[LineBodyId];
		Tessellation.BodyUuId = BodyHeader[LineBodyUuId];
		Tessellation.BodyFaceNum = BodyHeader[LineBodyFaceNum];
		Tessellation.MaterialId = BodyHeader[LineMaterialId];
		Tessellation.MaterialHash = BodyHeader[LineMaterialHash];
		Tessellation.VertexCount = BodyHeader[LineVertexCount];
		Tessellation.SizeOfVertexType = BodyHeader[LineVertexType];
		Tessellation.NormalCount = BodyHeader[LineNormalCount];
		Tessellation.SizeOfNormalType = BodyHeader[LineNormalType];
		Tessellation.IndexCount = BodyHeader[LineIndexCount];
		Tessellation.SizeOfIndexType = BodyHeader[LineIndexType];
		Tessellation.TexCoordCount = BodyHeader[LineTexCoordCount];
		Tessellation.SizeOfTexCoordType = BodyHeader[LineTexCoordType];

		uint32 VertexSize = Tessellation.VertexCount * Tessellation.SizeOfVertexType * 3;
		uint32 NormalSize = Tessellation.NormalCount * Tessellation.SizeOfNormalType * 3;
		uint32 IndexSize = Tessellation.IndexCount * Tessellation.SizeOfIndexType;
		uint32 TexCoordSize = Tessellation.TexCoordCount * Tessellation.SizeOfTexCoordType * 2;

		Tessellation.VertexArray.Empty(VertexSize);
		Tessellation.NormalArray.Empty(NormalSize);
		Tessellation.IndexArray.Empty(IndexSize);
		Tessellation.TexCoordArray.Empty(TexCoordSize);

		Tessellation.VertexArray.Append(RawData.GetData() + RawDataOffset, VertexSize);
		RawDataOffset += VertexSize;
		Tessellation.NormalArray.Append(RawData.GetData() + RawDataOffset, NormalSize);
		RawDataOffset += NormalSize;
		Tessellation.IndexArray.Append(RawData.GetData() + RawDataOffset, IndexSize);
		RawDataOffset += IndexSize;
		Tessellation.TexCoordArray.Append(RawData.GetData() + RawDataOffset, TexCoordSize);
		RawDataOffset += TexCoordSize;
	}
	return true;
}


}
