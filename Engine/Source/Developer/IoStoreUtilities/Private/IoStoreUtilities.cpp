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

struct FContainerSourceFile 
{
	FString NormalizedPath;
	bool bNeedsCompression = false;
};

struct FContainerSourceSpec
{
	FString OutputPath;
	TArray<FContainerSourceFile> SourceFiles;
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

struct FContainerTargetFile
{
	FPackage* Package = nullptr;
	FString NormalizedSourcePath;
	FString TargetPath;
	uint64 SourceSize = 0;
	uint64 TargetSize = 0;
	uint64 Offset = 0;
	uint64 Padding = 0;
	uint64 Alignment = 0;
	FIoChunkId ChunkId;
	FSHAHash Hash;
	bool bIsBulkData = false;
	bool bIsOptionalBulkData = false;
	bool bIsMemoryMappedBulkData = false;
	bool bForceUncompressed = false;
};

struct FIoStoreArguments
{
	FString OutputDir;
	FString CookedDir;
	FString OutputReleaseVersionDir;
	FString BasedOnReleaseVersionDir;
	TArray<FContainerSourceSpec> Containers;
	FCookedFileStatMap CookedFileStatMap;
	TMap<FName, uint64> PackageOrderMap;
	TMap<FName, uint64> CookerOrderMap;
	int64 MemoryMappingAlignment = 0;
};

struct FTocMetaEntry
{
	uint64 Offset = 0;
	uint64 Size = 0;
	FSHAHash Hash;

	friend FArchive& operator<<(FArchive& Ar, FTocMetaEntry& TocMetaEntry)
	{
		Ar << TocMetaEntry.Offset;
		Ar << TocMetaEntry.Size;
		Ar << TocMetaEntry.Hash;
		return Ar;
	}
};

struct FContainerTargetSpec
{
	FString Name;
	FString OutputPath;
	FIoStoreWriter* IoStoreWriter;
	TArray<FContainerTargetFile> TargetFiles;
	TUniquePtr<FIoStoreEnvironment> IoStoreEnv;
	TArray<FTocMetaEntry> PreviousBuildToc;
	bool bIsCompressed = false;
};

struct FPackageAssetData
{
	TArray<FObjectImport> ObjectImports;
	TArray<FObjectExport> ObjectExports;
	TArray<FPackageIndex> PreloadDependencies;
};

class FNameProxyArchive
	: public FArchiveProxy
{
public:
	using FArchiveProxy::FArchiveProxy;

	FNameProxyArchive(FArchive& InAr, const TArray<FNameEntryId>& InNameMap)
		: FArchiveProxy(InAr)
		, NameMap(InNameMap) { }

	virtual FArchive& operator<<(FName& Name) override
	{
		int32 NameIndex, Number;
		InnerArchive << NameIndex << Number;

		if (!NameMap.IsValidIndex(NameIndex))
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Bad name index %i/%i"), NameIndex, NameMap.Num());
		}

		const FNameEntryId MappedName = NameMap[NameIndex];
		Name = FName::CreateFromDisplayId(MappedName, Number);

		return *this;
	}

private:
	const TArray<FNameEntryId>& NameMap;
};

struct FPackage;
using FPackageMap = TMap<FName, FPackage*>;
using FSourceToLocalizedPackageIdMap = TMap<int32,int32>;
using FSourceToLocalizedPackageMultimap = TMultiMap<FPackage*,FPackage*>;
using FLocalizedToSourceImportIndexMap = TMap<int32, int32>;
using FCulturePackageMap = TMap<FString, FSourceToLocalizedPackageIdMap>;

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

class FNameMapBuilder
{
public:
	void MarkNamesAsReferenced(const TArray<FName>& Names, TArray<FNameEntryId>& OutNameMap, TArray<int32>& OutNameIndices)
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

			OutNameMap.Emplace(Name.GetDisplayIndex());
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

	int32 MapName(const FName& Name) const
	{
		const FNameEntryId Id = Name.GetComparisonIndex();
		const int32* Index = NameIndices.Find(Id);
		check(Index);
		return Index ? *Index - 1 : INDEX_NONE;
	}

	void SerializeName(FArchive& A, const FName& N) const
	{
		int32 NameIndex = MapName(N);
		int32 NameNumber = N.GetNumber();
		A << NameIndex << NameNumber;
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

private:
	TMap<FNameEntryId, int32> NameIndices;
	TArray<FNameEntryId> NameMap;
#if OUTPUT_NAMEMAP_CSV
	TMap<FNameEntryId, TTuple<int32,int32,int32>> DebugNameCounts; // <Number0Count,OtherNumberCount,MaxNumber>
#endif
};

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

static FIoChunkId CreateChunkId(int32 GlobalPackageId, uint16 ChunkIndex, EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateIoChunkId(GlobalPackageId, ChunkIndex, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, ChunkIndex, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

static FIoChunkId CreateChunkIdForBulkData(int32 GlobalPackageId,EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateIoChunkId(GlobalPackageId, 0, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, 0, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

static FIoChunkId CreateChunkIdForBulkData(int32 GlobalPackageId, int64 BulkdataOffset, EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateBulkdataChunkId(GlobalPackageId, BulkdataOffset, ChunkType);
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
	TSet<uint32> ScriptDependencies;
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
	FSHAHash ExportBundleHash;
};
#endif

struct FPackage
{
	FName Name;
	FName SourcePackageName; // for localized packages
	FString FileName;
	int32 GlobalPackageId = 0;
	FString Region; // for localized packages
	int32 SourceGlobalPackageId = -1; // for localized packages
	int32 ImportedPackagesSerializeCount = 0; // < ImportedPackages.Num() for source packages that have localized packages
	uint32 PackageFlags = 0;
	uint32 CookedHeaderSize = 0;
	int32 NameCount = 0;
	int32 ImportCount = 0;
	int32 PreloadDependencyCount = 0;
	int32 ExportCount = 0;
	int32 FirstGlobalImport = -1;
	int32 GlobalImportCount = -1;
	int32 ImportIndexOffset = -1;
	int32 ExportIndexOffset = -1;
	int32 PreloadIndexOffset = -1;
	int32 FirstExportBundleMetaEntry = -1;
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

	TArray<FNameEntryId> NameMap;
	TArray<int32> NameIndices;

	TArray<int32> Imports;
	TArray<int32> Exports;
	TArray<FArc> InternalArcs;
	TMap<FPackage*, TArray<FArc>> ExternalArcs;
	TArray<FArc> ScriptArcs;
	
	TArray<FExportBundle> ExportBundles;
	TMap<FExportGraphNode*, uint32> ExportBundleMap;

	TArray<FExportGraphNode*> CreateExportNodes;
	TArray<FExportGraphNode*> SerializeExportNodes;

	TArray<FExportGraphNode*> NodesWithNoIncomingEdges;
	FPackageGraphNode* Node = nullptr;

	TArray<uint8> HeaderData;
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

static void AddScriptArc(FPackage& Package, uint32 GlobalImportIndex, uint32 ExportIndex, EPreloadDependencyType Phase)
{
	FExportGraphNode* Node = Phase == PreloadDependencyType_Create ? Package.CreateExportNodes[ExportIndex] : Package.SerializeExportNodes[ExportIndex];
	Node->ScriptDependencies.Add(GlobalImportIndex);
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

static void AddUniqueScriptBundleArc(FPackage& Package, uint32 GlobalImportIndex, uint32 BundleIndex)
{
	uint32 NodeIndex = EEventLoadNode2::Package_NumPhases + BundleIndex * EEventLoadNode2::ExportBundle_NumPhases + EEventLoadNode2::ExportBundle_Process;
	Package.ScriptArcs.AddUnique({ GlobalImportIndex, NodeIndex });
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

static void AddPostLoadDependencies(
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

static void BuildBundles(FExportGraph& ExportGraph, const TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(BuildBundles)

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
		for (uint32 ScriptDependencyGlobalImportIndex : Node->ScriptDependencies)
		{
			AddUniqueScriptBundleArc(*Package, ScriptDependencyGlobalImportIndex, BundleIndex);
		}
		Bundle->Nodes.Add(Node);
		Package->ExportBundleMap.Add(Node, BundleIndex);
	}
}

struct FFileLayoutNode
{
	FFileLayoutNode* Prev = nullptr;
	FFileLayoutNode* Next = nullptr;
	FContainerTargetFile* TargetFile = nullptr;
	uint64 IdealOrder = 0;
	uint64 PreviousOffset = 0;
	bool bHasPreviousOffset = false;
};

static void CreateDiskLayout(
	TArray<FContainerTargetSpec>& ContainerTargets,
	const TArray<FPackage*>& Packages,
	const TMap<FName, uint64> PackageOrderMap,
	const TMap<FName, uint64>& CookerOrderMap,
	uint64 CompressionBlockSize,
	uint64 MemoryMappingAlignment)
{
	IOSTORE_CPU_SCOPE(CreateDiskLayout);

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
		uint64 PackageLoadOrder;
		uint64 CookerOpenOrder;

		bool operator<(const FPackageAndOrder& Other) const
		{
			if (PackageLoadOrder != Other.PackageLoadOrder)
			{
				return PackageLoadOrder < Other.PackageLoadOrder;
			}
			if (CookerOpenOrder != Other.CookerOpenOrder)
			{
				return CookerOpenOrder < Other.CookerOpenOrder;
			}
			// Fallback to reverse bundle order
			return Package->ExportBundles[0].LoadOrder > Other.Package->ExportBundles[0].LoadOrder;
		}
	};

	TArray<FPackageAndOrder> SortedPackages;
	SortedPackages.Reserve(Packages.Num());
	for (FPackage* Package : Packages)
	{
		FPackageAndOrder& Entry = SortedPackages.AddDefaulted_GetRef();
		Entry.Package = Package;
		const uint64* FindPackageLoadOrder = PackageOrderMap.Find(Package->Name);
		Entry.PackageLoadOrder = FindPackageLoadOrder ? *FindPackageLoadOrder : MAX_uint64;
		const uint64* FindCookerOpenOrder = CookerOrderMap.Find(Package->Name);
		/*if (!FindCookerOpenOrder)
		{
			UE_LOG(LogIoStore, Warning, TEXT("Missing cooker order for package: %s"), *Package->Name.ToString());
		}*/
		Entry.CookerOpenOrder = FindCookerOpenOrder ? *FindCookerOpenOrder : MAX_uint64;
	}
	bool bHasPackageOrder = true;
	bool bHasCookerOrder = true;
	int32 LastAssignedCount = 0;
	Algo::Sort(SortedPackages);
	for (FPackageAndOrder& Entry : SortedPackages)
	{
		if (bHasPackageOrder && Entry.PackageLoadOrder == MAX_uint64)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using package load order"), AssignedPackages.Num(), Packages.Num());
			LastAssignedCount = AssignedPackages.Num();
			bHasPackageOrder = false;
		}
		if (!bHasPackageOrder && bHasCookerOrder && Entry.CookerOpenOrder == MAX_uint64)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using cooker order"), AssignedPackages.Num() - LastAssignedCount, Packages.Num() - LastAssignedCount);
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
					Cluster->Packages.Add(PackageToProcess);
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

	for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
	{
		Algo::Sort(ContainerTarget.TargetFiles, [](const FContainerTargetFile& A, const FContainerTargetFile& B)
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

		TMap<FSHAHash, FTocMetaEntry> PreviousBuildTocMap;
		for (const FTocMetaEntry& TocMetaEntry : ContainerTarget.PreviousBuildToc)
		{
			if (PreviousBuildTocMap.Find(TocMetaEntry.Hash))
			{
				UE_DEBUG_BREAK();
			}
			PreviousBuildTocMap.Add(TocMetaEntry.Hash, TocMetaEntry);
		}

		uint64 DiffCount = 0;
		uint64 DiffSize = 0;
		TArray<FFileLayoutNode*> LayoutNodes;
		LayoutNodes.Reserve(ContainerTarget.TargetFiles.Num());
		uint64 IdealOrder = 0;
		TMap<uint64, FFileLayoutNode*> UnassignedNodes;
		TMap<uint64, FFileLayoutNode*> AssignedNodes;
		for (FContainerTargetFile& ContainerTargetFile : ContainerTarget.TargetFiles)
		{
			FFileLayoutNode* LayoutNode = new FFileLayoutNode();
			LayoutNodes.Add(LayoutNode);
			LayoutNode->TargetFile = &ContainerTargetFile;
			LayoutNode->IdealOrder = IdealOrder++;
			UnassignedNodes.Add(LayoutNode->IdealOrder, LayoutNode);

			FTocMetaEntry* FindPreviousBuildEntry = PreviousBuildTocMap.Find(ContainerTargetFile.Hash);
			if (!FindPreviousBuildEntry)
			{
				++DiffCount;
				DiffSize += ContainerTargetFile.TargetSize;
				//UE_LOG(LogIoStore, Display, TEXT("Diffing: %s"), *ContainerTargetFile.TargetPath);
			}
			else
			{
				LayoutNode->PreviousOffset = FindPreviousBuildEntry->Offset;
				LayoutNode->bHasPreviousOffset = true;
			}
		}
		UE_LOG(LogIoStore, Display, TEXT("%s: %d/%d new/modified entries (%fMB)"), *ContainerTarget.Name, DiffCount, ContainerTarget.TargetFiles.Num(), DiffSize / 1024.0 / 1024.0);

		Algo::Sort(LayoutNodes, [](const FFileLayoutNode* A, const FFileLayoutNode* B)
		{
			if (A->bHasPreviousOffset != B->bHasPreviousOffset)
			{
				return A->bHasPreviousOffset;
			}
			else if (A->PreviousOffset != B->PreviousOffset)
			{
				return A->PreviousOffset < B->PreviousOffset;
			}
			return A->IdealOrder < B->IdealOrder;
		});

		FFileLayoutNode Head;
		FFileLayoutNode Tail;
		Head.Next = &Tail;
		Tail.Prev = &Head;
		FFileLayoutNode* PreviousNode = &Head;
		for (FFileLayoutNode* LayoutNode : LayoutNodes)
		{
			check(UnassignedNodes.Contains(LayoutNode->IdealOrder));
			check(!AssignedNodes.Contains(LayoutNode->IdealOrder));
			
			FFileLayoutNode* OrderAfterNode;
			if (LayoutNode->bHasPreviousOffset)
			{
				OrderAfterNode = PreviousNode;
			}
			else
			{
				if (LayoutNode->IdealOrder == 0)
				{
					OrderAfterNode = &Head;
				}
				else
				{
					OrderAfterNode = AssignedNodes.FindRef(LayoutNode->IdealOrder - 1);
					check(OrderAfterNode);
				}
			}

			OrderAfterNode->Next->Prev = LayoutNode;
			LayoutNode->Next = OrderAfterNode->Next;
			LayoutNode->Prev = OrderAfterNode;
			OrderAfterNode->Next = LayoutNode;
			
			AssignedNodes.Add(LayoutNode->IdealOrder, LayoutNode);
			UnassignedNodes.Remove(LayoutNode->IdealOrder);

			PreviousNode = LayoutNode;
		}

		TArray<bool> BlocksModificationState;
		uint64 CurrentOffset = 0;
		FFileLayoutNode* CurrentNode = Head.Next;
		bool bIsAlignedForMemoryMappedBulkData = false;
		while (CurrentNode != &Tail)
		{
			uint64 ModificationOffset = 0;
			uint64 ModificationSize = 0;

			if (CurrentNode->TargetFile->bIsMemoryMappedBulkData)
			{
				if (!bIsAlignedForMemoryMappedBulkData)
				{
					if (!IsAligned(CurrentOffset, CompressionBlockSize))
					{
						uint64 RemainingInBlock = Align(CurrentOffset, CompressionBlockSize) - CurrentOffset;
						CurrentNode->TargetFile->Padding = RemainingInBlock;
						CurrentOffset += RemainingInBlock;
					}
					CurrentNode->TargetFile->Alignment = MemoryMappingAlignment;
					bIsAlignedForMemoryMappedBulkData = true;
				}
				else
				{
					if (!IsAligned(CurrentOffset, MemoryMappingAlignment))
					{
						uint64 RequiredPadding = Align(CurrentOffset, MemoryMappingAlignment) - CurrentOffset;
						CurrentNode->TargetFile->Padding = RequiredPadding;
						CurrentOffset += RequiredPadding;
					}
				}
			}
			else
			{
				if (!IsAligned(CurrentOffset, CompressionBlockSize))
				{
					uint64 RemainingInBlock = Align(CurrentOffset, CompressionBlockSize) - CurrentOffset;
					CurrentNode->TargetFile->Padding = RemainingInBlock;
					CurrentOffset += RemainingInBlock;
				}
				bIsAlignedForMemoryMappedBulkData = false;
			}

			if (CurrentNode->bHasPreviousOffset)
			{
				uint64 PreviousOffsetInBlock = CurrentNode->PreviousOffset % CompressionBlockSize;
				uint64 CurrentOffsetInBlock = CurrentOffset % CompressionBlockSize;
				if (CurrentOffsetInBlock != PreviousOffsetInBlock)
				{
					uint64 PadBytes = 0;
					if (CurrentOffsetInBlock < PreviousOffsetInBlock)
					{
						PadBytes = PreviousOffsetInBlock - CurrentOffsetInBlock;
					}
					else if (PreviousOffsetInBlock < CurrentOffsetInBlock)
					{
						PadBytes = CompressionBlockSize - CurrentOffsetInBlock + PreviousOffsetInBlock;
					}
					ModificationOffset = CurrentOffset;
					ModificationSize += PadBytes;
					CurrentOffset += PadBytes;
					CurrentNode->TargetFile->Padding = PadBytes;
				}
				check(CurrentOffset % CompressionBlockSize == CurrentNode->PreviousOffset % CompressionBlockSize);
			}
			else
			{
				ModificationOffset = CurrentOffset;
				ModificationSize = CurrentNode->TargetFile->TargetSize;
			}

			if (ModificationSize)
			{
				int32 BeginBlockIndex = ModificationOffset / CompressionBlockSize;
				int32 EndBlockIndex = Align(ModificationOffset + ModificationSize, CompressionBlockSize) / CompressionBlockSize;
				if (BlocksModificationState.Num() < EndBlockIndex)
				{
					BlocksModificationState.AddZeroed(EndBlockIndex - BlocksModificationState.Num());
				}
				for (int32 BlockIndex = BeginBlockIndex; BlockIndex < EndBlockIndex; ++BlockIndex)
				{
					BlocksModificationState[BlockIndex] = true;
				}
			}

			CurrentNode->TargetFile->Offset = CurrentOffset;
			CurrentOffset += CurrentNode->TargetFile->TargetSize;
			CurrentNode = CurrentNode->Next;
		}

		uint64 ModifiedBlocksCount = 0;
		for (bool BlockModified : BlocksModificationState)
		{
			if (BlockModified)
			{
				++ModifiedBlocksCount;
			}
		}
		UE_LOG(LogIoStore, Display, TEXT("%s: %d new/modified blocks (%fMB)"), *ContainerTarget.Name, ModifiedBlocksCount, ModifiedBlocksCount * CompressionBlockSize / 1024.0f / 1024.0f);

		for (FFileLayoutNode* LayoutNode : LayoutNodes)
		{
			delete LayoutNode;
		}

		Algo::Sort(ContainerTarget.TargetFiles, [](const FContainerTargetFile& A, const FContainerTargetFile& B)
		{
			return A.Offset < B.Offset;
		});
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

static bool MapAdditionalBulkDataChunks(const FContainerTargetFile& TargetFile, const FPackageStoreBulkDataManifest& BulkDataManifest, FIoStoreWriter* IoStoreWriter)
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
				const FIoChunkId AccessChunkId = CreateChunkIdForBulkData(Package.GlobalPackageId, BulkDataDesc.ChunkId, ChunkIdType, *Package.FileName);
				const FIoStatus PartialResult = IoStoreWriter->MapPartialRange(TargetFile.ChunkId, BulkDataDesc.Offset, BulkDataDesc.Size, AccessChunkId);
				if (!PartialResult.IsOk())
				{
					UE_LOG(LogIoStore, Warning, TEXT("Failed to map partial range for '%s' due to: %s"), *Package.FileName, *PartialResult.ToString());
				}
			}
		}
	}
	else
	{
		UE_LOG(LogIoStore, Error, TEXT("Unable to find an entry in the bulkdata manifest for '%s' the file might be out of date!"), *Package.FileName);
		return false;
	}

	return true;
}

struct FImportData
{
	int32 GlobalIndex = -1;
	int32 OuterIndex = -1;
	int32 OutermostIndex = -1;
	int32 GlobalExportIndex = -1;
	int32 RefCount = 0;
	FName ObjectName;
	bool bIsPackage = false;
	bool bIsScript = false;
	bool bIsLocalized = false;
	FString FullName;
	FPackage* Package = nullptr;
	bool bIsMissingImport = false;

	bool operator<(const FImportData& Other) const
	{
		if (bIsScript != Other.bIsScript)
		{
			return bIsScript;
		}
		if (bIsLocalized != Other.bIsLocalized)
		{
			return Other.bIsLocalized;
		}
		if (OutermostIndex != Other.OutermostIndex)
		{
			return OutermostIndex < Other.OutermostIndex;
		}
		if (bIsPackage != Other.bIsPackage)
		{
			return bIsPackage;
		}
		return FullName < Other.FullName;
	}
};

struct FExportData
{
	int32 GlobalIndex = -1;
	FName SourcePackageName;
	FName ObjectName;
	int32 SourceIndex = -1;
	int32 GlobalImportIndex = -1;
	FPackageIndex OuterIndex;
	FPackageIndex ClassIndex;
	FPackageIndex SuperIndex;
	FPackageIndex TemplateIndex;
	FString FullName;

	FExportGraphNode* CreateNode = nullptr;
	FExportGraphNode* SerializeNode = nullptr;
};

template <typename T>
struct FGlobalObjects
{
	TArray<T> Objects;
	TMap<FString, int32> ObjectsByFullName;
};

using FGlobalImports = FGlobalObjects<FImportData>;
using FGlobalExports = FGlobalObjects<FExportData>;

struct FGlobalPackageData
{
	FGlobalImports Imports;
	FGlobalExports Exports;
};

static void FindImport(
	FGlobalImports& GlobalImports,
	TArray<FString>& TempFullNames,
	FObjectImport* ImportMap,
	const int32 LocalImportIndex)
{
	FObjectImport* Import = &ImportMap[LocalImportIndex];
	FString& FullName = TempFullNames[LocalImportIndex];
	if (FullName.Len() == 0)
	{
		if (Import->OuterIndex.IsNull())
		{
			Import->ObjectName.AppendString(FullName);
			const int32* FindGlobalImport = GlobalImports.ObjectsByFullName.Find(FullName);
			if (!FindGlobalImport)
			{
				// first time, assign global index for this root package
				const int32 GlobalImportIndex = GlobalImports.Objects.Num();
				GlobalImports.ObjectsByFullName.Add(FullName, GlobalImportIndex);
				FImportData& GlobalImport = GlobalImports.Objects.AddDefaulted_GetRef();
				GlobalImport.GlobalIndex = GlobalImportIndex;
				GlobalImport.OutermostIndex = GlobalImportIndex;
				GlobalImport.OuterIndex = -1;
				GlobalImport.ObjectName = Import->ObjectName;
				GlobalImport.bIsPackage = true;
				GlobalImport.bIsScript = FullName.StartsWith(TEXT("/Script/"));
				GlobalImport.bIsLocalized = FullName.Contains(L10NPrefix);
				GlobalImport.FullName = FullName;
				GlobalImport.RefCount = 1;
			}
			else
			{
				++GlobalImports.Objects[*FindGlobalImport].RefCount;
			}
		}
		else
		{
			const int32 LocalOuterIndex = Import->OuterIndex.ToImport();
			FindImport(GlobalImports, TempFullNames, ImportMap, LocalOuterIndex);
			FString& OuterName = TempFullNames[LocalOuterIndex];
			ensure(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Import->ObjectName.AppendString(FullName);
			const int32* FindGlobalImport = GlobalImports.ObjectsByFullName.Find(FullName);
			if (!FindGlobalImport)
			{
				// first time, assign global index for this intermediate import
				const int32 GlobalImportIndex = GlobalImports.Objects.Num();
				GlobalImports.ObjectsByFullName.Add(FullName, GlobalImportIndex);
				FImportData& GlobalImport = GlobalImports.Objects.AddDefaulted_GetRef();
				const int32* FindOuterGlobalImport = GlobalImports.ObjectsByFullName.Find(OuterName);
				check(FindOuterGlobalImport);
				const FImportData& OuterGlobalImport = GlobalImports.Objects[*FindOuterGlobalImport];
				GlobalImport.GlobalIndex = GlobalImportIndex;
				GlobalImport.OutermostIndex = OuterGlobalImport.OutermostIndex;
				GlobalImport.OuterIndex = OuterGlobalImport.GlobalIndex;
				GlobalImport.ObjectName = Import->ObjectName;
				GlobalImport.bIsScript = OuterGlobalImport.bIsScript;
				GlobalImport.bIsLocalized = OuterGlobalImport.bIsLocalized;
				GlobalImport.FullName = FullName;
				GlobalImport.RefCount = 1;
			}
			else
			{
				++GlobalImports.Objects[*FindGlobalImport].RefCount;
			}
		}
	}
}

static void FindExport(
	FGlobalExports& GlobalExports,
	TArray<FString>& TempFullNames,
	const FObjectExport* ExportMap,
	const int32 LocalExportIndex,
	const FName& PackageName)
{
	const FObjectExport* Export = ExportMap + LocalExportIndex;
	FString& FullName = TempFullNames[LocalExportIndex];

	if (FullName.Len() == 0)
	{
		if (Export->OuterIndex.IsNull())
		{
			PackageName.AppendString(FullName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
		}
		else
		{
			check(Export->OuterIndex.IsExport());

			FindExport(GlobalExports, TempFullNames, ExportMap, Export->OuterIndex.ToExport(), PackageName);
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
		ExportData.SourcePackageName = PackageName;
		ExportData.ObjectName = Export->ObjectName;
		ExportData.SourceIndex = LocalExportIndex;
		ExportData.FullName = FullName;
	}
}

FPackage* FindOrAddPackage(
	const TCHAR* RelativeFileName,
	TArray<FPackage*>& Packages,
	FPackageMap& PackageMap,
	TMap<FName, int32>& PackageGlobalIdMap)
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
		const int32* FindPackageGlobalId = PackageGlobalIdMap.Find(PackageFName);
		if (FindPackageGlobalId)
		{
			Package->GlobalPackageId = *FindPackageGlobalId;
		}
		else
		{
			Package->GlobalPackageId = PackageGlobalIdMap.Num();
			PackageGlobalIdMap.Add(PackageFName, Package->GlobalPackageId);
		}
		Packages.Add(Package);
		PackageMap.Add(PackageFName, Package);
	}

	return Package;
}

static bool ConformLocalizedPackage(
	const FPackageMap& PackageMap,
	const TArray<FImportData>& GlobalImports,
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
			LocalizedPackage.GlobalPackageId,
			*LocalizedPackage.SourcePackageName.ToString(),
			SourcePackage.GlobalPackageId,
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

	const int32 LocalizedPackageNameLen = LocalizedPackage.Name.GetStringLength();
	const int32 SourcePackageNameLen = SourcePackage.Name.GetStringLength();

	TArray <TPair<int32, int32>, TInlineAllocator<64>> MatchingPublicExports;
	MatchingPublicExports.Reserve(ExportCount);

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
				LocalizedPackage.GlobalPackageId,
				*LocalizedPackage.SourcePackageName.ToString(),
				SourcePackage.GlobalPackageId)
			return false;
		}

		int32 CompareResult = FCString::Stricmp(LocalizedExportStr, SourceExportStr);
		if (CompareResult < 0)
		{
			++LocalizedIndex;

			if (LocalizedExportData.GlobalImportIndex != -1)
			{
				FailReason.Appendf(TEXT("Public localized export '%s' is missing in the source package"),
					*LocalizedExportData.ObjectName.ToString());
			}
		}
		else if (CompareResult > 0)
		{
			++SourceIndex;

			if (SourceExportData.GlobalImportIndex != -1)
			{
				FailReason.Appendf(TEXT("Public source export '%s' is missing in the localized package"),
					*SourceExportData.ObjectName.ToString());
			}
		}
		else
		{
			++LocalizedIndex;
			++SourceIndex;

			if (SourceExportData.GlobalImportIndex != -1)
			{
				if (LocalizedExportData.ClassIndex != SourceExportData.ClassIndex)
				{
					const FImportData& LocalizedImportData = GlobalImports[LocalizedExportData.ClassIndex.ToImport()];
					const FImportData& SourceImportData = GlobalImports[SourceExportData.ClassIndex.ToImport()];

					FailReason.Appendf(TEXT("Public export '%s' has class %s (%d) vs. %s (%d)"),
						*LocalizedExportData.ObjectName.ToString(),
						*LocalizedImportData.ObjectName.ToString(),
						LocalizedExportData.ClassIndex.ForDebugging(),
						*SourceImportData.ObjectName.ToString(),
						SourceExportData.ClassIndex.ForDebugging());
				}
				else if (LocalizedExportData.TemplateIndex != SourceExportData.TemplateIndex)
				{
					const FImportData& LocalizedImportData = GlobalImports[LocalizedExportData.TemplateIndex.ToImport()];
					const FImportData& SourceImportData = GlobalImports[SourceExportData.TemplateIndex.ToImport()];

					FailReason.Appendf(TEXT("Public export '%s' has template %s (%d) vs. %s (%d)"),
						*LocalizedExportData.ObjectName.ToString(),
						*LocalizedImportData.ObjectName.ToString(),
						LocalizedExportData.ClassIndex.ForDebugging(),
						*SourceImportData.ObjectName.ToString(),
						SourceExportData.ClassIndex.ForDebugging());
				}
				else if (LocalizedExportData.SuperIndex != SourceExportData.SuperIndex)
				{
					const FImportData& LocalizedImportData = GlobalImports[LocalizedExportData.ClassIndex.ToImport()];
					const FImportData& SourceImportData = GlobalImports[SourceExportData.ClassIndex.ToImport()];

					FailReason.Appendf(TEXT("Public export '%s' has super %s (%d) vs. %s (%d)"),
						*LocalizedExportData.ObjectName.ToString(),
						*LocalizedImportData.ObjectName.ToString(),
						LocalizedExportData.ClassIndex.ForDebugging(),
						*SourceImportData.ObjectName.ToString(),
						SourceExportData.ClassIndex.ForDebugging());
				}
				else
				{
					MatchingPublicExports.Emplace(LocalizedIndex - 1, SourceIndex - 1);
				}
			}
			else if (LocalizedExportData.GlobalImportIndex != -1)
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
				LocalizedPackage.GlobalPackageId,
				*LocalizedPackage.SourcePackageName.ToString(),
				SourcePackage.GlobalPackageId,
				*FailReason);
			bSuccess = false;
		}
	}

	if (bSuccess)
	{
		for (TPair<int32, int32>& Pair : MatchingPublicExports)
		{
			FExportData& LocalizedExportData = GlobalExports[LocalizedPackage.Exports[Pair.Key]];
			const FExportData& SourceExportData = GlobalExports[SourcePackage.Exports[Pair.Value]];

			LocalizedToSourceImportIndexMap.Add(
				LocalizedExportData.GlobalImportIndex,
				SourceExportData.GlobalImportIndex);

			LocalizedExportData.GlobalImportIndex = SourceExportData.GlobalImportIndex;

		}
	LocalizedPackage.FirstGlobalImport = SourcePackage.FirstGlobalImport;
	LocalizedPackage.GlobalImportCount = SourcePackage.GlobalImportCount;
	}

	return bSuccess;
}

void FinalizePackageHeader(
	FPackage* Package,
	const FNameMapBuilder& NameMapBuilder,
	const TArray<FObjectExport>& ObjectExports,
	const TArray<FExportData>& GlobalExports,
	const TMap<FString, int32>& GlobalImportsByFullName,
	FExportBundleMetaEntry* ExportBundleMetaEntries)
{
	// Temporary Archive for serializing ImportMap
	FBufferWriter ImportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (int32 GlobalImportIndex : Package->Imports)
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
		ExportMapEntry.ObjectName[0] = NameMapBuilder.MapName(ObjectExport.ObjectName);
		ExportMapEntry.ObjectName[1] = ObjectExport.ObjectName.GetNumber();
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

	// Global export bundle data
	{
		check(Package->ExportBundles.Num());

		for (int32 ExportBundleIndex = 0; ExportBundleIndex < Package->ExportBundles.Num(); ++ExportBundleIndex)
		{
			FExportBundle& ExportBundle = Package->ExportBundles[ExportBundleIndex];
			FExportBundleMetaEntry* ExportBundleMetaEntry = ExportBundleMetaEntries + Package->FirstExportBundleMetaEntry + ExportBundleIndex;
			ExportBundleMetaEntry->LoadOrder = ExportBundle.LoadOrder;
		}

		FExportBundleMetaEntry* FirstExportBundleMetaEntry = ExportBundleMetaEntries + Package->FirstExportBundleMetaEntry;
		FirstExportBundleMetaEntry->PayloadSize = Package->HeaderData.Num() + Package->ExportsSerialSize;
	}

	uint64 PackageHeaderSize =
		sizeof(FPackageSummary)
		+ Package->NameMapSize
		+ Package->ImportMapSize
		+ Package->ExportMapSize
		+ Package->ExportBundlesHeaderSize
		+ Package->UGraphSize;

	Package->HeaderData.AddZeroed(PackageHeaderSize);
	uint8* PackageHeaderBuffer = Package->HeaderData.GetData();
	FPackageSummary* PackageSummary = reinterpret_cast<FPackageSummary*>(PackageHeaderBuffer);

	PackageSummary->PackageFlags = Package->PackageFlags;
	PackageSummary->CookedHeaderSize = Package->CookedHeaderSize;
	PackageSummary->GraphDataSize = Package->UGraphSize;
	const int32* FindGlobalImportIndexForPackage = GlobalImportsByFullName.Find(Package->Name.ToString());
	PackageSummary->GlobalImportIndex = FindGlobalImportIndexForPackage ? *FindGlobalImportIndexForPackage : -1;

	FBufferWriter SummaryArchive(PackageHeaderBuffer, PackageHeaderSize);
	SummaryArchive.Seek(sizeof(FPackageSummary));

	// NameMap data
	{
		PackageSummary->NameMapOffset = SummaryArchive.Tell();
		SummaryArchive.Serialize(Package->NameIndices.GetData(), Package->NameMapSize);
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

static FIoBuffer CreateExportBundleBuffer(const FPackage* Package, const TArray<FObjectExport>& ObjectExports, const FIoBuffer UExpBuffer)
{
	uint64 BundleBufferSize = Package->HeaderData.Num() + Package->ExportsSerialSize;
	FIoBuffer BundleBuffer(BundleBufferSize);
	FMemory::Memcpy(BundleBuffer.Data(), Package->HeaderData.GetData(), Package->HeaderData.Num());
	uint64 BundleBufferOffset = Package->HeaderData.Num();
	for (const FExportBundle& ExportBundle : Package->ExportBundles)
	{
		for (const FExportGraphNode* Node : ExportBundle.Nodes)
		{
			if (Node->BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + Node->BundleEntry.LocalExportIndex];
				const int64 Offset = ObjectExport.SerialOffset - Package->UAssetSize;
				FMemory::Memcpy(BundleBuffer.Data() + BundleBufferOffset, UExpBuffer.Data() + Offset, ObjectExport.SerialSize);
				BundleBufferOffset += ObjectExport.SerialSize;
			}
		}
	}
	return BundleBuffer;
}

static void ParsePackageAssets(
	FNameMapBuilder& NameMapBuilder,
	TArray<FPackage*>& Packages,
	FPackageAssetData& PackageAssetData,
	FGlobalPackageData& GlobalPackageData,
	FExportGraph& ExportGraph)
{
	IOSTORE_CPU_SCOPE(ParsePackageAssets);

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
			check(FileHandle);
			bool bSuccess = FileHandle->Read(Buffer, Package->UAssetSize);
			check(bSuccess);
			delete FileHandle;
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

				TArray<FName> Names;
				Names.Reserve(Summary.NameCount);
				Package.NameMap.Reserve(Summary.NameCount);
				Package.NameIndices.Reserve(Summary.NameCount);
				FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);

				for (int32 I = 0; I < Summary.NameCount; ++I)
				{
					Ar << NameEntry;
					Names.Emplace(NameEntry);
				}

				NameMapBuilder.MarkNamesAsReferenced(Names, Package.NameMap, Package.NameIndices);
			}

			Package.NameMapSize = Package.NameIndices.Num() * Package.NameIndices.GetTypeSize();
		}

		ParallelFor(TotalPackageCount,[
				&ParseCount,
				&PackageAssetBuffers,
				&PackageFileSummaries,
				&Packages,
				&NameMapBuilder,
				&PackageAssetData,
				&GlobalPackageData,
				&ExportGraph](int32 Index)
		{
			uint8* PackageBuffer = PackageAssetBuffers[Index];
			const FPackageFileSummary& Summary = PackageFileSummaries[Index];
			FPackage& Package = *Packages[Index];
			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			FMemoryReaderView Ar(MemView);

			IOSTORE_CPU_SCOPE_DATA(ParsePackage, TCHAR_TO_ANSI(*Package.FileName));

			const int32 Count = ParseCount.IncrementExchange();
			UE_CLOG(Count % 1000 == 0, LogIoStore, Display, TEXT("Parsing %d/%d: '%s'"), Count, Packages.Num(), *Package.FileName);

			if (Summary.ImportCount > 0)
			{
				FNameProxyArchive ProxyAr(Ar, Package.NameMap);
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
				FNameProxyArchive ProxyAr(Ar, Package.NameMap);
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
	FPackageAssetData& PackageAssetData,
	FGlobalPackageData& GlobalPackageData,
	FExportGraph& ExportGraph)
{
	IOSTORE_CPU_SCOPE(CreateGlobalImportsAndExports);

	TArray<FString> TmpFullNames;
	for (FPackage* Package : Packages)
	{
		if (Package->ImportCount > 0)
		{
			Package->ImportedFullNames.SetNum(Package->ImportCount);
			for (int32 ImportIndex = 0; ImportIndex < Package->ImportCount; ++ImportIndex)
			{
				FindImport(
					GlobalPackageData.Imports,
					Package->ImportedFullNames,
					PackageAssetData.ObjectImports.GetData() + Package->ImportIndexOffset,
					ImportIndex);
			}
		}

		if (Package->ExportCount > 0)
		{
			TmpFullNames.Reset();
			TmpFullNames.SetNum(Package->ExportCount);

			for (int32 ExportIndex = 0; ExportIndex < Package->ExportCount; ExportIndex++)
			{
				FindExport(
					GlobalPackageData.Exports,
					TmpFullNames,
					PackageAssetData.ObjectExports.GetData() + Package->ExportIndexOffset,
					ExportIndex,
					Package->Name);
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
}

void ProcessLocalizedPackages(
	const TArray<FPackage*>& Packages,
	const FPackageMap& PackageMap,
	const TArray<FImportData>& GlobalImports,
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
				Package->GlobalPackageId)
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
				Package->GlobalPackageId);
			continue;
		}

		Package->SourceGlobalPackageId = SourcePackage->GlobalPackageId;

		Package->bIsLocalizedAndConformed = ConformLocalizedPackage(
			PackageMap, GlobalImports, *SourcePackage,
			*Package, GlobalExports, LocalizedToSourceImportIndexMap);

		if (Package->bIsLocalizedAndConformed)
		{
			UE_LOG(LogIoStore, Verbose, TEXT("For culture '%s': Adding conformed localized package '%s' (%d) for '%s' (%d). ")
				TEXT("When loading the source package it will be remapped to this localized package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId,
				*Package->SourcePackageName.ToString(),
				SourcePackage->GlobalPackageId);

			OutSourceToLocalizedPackageMap.Add(SourcePackage, Package);
			OutCulturePackageMap.FindOrAdd(Package->Region).Add(SourcePackage->GlobalPackageId, Package->GlobalPackageId);
		}
		else
		{
			UE_LOG(LogIoStore, Warning,
				TEXT("For culture '%s': Localized package '%s' (%d) does not conform to source package '%s' (%d) due to mismatching public exports. ")
				TEXT("When loading the source package will never be remapped to this localized package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId,
				*Package->SourcePackageName.ToString(),
				SourcePackage->GlobalPackageId);
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
					Package->GlobalPackageId,
					*LocalizedPackage->Name.ToString(),
					LocalizedPackage->GlobalPackageId);
			}
		}
		Package->ImportedPackagesSerializeCount = Package->ImportedPackages.Num();
		Package->ImportedPackages.Append(LocalizedPackages);
	}

	UE_LOG(LogIoStore, Display, TEXT("Conforming localized imports..."));
	for (FPackage* Package : Packages)
	{
		for (int32& GlobalImportIndex : Package->Imports)
		{
			const FImportData& ImportData = GlobalImports[GlobalImportIndex];
			if (ImportData.bIsLocalized)
			{
				const int32* SourceGlobalImportIndex = LocalizedToSourceImportIndexMap.Find(GlobalImportIndex);
				if (SourceGlobalImportIndex)
				{
					GlobalImportIndex = *SourceGlobalImportIndex;

					const FImportData& SourceImportData = GlobalImports[*SourceGlobalImportIndex];
					UE_LOG(LogIoStore, Verbose,
						TEXT("For package '%s' (%d): Remap localized import %s to source import %s (in a conformed localized package)"),
						*Package->Name.ToString(),
						Package->GlobalPackageId,
						*ImportData.FullName,
						*SourceImportData.FullName);
				}
				else
				{
					UE_LOG(LogIoStore, Verbose,
						TEXT("For package '%s' (%d): Skip remap for localized import %s")
						TEXT(", either there is no source package or the localized package did not conform to it."),
						*Package->Name.ToString(),
						Package->GlobalPackageId,
						*ImportData.FullName);
				}
			}
		}
	}
}

static void SaveReleaseVersionMeta(const TCHAR* ReleaseVersionOutputDir, const FNameMapBuilder& NameMap, const TMap<FName, int32> PackageGlobalIdMap, const TArray<FContainerTargetSpec>& ContainerTargets)
{
	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(ReleaseVersionOutputDir);

	{
		FString NameMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.unamemap"));
		TUniquePtr<FArchive> NameMapArchive(IFileManager::Get().CreateFileWriter(*NameMapOutputPath));
		(*NameMapArchive) << const_cast<FNameMapBuilder&>(NameMap);
	}

	{
		FString PackageMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.upackagemap"));
		TUniquePtr<FArchive> PackageMapArchive(IFileManager::Get().CreateFileWriter(*PackageMapOutputPath));

		TArray<FName> OrderedGlobalIdMap;
		OrderedGlobalIdMap.SetNum(PackageGlobalIdMap.Num());
		for (const auto& KV : PackageGlobalIdMap)
		{
			FName PackageName = KV.Key;
			int32 PackageGlobalId = KV.Value;
			OrderedGlobalIdMap[PackageGlobalId] = PackageName;
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

	for (const FContainerTargetSpec& ContainerTarget : ContainerTargets)
	{
		TArray<FTocMetaEntry> TocMetaEntries;
		TocMetaEntries.Reserve(ContainerTarget.TargetFiles.Num());
		for (const FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
		{
			FTocMetaEntry& TocMetaEntry = TocMetaEntries.AddDefaulted_GetRef();
			TocMetaEntry.Offset = TargetFile.Offset;
			TocMetaEntry.Size = TargetFile.TargetSize;
			TocMetaEntry.Hash = TargetFile.Hash;
		}

		FString TocMetaOutputPath = FPaths::Combine(ReleaseVersionOutputDir, ContainerTarget.Name + TEXT(".utocmeta"));
		TUniquePtr<FArchive> TocMetaArchive(IFileManager::Get().CreateFileWriter(*TocMetaOutputPath));
		(*TocMetaArchive) << TocMetaEntries;
	}
}

static void LoadReleaseVersionMeta(const TCHAR* ReleaseVersionOutputDir, FNameMapBuilder& NameMap, TMap<FName, int32>& PackageGlobalIdMap, TArray<FContainerTargetSpec>& ContainerTargets)
{
	{
		FString NameMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.unamemap"));
		TUniquePtr<FArchive> NameMapArchive(IFileManager::Get().CreateFileReader(*NameMapOutputPath));
		if (NameMapArchive)
		{
			(*NameMapArchive) << NameMap;
		}
	}
	{
		FString PackageMapOutputPath = FPaths::Combine(ReleaseVersionOutputDir, TEXT("iodispatcher.upackagemap"));
		TUniquePtr<FArchive> PackageMapArchive(IFileManager::Get().CreateFileReader(*PackageMapOutputPath));
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
				PackageGlobalIdMap.Add(Name, PackageIndex);
			}
			check(PackageGlobalIdMap.Num() == PackageCount);
		}
	}

	for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
	{
		FString TocMetaOutputPath = FPaths::Combine(ReleaseVersionOutputDir, ContainerTarget.Name + TEXT(".utocmeta"));
		TUniquePtr<FArchive> TocMetaArchive(IFileManager::Get().CreateFileReader(*TocMetaOutputPath));
		if (TocMetaArchive)
		{
			(*TocMetaArchive) << ContainerTarget.PreviousBuildToc;
		}
	}
}

int32 CreateTarget(const FIoStoreArguments& Arguments, const FIoStoreWriterSettings& GeneralIoWriterSettings)
{
	TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);

	FPackageStoreBulkDataManifest BulkDataManifest(FString(Arguments.CookedDir) / FApp::GetProjectName());
	if (!BulkDataManifest.Load())
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to load Bulk Data manifest %s"), *BulkDataManifest.GetFilename());
		return -1;
	}
	UE_LOG(LogIoStore, Display, TEXT("Loaded Bulk Data manifest '%s'"), *BulkDataManifest.GetFilename());
	
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.CreateOutputFile(CookedDir);
#endif

	FNameMapBuilder NameMapBuilder;
	FPackageAssetData PackageAssetData;
	FGlobalPackageData GlobalPackageData;
	FExportGraph ExportGraph;
	TArray<FExportBundleMetaEntry> ExportBundleMetaEntries;

	TArray<FPackage*> Packages;
	FPackageMap PackageMap;
	TMap<FName, int32> PackageGlobalIdMap;

#if OUTPUT_DEBUG_PACKAGE_HASHES
	TMap<FName, FPackageHashes> PreviousBuildPackageHashes;
#endif

	TArray<FContainerTargetSpec> ContainerTargets;
	UE_LOG(LogIoStore, Display, TEXT("Creating container targets..."));
	{
		IOSTORE_CPU_SCOPE(CreateContainerTargets);

		int32 CookedDirLen = Arguments.CookedDir.Len() + 1;
		for (const FContainerSourceSpec& Container : Arguments.Containers)
		{
			FContainerTargetSpec& ContainerTarget = ContainerTargets.AddDefaulted_GetRef();
			ContainerTarget.OutputPath = Container.OutputPath;
			ContainerTarget.Name = FPaths::GetBaseFilename(ContainerTarget.OutputPath);
		}

		if (!Arguments.BasedOnReleaseVersionDir.IsEmpty())
		{
			LoadReleaseVersionMeta(*Arguments.BasedOnReleaseVersionDir, NameMapBuilder, PackageGlobalIdMap, ContainerTargets);
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

		for (int32 ContainerIndex = 0; ContainerIndex < ContainerTargets.Num(); ++ContainerIndex)
		{
			const FContainerSourceSpec& Container = Arguments.Containers[ContainerIndex];
			FContainerTargetSpec& ContainerTarget = ContainerTargets[ContainerIndex];
			{
				IOSTORE_CPU_SCOPE(ProcessSourceFiles);
				for (const FContainerSourceFile& SourceFile : Container.SourceFiles)
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

					FString RelativeFileName = TEXT("../../../");
					RelativeFileName.AppendChars(*NormalizedSourcePath + CookedDirLen, NormalizedSourcePath.Len() - CookedDirLen);
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
						FContainerTargetFile& TargetFile = ContainerTarget.TargetFiles.AddDefaulted_GetRef();
						TargetFile.SourceSize = uint64(CookedFileStatData->FileSize);
						TargetFile.NormalizedSourcePath = NormalizedSourcePath;
						TargetFile.TargetPath = MoveTemp(RelativeFileName);
						TargetFile.Package = Package;
						if (SourceFile.bNeedsCompression)
						{
							ContainerTarget.bIsCompressed = true;
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
			}
		}
	}

	Algo::Sort(Packages, [](const FPackage* A, const FPackage* B)
	{
		return A->GlobalPackageId < B->GlobalPackageId;
	});

	UE_LOG(LogIoStore, Display, TEXT("Parsing packages..."));
	ParsePackageAssets(NameMapBuilder, Packages, PackageAssetData, GlobalPackageData, ExportGraph);

	UE_LOG(LogIoStore, Display, TEXT("Creating global imports and exports..."));
	CreateGlobalImportsAndExports(Packages, PackageAssetData, GlobalPackageData, ExportGraph);

	TArray<FImportData>& GlobalImports = GlobalPackageData.Imports.Objects;
	TArray<FExportData>& GlobalExports = GlobalPackageData.Exports.Objects;
	TMap<FString, int32>& GlobalImportsByFullName = GlobalPackageData.Imports.ObjectsByFullName;
	TMap<FString, int32>& GlobalExportsByFullName = GlobalPackageData.Exports.ObjectsByFullName;

	int32 NumScriptImports = 0;
	{
		IOSTORE_CPU_SCOPE(GlobalImports);

		// Sort imports by script objects first
		GlobalImports.Sort();

		// build remap from old global import index to new sorted global import index
		TMap<int32, int32> Remap;
		Remap.Reserve(GlobalImports.Num() + 1);
		Remap.Add(-1, -1);
		for (int32 I = 0; I < GlobalImports.Num(); ++I)
		{
			FImportData& Import = GlobalImports[I];
			Remap.Add(Import.GlobalIndex, I);
		}

		// remap all global import indices and lookup package pointers and export indices
		FPackage* LastPackage = nullptr; 
		for (int32 I = 0; I < GlobalImports.Num(); ++I)
		{
			FImportData& GlobalImport = GlobalImports[I];

			GlobalImport.GlobalIndex = Remap[GlobalImport.GlobalIndex];
			GlobalImport.OuterIndex = Remap[GlobalImport.OuterIndex];
			GlobalImport.OutermostIndex = Remap[GlobalImport.OutermostIndex];

			if (!GlobalImport.bIsScript)
			{
				if (NumScriptImports == 0)
				{
					NumScriptImports = I;
				}

				if (GlobalImport.bIsPackage)
				{
					if (LastPackage)
					{
						LastPackage->GlobalImportCount = I - LastPackage->FirstGlobalImport;
					}
					FPackage* FindPackage = PackageMap.FindRef(GlobalImport.ObjectName);
					if (!FindPackage)
					{
						UE_LOG(LogIoStore, Warning, TEXT("Missing import package '%s'"), *GlobalImport.ObjectName.ToString());
						LastPackage = nullptr;
						GlobalImport.bIsMissingImport = true;
					}
					else
					{
						FindPackage->FirstGlobalImport = I;
						LastPackage = FindPackage;
						GlobalImport.Package = FindPackage;
					}
				}
				else
				{
					int32* FindGlobalExport = GlobalExportsByFullName.Find(GlobalImport.FullName);
					if (!FindGlobalExport)
					{
						UE_LOG(LogIoStore, Warning, TEXT("Missing import object '%s'"), *GlobalImport.FullName);
						GlobalImport.bIsMissingImport = true;
					}
					else
					{
						GlobalImport.GlobalExportIndex = *FindGlobalExport;
					}
				}
			}

			GlobalImportsByFullName[GlobalImport.FullName] = I;
		}
		if (LastPackage)
		{
			LastPackage->GlobalImportCount = GlobalImports.Num() - LastPackage->FirstGlobalImport;
		}
	}

	{
		IOSTORE_CPU_SCOPE(GlobalExports);

		for (FExportData& GlobalExport : GlobalPackageData.Exports.Objects)
		{
			int32* FindGlobalImport = GlobalPackageData.Imports.ObjectsByFullName.Find(GlobalExport.FullName);
			GlobalExport.GlobalImportIndex = FindGlobalImport ? *FindGlobalImport : -1;
		}
	}

#if OUTPUT_NAMEMAP_CSV
	FString CsvFilePath = OutputDir / TEXT("AllImports.csv");
	TUniquePtr<FArchive> CsvArchive(IFileManager::Get().CreateFileWriter(*CsvFilePath));
	if (CsvArchive)
	{
		ANSICHAR Line[MAX_SPRINTF + FName::StringBufferSize];
		ANSICHAR Header[] = "Count\tOuter\tOutermost\tImportName\n";
		CsvArchive->Serialize(Header, sizeof(Header) - 1);
		for (const FImportData& ImportData : GlobalImports)
		{
			FCStringAnsi::Sprintf(Line, "%d\t%d\t%d\t",
				ImportData.RefCount, ImportData.OuterIndex, ImportData.OutermostIndex);
			ANSICHAR* L = Line + FCStringAnsi::Strlen(Line);
			const TCHAR* N = *ImportData.FullName;
			while (*N)
			{
				*L++ = CharCast<ANSICHAR,TCHAR>(*N++);
			}
			*L++ = '\n';
			CsvArchive.Get()->Serialize(Line, L - Line);
		}
	}
#endif


	// Lookup global indices and package pointers for all imports before adding preload and postload arcs
	UE_LOG(LogIoStore, Display, TEXT("Looking up import packages..."));
	{
		IOSTORE_CPU_SCOPE(ImportedPackages);

		for (FPackage* Package : Packages)
		{
			bool bHasMissingImports = false;
			Package->ImportedPackages.Reserve(Package->ImportCount);
			for (int32 I = 0; I < Package->ImportCount; ++I)
			{
				int32 GlobalImportIndex = *GlobalImportsByFullName.Find(Package->ImportedFullNames[I]);

				FImportData& ImportData = GlobalImports[GlobalImportIndex];
				bHasMissingImports |= ImportData.bIsMissingImport;
				Package->Imports.Add(ImportData.GlobalIndex);
				if (ImportData.Package)
				{
					Package->ImportedPackages.Add(ImportData.Package);
					ImportData.Package->ImportedByPackages.Add(Package);
				}
			}
			UE_CLOG(bHasMissingImports, LogIoStore, Warning, TEXT("Missing imports for package '%s'"), *Package->Name.ToString());
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Converting export map import indices..."));
	{
		IOSTORE_CPU_SCOPE(ExportData);

		for (FPackage* Package : Packages)
		{
			for (int32 I = 0; I < Package->ExportCount; ++I)
			{
				FObjectExport& ObjectExport = PackageAssetData.ObjectExports[Package->ExportIndexOffset + I];
				FExportData& ExportData = GlobalPackageData.Exports.Objects[Package->Exports[I]];

				check(!ObjectExport.OuterIndex.IsImport());
				ExportData.OuterIndex = ObjectExport.OuterIndex;
				ExportData.ClassIndex =
					ObjectExport.ClassIndex.IsImport() ?
					FPackageIndex::FromImport(Package->Imports[ObjectExport.ClassIndex.ToImport()]) :
					ObjectExport.ClassIndex;
				ExportData.SuperIndex =
					ObjectExport.SuperIndex.IsImport() ?
					FPackageIndex::FromImport(Package->Imports[ObjectExport.SuperIndex.ToImport()]) :
					ObjectExport.SuperIndex;
				ExportData.TemplateIndex =
					ObjectExport.TemplateIndex.IsImport() ?
					FPackageIndex::FromImport(Package->Imports[ObjectExport.TemplateIndex.ToImport()]) :
					ObjectExport.TemplateIndex;
			}
		}
	}

	FSourceToLocalizedPackageMultimap SourceToLocalizedPackageMap;
	FCulturePackageMap CulturePackageMap;

	ProcessLocalizedPackages(
		Packages, PackageMap, GlobalImports, GlobalExports,
		CulturePackageMap, SourceToLocalizedPackageMap);

	int32 CircularChainCount = 0;

	UE_LOG(LogIoStore, Display, TEXT("Adding postload dependencies..."));
	{
		IOSTORE_CPU_SCOPE(PostLoadDependencies);

		TSet<FPackage*> Visited;
		TSet<FCircularImportChain> CircularChains;

		for (FPackage* Package : Packages)
		{
			Visited.Reset();
			Visited.Add(Package);
			AddPostLoadDependencies(*Package, Visited, CircularChains);
		}
		CircularChainCount = CircularChains.Num();
	}
		
	UE_LOG(LogIoStore, Display, TEXT("Adding preload dependencies..."));
	{
		IOSTORE_CPU_SCOPE(PreLoadDependencies);

		TArray<FPackage*> LocalizedPackages;
		for (FPackage* Package : Packages)
		{
			// Convert PreloadDependencies to arcs
			for (int32 I = 0; I < Package->ExportCount; ++I)
			{
				FObjectExport& ObjectExport = PackageAssetData.ObjectExports[Package->ExportIndexOffset + I];
				FExportData& ExportData = GlobalPackageData.Exports.Objects[Package->Exports[I]];
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
						FImportData& Import = GlobalImports[Package->Imports[Dep.ToImport()]];
						check(!Import.bIsPackage);
						if (Import.bIsScript)
						{
							// Add script arc with null package and global import index as node index
							AddScriptArc(*Package, Import.GlobalIndex, I, PhaseTo);
						}
						else
						{
							if (Import.GlobalExportIndex != -1)
							{
								FExportData& Export = GlobalExports[Import.GlobalExportIndex];

								FPackage* SourcePackage = PackageMap.FindRef(Export.SourcePackageName);
								check(SourcePackage);

								AddExternalExportArc(ExportGraph, *SourcePackage, Export.SourceIndex, PhaseFrom, *Package, I, PhaseTo);
								Package->ImportedPreloadPackages.Add(SourcePackage);

								LocalizedPackages.Reset();
								SourceToLocalizedPackageMap.MultiFind(SourcePackage, LocalizedPackages);
								for (FPackage* LocalizedPackage : LocalizedPackages)
								{
									UE_LOG(LogIoStore, Verbose, TEXT("For package '%s' (%d): Adding localized preload dependency '%s' in '%s'"),
										*Package->Name.ToString(),
										Package->GlobalPackageId,
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
	}

	UE_LOG(LogIoStore, Display, TEXT("Building bundles..."));
	BuildBundles(ExportGraph, Packages);

	TUniquePtr<FLargeMemoryWriter> StoreTocArchive = MakeUnique<FLargeMemoryWriter>(0, true);
	TUniquePtr<FLargeMemoryWriter> ImportedPackagesArchive = MakeUnique<FLargeMemoryWriter>(0, true);
	TUniquePtr<FLargeMemoryWriter> GlobalImportNamesArchive = MakeUnique<FLargeMemoryWriter>(0, true);
	TUniquePtr<FLargeMemoryWriter> InitialLoadArchive = MakeUnique<FLargeMemoryWriter>(0, true);

	UE_LOG(LogIoStore, Display, TEXT("Serializing global import names..."));
	if (GlobalImportNamesArchive)
	{
		IOSTORE_CPU_SCOPE(SerializeGlobalImportNames);

		for (const FImportData& ImportData : GlobalImports)
		{
			NameMapBuilder.SerializeName(*GlobalImportNamesArchive, ImportData.ObjectName);
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Serializing initial load..."));
	// Separate file for script arcs that are only required during initial loading
	if (InitialLoadArchive)
	{
		IOSTORE_CPU_SCOPE(SerializeInitialLoad);

		int32 PackageCount = PackageMap.Num();
		*InitialLoadArchive << PackageCount;
		*InitialLoadArchive << NumScriptImports;

		FBufferWriter ScriptArcsArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);

		for (auto& PackageKV : PackageMap)
		{
			FPackage& Package = *PackageKV.Value;

			int32 ScriptArcsOffset = ScriptArcsArchive.Tell();
			int32 ScriptArcsCount = Package.ScriptArcs.Num();

			*InitialLoadArchive << ScriptArcsOffset;
			*InitialLoadArchive << ScriptArcsCount;

			for (FArc& ScriptArc : Package.ScriptArcs)
			{
				ScriptArcsArchive << ScriptArc.FromNodeIndex;
				ScriptArcsArchive << ScriptArc.ToNodeIndex;
			}
		}

		for (int32 I = 0; I < NumScriptImports; ++I)
		{
			FImportData& ImportData = GlobalImports[I];
			FPackageIndex OuterIndex = 
				ImportData.OuterIndex >= 0 ?
				FPackageIndex::FromImport(ImportData.OuterIndex) :
				FPackageIndex();

			*InitialLoadArchive << OuterIndex;
		}

		InitialLoadArchive->Serialize(ScriptArcsArchive.GetWriterData(), ScriptArcsArchive.Tell());
	}

	{
		IOSTORE_CPU_SCOPE(SerializeStoreToc);

		for (FPackage* Package : Packages)
		{
			Package->FirstExportBundleMetaEntry = ExportBundleMetaEntries.Num();
			ExportBundleMetaEntries.AddDefaulted(Package->ExportBundles.Num());

			NameMapBuilder.MarkNameAsReferenced(Package->Name);
			NameMapBuilder.SerializeName(*StoreTocArchive, Package->Name);
			*StoreTocArchive << Package->SourceGlobalPackageId;
			*StoreTocArchive << Package->ExportCount;
			int32 ExportBundleCount = Package->ExportBundles.Num();
			*StoreTocArchive << ExportBundleCount;
			*StoreTocArchive << Package->FirstExportBundleMetaEntry;
			*StoreTocArchive << Package->FirstGlobalImport;
			*StoreTocArchive << Package->GlobalImportCount;
			int32 ImportedPackagesCount = Package->ImportedPackagesSerializeCount;
			*StoreTocArchive << ImportedPackagesCount;
			int32 ImportedPackagesOffset = ImportedPackagesArchive->Tell();
			*StoreTocArchive << ImportedPackagesOffset;

			for (int32 I = 0; I < Package->ImportedPackagesSerializeCount; ++I)
			{
				FPackage* ImportedPackage = Package->ImportedPackages[I];
				*ImportedPackagesArchive << ImportedPackage->GlobalPackageId;
			}
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Finalizing package headers..."));
	{
		IOSTORE_CPU_SCOPE(FinalizePackageHeaders);

		for (FPackage* Package : Packages)
		{
			FinalizePackageHeader(
				Package,
				NameMapBuilder,
				PackageAssetData.ObjectExports,
				GlobalPackageData.Exports.Objects,
				GlobalPackageData.Imports.ObjectsByFullName,
				ExportBundleMetaEntries.GetData());
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Calculating hashes"));
	{
		IOSTORE_CPU_SCOPE(CalculateHashes);

		int32 TotalFileCount = 0;
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			TotalFileCount += ContainerTarget.TargetFiles.Num();
		}
		TArray<FContainerTargetFile*> AllTargetFiles;
		AllTargetFiles.Reserve(TotalFileCount);
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
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
				IoBuffer = CreateExportBundleBuffer(TargetFile->Package, ObjectExports, IoBuffer);
				TargetFile->TargetSize = IoBuffer.DataSize();
			}
			else
			{
				TargetFile->TargetSize = TargetFile->SourceSize;
			}
			FSHA1::HashBuffer(IoBuffer.Data(), IoBuffer.DataSize(), TargetFile->Hash.Hash);
#if OUTPUT_DEBUG_PACKAGE_HASHES
			if (!TargetFile->bIsBulkData)
			{
				TargetFile->Package->Hashes.ExportBundleHash = TargetFile->Hash;
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

	UE_LOG(LogIoStore, Display, TEXT("Creating disk layout..."));
	CreateDiskLayout(ContainerTargets, Packages, Arguments.PackageOrderMap, Arguments.CookerOrderMap, GeneralIoWriterSettings.CompressionBlockSize, Arguments.MemoryMappingAlignment);

	UE_LOG(LogIoStore, Display, TEXT("Serializing container(s)..."));
	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext(new FIoStoreWriterContext());
	TArray<FIoStoreWriter*> IoStoreWriters;
	FIoStoreEnvironment GlobalIoStoreEnv;
	FIoStoreWriter* GlobalIoStoreWriter;
	{
		IOSTORE_CPU_SCOPE(InitializeIoStoreWriters);
		FString GlobalOutputPath = FString(Arguments.OutputDir) / TEXT("global");
		GlobalIoStoreEnv.InitializeFileEnvironment(*GlobalOutputPath);
		GlobalIoStoreWriter = new FIoStoreWriter(GlobalIoStoreEnv);
		IoStoreWriters.Add(GlobalIoStoreWriter);
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			if (ContainerTarget.OutputPath.IsEmpty())
			{
				ContainerTarget.IoStoreWriter = GlobalIoStoreWriter;
			}
			else
			{
				ContainerTarget.IoStoreEnv.Reset(new FIoStoreEnvironment());
				ContainerTarget.IoStoreEnv->InitializeFileEnvironment(ContainerTarget.OutputPath);
				ContainerTarget.IoStoreWriter = new FIoStoreWriter(*ContainerTarget.IoStoreEnv);
				IoStoreWriters.Add(ContainerTarget.IoStoreWriter);
			}
		}
		FIoStatus IoStatus = IoStoreWriterContext->Initialize(GeneralIoWriterSettings);
		check(IoStatus.IsOk());
		IoStatus = GlobalIoStoreWriter->Initialize(*IoStoreWriterContext, /* bIsCompressed */ false);
		check(IoStatus.IsOk());
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			IoStatus = ContainerTarget.IoStoreWriter->Initialize(*IoStoreWriterContext, ContainerTarget.bIsCompressed);
			check(IoStatus.IsOk());
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
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			TotalFileCount += ContainerTarget.TargetFiles.Num();
		}

		TArray<FReadFileTask> ReadFileTasks;
		ReadFileTasks.SetNum(TotalFileCount);
		int32 ReadFileTaskIndex = 0;
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
			{
				FReadFileTask& ReadFileTask = ReadFileTasks[ReadFileTaskIndex++];
				ReadFileTask.ContainerTargetFile = &TargetFile;
				ReadFileTask.IoStoreWriter = ContainerTarget.IoStoreWriter;
			}
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
					ReadFileTask.IoBuffer = CreateExportBundleBuffer(TargetFile.Package, ObjectExports, ReadFileTask.IoBuffer);
				}

				if (TargetFile.Padding)
				{
					const FIoStatus PaddingResult = ReadFileTask.IoStoreWriter->AppendPadding(TargetFile.Padding);
					if (!PaddingResult.IsOk())
					{
						UE_LOG(LogIoStore, Warning, TEXT("Failed to append padding due to: %s"), *PaddingResult.ToString());
					}
				}
				FIoWriteOptions WriteOptions;
				WriteOptions.DebugName = *TargetFile.TargetPath;
				WriteOptions.bForceUncompressed = TargetFile.bForceUncompressed;
				WriteOptions.Alignment = TargetFile.Alignment;
				FIoStatus Status = ReadFileTask.IoStoreWriter->Append(TargetFile.ChunkId, ReadFileTask.IoBuffer, WriteOptions);
				UE_CLOG(!Status.IsOk(), LogIoStore, Fatal, TEXT("Failed to append chunk to container file due to '%s'"), *Status.ToString());

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
		IOSTORE_CPU_SCOPE(MapAdditionalBulkDataChunkIds);
		for (FContainerTargetSpec& ContainerTarget : ContainerTargets)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
			{
				if (TargetFile.bIsBulkData)
				{
					MapAdditionalBulkDataChunks(TargetFile, BulkDataManifest, ContainerTarget.IoStoreWriter);
				}
			}
		}
	}

	int32 StoreTocByteCount = StoreTocArchive->TotalSize();
	int32 ImportedPackagesByteCount = ImportedPackagesArchive->TotalSize();
	int32 GlobalImportNamesByteCount = GlobalImportNamesArchive->TotalSize();
	int32 ExportBundleMetaByteCount = ExportBundleMetaEntries.Num() * sizeof(FExportBundleMetaEntry);
	{
		IOSTORE_CPU_SCOPE(SerializeGlobalMetaData);

		UE_LOG(LogIoStore, Display, TEXT("Saving global meta data to container file"));
		FLargeMemoryWriter GlobalMetaArchive(0, true);

		GlobalMetaArchive << StoreTocByteCount;
		GlobalMetaArchive.Serialize(StoreTocArchive->GetData(), StoreTocByteCount);

		GlobalMetaArchive << ImportedPackagesByteCount;
		GlobalMetaArchive.Serialize(ImportedPackagesArchive->GetData(), ImportedPackagesByteCount);

		GlobalMetaArchive << GlobalImportNamesByteCount;
		GlobalMetaArchive.Serialize(GlobalImportNamesArchive->GetData(), GlobalImportNamesByteCount);

		GlobalMetaArchive << ExportBundleMetaByteCount;
		GlobalMetaArchive.Serialize(ExportBundleMetaEntries.GetData(), ExportBundleMetaByteCount);

		GlobalMetaArchive << CulturePackageMap;

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
		const FIoStatus Status = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoBuffer(FIoBuffer::Wrap, InitialLoadArchive->GetData(), InitialLoadArchive->TotalSize()), WriteOptions);
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
		SaveNameBatch(NameMapBuilder.GetNameMap(), /* out */ Names, /* out */ Hashes);

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
		SaveReleaseVersionMeta(*Arguments.OutputReleaseVersionDir, NameMapBuilder, PackageGlobalIdMap, ContainerTargets);
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
		uint64 ModifiedUExpCount = 0;
		uint64 ModifiedExportBundlesCount = 0;
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
				}
				if (PreviousHashes->UExpHash != Package->Hashes.UExpHash)
				{
					++ModifiedUExpCount;
					bModified = true;
				}
				if (PreviousHashes->ExportBundleHash != Package->Hashes.ExportBundleHash)
				{
					++ModifiedExportBundlesCount;
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
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (export bundle): %d"), ModifiedExportBundlesCount);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (uasset|uexp): %d"), ModifiedCountUAssetOrUExpCount);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (uasset): %d"), ModifiedUAssetCount);
		UE_LOG(LogIoStore, Display, TEXT("Modified packages (uexp): %d"), ModifiedUExpCount);
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
	uint64 InitialLoadSize = InitialLoadArchive->Tell();
	uint64 ScriptArcsCount = 0;
	uint64 CircularPackagesCount = 0;
	uint64 TotalInternalArcCount = 0;
	uint64 TotalExternalArcCount = 0;
	uint64 NameCount = 0;

	uint64 PackagesWithoutImportDependenciesCount = 0;
	uint64 PackagesWithoutPreloadDependenciesCount = 0;
	uint64 BundleCount = 0;
	uint64 BundleEntryCount = 0;

	uint64 PackageHeaderSize = 0;

	uint64 UniqueImportPackages = 0;
	for (const FImportData& ImportData : GlobalImports)
	{
		UniqueImportPackages += (ImportData.OuterIndex == 0);
	}

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
		NameMapCount += Package.NameIndices.Num();
		ScriptArcsCount += Package.ScriptArcs.Num();
		CircularPackagesCount += Package.bHasCircularImportDependencies;
		TotalInternalArcCount += Package.InternalArcs.Num();
		ImportedPackagesCount += Package.ImportedPackages.Num();
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
	UE_LOG(LogIoStore, Display, TEXT("%-35s %15s %15s %15s %25s"), TEXT("Container"), TEXT("TOC Size (KB)"), TEXT("TOC Entries"), TEXT("Size (MB)"), TEXT("Compressed (MB)"));
	UE_LOG(LogIoStore, Display, TEXT("-------------------------------------------------------------------------------------------------------------"));
	for (const FIoStoreWriterResult& Result : IoStoreWriterResults)
	{
		const double Compression = Result.CompressionMethod == NAME_None
			? 0.0
			: (double(Result.UncompressedContainerSize - Result.CompressedContainerSize) / double(Result.UncompressedContainerSize)) * 100.0;

		UE_LOG(LogIoStore, Display, TEXT("%-35s %15.2lf %15d %15.2lf %25s"),
			*Result.ContainerName,
			(double)Result.TocSize / 1024.0,
			Result.TocEntryCount,
			(double)Result.UncompressedContainerSize / 1024.0 / 1024.0,
			*FString::Printf(TEXT("%.2lf (%.2lf%% %s)"),
				(double)Result.CompressedContainerSize / 1024.0 / 1024.0,
				Compression,
				*Result.CompressionMethod.ToString()));
	}

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("Packages: %8d total, %d circular dependencies, %d no preload dependencies, %d no import dependencies"),
		PackageMap.Num(), CircularPackagesCount, PackagesWithoutPreloadDependenciesCount, PackagesWithoutImportDependenciesCount);
	UE_LOG(LogIoStore, Display, TEXT("Bundles:  %8d total, %d entries, %d export objects"), BundleCount, BundleEntryCount, GlobalPackageData.Exports.Objects.Num());

	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalNames, %d unique names"), (double)GlobalNamesMB, NameMapBuilder.GetNameMap().Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalNameHashes"), (double)GlobalNameHashesMB);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalPackageData"), (double)StoreTocByteCount / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalImportedPackages, %d imported packages"), (double)ImportedPackagesByteCount / 1024.0 / 1024.0, ImportedPackagesCount);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalBundleMeta, %d bundles"), (double)ExportBundleMetaByteCount / 1024.0 / 1024.0, ExportBundleMetaEntries.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalImportNames, %d total imports, %d script imports, %d UPackage imports"),
		(double)GlobalImportNamesByteCount / 1024.0 / 1024.0, GlobalImportsByFullName.Num(), NumScriptImports, UniqueImportPackages);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB InitialLoadData, %d script arcs, %d script outers, %d packages"), (double)InitialLoadSize / 1024.0 / 1024.0, ScriptArcsCount, NumScriptImports, Packages.Num());
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
	FString PackageOrderFilePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("PackageOrder="), PackageOrderFilePath))
	{
		if (!ParsePakOrderFile(*PackageOrderFilePath, Arguments.PackageOrderMap))
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

	if (FParse::Value(CmdLine, TEXT("-AlignForMemoryMapping="), Arguments.MemoryMappingAlignment))
	{
		UE_LOG(LogIoStore, Warning, TEXT("Using memory mapping alignment '%ld'"), Arguments.MemoryMappingAlignment);
	}
	else
	{
		UE_LOG(LogIoStore, Warning, TEXT("Using default memory mapping alignment '%ld'"), DefaultMemoryMappingAlignment);
		Arguments.MemoryMappingAlignment = DefaultMemoryMappingAlignment;
	}

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
			UE_LOG(LogIoStore, Warning, TEXT("Using compression format '%s'"), *GeneralIoWriterSettings.CompressionMethod.ToString());
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

		UE_LOG(LogIoStore, Warning, TEXT("Using compression blocksize '%ld'"), GeneralIoWriterSettings.CompressionBlockSize);
	}

	FParse::Value(CmdLine, TEXT("-CreateReleaseVersionDirectory="), Arguments.OutputReleaseVersionDir);
	FParse::Value(CmdLine, TEXT("-BasedOnReleaseVersionDirectory="), Arguments.BasedOnReleaseVersionDir);

	FString CommandListFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("Commands="), CommandListFile))
	{
		UE_LOG(LogIoStore, Display, TEXT("Using command list file: '%s'"), *CommandListFile);

		if (!FParse::Value(FCommandLine::Get(), TEXT("OutputDirectory="), Arguments.OutputDir))
		{
			UE_LOG(LogIoStore, Error, TEXT("OutputDirectory must be specified"));
			return 1;
		}

		if (!FParse::Value(FCommandLine::Get(), TEXT("CookedDirectory="), Arguments.CookedDir))
		{
			UE_LOG(LogIoStore, Error, TEXT("CookedDirectory must be specified"));
			return 1;
		}

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
			ContainerSpec.OutputPath = FPaths::Combine(Arguments.OutputDir, ContainerSpec.OutputPath);

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

		UE_LOG(LogIoStore, Display, TEXT("Searching for cooked assets in folder '%s'"), *Arguments.CookedDir);
		FCookedFileStatMap CookedFileStatMap;
		FCookedFileVisitor CookedFileVistor(Arguments.CookedFileStatMap);
		IFileManager::Get().IterateDirectoryStatRecursively(*Arguments.CookedDir, CookedFileVistor);
		UE_LOG(LogIoStore, Display, TEXT("Found '%d' files"), Arguments.CookedFileStatMap.Num());

		if (Arguments.CookedFileStatMap.Num() == 0)
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed find cooked assets in folder '%s'"), *Arguments.CookedDir);
			return -1;
		}

		int32 ReturnValue = CreateTarget(Arguments, GeneralIoWriterSettings);
		if (ReturnValue != 0)
		{
			return ReturnValue;
		}
	}
	else
	{
		const TArray<ITargetPlatform*>& Platforms = GetTargetPlatformManagerRef().GetActiveTargetPlatforms();

		for (const ITargetPlatform* TargetPlatform : Platforms)
		{
			Arguments.CookedDir = FPaths::ProjectSavedDir() / TEXT("Cooked") / TargetPlatform->PlatformName();
			Arguments.OutputDir = Arguments.CookedDir / FApp::GetProjectName();

			TArray<FContainerSourceSpec> Containers;
			FContainerSourceSpec& ContainerSpec = Arguments.Containers.AddDefaulted_GetRef();
			FCookedFileVisitor CookedFileVistor(Arguments.CookedFileStatMap, &ContainerSpec);

			UE_LOG(LogIoStore, Display, TEXT("Searching for cooked files..."));
			IFileManager::Get().IterateDirectoryStatRecursively(*Arguments.CookedDir, CookedFileVistor);

			UE_LOG(LogIoStore, Display, TEXT("Found '%d' files"), ContainerSpec.SourceFiles.Num());

			UE_LOG(LogIoStore, Display, TEXT("Creating target: '%s' using output path: '%s'"), *TargetPlatform->PlatformName(), *Arguments.OutputDir);

			int32 ReturnValue = CreateTarget(Arguments, GeneralIoWriterSettings);
			if (ReturnValue != 0)
			{
				return ReturnValue;
			}
		}
	}

	return 0;
}
