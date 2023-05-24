// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoStoreOnDemand.h"
#include "OnDemandIoDispatcherBackend.h"

#include "FileCache.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/LargeMemoryWriter.h"

DEFINE_LOG_CATEGORY(LogIoStoreOnDemand);

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

	static uint64 ParseSizeParam(const TCHAR* Param);
};

IMPLEMENT_MODULE(FIoStoreOnDemandModule, IoStoreOnDemand);

uint64 FIoStoreOnDemandModule::ParseSizeParam(const TCHAR* Param)
{
	FString ParamValue;
	if (FParse::Value(FCommandLine::Get(), Param, ParamValue))
	{
		uint64 Size = 0;
		if (FParse::Value(FCommandLine::Get(), Param, Size))
		{
			if (ParamValue.EndsWith(TEXT("GB")))
			{
				Size *= 1024*1024*1024;
			}
			if (ParamValue.EndsWith(TEXT("MB")))
			{
				Size *= 1024*1024;
			}
			if (ParamValue.EndsWith(TEXT("KB")))
			{
				Size *= 1024;
			}

			return Size;
		}
	}

	return 0;
}

void FIoStoreOnDemandModule::StartupModule()
{
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
