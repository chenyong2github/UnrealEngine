// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreUtilities.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Hash/CityHash.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "IO/IoDispatcher.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "Serialization/Archive.h"
#include "Serialization/BulkDataManifest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/AsyncLoading2.h"
#include "UObject/NameBatchSerialization.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"
#include "Algo/Find.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Async/ParallelFor.h"
#include "Async/AsyncFileHandle.h"
#include "Async/Async.h"

//PRAGMA_DISABLE_OPTIMIZATION

IMPLEMENT_MODULE(FDefaultModuleImpl, IoStoreUtilities);

DEFINE_LOG_CATEGORY_STATIC(LogIoStore, Log, All);

#define IOSTORE_CPU_SCOPE(NAME) TRACE_CPUPROFILER_EVENT_SCOPE(IoStore##NAME);
#define IOSTORE_CPU_SCOPE_DATA(NAME, DATA) TRACE_CPUPROFILER_EVENT_SCOPE(IoStore##NAME);

#define OUTPUT_CHUNKID_DIRECTORY 0
#define OUTPUT_NAMEMAP_CSV 0
#define OUTPUT_IMPORTMAP_CSV 0
#define OUTPUT_DEBUG_PACKAGE_HASHES 0

static const FName DefaultCompressionMethod = NAME_Zlib;
static const int64 DefaultCompressionBlockSize = 64 << 10;
const int64 DefaultMemoryMappingAlignment = 16 << 10;

class FNameMapBuilder
{
public:
	void SetNameMapType(FMappedName::EType InNameMapType)
	{
		NameMapType = InNameMapType;
	}

	void MarkNamesAsReferenced(const TArray<FName>& Names, TArray<int32>& OutNameIndices)
	{
		for (const FName& Name : Names)
		{
			const FNameEntryId Id = Name.GetComparisonIndex();
			int32& Index = NameIndices.FindOrAdd(Id);
			if (Index == 0)
			{
				Index = NameIndices.Num();
				NameMap.Add(Id);
			}

			OutNameIndices.Add(Index - 1);
		}
	}

	void MarkNameAsReferenced(const FName& Name)
	{
		const FNameEntryId Id = Name.GetComparisonIndex();
		int32& Index = NameIndices.FindOrAdd(Id);
		if (Index == 0)
		{
			Index = NameIndices.Num();
			NameMap.Add(Id);
		}
#if OUTPUT_NAMEMAP_CSV
		// debug counts
		{
			const int32 Number = Name.GetNumber();
			TTuple<int32,int32,int32>& Counts = DebugNameCounts.FindOrAdd(Id);

			if (Number == 0)
			{
				++Counts.Get<0>();
			}
			else
			{
				++Counts.Get<1>();
				if (Number > Counts.Get<2>())
				{
					Counts.Get<2>() = Number;
				}
			}
		}
#endif
	}

	FMappedName MapName(const FName& Name) const
	{
		const FNameEntryId Id = Name.GetComparisonIndex();
		const int32* Index = NameIndices.Find(Id);
		check(Index);
		return FMappedName::Create(*Index - 1, Name.GetNumber(), NameMapType);
	}

	const TArray<FNameEntryId>& GetNameMap() const
	{
		return NameMap;
	}

	friend FArchive& operator<<(FArchive& Ar, FNameMapBuilder& NameMapBuilder)
	{
		if (Ar.IsSaving())
		{
			int32 NameCount = NameMapBuilder.NameMap.Num();
			Ar << NameCount;
			for (FNameEntryId NameEntryId : NameMapBuilder.NameMap)
			{
				const FNameEntry* NameEntry = FName::GetEntry(NameEntryId);
				NameEntry->Write(Ar);
			}
		}
		else
		{
			int32 NameCount;
			Ar << NameCount;
			for (int32 NameIndex = 0; NameIndex < NameCount; ++NameIndex)
			{
				FNameEntrySerialized NameEntrySerialized(ENAME_LinkerConstructor);
				Ar << NameEntrySerialized;
				FName Name(NameEntrySerialized);
				FNameEntryId NameId = Name.GetComparisonIndex();
				NameMapBuilder.NameMap.Add(NameId);
				NameMapBuilder.NameIndices.Add(NameId, NameIndex + 1);
			}
		}
		return Ar;
	}

#if OUTPUT_NAMEMAP_CSV
	void SaveCsv(const FString& CsvFilePath)
	{
		{
			TUniquePtr<FArchive> CsvArchive(IFileManager::Get().CreateFileWriter(*CsvFilePath));
			if (CsvArchive)
			{
				TCHAR Name[FName::StringBufferSize];
				ANSICHAR Line[MAX_SPRINTF + FName::StringBufferSize];
				ANSICHAR Header[] = "Length\tMaxNumber\tNumberCount\tBaseCount\tTotalCount\tFName\n";
				CsvArchive->Serialize(Header, sizeof(Header) - 1);
				for (auto& Counts : DebugNameCounts)
				{
					const int32 NameLen = FName::CreateFromDisplayId(Counts.Key, 0).ToString(Name);
					FCStringAnsi::Sprintf(Line, "%d\t%d\t%d\t%d\t%d\t",
						NameLen, Counts.Value.Get<2>(), Counts.Value.Get<1>(), Counts.Value.Get<0>(), Counts.Value.Get<0>() + Counts.Value.Get<1>());
					ANSICHAR* L = Line + FCStringAnsi::Strlen(Line);
					const TCHAR* N = Name;
					while (*N)
					{
						*L++ = CharCast<ANSICHAR,TCHAR>(*N++);
					}
					*L++ = '\n';
					CsvArchive.Get()->Serialize(Line, L - Line);
				}
			}
		}
	}
#endif

	void Empty()
	{
		NameIndices.Empty();
		NameMap.Empty();
#if OUTPUT_NAMEMAP_CSV
		DebugNameCounts.Empty();
#endif
	}
private:
	TMap<FNameEntryId, int32> NameIndices;
	TArray<FNameEntryId> NameMap;
#if OUTPUT_NAMEMAP_CSV
	TMap<FNameEntryId, TTuple<int32,int32,int32>> DebugNameCounts; // <Number0Count,OtherNumberCount,MaxNumber>
#endif
	FMappedName::EType NameMapType = FMappedName::EType::Global;
};

class FNameReaderProxyArchive
	: public FArchiveProxy
{
	const TArray<FNameEntryId>& NameMap;

public:
	using FArchiveProxy::FArchiveProxy;

	FNameReaderProxyArchive(FArchive& InAr, const TArray<FNameEntryId>& InNameMap)
		: FArchiveProxy(InAr)
		, NameMap(InNameMap)
	{ 
		// Replicate the filter editor only state of the InnerArchive as FArchiveProxy will
		// not intercept it.
		FArchive::SetFilterEditorOnly(InAr.IsFilterEditorOnly());
	}

	FArchive& operator<<(FName& Name)
	{
		int32 NameIndex, Number;
		InnerArchive << NameIndex << Number;

		if (!NameMap.IsValidIndex(NameIndex))
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Bad name index %i/%i"), NameIndex, NameMap.Num());
		}

		const FNameEntryId MappedName = NameMap[NameIndex];
		Name = FName::CreateFromDisplayId(MappedName, Number);

		return *this;
	}
};

struct FContainerSourceFile 
{
	FString NormalizedPath;
	bool bNeedsCompression = false;
};

struct FContainerSourceSpec
{
	FName Name;
	FString OutputPath;
	TArray<FContainerSourceFile> SourceFiles;
	TArray<FString> PatchSourceContainerFiles;
	bool bGenerateDiffPatch;
};

struct FCookedFileStatData
{
	enum EFileExt { UMap, UAsset, UExp, UBulk, UPtnl, UMappedBulk };
	enum EFileType { PackageHeader, PackageData, BulkData };

	int64 FileSize = 0;
	EFileType FileType = PackageHeader;
	EFileExt FileExt = UMap;
};

using FCookedFileStatMap = TMap<FString, FCookedFileStatData>;

struct FPackage;
struct FContainerTargetSpec;

struct FContainerTargetFilePartialMapping
{
	FIoChunkId PartialChunkId;
	uint64 Offset;
	uint64 Length;
};

struct FContainerTargetFile
{
	FContainerTargetSpec* ContainerTarget = nullptr;
	FPackage* Package = nullptr;
	FString NormalizedSourcePath;
	FString TargetPath;
	uint64 SourceSize = 0;
	uint64 TargetSize = 0;
	uint64 Offset = 0;
	uint64 Padding = 0;
	uint64 Alignment = 0;
	FIoChunkId ChunkId;
	FIoChunkHash ChunkHash;
	TArray<uint8> PackageHeaderData;
	TArray<int32> NameIndices;
	TArray<FContainerTargetFilePartialMapping> PartialMappings;
	bool bIsBulkData = false;
	bool bIsOptionalBulkData = false;
	bool bIsMemoryMappedBulkData = false;
	bool bForceUncompressed = false;
};

struct FIoStoreArguments
{
	FString GlobalContainerPath;
	FString CookedDir;
	FString OutputReleaseVersionDir;
	FString BasedOnReleaseVersionDir;
	TArray<FContainerSourceSpec> Containers;
	FCookedFileStatMap CookedFileStatMap;
	TMap<FName, uint64> GameOrderMap;
	TMap<FName, uint64> CookerOrderMap;
	int64 MemoryMappingAlignment = 0;
};

struct FContainerMeta
{
	FString ContainerName;
	FIoContainerId ContainerId;
	FNameMapBuilder NameMapBuilder;

	friend FArchive& operator<<(FArchive& Ar, FContainerMeta& ContainerMeta)
	{
		Ar << ContainerMeta.ContainerName;
		Ar << ContainerMeta.ContainerId;
		Ar << ContainerMeta.NameMapBuilder;
		return Ar;
	}
};

struct FContainerTargetSpec
{
	FContainerHeader Header;
	FName Name;
	FString OutputPath;
	FIoStoreWriter* IoStoreWriter;
	TArray<FContainerTargetFile> TargetFiles;
	TUniquePtr<FIoStoreEnvironment> IoStoreEnv;
	TArray<TUniquePtr<FIoStoreReader>> PatchSourceReaders;
	FNameMapBuilder LocalNameMapBuilder;
	FNameMapBuilder* NameMapBuilder = nullptr;
	bool bIsCompressed = false;
	bool bUseLocalNameMap = false;
	bool bGenerateDiffPatch = false;
};

struct FPackageAssetData
{
	TArray<FObjectImport> ObjectImports;
	TArray<FObjectExport> ObjectExports;
	TArray<FPackageIndex> PreloadDependencies;
};

struct FPackage;
using FPackageMap = TMap<FName, FPackage*>;
using FPackageGlobalIdMap = TMap<FName, FPackageId>;
using FSourceToLocalizedPackageMultimap = TMultiMap<FPackage*, FPackage*>;
using FLocalizedToSourceImportIndexMap = TMap<FPackageObjectIndex, FPackageObjectIndex>;

static constexpr TCHAR L10NPrefix[] = TEXT("/Game/L10N/");

// modified copy from PakFileUtilities
static FString RemapLocalizationPathIfNeeded(const FString& Path, FString* OutRegion)
{
	static constexpr int32 L10NPrefixLength = sizeof(L10NPrefix)/sizeof(TCHAR) - 1;

	int32 FoundIndex = Path.Find(L10NPrefix, ESearchCase::IgnoreCase);
	if (FoundIndex >= 0)
	{
		// Validate the content index is the first one
		int32 ContentIndex = Path.Find(TEXT("/Game/"), ESearchCase::IgnoreCase);
		if (ContentIndex == FoundIndex)
		{
			int32 EndL10NOffset = ContentIndex + L10NPrefixLength;
			int32 NextSlashIndex = Path.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, EndL10NOffset);
			int32 RegionLength = NextSlashIndex - EndL10NOffset;
			if (RegionLength >= 2)
			{
				FString NonLocalizedPath = Path.Mid(0, ContentIndex) + TEXT("/Game") + Path.Mid(NextSlashIndex);
				if (OutRegion)
				{
					*OutRegion = Path.Mid(EndL10NOffset, RegionLength);
					OutRegion->ToLowerInline();
				}
				return NonLocalizedPath;
			}
		}
	}
	return Path;
}

#if OUTPUT_CHUNKID_DIRECTORY
class FChunkIdCsv
{
public:

	~FChunkIdCsv()
	{
		if (OutputArchive)
		{
			OutputArchive->Flush();
		}
	}

	void CreateOutputFile(const FString& RootPath)
	{
		const FString OutputFilename = RootPath / TEXT("chunkid_directory.csv");
		OutputArchive.Reset(IFileManager::Get().CreateFileWriter(*OutputFilename));
		if (OutputArchive)
		{
			const ANSICHAR* Output = "NameIndex,NameNumber,ChunkIndex,ChunkType,ChunkIdHash,DebugString\n";
			OutputArchive->Serialize((void*)Output, FPlatformString::Strlen(Output));
		}
	}

	void AddChunk(uint32 NameIndex, uint32 NameNumber, uint16 ChunkIndex, uint8 ChunkType, uint32 ChunkIdHash, const TCHAR* DebugString)
	{
		ANSICHAR Buffer[MAX_SPRINTF + 1] = { 0 };
		int32 NumOfCharacters = FCStringAnsi::Sprintf(Buffer, "%u,%u,%u,%u,%u,%s\n", NameIndex, NameNumber, ChunkIndex, ChunkType, ChunkIdHash, TCHAR_TO_ANSI(DebugString));
		OutputArchive->Serialize(Buffer, NumOfCharacters);
	}	

private:
	TUniquePtr<FArchive> OutputArchive;
};
FChunkIdCsv ChunkIdCsv;

#endif

static FIoChunkId CreateChunkId(FPackageId GlobalPackageId, uint16 ChunkIndex, EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateIoChunkId(GlobalPackageId.ToIndex(), ChunkIndex, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, ChunkIndex, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

static FIoChunkId CreateChunkIdForBulkData(FPackageId GlobalPackageId,EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateIoChunkId(GlobalPackageId.ToIndex(), 0, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, 0, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

static FIoChunkId CreateChunkIdForBulkData(FPackageId GlobalPackageId, int64 BulkdataOffset, EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateBulkdataChunkId(GlobalPackageId.ToIndex(), BulkdataOffset, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, 0, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

enum EPreloadDependencyType
{
	PreloadDependencyType_Create,
	PreloadDependencyType_Serialize,
};

struct FArc
{
	uint32 FromNodeIndex;
	uint32 ToNodeIndex;

	bool operator==(const FArc& Other) const
	{
		return ToNodeIndex == Other.ToNodeIndex && FromNodeIndex == Other.FromNodeIndex;
	}
};

struct FExportGraphNode;

struct FExportBundle
{
	TArray<FExportGraphNode*> Nodes;
	uint32 LoadOrder;
};

struct FPackageGraphNode
{
	FPackage* Package = nullptr;
	bool bTemporaryMark = false;
	bool bPermanentMark = false;
};

class FPackageGraph
{
public:
	FPackageGraph()
	{

	}

	~FPackageGraph()
	{
		for (FPackageGraphNode* Node : Nodes)
		{
			delete Node;
		}
	}

	FPackageGraphNode* AddNode(FPackage* Package)
	{
		FPackageGraphNode* Node = new FPackageGraphNode();
		Node->Package = Package;
		Nodes.Add(Node);
		return Node;
	}

	void AddImportDependency(FPackageGraphNode* FromNode, FPackageGraphNode* ToNode)
	{
		Edges.Add(FromNode, ToNode);
	}

	TArray<FPackage*> TopologicalSort() const;

private:
	TArray<FPackageGraphNode*> Nodes;
	TMultiMap<FPackageGraphNode*, FPackageGraphNode*> Edges;
};

struct FExportGraphNode
{
	FPackage* Package;
	FExportBundleEntry BundleEntry;
	TSet<FExportGraphNode*> ExternalDependencies;
	uint64 NodeIndex;
};

class FExportGraph
{
public:
	FExportGraph()
	{

	}

	~FExportGraph()
	{
		for (FExportGraphNode* Node : Nodes)
		{
			delete Node;
		}
	}

	FExportGraphNode* AddNode(FPackage* Package, const FExportBundleEntry& BundleEntry)
	{
		FExportGraphNode* Node = new FExportGraphNode();
		Node->Package = Package;
		Node->BundleEntry = BundleEntry;
		Node->NodeIndex = Nodes.Num();
		Nodes.Add(Node);
		return Node;
	}

	void AddInternalDependency(FExportGraphNode* FromNode, FExportGraphNode* ToNode)
	{
		AddEdge(FromNode, ToNode);
	}

	void AddExternalDependency(FExportGraphNode* FromNode, FExportGraphNode* ToNode)
	{
		AddEdge(FromNode, ToNode);
		ToNode->ExternalDependencies.Add(FromNode);
	}

	TArray<FExportGraphNode*> ComputeLoadOrder(const TArray<FPackage*>& Packages) const;

private:
	void AddEdge(FExportGraphNode* FromNode, FExportGraphNode* ToNode)
	{
		Edges.Add(FromNode, ToNode);
	}

	TArray<FExportGraphNode*> TopologicalSort() const;

	TArray<FExportGraphNode*> Nodes;
	TMultiMap<FExportGraphNode*, FExportGraphNode*> Edges;
};

#if OUTPUT_DEBUG_PACKAGE_HASHES
struct FPackageHashes
{
	FSHAHash UAssetHash;
	FSHAHash UExpHash;
	FIoChunkHash ExportBundleHash;
};
#endif

struct FPackage
{
	FName Name;
	FName SourcePackageName; // for localized packages
	FString FileName;
	FPackageId GlobalPackageId;
	FString Region; // for localized packages
	FPackageId SourceGlobalPackageId; // for localized packages
	int32 ImportedPackagesSerializeCount = 0; // < ImportedPackages.Num() for source packages that have localized packages
	uint32 PackageFlags = 0;
	uint32 CookedHeaderSize = 0;
	int32 NameCount = 0;
	int32 ImportCount = 0;
	int32 PreloadDependencyCount = 0;
	int32 ExportCount = 0;
	int32 ImportIndexOffset = -1;
	int32 ExportIndexOffset = -1;
	int32 PreloadIndexOffset = -1;
	int64 UExpSize = 0;
	int64 UAssetSize = 0;
	int64 SummarySize = 0;
	int64 UGraphSize = 0;
	int64 NameMapSize = 0;
	int64 ImportMapSize = 0;
	int64 ExportMapSize = 0;
	int64 ExportBundlesHeaderSize = 0;

	bool bIsLocalizedAndConformed = false;
	bool bHasCircularImportDependencies = false;

	TArray<FString> ImportedFullNames;

	TArray<FPackage*> ImportedPackages;
	TArray<FPackage*> ImportedByPackages;
	TSet<FPackage*> AllReachablePackages;
	TSet<FPackage*> ImportedPreloadPackages;

	TArray<FName> Names;
	TArray<FNameEntryId> NameMap;

	TArray<FPackageObjectIndex> Imports;
	TArray<FPackageObjectIndex> PublicExports;
	TArray<int32> Exports;
	TArray<FArc> InternalArcs;
	TMap<FPackage*, TArray<FArc>> ExternalArcs;
	
	TArray<FExportBundle> ExportBundles;
	TMap<FExportGraphNode*, uint32> ExportBundleMap;

	TArray<FExportGraphNode*> CreateExportNodes;
	TArray<FExportGraphNode*> SerializeExportNodes;

	TArray<FExportGraphNode*> NodesWithNoIncomingEdges;
	FPackageGraphNode* Node = nullptr;

	uint64 HeaderSerialSize = 0;
	uint64 ExportsSerialSize = 0;

	uint64 DiskLayoutOrder = MAX_uint64;
#if OUTPUT_DEBUG_PACKAGE_HASHES
	FPackageHashes Hashes;
#endif
};

struct FCircularImportChain
{
	TArray<FName> SortedNames;
	TArray<FPackage*> Packages;
	uint32 Hash = 0;

	FCircularImportChain()
	{
		Packages.Reserve(128);
	}

	void Add(FPackage* Package)
	{
		Packages.Add(Package);
	}

	void Pop()
	{
		Packages.Pop();
	}

	int32 Num()
	{
		return Packages.Num();
	}

	void SortAndGenerateHash()
	{
		SortedNames.Empty(Packages.Num());
		for (FPackage* Package : Packages)
		{
			SortedNames.Emplace(Package->Name);
		}
		SortedNames.Sort(FNameLexicalLess());
		Hash = CityHash32((char*)SortedNames.GetData(), SortedNames.Num() * SortedNames.GetTypeSize());
	}

	FString ToString()
	{
		FString Result = FString::Printf(TEXT("%d:%u: "), SortedNames.Num(), Hash);
		for (const FName& Name : SortedNames)
		{
			Result.Append(Name.ToString());
			Result.Append(TEXT(" -> "));
		}
		Result.Append(SortedNames[0].ToString());
		return Result;
	}

	bool operator==(const FCircularImportChain& Other) const
	{
		return Hash == Other.Hash && SortedNames == Other.SortedNames;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FCircularImportChain& In)
	{
		return In.Hash;
	}
};

TArray<FPackage*> FPackageGraph::TopologicalSort() const
{
	TMultiMap<FPackageGraphNode*, FPackageGraphNode*> EdgesCopy = Edges;
	TArray<FPackage*> Result;
	Result.Reserve(Nodes.Num());
	
	struct
	{
		void Visit(FPackageGraphNode* Node)
		{
			if (Node->bPermanentMark || Node->bTemporaryMark)
			{
				return;
			}
			Node->bTemporaryMark = true;
			for (auto EdgeIt = Edges.CreateKeyIterator(Node); EdgeIt; ++EdgeIt)
			{
				FPackageGraphNode* ToNode = EdgeIt.Value();
				Visit(ToNode);
			}
			Node->bTemporaryMark = false;
			Node->bPermanentMark = true;
			Result.Add(Node->Package);
		}

		TMultiMap<FPackageGraphNode*, FPackageGraphNode*>& Edges;
		TArray<FPackage*>& Result;

	} Visitor{ EdgesCopy, Result };

	for (FPackageGraphNode* Node : Nodes)
	{
		Visitor.Visit(Node);
	}
	check(Result.Num() == Nodes.Num());

	Algo::Reverse(Result);
	return Result;
}

TArray<FExportGraphNode*> FExportGraph::ComputeLoadOrder(const TArray<FPackage*>& Packages) const
{
	IOSTORE_CPU_SCOPE(ComputeLoadOrder);
	FPackageGraph PackageGraph;
	{
		for (FPackage* Package : Packages)
		{
			Package->Node = PackageGraph.AddNode(Package);
		}
		for (FPackage* Package : Packages)
		{
			for (FPackage* ImportedPackage : Package->ImportedPackages)
			{
				PackageGraph.AddImportDependency(ImportedPackage->Node, Package->Node);
			}
		}
	}

	TArray<FPackage*> SortedPackages = PackageGraph.TopologicalSort();
	
	int32 NodeCount = Nodes.Num();
	TArray<uint32> NodesIncomingEdgeCount;
	NodesIncomingEdgeCount.AddZeroed(NodeCount);

	TMultiMap<FExportGraphNode*, FExportGraphNode*> EdgesCopy = Edges;
	for (auto& KV : Edges)
	{
		FExportGraphNode* ToNode = KV.Value;
		++NodesIncomingEdgeCount[ToNode->NodeIndex];
	}

	TArray<FExportGraphNode*> LoadOrder;
	LoadOrder.Reserve(NodeCount);
	
	for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
	{
		if (NodesIncomingEdgeCount[NodeIndex] == 0)
		{
			FExportGraphNode* Node = Nodes[NodeIndex];
			Node->Package->NodesWithNoIncomingEdges.Push(Node);
		}
	}
	while (LoadOrder.Num() < NodeCount)
	{
		for (FPackage* Package : SortedPackages)
		{
			while (Package->NodesWithNoIncomingEdges.Num() > 0)
			{
				FExportGraphNode* RemovedNode = Package->NodesWithNoIncomingEdges.Pop(false);
				LoadOrder.Add(RemovedNode);
				for (auto EdgeIt = EdgesCopy.CreateKeyIterator(RemovedNode); EdgeIt; ++EdgeIt)
				{
					FExportGraphNode* ToNode = EdgeIt.Value();
					if (--NodesIncomingEdgeCount[ToNode->NodeIndex] == 0)
					{
						ToNode->Package->NodesWithNoIncomingEdges.Push(ToNode);
					}
					EdgeIt.RemoveCurrent();
				}
			}
		}
	}

	return LoadOrder;
}

static void AddInternalExportArc(FExportGraph& ExportGraph, FPackage& Package, uint32 FromExportIndex, EPreloadDependencyType FromPhase, uint32 ToExportIndex, EPreloadDependencyType ToPhase)
{
	FExportGraphNode* FromNode = FromPhase == PreloadDependencyType_Create ? Package.CreateExportNodes[FromExportIndex] : Package.SerializeExportNodes[FromExportIndex];
	FExportGraphNode* ToNode = ToPhase == PreloadDependencyType_Create ? Package.CreateExportNodes[ToExportIndex] : Package.SerializeExportNodes[ToExportIndex];
	ExportGraph.AddInternalDependency(FromNode, ToNode);
}

static void AddExternalExportArc(FExportGraph& ExportGraph, FPackage& FromPackage, uint32 FromExportIndex, EPreloadDependencyType FromPhase, FPackage& ToPackage, uint32 ToExportIndex, EPreloadDependencyType ToPhase)
{
	FExportGraphNode* FromNode = FromPhase == PreloadDependencyType_Create ? FromPackage.CreateExportNodes[FromExportIndex] : FromPackage.SerializeExportNodes[FromExportIndex];
	FExportGraphNode* ToNode = ToPhase == PreloadDependencyType_Create ? ToPackage.CreateExportNodes[ToExportIndex] : ToPackage.SerializeExportNodes[ToExportIndex];
	ExportGraph.AddExternalDependency(FromNode, ToNode);
}

static void AddPostLoadArc(FPackage& FromPackage, FPackage& ToPackage)
{
	TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(&FromPackage);
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_PostLoad})));
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_PostLoad, EEventLoadNode2::Package_PostLoad})));
	ExternalArcs.Add({ EEventLoadNode2::Package_PostLoad, EEventLoadNode2::Package_PostLoad });
}

static void AddExportsDoneArc(FPackage& FromPackage, FPackage& ToPackage)
{
	TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(&FromPackage);
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_PostLoad})));
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_PostLoad, EEventLoadNode2::Package_PostLoad})));
	ExternalArcs.Add({ EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_PostLoad });
}

static void AddUniqueExternalBundleArc(FPackage& FromPackage, uint32 FromBundleIndex, FPackage& ToPackage, uint32 ToBundleIndex)
{
	uint32 FromNodeIndex = EEventLoadNode2::Package_NumPhases + FromBundleIndex * EEventLoadNode2::ExportBundle_NumPhases + EEventLoadNode2::ExportBundle_Process;
	uint32 ToNodeIndex = EEventLoadNode2::Package_NumPhases + ToBundleIndex * EEventLoadNode2::ExportBundle_NumPhases + EEventLoadNode2::ExportBundle_Process;
	TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(&FromPackage);
	ExternalArcs.AddUnique({ FromNodeIndex, ToNodeIndex });
}

static void AddReachablePackagesRecursive(FPackage& Package, FPackage& PackageWithImports, TSet<FPackage*>& Visited, bool bFirst)
{
	if (!bFirst)
	{
		bool bIsVisited = false;
		Visited.Add(&PackageWithImports, &bIsVisited);
		if (bIsVisited)
		{
			return;
		}

		if (&PackageWithImports == &Package)
		{
			return;
		}
	}

	if (PackageWithImports.AllReachablePackages.Num() > 0)
	{
		Visited.Append(PackageWithImports.AllReachablePackages);
		
	}
	else
	{
		for (FPackage* ImportedPackage : PackageWithImports.ImportedPackages)
		{
			AddReachablePackagesRecursive(Package, *ImportedPackage, Visited, false);
		}
	}
}

static bool FindNewCircularImportChains(
	FPackage& Package,
	FPackage& ImportedPackage,
	TSet<FPackage*>& Visited,
	TSet<FCircularImportChain>& CircularChains,
	FCircularImportChain& CurrentChain)
{
	if (&ImportedPackage == &Package)
	{
		Package.bHasCircularImportDependencies = true;
		CurrentChain.SortAndGenerateHash();
		bool bAlreadyFound = true;
		CircularChains.AddByHash(CurrentChain.Hash, CurrentChain, &bAlreadyFound);

		if (bAlreadyFound)
		{
			// UE_LOG(LogIoStore, Display, TEXT("OLD-IsCircular: %s with %s"), *Package.Name.ToString(), *CurrentChain.ToString());
			return false;
		}
		else
		{
			// UE_LOG(LogIoStore, Display, TEXT("NEW-IsCircular: %s with %s"), *Package.Name.ToString(), *CurrentChain.ToString());
			return true;
		}
	}

	bool bIsVisited = false;
	Visited.Add(&ImportedPackage, &bIsVisited);
	if (bIsVisited)
	{
		return false;
	}

	bool bFoundNew = false;
	for (FPackage* DependentPackage : ImportedPackage.ImportedPackages)
	{
		CurrentChain.Add(DependentPackage);
		bFoundNew |= FindNewCircularImportChains(Package, *DependentPackage, Visited, CircularChains, CurrentChain);
		CurrentChain.Pop();
	}

	return bFoundNew;
}

static void AddPackagePostLoadDependencies(
	FPackage& Package,
	TSet<FPackage*>& Visited,
	TSet<FCircularImportChain>& CircularChains)
{
	TSet<FPackage*> DependentPackages;

	for (FPackage* ImportedPackage : Package.ImportedPackages)
	{
		Visited.Reset();
		FCircularImportChain CurrentChain;
		CurrentChain.Add(ImportedPackage);
		if (FindNewCircularImportChains(Package, *ImportedPackage, Visited, CircularChains, CurrentChain))
		{
			DependentPackages.Append(MoveTemp(Visited));
		}
	}

	// if (Package.bHasCircularImportDependencies /* || Package.bHasExternalReadDependencies*/)
	{
		for (FPackage* ImportedPackage : Package.ImportedPackages)
		{
			if (!DependentPackages.Contains(ImportedPackage))
			{
				AddPostLoadArc(*ImportedPackage, Package);
			}
		}

		DependentPackages.Remove(&Package);
		for (FPackage* DependentPackage : DependentPackages)
		{
			AddExportsDoneArc(*DependentPackage, Package);
		}
	}

	/*
	if (Package.bHasCircularImportDependencies)
	{
		int32 diff = Package.AllReachablePackages.Num() - 1 - DependentPackages.Num();
		if (DependentPackages.Num() == 0)
		{
			UE_LOG(LogIoStore, Display, TEXT("OPT-ALL: %s: Skipping %d/%d arcs"), *Package.Name.ToString(),
				diff,
				Package.AllReachablePackages.Num() - 1);
		}
		else if (diff > 0)
		{
			UE_LOG(LogIoStore, Display, TEXT("OPT: %s: Skipping %d/%d arcs"), *Package.Name.ToString(),
				diff,
				Package.AllReachablePackages.Num() - 1);
		}
		else
		{
			UE_LOG(LogIoStore, Display, TEXT("NOP: %s: Skipping %d/%d arcs"), *Package.Name.ToString(),
				0,
				Package.AllReachablePackages.Num() - 1);
		}
	}
	*/
}

static int32 AddPostLoadDependencies(TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(PostLoadDependencies);
	UE_LOG(LogIoStore, Display, TEXT("Adding postload dependencies..."));

	TSet<FPackage*> Visited;
	TSet<FCircularImportChain> CircularChains;

	for (FPackage* Package : Packages)
	{
		Visited.Reset();
		Visited.Add(Package);
		AddPackagePostLoadDependencies(*Package, Visited, CircularChains);
	}
	return CircularChains.Num();
};

static void BuildBundles(FExportGraph& ExportGraph, const TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(BuildBundles)
	UE_LOG(LogIoStore, Display, TEXT("Building bundles..."));

	TArray<FExportGraphNode*> ExportLoadOrder = ExportGraph.ComputeLoadOrder(Packages);
	FPackage* LastPackage = nullptr;
	uint32 BundleLoadOrder = 0;
	for (FExportGraphNode* Node : ExportLoadOrder)
	{
		FPackage* Package = Node->Package;
		check(Package);
		if (!Package)
		{
			continue;
		}

		uint32 BundleIndex;
		FExportBundle* Bundle;
		if (Package != LastPackage)
		{
			BundleIndex = Package->ExportBundles.Num();
			Bundle = &Package->ExportBundles.AddDefaulted_GetRef();
			Bundle->LoadOrder = BundleLoadOrder++;
			LastPackage = Package;
		}
		else
		{
			BundleIndex = Package->ExportBundles.Num() - 1;
			Bundle = &Package->ExportBundles[BundleIndex];
		}
		for (FExportGraphNode* ExternalDependency : Node->ExternalDependencies)
		{
			uint32* FindDependentBundleIndex = ExternalDependency->Package->ExportBundleMap.Find(ExternalDependency);
			check(FindDependentBundleIndex);
			if (BundleIndex > 0)
			{
				AddUniqueExternalBundleArc(*ExternalDependency->Package, *FindDependentBundleIndex, *Package, BundleIndex);
			}
		}
		Bundle->Nodes.Add(Node);
		Package->ExportBundleMap.Add(Node, BundleIndex);
	}
}

static void AssignPackagesDiskOrder(
	const TArray<FPackage*>& Packages,
	const TMap<FName, uint64> GameOrderMap,
	const TMap<FName, uint64>& CookerOrderMap)
{
	struct FCluster
	{
		TArray<FPackage*> Packages;
	};

	TArray<FCluster*> Clusters;
	TSet<FPackage*> AssignedPackages;
	TArray<FPackage*> ProcessStack;

	struct FPackageAndOrder
	{
		FPackage* Package;
		uint64 GameOpenOrder;
		uint64 CookerOpenOrder;

		bool operator<(const FPackageAndOrder& Other) const
		{
			if (GameOpenOrder != Other.GameOpenOrder)
			{
				return GameOpenOrder < Other.GameOpenOrder;
			}
			if (CookerOpenOrder != Other.CookerOpenOrder)
			{
				return CookerOpenOrder < Other.CookerOpenOrder;
			}
			// Fallback to reverse bundle order (so that packages are considered before their imports)
			return Package->ExportBundles[0].LoadOrder > Other.Package->ExportBundles[0].LoadOrder;
		}
	};

	TArray<FPackageAndOrder> SortedPackages;
	SortedPackages.Reserve(Packages.Num());
	for (FPackage* Package : Packages)
	{
		FPackageAndOrder& Entry = SortedPackages.AddDefaulted_GetRef();
		Entry.Package = Package;
		const uint64* FindGameOpenOrder = GameOrderMap.Find(Package->Name);
		Entry.GameOpenOrder = FindGameOpenOrder ? *FindGameOpenOrder : MAX_uint64;
		const uint64* FindCookerOpenOrder = CookerOrderMap.Find(Package->Name);
		/*if (!FindCookerOpenOrder)
		{
			UE_LOG(LogIoStore, Warning, TEXT("Missing cooker order for package: %s"), *Package->Name.ToString());
		}*/
		Entry.CookerOpenOrder = FindCookerOpenOrder ? *FindCookerOpenOrder : MAX_uint64;
	}
	bool bHasGameOrder = true;
	bool bHasCookerOrder = true;
	int32 LastAssignedCount = 0;
	Algo::Sort(SortedPackages);
	for (FPackageAndOrder& Entry : SortedPackages)
	{
		if (bHasGameOrder && Entry.GameOpenOrder == MAX_uint64)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using game open order"), AssignedPackages.Num(), Packages.Num());
			LastAssignedCount = AssignedPackages.Num();
			bHasGameOrder = false;
		}
		if (!bHasGameOrder && bHasCookerOrder && Entry.CookerOpenOrder == MAX_uint64)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using cooker open order"), AssignedPackages.Num() - LastAssignedCount, Packages.Num() - LastAssignedCount);
			LastAssignedCount = AssignedPackages.Num();
			bHasCookerOrder = false;
		}
		if (!AssignedPackages.Contains(Entry.Package))
		{
			FCluster* Cluster = new FCluster();
			Clusters.Add(Cluster);
			ProcessStack.Push(Entry.Package);

			while (ProcessStack.Num())
			{
				FPackage* PackageToProcess = ProcessStack.Pop(false);
				if (!AssignedPackages.Contains(PackageToProcess))
				{
					AssignedPackages.Add(PackageToProcess);
					if (PackageToProcess->ExportBundles.Num())
					{
						Cluster->Packages.Add(PackageToProcess);
					}
					for (FPackage* ImportedPackage : PackageToProcess->ImportedPackages)
					{
						ProcessStack.Push(ImportedPackage);
					}
				}
			}
		}
	}
	UE_LOG(LogIoStore, Display, TEXT("Ordered %d packages using fallback bundle order"), AssignedPackages.Num() - LastAssignedCount);

	check(AssignedPackages.Num() == Packages.Num());
	
	for (FCluster* Cluster : Clusters)
	{
		Algo::Sort(Cluster->Packages, [](const FPackage* A, const FPackage* B)
		{
			return A->ExportBundles[0].LoadOrder < B->ExportBundles[0].LoadOrder;
		});
	}

	uint64 LayoutIndex = 0;
	for (FCluster* Cluster : Clusters)
	{
		for (FPackage* Package : Cluster->Packages)
		{
			Package->DiskLayoutOrder = LayoutIndex++;
		}
		delete Cluster;
	}
}

static void CreateDiskLayout(
	const TArray<FContainerTargetSpec*>& ContainerTargets,
	const TArray<FPackage*>& Packages,
	const TMap<FName, uint64> PackageOrderMap,
	const TMap<FName, uint64>& CookerOrderMap,
	uint64 CompressionBlockSize,
	uint64 MemoryMappingAlignment)
{
	IOSTORE_CPU_SCOPE(CreateDiskLayout);

	struct FLayoutEntry
	{
		enum ELayoutEntryType
		{
			Invalid,
			File,
			FreeSpace,
			BlockBoundary,
		};
		FLayoutEntry* Prev = nullptr;
		FLayoutEntry* Next = nullptr;
		int32 BeginBlockIndex = -1;
		int32 EndBlockIndex = -1;
		ELayoutEntryType Type = Invalid;
		uint64 Size = 0;
		uint64 PreviousBuildOffset = 0;
		uint64 IdealOrder = 0;
		FContainerTargetFile* TargetFile = nullptr;
		bool bHasPreviousBuildOffset = false;
		bool bModified = false;
		bool bLocked = false;
	};

	struct FLayoutBlock
	{
		FLayoutEntry* FirstEntry = nullptr;
		FLayoutEntry* LastEntry = nullptr;
		bool bModified = false;
	};

	AssignPackagesDiskOrder(Packages, PackageOrderMap, CookerOrderMap);

	for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		TArray<FLayoutEntry*> Entries;

		FLayoutEntry* EntriesHead = new FLayoutEntry();
		Entries.Add(EntriesHead);

		TMap<int64, FLayoutEntry*> EntriesByOrderMap;
		FLayoutEntry* LastAddedEntry = EntriesHead;

		TMap<FIoChunkHash, TArray<FLayoutEntry*>> PreviousBuildFileByHash;
		TMap<FIoChunkId, FIoChunkHash> PreviousBuildHashByChunkId;
		uint64 CurrentOffset = 0;
		FLayoutEntry* PrevEntryLink = EntriesHead;
		
		if (ContainerTarget->bGenerateDiffPatch)
		{
			for (const TUniquePtr<FIoStoreReader>& PatchSourceReader : ContainerTarget->PatchSourceReaders)
			{
				PatchSourceReader->EnumerateChunks([&PreviousBuildHashByChunkId](const FIoStoreTocChunkInfo& ChunkInfo)
				{
					PreviousBuildHashByChunkId.Add(ChunkInfo.Id, ChunkInfo.Hash);
					return true;
				});
			}
		}
		else
		{
			if (ContainerTarget->PatchSourceReaders.Num())
			{
				ContainerTarget->PatchSourceReaders[0]->EnumerateChunks([&CurrentOffset, &PrevEntryLink, &Entries, &PreviousBuildFileByHash, &PreviousBuildHashByChunkId](const FIoStoreTocChunkInfo& ChunkInfo)
				{
					if (CurrentOffset < ChunkInfo.Offset)
					{
						FLayoutEntry* FreeSpaceEntry = new FLayoutEntry();
						FreeSpaceEntry->Type = FLayoutEntry::FreeSpace;
						FreeSpaceEntry->Size = ChunkInfo.Offset - CurrentOffset;
						FreeSpaceEntry->PreviousBuildOffset = CurrentOffset;
						FreeSpaceEntry->bHasPreviousBuildOffset = true;
						CurrentOffset = ChunkInfo.Offset;
						PrevEntryLink->Next = FreeSpaceEntry;
						FreeSpaceEntry->Prev = PrevEntryLink;
						PrevEntryLink = FreeSpaceEntry;
						Entries.Add(FreeSpaceEntry);
					}
					FLayoutEntry* FileEntry = new FLayoutEntry();
					FileEntry->Type = FLayoutEntry::File;
					FileEntry->Size = ChunkInfo.Size;
					FileEntry->PreviousBuildOffset = ChunkInfo.Offset;
					FileEntry->bHasPreviousBuildOffset = true;

					PrevEntryLink->Next = FileEntry;
					FileEntry->Prev = PrevEntryLink;
					PrevEntryLink = FileEntry;

					CurrentOffset += ChunkInfo.Size;

					Entries.Add(FileEntry);

					TArray<FLayoutEntry*>& PreviousBuildFileHashesArray = PreviousBuildFileByHash.FindOrAdd(ChunkInfo.Hash);
					PreviousBuildFileHashesArray.Push(FileEntry);
					PreviousBuildHashByChunkId.Add(ChunkInfo.Id, ChunkInfo.Hash);

					return true;
				});
			}
		}
		if (CurrentOffset % CompressionBlockSize != 0)
		{
			FLayoutEntry* LastFreeSpace = new FLayoutEntry();
			LastFreeSpace->Type = FLayoutEntry::FreeSpace;
			LastFreeSpace->Size = Align(CurrentOffset, CompressionBlockSize) - CurrentOffset;
			LastFreeSpace->Prev = PrevEntryLink;
			PrevEntryLink->Next = LastFreeSpace;
			PrevEntryLink = LastFreeSpace;
			Entries.Add(LastFreeSpace);
			CurrentOffset += LastFreeSpace->Size;
		}

		FLayoutEntry* EntriesTail = new FLayoutEntry();
		Entries.Add(EntriesTail);
		PrevEntryLink->Next = EntriesTail;
		EntriesTail->Prev = PrevEntryLink;

		Algo::Sort(ContainerTarget->TargetFiles, [](const FContainerTargetFile& A, const FContainerTargetFile& B)
		{
			if (A.bIsMemoryMappedBulkData != B.bIsMemoryMappedBulkData)
			{
				return B.bIsMemoryMappedBulkData;
			}
			if (A.bIsBulkData != B.bIsBulkData)
			{
				return B.bIsBulkData;
			}

			return A.Package->DiskLayoutOrder < B.Package->DiskLayoutOrder;
		});

		uint64 DiffFileCount = 0;
		uint64 DiffFileSize = 0;
		uint64 AddedFileCount = 0;
		uint64 AddedFileSize = 0;
		int64 IdealOrder = 0;
		TArray<FLayoutEntry*> UnassignedEntries;
		for (FContainerTargetFile& ContainerTargetFile : ContainerTarget->TargetFiles)
		{
			bool bIsAddedOrModified = false;
			const FIoChunkHash* FindPreviousHashByChunkId = PreviousBuildHashByChunkId.Find(ContainerTargetFile.ChunkId);
			if (FindPreviousHashByChunkId)
			{
				if (*FindPreviousHashByChunkId != ContainerTargetFile.ChunkHash)
				{
					//UE_LOG(LogIoStore, Display, TEXT("Diffing: %s"), *ContainerTargetFile.TargetPath);
					++DiffFileCount;
					DiffFileSize += ContainerTargetFile.TargetSize;
					bIsAddedOrModified = true;
				}
			}
			else
			{
				//UE_LOG(LogIoStore, Display, TEXT("Added: %s"), *ContainerTargetFile.TargetPath);
				++AddedFileCount;
				AddedFileSize += ContainerTargetFile.TargetSize;
				bIsAddedOrModified = true;
			}

			bool bAddToUnassignedPool = true;
			if (ContainerTarget->bGenerateDiffPatch)
			{
				if (!bIsAddedOrModified)
				{
					bAddToUnassignedPool = false;
				}
			}
			else
			{
				FLayoutEntry* FindPreviousFileEntry = nullptr;
				TArray<FLayoutEntry*>* FindPreviousBuildFileHashesArray = PreviousBuildFileByHash.Find(ContainerTargetFile.ChunkHash);
				if (FindPreviousBuildFileHashesArray && FindPreviousBuildFileHashesArray->Num())
				{
					FindPreviousFileEntry = FindPreviousBuildFileHashesArray->Pop(false);
				}

				if (FindPreviousFileEntry && !FindPreviousFileEntry->TargetFile && ContainerTargetFile.TargetSize == FindPreviousFileEntry->Size)
				{
					FindPreviousFileEntry->TargetFile = &ContainerTargetFile;
					FindPreviousFileEntry->IdealOrder = IdealOrder;
					EntriesByOrderMap.Add(IdealOrder, FindPreviousFileEntry);
					bAddToUnassignedPool = false;
				}
			}
			if (bAddToUnassignedPool)
			{
				FLayoutEntry* NewEntry = new FLayoutEntry();
				NewEntry->Type = FLayoutEntry::File;
				NewEntry->Size = ContainerTargetFile.TargetSize;
				NewEntry->TargetFile = &ContainerTargetFile;
				NewEntry->IdealOrder = IdealOrder;
				NewEntry->bModified = true;
				Entries.Add(NewEntry);
				UnassignedEntries.Add(NewEntry);
			}
			++IdealOrder;
		}
		UE_LOG(LogIoStore, Display, TEXT("%s: %d/%d modified entries (%fMB)"), *ContainerTarget->Name.ToString(), DiffFileCount, ContainerTarget->TargetFiles.Num(), DiffFileSize / 1024.0 / 1024.0);
		UE_LOG(LogIoStore, Display, TEXT("%s: %d/%d added entries (%fMB)"), *ContainerTarget->Name.ToString(), AddedFileCount, ContainerTarget->TargetFiles.Num(), AddedFileSize / 1024.0 / 1024.0);

		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			if (EntryIt->Type == FLayoutEntry::File && !EntryIt->TargetFile)
			{
				EntryIt->Type = FLayoutEntry::FreeSpace;
				EntryIt->bModified = true;
			}
		}

		// Assign entries to blocks
		TArray<FLayoutBlock> Blocks;
		Blocks.SetNum(CurrentOffset / CompressionBlockSize);
		CurrentOffset = 0;
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			EntryIt->BeginBlockIndex = int32(CurrentOffset / CompressionBlockSize);
			EntryIt->EndBlockIndex = int32(Align(CurrentOffset + EntryIt->Size, CompressionBlockSize) / CompressionBlockSize);
			CurrentOffset += EntryIt->Size;
			for (int32 BlockIndex = EntryIt->BeginBlockIndex; BlockIndex < EntryIt->EndBlockIndex; ++BlockIndex)
			{
				FLayoutBlock& Block = Blocks[BlockIndex];
				Block.bModified |= EntryIt->bModified;
				if (!Block.FirstEntry)
				{
					Block.FirstEntry = EntryIt;
				}
				Block.LastEntry = EntryIt;
			}
		}
		// Put all file entries that only touch already modified blocks back to the unassigned pool
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			if (EntryIt->Type == FLayoutEntry::File)
			{
				bool bAllBlocksTouchedByEntryModified = true;
				for (int32 BlockIndex = EntryIt->BeginBlockIndex; BlockIndex < EntryIt->EndBlockIndex; ++BlockIndex)
				{
					FLayoutBlock& Block = Blocks[BlockIndex];
					if (!Block.bModified)
					{
						bAllBlocksTouchedByEntryModified = false;
						break;
					}
				}
				if (bAllBlocksTouchedByEntryModified)
				{
					FLayoutEntry* ReleasedEntry = new FLayoutEntry();
					ReleasedEntry->Type = FLayoutEntry::File;
					ReleasedEntry->Size = EntryIt->Size;
					ReleasedEntry->TargetFile = EntryIt->TargetFile;
					ReleasedEntry->IdealOrder = EntryIt->IdealOrder;
					ReleasedEntry->bModified = true;
					Entries.Add(ReleasedEntry);
					UnassignedEntries.Add(ReleasedEntry);

					EntryIt->Type = FLayoutEntry::FreeSpace;
					EntryIt->TargetFile = nullptr;
					EntryIt->bModified = true;
				}
			}
		}
		uint64 UnmodifiedBlocksCount = 0;
		for (const FLayoutBlock& Block : Blocks)
		{
			if (!Block.bModified)
			{
				++UnmodifiedBlocksCount;
			}
		}

		// Split all free space entries so that they don't cross any block boundaries
		CurrentOffset = 0;
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			uint64 NextOffset = CurrentOffset + EntryIt->Size;
			if (EntryIt->Type == FLayoutEntry::FreeSpace)
			{
				if (EntryIt->BeginBlockIndex != EntryIt->EndBlockIndex - 1)
				{
					uint64 SizeInFirstBlock = Align(CurrentOffset, CompressionBlockSize) - CurrentOffset;
					if (SizeInFirstBlock)
					{
						FLayoutEntry* FreeSpaceEntry = new FLayoutEntry();
						FreeSpaceEntry->Type = FLayoutEntry::FreeSpace;
						FreeSpaceEntry->Size = SizeInFirstBlock;
						FreeSpaceEntry->bModified = EntryIt->bModified;
						EntryIt->Size -= SizeInFirstBlock;
						FreeSpaceEntry->Prev = EntryIt->Prev;
						FreeSpaceEntry->Next = EntryIt;
						EntryIt->Prev->Next = FreeSpaceEntry;
						EntryIt->Prev = FreeSpaceEntry;
						Entries.Add(FreeSpaceEntry);
					}
					uint64 SizeInLastBlock = EntryIt->Size % CompressionBlockSize;
					if (SizeInLastBlock != EntryIt->Size)
					{
						uint64 SizeInMiddleBlocks = EntryIt->Size - SizeInLastBlock;
						FLayoutEntry* FreeSpaceEntry = new FLayoutEntry();
						FreeSpaceEntry->Type = FLayoutEntry::FreeSpace;
						FreeSpaceEntry->Size = SizeInMiddleBlocks;
						FreeSpaceEntry->bModified = EntryIt->bModified;
						EntryIt->Size -= SizeInMiddleBlocks;
						FreeSpaceEntry->Prev = EntryIt->Prev;
						FreeSpaceEntry->Next = EntryIt;
						EntryIt->Prev->Next = FreeSpaceEntry;
						EntryIt->Prev = FreeSpaceEntry;
						Entries.Add(FreeSpaceEntry);
					}
				}
			}
			CurrentOffset = NextOffset;
		}
		// Update entry block assignment
		// Lock all file entries
		CurrentOffset = 0;
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			EntryIt->BeginBlockIndex = int32(CurrentOffset / CompressionBlockSize);
			EntryIt->EndBlockIndex = int32(Align(CurrentOffset + EntryIt->Size, CompressionBlockSize) / CompressionBlockSize);
			CurrentOffset += EntryIt->Size;
			for (int32 BlockIndex = EntryIt->BeginBlockIndex; BlockIndex < EntryIt->EndBlockIndex; ++BlockIndex)
			{
				FLayoutBlock& Block = Blocks[BlockIndex];
				check(!EntryIt->bModified || Block.bModified);
				if (!Block.FirstEntry)
				{
					Block.FirstEntry = EntryIt;
				}
				Block.LastEntry = EntryIt;
			}
			if (EntryIt->Type == FLayoutEntry::File)
			{
				check(!EntryIt->bModified);
				EntryIt->bLocked = true;
			}
		}
		// Lock all free space entries in unmodified blocks
		for (FLayoutBlock& Block : Blocks)
		{
			if (!Block.bModified)
			{
				for (FLayoutEntry* EntryIt = Block.FirstEntry; EntryIt != Block.LastEntry->Next; EntryIt = EntryIt->Next)
				{
					if (EntryIt->Type == FLayoutEntry::FreeSpace)
					{
						EntryIt->bLocked = true;
					}
				}
			}
		}
		// Merge and shrink all unlocked free space
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			if (EntryIt->Type == FLayoutEntry::FreeSpace && !EntryIt->bLocked)
			{
				if (EntryIt->Prev->Type == FLayoutEntry::FreeSpace && !EntryIt->Prev->bLocked)
				{
					FLayoutEntry* MergeWithFreeSpace = EntryIt->Prev;
					EntryIt->Size += MergeWithFreeSpace->Size;
					MergeWithFreeSpace->Prev->Next = EntryIt;
					EntryIt->Prev = MergeWithFreeSpace->Prev;
				}
				EntryIt->Size %= CompressionBlockSize;
			}
		}
		// Insert block boundaries
		CurrentOffset = 0;
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			if (CurrentOffset % CompressionBlockSize == 0)
			{
				FLayoutEntry* BlockBoundaryEntry = new FLayoutEntry();
				BlockBoundaryEntry->Type = FLayoutEntry::BlockBoundary;
				BlockBoundaryEntry->Prev = EntryIt->Prev;
				BlockBoundaryEntry->Next = EntryIt;
				EntryIt->Prev->Next = BlockBoundaryEntry;
				EntryIt->Prev = BlockBoundaryEntry;
				Entries.Add(BlockBoundaryEntry);
			}
			
			CurrentOffset += EntryIt->Size;
		}
		check(CurrentOffset % CompressionBlockSize == 0);
		FLayoutEntry* LastBlockBoundaryEntry = new FLayoutEntry();
		LastBlockBoundaryEntry->Type = FLayoutEntry::BlockBoundary;
		LastBlockBoundaryEntry->Prev = EntriesTail->Prev;
		LastBlockBoundaryEntry->Next = EntriesTail;
		EntriesTail->Prev->Next = LastBlockBoundaryEntry;
		EntriesTail->Prev = LastBlockBoundaryEntry;
		Entries.Add(LastBlockBoundaryEntry);

		FLayoutEntry* MemoryMappedFilesTarget = new FLayoutEntry();
		MemoryMappedFilesTarget->Type = FLayoutEntry::BlockBoundary;
		MemoryMappedFilesTarget->Prev = LastBlockBoundaryEntry;
		LastBlockBoundaryEntry->Next = MemoryMappedFilesTarget;
		EntriesTail->Prev = MemoryMappedFilesTarget;
		MemoryMappedFilesTarget->Next = EntriesTail;
		Entries.Add(MemoryMappedFilesTarget);

		// Insert new/modified entries according to their ideal order
		Algo::Sort(UnassignedEntries, [](const FLayoutEntry* A, const FLayoutEntry* B)
		{
			return A->IdealOrder < B->IdealOrder;
		});
		for (FLayoutEntry* UnassignedEntry : UnassignedEntries)
		{
			check(UnassignedEntry->TargetFile);
			if (UnassignedEntry->TargetFile->bIsMemoryMappedBulkData)
			{
				UnassignedEntry->Prev = MemoryMappedFilesTarget->Prev;
				UnassignedEntry->Next = MemoryMappedFilesTarget;
				MemoryMappedFilesTarget->Prev->Next = UnassignedEntry;
				MemoryMappedFilesTarget->Prev = UnassignedEntry;

				uint64 AlignedSize = Align(UnassignedEntry->Size, MemoryMappingAlignment);
				uint64 MemoryMapPadding = AlignedSize - UnassignedEntry->Size;
				if (MemoryMapPadding)
				{
					FLayoutEntry* PaddingEntry = new FLayoutEntry();
					PaddingEntry->Type = FLayoutEntry::FreeSpace;
					PaddingEntry->Size = MemoryMapPadding;

					PaddingEntry->Prev = UnassignedEntry;
					PaddingEntry->Next = UnassignedEntry->Next;
					UnassignedEntry->Next->Prev = PaddingEntry;
					UnassignedEntry->Next = PaddingEntry;
					Entries.Add(PaddingEntry);
				}
			}
			else
			{
				FLayoutEntry* PutAfterEntry = EntriesByOrderMap.FindRef(UnassignedEntry->IdealOrder - 1);
				if (!PutAfterEntry)
				{
					PutAfterEntry = LastAddedEntry;
				}
				
				FLayoutEntry* TargetFreeSpace = nullptr;
				FLayoutEntry* CandidateTarget = PutAfterEntry->Next;
				if (CandidateTarget->Type == FLayoutEntry::FreeSpace && !CandidateTarget->bLocked)
				{
					TargetFreeSpace = CandidateTarget;
					if (TargetFreeSpace->Size < UnassignedEntry->Size)
					{
						uint64 SizeExtension = Align(UnassignedEntry->Size - TargetFreeSpace->Size, CompressionBlockSize);
						TargetFreeSpace->Size += SizeExtension;
					}
				}

				if (!TargetFreeSpace)
				{
					FLayoutEntry* NextBlockBoundary = PutAfterEntry->Next;
					check(NextBlockBoundary);
					while (NextBlockBoundary->Type != FLayoutEntry::BlockBoundary)
					{
						NextBlockBoundary = NextBlockBoundary->Next;
					}
					FLayoutEntry* NewFreeSpaceEntry = new FLayoutEntry();
					NewFreeSpaceEntry->Type = FLayoutEntry::FreeSpace;
					NewFreeSpaceEntry->Size = Align(UnassignedEntry->Size, CompressionBlockSize);
					Entries.Add(NewFreeSpaceEntry);
					FLayoutEntry* NewBlockBoundaryEntry = new FLayoutEntry();
					NewBlockBoundaryEntry->Type = FLayoutEntry::BlockBoundary;
					Entries.Add(NewBlockBoundaryEntry);

					NewFreeSpaceEntry->Prev = NextBlockBoundary;
					NewFreeSpaceEntry->Next = NewBlockBoundaryEntry;
					NewBlockBoundaryEntry->Prev = NewFreeSpaceEntry;
					NewBlockBoundaryEntry->Next = NextBlockBoundary->Next;
					NextBlockBoundary->Next->Prev = NewBlockBoundaryEntry;
					NextBlockBoundary->Next = NewFreeSpaceEntry;
					TargetFreeSpace = NewFreeSpaceEntry;
				}

				check(TargetFreeSpace->Type == FLayoutEntry::FreeSpace);
				check(TargetFreeSpace->Size >= UnassignedEntry->Size);
				check(!TargetFreeSpace->bLocked);
				UnassignedEntry->Prev = TargetFreeSpace->Prev;
				UnassignedEntry->Next = TargetFreeSpace;
				TargetFreeSpace->Prev->Next = UnassignedEntry;
				TargetFreeSpace->Prev = UnassignedEntry;
				TargetFreeSpace->Size -= UnassignedEntry->Size;

				EntriesByOrderMap.Add(UnassignedEntry->IdealOrder, UnassignedEntry);
				LastAddedEntry = UnassignedEntry;
			}
		}
		TArray<FContainerTargetFile> IncludedContainerTargetFiles;
		CurrentOffset = 0;
		uint64 PaddingBytes = 0;
		uint64 TotalChunkPaddingSize = 0;
		for (FLayoutEntry* EntryIt = EntriesHead->Next; EntryIt != EntriesTail; EntryIt = EntryIt->Next)
		{
			if (EntryIt->Type == FLayoutEntry::FreeSpace)
			{
				PaddingBytes += EntryIt->Size;
			}
			else if (EntryIt->Type == FLayoutEntry::File)
			{
				check(EntryIt->TargetFile);
				EntryIt->TargetFile->Padding = PaddingBytes;
				TotalChunkPaddingSize += PaddingBytes;
				PaddingBytes = 0;
				EntryIt->TargetFile->Offset = CurrentOffset;

				if (EntryIt->bHasPreviousBuildOffset && EntryIt->bLocked)
				{
					check(EntryIt->PreviousBuildOffset % CompressionBlockSize == EntryIt->TargetFile->Offset % CompressionBlockSize);
				}
				if (EntryIt->TargetFile->bIsMemoryMappedBulkData)
				{
					check(IsAligned(CurrentOffset, MemoryMappingAlignment));
				}
				IncludedContainerTargetFiles.Add(*EntryIt->TargetFile);
			}
			CurrentOffset += EntryIt->Size;
		}
		uint64 TotalBlockCount = Align(CurrentOffset, CompressionBlockSize) / CompressionBlockSize;
		uint64 ModifiedBlocksCount = TotalBlockCount - UnmodifiedBlocksCount;
		UE_LOG(LogIoStore, Display, TEXT("%s: %d/%d modified blocks (%fMB)"), *ContainerTarget->Name.ToString(), ModifiedBlocksCount, TotalBlockCount, (ModifiedBlocksCount * CompressionBlockSize) / 1024.0 / 1024.0);
		UE_LOG(LogIoStore, Display, TEXT("%s: Total chunk padding %fMB"), *ContainerTarget->Name.ToString(), TotalChunkPaddingSize / 1024.0 / 1024.0);

		for (FLayoutEntry* Entry : Entries)
		{
			delete Entry;
		}

		Swap(ContainerTarget->TargetFiles, IncludedContainerTargetFiles);
	}
}

static EIoChunkType BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType Type)
{
	switch (Type)
	{
	case FPackageStoreBulkDataManifest::EBulkdataType::Normal:
		return EIoChunkType::BulkData;
	case FPackageStoreBulkDataManifest::EBulkdataType::Optional:
		return EIoChunkType::OptionalBulkData;
	case FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped:
		return EIoChunkType::MemoryMappedBulkData;
	default:
		UE_LOG(LogIoStore, Error, TEXT("Invalid EBulkdataType (%d) found!"), Type);
		return EIoChunkType::Invalid;
	}
}

static bool MapAdditionalBulkDataChunks(FContainerTargetFile& TargetFile, const FPackageStoreBulkDataManifest& BulkDataManifest)
{
	const FPackage& Package = *TargetFile.Package;
	const FPackageStoreBulkDataManifest::FPackageDesc* PackageDesc = BulkDataManifest.Find(Package.FileName);
	if (PackageDesc != nullptr)
	{
		FPackageStoreBulkDataManifest::EBulkdataType BulkDataType = TargetFile.bIsOptionalBulkData
			? FPackageStoreBulkDataManifest::EBulkdataType::Optional
			: TargetFile.bIsMemoryMappedBulkData
				? FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped
				: FPackageStoreBulkDataManifest::EBulkdataType::Normal;

		const EIoChunkType ChunkIdType = BulkdataTypeToChunkIdType(BulkDataType);

		// Create additional mapping chunks as needed
		for (const FPackageStoreBulkDataManifest::FPackageDesc::FBulkDataDesc& BulkDataDesc : PackageDesc->GetDataArray())
		{
			if (BulkDataDesc.Type == BulkDataType)
			{
				FContainerTargetFilePartialMapping& PartialMapping = TargetFile.PartialMappings.AddDefaulted_GetRef();
				PartialMapping.PartialChunkId = CreateChunkIdForBulkData(Package.GlobalPackageId, BulkDataDesc.ChunkId, ChunkIdType, *Package.FileName);
				PartialMapping.Offset = BulkDataDesc.Offset;
				PartialMapping.Length = BulkDataDesc.Size;
			}
		}
	}
	else
	{
		UE_LOG(LogIoStore, Warning, TEXT("Unable to find an entry in the bulkdata manifest for '%s' the file might be out of date!"), *Package.FileName);
		return false;
	}

	return true;
}

struct FScriptImportData
{
	FName ObjectName;
	FString FullName;
	FPackageObjectIndex GlobalIndex;
	FPackageObjectIndex OuterIndex;
	FPackageObjectIndex OutermostIndex;
	FPackageObjectIndex CDOClassIndex;
	FString CDOClassFullName;
	bool bInitialized = false;
};

struct FPackageImportData
{
	FName ObjectName;
	FString FullName;
	FPackageObjectIndex GlobalIndex;
	int32 GlobalExportIndex = -1;
	FPackage* Package = nullptr;
	bool bIsLocalized = false;
	bool bIsMissingImport = false;
	bool bInitialized = false;
};

struct FExportData
{
	int32 GlobalIndex = -1;
	FName ObjectName;
	int32 SourceIndex = -1;
	FPackageObjectIndex GlobalImportIndex;
	FPackageObjectIndex OuterIndex;
	FPackageObjectIndex ClassIndex;
	FPackageObjectIndex SuperIndex;
	FPackageObjectIndex TemplateIndex;
	FString FullName;

	FPackage* Package = nullptr;
	FExportGraphNode* CreateNode = nullptr;
	FExportGraphNode* SerializeNode = nullptr;
};

using FImportObjectsByFullName = TMap<FString, FPackageObjectIndex>;

template <typename T>
struct FGlobalObjects
{
	TArray<T> Objects;
	TMap<FString, int32> ObjectsByFullName;
};

using FGlobalScriptImports = TArray<FScriptImportData>;
using FGlobalPackageImports = TArray<FPackageImportData>;
using FGlobalExports = FGlobalObjects<FExportData>;

struct FGlobalImports
{
	FGlobalScriptImports ScriptImports;
	FGlobalPackageImports PackageImports;
	FImportObjectsByFullName ObjectsByFullName;
};

struct FGlobalPackageData
{
	FGlobalImports Imports;
	FGlobalExports Exports;
};

static void FindImport(
	FGlobalImports& GlobalImports,
	TArray<FString>& TempFullNames,
	FObjectImport* ImportMap,
	const int32 LocalImportIndex,
	const FPackageMap& PackageMap,
	FPackage*& CurrentPackage)
{
	FObjectImport* Import = &ImportMap[LocalImportIndex];
	FString& FullName = TempFullNames[LocalImportIndex];

	if (Import->OuterIndex.IsNull())
	{
		CurrentPackage = PackageMap.FindRef(Import->ObjectName);
	}

	if (FullName.Len() == 0)
	{
		if (Import->OuterIndex.IsNull())
		{
			Import->ObjectName.AppendString(FullName);

			const bool bIsScript = FullName.StartsWith(TEXT("/Script/"));
			if (bIsScript)
			{
				FScriptImportData* ScriptImport;
				FPackageObjectIndex FindGlobalImportIndex = GlobalImports.ObjectsByFullName.FindRef(FullName);
				if (FindGlobalImportIndex.IsNull())
				{
					// assign global index for this script UPackage
					FPackageObjectIndex::Type IndexType = FPackageObjectIndex::ScriptImport;
					TArray<FScriptImportData>& ScriptImports = GlobalImports.ScriptImports;
					FPackageObjectIndex GlobalImportIndex(IndexType, ScriptImports.Num());
					GlobalImports.ObjectsByFullName.Add(FullName, GlobalImportIndex);

					ScriptImport = &ScriptImports.AddDefaulted_GetRef();
					ScriptImport->GlobalIndex = GlobalImportIndex;
					ScriptImport->FullName = FullName;
				}
				else
				{
					ScriptImport = &GlobalImports.ScriptImports[FindGlobalImportIndex.GetIndex()];
				}
				if (!ScriptImport->bInitialized)
				{
					ScriptImport->OuterIndex = FPackageObjectIndex();
					ScriptImport->ObjectName = Import->ObjectName;
					ScriptImport->bInitialized = true;
				}
			}
		}
		else
		{
			const int32 LocalOuterIndex = Import->OuterIndex.ToImport();
			FindImport(GlobalImports, TempFullNames, ImportMap, LocalOuterIndex, PackageMap, CurrentPackage);
			const FString& OuterName = TempFullNames[LocalOuterIndex];
			check(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Import->ObjectName.AppendString(FullName);

			const bool bIsScript = FullName.StartsWith(TEXT("/Script/"));
			FPackageObjectIndex FindGlobalImportIndex = GlobalImports.ObjectsByFullName.FindRef(FullName);
			FPackageObjectIndex FindOuterGlobalImport = GlobalImports.ObjectsByFullName.FindRef(OuterName);
			if (bIsScript)
			{
				check(FindOuterGlobalImport.IsScriptImport());

				FScriptImportData* ScriptImport;
				if (FindGlobalImportIndex.IsNull())
				{
					// assign global index for this script UObject
					TArray<FScriptImportData>& ScriptImports = GlobalImports.ScriptImports;
					FPackageObjectIndex GlobalImportIndex(FPackageObjectIndex::ScriptImport, ScriptImports.Num());
					GlobalImports.ObjectsByFullName.Add(FullName, GlobalImportIndex);

					ScriptImport = &GlobalImports.ScriptImports.AddDefaulted_GetRef();
					ScriptImport->GlobalIndex = GlobalImportIndex;
					ScriptImport->FullName = FullName;
				}
				else
				{
					ScriptImport = &GlobalImports.ScriptImports[FindGlobalImportIndex.GetIndex()];
				}
				if (!ScriptImport->bInitialized)
				{
					ScriptImport->OuterIndex = FindOuterGlobalImport;
					ScriptImport->ObjectName = Import->ObjectName;

					const FScriptImportData& OuterScriptImport = GlobalImports.ScriptImports[FindOuterGlobalImport.GetIndex()];
					if (OuterScriptImport.CDOClassFullName.Len() > 0)
					{
						ScriptImport->CDOClassFullName = OuterScriptImport.CDOClassFullName;
					}
					else
					{
						TCHAR NameBuffer[FName::StringBufferSize];
						uint32 Len = Import->ObjectName.ToString(NameBuffer);
						if (FCString::Strncmp(NameBuffer, TEXT("Default__"), 9) == 0)
						{
							ScriptImport->CDOClassFullName.Append(OuterName);
							ScriptImport->CDOClassFullName.AppendChar(TEXT('/'));
							ScriptImport->CDOClassFullName.AppendChars(NameBuffer + 9, Len - 9);
						}
					}
					ScriptImport->bInitialized = true;
				}
			}
			else
			{
				FPackageImportData* PackageImport;
				if (FindGlobalImportIndex.IsNull())
				{
					TArray<FPackageImportData>& PackageImports = GlobalImports.PackageImports;
					FPackageObjectIndex GlobalImportIndex(FPackageObjectIndex::PackageImport, PackageImports.Num());
					GlobalImports.ObjectsByFullName.Add(FullName, GlobalImportIndex);

					PackageImport = &GlobalImports.PackageImports.AddDefaulted_GetRef();
					PackageImport->GlobalIndex = GlobalImportIndex;
					PackageImport->FullName = FullName;
				}
				else
				{
					PackageImport = &GlobalImports.PackageImports[FindGlobalImportIndex.GetIndex()];
				}
				if (!PackageImport->bInitialized)
				{
					PackageImport->ObjectName = Import->ObjectName;
					PackageImport->bIsLocalized = FullName.Contains(L10NPrefix);

					if (FindOuterGlobalImport.IsPackageImport())
					{
						const FPackageImportData& OuterPackageImport = GlobalImports.PackageImports[FindOuterGlobalImport.GetIndex()];
						PackageImport->Package = OuterPackageImport.Package;
					}
					else
					{
						PackageImport->Package = CurrentPackage;
					}
					PackageImport->bInitialized = true;
				}
			}
		}
	}
}

static void FindExport(
	FGlobalExports& GlobalExports,
	TArray<FString>& TempFullNames,
	const FObjectExport* ExportMap,
	const int32 LocalExportIndex,
	FPackage* Package)
{
	const FObjectExport* Export = ExportMap + LocalExportIndex;
	FString& FullName = TempFullNames[LocalExportIndex];

	if (FullName.Len() == 0)
	{
		if (Export->OuterIndex.IsNull())
		{
			Package->Name.AppendString(FullName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
		}
		else
		{
			check(Export->OuterIndex.IsExport());

			FindExport(GlobalExports, TempFullNames, ExportMap, Export->OuterIndex.ToExport(), Package);
			FString& OuterName = TempFullNames[Export->OuterIndex.ToExport()];
			check(OuterName.Len() > 0);
			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
		}
		check(!GlobalExports.ObjectsByFullName.Contains(FullName));
		const int32 GlobalExportIndex = GlobalExports.Objects.Num();
		GlobalExports.ObjectsByFullName.Add(FullName, GlobalExportIndex);
		FExportData& ExportData = GlobalExports.Objects.AddDefaulted_GetRef();
		ExportData.GlobalIndex = GlobalExportIndex;
		ExportData.Package = Package;
		ExportData.ObjectName = Export->ObjectName;
		ExportData.SourceIndex = LocalExportIndex;
		ExportData.FullName = FullName;
	}
}

FContainerTargetSpec* AddContainer(
	FName Name,
	FIoContainerId Id,
	TArray<FContainerTargetSpec*>& Containers,
	TMap<FName, FContainerTargetSpec*>& ContainerMap)
{
	for (const FContainerTargetSpec* ExistingContainer : Containers)
	{
		check(ExistingContainer->Header.ContainerId != Id);
	}
	check(!ContainerMap.Contains(Name));
	FContainerTargetSpec* ContainerTargetSpec = new FContainerTargetSpec();
	ContainerTargetSpec->Name = Name;
	ContainerTargetSpec->Header.ContainerId = Id;
	ContainerMap.Add(Name, ContainerTargetSpec);
	Containers.Add(ContainerTargetSpec);
	return ContainerTargetSpec;
}

FContainerTargetSpec* FindOrAddContainer(
	FName Name,
	TArray<FContainerTargetSpec*>& Containers,
	TMap<FName, FContainerTargetSpec*>& ContainerMap)
{
	FContainerTargetSpec* ContainerTargetSpec = ContainerMap.FindRef(Name);
	if (!ContainerTargetSpec)
	{
		uint16 NextContainerTargetId = 1;
		for (const FContainerTargetSpec* ExistingContainer : Containers)
		{
			check(ExistingContainer->Header.ContainerId.IsValid());
			NextContainerTargetId = FMath::Max<uint16>(ExistingContainer->Header.ContainerId.ToIndex() + 1, NextContainerTargetId);
		}
		ContainerTargetSpec = new FContainerTargetSpec();
		ContainerTargetSpec->Name = Name;
		ContainerTargetSpec->Header.ContainerId = FIoContainerId::FromIndex(NextContainerTargetId);
		ContainerMap.Add(Name, ContainerTargetSpec);
		Containers.Add(ContainerTargetSpec);
	}
	return ContainerTargetSpec;
}

FPackage* FindOrAddPackage(
	const TCHAR* RelativeFileName,
	TArray<FPackage*>& Packages,
	FPackageMap& PackageMap,
	FPackageGlobalIdMap& PackageGlobalIdMap)
{
	FString PackageName;
	FString ErrorMessage;
	if (!FPackageName::TryConvertFilenameToLongPackageName(RelativeFileName, PackageName, &ErrorMessage))
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed to convert file name from file name '%s'"), *ErrorMessage);
		return nullptr;
	}

	FName PackageFName = *PackageName;

	FPackage* Package = PackageMap.FindRef(PackageFName);
	if (!Package)
	{
		Package = new FPackage();
		Package->Name = PackageFName;
		Package->SourcePackageName = *RemapLocalizationPathIfNeeded(PackageName, &Package->Region);
		const FPackageId* FindPackageGlobalId = PackageGlobalIdMap.Find(PackageFName);
		if (FindPackageGlobalId)
		{
			Package->GlobalPackageId = *FindPackageGlobalId;
		}
		else
		{
			Package->GlobalPackageId = FPackageId::FromIndex(PackageGlobalIdMap.Num());
			PackageGlobalIdMap.Add(PackageFName, Package->GlobalPackageId);
		}
		Packages.Add(Package);
		PackageMap.Add(PackageFName, Package);
	}

	return Package;
}

static bool ConformLocalizedPackage(
	const FPackageMap& PackageMap,
	const FGlobalImports& GlobalImports,
	const FPackage& SourcePackage,
	FPackage& LocalizedPackage,
	TArray<FExportData>& GlobalExports,
	FLocalizedToSourceImportIndexMap& LocalizedToSourceImportIndexMap)
{
	const int32 ExportCount =
		SourcePackage.ExportCount < LocalizedPackage.ExportCount ?
		SourcePackage.ExportCount :
		LocalizedPackage.ExportCount;

	UE_CLOG(SourcePackage.ExportCount != LocalizedPackage.ExportCount, LogIoStore, Verbose,
		TEXT("For culture '%s': Localized package '%s' (%d) for source package '%s' (%d)  - Has ExportCount %d vs. %d"),
			*LocalizedPackage.Region,
			*LocalizedPackage.Name.ToString(),
			LocalizedPackage.GlobalPackageId.ToIndexForDebugging(),
			*LocalizedPackage.SourcePackageName.ToString(),
			SourcePackage.GlobalPackageId.ToIndexForDebugging(),
			LocalizedPackage.ExportCount,
			SourcePackage.ExportCount);

	auto GetExportNameSafe = [](
		const FString& ExportFullName,
		const FName& PackageName,
		int32 PackageNameLen) -> const TCHAR*
	{
		const bool bValidNameLen = ExportFullName.Len() > PackageNameLen + 1;
		if (bValidNameLen)
		{
			const TCHAR* ExportNameStr = *ExportFullName + PackageNameLen;
			const bool bValidNameFormat = *ExportNameStr == '/';
			if (bValidNameFormat)
			{
				return ExportNameStr + 1; // skip verified '/'
			}
			else
			{
				UE_CLOG(!bValidNameFormat, LogIoStore, Warning,
					TEXT("Export name '%s' should start with '/' at position %d, i.e. right after package prefix '%s'"),
					*ExportFullName,
					PackageNameLen,
					*PackageName.ToString());
			}
		}
		else
		{
			UE_CLOG(!bValidNameLen, LogIoStore, Warning,
				TEXT("Export name '%s' with length %d should be longer than package name '%s' with length %d"),
				*ExportFullName,
				PackageNameLen,
				*PackageName.ToString());
		}

		return nullptr;
	};

	auto AppendMismatchMessage = [&GlobalImports, &GlobalExports, &LocalizedPackage, &SourcePackage](
		const TCHAR* Text, FName ExportName, FPackageObjectIndex LocIndex, FPackageObjectIndex SrcIndex, FString& FailReason)
	{
		FString LocString = 
			LocIndex.IsPackageImport() ?
			GlobalImports.PackageImports[LocIndex.GetIndex()].ObjectName.ToString() :
			LocIndex.IsScriptImport() ?
			GlobalImports.ScriptImports[LocIndex.GetIndex()].ObjectName.ToString() :
			LocIndex.IsExport() ?
			GlobalExports[LocalizedPackage.Exports[LocIndex.GetIndex()]].ObjectName.ToString() :
			TEXT("");
		FString SrcString = 
			SrcIndex.IsPackageImport() ?
			GlobalImports.PackageImports[SrcIndex.GetIndex()].ObjectName.ToString() :
			SrcIndex.IsScriptImport() ?
			GlobalImports.ScriptImports[SrcIndex.GetIndex()].ObjectName.ToString() :
			SrcIndex.IsExport() ?
			GlobalExports[LocalizedPackage.Exports[SrcIndex.GetIndex()]].ObjectName.ToString() :
			TEXT("");

		FailReason.Appendf(TEXT("Public export '%s' has %s %s (%d) vs. %s (%d)"),
			*ExportName.ToString(),
			Text,
			*LocString,
			LocIndex.GetIndex(),
			*SrcString,
			SrcIndex.GetIndex());
	};

	const int32 LocalizedPackageNameLen = LocalizedPackage.Name.GetStringLength();
	const int32 SourcePackageNameLen = SourcePackage.Name.GetStringLength();

	TArray <TPair<int32, int32>, TInlineAllocator<64>> NewPublicExports;
	NewPublicExports.Reserve(ExportCount);

	bool bSuccess = true;
	int32 LocalizedIndex = 0;
	int32 SourceIndex = 0;
	while (LocalizedIndex < ExportCount && SourceIndex < ExportCount)
	{
		FString FailReason;
		const FExportData& LocalizedExportData = GlobalExports[LocalizedPackage.Exports[LocalizedIndex]];
		const FExportData& SourceExportData = GlobalExports[SourcePackage.Exports[SourceIndex]];

		const TCHAR* LocalizedExportStr = GetExportNameSafe(
			LocalizedExportData.FullName, LocalizedPackage.Name, LocalizedPackageNameLen);
		const TCHAR* SourceExportStr = GetExportNameSafe(
			SourceExportData.FullName, SourcePackage.Name, SourcePackageNameLen);

		if (!LocalizedExportStr || !SourceExportStr)
		{
			UE_LOG(LogIoStore, Error,
				TEXT("Culture '%s': Localized package '%s' (%d) for source package '%s' (%d) - Has some bad data from an earlier phase."),
				*LocalizedPackage.Region,
				*LocalizedPackage.Name.ToString(),
				LocalizedPackage.GlobalPackageId.ToIndexForDebugging(),
				*LocalizedPackage.SourcePackageName.ToString(),
				SourcePackage.GlobalPackageId.ToIndexForDebugging())
			return false;
		}

		int32 CompareResult = FCString::Stricmp(LocalizedExportStr, SourceExportStr);
		if (CompareResult < 0)
		{
			++LocalizedIndex;

			if (LocalizedExportData.GlobalImportIndex.IsImport())
			{
				// public localized export is missing in the source package, so just keep it as it is
				NewPublicExports.Emplace(LocalizedIndex - 1, 1);
			}
		}
		else if (CompareResult > 0)
		{
			++SourceIndex;

			if (SourceExportData.GlobalImportIndex.IsImport())
			{
				FailReason.Appendf(TEXT("Public source export '%s' is missing in the localized package"),
					*SourceExportData.ObjectName.ToString());
			}
		}
		else
		{
			++LocalizedIndex;
			++SourceIndex;

			if (SourceExportData.GlobalImportIndex.IsImport())
			{
				if (LocalizedExportData.ClassIndex != SourceExportData.ClassIndex)
				{
					AppendMismatchMessage(TEXT("class"), LocalizedExportData.ObjectName,
						LocalizedExportData.ClassIndex, SourceExportData.ClassIndex, FailReason);
				}
				else if (LocalizedExportData.TemplateIndex != SourceExportData.TemplateIndex)
				{
					AppendMismatchMessage(TEXT("template"), LocalizedExportData.ObjectName,
						LocalizedExportData.TemplateIndex, SourceExportData.TemplateIndex, FailReason);
				}
				else if (LocalizedExportData.SuperIndex != SourceExportData.SuperIndex)
				{
					AppendMismatchMessage(TEXT("super"), LocalizedExportData.ObjectName,
						LocalizedExportData.SuperIndex, SourceExportData.SuperIndex, FailReason);
				}
				else
				{
					NewPublicExports.Emplace(LocalizedIndex - 1, SourceIndex - 1);
				}
			}
			else if (LocalizedExportData.GlobalImportIndex.IsImport())
			{
				FailReason.Appendf(TEXT("Public localized export '%s' exists in the source package")
					TEXT(", but is not part of the source package interface (it is not imported by any other package)."),
					*LocalizedExportData.ObjectName.ToString());
			}
		}

		if (FailReason.Len() > 0)
		{
			UE_LOG(LogIoStore, Warning,
				TEXT("Culture '%s': Localized package '%s' (%d) for '%s' (%d) - %s"),
				*LocalizedPackage.Region,
				*LocalizedPackage.Name.ToString(),
				LocalizedPackage.GlobalPackageId.ToIndexForDebugging(),
				*LocalizedPackage.SourcePackageName.ToString(),
				SourcePackage.GlobalPackageId.ToIndexForDebugging(),
				*FailReason);
			bSuccess = false;
		}
	}

	if (bSuccess)
	{
		LocalizedPackage.PublicExports.Reset();
		for (TPair<int32, int32>& Pair : NewPublicExports)
		{
			FExportData& LocalizedExportData = GlobalExports[LocalizedPackage.Exports[Pair.Key]];
			if (Pair.Value != -1)
			{
				const FExportData& SourceExportData = GlobalExports[SourcePackage.Exports[Pair.Value]];

				LocalizedToSourceImportIndexMap.Add(
					LocalizedExportData.GlobalImportIndex,
					SourceExportData.GlobalImportIndex);

				LocalizedExportData.GlobalImportIndex = SourceExportData.GlobalImportIndex;
			}
			LocalizedPackage.PublicExports.Add(LocalizedExportData.GlobalImportIndex);
		}
	}

	return bSuccess;
}

static void AddPreloadDependencies(
	const FPackageAssetData& PackageAssetData,
	const FGlobalPackageData& GlobalPackageData,
	const FSourceToLocalizedPackageMultimap& SourceToLocalizedPackageMap,
	FExportGraph& ExportGraph,
	TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(PreLoadDependencies);
	UE_LOG(LogIoStore, Display, TEXT("Adding preload dependencies..."));

	const FGlobalPackageImports& PackageImports = GlobalPackageData.Imports.PackageImports;
	const TArray<FExportData>& GlobalExports = GlobalPackageData.Exports.Objects;

	TArray<FPackage*> LocalizedPackages;
	for (FPackage* Package : Packages)
	{
		// Convert PreloadDependencies to arcs
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = PackageAssetData.ObjectExports[Package->ExportIndexOffset + I];
			int32 PreloadDependenciesBaseIndex = Package->PreloadIndexOffset;

			FPackageIndex ExportPackageIndex = FPackageIndex::FromExport(I);

			auto AddPreloadArc = [&](FPackageIndex Dep, EPreloadDependencyType PhaseFrom, EPreloadDependencyType PhaseTo)
			{
				if (Dep.IsExport())
				{
					AddInternalExportArc(ExportGraph, *Package, Dep.ToExport(), PhaseFrom, I, PhaseTo);
				}
				else
				{
					FPackageObjectIndex ImportIndex = Package->Imports[Dep.ToImport()];
					if (ImportIndex.IsPackageImport())
					{
						const FPackageImportData& Import = PackageImports[ImportIndex.GetIndex()];
						check(Import.GlobalIndex == ImportIndex);
						if (Import.GlobalExportIndex != -1)
						{
							const FExportData& Export = GlobalExports[Import.GlobalExportIndex];

							AddExternalExportArc(ExportGraph, *Export.Package, Export.SourceIndex, PhaseFrom, *Package, I, PhaseTo);
							Package->ImportedPreloadPackages.Add(Export.Package);

							LocalizedPackages.Reset();
							SourceToLocalizedPackageMap.MultiFind(Export.Package, LocalizedPackages);
							for (FPackage* LocalizedPackage : LocalizedPackages)
							{
								UE_LOG(LogIoStore, Verbose, TEXT("For package '%s' (%d): Adding localized preload dependency '%s' in '%s'"),
									*Package->Name.ToString(),
									Package->GlobalPackageId.ToIndexForDebugging(),
									*Export.ObjectName.ToString(),
									*LocalizedPackage->Name.ToString());

								AddExternalExportArc(ExportGraph, *LocalizedPackage, Export.SourceIndex, PhaseFrom, *Package, I, PhaseTo);
								Package->ImportedPreloadPackages.Add(LocalizedPackage);
							}
						}
						else
						{
							UE_LOG(LogIoStore, Verbose, TEXT("Skipping export arc to '%s' due to missing import"), *Package->Name.ToString());
						}
					}
				}
			};

			if (PreloadDependenciesBaseIndex >= 0 && ObjectExport.FirstExportDependency >= 0)
			{
				int32 RunningIndex = PreloadDependenciesBaseIndex + ObjectExport.FirstExportDependency;
				for (int32 Index = ObjectExport.SerializationBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Serialize, PreloadDependencyType_Serialize);
				}

				for (int32 Index = ObjectExport.CreateBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Create, PreloadDependencyType_Serialize);
				}

				for (int32 Index = ObjectExport.SerializationBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Serialize, PreloadDependencyType_Create);
				}

				for (int32 Index = ObjectExport.CreateBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					// can't create this export until these things are created
					AddPreloadArc(Dep, PreloadDependencyType_Create, PreloadDependencyType_Create);
				}
			}
		}
	}
};

void BuildContainerNameMap(FContainerTargetSpec& ContainerTarget)
{
	FNameMapBuilder& NameMapBuilder = *ContainerTarget.NameMapBuilder;

	for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
	{
		NameMapBuilder.MarkNameAsReferenced(TargetFile.Package->Name);
		NameMapBuilder.MarkNamesAsReferenced(TargetFile.Package->Names, TargetFile.NameIndices);
	}
}

void FinalizePackageHeaders(
	FContainerTargetSpec& ContainerTarget,
	const TArray<FObjectExport>& ObjectExports,
	const TArray<FExportData>& GlobalExports,
	const FImportObjectsByFullName& GlobalImportsByFullName)
{
	const uint16 NameMapIndex = ContainerTarget.bUseLocalNameMap ? ContainerTarget.Header.ContainerId.ToIndex() : 0;

	for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
	{
		FPackage* Package = TargetFile.Package;
		check(TargetFile.ContainerTarget->NameMapBuilder);
		const FNameMapBuilder& NameMapBuilder = *TargetFile.ContainerTarget->NameMapBuilder;

		// Temporary Archive for serializing ImportMap
		FBufferWriter ImportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		for (FPackageObjectIndex& GlobalImportIndex : Package->Imports)
		{
			ImportMapArchive << GlobalImportIndex;
		}
		Package->ImportMapSize = ImportMapArchive.Tell();

		// Temporary Archive for serializing EDL graph data
		FBufferWriter GraphArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);

		int32 InternalArcCount = Package->InternalArcs.Num();
		GraphArchive << InternalArcCount;
		GraphArchive.Serialize(Package->InternalArcs.GetData(), Package->InternalArcs.Num() * sizeof(FArc));

		int32 ReferencedPackagesCount = Package->ExternalArcs.Num();
		GraphArchive << ReferencedPackagesCount;
		for (auto& KV : Package->ExternalArcs)
		{
			FPackage* ImportedPackage = KV.Key;
			TArray<FArc>& Arcs = KV.Value;
			int32 ExternalArcCount = Arcs.Num();

			GraphArchive << ImportedPackage->GlobalPackageId;
			GraphArchive << ExternalArcCount;
			GraphArchive.Serialize(Arcs.GetData(), ExternalArcCount * sizeof(FArc));
		}
		Package->UGraphSize = GraphArchive.Tell();

		// Temporary Archive for serializing export map data
		FBufferWriter ExportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			const FExportData& ExportData = GlobalExports[Package->Exports[I]];

			FExportMapEntry ExportMapEntry;
			ExportMapEntry.CookedSerialOffset = ObjectExport.SerialOffset;
			ExportMapEntry.CookedSerialSize = ObjectExport.SerialSize;
			ExportMapEntry.ObjectName = NameMapBuilder.MapName(ObjectExport.ObjectName);
			ExportMapEntry.OuterIndex = ExportData.OuterIndex;
			ExportMapEntry.ClassIndex = ExportData.ClassIndex;
			ExportMapEntry.SuperIndex = ExportData.SuperIndex;
			ExportMapEntry.TemplateIndex = ExportData.TemplateIndex;
			ExportMapEntry.GlobalImportIndex = ExportData.GlobalImportIndex;
			ExportMapEntry.ObjectFlags = ObjectExport.ObjectFlags;
			ExportMapEntry.FilterFlags = EExportFilterFlags::None;
			if (ObjectExport.bNotForClient)
			{
				ExportMapEntry.FilterFlags = EExportFilterFlags::NotForClient;
			}
			else if (ObjectExport.bNotForServer)
			{
				ExportMapEntry.FilterFlags = EExportFilterFlags::NotForServer;
			}

			ExportMapArchive << ExportMapEntry;
		}
		Package->ExportMapSize = ExportMapArchive.Tell();

		// Temporary archive for serializing export bundle data
		FBufferWriter ExportBundlesArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		uint32 ExportBundleEntryIndex = 0;
		for (FExportBundle& ExportBundle : Package->ExportBundles)
		{
			const uint32 EntryCount = ExportBundle.Nodes.Num();
			FExportBundleHeader ExportBundleHeader { ExportBundleEntryIndex, EntryCount };
			ExportBundlesArchive << ExportBundleHeader ;

			ExportBundleEntryIndex += EntryCount; 
		}
		for (FExportBundle& ExportBundle : Package->ExportBundles)
		{
			for (FExportGraphNode* ExportNode : ExportBundle.Nodes)
			{
				ExportBundlesArchive << ExportNode->BundleEntry;
			}
		}
		Package->ExportBundlesHeaderSize = ExportBundlesArchive.Tell();

		const uint64 NameMapSize = TargetFile.NameIndices.Num() * TargetFile.NameIndices.GetTypeSize();
		check(Package->NameMapSize == 0 || Package->NameMapSize == NameMapSize);
		Package->NameMapSize = NameMapSize;

		const uint64 PackageHeaderSize =
			sizeof(FPackageSummary)
			+ Package->NameMapSize
			+ Package->ImportMapSize
			+ Package->ExportMapSize
			+ Package->ExportBundlesHeaderSize
			+ Package->UGraphSize;

		check(Package->HeaderSerialSize == 0 || Package->HeaderSerialSize == PackageHeaderSize);
		Package->HeaderSerialSize = PackageHeaderSize;

		TargetFile.PackageHeaderData.AddZeroed(PackageHeaderSize);
		uint8* PackageHeaderBuffer = TargetFile.PackageHeaderData.GetData();
		FPackageSummary* PackageSummary = reinterpret_cast<FPackageSummary*>(PackageHeaderBuffer);

		PackageSummary->PackageFlags = Package->PackageFlags;
		PackageSummary->CookedHeaderSize = Package->CookedHeaderSize;
		PackageSummary->NameMapIndex = NameMapIndex;
		PackageSummary->Pad = 0;
		PackageSummary->GraphDataSize = Package->UGraphSize;

		FBufferWriter SummaryArchive(PackageHeaderBuffer, PackageHeaderSize);
		SummaryArchive.Seek(sizeof(FPackageSummary));

		// NameMap data
		{
			PackageSummary->NameMapOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(TargetFile.NameIndices.GetData(),
				TargetFile.NameIndices.Num() * TargetFile.NameIndices.GetTypeSize());
		}

		// ImportMap data
		{
			check(ImportMapArchive.Tell() == Package->ImportMapSize);
			PackageSummary->ImportMapOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(ImportMapArchive.GetWriterData(), ImportMapArchive.Tell());
		}

		// ExportMap data
		{
			check(ExportMapArchive.Tell() == Package->ExportMapSize);
			PackageSummary->ExportMapOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(ExportMapArchive.GetWriterData(), ExportMapArchive.Tell());
		}

		// ExportBundle data
		{
			check(ExportBundlesArchive.Tell() == Package->ExportBundlesHeaderSize);
			PackageSummary->ExportBundlesOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(ExportBundlesArchive.GetWriterData(), ExportBundlesArchive.Tell());
		}

		// Graph data
		{
			check(GraphArchive.Tell() == Package->UGraphSize);
			PackageSummary->GraphDataOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(GraphArchive.GetWriterData(), GraphArchive.Tell());
		}
	}
}

void SerializePackageStoreEntries(
	TArray<FPackage*> Packages,
	FArchive& StoreTocArchive,
	FArchive& StoreDataArchive)
{
	IOSTORE_CPU_SCOPE(SerializePackageStoreEntries);
	UE_LOG(LogIoStore, Display, TEXT("Finalizing package store..."));

	int32 StoreTocSize = Packages.Num() * sizeof(FPackageStoreEntry);

	auto SerializePackageEntryCArrayHeader = [&StoreTocSize,&StoreTocArchive,&StoreDataArchive](int32 Count)
	{
		const int32 RemainingTocSize = StoreTocSize - StoreTocArchive.Tell();
		const int32 OffsetFromThis = RemainingTocSize + StoreDataArchive.Tell();
		uint32 ArrayNum = Count > 0 ? Count : 0; 
		uint32 OffsetToDataFromThis = ArrayNum > 0 ? OffsetFromThis : 0;

		StoreTocArchive << ArrayNum;
		StoreTocArchive << OffsetToDataFromThis;
	};

	for (FPackage* Package : Packages)
	{
		FMappedName PackageName;
		StoreTocArchive << PackageName;
		StoreTocArchive << Package->SourceGlobalPackageId;
		StoreTocArchive << Package->ExportCount;

		// Global imported packages meta data
		{
			SerializePackageEntryCArrayHeader(Package->ImportedPackagesSerializeCount);
			for (int32 I = 0; I < Package->ImportedPackagesSerializeCount; ++I)
			{
				FPackage* ImportedPackage = Package->ImportedPackages[I];
				StoreDataArchive << ImportedPackage->GlobalPackageId;
			}
		}

		// Global public exports meta data
		{
			SerializePackageEntryCArrayHeader(Package->PublicExports.Num());
			for (FPackageObjectIndex ObjectIndex : Package->PublicExports)
			{
				StoreDataArchive << ObjectIndex;
			}
		}

		// Global export bundle meta data
		{
			// TODO: Request IO for each package export bundle individually in the loader,
			//       or just store the data for the first entry directly on the package entry itself
			SerializePackageEntryCArrayHeader(Package->ExportBundles.Num());
			FExportBundleMetaEntry ExportBundleMetaEntry;
			ExportBundleMetaEntry.PayloadSize = Package->HeaderSerialSize + Package->ExportsSerialSize;
			for (const FExportBundle& ExportBundle : Package->ExportBundles)
			{
				ExportBundleMetaEntry.LoadOrder = ExportBundle.LoadOrder;
				StoreDataArchive << ExportBundleMetaEntry;

				ExportBundleMetaEntry.PayloadSize = 0; // currently only specified for the first entry
			}
		}
	}
}

static void SerializeInitialLoad(
	FNameMapBuilder& GlobalNameMapBuilder,
	const FGlobalScriptImports& GlobalScriptImports,
	FArchive& InitialLoadArchive)
{
	IOSTORE_CPU_SCOPE(SerializeInitialLoad);
	UE_LOG(LogIoStore, Display, TEXT("Finalizing initial load..."));

	int32 NumScriptObjects = GlobalScriptImports.Num();
	InitialLoadArchive << NumScriptObjects; 

	for (const FScriptImportData& ImportData : GlobalScriptImports)
	{
		GlobalNameMapBuilder.MarkNameAsReferenced(ImportData.ObjectName);
		FScriptObjectEntry Entry;
		Entry.ObjectName = GlobalNameMapBuilder.MapName(ImportData.ObjectName).ToUnresolvedMinimalName();
		Entry.OuterIndex = ImportData.OuterIndex;
		Entry.CDOClassIndex = ImportData.CDOClassIndex;

		InitialLoadArchive << Entry; 
	}
};

static FIoBuffer CreateExportBundleBuffer(const FContainerTargetFile& TargetFile, const TArray<FObjectExport>& ObjectExports, const FIoBuffer UExpBuffer)
{
	const FPackage* Package = TargetFile.Package;
	check(TargetFile.PackageHeaderData.Num() > 0);
	const uint64 BundleBufferSize = TargetFile.PackageHeaderData.Num() + TargetFile.Package->ExportsSerialSize;
	FIoBuffer BundleBuffer(BundleBufferSize);
	FMemory::Memcpy(BundleBuffer.Data(), TargetFile.PackageHeaderData.GetData(), TargetFile.PackageHeaderData.Num());
	uint64 BundleBufferOffset = TargetFile.PackageHeaderData.Num();
	for (const FExportBundle& ExportBundle : TargetFile.Package->ExportBundles)
	{
		for (const FExportGraphNode* Node : ExportBundle.Nodes)
		{
			if (Node->BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + Node->BundleEntry.LocalExportIndex];
				const int64 Offset = ObjectExport.SerialOffset - Package->UAssetSize;
				check(uint64(Offset + ObjectExport.SerialSize) <= UExpBuffer.DataSize());
				FMemory::Memcpy(BundleBuffer.Data() + BundleBufferOffset, UExpBuffer.Data() + Offset, ObjectExport.SerialSize);
				BundleBufferOffset += ObjectExport.SerialSize;
			}
		}
	}
	check(BundleBufferOffset == BundleBuffer.DataSize());
	return BundleBuffer;
}

static void ParsePackageAssets(
	TArray<FPackage*>& Packages,
	FPackageAssetData& PackageAssetData,
	FExportGraph& ExportGraph)
{
	IOSTORE_CPU_SCOPE(ParsePackageAssets);
	UE_LOG(LogIoStore, Display, TEXT("Parsing packages..."));

	TAtomic<int32> ReadCount {0};
	TAtomic<int32> ParseCount {0};
	const int32 TotalPackageCount = Packages.Num();

	TArray<FPackageFileSummary> PackageFileSummaries;
	PackageFileSummaries.SetNum(TotalPackageCount);

	uint8* UAssetMemory = nullptr;
	TArray<uint8*> PackageAssetBuffers;
	PackageAssetBuffers.SetNum(TotalPackageCount);

	UE_LOG(LogIoStore, Display, TEXT("Reading package assets..."));
	{
		IOSTORE_CPU_SCOPE(ReadUAssetFiles);

		uint64 TotalUAssetSize = 0;
		for (const FPackage* Package : Packages)
		{
			TotalUAssetSize += Package->UAssetSize;
		}
		UAssetMemory = reinterpret_cast<uint8*>(FMemory::Malloc(TotalUAssetSize));
		uint8* UAssetMemoryPtr = UAssetMemory;
		for (int32 Index = 0; Index < TotalPackageCount; ++Index)
		{
			PackageAssetBuffers[Index] = UAssetMemoryPtr;
			UAssetMemoryPtr += Packages[Index]->UAssetSize;
		}

		TAtomic<uint64> CurrentFileIndex{ 0 };
		ParallelFor(TotalPackageCount, [&ReadCount, &PackageAssetBuffers, &Packages, &CurrentFileIndex](int32 Index)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ReadUAssetFile);
			FPackage* Package = Packages[Index];
			uint8* Buffer = PackageAssetBuffers[Index];
			IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Package->FileName);
			if (FileHandle)
			{
				bool bSuccess = FileHandle->Read(Buffer, Package->UAssetSize);
				UE_CLOG(!bSuccess, LogIoStore, Warning, TEXT("Failed reading file '%s'"), *Package->FileName);
				delete FileHandle;
			}
			else
			{
				UE_LOG(LogIoStore, Warning, TEXT("Couldn't open file '%s'"), *Package->FileName);
			}
			uint64 LocalFileIndex = CurrentFileIndex.IncrementExchange() + 1;
			UE_CLOG(LocalFileIndex % 1000 == 0, LogIoStore, Display, TEXT("Reading %d/%d: '%s'"), LocalFileIndex, Packages.Num(), *Package->FileName);
		}, EParallelForFlags::Unbalanced);
	}

	{
		IOSTORE_CPU_SCOPE(SerializeSummaries);

		ParallelFor(TotalPackageCount, [
				&ReadCount,
				&PackageAssetBuffers,
				&PackageFileSummaries,
				&Packages](int32 Index)
		{
			uint8* PackageBuffer = PackageAssetBuffers[Index];
			FPackageFileSummary& Summary = PackageFileSummaries[Index];
			FPackage& Package = *Packages[Index];

			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			if (!Package.UAssetSize)
			{
				return;
			}

			FMemoryReaderView Ar(MemView);
			Ar << Summary;

			Package.SummarySize = Ar.Tell();
			Package.NameCount = Summary.NameCount;
			Package.ImportCount = Summary.ImportCount;
			Package.PreloadDependencyCount = Summary.PreloadDependencyCount;
			Package.ExportCount = Summary.ExportCount;
			Package.PackageFlags = Summary.PackageFlags;
			Package.CookedHeaderSize = Summary.TotalHeaderSize;

		}, EParallelForFlags::Unbalanced);
	}

	int32 TotalImportCount = 0;
	int32 TotalPreloadDependencyCount = 0;
	int32 TotalExportCount = 0;
	for (FPackage* Package : Packages)
	{
		if (Package->ImportCount > 0)
		{
			Package->ImportIndexOffset = TotalImportCount;
			TotalImportCount += Package->ImportCount;
		}

		if (Package->PreloadDependencyCount > 0)
		{
			Package->PreloadIndexOffset = TotalPreloadDependencyCount;
			TotalPreloadDependencyCount += Package->PreloadDependencyCount;
		}

		if (Package->ExportCount > 0)
		{
			Package->ExportIndexOffset = TotalExportCount;
			TotalExportCount += Package->ExportCount;
		}
	}
	PackageAssetData.ObjectImports.AddUninitialized(TotalImportCount);
	PackageAssetData.PreloadDependencies.AddUninitialized(TotalPreloadDependencyCount);
	PackageAssetData.ObjectExports.AddUninitialized(TotalExportCount);

	UE_LOG(LogIoStore, Display, TEXT("Parsing package assets..."));
	{
		IOSTORE_CPU_SCOPE(SerializeAssets);

		for (int32 PackageIndex = 0; PackageIndex < TotalPackageCount; ++PackageIndex)
		{
			uint8* PackageBuffer = PackageAssetBuffers[PackageIndex];
			const FPackageFileSummary& Summary = PackageFileSummaries[PackageIndex];
			FPackage& Package = *Packages[PackageIndex];
			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			FMemoryReaderView Ar(MemView);

			if (Summary.NameCount > 0)
			{
				Ar.Seek(Summary.NameOffset);

				Package.Names.Reserve(Summary.NameCount);
				Package.NameMap.Reserve(Summary.NameCount);
				FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);

				for (int32 I = 0; I < Summary.NameCount; ++I)
				{
					Ar << NameEntry;
					FName& Name = Package.Names.Emplace_GetRef(NameEntry);
					Package.NameMap.Emplace(Name.GetDisplayIndex());
				}
			}
		}

		ParallelFor(TotalPackageCount,[
				&ParseCount,
				&PackageAssetBuffers,
				&PackageFileSummaries,
				&Packages,
				&PackageAssetData,
				&ExportGraph](int32 Index)
		{
			uint8* PackageBuffer = PackageAssetBuffers[Index];
			const FPackageFileSummary& Summary = PackageFileSummaries[Index];
			FPackage& Package = *Packages[Index];
			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			FMemoryReaderView Ar(MemView);
			Ar.SetFilterEditorOnly((Package.PackageFlags & EPackageFlags::PKG_FilterEditorOnly) != 0);

			IOSTORE_CPU_SCOPE_DATA(ParsePackage, TCHAR_TO_ANSI(*Package.FileName));

			const int32 Count = ParseCount.IncrementExchange();
			UE_CLOG(Count % 1000 == 0, LogIoStore, Display, TEXT("Parsing %d/%d: '%s'"), Count, Packages.Num(), *Package.FileName);

			if (Summary.ImportCount > 0)
			{
				FNameReaderProxyArchive ProxyAr(Ar, Package.NameMap);
				ProxyAr.Seek(Summary.ImportOffset);

				for (int32 I = 0; I < Summary.ImportCount; ++I)
				{
					FObjectImport& ObjectImport = PackageAssetData.ObjectImports[Package.ImportIndexOffset + I];
					ProxyAr << ObjectImport;
				}
			}

			if (Summary.PreloadDependencyCount > 0)
			{
				Ar.Seek(Summary.PreloadDependencyOffset);
				Ar.Serialize(PackageAssetData.PreloadDependencies.GetData() + Package.PreloadIndexOffset, Summary.PreloadDependencyCount * sizeof(FPackageIndex));
			}

			if (Summary.ExportCount > 0)
			{
				FNameReaderProxyArchive ProxyAr(Ar, Package.NameMap);
				ProxyAr.Seek(Summary.ExportOffset);

				for (int32 I = 0; I < Summary.ExportCount; ++I)
				{
					FObjectExport& ObjectExport = PackageAssetData.ObjectExports[Package.ExportIndexOffset + I];
					ProxyAr << ObjectExport;
					Package.ExportsSerialSize += ObjectExport.SerialSize;
				}
			}
		}, EParallelForFlags::Unbalanced);
	}

	FMemory::Free(UAssetMemory);
}

static void CreateGlobalImportsAndExports(
	TArray<FPackage*>& Packages,
	const FPackageMap& PackageMap,
	FPackageAssetData& PackageAssetData,
	FGlobalPackageData& GlobalPackageData,
	FExportGraph& ExportGraph)
{
	IOSTORE_CPU_SCOPE(CreateGlobalImportsAndExports);
	UE_LOG(LogIoStore, Display, TEXT("Creating global imports and exports..."));

	FGlobalImports& GlobalImports = GlobalPackageData.Imports;
	FImportObjectsByFullName& GlobalImportsByFullName = GlobalPackageData.Imports.ObjectsByFullName;

	TArray<FString> TmpFullNames;

	for (FPackage* Package : Packages)
	{
		if (Package->ImportCount > 0)
		{
			FPackage* CurrentImportPackage = nullptr;
			Package->ImportedFullNames.SetNum(Package->ImportCount);
			for (int32 ImportIndex = 0; ImportIndex < Package->ImportCount; ++ImportIndex)
			{
				FindImport(
					GlobalPackageData.Imports,
					Package->ImportedFullNames,
					PackageAssetData.ObjectImports.GetData() + Package->ImportIndexOffset,
					ImportIndex,
					PackageMap,
					CurrentImportPackage);
			}
		}

		if (Package->ExportCount > 0)
		{
			TmpFullNames.Reset();
			TmpFullNames.SetNum(Package->ExportCount, false);

			for (int32 ExportIndex = 0; ExportIndex < Package->ExportCount; ExportIndex++)
			{
				FindExport(
					GlobalPackageData.Exports,
					TmpFullNames,
					PackageAssetData.ObjectExports.GetData() + Package->ExportIndexOffset,
					ExportIndex,
					Package);
				const int32* GlobalIndex = GlobalPackageData.Exports.ObjectsByFullName.Find(TmpFullNames[ExportIndex]);
				FExportData& ExportData = GlobalPackageData.Exports.Objects[*GlobalIndex];
				Package->Exports.Add(ExportData.GlobalIndex);
				ExportData.CreateNode = ExportGraph.AddNode(Package, { uint32(ExportIndex), FExportBundleEntry::ExportCommandType_Create });
				ExportData.SerializeNode = ExportGraph.AddNode(Package, { uint32(ExportIndex), FExportBundleEntry::ExportCommandType_Serialize });
				Package->CreateExportNodes.Add(ExportData.CreateNode);
				Package->SerializeExportNodes.Add(ExportData.SerializeNode);
				ExportGraph.AddInternalDependency(ExportData.CreateNode, ExportData.SerializeNode);
			}
		}
	}

	// lookup script CDO class indices for script imports
	for (FScriptImportData& ImportData : GlobalImports.ScriptImports)
	{
		if (ImportData.bInitialized && ImportData.CDOClassFullName.Len() > 0)
		{
			FPackageObjectIndex* CDOClassIndex = GlobalImportsByFullName.Find(ImportData.CDOClassFullName);
			if (CDOClassIndex)
			{
				ImportData.CDOClassIndex = *CDOClassIndex;
			}
			else
			{
				UE_LOG(LogIoStore, Warning, TEXT("Missing script import class '%s' for CDO '%s'"),
					*ImportData.CDOClassFullName, *ImportData.FullName);
			}
		}
	}

	// lookup mappings between package imports and exports, and fill the package public exports
	for (FPackageImportData& ImportData : GlobalImports.PackageImports)
	{
		if (ImportData.bInitialized)
		{
			int32* FindGlobalExport = GlobalPackageData.Exports.ObjectsByFullName.Find(ImportData.FullName);
			if (FindGlobalExport)
			{
				check(ImportData.Package);
				ImportData.Package->PublicExports.Add(ImportData.GlobalIndex);
				ImportData.GlobalExportIndex = *FindGlobalExport;
				GlobalPackageData.Exports.Objects[*FindGlobalExport].GlobalImportIndex = ImportData.GlobalIndex;
			}
			else
			{
				UE_LOG(LogIoStore, Warning, TEXT("Missing import '%s' due to missing export"), *ImportData.FullName);
				ImportData.bIsMissingImport = true;
			}
		}
	}
}

static void MapImportedPackages(
	const FGlobalImports& GlobalImports,
	TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(MapImportedPackages);
	UE_LOG(LogIoStore, Display, TEXT("Mapping import packages..."));

	const FImportObjectsByFullName& ObjectsByFullName = GlobalImports.ObjectsByFullName;
	TSet<FPackage*> ImportedPackages;
	for (FPackage* Package : Packages)
	{
		bool bHasMissingImports = false;
		Package->Imports.Reserve(Package->ImportCount);
		Package->ImportedPackages.Reserve(Package->ImportCount / 2);
		ImportedPackages.Reset();
		for (int32 I = 0; I < Package->ImportCount; ++I)
		{
			FPackageObjectIndex GlobalImportIndex = ObjectsByFullName.FindRef(Package->ImportedFullNames[I]);
			Package->Imports.Add(GlobalImportIndex);
			if (GlobalImportIndex.IsPackageImport())
			{
				const FPackageImportData& ImportData = GlobalImports.PackageImports[GlobalImportIndex.GetIndex()];
				bHasMissingImports |= ImportData.bIsMissingImport;
				if (ImportData.Package)
				{
					bool bAlreadyInSet;
					ImportedPackages.Add(ImportData.Package, &bAlreadyInSet);
					if (!bAlreadyInSet)
					{
						Package->ImportedPackages.Add(ImportData.Package);
						ImportData.Package->ImportedByPackages.Add(Package);
					}
				}
			}
		}
		UE_CLOG(bHasMissingImports, LogIoStore, Warning, TEXT("Missing imports for package '%s'"), *Package->Name.ToString());
	}
};

static void MapExportEntryIndices(
	TArray<FObjectExport>& ObjectExports,
	TArray<FExportData>& GlobalExports,
	TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(ExportData);
	UE_LOG(LogIoStore, Display, TEXT("Converting export map import indices..."));

	auto PackageObjectIndexFromPackageIndex =
		[](const TArray<FPackageObjectIndex>& Imports, const FPackageIndex& PackageIndex) -> FPackageObjectIndex 
		{ 
			if (PackageIndex.IsImport())
			{
				return Imports[PackageIndex.ToImport()];
			}
			if (PackageIndex.IsExport())
			{
				return FPackageObjectIndex(FPackageObjectIndex::Export, PackageIndex.ToExport());
			}
			return FPackageObjectIndex();
		};

	for (FPackage* Package : Packages)
	{
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			FExportData& ExportData = GlobalExports[Package->Exports[I]];
			ExportData.OuterIndex = PackageObjectIndexFromPackageIndex(Package->Imports, ObjectExport.OuterIndex);
			ExportData.ClassIndex = PackageObjectIndexFromPackageIndex(Package->Imports, ObjectExport.ClassIndex);
			ExportData.SuperIndex = PackageObjectIndexFromPackageIndex(Package->Imports, ObjectExport.SuperIndex);
			ExportData.TemplateIndex = PackageObjectIndexFromPackageIndex(Package->Imports, ObjectExport.TemplateIndex);
		}
	}
};
static void ProcessLocalizedPackages(
	const TArray<FPackage*>& Packages,
	const FPackageMap& PackageMap,
	const FGlobalImports& GlobalImports,
	TArray<FExportData>& GlobalExports,
	FCulturePackageMap& OutCulturePackageMap,
	FSourceToLocalizedPackageMultimap& OutSourceToLocalizedPackageMap)
{
	IOSTORE_CPU_SCOPE(ProcessLocalizedPackages);

	FLocalizedToSourceImportIndexMap LocalizedToSourceImportIndexMap;

	UE_LOG(LogIoStore, Display, TEXT("Conforming localized packages..."));
	for (FPackage* Package : Packages)
	{
		if (Package->Region.Len() == 0)
		{
			continue;
		}

		if (Package->Name == Package->SourcePackageName)
		{
			UE_LOG(LogIoStore, Error,
				TEXT("For culture '%s': Localized package '%s' (%d) should have a package name different from source name."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ToIndexForDebugging())
			continue;
		}

		FPackage* SourcePackage = PackageMap.FindRef(Package->SourcePackageName);
		if (!SourcePackage)
		{
			// no update or verification required
			UE_LOG(LogIoStore, Verbose,
				TEXT("For culture '%s': Localized package '%s' (%d) is unique and does not override a source package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ToIndexForDebugging());
			continue;
		}

		Package->SourceGlobalPackageId = SourcePackage->GlobalPackageId;

		Package->bIsLocalizedAndConformed = ConformLocalizedPackage(
			PackageMap, GlobalImports, *SourcePackage,
			*Package, GlobalExports, LocalizedToSourceImportIndexMap);

		if (Package->bIsLocalizedAndConformed)
		{
			UE_LOG(LogIoStore, Verbose, TEXT("For culture '%s': Adding conformed localized package '%s' (%d) for '%s' (%d). ")
				TEXT("When loading the source package, it will be remapped to this localized package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ToIndexForDebugging(),
				*Package->SourcePackageName.ToString(),
				SourcePackage->GlobalPackageId.ToIndexForDebugging());

			OutSourceToLocalizedPackageMap.Add(SourcePackage, Package);
			OutCulturePackageMap.FindOrAdd(Package->Region).Add(SourcePackage->GlobalPackageId, Package->GlobalPackageId);
		}
		else
		{
			UE_LOG(LogIoStore, Warning,
				TEXT("For culture '%s': Localized package '%s' (%d) does not conform to source package '%s' (%d) due to mismatching public exports. ")
				TEXT("When loading the source package, it will never be remapped to this localized package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ToIndexForDebugging(),
				*Package->SourcePackageName.ToString(),
				SourcePackage->GlobalPackageId.ToIndexForDebugging());
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Adding localized import packages..."));
	TArray<FPackage*> LocalizedPackages;

	for (FPackage* Package : Packages)
	{
		LocalizedPackages.Reset();
		for (FPackage* ImportedPackage : Package->ImportedPackages)
		{
			LocalizedPackages.Reset();
			OutSourceToLocalizedPackageMap.MultiFind(ImportedPackage, LocalizedPackages);
			for (FPackage* LocalizedPackage : LocalizedPackages)
			{
				UE_LOG(LogIoStore, Verbose, TEXT("For package '%s' (%d): Adding localized imported package '%s' (%d)"),
					*Package->Name.ToString(),
					Package->GlobalPackageId.ToIndexForDebugging(),
					*LocalizedPackage->Name.ToString(),
					LocalizedPackage->GlobalPackageId.ToIndexForDebugging());
			}
		}
		Package->ImportedPackagesSerializeCount = Package->ImportedPackages.Num();
		Package->ImportedPackages.Append(LocalizedPackages);
	}

	UE_LOG(LogIoStore, Display, TEXT("Conforming localized imports..."));
	for (FPackage* Package : Packages)
	{
		for (FPackageObjectIndex& GlobalImportIndex : Package->Imports)
		{
			if (GlobalImportIndex.IsPackageImport())
			{
				const FPackageImportData& ImportData = GlobalImports.PackageImports[GlobalImportIndex.GetIndex()];
				if (ImportData.bIsLocalized)
				{
					const FPackageObjectIndex* SourceGlobalImportIndex = LocalizedToSourceImportIndexMap.Find(GlobalImportIndex);
					if (SourceGlobalImportIndex)
					{
						GlobalImportIndex = *SourceGlobalImportIndex;

						const FPackageImportData& SourceImportData = GlobalImports.PackageImports[SourceGlobalImportIndex->GetIndex()];
						UE_LOG(LogIoStore, Verbose,
							TEXT("For package '%s' (%d): Remap localized import %s to source import %s (in a conformed localized package)"),
							*Package->Name.ToString(),
							Package->GlobalPackageId.ToIndexForDebugging(),
							*ImportData.FullName,
							*SourceImportData.FullName);
					}
					else
					{
						UE_LOG(LogIoStore, Verbose,
							TEXT("For package '%s' (%d): Skip remap for localized import %s")
							TEXT(", either there is no source package or the localized package did not conform to it."),
							*Package->Name.ToString(),
							Package->GlobalPackageId.ToIndexForDebugging(),
							*ImportData.FullName);
					}
				}
			}
		}
	}
}

static void SaveReleaseVersionMeta(const TCHAR* ReleaseVersionOutputDir, const FNameMapBuilder& GlobalNameMap, const FPackageGlobalIdMap& PackageGlobalIdMap, const FGlobalImports& GlobalImports, TArray<FContainerTargetSpec*>& ContainerTargets)
{
	UE_LOG(LogIoStore, Display, TEXT("Saving release meta data to '%s'"), ReleaseVersionOutputDir);

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(ReleaseVersionOutputDir);

	{
		FString NameMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.unamemap"));
		TUniquePtr<FArchive> NameMapArchive(IFileManager::Get().CreateFileWriter(*NameMapOutputPath));
		(*NameMapArchive) << const_cast<FNameMapBuilder&>(GlobalNameMap);
	}

	{
		FString PackageMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.upackagemap"));
		TUniquePtr<FArchive> PackageMapArchive(IFileManager::Get().CreateFileWriter(*PackageMapOutputPath));

		TArray<FName> OrderedGlobalIdMap;
		OrderedGlobalIdMap.SetNum(PackageGlobalIdMap.Num());
		for (const auto& KV : PackageGlobalIdMap)
		{
			FName PackageName = KV.Key;
			FPackageId PackageGlobalId = KV.Value;
			OrderedGlobalIdMap[PackageGlobalId.ToIndex()] = PackageName;
		}

		int32 PackageCount = OrderedGlobalIdMap.Num();
		(*PackageMapArchive) << PackageCount;
		for (const FName& PackageName : OrderedGlobalIdMap)
		{
			const FNameEntry* NameEntry = FName::GetEntry(PackageName.GetComparisonIndex());
			NameEntry->Write(*PackageMapArchive);
			int32 NameNumber = PackageName.GetNumber();
			(*PackageMapArchive) << NameNumber;
		}
	}

	{
		FString ImportMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.uimportmap"));
		TUniquePtr<FArchive> ImportMapArchive(IFileManager::Get().CreateFileWriter(*ImportMapOutputPath));

		int32 ImportCount = GlobalImports.ObjectsByFullName.Num();
		(*ImportMapArchive) << ImportCount;
		for (const auto& KV : GlobalImports.ObjectsByFullName)
		{
			FString ObjectName = KV.Key;
			FPackageObjectIndex ObjectIndex = KV.Value;
			int32 Type = ObjectIndex.GetType();
			int32 Index = ObjectIndex.GetIndex();

			(*ImportMapArchive) << ObjectName;
			(*ImportMapArchive) << Type;
			(*ImportMapArchive) << Index;
		}
	}

	for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		FContainerMeta ContainerMeta;
		ContainerMeta.ContainerId = ContainerTarget->Header.ContainerId;
		ContainerMeta.ContainerName = ContainerTarget->Name.ToString();

		// Should be safe now as all container(s) has been saved.
		ContainerMeta.NameMapBuilder = MoveTemp(ContainerTarget->LocalNameMapBuilder);

		FString MetaOutputPath = FPaths::Combine(ReleaseVersionOutputDir, ContainerTarget->Name.ToString() + TEXT(".ucontainermeta"));
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*MetaOutputPath));
		*Ar << ContainerMeta;

		UE_LOG(LogIoStore, Display,
			TEXT("Saved container release meta '%s' with container ID '%d' and '%d' names"),
			*MetaOutputPath,
			ContainerMeta.ContainerId.ToIndex(),
			ContainerMeta.NameMapBuilder.GetNameMap().Num());
	}	
}

static void LoadReleaseVersionMeta(
	const TCHAR* ReleaseVersionOutputDir,
	FNameMapBuilder& GlobalNameMap,
	FPackageGlobalIdMap& PackageGlobalIdMap,
	FGlobalImports& GlobalImports,
	TArray<FContainerTargetSpec*>& ContainerTargets,
	TMap<FName, FContainerTargetSpec*>& ContainerTargetMap)
{
	UE_LOG(LogIoStore, Display, TEXT("Loading release meta data from '%s'"), ReleaseVersionOutputDir);

	{
		FString NameMapPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.unamemap"));
		TUniquePtr<FArchive> NameMapArchive(IFileManager::Get().CreateFileReader(*NameMapPath));
		if (NameMapArchive)
		{
			(*NameMapArchive) << GlobalNameMap;
		}
	}
	{
		FString PackageMapPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.upackagemap"));
		TUniquePtr<FArchive> PackageMapArchive(IFileManager::Get().CreateFileReader(*PackageMapPath));
		if (PackageMapArchive)
		{
			int32 PackageCount;
			(*PackageMapArchive) << PackageCount;
			for (int32 PackageIndex = 0; PackageIndex < PackageCount; ++PackageIndex)
			{
				FNameEntrySerialized NameEntrySerialized(ENAME_LinkerConstructor);
				(*PackageMapArchive) << NameEntrySerialized;
				int32 NameNumber;
				(*PackageMapArchive) << NameNumber;
				FName Name(NameEntrySerialized, NameNumber);
				check(!PackageGlobalIdMap.Contains(Name));
				PackageGlobalIdMap.Add(Name, FPackageId::FromIndex(PackageIndex));
			}
			check(PackageGlobalIdMap.Num() == PackageCount);
		}
	}
	{
		FString ImportMapPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.uimportmap"));
		TUniquePtr<FArchive> ImportMapArchive(IFileManager::Get().CreateFileReader(*ImportMapPath));
		if (ImportMapArchive)
		{
			int32 ImportCount = 0;
			(*ImportMapArchive) << ImportCount;
			for (int32 ImportIndex = 0; ImportIndex < ImportCount; ++ImportIndex)
			{
				FString FullName;
				int32 Type;
				int32 Index;

				(*ImportMapArchive) << FullName;
				(*ImportMapArchive) << Type;
				(*ImportMapArchive) << Index;

				FPackageObjectIndex ObjectIndex(static_cast<FPackageObjectIndex::Type>(Type), Index);
				GlobalImports.ObjectsByFullName.Add(FullName, ObjectIndex);
				if (Type == FPackageObjectIndex::ScriptImport)
				{
					TArray<FScriptImportData>& ScriptImports = GlobalImports.ScriptImports;
					if (ScriptImports.Num() <= Index)
					{
						ScriptImports.AddDefaulted(Index - ScriptImports.Num() + 1);
					}
					FScriptImportData& ScriptImport = ScriptImports[Index];
					ScriptImport.GlobalIndex = ObjectIndex;
					ScriptImport.OutermostIndex = ObjectIndex;
					ScriptImport.FullName = FullName;
				}
				else
				{
					TArray<FPackageImportData>& PackageImports = GlobalImports.PackageImports;
					if (PackageImports.Num() <= Index)
					{
						PackageImports.AddDefaulted(Index - PackageImports.Num() + 1);
					}
					FPackageImportData& PackageImport = PackageImports[Index];
					PackageImport.GlobalIndex = ObjectIndex;
					PackageImport.FullName = FullName;
				}
			}
		}
	}

	FString ContainerMetaWildcard = FPaths::Combine(ReleaseVersionOutputDir, TEXT("*.ucontainermeta"));
	TArray<FString> ContainerMetaFileNames;
	IFileManager::Get().FindFiles(ContainerMetaFileNames, *ContainerMetaWildcard, true, false);
	for (FString& ContainerMetaFileName : ContainerMetaFileNames)
	{
		ContainerMetaFileName = ReleaseVersionOutputDir / ContainerMetaFileName;
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*ContainerMetaFileName));
		if (Ar)
		{
			FContainerMeta ContainerMeta;
			*Ar << ContainerMeta;

			FContainerTargetSpec* ContainerTarget = AddContainer(FName(ContainerMeta.ContainerName), ContainerMeta.ContainerId, ContainerTargets, ContainerTargetMap);
			ContainerTarget->LocalNameMapBuilder = MoveTemp(ContainerMeta.NameMapBuilder);

			UE_LOG(LogIoStore, Display,
				TEXT("Loaded container release meta '%s' with container ID '%d' and '%d' names"),
				*ContainerMetaFileName,
				ContainerTarget->Header.ContainerId.ToIndex(),
				ContainerTarget->LocalNameMapBuilder.GetNameMap().Num());
		}
	}
}

void InitializeContainerTargetsAndPackages(
	const FIoStoreArguments& Arguments,
	TArray<FPackage*>& Packages,
	FPackageMap& PackageMap,
	FPackageGlobalIdMap& PackageGlobalIdMap,
	TArray<FContainerTargetSpec*>& ContainerTargets,
	TMap<FName, FContainerTargetSpec*>& ContainerTargetMap,
	FNameMapBuilder& GlobalNameMapBuilder)
{
	FString ProjectName = FApp::GetProjectName();
	FString RelativeEnginePath = FPaths::GetRelativePathToRoot();
	FString RelativeProjectPath = FPaths::ProjectDir();
	int32 CookedEngineDirLen = Arguments.CookedDir.Len() + 1;;
	int32 CookedProjectDirLen = CookedEngineDirLen + ProjectName.Len() + 1;

	auto ConvertCookedPathToRelativePath = [
		&ProjectName,
		&CookedEngineDirLen,
		&CookedProjectDirLen,
		&RelativeEnginePath,
		&RelativeProjectPath]
		(const FString& CookedFile) -> FString
	{
		FString RelativeFileName;
		const TCHAR* FileName = *CookedFile + CookedEngineDirLen;
		if (FCString::Strncmp(FileName, *ProjectName, ProjectName.Len()))
		{
			int32 FileNameLen = CookedFile.Len() - CookedEngineDirLen;
			RelativeFileName.Reserve(RelativeEnginePath.Len() + FileNameLen);
			RelativeFileName = RelativeEnginePath;
			RelativeFileName.AppendChars(*CookedFile + CookedEngineDirLen, FileNameLen);
		}
		else
		{
			FileName = *CookedFile + CookedProjectDirLen;
			int32 FileNameLen = CookedFile.Len() - CookedProjectDirLen;
			RelativeFileName.Reserve(RelativeProjectPath.Len() + FileNameLen);
			RelativeFileName = RelativeProjectPath;
			RelativeFileName.AppendChars(*CookedFile + CookedProjectDirLen, FileNameLen);
		}
		return RelativeFileName;
	};

	for (const FContainerSourceSpec& ContainerSource : Arguments.Containers)
	{
		FContainerTargetSpec* ContainerTarget = FindOrAddContainer(ContainerSource.Name, ContainerTargets, ContainerTargetMap);
		ContainerTarget->OutputPath = ContainerSource.OutputPath;
		ContainerTarget->bGenerateDiffPatch = ContainerSource.bGenerateDiffPatch;

		for (const FString& PatchSourceContainerFile : ContainerSource.PatchSourceContainerFiles)
		{
			FIoStoreEnvironment PatchSourceEnvironment;
			PatchSourceEnvironment.InitializeFileEnvironment(FPaths::ChangeExtension(PatchSourceContainerFile, TEXT("")));
			TUniquePtr<FIoStoreReader> PatchSourceReader(new FIoStoreReader());
			FIoStatus Status = PatchSourceReader->Initialize(PatchSourceEnvironment);
			if (Status.IsOk())
			{
				UE_LOG(LogIoStore, Display, TEXT("Loaded patch source container '%s'"), *PatchSourceContainerFile);
				ContainerTarget->PatchSourceReaders.Add(MoveTemp(PatchSourceReader));
			}
			else
			{
				UE_LOG(LogIoStore, Warning, TEXT("Failed loading patch source container '%s' [%s]"), *PatchSourceContainerFile, *Status.ToString())
			}
		}
		
		ContainerTarget->LocalNameMapBuilder.SetNameMapType(FMappedName::EType::Container);
		{
			IOSTORE_CPU_SCOPE(ProcessSourceFiles);
			for (const FContainerSourceFile& SourceFile : ContainerSource.SourceFiles)
			{
				const FCookedFileStatData* OriginalCookedFileStatData = Arguments.CookedFileStatMap.Find(SourceFile.NormalizedPath);
				if (!OriginalCookedFileStatData)
				{
					UE_LOG(LogIoStore, Warning, TEXT("File not found: '%s'"), *SourceFile.NormalizedPath);
					continue;
				}
				const FCookedFileStatData* CookedFileStatData = OriginalCookedFileStatData;
				FString NormalizedSourcePath = SourceFile.NormalizedPath;
				if (CookedFileStatData->FileType == FCookedFileStatData::PackageHeader)
				{
					NormalizedSourcePath = FPaths::ChangeExtension(SourceFile.NormalizedPath, TEXT(".uexp"));
					CookedFileStatData = Arguments.CookedFileStatMap.Find(NormalizedSourcePath);
					if (!CookedFileStatData)
					{
						UE_LOG(LogIoStore, Warning, TEXT("File not found: '%s'"), *NormalizedSourcePath);
						continue;
					}
				}

				FString RelativeFileName = ConvertCookedPathToRelativePath(SourceFile.NormalizedPath);
				FPackage* Package = nullptr;
				const bool bIsMemoryMappedBulkData = CookedFileStatData->FileExt == FCookedFileStatData::EFileExt::UMappedBulk;

				if (bIsMemoryMappedBulkData)
				{
					FString TmpFileName = FString(RelativeFileName.Len() - 8, GetData(RelativeFileName)) + TEXT(".ubulk");
					Package = FindOrAddPackage(*TmpFileName, Packages, PackageMap, PackageGlobalIdMap);
				}
				else
				{
					Package = FindOrAddPackage(*RelativeFileName, Packages, PackageMap, PackageGlobalIdMap);
				}

				if (Package)
				{
					FContainerTargetFile& TargetFile = ContainerTarget->TargetFiles.AddDefaulted_GetRef();
					TargetFile.ContainerTarget = ContainerTarget;
					TargetFile.SourceSize = uint64(CookedFileStatData->FileSize);
					TargetFile.NormalizedSourcePath = NormalizedSourcePath;
					TargetFile.TargetPath = MoveTemp(RelativeFileName);
					TargetFile.Package = Package;
					if (SourceFile.bNeedsCompression)
					{
						ContainerTarget->bIsCompressed = true;
					}
					else
					{
						TargetFile.bForceUncompressed = true;
					}

					if (CookedFileStatData->FileType == FCookedFileStatData::BulkData)
					{
						TargetFile.bIsBulkData = true;
						if (CookedFileStatData->FileExt == FCookedFileStatData::UPtnl)
						{
							TargetFile.bIsOptionalBulkData = true;
							TargetFile.ChunkId = CreateChunkIdForBulkData(Package->GlobalPackageId, BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType::Optional), *TargetFile.TargetPath);
						}
						else if (CookedFileStatData->FileExt == FCookedFileStatData::UMappedBulk)
						{
							TargetFile.bIsMemoryMappedBulkData = true;
							TargetFile.bForceUncompressed = true;
							TargetFile.ChunkId = CreateChunkIdForBulkData(Package->GlobalPackageId, BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped), *TargetFile.TargetPath);
						}
						else
						{
							TargetFile.ChunkId = CreateChunkIdForBulkData(Package->GlobalPackageId, BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType::Normal), *TargetFile.TargetPath);
						}
						if (Package->FileName.IsEmpty())
						{
							Package->FileName = FPaths::ChangeExtension(SourceFile.NormalizedPath, TEXT(".uasset"));
						}
					}
					else
					{
						check(CookedFileStatData->FileType == FCookedFileStatData::PackageData);
						Package->FileName = SourceFile.NormalizedPath; // .uasset path
						Package->UAssetSize = OriginalCookedFileStatData->FileSize;
						Package->UExpSize = CookedFileStatData->FileSize;
						TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, EIoChunkType::ExportBundleData, *TargetFile.TargetPath);
					}
				}
			}

			if (ContainerTarget->bUseLocalNameMap)
			{
				ContainerTarget->NameMapBuilder = &ContainerTarget->LocalNameMapBuilder;
			}
			else
			{
				// Clear the local container map in case this was used in a previous release
				ContainerTarget->LocalNameMapBuilder.Empty();
				ContainerTarget->NameMapBuilder = &GlobalNameMapBuilder;
			}
		}
	}

	Algo::Sort(Packages, [](const FPackage* A, const FPackage* B)
	{
		return A->GlobalPackageId < B->GlobalPackageId;
	});
};

int32 CreateTarget(const FIoStoreArguments& Arguments, const FIoStoreWriterSettings& GeneralIoWriterSettings)
{
	TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);

	FPackageStoreBulkDataManifest BulkDataManifest(FString(Arguments.CookedDir) / FApp::GetProjectName());
	if (!BulkDataManifest.Load())
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed to load Bulk Data manifest %s"), *BulkDataManifest.GetFilename());
	}
	else
	{
		UE_LOG(LogIoStore, Display, TEXT("Loaded Bulk Data manifest '%s'"), *BulkDataManifest.GetFilename());
	}
	
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.CreateOutputFile(CookedDir);
#endif

	FNameMapBuilder GlobalNameMapBuilder;
	FPackageAssetData PackageAssetData;
	FGlobalPackageData GlobalPackageData;
	FExportGraph ExportGraph;

	TArray<FPackage*> Packages;
	FPackageMap PackageMap;
	FPackageGlobalIdMap PackageGlobalIdMap;

#if OUTPUT_DEBUG_PACKAGE_HASHES
	TMap<FName, FPackageHashes> PreviousBuildPackageHashes;
#endif

	TArray<FContainerTargetSpec*> ContainerTargets;
	TMap<FName, FContainerTargetSpec*> ContainerTargetMap;
	UE_LOG(LogIoStore, Display, TEXT("Creating container targets..."));
	{
		IOSTORE_CPU_SCOPE(CreateContainerTargets);

		if (!Arguments.BasedOnReleaseVersionDir.IsEmpty())
		{
			LoadReleaseVersionMeta(*Arguments.BasedOnReleaseVersionDir, GlobalNameMapBuilder, PackageGlobalIdMap, GlobalPackageData.Imports, ContainerTargets, ContainerTargetMap);
#if OUTPUT_DEBUG_PACKAGE_HASHES
			FString PackageHashesOutputPath = FPaths::Combine(*Arguments.BasedOnReleaseVersionDir, TEXT("iodispatcher.upackagehashes"));
			TUniquePtr<FArchive> PackageHashesArchive(IFileManager::Get().CreateFileReader(*PackageHashesOutputPath));
			if (PackageHashesArchive)
			{
				int32 HashesCount;
				(*PackageHashesArchive) << HashesCount;
				for (int32 Index = 0; Index < HashesCount; ++Index)
				{
					FNameEntrySerialized NameEntrySerialized(ENAME_LinkerConstructor);
					(*PackageHashesArchive) << NameEntrySerialized;
					int32 NameNumber;
					(*PackageHashesArchive) << NameNumber;
					FName Name(NameEntrySerialized, NameNumber);
					FPackageHashes& Hashes = PreviousBuildPackageHashes.Add(Name);
					(*PackageHashesArchive) << Hashes.UAssetHash;
					(*PackageHashesArchive) << Hashes.UExpHash;
					(*PackageHashesArchive) << Hashes.ExportBundleHash;
				}
			}
#endif
		}

		InitializeContainerTargetsAndPackages(Arguments, Packages, PackageMap, PackageGlobalIdMap, ContainerTargets, ContainerTargetMap, GlobalNameMapBuilder);
	}

	ParsePackageAssets(Packages, PackageAssetData, ExportGraph);
	CreateGlobalImportsAndExports(Packages, PackageMap, PackageAssetData, GlobalPackageData, ExportGraph);

	FGlobalPackageImports& PackageImports = GlobalPackageData.Imports.PackageImports;
	FGlobalScriptImports& ScriptImports = GlobalPackageData.Imports.ScriptImports;
	TArray<FExportData>& GlobalExports = GlobalPackageData.Exports.Objects;

	// Mapped import and exports are required before processing localization, and preload/postload arcs
	MapImportedPackages(GlobalPackageData.Imports, Packages);
	MapExportEntryIndices(PackageAssetData.ObjectExports, GlobalExports, Packages);

	FSourceToLocalizedPackageMultimap SourceToLocalizedPackageMap;
	FCulturePackageMap CulturePackageMap;

	ProcessLocalizedPackages(
		Packages, PackageMap, GlobalPackageData.Imports, GlobalExports,
		CulturePackageMap, SourceToLocalizedPackageMap);

	const int32 CircularChainCount = AddPostLoadDependencies(Packages);
	
	AddPreloadDependencies(
		PackageAssetData,
		GlobalPackageData,
		SourceToLocalizedPackageMap,
		ExportGraph,
		Packages);

	BuildBundles(ExportGraph, Packages);

	{
		IOSTORE_CPU_SCOPE(BuildContainerNameMaps);
		UE_LOG(LogIoStore, Display, TEXT("Creating container local name map(s)..."));

		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			BuildContainerNameMap(*ContainerTarget);
		}
	}

	{
		IOSTORE_CPU_SCOPE(FinalizePackageHeaders);
		UE_LOG(LogIoStore, Display, TEXT("Finalizing package headers..."));

		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			FinalizePackageHeaders(
				*ContainerTarget,
				PackageAssetData.ObjectExports,
				GlobalPackageData.Exports.Objects,
				GlobalPackageData.Imports.ObjectsByFullName);

			const FNameMapBuilder& NameMapBuilder = *ContainerTarget->NameMapBuilder;
			SaveNameBatch(ContainerTarget->LocalNameMapBuilder.GetNameMap(), ContainerTarget->Header.Names, ContainerTarget->Header.NameHashes);
			ContainerTarget->Header.PackageIds.Reserve(ContainerTarget->TargetFiles.Num());
			ContainerTarget->Header.PackageNames.Reserve(ContainerTarget->TargetFiles.Num());
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				if (!TargetFile.bIsBulkData)
				{
					ContainerTarget->Header.PackageIds.Add(TargetFile.Package->GlobalPackageId);
					ContainerTarget->Header.PackageNames.Add(NameMapBuilder.MapName(TargetFile.Package->Name));
				}
			}
		}
	}

	FLargeMemoryWriter GlobalMetaArchive(0, true);
	FLargeMemoryWriter InitialLoadArchive(0, true);
	int32 StoreTocByteCount = 0;
	int32 StoreDataByteCount = 0;
	int32 GlobalImportNamesByteCount = 0;

	{
		IOSTORE_CPU_SCOPE(SerializeGlobalMetaData);
		UE_LOG(LogIoStore, Display, TEXT("Serializing global meta data"));

		FLargeMemoryWriter StoreTocArchive(0, true);
		FLargeMemoryWriter StoreDataArchive(0, true);

		SerializePackageStoreEntries(
			Packages,
			StoreTocArchive,
			StoreDataArchive);

		SerializeInitialLoad(
			GlobalNameMapBuilder,
			ScriptImports,
			InitialLoadArchive);

		StoreTocByteCount = StoreTocArchive.TotalSize();
		StoreDataByteCount = StoreDataArchive.TotalSize();

		int32 NumPackages = Packages.Num();
		int32 PackageImportCount = PackageImports.Num();
		int32 StoreByteCount = StoreTocByteCount + StoreDataByteCount;

		GlobalMetaArchive << NumPackages;
		GlobalMetaArchive << PackageImportCount;
		GlobalMetaArchive << StoreByteCount;
		GlobalMetaArchive.Serialize(StoreTocArchive.GetData(), StoreTocByteCount);
		GlobalMetaArchive.Serialize(StoreDataArchive.GetData(), StoreDataByteCount);

		GlobalMetaArchive << CulturePackageMap;
	}

	UE_LOG(LogIoStore, Display, TEXT("Calculating hashes"));
	{
		IOSTORE_CPU_SCOPE(CalculateHashes);

		int32 TotalFileCount = 0;
		for (const FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			TotalFileCount += ContainerTarget->TargetFiles.Num();
		}
		TArray<FContainerTargetFile*> AllTargetFiles;
		AllTargetFiles.Reserve(TotalFileCount);
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				AllTargetFiles.Add(&TargetFile);
			}
		}
		TArray<FObjectExport>& ObjectExports = PackageAssetData.ObjectExports;
		TAtomic<uint64> CurrentFileIndex{ 0 };
		ParallelFor(TotalFileCount, [&AllTargetFiles, &ObjectExports, &CurrentFileIndex](int32 Index)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HashFile);
			FContainerTargetFile* TargetFile = AllTargetFiles[Index];
			IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*TargetFile->NormalizedSourcePath);
			check(FileHandle);
			FIoBuffer IoBuffer(TargetFile->SourceSize);
			bool bSuccess = FileHandle->Read(IoBuffer.Data(), TargetFile->SourceSize);
			check(bSuccess);
			delete FileHandle;
			if (!TargetFile->bIsBulkData)
			{
				IoBuffer = CreateExportBundleBuffer(*TargetFile, ObjectExports, IoBuffer);
				TargetFile->TargetSize = IoBuffer.DataSize();
			}
			else
			{
				TargetFile->TargetSize = TargetFile->SourceSize;
			}
			TargetFile->ChunkHash = FIoChunkHash::HashBuffer(IoBuffer.Data(), IoBuffer.DataSize());
#if OUTPUT_DEBUG_PACKAGE_HASHES
			if (!TargetFile->bIsBulkData)
			{
				TargetFile->Package->Hashes.ExportBundleHash = TargetFile->ChunkHash;
			}
#endif
			uint64 LocalFileIndex = CurrentFileIndex.IncrementExchange() + 1;
			UE_CLOG(LocalFileIndex % 1000 == 0, LogIoStore, Display, TEXT("Hashing %d/%d: '%s'"), LocalFileIndex, AllTargetFiles.Num(), *TargetFile->NormalizedSourcePath);
		}, EParallelForFlags::Unbalanced);

#if OUTPUT_DEBUG_PACKAGE_HASHES
		ParallelFor(Packages.Num(), [&Packages](int32 Index)
		{
			FPackage* Package = Packages[Index];
			
			FString FileName = Package->FileName;
			IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FileName);
			check(FileHandle);
			FIoBuffer IoBuffer(FileHandle->Size());
			bool bSuccess = FileHandle->Read(IoBuffer.Data(), IoBuffer.DataSize());
			check(bSuccess);
			delete FileHandle;
			FSHA1::HashBuffer(IoBuffer.Data(), IoBuffer.DataSize(), Package->Hashes.UAssetHash.Hash);
			
			FileName = FPaths::ChangeExtension(FileName, TEXT(".uexp"));
			FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FileName);
			check(FileHandle);
			IoBuffer = FIoBuffer(FileHandle->Size());
			bSuccess = FileHandle->Read(IoBuffer.Data(), IoBuffer.DataSize());
			check(bSuccess);
			delete FileHandle;
			FSHA1::HashBuffer(IoBuffer.Data(), IoBuffer.DataSize(), Package->Hashes.UExpHash.Hash);

		}, EParallelForFlags::Unbalanced);
#endif
	}

	{
		IOSTORE_CPU_SCOPE(MapAdditionalBulkDataChunkIds);
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				if (TargetFile.bIsBulkData)
				{
					MapAdditionalBulkDataChunks(TargetFile, BulkDataManifest);
				}
			}
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Creating disk layout..."));
	CreateDiskLayout(ContainerTargets, Packages, Arguments.GameOrderMap, Arguments.CookerOrderMap, GeneralIoWriterSettings.CompressionBlockSize, Arguments.MemoryMappingAlignment);

	UE_LOG(LogIoStore, Display, TEXT("Serializing container(s)..."));
	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext(new FIoStoreWriterContext());
	TArray<FIoStoreWriter*> IoStoreWriters;
	FIoStoreEnvironment GlobalIoStoreEnv;
	FIoStoreWriter* GlobalIoStoreWriter;
	{
		IOSTORE_CPU_SCOPE(InitializeIoStoreWriters);
		GlobalIoStoreEnv.InitializeFileEnvironment(*Arguments.GlobalContainerPath);
		GlobalIoStoreWriter = new FIoStoreWriter(GlobalIoStoreEnv, FIoContainerId::FromIndex(0));
		IoStoreWriters.Add(GlobalIoStoreWriter);
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			check(ContainerTarget->Header.ContainerId.IsValid());
			if (!ContainerTarget->OutputPath.IsEmpty())
			{
				ContainerTarget->IoStoreEnv.Reset(new FIoStoreEnvironment());
				ContainerTarget->IoStoreEnv->InitializeFileEnvironment(ContainerTarget->OutputPath);
				ContainerTarget->IoStoreWriter = new FIoStoreWriter(*ContainerTarget->IoStoreEnv, ContainerTarget->Header.ContainerId);
				IoStoreWriters.Add(ContainerTarget->IoStoreWriter);
			}
		}
		FIoStatus IoStatus = IoStoreWriterContext->Initialize(GeneralIoWriterSettings);
		check(IoStatus.IsOk());
		IoStatus = GlobalIoStoreWriter->Initialize(*IoStoreWriterContext, /* bIsCompressed */ false);
		check(IoStatus.IsOk());
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			if (ContainerTarget->IoStoreWriter && ContainerTarget->IoStoreWriter != GlobalIoStoreWriter)
			{
				IoStatus = ContainerTarget->IoStoreWriter->Initialize(*IoStoreWriterContext, ContainerTarget->bIsCompressed);
				check(IoStatus.IsOk());
			}
		}
	}
	{
		IOSTORE_CPU_SCOPE(SerializeContainers);
		
		struct FReadFileTask
		{
			const FContainerTargetFile* ContainerTargetFile = nullptr;
			FIoStoreWriter* IoStoreWriter = nullptr;
			IAsyncReadFileHandle* FileHandle = nullptr;
			IAsyncReadRequest* ReadRequest = nullptr;
			FIoBuffer IoBuffer;
			TAtomic<bool> bStarted{ false };
		};

		int32 TotalFileCount = 0;
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			check(ContainerTarget->IoStoreWriter || ContainerTarget->TargetFiles.Num() == 0);
			TotalFileCount += ContainerTarget->TargetFiles.Num();
		}

		TArray<uint8> ContainerHeaderPadding;
		if (GeneralIoWriterSettings.CompressionBlockSize > 0)
		{
			ContainerHeaderPadding.AddZeroed(GeneralIoWriterSettings.CompressionBlockSize);
		}

		TArray<FReadFileTask> ReadFileTasks;
		ReadFileTasks.SetNum(TotalFileCount);
		int32 ReadFileTaskIndex = 0;
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			if (!ContainerTarget->IoStoreWriter)
			{
				continue;
			}
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				FReadFileTask& ReadFileTask = ReadFileTasks[ReadFileTaskIndex++];
				ReadFileTask.ContainerTargetFile = &TargetFile;
				ReadFileTask.IoStoreWriter = ContainerTarget->IoStoreWriter;
			}

			FLargeMemoryWriter Ar(0, true);
			Ar << ContainerTarget->Header;

			if (GeneralIoWriterSettings.CompressionBlockSize > 0)
			{
				const uint64 RemainingInBlock = Align(Ar.TotalSize(), GeneralIoWriterSettings.CompressionBlockSize) - Ar.TotalSize();
				if (RemainingInBlock > 0)
				{
					Ar.Serialize(ContainerHeaderPadding.GetData(), RemainingInBlock);
				}
			}

			FIoWriteOptions WriteOptions;
			WriteOptions.DebugName = TEXT("ContainerHeader");
			FIoStatus Status = ContainerTarget->IoStoreWriter->Append(
				CreateIoChunkId(0, ContainerTarget->Header.ContainerId.ToIndex(), EIoChunkType::ContainerHeader), 
				FIoBuffer(FIoBuffer::Wrap, Ar.GetData(), Ar.TotalSize()), WriteOptions);

			UE_CLOG(!Status.IsOk(), LogIoStore, Error, TEXT("Failed to serialize container header"));
		}

		FEvent* TaskStartedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		FEvent* TaskFinishedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		
		TAtomic<uint64> BufferMemoryAllocated{ 0 };
		TAtomic<uint64> OpenFileHandlesCount{ 0 };

		TArray<FObjectExport>& ObjectExports = PackageAssetData.ObjectExports;
		TFuture<void> ReaderTask = Async(EAsyncExecution::ThreadPool, [&ReadFileTasks, &BufferMemoryAllocated, &OpenFileHandlesCount, TaskStartedEvent, TaskFinishedEvent, &BulkDataManifest, &ObjectExports, &GeneralIoWriterSettings]()
		{
			IOSTORE_CPU_SCOPE(ReadContainerSourceFilesTask);
			for (FReadFileTask& ReadFileTask : ReadFileTasks)
			{
				while (!ReadFileTask.bStarted)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(WaitUntilStarted);
					TaskStartedEvent->Wait();
				}
				ReadFileTask.ReadRequest->WaitCompletion();
				delete ReadFileTask.ReadRequest;
				delete ReadFileTask.FileHandle;

				--OpenFileHandlesCount;

				const FContainerTargetFile& TargetFile = *ReadFileTask.ContainerTargetFile;

				uint64 BufferSize = ReadFileTask.IoBuffer.DataSize();
				if (!TargetFile.bIsBulkData)
				{
					ReadFileTask.IoBuffer = CreateExportBundleBuffer(TargetFile, ObjectExports, ReadFileTask.IoBuffer);
				}

				if (TargetFile.Padding)
				{
					ReadFileTask.IoStoreWriter->AppendPadding(TargetFile.Padding);
				}
				FIoWriteOptions WriteOptions;
				WriteOptions.DebugName = *TargetFile.TargetPath;
				WriteOptions.bForceUncompressed = TargetFile.bForceUncompressed;
				WriteOptions.Alignment = TargetFile.Alignment;
				FIoStatus Status = ReadFileTask.IoStoreWriter->Append(TargetFile.ChunkId, TargetFile.ChunkHash, ReadFileTask.IoBuffer, WriteOptions);
				UE_CLOG(!Status.IsOk(), LogIoStore, Fatal, TEXT("Failed to append chunk to container file due to '%s'"), *Status.ToString());

				for (const FContainerTargetFilePartialMapping& PartialMapping : TargetFile.PartialMappings)
				{
					const FIoStatus PartialResult = ReadFileTask.IoStoreWriter->MapPartialRange(TargetFile.ChunkId, PartialMapping.Offset, PartialMapping.Length, PartialMapping.PartialChunkId);
					if (!PartialResult.IsOk())
					{
						UE_LOG(LogIoStore, Warning, TEXT("Failed to map partial range for '%s' due to: %s"), *TargetFile.Package->FileName, *PartialResult.ToString());
					}
				}

				ReadFileTask.IoBuffer = FIoBuffer();
				BufferMemoryAllocated -= BufferSize;
				TaskFinishedEvent->Trigger();
			}
		});

		uint64 CurrentFileIndex = 0;
		const uint64 MaxReadFileBufferSize = 2ull << 30;
		const uint64 MaxReadFileHandlesCount = 65536;
		for (FReadFileTask& ReadFileTask : ReadFileTasks)
		{
			while (BufferMemoryAllocated > MaxReadFileBufferSize)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(WaitForBufferMemory);
				TaskFinishedEvent->Wait();
			}
			while (OpenFileHandlesCount > MaxReadFileHandlesCount)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(WaitForFileHandle);
				TaskFinishedEvent->Wait();
			}
			BufferMemoryAllocated += ReadFileTask.ContainerTargetFile->SourceSize;
			ReadFileTask.IoBuffer = FIoBuffer(ReadFileTask.ContainerTargetFile->SourceSize);
			++OpenFileHandlesCount;
			ReadFileTask.FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*ReadFileTask.ContainerTargetFile->NormalizedSourcePath);
			ReadFileTask.ReadRequest = ReadFileTask.FileHandle->ReadRequest(0, ReadFileTask.ContainerTargetFile->SourceSize, AIOP_Normal, nullptr, ReadFileTask.IoBuffer.Data());
			ReadFileTask.bStarted = true;
			TaskStartedEvent->Trigger();

			++CurrentFileIndex;
			UE_CLOG(CurrentFileIndex % 1000 == 0, LogIoStore, Display, TEXT("Serializing %d/%d: '%s'"), CurrentFileIndex, ReadFileTasks.Num(), *ReadFileTask.ContainerTargetFile->NormalizedSourcePath);
		}
		{
			IOSTORE_CPU_SCOPE(CompleteReads);
			ReaderTask.Wait();
		}
		FPlatformProcess::ReturnSynchEventToPool(TaskFinishedEvent);
		FPlatformProcess::ReturnSynchEventToPool(TaskStartedEvent);
	}

	{
		UE_LOG(LogIoStore, Display, TEXT("Saving global meta data to container file"));
		FIoWriteOptions WriteOptions;
		WriteOptions.DebugName = TEXT("LoaderGlobalMeta");
		const FIoStatus Status = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalMeta), FIoBuffer(FIoBuffer::Wrap, GlobalMetaArchive.GetData(), GlobalMetaArchive.TotalSize()), WriteOptions);
		if (!Status.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to save global meta data to container file"));
		}
	}

	{
		UE_LOG(LogIoStore, Display, TEXT("Saving initial load meta data to container file"));
		FIoWriteOptions WriteOptions;
		WriteOptions.DebugName = TEXT("LoaderInitialLoadMeta");
		const FIoStatus Status = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoBuffer(FIoBuffer::Wrap, InitialLoadArchive.GetData(), InitialLoadArchive.TotalSize()), WriteOptions);
		if (!Status.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to save initial load meta data to container file"));
		}
	}

	uint64 GlobalNamesMB = 0;
	uint64 GlobalNameHashesMB = 0;
	{
		IOSTORE_CPU_SCOPE(SerializeGlobalNameMap);

		UE_LOG(LogIoStore, Display, TEXT("Saving global name map to container file"));

		TArray<uint8> Names;
		TArray<uint8> Hashes;
		SaveNameBatch(GlobalNameMapBuilder.GetNameMap(), /* out */ Names, /* out */ Hashes);

		GlobalNamesMB = Names.Num() >> 20;
		GlobalNameHashesMB = Hashes.Num() >> 20;

		FIoWriteOptions WriteOptions;
		WriteOptions.DebugName = TEXT("LoaderGlobalNames");
		FIoStatus NameStatus = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames), 
													 FIoBuffer(FIoBuffer::Wrap, Names.GetData(), Names.Num()), WriteOptions);
		WriteOptions.DebugName = TEXT("LoaderGlobalNameHashes");
		FIoStatus HashStatus = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes),
													 FIoBuffer(FIoBuffer::Wrap, Hashes.GetData(), Hashes.Num()), WriteOptions);
		
		if (!NameStatus.IsOk() || !HashStatus.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to save global name map to container file"));
		}

#if OUTPUT_NAMEMAP_CSV
		NameMapBuilder.SaveCsv(OutputDir / TEXT("Container.namemap.csv"));
#endif
	}

	TArray<FIoStoreWriterResult> IoStoreWriterResults;
	IoStoreWriterResults.Reserve(IoStoreWriters.Num());
	for (FIoStoreWriter* IoStoreWriter : IoStoreWriters)
	{
		IoStoreWriterResults.Emplace(IoStoreWriter->Flush().ConsumeValueOrDie());
		delete IoStoreWriter;
	}
	IoStoreWriters.Empty();

	if (!Arguments.OutputReleaseVersionDir.IsEmpty())
	{
		SaveReleaseVersionMeta(*Arguments.OutputReleaseVersionDir, GlobalNameMapBuilder, PackageGlobalIdMap, GlobalPackageData.Imports, ContainerTargets);
#if OUTPUT_DEBUG_PACKAGE_HASHES
		FString PackageHashesOutputPath = FPaths::Combine(*Arguments.OutputReleaseVersionDir, TEXT("iodispatcher.upackagehashes"));
		TUniquePtr<FArchive> PackageHashesArchive(IFileManager::Get().CreateFileWriter(*PackageHashesOutputPath));

		int32 PackageCount = Packages.Num();
		(*PackageHashesArchive) << PackageCount;
		for (FPackage* Package : Packages)
		{
			const FNameEntry* NameEntry = FName::GetEntry(Package->Name.GetComparisonIndex());
			NameEntry->Write(*PackageHashesArchive);
			int32 NameNumber = Package->Name.GetNumber();
			(*PackageHashesArchive) << NameNumber;
			(*PackageHashesArchive) << Package->Hashes.UAssetHash;
			(*PackageHashesArchive) << Package->Hashes.UExpHash;
			(*PackageHashesArchive) << Package->Hashes.ExportBundleHash;
		}
#endif
	}
#if OUTPUT_DEBUG_PACKAGE_HASHES
	{
		uint64 AddedCount = 0;
		uint64 DeletedCount = 0;
		uint64 ModifiedCountUAssetOrUExpCount = 0;
		uint64 ModifiedUAssetCount = 0;
		uint64 ModifiedUAssetSize = 0;
		uint64 ModifiedUExpCount = 0;
		uint64 ModifiedUExpSize = 0;
		uint64 ModifiedExportBundlesCount = 0;
		uint64 ModifiedExportBundlesSize = 0;
		for (FPackage* Package : Packages)
		{
			const FPackageHashes* PreviousHashes = PreviousBuildPackageHashes.Find(Package->Name);
			if (PreviousHashes)
			{
				bool bModified = false;
				if (PreviousHashes->UAssetHash != Package->Hashes.UAssetHash)
				{
					++ModifiedUAssetCount;
					bModified = true;
					
					ModifiedUAssetSize += Package->UAssetSize;
				}
				if (PreviousHashes->UExpHash != Package->Hashes.UExpHash)
				{
					++ModifiedUExpCount;
					bModified = true;

					ModifiedUExpSize += Package->UExpSize;
				}
				if (PreviousHashes->ExportBundleHash != Package->Hashes.ExportBundleHash)
				{
					++ModifiedExportBundlesCount;
					ModifiedExportBundlesSize += Package->ExportBundlesHeaderSize + Package->ExportsSerialSize;
				}
				if (bModified)
				{
					++ModifiedCountUAssetOrUExpCount;
				}
			}
			else
			{
				++AddedCount;
			}
		}
		for (const auto& KV : PreviousBuildPackageHashes)
		{
			if (!PackageMap.Contains(KV.Key))
			{
				++DeletedCount;
			}
		}
		UE_LOG(LogIoStore, Display, TEXT("Added packages: %d"), AddedCount);
		UE_LOG(LogIoStore, Display, TEXT("Deleted packages: %d"), DeletedCount);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (export bundle): %d, %fMB"), ModifiedExportBundlesCount, ModifiedExportBundlesSize / 1024.0 / 1024.0);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (uasset|uexp): %d, %fMB"), ModifiedCountUAssetOrUExpCount, (ModifiedUAssetSize + ModifiedUExpSize) / 1024.0 / 1024.0);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (uasset): %d, %fMB"), ModifiedUAssetCount, ModifiedUAssetSize / 1024.0 / 1024.0);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (uexp): %d, %fMB"), ModifiedUExpCount, ModifiedUExpSize / 1024.0 / 1024.0);
	}
#endif

	IOSTORE_CPU_SCOPE(CalculateStats);

	UE_LOG(LogIoStore, Display, TEXT("Calculating stats..."));
	uint64 UExpSize = 0;
	uint64 UAssetSize = 0;
	uint64 SummarySize = 0;
	uint64 UGraphSize = 0;
	uint64 ImportMapSize = 0;
	uint64 ExportMapSize = 0;
	uint64 NameMapSize = 0;
	uint64 NameMapCount = 0;
	uint64 PackageSummarySize = Packages.Num() * sizeof(FPackageSummary);
	uint64 ImportedPackagesCount = 0;
	uint64 PublicExportsCount = 0;
	uint64 ExportBundlesMetaCount = 0;
	uint64 InitialLoadSize = InitialLoadArchive.Tell();
	uint64 CircularPackagesCount = 0;
	uint64 TotalInternalArcCount = 0;
	uint64 TotalExternalArcCount = 0;
	uint64 NameCount = 0;

	uint64 PackagesWithoutImportDependenciesCount = 0;
	uint64 PackagesWithoutPreloadDependenciesCount = 0;
	uint64 BundleCount = 0;
	uint64 BundleEntryCount = 0;

	uint64 PackageHeaderSize = 0;

	for (auto& PackageKV : PackageMap)
	{
		FPackage& Package = *PackageKV.Value;

		UExpSize += Package.UExpSize;
		UAssetSize += Package.UAssetSize;
		SummarySize += Package.SummarySize;
		UGraphSize += Package.UGraphSize;
		ImportMapSize += Package.ImportMapSize;
		ExportMapSize += Package.ExportMapSize;
		NameMapSize += Package.NameMapSize;
		NameMapCount += Package.NameMap.Num();
		CircularPackagesCount += Package.bHasCircularImportDependencies;
		TotalInternalArcCount += Package.InternalArcs.Num();
		ImportedPackagesCount += Package.ImportedPackagesSerializeCount;
		PublicExportsCount += Package.PublicExports.Num();
		ExportBundlesMetaCount += Package.ExportBundles.Num();
		NameCount += Package.NameMap.Num();
		PackagesWithoutPreloadDependenciesCount += Package.ImportedPreloadPackages.Num() == 0;
		PackagesWithoutImportDependenciesCount += Package.ImportedPackages.Num() == 0;

		for (auto& KV : Package.ExternalArcs)
		{
			TArray<FArc>& Arcs = KV.Value;
			TotalExternalArcCount += Arcs.Num();
		}

		for (FExportBundle& Bundle : Package.ExportBundles)
		{
			++BundleCount;
			BundleEntryCount += Bundle.Nodes.Num();
		}
	}

	PackageHeaderSize = PackageSummarySize + NameMapSize + ImportMapSize + ExportMapSize + UGraphSize;

	UE_LOG(LogIoStore, Display, TEXT("--------------------------------------------- IoStore Summary -----------------------------------------------"));

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("%-4s %-35s %15s %15s %15s %25s"), TEXT("ID"), TEXT("Container"), TEXT("TOC Size (KB)"), TEXT("TOC Entries"), TEXT("Size (MB)"), TEXT("Compressed (MB)"));
	UE_LOG(LogIoStore, Display, TEXT("-------------------------------------------------------------------------------------------------------------"));
	int64 TotalPaddingSize = 0;
	for (const FIoStoreWriterResult& Result : IoStoreWriterResults)
	{
		const double Compression = Result.CompressionMethod == NAME_None
			? 0.0
			: (double(Result.UncompressedContainerSize - Result.CompressedContainerSize) / double(Result.UncompressedContainerSize)) * 100.0;

		UE_LOG(LogIoStore, Display, TEXT("%-4d %-35s %15.2lf %15d %15.2lf %25s"),
			Result.ContainerId.ToIndex(),
			*Result.ContainerName,
			(double)Result.TocSize / 1024.0,
			Result.TocEntryCount,
			(double)Result.UncompressedContainerSize / 1024.0 / 1024.0,
			*FString::Printf(TEXT("%.2lf (%.2lf%% %s)"),
				(double)Result.CompressedContainerSize / 1024.0 / 1024.0,
				Compression,
				*Result.CompressionMethod.ToString()));
		TotalPaddingSize += Result.PaddingSize;
	}
	UE_LOG(LogIoStore, Display, TEXT("Compression block padding: %8.2f MB"), TotalPaddingSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("Packages: %8d total, %d circular dependencies, %d no preload dependencies, %d no import dependencies"),
		PackageMap.Num(), CircularPackagesCount, PackagesWithoutPreloadDependenciesCount, PackagesWithoutImportDependenciesCount);
	UE_LOG(LogIoStore, Display, TEXT("Bundles:  %8d total, %d entries, %d export objects"), BundleCount, BundleEntryCount, GlobalPackageData.Exports.Objects.Num());

	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalNames, %d unique names"), (double)GlobalNamesMB, GlobalNameMapBuilder.GetNameMap().Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalNameHashes"), (double)GlobalNameHashesMB);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalPackageStoreToc"), (double)StoreTocByteCount / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalPackageStoreData, %d imported packages, %d public exports, %d export bundles"),
		(double)StoreDataByteCount / 1024.0 / 1024.0,
		ImportedPackagesCount,
		PublicExportsCount,
		ExportBundlesMetaCount);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageImportNames, %d imports"),
		(double)GlobalImportNamesByteCount / 1024.0 / 1024.0, PackageImports.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB InitialLoadData, %d script imports"), (double)InitialLoadSize / 1024.0 / 1024.0, ScriptImports.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageHeader, %d packages"), (double)PackageHeaderSize / 1024.0 / 1024.0, Packages.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageSummary"), (double)PackageSummarySize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageNameMap, %d indices"), (double)NameMapSize / 1024.0 / 1024.0, NameMapCount);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageImportMap"), (double)ImportMapSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageExportMap"), (double)ExportMapSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageArcs, %d internal arcs, %d external arcs, %d circular packages (%d chains)"),
		(double)UGraphSize / 1024.0 / 1024.0, TotalInternalArcCount, TotalExternalArcCount, CircularPackagesCount, CircularChainCount);

	return 0;
}

static bool ParsePakResponseFile(const TCHAR* FilePath, TArray<FContainerSourceFile>& OutFiles)
{
	TArray<FString> ResponseFileContents;
	if (!FFileHelper::LoadFileToStringArray(ResponseFileContents, FilePath))
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read response file '%s'."), *FilePath);
		return false;
	}

	for (const FString& ResponseLine : ResponseFileContents)
	{
		TArray<FString> SourceAndDest;
		TArray<FString> Switches;

		FString NextToken;
		const TCHAR* ResponseLinePtr = *ResponseLine;
		while (FParse::Token(ResponseLinePtr, NextToken, false))
		{
			if ((**NextToken == TCHAR('-')))
			{
				new(Switches) FString(NextToken.Mid(1));
			}
			else
			{
				new(SourceAndDest) FString(NextToken);
			}
		}

		if (SourceAndDest.Num() == 0)
		{
			continue;
		}

		if (SourceAndDest.Num() != 2)
		{
			UE_LOG(LogIoStore, Error, TEXT("Invalid line in response file '%s'."), *ResponseLine);
			return false;
		}

		FPaths::NormalizeFilename(SourceAndDest[0]);

		FContainerSourceFile& FileEntry = OutFiles.AddDefaulted_GetRef();
		FileEntry.NormalizedPath = MoveTemp(SourceAndDest[0]);

		for (int32 Index = 0; Index < Switches.Num(); ++Index)
		{
			if (Switches[Index] == TEXT("compress"))
			{
				FileEntry.bNeedsCompression = true;
			}
		}
	}
	return true;
}

static bool ParsePakOrderFile(const TCHAR* FilePath, TMap<FName, uint64>& OutMap)
{
	TArray<FString> OrderFileContents;
	if (!FFileHelper::LoadFileToStringArray(OrderFileContents, FilePath))
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read order file '%s'."), *FilePath);
		return false;
	}

	uint64 LineNumber = 1;
	for (const FString& OrderLine : OrderFileContents)
	{
		const TCHAR* OrderLinePtr = *OrderLine;
		FString Path;
		if (!FParse::Token(OrderLinePtr, Path, false))
		{
			UE_LOG(LogIoStore, Error, TEXT("Invalid line in order file '%s'."), *OrderLine);
			return false;
		}
		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(Path, PackageName, nullptr))
		{
			continue;;
		}

		uint64 Order = LineNumber;
		FString OrderStr;
		if (FParse::Token(OrderLinePtr, OrderStr, false))
		{
			if (!OrderStr.IsNumeric())
			{
				UE_LOG(LogIoStore, Error, TEXT("Invalid line in order file '%s'."), *OrderLine);
				return false;
			}
			Order = FCString::Atoi64(*OrderStr);
		}

		FName PackageFName(MoveTemp(PackageName));
		if (!OutMap.Contains(PackageFName))
		{
			OutMap.Emplace(PackageFName, Order);
		}

		++LineNumber;
	}
	return true;
}

class FCookedFileVisitor : public IPlatformFile::FDirectoryStatVisitor
{
	FCookedFileStatMap& CookedFileStatMap;
	FContainerSourceSpec* ContainerSpec = nullptr;

public:
	FCookedFileVisitor(FCookedFileStatMap& InCookedFileSizes, FContainerSourceSpec* InContainerSpec)
		: CookedFileStatMap(InCookedFileSizes)
		, ContainerSpec(InContainerSpec)
	{}

	FCookedFileVisitor(FCookedFileStatMap& InFileSizes)
		: CookedFileStatMap(InFileSizes)
	{}

	virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData)
	{
		// Should match FCookedFileStatData::EFileExt
		static const TCHAR* Extensions[] = { TEXT("umap"), TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk") };
		static const int32 NumPackageExtensions = 2;
		static const int32 UExpExtensionIndex = 2;

		if (StatData.bIsDirectory)
		{
			return true;
		}

		const TCHAR* Extension = FCString::Strrchr(FilenameOrDirectory, '.');
		if (!Extension || *(++Extension) == TEXT('\0'))
		{
			return true;
		}

		int32 ExtIndex = 0;
		if (0 == FCString::Stricmp(Extension, Extensions[3]))
		{
			ExtIndex = 3;
			if (0 == FCString::Stricmp(Extension - 3, TEXT(".m.ubulk")))
			{
				ExtIndex = 5;
			}
		}
		else
		{
			for (ExtIndex = 0; ExtIndex < UE_ARRAY_COUNT(Extensions); ++ExtIndex)
			{
				if (0 == FCString::Stricmp(Extension, Extensions[ExtIndex]))
					break;
			}
		}

		if (ExtIndex >= UE_ARRAY_COUNT(Extensions))
		{
			return true;
		}

		FString Path = FilenameOrDirectory;
		FPaths::NormalizeFilename(Path);

		if (ContainerSpec && ExtIndex != UExpExtensionIndex)
		{
			FContainerSourceFile& FileEntry = ContainerSpec->SourceFiles.AddDefaulted_GetRef();
			FileEntry.NormalizedPath = Path;
		}

		FCookedFileStatData& CookedFileStatData = CookedFileStatMap.Add(MoveTemp(Path));
		CookedFileStatData.FileSize = StatData.FileSize;
		CookedFileStatData.FileExt = FCookedFileStatData::EFileExt(ExtIndex);
		if (ExtIndex < NumPackageExtensions)
		{
			CookedFileStatData.FileType = FCookedFileStatData::PackageHeader;
		}
		else if (ExtIndex == UExpExtensionIndex)
		{
			CookedFileStatData.FileType = FCookedFileStatData::PackageData;
		}
		else
		{
			CookedFileStatData.FileType = FCookedFileStatData::BulkData;
		}
		return true;
	}
};

int32 CreateIoStoreContainerFiles(const TCHAR* CmdLine)
{
	IOSTORE_CPU_SCOPE(CreateIoStoreContainerFiles);

	UE_LOG(LogIoStore, Display, TEXT("==================== IoStore Utils ===================="));

	FIoStoreArguments Arguments;
	FString GameOrderFilePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("GameOrder="), GameOrderFilePath))
	{
		if (!ParsePakOrderFile(*GameOrderFilePath, Arguments.GameOrderMap))
		{
			return -1;
		}
	}

	FString CookerOrderFilePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("CookerOrder="), CookerOrderFilePath))
	{
		if (!ParsePakOrderFile(*CookerOrderFilePath, Arguments.CookerOrderMap))
		{
			return -1;
		}
	}

	FIoStoreWriterSettings GeneralIoWriterSettings { DefaultCompressionMethod, DefaultCompressionBlockSize, false };
	GeneralIoWriterSettings.bEnableCsvOutput = FParse::Param(CmdLine, TEXT("-csvoutput"));

	if (!FParse::Value(CmdLine, TEXT("-AlignForMemoryMapping="), Arguments.MemoryMappingAlignment))
	{
		Arguments.MemoryMappingAlignment = DefaultMemoryMappingAlignment;
	}
	UE_LOG(LogIoStore, Display, TEXT("Using memory mapping alignment '%ld'"), Arguments.MemoryMappingAlignment);
	
	TArray<FName> CompressionFormats;
	FString DesiredCompressionFormats;
	if (FParse::Value(CmdLine, TEXT("-compressionformats="), DesiredCompressionFormats) ||
		FParse::Value(CmdLine, TEXT("-compressionformat="), DesiredCompressionFormats))
	{
		TArray<FString> Formats;
		DesiredCompressionFormats.ParseIntoArray(Formats, TEXT(","));
		for (FString& Format : Formats)
		{
			// look until we have a valid format
			FName FormatName = *Format;

			if (FCompression::IsFormatValid(FormatName))
			{
				GeneralIoWriterSettings.CompressionMethod = FormatName;
				break;
			}
		}

		if (GeneralIoWriterSettings.CompressionMethod == NAME_None)
		{
			UE_LOG(LogIoStore, Warning, TEXT("Failed to find desired compression format(s) '%s'. Using falling back to '%s'"),
				*DesiredCompressionFormats, *DefaultCompressionMethod.ToString());
		}
		else
		{
			UE_LOG(LogIoStore, Display, TEXT("Using compression format '%s'"), *GeneralIoWriterSettings.CompressionMethod.ToString());
		}
	}

	FString CompressionBlockSizeString;
	if (FParse::Value(CmdLine, TEXT("-compressionblocksize="), CompressionBlockSizeString))
	{
		FParse::Value(CmdLine, TEXT("-compressionblocksize="), GeneralIoWriterSettings.CompressionBlockSize);

		if (CompressionBlockSizeString.EndsWith(TEXT("MB")))
		{
			GeneralIoWriterSettings.CompressionBlockSize *= 1024 * 1024;
		}
		else if (CompressionBlockSizeString.EndsWith(TEXT("KB")))
		{
			GeneralIoWriterSettings.CompressionBlockSize *= 1024;
		}
	}
	UE_LOG(LogIoStore, Display, TEXT("Using compression block size '%ld'"), GeneralIoWriterSettings.CompressionBlockSize);

	FParse::Value(CmdLine, TEXT("-compressionblockalignment="), GeneralIoWriterSettings.CompressionBlockAlignment);
	UE_LOG(LogIoStore, Display, TEXT("Using compression block alignment '%ld'"), GeneralIoWriterSettings.CompressionBlockAlignment);

	FParse::Value(CmdLine, TEXT("-CreateReleaseVersionDirectory="), Arguments.OutputReleaseVersionDir);
	FParse::Value(CmdLine, TEXT("-BasedOnReleaseVersionDirectory="), Arguments.BasedOnReleaseVersionDir);

	if (FParse::Value(FCommandLine::Get(), TEXT("CreateGlobalContainer="), Arguments.GlobalContainerPath))
	{
		Arguments.GlobalContainerPath = FPaths::ChangeExtension(Arguments.GlobalContainerPath, TEXT(""));

		if (!FParse::Value(FCommandLine::Get(), TEXT("CookedDirectory="), Arguments.CookedDir))
		{
			UE_LOG(LogIoStore, Error, TEXT("CookedDirectory must be specified"));
			return 1;
		}

		FContainerSourceSpec* CookedFilesVisitorContainerSpec = nullptr;
		FString CommandListFile;
		if (FParse::Value(FCommandLine::Get(), TEXT("Commands="), CommandListFile))
		{
			UE_LOG(LogIoStore, Display, TEXT("Using command list file: '%s'"), *CommandListFile);
			TArray<FString> Commands;
			if (!FFileHelper::LoadFileToStringArray(Commands, *CommandListFile))
			{
				UE_LOG(LogIoStore, Error, TEXT("Failed to read command list file '%s'."), *CommandListFile);
				return -1;
			}

			Arguments.Containers.Reserve(Commands.Num());
			for (const FString& Command : Commands)
			{
				FContainerSourceSpec& ContainerSpec = Arguments.Containers.AddDefaulted_GetRef();

				if (!FParse::Value(*Command, TEXT("Output="), ContainerSpec.OutputPath))
				{
					UE_LOG(LogIoStore, Error, TEXT("Output argument missing from command '%s'"), *Command);
					return -1;
				}
				ContainerSpec.OutputPath = FPaths::ChangeExtension(ContainerSpec.OutputPath, TEXT(""));

				FString ContainerName;
				if (!FParse::Value(*Command, TEXT("ContainerName="), ContainerName))
				{
					UE_LOG(LogIoStore, Error, TEXT("ContainerName argument missing from command '%s'"), *Command);
					return -1;
				}
				ContainerSpec.Name = FName(ContainerName);

				FString PatchSourceWildcard;
				if (FParse::Value(*Command, TEXT("PatchSource="), PatchSourceWildcard))
				{
					IFileManager::Get().FindFiles(ContainerSpec.PatchSourceContainerFiles, *PatchSourceWildcard, true, false);
					FString PatchSourceContainersDirectory = FPaths::GetPath(*PatchSourceWildcard);
					for (FString& PatchSourceContainerFile : ContainerSpec.PatchSourceContainerFiles)
					{
						PatchSourceContainerFile = PatchSourceContainersDirectory / PatchSourceContainerFile;
						FPaths::NormalizeFilename(PatchSourceContainerFile);
					}
				}

				ContainerSpec.bGenerateDiffPatch = FParse::Param(*Command, TEXT("GenerateDiffPatch"));
				
				FString ResponseFilePath;
				if (!FParse::Value(*Command, TEXT("ResponseFile="), ResponseFilePath))
				{
					UE_LOG(LogIoStore, Error, TEXT("ResponseFile argument missing from command '%s'"), *Command);
					return -1;
				}

				if (!ParsePakResponseFile(*ResponseFilePath, ContainerSpec.SourceFiles))
				{
					UE_LOG(LogIoStore, Error, TEXT("Failed to parse Pak response file '%s'"), *ResponseFilePath);
					return -1;
				}
			}
		}
		else
		{
			CookedFilesVisitorContainerSpec = &Arguments.Containers.AddDefaulted_GetRef();
		}

		UE_LOG(LogIoStore, Display, TEXT("Searching for cooked assets in folder '%s'"), *Arguments.CookedDir);
		FCookedFileStatMap CookedFileStatMap;
		FCookedFileVisitor CookedFileVistor(Arguments.CookedFileStatMap, CookedFilesVisitorContainerSpec);
		IFileManager::Get().IterateDirectoryStatRecursively(*Arguments.CookedDir, CookedFileVistor);
		UE_LOG(LogIoStore, Display, TEXT("Found '%d' files"), Arguments.CookedFileStatMap.Num());

		int32 ReturnValue = CreateTarget(Arguments, GeneralIoWriterSettings);
		if (ReturnValue != 0)
		{
			return ReturnValue;
		}
	}
	else
	{
		UE_LOG(LogIoStore, Error, TEXT("Nothing to do!"));
		return -1;
	}

	return 0;
}
