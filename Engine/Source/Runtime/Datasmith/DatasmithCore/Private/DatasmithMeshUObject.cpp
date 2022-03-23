// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithMeshUObject.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

void FDatasmithMeshSourceModel::SerializeBulkData(FArchive& Ar, UObject* Owner)
{
	RawMeshBulkData.Serialize( Ar, Owner );
}

void UDatasmithMesh::Serialize(FArchive& Ar)
{
	Super::Serialize( Ar );

	for ( FDatasmithMeshSourceModel& SourceModel : SourceModels )
	{
		SourceModel.SerializeBulkData( Ar, this );
	}
}

void operator<<(FArchive& Ar, FDatasmithMeshModels& Models)
{
	Ar << Models.MeshName;
	Ar << Models.bIsCollisionMesh;
	Ar << Models.SourceModels;
}

void operator<<(FArchive& Ar, FDatasmithPackedMeshes& Pack)
{
	FString Guard = Ar.IsLoading() ? TEXT("") : TEXT("FDatasmithPackedMeshes");
	Ar << Guard;
	if (!ensure(Guard == TEXT("FDatasmithPackedMeshes")))
	{
		Ar.SetError();
		return;
	}

	uint8 SerialVersionMajor = 1; // increment this when forward compatibility is not possible
	Ar << SerialVersionMajor;
	if (SerialVersionMajor > 1)
	{
		Ar.SetError();
		return;
	}

	uint8 SerialVersionMinor = 0; // increment this to handle logic changes that preserves forward compatibility
	Ar << SerialVersionMinor;

	uint8 BufferType = 0;
	Ar << BufferType; // (MeshDesc, Zipped Mesh desc;...)

	if (Ar.IsLoading())
	{
		TArray<uint8> Bytes;
		Ar << Bytes;
		FMemoryReader Buffer(Bytes, true);
		Buffer << Pack.MeshesToExport;
	}
	else
	{
		TArray<uint8> Bytes;
		FMemoryWriter Buffer(Bytes, true);
		Buffer << Pack.MeshesToExport;
		Ar << Bytes;
		FMD5 Md5;
		Md5.Update(Bytes.GetData(), Bytes.Num());
		Pack.OutHash.Set(Md5);
	}
}
