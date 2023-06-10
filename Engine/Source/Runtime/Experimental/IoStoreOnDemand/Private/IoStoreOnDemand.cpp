// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoStoreOnDemand.h"
#include "OnDemandIoDispatcherBackend.h"
#include "EncryptionKeyManager.h"

#include "FileCache.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/LargeMemoryWriter.h"

DEFINE_LOG_CATEGORY(LogIoStoreOnDemand);

namespace UE::IO::Private
{

////////////////////////////////////////////////////////////////////////////////
static int64 ParseSizeParam(FStringView Value)
{
	Value = Value.TrimStartAndEnd();

	int64 Size = -1;
	LexFromString(Size, Value.GetData());
	if (Size >= 0)
	{
		if (Value.EndsWith(TEXT("GB"))) return Size << 30;
		if (Value.EndsWith(TEXT("MB"))) return Size << 20;
		if (Value.EndsWith(TEXT("KB"))) return Size << 10;
	}
	return Size;
}

////////////////////////////////////////////////////////////////////////////////
static int64 ParseSizeParam(const TCHAR* CommandLine, const TCHAR* Param)
{
	FString ParamValue;
	if (!FParse::Value(CommandLine, Param, ParamValue))
	{
		return -1;
	}

	return ParseSizeParam(ParamValue);
}

////////////////////////////////////////////////////////////////////////////////
static bool ParseEncryptionKeyParam(const FString& Param, FGuid& OutKeyGuid, FAES::FAESKey& OutKey)
{
	TArray<FString> Tokens;
	Param.ParseIntoArray(Tokens, TEXT(":"), true);

	if (Tokens.Num() == 2)
	{
		TArray<uint8> KeyBytes;
		if (FGuid::Parse(Tokens[0], OutKeyGuid) && FBase64::Decode(Tokens[1], KeyBytes))
		{
			if (OutKeyGuid != FGuid() && KeyBytes.Num() == FAES::FAESKey::KeySize)
			{
				FMemory::Memcpy(OutKey.Key, KeyBytes.GetData(), FAES::FAESKey::KeySize);
				return true;
			}
		}
	}
	
	return false;
}

} // namespace UE::IO::Private



namespace UE
{

////////////////////////////////////////////////////////////////////////////////
FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocHeader& Header)
{
	Writer.BeginObject();
	Writer.AddInteger(UTF8TEXTVIEW("Magic"), Header.Magic);
	Writer.AddInteger(UTF8TEXTVIEW("Version"), Header.Version);
	Writer.AddInteger(UTF8TEXTVIEW("ChunkVersion"), Header.ChunkVersion);
	Writer.AddInteger(UTF8TEXTVIEW("BlockSize"), Header.BlockSize);
	Writer.AddString(UTF8TEXTVIEW("CompressionFormat"), Header.CompressionFormat);
	Writer.AddString(UTF8TEXTVIEW("ChunksDirectory"), Header.ChunksDirectory);
	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocHeader& OutTocHeader)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		OutTocHeader.Magic = Obj["Magic"].AsUInt64();
		OutTocHeader.Version = Obj["Version"].AsUInt32();
		OutTocHeader.ChunkVersion = Obj["ChunkVersion"].AsUInt32();
		OutTocHeader.BlockSize = Obj["BlockSize"].AsUInt32();
		OutTocHeader.CompressionFormat = FString(Obj["CompressionFormat"].AsString());
		OutTocHeader.ChunksDirectory = FString(Obj["ChunksDirectory"].AsString());

		return OutTocHeader.Magic == FOnDemandTocHeader::ExpectedMagic &&
			static_cast<EOnDemandTocVersion>(OutTocHeader.Version) != EOnDemandTocVersion::Invalid;
	}

	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocEntry& Entry)
{
	Writer.BeginObject();
	Writer.AddHash(UTF8TEXTVIEW("Hash"), Entry.Hash);
	Writer.AddHash(UTF8TEXTVIEW("RawHash"), Entry.RawHash);
	Writer << UTF8TEXTVIEW("ChunkId") << Entry.ChunkId;
	Writer.AddInteger(UTF8TEXTVIEW("RawSize"), Entry.RawSize);
	Writer.AddInteger(UTF8TEXTVIEW("EncodedSize"), Entry.EncodedSize);
	Writer.AddInteger(UTF8TEXTVIEW("BlockOffset"), Entry.BlockOffset);
	Writer.AddInteger(UTF8TEXTVIEW("BlockCount"), Entry.BlockCount);
	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocEntry& OutTocEntry)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		if (!LoadFromCompactBinary(Obj["ChunkId"], OutTocEntry.ChunkId))
		{
			return false;
		}

		OutTocEntry.Hash = Obj["Hash"].AsHash();
		OutTocEntry.RawHash = Obj["RawHash"].AsHash();
		OutTocEntry.RawSize = Obj["RawSize"].AsUInt64(~uint64(0));
		OutTocEntry.EncodedSize = Obj["EncodedSize"].AsUInt64(~uint64(0));
		OutTocEntry.BlockOffset = Obj["BlockOffset"].AsUInt32(~uint32(0));
		OutTocEntry.BlockCount = Obj["BlockCount"].AsUInt32();

		return OutTocEntry.Hash != FIoHash::Zero &&
			OutTocEntry.RawSize != ~uint64(0) &&
			OutTocEntry.EncodedSize != ~uint64(0) &&
			OutTocEntry.BlockOffset != ~uint32(0);
	}

	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandTocContainerEntry& ContainerEntry)
{
	Writer.BeginObject();
	Writer.AddString(UTF8TEXTVIEW("Name"), ContainerEntry.ContainerName);
	Writer.AddString(UTF8TEXTVIEW("EncryptionKeyGuid"), ContainerEntry.EncryptionKeyGuid);

	Writer.BeginArray(UTF8TEXTVIEW("Entries"));
	for (const FOnDemandTocEntry& Entry : ContainerEntry.Entries)
	{
		Writer << Entry;
	}
	Writer.EndArray();
	
	Writer.BeginArray(UTF8TEXTVIEW("BlockSizes"));
	for (uint32 BlockSize : ContainerEntry.BlockSizes)
	{
		Writer << BlockSize;
	}
	Writer.EndArray();

	Writer.BeginArray(UTF8TEXTVIEW("BlockHashes"));
	for (const FIoHash& BlockHash : ContainerEntry.BlockHashes)
	{
		Writer << BlockHash;
	}
	Writer.EndArray();

	Writer.EndObject();

	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandTocContainerEntry& OutContainer)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		OutContainer.ContainerName = FString(Obj["Name"].AsString());
		OutContainer.EncryptionKeyGuid = FString(Obj["EncryptionKeyGuid"].AsString());

		FCbArrayView Entries = Obj["Entries"].AsArrayView();
		OutContainer.Entries.Reserve(int32(Entries.Num()));
		for (FCbFieldView ArrayField : Entries)
		{
			if (!LoadFromCompactBinary(ArrayField, OutContainer.Entries.AddDefaulted_GetRef()))
			{
				return false;
			}
		}

		FCbArrayView BlockSizes = Obj["BlockSizes"].AsArrayView();
		OutContainer.BlockSizes.Reserve(int32(BlockSizes.Num()));
		for (FCbFieldView ArrayField : BlockSizes)
		{
			OutContainer.BlockSizes.Add(ArrayField.AsUInt32());
		}

		FCbArrayView BlockHashes = Obj["BlockHashes"].AsArrayView();
		OutContainer.BlockHashes.Reserve(int32(BlockHashes.Num()));
		for (FCbFieldView ArrayField : BlockHashes)
		{
			OutContainer.BlockHashes.Add(ArrayField.AsHash());
		}

		return true;
	}

	return false;
}

FCbWriter& operator<<(FCbWriter& Writer, const FOnDemandToc& TocResource)
{
	Writer.BeginObject();
	Writer << UTF8TEXTVIEW("Header") << TocResource.Header;

	Writer.BeginArray(UTF8TEXTVIEW("Containers"));
	for (const FOnDemandTocContainerEntry& Container : TocResource.Containers)
	{
		Writer << Container;
	}
	Writer.EndArray();
	Writer.EndObject();
	
	return Writer;
}

TIoStatusOr<FString> FOnDemandToc::Save(const TCHAR* Directory, const FOnDemandToc& TocResource)
{
	if (TocResource.Header.Magic != FOnDemandTocHeader::ExpectedMagic)
	{
		return FIoStatus(EIoErrorCode::CorruptToc);
	}

	if (TocResource.Header.CompressionFormat.IsEmpty())
	{
		return FIoStatus(EIoErrorCode::CorruptToc);
	}

	FCbWriter Writer;
	Writer << TocResource;

	FLargeMemoryWriter Ar;
	SaveCompactBinary(Ar, Writer.Save());

	const FIoHash TocHash = FIoHash::HashBuffer(Ar.GetView());
	const FString FilePath = FString(Directory) / LexToString(TocHash) + TEXT(".iochunktoc");

	IFileManager& FileMgr = IFileManager::Get();
	if (TUniquePtr<FArchive> FileAr(FileMgr.CreateFileWriter(*FilePath)); FileAr.IsValid())
	{
		FileAr->Serialize(Ar.GetData(), Ar.TotalSize());
		FileAr->Flush();
		FileAr->Close();

		return FilePath;
	}

	return FIoStatus(EIoErrorCode::WriteError);
}

bool LoadFromCompactBinary(FCbFieldView Field, FOnDemandToc& OutToc)
{
	if (FCbObjectView Obj = Field.AsObjectView())
	{
		if (!LoadFromCompactBinary(Obj["Header"], OutToc.Header))
		{
			return false;
		}

		FCbArrayView Containers = Obj["Containers"].AsArrayView();
		OutToc.Containers.Reserve(int32(Containers.Num()));
		for (FCbFieldView ArrayField : Containers)
		{
			if (!LoadFromCompactBinary(ArrayField, OutToc.Containers.AddDefaulted_GetRef()))
			{
				return false;
			}
		}

		return true;
	}

	return false;
}

} // namespace UE

////////////////////////////////////////////////////////////////////////////////
class FIoStoreOnDemandModule
	: public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
};

IMPLEMENT_MODULE(FIoStoreOnDemandModule, IoStoreOnDemand);

void FIoStoreOnDemandModule::StartupModule()
{
	using namespace UE::IO::Private;

#if !WITH_EDITOR
	UE::FOnDemandEndpoint Endpoint;
	
	FString UrlParam;
	if (FParse::Value(FCommandLine::Get(), TEXT("IoStoreOnDemand="), UrlParam))
	{
		FStringView UrlView(UrlParam);
		if (UrlView.StartsWith(TEXTVIEW("http://")) && UrlView.EndsWith(TEXTVIEW(".iochunktoc")))
		{
			int32 Delim = INDEX_NONE;
			if (UrlView.RightChop(7).FindChar(TEXT('/'), Delim))
			{
				Endpoint.ServiceUrl = UrlView.Left(7 +  Delim);
				Endpoint.TocPath = UrlView.RightChop(Endpoint.ServiceUrl.Len() + 1);
			}
		}
	}

	{
		FString EncryptionKey;
		if (FParse::Value(FCommandLine::Get(), TEXT("OnDemandEncryptionKey="), EncryptionKey))
		{
			FGuid KeyGuid;
			FAES::FAESKey Key;
			if (ParseEncryptionKeyParam(EncryptionKey, KeyGuid, Key))
			{
				UE::FEncryptionKeyManager::Get().AddKey(KeyGuid, Key);
			}
		}
	}

	if (!Endpoint.IsValid())
	{
		Endpoint = UE::FOnDemandEndpoint();
		FString ConfigFileName = TEXT("IoStoreOnDemand.ini");
		FString ConfigPath = FPaths::Combine(TEXT("Cloud"), ConfigFileName);
		FString ConfigContent = FPlatformMisc::LoadTextFileFromPlatformPackage(ConfigPath);

		if (ConfigContent.Len())
		{
			FConfigFile Config;
			Config.ProcessInputFileContents(ConfigContent, ConfigFileName);

			Config.GetString(TEXT("Endpoint"), TEXT("DistributionUrl"), Endpoint.DistributionUrl);
			Config.GetString(TEXT("Endpoint"), TEXT("ServiceUrl"), Endpoint.ServiceUrl);
			Config.GetString(TEXT("Endpoint"), TEXT("TocPath"), Endpoint.TocPath);
			
			if (Endpoint.DistributionUrl.EndsWith(TEXT("/")))
			{
				Endpoint.DistributionUrl = Endpoint.DistributionUrl.Left(Endpoint.DistributionUrl.Len() - 1);
			}
			
			if (Endpoint.ServiceUrl.EndsWith(TEXT("/")))
			{
				Endpoint.ServiceUrl = Endpoint.DistributionUrl.Left(Endpoint.ServiceUrl.Len() - 1);
			}

			if (Endpoint.TocPath.StartsWith(TEXT("/")))
			{
				Endpoint.TocPath.RightChopInline(1);
			}

			FString ContentKey;
			if (Config.GetString(TEXT("Endpoint"), TEXT("ContentKey"), ContentKey))
			{
				FGuid KeyGuid;
				FAES::FAESKey Key;
				if (ParseEncryptionKeyParam(ContentKey, KeyGuid, Key))
				{
					UE::FEncryptionKeyManager::Get().AddKey(KeyGuid, Key);
				}
			}
		}
	}

	if (Endpoint.IsValid())
	{
		TSharedPtr<IIoCache> Cache;

		if (uint64 DiskSize = ParseSizeParam(TEXT("OnDemandFileCache=")); DiskSize > 0)
		{
			uint64 MemorySize = ParseSizeParam(TEXT("OnDemandFileCacheQueueSize="));
			FFileIoCacheConfig FileCacheConfig;
			FileCacheConfig.DiskStorageSize = DiskSize;
			FileCacheConfig.MemoryStorageSize = MemorySize > 0 ? MemorySize : (16ull << 20);

			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Using %lluB file cache"), DiskSize);
			Cache = MakeShareable(MakeFileIoCache(FileCacheConfig).Release());
		}
		TSharedPtr<UE::IOnDemandIoDispatcherBackend> Backend = UE::MakeOnDemandIoDispatcherBackend(Cache);

		Endpoint.EndpointType = UE::EOnDemandEndpointType::CDN;
		Backend->Mount(Endpoint);
		FIoDispatcher::Get().Mount(Backend.ToSharedRef(), -10);
	}
#endif // !WITH_EDITOR
}

void FIoStoreOnDemandModule::ShutdownModule()
{
}
