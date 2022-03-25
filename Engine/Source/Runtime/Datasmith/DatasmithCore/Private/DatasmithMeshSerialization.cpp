// Copyright Epic Games, Inc. All Rights Reserved.
#include "DatasmithMeshSerialization.h"

#include "DatasmithMeshUObject.h"

#include "HAL/FileManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

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

TOptional<FMeshDescription> ExtractToMeshDescription(FDatasmithMeshSourceModel& SourceModel)
{
	FRawMesh RawMesh;
	SourceModel.RawMeshBulkData.LoadRawMesh( RawMesh );

	if ( !RawMesh.IsValid() )
	{
		return {};
	}

	// RawMesh -> MeshDescription conversion requires an {mat_index: slot_name} map for its PolygonGroups.
	TMap<int32, FName> GroupNamePerGroupIndex;

	// There is no guaranty that incoming RawMesh.FaceMaterialIndices are sequential, but the conversion assumes so.
	// -> we remap materials identifiers to material indices
	// eg:
	//   incoming per-face mat identifier   5   5   1   1   1   99   99
	//   remapped per-face index            0   0   1   1   1   2    2
	//   per PolygonGroup FName:           "5" "5" "1" "1" "1" "99" "99"
	TSet<int32> MaterialIdentifiers;
	for (int32& MatIdentifier : RawMesh.FaceMaterialIndices)
	{
		bool bAlreadyIn = false;
		int32 IndexOfIdentifier = MaterialIdentifiers.Add(MatIdentifier, &bAlreadyIn).AsInteger();

		// identifier -> name association
		if (!bAlreadyIn)
		{
			FName MaterialSlotName = *FString::FromInt(MatIdentifier);
			GroupNamePerGroupIndex.Add(IndexOfIdentifier, MaterialSlotName);
		}

		// remap old identifier to material index
		MatIdentifier = IndexOfIdentifier;
	}

	FMeshDescription MeshDescription;
	FStaticMeshAttributes(MeshDescription).Register();

	// Do not compute normals and tangents during conversion since we have other operations to apply
	// on the mesh that might invalidate them anyway and we must also validate the mesh to detect
	// vertex positions containing NaN before doing computation as MikkTSpace crashes on NaN values.
	const bool bSkipNormalsAndTangents = true;
	FStaticMeshOperations::ConvertFromRawMesh(RawMesh, MeshDescription, GroupNamePerGroupIndex, bSkipNormalsAndTangents);
	return MeshDescription;
}

TArray<FDatasmithMeshModels> GetDatasmithMeshFromMeshPath_Legacy(FArchive* Archive, int32 LeagacyNumMeshesCount)
{
	TArray< FDatasmithMeshModels > Result;

	UDatasmithMesh* DatasmithMesh = nullptr;
	{
		// Make sure the new UDatasmithMesh object is not created while a garbage collection is performed
		FGCScopeGuard GCGuard;
		// Setting the RF_Standalone bitmask on the new UDatasmithMesh object, to make sure it is not garbage collected
		// while loading and processing the udsmesh file. This can happen on very big meshes (5M+ triangles)
		DatasmithMesh = NewObject< UDatasmithMesh >(GetTransientPackage(), NAME_None, RF_Standalone);
	}

	// Currently we only have 1 mesh per file. If there's a second mesh, it will be a CollisionMesh
	while ( LeagacyNumMeshesCount-- > 0)
	{
		TArray< uint8 > Bytes;
		*Archive << Bytes;

		FMemoryReader MemoryReader( Bytes, true );
		MemoryReader.ArIgnoreClassRef = false;
		MemoryReader.ArIgnoreArchetypeRef = false;
		MemoryReader.SetWantBinaryPropertySerialization(true);
		DatasmithMesh->Serialize( MemoryReader );

		FDatasmithMeshModels& MeshInternal = Result.AddDefaulted_GetRef();
		MeshInternal.bIsCollisionMesh = DatasmithMesh->bIsCollisionMesh;
		for (FDatasmithMeshSourceModel& SourceModel : DatasmithMesh->SourceModels)
		{
			if (TOptional<FMeshDescription> OptionalMesh = ExtractToMeshDescription(SourceModel))
			{
				MeshInternal.SourceModels.Add(MoveTemp(*OptionalMesh));
			}
		}
	}

	// Tell the garbage collector DatasmithMesh can now be deleted.
	DatasmithMesh->ClearInternalFlags(EInternalObjectFlags::Async);
	DatasmithMesh->ClearFlags(RF_Standalone);
	return Result;
}

TArray<FDatasmithMeshModels> GetDatasmithMeshFromMeshPath(const FString& MeshPath)
{
	TArray< FDatasmithMeshModels > Result;

	TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileReader(*MeshPath) );
	if ( !Archive.IsValid() )
	{
		UE_LOG(LogDatasmith, Warning, TEXT("Cannot read file %s"), *MeshPath);
		return Result;
	}

	int32 LeagacyNumMeshesCount = 0;
	*Archive << LeagacyNumMeshesCount;

	if (LeagacyNumMeshesCount > 0)
	{
		return GetDatasmithMeshFromMeshPath_Legacy(Archive.Get(), LeagacyNumMeshesCount);
	}

	if (LeagacyNumMeshesCount == 0)
	{
		FDatasmithPackedMeshes Pack;
		*Archive << Pack;

		if (!Archive->IsError())
		{
			for (FDatasmithMeshModels& Mesh : Pack.MeshesToExport)
			{
				FDatasmithMeshModels& MeshInternal = Result.AddDefaulted_GetRef();
				MeshInternal.bIsCollisionMesh = Mesh.bIsCollisionMesh;
				MeshInternal.SourceModels = MoveTemp(Mesh.SourceModels);
			}
		}
		else
		{
			UE_LOG(LogDatasmith, Warning, TEXT("Failed to read meshes from %s"), *MeshPath);
		}
	}

	return Result;
}