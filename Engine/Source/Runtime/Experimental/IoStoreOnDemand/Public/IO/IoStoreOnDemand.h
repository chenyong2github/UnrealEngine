// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "IO/IoChunkId.h"
#include "IO/IoHash.h"
#include "IO/IoStatus.h"
#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#define UE_API IOSTOREONDEMAND_API

class FCbWriter;
class FCbFieldView;
class IIoStoreWriter;
struct FIoContainerSettings;
struct FIoStoreWriterSettings;

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogIas, VeryVerbose, All);

namespace UE
{

////////////////////////////////////////////////////////////////////////////////
enum class EOnDemandTocVersion : uint32
{
	Invalid			= 0,
	Initial			= 1,

	LatestPlusOne,
	Latest			= (LatestPlusOne - 1)
};

enum class EOnDemandChunkVersion : uint32
{
	Invalid			= 0,
	Initial			= 1,

	LatestPlusOne,
	Latest			= (LatestPlusOne - 1)
};

struct FOnDemandTocHeader
{
	static constexpr uint64 ExpectedMagic = 0x6f6e64656d616e64; // ondemand

	uint64 Magic = ExpectedMagic;
	uint32 Version = uint32(EOnDemandTocVersion::Latest);
	uint32 ChunkVersion = uint32(EOnDemandChunkVersion::Latest);
	uint32 BlockSize = 0;
	FString CompressionFormat;
	FString ChunksDirectory;
	
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocHeader& Header);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocHeader& OutTocHeader);

struct FOnDemandTocEntry
{
	FIoHash Hash = FIoHash::Zero;
	FIoHash RawHash = FIoHash::Zero;
	FIoChunkId ChunkId = FIoChunkId::InvalidChunkId;
	uint64 RawSize = 0;
	uint64 EncodedSize = 0;
	uint32 BlockOffset = ~uint32(0);
	uint32 BlockCount = 0; 
	
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocEntry& Entry);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocEntry& OutTocEntry);

struct FOnDemandTocContainerEntry
{
	FString ContainerName;
	FString EncryptionKeyGuid;
	TArray<FOnDemandTocEntry> Entries;
	TArray<uint32> BlockSizes;
	TArray<FIoHash> BlockHashes;

	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocContainerEntry& ContainerEntry);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocContainerEntry& OutContainer);

struct FOnDemandToc
{
	FOnDemandTocHeader Header;
	TArray<FOnDemandTocContainerEntry> Containers;
	
	UE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandToc& TocResource);

	UE_NODISCARD UE_API static TIoStatusOr<FString> Save(const TCHAR* Directory, const FOnDemandToc& TocResource);
};

UE_API bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandToc& OutToc);

////////////////////////////////////////////////////////////////////////////////
#if (IS_PROGRAM || WITH_EDITOR)

class IOnDemandIoStoreWriter
{
public:
	virtual ~IOnDemandIoStoreWriter() = default;
	virtual TSharedPtr<IIoStoreWriter> CreateContainer(const FString& ContainerName, const FIoContainerSettings& ContainerSettings) = 0;
	virtual void Flush() = 0;
};

UE_API TUniquePtr<IOnDemandIoStoreWriter> MakeOnDemandIoStoreWriter(
	const FIoStoreWriterSettings& WriterSettings,
	const FString& OutputDirectory,
	uint32 MaxConcurrentWrites = 64);

#endif // (IS_PROGRAM || WITH_EDITOR)

} // namespace UE

#undef UE_API
