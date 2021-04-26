// Copyright Epic Games, Inc. All Rights Reserved.
#include "PipelineCacheUtilities.h"
#if WITH_EDITOR	// this has no business existing in a cooked game
#include "Misc/SecureHash.h"
#include "Serialization/NameAsStringIndexProxyArchive.h"
#include "Serialization/VarInt.h"
#include "HAL/FileManagerGeneric.h"

namespace UE
{
namespace PipelineCacheUtilities
{
namespace Private
{
	/** Header of the binary file. */
	struct FStableKeysSerializedHeader
	{
		enum class EMagic : uint64
		{
			Magic = 0x524448534C425453ULL	// STBLSHDR
		};

		enum class EVersion : int32
		{
			Current = 1
		};

		/** Magic to reject other files */
		EMagic		Magic = EMagic::Magic;
		/** Format version */
		EVersion	Version = EVersion::Current;
		/** Number of stable key entries. */
		int64		NumEntries;

		friend FArchive& operator<<(FArchive& Ar, FStableKeysSerializedHeader& Info)
		{
			return Ar << Info.Magic << Info.Version << Info.NumEntries;
		}

	};

}
}
}

bool UE::PipelineCacheUtilities::LoadStableKeysFile(const FStringView& Filename, TArray<FStableShaderKeyAndValue>& InOutArray)
{
	TUniquePtr<FArchive> FileArchiveInner(IFileManager::Get().CreateFileReader(*FString(Filename.Len(), Filename.GetData())));
	if (!FileArchiveInner)
	{
		return false;
	}

	TUniquePtr<FArchive> Archive(new FNameAsStringIndexProxyArchive(*FileArchiveInner.Get()));
	UE::PipelineCacheUtilities::Private::FStableKeysSerializedHeader Header;
	UE::PipelineCacheUtilities::Private::FStableKeysSerializedHeader SupportedHeader;

	*Archive << Header;

	if (Header.Magic != SupportedHeader.Magic)
	{
		return false;
	}

	// start restrictive, as the format isn't really forward compatible, nor needs to be
	if (Header.Version != SupportedHeader.Version)
	{
		return false;
	}

	TArray<FSHAHash> Hashes;
	int32 NumHashes;
	*Archive << NumHashes;
	Hashes.AddUninitialized(NumHashes);
	for (int32 IdxHash = 0; IdxHash < NumHashes; ++IdxHash)
	{
		*Archive << Hashes[IdxHash];
	}

	for (int64 Idx = 0; Idx < Header.NumEntries; ++Idx)
	{
		FStableShaderKeyAndValue Item;
		int8 CompactNamesNum = -1;
		*Archive << CompactNamesNum;
		if (CompactNamesNum > 0)
		{
			Item.ClassNameAndObjectPath.ObjectClassAndPath.AddDefaulted(CompactNamesNum);

			for (int IdxName = 0; IdxName < (int)CompactNamesNum; ++IdxName)
			{
				*Archive << Item.ClassNameAndObjectPath.ObjectClassAndPath[IdxName];
			}
		}

		*Archive << Item.ShaderType;
		*Archive << Item.ShaderClass;
		*Archive << Item.MaterialDomain;
		*Archive << Item.FeatureLevel;
		*Archive << Item.QualityLevel;
		*Archive << Item.TargetFrequency;
		*Archive << Item.TargetPlatform;
		*Archive << Item.VFType;
		*Archive << Item.PermutationId;

		uint64 HashIdx = ReadVarUIntFromArchive(*Archive);
		Item.PipelineHash = Hashes[static_cast<int32>(HashIdx)];
		HashIdx = ReadVarUIntFromArchive(*Archive);
		Item.OutputHash = Hashes[static_cast<int32>(HashIdx)];

		Item.ComputeKeyHash();
		InOutArray.Add(Item);
	}

	return true;
}

bool UE::PipelineCacheUtilities::SaveStableKeysFile(const FStringView& Filename, const TSet<FStableShaderKeyAndValue>& Values)
{
	TUniquePtr<FArchive> FileArchiveInner(IFileManager::Get().CreateFileWriter(*FString(Filename.Len(), Filename.GetData())));
	if (!FileArchiveInner)
	{
		return false;
	}
	TUniquePtr<FArchive> Archive(new FNameAsStringIndexProxyArchive(*FileArchiveInner.Get()));

	UE::PipelineCacheUtilities::Private::FStableKeysSerializedHeader Header;
	Header.NumEntries = Values.Num();

	*Archive << Header;

	// go through all the hashes and index them
	TArray<FSHAHash> Hashes;
	TMap<FSHAHash, int32> HashToIndex;

	auto IndexHash = [&Hashes, &HashToIndex](const FSHAHash& Hash)
	{
		if (HashToIndex.Find(Hash) == nullptr)
		{
			Hashes.Add(Hash);
			HashToIndex.Add(Hash, Hashes.Num() - 1);
		}
	};

	for (const FStableShaderKeyAndValue& Item : Values)
	{
		IndexHash(Item.PipelineHash);
		IndexHash(Item.OutputHash);
	}

	int32 NumHashes = Hashes.Num();
	*Archive << NumHashes;
	for (int32 IdxHash = 0; IdxHash < NumHashes; ++IdxHash)
	{
		*Archive << Hashes[IdxHash];
	}

	// save the rest of the properties
	for (const FStableShaderKeyAndValue& ConstItem : Values)
	{
		// serialization unfortunately needs non-const and this is easier than const-casting every field
		FStableShaderKeyAndValue& Item = const_cast<FStableShaderKeyAndValue&>(ConstItem);

		int8 CompactNamesNum = static_cast<int8>(Item.ClassNameAndObjectPath.ObjectClassAndPath.Num());
		ensure(Item.ClassNameAndObjectPath.ObjectClassAndPath.Num() < 256);
		*Archive << CompactNamesNum;
		for (int Idx = 0; Idx < (int)CompactNamesNum; ++Idx)
		{
			*Archive << Item.ClassNameAndObjectPath.ObjectClassAndPath[Idx];
		}

		*Archive << Item.ShaderType;
		*Archive << Item.ShaderClass;
		*Archive << Item.MaterialDomain;
		*Archive << Item.FeatureLevel;
		*Archive << Item.QualityLevel;
		*Archive << Item.TargetFrequency;
		*Archive << Item.TargetPlatform;
		*Archive << Item.VFType;
		*Archive << Item.PermutationId;

		uint64 PipelineHashIdx = static_cast<uint64>(*HashToIndex.Find(Item.PipelineHash))	;
		WriteVarUIntToArchive(*Archive, PipelineHashIdx);
		uint64 OutputHashIdx = static_cast<uint64>(*HashToIndex.Find(Item.OutputHash));
		WriteVarUIntToArchive(*Archive, OutputHashIdx);
	}

	return true;
}
#endif // WITH_EDITOR
