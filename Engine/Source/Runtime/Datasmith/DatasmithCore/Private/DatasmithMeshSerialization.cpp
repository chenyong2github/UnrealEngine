// Copyright Epic Games, Inc. All Rights Reserved.
#include "DatasmithMeshSerialization.h"

#include "DatasmithMeshUObject.h"

#include "Compression/OodleDataCompression.h"
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


namespace DatasmithMeshSerializationImpl
{
enum class ECompressionMethod
{
	ECM_ZLib  = 1,
	ECM_Gzip  = 2,
	ECM_LZ4   = 3,
	ECM_Oodle = 4,

	ECM_Default = ECM_Oodle,
};

FName NAME_Oodle("Oodle");
FName GetMethodName(ECompressionMethod MethodCode)
{
	switch (MethodCode)
	{
		case ECompressionMethod::ECM_ZLib: return NAME_Zlib;
		case ECompressionMethod::ECM_Gzip: return NAME_Gzip;
		case ECompressionMethod::ECM_LZ4:  return NAME_LZ4;
		case ECompressionMethod::ECM_Oodle:return NAME_Oodle;
		default: ensure(0); return NAME_None;
	}
}

bool CompressInline(TArray<uint8>& UncompressedData, ECompressionMethod Method)
{
	int32 UncompressedSize = UncompressedData.Num();
	FName MethodName = GetMethodName(Method);
	int32 CompressedSize = (Method != ECompressionMethod::ECM_Oodle)
		? FCompression::CompressMemoryBound(MethodName, UncompressedSize)
		: FOodleDataCompression::CompressedBufferSizeNeeded(UncompressedSize);

	TArray<uint8> CompressedData;
	int32 HeaderSize = 5;
	CompressedData.AddUninitialized(CompressedSize + HeaderSize);

	// header
	{
		FMemoryWriter Ar(CompressedData);
		uint8 MethodCode = uint8(Method);
		Ar << MethodCode;
		Ar << UncompressedSize;
		check(HeaderSize == Ar.Tell());
	}

	if (Method == ECompressionMethod::ECM_Oodle)
	{
		auto Compressor = FOodleDataCompression::ECompressor::Kraken;
		auto Level = FOodleDataCompression::ECompressionLevel::VeryFast;
		if (int64 ActualCompressedSize = FOodleDataCompression::Compress(CompressedData.GetData() + HeaderSize, CompressedSize, UncompressedData.GetData(), UncompressedSize, Compressor, Level))
		{
			CompressedData.SetNum(ActualCompressedSize + HeaderSize, true);
			UncompressedData = MoveTemp(CompressedData);

			return true;
		}
	}
	else
	{
		if (FCompression::CompressMemory(MethodName, CompressedData.GetData() + HeaderSize, CompressedSize, UncompressedData.GetData(), UncompressedSize))
		{

			CompressedData.SetNum(CompressedSize + HeaderSize, true);
			UncompressedData = MoveTemp(CompressedData);

			return true;
		}
	}

	UE_LOG(LogDatasmith, Warning, TEXT("Compression failed"));
	return false;
}

bool DecompressInline(TArray<uint8>& CompressedData)
{
	ECompressionMethod Method;
	uint8* BufferStart = nullptr;
	int32 UncompressedSize = -1;
	int32 CompressedSize = CompressedData.Num();
	int32 HeaderSize = 0;
	{
		FMemoryReader Ar(CompressedData);
		uint8 MethodCode = 0;
		Ar << MethodCode;
		Method = ECompressionMethod(MethodCode);
		Ar << UncompressedSize;
		HeaderSize = Ar.Tell();
	}

	FName MethodName = GetMethodName(Method);
	if (MethodName == NAME_Oodle)
	{
		TArray<uint8> UncompressedData;
		UncompressedData.SetNumUninitialized(UncompressedSize);
		bool Ok = FOodleDataCompression::Decompress(UncompressedData.GetData(), UncompressedSize, CompressedData.GetData() + HeaderSize, CompressedData.Num() - HeaderSize);
		if (Ok)
		{
			CompressedData = MoveTemp(UncompressedData);
			return true;
		}
	}
	else if (MethodName != NAME_None)
	{
		TArray<uint8> UncompressedData;
		UncompressedData.SetNumUninitialized(UncompressedSize);
		bool Ok = FCompression::UncompressMemory(MethodName, UncompressedData.GetData(), UncompressedData.Num(), CompressedData.GetData() + HeaderSize, CompressedData.Num() - HeaderSize);
		if (Ok)
		{
			CompressedData = MoveTemp(UncompressedData);
			return true;
		}
	}
	UE_LOG(LogDatasmith, Warning, TEXT("Decompression failed"));
	return false;
}

} // ns DatasmithMeshSerializationImpl


FMD5Hash FDatasmithPackedMeshes::Serialize(FArchive& Ar, bool bCompressed)
{
	using namespace DatasmithMeshSerializationImpl;

	FString Guard = Ar.IsLoading() ? TEXT("") : TEXT("FDatasmithPackedMeshes");
	Ar << Guard;
	if (!ensure(Guard == TEXT("FDatasmithPackedMeshes")))
	{
		Ar.SetError();
		return {};
	}

	uint32 SerialVersion = 0;
	Ar << SerialVersion;

	enum EBufferType{ RawMeshDescription, CompressedMeshDescription };
	uint8 BufferType = bCompressed ? CompressedMeshDescription : RawMeshDescription;
	Ar << BufferType;

	FMD5Hash OutHash;
	if (Ar.IsLoading())
	{
		FCustomVersionContainer CustomVersions;
		CustomVersions.Serialize(Ar);

		TArray<uint8> Bytes;
		Ar << Bytes;

		if (BufferType == CompressedMeshDescription)
		{
			DecompressInline(Bytes);
		}

		FMemoryReader Buffer(Bytes, true);
		Buffer.SetCustomVersions(CustomVersions);
		Buffer << Meshes;
	}
	else
	{
		TArray<uint8> Bytes;
		FMemoryWriter Buffer(Bytes, true);
		Buffer << Meshes;

		// MeshDescriptions uses custom versionning,
		const_cast<FCustomVersionContainer&>(Buffer.GetCustomVersions()).Serialize(Ar);

		if (BufferType == CompressedMeshDescription)
		{
			CompressInline(Bytes, ECompressionMethod::ECM_Default);
		}
		Ar << Bytes;
		FMD5 Md5;
		Md5.Update(Bytes.GetData(), Bytes.Num());
		OutHash.Set(Md5);
	}
	return OutHash;
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

// Reads previous format of meshes (RawMesh based)
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

FDatasmithPackedMeshes GetDatasmithMeshFromFile(const FString& MeshPath)
{
	FDatasmithPackedMeshes Result;

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
		Result.Meshes = GetDatasmithMeshFromMeshPath_Legacy(Archive.Get(), LeagacyNumMeshesCount);
	}
	else
	{
		Result.Serialize(*Archive);

		if (Archive->IsError())
		{
			Result = FDatasmithPackedMeshes{};
			UE_LOG(LogDatasmith, Warning, TEXT("Failed to read meshes from %s"), *MeshPath);
		}
	}

	return Result;
}

FDatasmithPackedMeshes GetDatasmithClothFromFile(const FString& Path)
{
	// #ue_ds_cloth_note: asset serialization
	FDatasmithPackedMeshes Result;

	TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileReader(*Path) );
	if ( !Archive.IsValid() )
	{
		UE_LOG(LogDatasmith, Warning, TEXT("Cannot read file %s"), *Path);
		return Result;
	}

	Result.Serialize(*Archive);

	if (Archive->IsError())
	{
		Result = FDatasmithPackedMeshes{};
		UE_LOG(LogDatasmith, Warning, TEXT("Failed to read cloth from %s"), *Path);
	}

	return Result;
}

