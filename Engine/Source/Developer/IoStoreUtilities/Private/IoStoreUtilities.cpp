// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IoStoreUtilities.h"
#include "HAL/FileManager.h"
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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "UObject/NameBatchSerialization.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Algo/Find.h"
#include "Misc/FileHelper.h"

//PRAGMA_DISABLE_OPTIMIZATION

IMPLEMENT_MODULE(FDefaultModuleImpl, IoStoreUtilities);

DEFINE_LOG_CATEGORY_STATIC(LogIoStore, Log, All);

#define OUTPUT_CHUNKID_DIRECTORY 0
#define OUTPUT_NAMEMAP_CSV 0
#define OUTPUT_IMPORTMAP_CSV 0
#define SKIP_WRITE_CONTAINER 0
#define SKIP_BULKDATA 0

struct FContainerTarget 
{
	ITargetPlatform* TargetPlatform;
	FString CookedDirectory;
	FString CookedProjectDirectory;
	FString OutputDirectory;
	FString ChunkListFile;
};

class FNameMapBuilder
{
public:
	void MarkNameAsReferenced(const FName& Name)
	{
		const FNameEntryId Id = Name.GetComparisonIndex();
		int32& Index = NameIndices.FindOrAdd(Id);
		if (Index == 0)
		{
			Index = NameIndices.Num();
			NameMap.Add(Id);
		}
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
	TMap<FNameEntryId, TTuple<int32,int32,int32>> DebugNameCounts; // <Number0Count,OtherNumberCount,MaxNumber>
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

static FIoChunkId CreateChunkIdForBulkData(int32 GlobalPackageId, uint64 BulkdataOffset, EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateBulkdataChunkId(GlobalPackageId, BulkdataOffset, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, 0, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

struct FZenPackageSummary
{
	uint32 PackageFlags;
	int32 NameMapOffset;
	int32 ImportMapOffset;
	int32 ExportMapOffset;
	int32 ExportBundlesOffset;
	int32 GraphDataOffset;
	int32 GraphDataSize;
	int32 BulkDataStartOffset;
	int32 GlobalImportIndex;
	int32 Pad;
};

enum EPreloadDependencyType
{
	PreloadDependencyType_Create,
	PreloadDependencyType_Serialize,
};

enum EEventLoadNode2 : uint8
{
	Package_ExportsSerialized,
	Package_StartPostLoad,
	Package_Tick,
	Package_Delete,
	Package_NumPhases,

	ExportBundle_Process = 0,
	ExportBundle_NumPhases,
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

struct FExportBundleEntry
{
	enum EExportCommandType
	{
		ExportCommandType_Create,
		ExportCommandType_Serialize
	};
	uint32 LocalExportIndex;
	EExportCommandType CommandType;
};

struct FExportGraphNode;

struct FExportBundle
{
	TArray<FExportGraphNode*> Nodes;
	uint32 LoadOrder;
};

struct FPackage;

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

struct FPackage
{
	FName Name;
	FString FileName;
	FString RelativeFileName;
	int32 GlobalPackageId = 0;
	uint32 PackageFlags = 0;
	int32 NameCount = 0;
	int32 ImportCount = 0;
	int32 ImportOffset = 0;
	int32 ExportCount = 0;
	int32 FirstGlobalImport = -1;
	int32 GlobalImportCount = -1;
	int32 ExportIndexOffset = -1;
	int32 PreloadIndexOffset = -1;
	int32 FirstExportBundleMetaEntry = -1;
	int64 BulkDataStartOffset = -1;
	int64 UExpSize = 0;
	int64 UAssetSize = 0;
	int64 SummarySize = 0;
	int64 UGraphSize = 0;
	int64 NameMapSize = 0;
	int64 ImportMapSize = 0;
	int64 ExportMapSize = 0;
	int64 ExportBundlesSize = 0;

	bool bHasCircularImportDependencies = false;
	bool bHasCircularPostLoadDependencies = false;

	TArray<FString> ImportedFullNames;

	TArray<FPackage*> ImportedPackages;
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

struct FInstallChunk
{
	FString Name;
	int32 ChunkId;
	TArray<FPackage*> Packages;
};

TArray<FPackage*> FPackageGraph::TopologicalSort() const
{
	TMultiMap<FPackageGraphNode*, FPackageGraphNode*> EdgesCopy = Edges;
	TArray<FPackage*> Result;
	Result.Reserve(Nodes.Num());
	
	TSet<FPackageGraphNode*> UnmarkedNodes;
	UnmarkedNodes.Append(Nodes);

	struct
	{
		void Visit(FPackageGraphNode* Node)
		{
			if (Node->bPermanentMark)
			{
				return;
			}
			if (Node->bTemporaryMark)
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
			UnmarkedNodes.Remove(Node);
			Result.Insert(Node->Package, 0);
		}

		TSet<FPackageGraphNode*>& UnmarkedNodes;
		TMultiMap<FPackageGraphNode*, FPackageGraphNode*>& Edges;
		TArray<FPackage*>& Result;

	} Visitor{ UnmarkedNodes, EdgesCopy, Result };

	while (Result.Num() < Nodes.Num())
	{
		auto It = UnmarkedNodes.CreateIterator();
		FPackageGraphNode* UnmarkedNode = *It;
		It.RemoveCurrent();
		Visitor.Visit(UnmarkedNode);
	}

	return Result;
}

TArray<FExportGraphNode*> FExportGraph::ComputeLoadOrder(const TArray<FPackage*>& Packages) const
{
	FPackageGraph PackageGraph;
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

	TArray<FPackage*> SortedPackages = PackageGraph.TopologicalSort();
	
	int32 NodeCount = Nodes.Num();
	TArray<uint32> NodesIncomingEdgeCount;
	NodesIncomingEdgeCount.AddZeroed(NodeCount);
	TMultiMap<FExportGraphNode*, FExportGraphNode*> EdgesCopy = Edges;
	for (auto& KV : EdgesCopy)
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
				FExportGraphNode* RemovedNode = Package->NodesWithNoIncomingEdges.Pop();
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
	// TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(FromPackage.Name);
	// ExternalArcs.Add({ EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_Tick });
}

static void AddStartPostLoadArc(FPackage& FromPackage, FPackage& ToPackage)
{
	TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(&FromPackage);
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_StartPostLoad})));
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_StartPostLoad, EEventLoadNode2::Package_StartPostLoad})));
	ExternalArcs.Add({ EEventLoadNode2::Package_StartPostLoad, EEventLoadNode2::Package_StartPostLoad });
}

static void AddExportsDoneArc(FPackage& FromPackage, FPackage& ToPackage)
{
	TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(&FromPackage);
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_StartPostLoad})));
	check(!ExternalArcs.Contains(FArc({EEventLoadNode2::Package_StartPostLoad, EEventLoadNode2::Package_StartPostLoad})));
	ExternalArcs.Add({ EEventLoadNode2::Package_ExportsSerialized, EEventLoadNode2::Package_StartPostLoad });
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

static void AddPostLoadDependenciesRecursive(FPackage& Package, FPackage& ImportedPackage, TSet<FPackage*>& Visited)
{
	if (&ImportedPackage == &Package)
	{
		Package.bHasCircularPostLoadDependencies = true;
		return;
	}

	bool bIsVisited = false;
	Visited.Add(&ImportedPackage, &bIsVisited);
	if (bIsVisited)
	{
		return;
	}

	AddPostLoadArc(ImportedPackage, Package);
	
	for (FPackage* DependentPackage : ImportedPackage.ImportedPackages)
	{
		AddPostLoadDependenciesRecursive(Package, *DependentPackage, Visited);
	}
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

	for (FPackage* ImportedPackage : Package.ImportedPackages)
	{
		if (!DependentPackages.Contains(ImportedPackage))
		{
			AddStartPostLoadArc(*ImportedPackage, Package);
		}
	}

	DependentPackages.Remove(&Package);
	for (FPackage* DependentPackage : DependentPackages)
	{
		AddExportsDoneArc(*DependentPackage, Package);
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

static bool WriteBulkData(	const FString& Filename, EIoChunkType Type, const FPackage& Package, const FPackageStoreBulkDataManifest& BulkDataManifest,
							FIoStoreWriter* IoStoreWriter)
{

	const FPackageStoreBulkDataManifest::PackageDesc* PackageDesc = BulkDataManifest.Find(Package.FileName);
	if (PackageDesc != nullptr)
	{
		const FIoChunkId BulkDataChunkId = CreateChunkIdForBulkData(Package.GlobalPackageId, TNumericLimits<uint64>::Max()-1, Type, *Package.FileName);

#if !SKIP_WRITE_CONTAINER		
		FIoBuffer IoBuffer;

		TUniquePtr<FArchive> BulkAr(IFileManager::Get().CreateFileReader(*Filename));
		if (BulkAr != nullptr)
		{
			uint8* BulkBuffer = static_cast<uint8*>(FMemory::Malloc(BulkAr->TotalSize()));
			BulkAr->Serialize(BulkBuffer, BulkAr->TotalSize());
			IoBuffer = FIoBuffer(FIoBuffer::AssumeOwnership, BulkBuffer, BulkAr->TotalSize());

			BulkAr->Close();
		}
		
		const FIoStatus AppendResult = IoStoreWriter->Append(BulkDataChunkId, IoBuffer);
		if (!AppendResult.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to append bulkdata for '%s' due to: %s"), *Package.FileName, *AppendResult.ToString());
			return false;
		}
#endif

		// Create additional mapping chunks as needed
		for (const FPackageStoreBulkDataManifest::PackageDesc::BulkDataDesc& BulkDataDesc : PackageDesc->GetDataArray())
		{
			if (BulkDataDesc.Type == Type)
			{
				const FIoChunkId AccessChunkId = CreateChunkIdForBulkData(Package.GlobalPackageId, BulkDataDesc.ChunkId, Type, *Package.FileName);
#if !SKIP_WRITE_CONTAINER	
				const FIoStatus PartialResult = IoStoreWriter->MapPartialRange(BulkDataChunkId, BulkDataDesc.Offset, BulkDataDesc.Size, AccessChunkId);
				if (!PartialResult.IsOk())
				{
					UE_LOG(LogIoStore, Warning, TEXT("Failed to map partial range for '%s' due to: %s"), *Package.FileName, *PartialResult.ToString());
				}
#endif
			}
		}
	}
	else if(IFileManager::Get().FileExists(*Filename))
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
	FString FullName;
	FPackage* Package = nullptr;

	bool operator<(const FImportData& Other) const
	{
		if (bIsScript != Other.bIsScript)
		{
			return bIsScript;
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

struct FExportBundleMeta
{
	uint32 LoadOrder = -1;
	uint32 PayloadSize = -1;
};

static void FindImport(TArray<FImportData>& GlobalImports, TMap<FString, int32>& GlobalImportsByFullName, TArray<FString>& TempFullNames, FObjectImport* ImportMap, int32 LocalImportIndex)
{
	FObjectImport* Import = &ImportMap[LocalImportIndex];
	FString& FullName = TempFullNames[LocalImportIndex];
	if (FullName.Len() == 0)
	{
		if (Import->OuterIndex.IsNull())
		{
			Import->ObjectName.AppendString(FullName);
			int32* FindGlobalImport = GlobalImportsByFullName.Find(FullName);
			if (!FindGlobalImport)
			{
				// first time, assign global index for this root package
				int32 GlobalImportIndex = GlobalImports.Num();
				GlobalImportsByFullName.Add(FullName, GlobalImportIndex);
				FImportData& GlobalImport = GlobalImports.AddDefaulted_GetRef();
				GlobalImport.GlobalIndex = GlobalImportIndex;
				GlobalImport.OutermostIndex = GlobalImportIndex;
				GlobalImport.OuterIndex = -1;
				GlobalImport.ObjectName = Import->ObjectName;
				GlobalImport.bIsPackage = true;
				GlobalImport.bIsScript = FullName.StartsWith(TEXT("/Script/"));
				GlobalImport.FullName = FullName;
				GlobalImport.RefCount = 1;
			}
			else
			{
				++GlobalImports[*FindGlobalImport].RefCount;
			}
		}
		else
		{
			int32 LocalOuterIndex = Import->OuterIndex.ToImport();
			FindImport(GlobalImports, GlobalImportsByFullName, TempFullNames, ImportMap, LocalOuterIndex);
			FString& OuterName = TempFullNames[LocalOuterIndex];
			ensure(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Import->ObjectName.AppendString(FullName);

			int32* FindGlobalImport = GlobalImportsByFullName.Find(FullName);
			if (!FindGlobalImport)
			{
				// first time, assign global index for this intermediate import
				int32 GlobalImportIndex = GlobalImports.Num();
				GlobalImportsByFullName.Add(FullName, GlobalImportIndex);
				FImportData& GlobalImport = GlobalImports.AddDefaulted_GetRef();
				int32* FindOuterGlobalImport = GlobalImportsByFullName.Find(OuterName);
				check(FindOuterGlobalImport);
				const FImportData& OuterGlobalImport = GlobalImports[*FindOuterGlobalImport];
				GlobalImport.GlobalIndex = GlobalImportIndex;
				GlobalImport.OutermostIndex = OuterGlobalImport.OutermostIndex;
				GlobalImport.OuterIndex = OuterGlobalImport.GlobalIndex;
				GlobalImport.ObjectName = Import->ObjectName;
				GlobalImport.bIsScript = OuterGlobalImport.bIsScript;
				GlobalImport.FullName = FullName;
				GlobalImport.RefCount = 1;
			}
			else
			{
				++GlobalImports[*FindGlobalImport].RefCount;
			}
		}
	}
};

static void FindExport(TArray<FExportData>& GlobalExports, TMap<FString, int32>& GlobalExportsByFullName, TArray<FString>& TempFullNames, const FObjectExport* ExportMap, int32 LocalExportIndex, const FName& PackageName)
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

			FindExport(GlobalExports, GlobalExportsByFullName, TempFullNames, ExportMap, Export->OuterIndex.ToExport(), PackageName);
			FString& OuterName = TempFullNames[Export->OuterIndex.ToExport()];
			check(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
		}
		check(!GlobalExportsByFullName.Contains(FullName));
		int32 GlobalExportIndex = GlobalExports.Num();
		GlobalExportsByFullName.Add(FullName, GlobalExportIndex);
		FExportData& ExportData = GlobalExports.AddDefaulted_GetRef();
		ExportData.GlobalIndex = GlobalExportIndex;
		ExportData.SourcePackageName = PackageName;
		ExportData.ObjectName = Export->ObjectName;
		ExportData.SourceIndex = LocalExportIndex;
		ExportData.FullName = FullName;
	}
};

FPackage* AddPackage(const TCHAR* FileName, const TCHAR* CookedDir, TArray<FPackage*>& Packages, TMap<FName, FPackage*>& PackageMap)
{
	FString RelativeFileName = FileName;
	RelativeFileName.RemoveFromStart(CookedDir);
	RelativeFileName.RemoveFromStart(TEXT("/"));
	RelativeFileName = TEXT("../../../") / RelativeFileName;

	FString PackageName;
	FString ErrorMessage;
	if (!FPackageName::TryConvertFilenameToLongPackageName(RelativeFileName, PackageName, &ErrorMessage))
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed to convert file name from file name '%s'"), *ErrorMessage);
		return nullptr;
	}

	FName PackageFName = *PackageName;

	FPackage* Package = PackageMap.FindRef(PackageFName);
	if (Package)
	{
		UE_LOG(LogIoStore, Warning, TEXT("Package in multiple pakchunks: '%s'"), *PackageFName.ToString());
	}
	else
	{
		Package = new FPackage();
		Package->Name = PackageFName;
		Package->FileName = FileName;
		Package->RelativeFileName = MoveTemp(RelativeFileName);
		Package->GlobalPackageId = Packages.Num();
		Packages.Add(Package);
		PackageMap.Add(PackageFName, Package);
	}

	return Package;
}

void SerializePackageData(
	FIoStoreWriter* IoStoreWriter,
	const TArray<FPackage*>& Packages,
	const FNameMapBuilder& NameMapBuilder,
	const TArray<FObjectExport>& ObjectExports,
	const TArray<FExportData>& GlobalExports,
	const TMap<FString, int32>& GlobalImportsByFullName,
	FExportBundleMeta* ExportBundleMetaEntries,
	const FPackageStoreBulkDataManifest& BulkDataManifest,
	bool bWithBulkDataManifest)
{
	for (FPackage* Package : Packages)
	{
		UE_CLOG(Package->GlobalPackageId % 1000 == 0, LogIoStore, Display, TEXT("Serializing %d/%d: '%s'"), Package->GlobalPackageId, Packages.Num(), *Package->Name.ToString());

		// Temporary Archive for serializing ImportMap
		FBufferWriter ImportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		for (int32 GlobalImportIndex : Package->Imports)
		{
			ImportMapArchive << GlobalImportIndex;
		}
		Package->ImportMapSize = ImportMapArchive.Tell();

		// Temporary Archive for serializing EDL graph data
		FBufferWriter ZenGraphArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);

		int32 InternalArcCount = Package->InternalArcs.Num();
		ZenGraphArchive << InternalArcCount;
		for (FArc& InternalArc : Package->InternalArcs)
		{
			ZenGraphArchive << InternalArc.FromNodeIndex;
			ZenGraphArchive << InternalArc.ToNodeIndex;
		}

		int32 ReferencedPackagesCount = Package->ExternalArcs.Num();
		ZenGraphArchive << ReferencedPackagesCount;
		for (auto& KV : Package->ExternalArcs)
		{
			FPackage* ImportedPackage = KV.Key;
			TArray<FArc>& Arcs = KV.Value;
			int32 ExternalArcCount = Arcs.Num();

			ZenGraphArchive << ImportedPackage->GlobalPackageId;
			ZenGraphArchive << ExternalArcCount;
			for (FArc& ExternalArc : Arcs)
			{
				ZenGraphArchive << ExternalArc.FromNodeIndex;
				ZenGraphArchive << ExternalArc.ToNodeIndex;
			}
		}
		Package->UGraphSize = ZenGraphArchive.Tell();

		// Temporary Archive for serializing export map data
		FBufferWriter ZenExportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			const FExportData& ExportData = GlobalExports[Package->Exports[I]];

			int64 SerialSize = ObjectExport.SerialSize;
			ZenExportMapArchive << SerialSize;
			NameMapBuilder.SerializeName(ZenExportMapArchive, ObjectExport.ObjectName);
			FPackageIndex OuterIndex = ExportData.OuterIndex;
			ZenExportMapArchive << OuterIndex;
			FPackageIndex ClassIndex = ExportData.ClassIndex;
			ZenExportMapArchive << ClassIndex;
			FPackageIndex SuperIndex = ExportData.SuperIndex;
			ZenExportMapArchive << SuperIndex;
			FPackageIndex TemplateIndex = ExportData.TemplateIndex;
			ZenExportMapArchive << TemplateIndex;
			int32 GlobalImportIndex = ExportData.GlobalImportIndex;
			ZenExportMapArchive << GlobalImportIndex;
			uint32 ObjectFlags = ObjectExport.ObjectFlags;
			ZenExportMapArchive << ObjectFlags;
		}
		Package->ExportMapSize = ZenExportMapArchive.Tell();

		// Temporary archive for serializing export bundle data
		FBufferWriter ZenExportBundlesArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		int32 ExportBundleEntryIndex = 0;
		for (FExportBundle& ExportBundle : Package->ExportBundles)
		{
			ZenExportBundlesArchive << ExportBundleEntryIndex;
			int32 EntryCount = ExportBundle.Nodes.Num();
			ZenExportBundlesArchive << EntryCount;
			ExportBundleEntryIndex += ExportBundle.Nodes.Num();
		}
		for (FExportBundle& ExportBundle : Package->ExportBundles)
		{
			for (FExportGraphNode* ExportNode : ExportBundle.Nodes)
			{
				uint32 CommandType = uint32(ExportNode->BundleEntry.CommandType);
				ZenExportBundlesArchive << CommandType;
				ZenExportBundlesArchive << ExportNode->BundleEntry.LocalExportIndex;
			}
		}
		Package->ExportBundlesSize = ZenExportBundlesArchive.Tell();

		Package->NameMapSize = Package->NameIndices.Num() * Package->NameIndices.GetTypeSize();

		{
			const uint64 ZenSummarySize =
				sizeof(FZenPackageSummary)
				+ Package->NameMapSize
				+ Package->ImportMapSize
				+ Package->ExportMapSize
				+ Package->ExportBundlesSize
				+ Package->UGraphSize;

			uint8* ZenSummaryBuffer = static_cast<uint8*>(FMemory::Malloc(ZenSummarySize));
			FZenPackageSummary* ZenSummary = reinterpret_cast<FZenPackageSummary*>(ZenSummaryBuffer);

			ZenSummary->PackageFlags = Package->PackageFlags;
			ZenSummary->GraphDataSize = Package->UGraphSize;
			ZenSummary->BulkDataStartOffset = Package->BulkDataStartOffset;
			const int32* FindGlobalImportIndexForPackage = GlobalImportsByFullName.Find(Package->Name.ToString());
			ZenSummary->GlobalImportIndex = FindGlobalImportIndexForPackage ? *FindGlobalImportIndexForPackage : -1;

			FBufferWriter ZenAr(ZenSummaryBuffer, ZenSummarySize);
			ZenAr.Seek(sizeof(FZenPackageSummary));

			// NameMap data
			{
				ZenSummary->NameMapOffset = ZenAr.Tell();
				ZenAr.Serialize(Package->NameIndices.GetData(), Package->NameMapSize);
			}

			// ImportMap data
			{
				check(ImportMapArchive.Tell() == Package->ImportMapSize);
				ZenSummary->ImportMapOffset = ZenAr.Tell();
				ZenAr.Serialize(ImportMapArchive.GetWriterData(), ImportMapArchive.Tell());
			}

			// ExportMap data
			{
				check(ZenExportMapArchive.Tell() == Package->ExportMapSize);
				ZenSummary->ExportMapOffset = ZenAr.Tell();
				ZenAr.Serialize(ZenExportMapArchive.GetWriterData(), ZenExportMapArchive.Tell());
			}

			// ExportBundle data
			{
				check(ZenExportBundlesArchive.Tell() == Package->ExportBundlesSize);
				ZenSummary->ExportBundlesOffset = ZenAr.Tell();
				ZenAr.Serialize(ZenExportBundlesArchive.GetWriterData(), ZenExportBundlesArchive.Tell());
			}

			// Graph data
			{
				check(ZenGraphArchive.Tell() == Package->UGraphSize);
				ZenSummary->GraphDataOffset = ZenAr.Tell();
				ZenAr.Serialize(ZenGraphArchive.GetWriterData(), ZenGraphArchive.Tell());
			}

			// Export bundle chunks
			{
				check(Package->ExportBundles.Num());

				FString UExpFileName = FPaths::ChangeExtension(Package->FileName, TEXT(".uexp"));
				TUniquePtr<FArchive> ExpAr(IFileManager::Get().CreateFileReader(*UExpFileName));
				Package->UExpSize = ExpAr->TotalSize();
#if !SKIP_WRITE_CONTAINER
				uint8* ExportsBuffer = static_cast<uint8*>(FMemory::Malloc(ExpAr->TotalSize()));
				ExpAr->Serialize(ExportsBuffer, ExpAr->TotalSize());
#endif
				ExpAr->Close();

				uint64 BundleBufferSize = ZenSummarySize;
				for (int32 ExportBundleIndex = 0; ExportBundleIndex < Package->ExportBundles.Num(); ++ExportBundleIndex)
				{
					FExportBundle& ExportBundle = Package->ExportBundles[ExportBundleIndex];
					FExportBundleMeta* ExportBundleMetaEntry = ExportBundleMetaEntries + Package->FirstExportBundleMetaEntry + ExportBundleIndex;
					ExportBundleMetaEntry->LoadOrder = ExportBundle.LoadOrder;
					for (FExportGraphNode* Node : ExportBundle.Nodes)
					{
						if (Node->BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
						{
							const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + Node->BundleEntry.LocalExportIndex];
							BundleBufferSize += ObjectExport.SerialSize;
						}
					}
					if (ExportBundleIndex == 0)
					{
						ExportBundleMetaEntry->PayloadSize = BundleBufferSize;
					}
				}

#if !SKIP_WRITE_CONTAINER
				uint8* BundleBuffer = static_cast<uint8*>(FMemory::Malloc(BundleBufferSize));
				FMemory::Memcpy(BundleBuffer, ZenSummaryBuffer, ZenSummarySize);
#endif
				FMemory::Free(ZenSummaryBuffer);
				uint64 BundleBufferOffset = ZenSummarySize;
				for (FExportBundle& ExportBundle : Package->ExportBundles)
				{
					for (FExportGraphNode* Node : ExportBundle.Nodes)
					{
						if (Node->BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
						{
							const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + Node->BundleEntry.LocalExportIndex];
							const int64 Offset = ObjectExport.SerialOffset - Package->UAssetSize;
#if !SKIP_WRITE_CONTAINER
							FMemory::Memcpy(BundleBuffer + BundleBufferOffset, ExportsBuffer + Offset, ObjectExport.SerialSize);
#endif
							BundleBufferOffset += ObjectExport.SerialSize;
						}
					}
				}

#if !SKIP_WRITE_CONTAINER
				FIoBuffer IoBuffer(FIoBuffer::Wrap, BundleBuffer, BundleBufferSize);
				IoStoreWriter->Append(CreateChunkId(Package->GlobalPackageId, 0, EIoChunkType::ExportBundleData, *Package->FileName), IoBuffer);
				FMemory::Free(BundleBuffer);
				FMemory::Free(ExportsBuffer);
#endif
			}

#if !SKIP_BULKDATA
			// Bulk chunks
			if (bWithBulkDataManifest)
			{
				FString BulkFileName = FPaths::ChangeExtension(Package->FileName, TEXT(".ubulk"));
				FPaths::NormalizeFilename(BulkFileName);

				WriteBulkData(BulkFileName, EIoChunkType::BulkData, *Package, BulkDataManifest, IoStoreWriter);

				FString OptionalBulkFileName = FPaths::ChangeExtension(Package->FileName, TEXT(".uptnl"));
				FPaths::NormalizeFilename(OptionalBulkFileName);

				WriteBulkData(OptionalBulkFileName, EIoChunkType::OptionalBulkData, *Package, BulkDataManifest, IoStoreWriter);
			}
#endif
		}
	}
}

int32 CreateTarget(const FContainerTarget& Target)
{
	TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);

	const FString CookedDir = Target.CookedDirectory;
	const FString OutputDir = Target.OutputDirectory;
	
	FPackageStoreBulkDataManifest BulkDataManifest(Target.CookedProjectDirectory);
	const bool bWithBulkDataManifest = BulkDataManifest.Load();
	if (bWithBulkDataManifest)
	{
		UE_LOG(LogIoStore, Display, TEXT("Loaded Bulk Data manifest '%s'"), *BulkDataManifest.GetFilename());
	}

#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.CreateOutputFile(CookedDir);
#endif

	FNameMapBuilder NameMapBuilder;

	uint64 NameSize = 0;
	TArray<FObjectImport> ObjectImports;
	TArray<FObjectExport> ObjectExports;
	TArray<FImportData> GlobalImports;
	TArray<FExportData> GlobalExports;
	TMap<FString, int32> GlobalImportsByFullName;
	TMap<FString, int32> GlobalExportsByFullName;
	TArray<FString> TempFullNames;
	TArray<FPackageIndex> PreloadDependencies;
	uint64 UPackageImports = 0;
	TArray<int32> ImportPreloadCounts;
	TArray<int32> ExportPreloadCounts;
	uint64 ImportPreloadCount = 0;
	uint64 ExportPreloadCount = 0;
	FExportGraph ExportGraph;
	TArray<FExportBundleMeta> ExportBundleMetaEntries;

	TArray<FInstallChunk> InstallChunks;
	TArray<FPackage*> Packages;
	TMap<FName, FPackage*> PackageMap;
	if (Target.ChunkListFile.IsEmpty())
	{
		UE_LOG(LogIoStore, Display, TEXT("Searching for .uasset and .umap files..."));
		TArray<FString> FileNames;
		IFileManager::Get().FindFilesRecursive(FileNames, *CookedDir, TEXT("*.uasset"), true, false, false);
		IFileManager::Get().FindFilesRecursive(FileNames, *CookedDir, TEXT("*.umap"), true, false, false);
		UE_LOG(LogIoStore, Display, TEXT("Found '%d' files"), FileNames.Num());
		FInstallChunk& InstallChunk = InstallChunks.AddDefaulted_GetRef();
		InstallChunk.Name = TEXT("container0");
		for (FString& FileName : FileNames)
		{
			if (FPackage* Package = AddPackage(*FileName, *CookedDir, Packages, PackageMap))
			{
				InstallChunk.Packages.Add(Package);
			}
		}
	}
	else
	{
		TArray<FString> ChunkManifestFileNames;
		FString ChunkFilesDirectory = FPaths::GetPath(Target.ChunkListFile);
		FFileHelper::LoadFileToStringArray(ChunkManifestFileNames, *Target.ChunkListFile);
		for (const FString& ChunkManifestFileName : ChunkManifestFileNames)
		{
			const TCHAR* PakChunkPrefix = TEXT("pakchunk");
			const int32 PakChunkPrefixLength = 8;
			if (!ChunkManifestFileName.StartsWith(PakChunkPrefix))
			{
				UE_LOG(LogIoStore, Warning, TEXT("Unexpected file name '%s' in '%s'"), *ChunkManifestFileName, *Target.ChunkListFile);
				continue;
			}
			int32 Index = PakChunkPrefixLength;
			int32 DigitCount = 0;
			while (Index < ChunkManifestFileName.Len() && FChar::IsDigit(ChunkManifestFileName[Index]))
			{
				++DigitCount;
				++Index;
			}
			if (DigitCount <= 0)
			{
				UE_LOG(LogIoStore, Warning, TEXT("Unexpected file name '%s' in '%s'"), *ChunkManifestFileName, *Target.ChunkListFile);
				continue;
			}
			FString ChunkIdString = ChunkManifestFileName.Mid(PakChunkPrefixLength, DigitCount);
			check(ChunkIdString.IsNumeric());
			int32 ChunkId;
			TTypeFromString<int32>::FromString(ChunkId, *ChunkIdString);
			FInstallChunk& InstallChunk = InstallChunks.AddDefaulted_GetRef();
			InstallChunk.Name = TEXT("container") + FPaths::GetBaseFilename(ChunkManifestFileName.RightChop(8));
			InstallChunk.ChunkId = ChunkId;
			FString ChunkManifestFullPath = ChunkFilesDirectory / ChunkManifestFileName;
			TArray<FString> ChunkManifest;
			FFileHelper::LoadFileToStringArray(ChunkManifest, *ChunkManifestFullPath);
			for (const FString& FileNameWithoutExtension : ChunkManifest)
			{
				FString RelativePathWithoutExtension = IFileManager::Get().ConvertToRelativePath(*FileNameWithoutExtension);
				FString FileName = RelativePathWithoutExtension + ".uasset";
				if (!IFileManager::Get().FileExists(*FileName))
				{
					FileName = RelativePathWithoutExtension + ".umap";
				}
				if (!IFileManager::Get().FileExists(*FileName))
				{
					UE_LOG(LogIoStore, Warning, TEXT("Couldn't find package file (uexp/umap) for package '%s'"), *FileName);
				}
				else
				{
					if (FPackage* Package = AddPackage(*FileName, *CookedDir, Packages, PackageMap))
					{
						InstallChunk.Packages.Add(Package);
					}
				}
			}
		}
	}

	ImportPreloadCounts.AddDefaulted(Packages.Num());
	ExportPreloadCounts.AddDefaulted(Packages.Num());

	for (FPackage* Package : Packages)
	{
		UE_CLOG(Package->GlobalPackageId % 1000 == 0, LogIoStore, Display, TEXT("Parsing %d/%d: '%s'"), Package->GlobalPackageId, Packages.Num(), *Package->FileName);

		FPackageFileSummary Summary;
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*Package->FileName));
		check(Ar);
		*Ar << Summary;

		Package->UAssetSize = Ar->TotalSize();
		Package->SummarySize = Ar->Tell();
		Package->NameCount = Summary.NameCount;
		Package->ImportCount = Summary.ImportCount;
		Package->ExportCount = Summary.ExportCount;
		Package->PackageFlags = Summary.PackageFlags;
		Package->BulkDataStartOffset = Summary.BulkDataStartOffset;

		if (Summary.NameCount > 0)
		{
			Ar->Seek(Summary.NameOffset);
			uint64 LastOffset = Summary.NameOffset;

			Package->NameMap.Reserve(Summary.NameCount);
			Package->NameIndices.Reserve(Summary.NameCount);
			FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);

			for (int32 I = 0; I < Summary.NameCount; ++I)
			{
				*Ar << NameEntry;
				FName Name(NameEntry);
				NameMapBuilder.MarkNameAsReferenced(Name);
				Package->NameMap.Emplace(Name.GetDisplayIndex());
				Package->NameIndices.Add(NameMapBuilder.MapName(Name));
			}

			NameSize += Ar->Tell() - Summary.NameOffset;
		}

		auto DeserializeName = [&](FArchive& A, FName& N)
		{
			int32 DisplayIndex, NameNumber;
			A << DisplayIndex << NameNumber;
			FNameEntryId DisplayEntry = Package->NameMap[DisplayIndex];
			N = FName::CreateFromDisplayId(DisplayEntry, NameNumber);
		};

		if (Summary.ImportCount > 0)
		{
			Ar->Seek(Summary.ImportOffset);

			int32 NumPackages = 0;
			int32 BaseIndex = ObjectImports.Num();
			ObjectImports.AddUninitialized(Summary.ImportCount);
			TArray<FString> ImportNames;
			ImportNames.Reserve(Summary.ImportCount);
			for (int32 I = 0; I < Summary.ImportCount; ++I)
			{
				FObjectImport& ObjectImport = ObjectImports[BaseIndex + I];
				DeserializeName(*Ar, ObjectImport.ClassPackage);
				DeserializeName(*Ar, ObjectImport.ClassName);
				*Ar << ObjectImport.OuterIndex;
				DeserializeName(*Ar, ObjectImport.ObjectName);

				if (ObjectImport.OuterIndex.IsNull())
				{
					++NumPackages;
				}

				ImportNames.Emplace(ObjectImport.ObjectName.ToString());
			}

			UPackageImports += NumPackages;

			Package->ImportedFullNames.SetNum(Summary.ImportCount);
			for (int32 I = 0; I < Summary.ImportCount; ++I)
			{
				FindImport(GlobalImports, GlobalImportsByFullName, Package->ImportedFullNames, ObjectImports.GetData() + BaseIndex, I);
			}
		}

		Package->PreloadIndexOffset = PreloadDependencies.Num();
		int32 PreloadDependenciesBaseIndex = -1;
		if (Summary.PreloadDependencyCount > 0)
		{
			Ar->Seek(Summary.PreloadDependencyOffset);
			PreloadDependenciesBaseIndex = PreloadDependencies.Num();
			PreloadDependencies.AddUninitialized(Summary.PreloadDependencyCount);
			for (int32 I = 0; I < Summary.PreloadDependencyCount; ++I)
			{
				FPackageIndex& Index = PreloadDependencies[PreloadDependenciesBaseIndex + I];
				*Ar << Index;
				if (Index.IsImport())
				{
					++ImportPreloadCounts[Package->GlobalPackageId];
					++ImportPreloadCount;
				}
				else
				{
					++ExportPreloadCounts[Package->GlobalPackageId];
					++ExportPreloadCount;
				}
			}
		}

		Package->ExportIndexOffset = ObjectExports.Num();
		if (Summary.ExportCount > 0)
		{
			Ar->Seek(Summary.ExportOffset);

			int32 BaseIndex = ObjectExports.Num();
			ObjectExports.AddUninitialized(Summary.ExportCount);
			for (int32 I = 0; I < Summary.ExportCount; ++I)
			{
				FObjectExport& ObjectExport = ObjectExports[BaseIndex + I];
				*Ar << ObjectExport.ClassIndex;
				*Ar << ObjectExport.SuperIndex;
				*Ar << ObjectExport.TemplateIndex;
				*Ar << ObjectExport.OuterIndex;
				DeserializeName(*Ar, ObjectExport.ObjectName);
				uint32 ObjectFlags;
				*Ar << ObjectFlags;
				ObjectExport.ObjectFlags = (EObjectFlags)ObjectFlags;
				*Ar << ObjectExport.SerialSize;
				*Ar << ObjectExport.SerialOffset;
				*Ar << ObjectExport.bForcedExport;
				*Ar << ObjectExport.bNotForClient;
				*Ar << ObjectExport.bNotForServer;
				*Ar << ObjectExport.PackageGuid;
				*Ar << ObjectExport.PackageFlags;
				*Ar << ObjectExport.bNotAlwaysLoadedForEditorGame;
				*Ar << ObjectExport.bIsAsset;
				*Ar << ObjectExport.FirstExportDependency;
				*Ar << ObjectExport.SerializationBeforeSerializationDependencies;
				*Ar << ObjectExport.CreateBeforeSerializationDependencies;
				*Ar << ObjectExport.SerializationBeforeCreateDependencies;
				*Ar << ObjectExport.CreateBeforeCreateDependencies;
			}

			TempFullNames.Reset();
			TempFullNames.SetNum(Summary.ExportCount, false);
			for (int32 I = 0; I < Summary.ExportCount; ++I)
			{
				FindExport(GlobalExports, GlobalExportsByFullName, TempFullNames, ObjectExports.GetData() + BaseIndex, I, Package->Name);

				FExportData& ExportData = GlobalExports[*GlobalExportsByFullName.Find(TempFullNames[I])];
				Package->Exports.Add(ExportData.GlobalIndex);
				ExportData.CreateNode = ExportGraph.AddNode(Package, { uint32(I), FExportBundleEntry::ExportCommandType_Create });
				ExportData.SerializeNode = ExportGraph.AddNode(Package, { uint32(I), FExportBundleEntry::ExportCommandType_Serialize });
				Package->CreateExportNodes.Add(ExportData.CreateNode);
				Package->SerializeExportNodes.Add(ExportData.SerializeNode);
				ExportGraph.AddInternalDependency(ExportData.CreateNode, ExportData.SerializeNode);
			}
		}
	
		Ar->Close();
	}

	int32 NumScriptImports = 0;
	{
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
					check(FindPackage);
					FindPackage->FirstGlobalImport = I;
					GlobalImport.Package = FindPackage;
					LastPackage = FindPackage;
				}
				else
				{
					int32* FindGlobalExport = GlobalExportsByFullName.Find(GlobalImport.FullName);
					check(FindGlobalExport);
					GlobalImport.GlobalExportIndex = *FindGlobalExport;
				}
			}

			GlobalImportsByFullName[GlobalImport.FullName] = I;
		}
		if (LastPackage)
		{
			LastPackage->GlobalImportCount = GlobalImports.Num() - LastPackage->FirstGlobalImport;
		}
	}

	for (FExportData& GlobalExport : GlobalExports)
	{
		int32* FindGlobalImport = GlobalImportsByFullName.Find(GlobalExport.FullName);
		GlobalExport.GlobalImportIndex = FindGlobalImport ? *FindGlobalImport : -1;
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

	TSet<FString> MissingExports;
	TSet<FPackage*> Visited;
	TSet<FCircularImportChain> CircularChains;

	// Lookup global indices and package pointers for all imports before adding preload and postload arcs
	UE_LOG(LogIoStore, Display, TEXT("Looking up import packages..."));
	for (FPackage* Package : Packages)
	{
		Package->ImportedPackages.Reserve(Package->ImportCount);
		for (int32 I = 0; I < Package->ImportCount; ++I)
		{
			int32 GlobalImportIndex = *GlobalImportsByFullName.Find(Package->ImportedFullNames[I]);

			FImportData& ImportData = GlobalImports[GlobalImportIndex];
			Package->Imports.Add(ImportData.GlobalIndex);
			if (ImportData.Package)
			{
				Package->ImportedPackages.Add(ImportData.Package);
			}
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Converting export map import indices..."));
	for (FPackage* Package : Packages)
	{
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			FExportData& ExportData = GlobalExports[Package->Exports[I]];

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

#if 0
	UE_LOG(LogIoStore, Display, TEXT("Simulating postload dependencies for stats..."));
	for (auto& PackageKV : PackageMap)
	{
		FPackage& Package = *PackageKV.Value;

		Visited.Reset();
		for (FPackage* DependentPackage : Package.ImportedPackages)
		{
			AddPostLoadDependenciesRecursive(Package, *DependentPackage, Visited);
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Adding reachable packages for stats..."));
	for (auto& PackageKV : PackageMap)
	{
		FPackage& Package = *PackageKV.Value;

		AddReachablePackagesRecursive(Package, Package, Package.AllReachablePackages, /*bFirst*/ true);
		Package.bHasCircularImportDependencies = Package.AllReachablePackages.Contains(&Package);
		// if (Package.bHasCircularImportDependencies)
		// {
		// 	UE_LOG(LogIoStore, Display, TEXT("Circular %s: %d reachable packages"), *Package.Name.ToString(),
		// 		Package.AllReachablePackages.Num());
		// }
	}
#endif

	UE_LOG(LogIoStore, Display, TEXT("Adding optimized postload dependencies..."));
	for (FPackage* Package : Packages)
	{
		Visited.Reset();
		Visited.Add(Package);
		AddPostLoadDependencies(*Package, Visited, CircularChains);
	}
		
	UE_LOG(LogIoStore, Display, TEXT("Adding preload dependencies..."));
	for (FPackage* Package : Packages)
	{
		// Convert PreloadDependencies to arcs
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			FExportData& ExportData = GlobalExports[Package->Exports[I]];
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
						check(Import.GlobalExportIndex != - 1);
						FExportData& Export = GlobalExports[Import.GlobalExportIndex];
						FPackage* SourcePackage = PackageMap.FindRef(Export.SourcePackageName);
						check(SourcePackage);
						FPackageIndex SourceDep = FPackageIndex::FromExport(Export.SourceIndex);
						AddExternalExportArc(ExportGraph, *SourcePackage, Export.SourceIndex, PhaseFrom, *Package, I, PhaseTo);
						Package->ImportedPreloadPackages.Add(SourcePackage);
					}
				}
			};

			if (PreloadDependenciesBaseIndex >= 0 && ObjectExport.FirstExportDependency >= 0)
			{
				int32 RunningIndex = PreloadDependenciesBaseIndex + ObjectExport.FirstExportDependency;
				for (int32 Index = ObjectExport.SerializationBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Serialize, PreloadDependencyType_Serialize);
				}

				for (int32 Index = ObjectExport.CreateBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Create, PreloadDependencyType_Serialize);
				}

				for (int32 Index = ObjectExport.SerializationBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Serialize, PreloadDependencyType_Create);
				}

				for (int32 Index = ObjectExport.CreateBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					// can't create this export until these things are created
					AddPreloadArc(Dep, PreloadDependencyType_Create, PreloadDependencyType_Create);
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
		for (const FImportData& ImportData : GlobalImports)
		{
			NameMapBuilder.SerializeName(*GlobalImportNamesArchive, ImportData.ObjectName);
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Serializing initial load..."));
	// Separate file for script arcs that are only required during initial loading
	if (InitialLoadArchive)
	{
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

	for (FPackage* Package : Packages)
	{
		Package->FirstExportBundleMetaEntry = ExportBundleMetaEntries.Num();
		ExportBundleMetaEntries.AddDefaulted(Package->ExportBundles.Num());

		NameMapBuilder.MarkNameAsReferenced(Package->Name);
		NameMapBuilder.SerializeName(*StoreTocArchive, Package->Name);
		*StoreTocArchive << Package->ExportCount;
		int32 ExportBundleCount = Package->ExportBundles.Num();
		*StoreTocArchive << ExportBundleCount;
		*StoreTocArchive << Package->FirstExportBundleMetaEntry;
		*StoreTocArchive << Package->FirstGlobalImport;
		*StoreTocArchive << Package->GlobalImportCount;
		int32 ImportedPackagesCount = Package->ImportedPackages.Num();
		*StoreTocArchive << ImportedPackagesCount;
		int32 ImportedPackagesOffset = ImportedPackagesArchive->Tell();
		*StoreTocArchive << ImportedPackagesOffset;

		for (FPackage* ImportedPackage : Package->ImportedPackages)
		{
			*ImportedPackagesArchive << ImportedPackage->GlobalPackageId;
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Serializing..."));

	TUniquePtr<FIoStoreWriter> GlobalIoStoreWriter;
	FIoStoreEnvironment GlobalIoStoreEnv;
	{
		GlobalIoStoreEnv.InitializeFileEnvironment(OutputDir);
		GlobalIoStoreWriter = MakeUnique<FIoStoreWriter>(GlobalIoStoreEnv);
#if !SKIP_WRITE_CONTAINER
		FIoStatus IoStatus = GlobalIoStoreWriter->Initialize();
		check(IoStatus.IsOk());
#endif
	}

	FIoStoreInstallManifest Manifest;
	for (const FInstallChunk& InstallChunk : InstallChunks)
	{
		TUniquePtr<FIoStoreWriter> IoStoreWriter;
		FIoStoreEnvironment InstallChunkIoStoreEnv(GlobalIoStoreEnv, InstallChunk.Name);
		IoStoreWriter = MakeUnique<FIoStoreWriter>(InstallChunkIoStoreEnv);
#if !SKIP_WRITE_CONTAINER
		FIoStatus IoStatus = IoStoreWriter->Initialize();
		check(IoStatus.IsOk());
#endif
		SerializePackageData(
			IoStoreWriter.Get(),
			InstallChunk.Packages,
			NameMapBuilder,
			ObjectExports,
			GlobalExports,
			GlobalImportsByFullName,
			ExportBundleMetaEntries.GetData(),
			BulkDataManifest,
			bWithBulkDataManifest);
		FIoStoreInstallManifest::FEntry& ManifestEntry = Manifest.EditEntries().AddDefaulted_GetRef();
		ManifestEntry.InstallChunkId = InstallChunk.ChunkId;
		ManifestEntry.PartitionName = InstallChunk.Name;
	}
	TUniquePtr<FLargeMemoryWriter> ManifestArchive = MakeUnique<FLargeMemoryWriter>(0, true);
	*ManifestArchive << Manifest;

	GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::InstallManifest), FIoBuffer(FIoBuffer::Wrap, ManifestArchive->GetData(), ManifestArchive->TotalSize()));

	int32 StoreTocByteCount = StoreTocArchive->TotalSize();
	int32 ImportedPackagesByteCount = ImportedPackagesArchive->TotalSize();
	int32 GlobalImportNamesByteCount = GlobalImportNamesArchive->TotalSize();
	int32 ExportBundleMetaByteCount = ExportBundleMetaEntries.Num() * sizeof(FExportBundleMeta);
	{
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

#if !SKIP_WRITE_CONTAINER
		const FIoStatus Status = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalMeta), FIoBuffer(FIoBuffer::Wrap, GlobalMetaArchive.GetData(), GlobalMetaArchive.TotalSize()));
		if (!Status.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to save global meta data to container file"));
		}
#endif
	}

	{
		UE_LOG(LogIoStore, Display, TEXT("Saving initial load meta data to container file"));
#if !SKIP_WRITE_CONTAINER
		const FIoStatus Status = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoBuffer(FIoBuffer::Wrap, InitialLoadArchive->GetData(), InitialLoadArchive->TotalSize()));
		if (!Status.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to save initial load meta data to container file"));
		}
#endif
	}

	uint64 GlobalNamesMB = 0;
	uint64 GlobalNameHashesMB = 0;
	{
		UE_LOG(LogIoStore, Display, TEXT("Saving global name map to container file"));

		TArray<uint8> Names;
		TArray<uint8> Hashes;
		SaveNameBatch(NameMapBuilder.GetNameMap(), /* out */ Names, /* out */ Hashes);

		GlobalNamesMB = Names.Num() >> 20;
		GlobalNameHashesMB = Hashes.Num() >> 20;

#if !SKIP_WRITE_CONTAINER
		FIoStatus NameStatus = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames), 
													 FIoBuffer(FIoBuffer::Wrap, Names.GetData(), Names.Num()));
		FIoStatus HashStatus = GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes),
													 FIoBuffer(FIoBuffer::Wrap, Hashes.GetData(), Hashes.Num()));
#endif
		
		if (!NameStatus.IsOk() || !HashStatus.IsOk())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to save global name map to container file"));
		}

#if OUTPUT_NAMEMAP_CSV
		NameMapBuilder.SaveCsv(OutputDir / TEXT("Container.namemap.csv"));
#endif
	}

	UE_LOG(LogIoStore, Display, TEXT("Calculating stats..."));
	uint64 UExpSize = 0;
	uint64 UAssetSize = 0;
	uint64 SummarySize = 0;
	uint64 UGraphSize = 0;
	uint64 ImportMapSize = 0;
	uint64 ExportMapSize = 0;
	uint64 NameMapSize = 0;
	uint64 NameMapCount = 0;
	uint64 ZenPackageSummarySize = Packages.Num() * sizeof(FZenPackageSummary);
	uint64 ImportedPackagesCount = 0;
	uint64 InitialLoadSize = InitialLoadArchive->Tell();
	uint64 ScriptArcsCount = 0;
	uint64 CircularPackagesCount = 0;
	uint64 TotalInternalArcCount = 0;
	uint64 TotalExternalArcCount = 0;
	uint64 NameCount = 0;

	uint64 PackagesWithCircularDependenciesCount = 0;
	uint64 PackagesWithoutImportDependenciesCount = 0;
	uint64 PackagesWithoutPreloadDependenciesCount = 0;
	uint64 BundleCount = 0;
	uint64 BundleEntryCount = 0;

	uint64 ZenPackageHeaderSize = 0;

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
		PackagesWithCircularDependenciesCount += Package.bHasCircularPostLoadDependencies;
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

	ZenPackageHeaderSize = ZenPackageSummarySize + NameMapSize + ImportMapSize + ExportMapSize + UGraphSize;

	UE_LOG(LogIoStore, Display, TEXT("-------------------- IoStore Summary: %s --------------------"), *Target.TargetPlatform->PlatformName());
	UE_LOG(LogIoStore, Display, TEXT("Packages: %8d total, %d circular dependencies, %d no preload dependencies, %d no import dependencies"),
		PackageMap.Num(), PackagesWithCircularDependenciesCount, PackagesWithoutPreloadDependenciesCount, PackagesWithoutImportDependenciesCount);
	UE_LOG(LogIoStore, Display, TEXT("Bundles:  %8d total, %d entries, %d export objects"), BundleCount, BundleEntryCount, GlobalExports.Num());

	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalNames, %d unique names"), (double)GlobalNamesMB, NameMapBuilder.GetNameMap().Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalNameHashes"), (double)GlobalNameHashesMB);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalPackageData"), (double)StoreTocByteCount / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalImportedPackages, %d imported packages"), (double)ImportedPackagesByteCount / 1024.0 / 1024.0, ImportedPackagesCount);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalBundleMeta, %d bundles"), (double)ExportBundleMetaByteCount / 1024.0 / 1024.0, ExportBundleMetaEntries.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB GlobalImportNames, %d total imports, %d script imports, %d UPackage imports"),
		(double)GlobalImportNamesByteCount / 1024.0 / 1024.0, GlobalImportsByFullName.Num(), NumScriptImports, UniqueImportPackages);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB InitialLoadData, %d script arcs, %d script outers, %d packages"), (double)InitialLoadSize / 1024.0 / 1024.0, ScriptArcsCount, NumScriptImports, Packages.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageHeader, %d packages"), (double)ZenPackageHeaderSize / 1024.0 / 1024.0, Packages.Num());
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageSummary"), (double)ZenPackageSummarySize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageNameMap, %d indices"), (double)NameMapSize / 1024.0 / 1024.0, NameMapCount);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageImportMap"), (double)ImportMapSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageExportMap"), (double)ExportMapSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("IoStore: %8.2f MB PackageArcs, %d internal arcs, %d external arcs, %d circular packages (%d chains)"),
		(double)UGraphSize / 1024.0 / 1024.0, TotalInternalArcCount, TotalExternalArcCount, CircularPackagesCount, CircularChains.Num());

	return 0;
}

int32 CreateIoStoreContainerFiles(const TCHAR* CmdLine)
{
	UE_LOG(LogIoStore, Display, TEXT("==================== IoStore Utils ===================="));

	FString TargetPlatformName;
	if (FParse::Value(FCommandLine::Get(), TEXT("TargetPlatform="), TargetPlatformName))
	{
		UE_LOG(LogIoStore, Display, TEXT("Using target platform: '%s'"), *TargetPlatformName);
	}
	else
	{
		UE_LOG(LogIoStore, Error, TEXT("No target platform specified [-targetplatform]"));
		return -1;
	}

	FString OutputDirectory;
	if (FParse::Value(FCommandLine::Get(), TEXT("OutputDirectory="), OutputDirectory))
	{
		UE_LOG(LogIoStore, Display, TEXT("Using output directory: '%s'"), *OutputDirectory);
	}
	else
	{
		UE_LOG(LogIoStore, Display, TEXT("No output directory specified, using project's cooked folder"));
	}

	FString ChunkListFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ChunkListFile="), ChunkListFile))
	{
		UE_LOG(LogIoStore, Display, TEXT("Using chunk list file: '%s'"), *ChunkListFile);
	}

	ITargetPlatformManagerModule& TargetPlatformManager = *GetTargetPlatformManager();
	ITargetPlatform* TargetPlatform = TargetPlatformManager.FindTargetPlatform(TargetPlatformName);

	if (!TargetPlatform)
	{
		UE_LOG(LogIoStore, Display, TEXT("Unknown target platform '%s'"), *TargetPlatformName);
		return -1;
	}

	FString TargetCookedDirectory = FPaths::ProjectSavedDir() / TEXT("Cooked") / TargetPlatform->PlatformName();
	FString TargetCookedProjectDirectory = TargetCookedDirectory / FApp::GetProjectName();

	FString TargetOutputDirectory = OutputDirectory.Len() > 0
		? OutputDirectory
		: TargetCookedProjectDirectory / TEXT("Content") / TEXT("Containers");

	FContainerTarget Target { TargetPlatform, TargetCookedDirectory, TargetCookedProjectDirectory, TargetOutputDirectory, ChunkListFile };

	UE_LOG(LogIoStore, Display, TEXT("Creating target: '%s' using output directory: '%s'"), *Target.TargetPlatform->PlatformName(), *Target.OutputDirectory);

	return CreateTarget(Target);
}
