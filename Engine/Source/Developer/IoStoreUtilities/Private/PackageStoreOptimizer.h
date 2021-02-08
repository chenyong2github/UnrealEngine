// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/AsyncLoading2.h"
#include "IO/IoDispatcher.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/UObjectMarks.h"

class FPackageStoreNameMapBuilder
{
public:
	void SetNameMapType(FMappedName::EType InNameMapType)
	{
		NameMapType = InNameMapType;
	}

	void AddName(const FName& Name)
	{
		const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
		const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
		NameMap.Add(DisplayIndex);
		int32 Index = NameMap.Num();
		NameIndices.Add(ComparisonIndex, Index);
	}

	void MarkNamesAsReferenced(const TArray<FName>& Names, TArray<int32>& OutNameIndices)
	{
		for (const FName& Name : Names)
		{
			const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
			const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
			int32& Index = NameIndices.FindOrAdd(ComparisonIndex);
			if (Index == 0)
			{
				NameMap.Add(DisplayIndex);
				Index = NameMap.Num();
			}

			OutNameIndices.Add(Index - 1);
		}
	}

	void MarkNameAsReferenced(const FName& Name)
	{
		const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
		const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
		int32& Index = NameIndices.FindOrAdd(ComparisonIndex);
		if (Index == 0)
		{
			NameMap.Add(DisplayIndex);
			Index = NameMap.Num();
		}
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

	void Empty()
	{
		NameIndices.Empty();
		NameMap.Empty();
	}

private:
	TMap<FNameEntryId, int32> NameIndices;
	TArray<FNameEntryId> NameMap;
	FMappedName::EType NameMapType = FMappedName::EType::Package;
};

class FPackageStoreContainerHeaderEntry
{
public:
	FPackageId GetId() const
	{
		return FPackageId::FromName(Name);
	}

	FPackageId GetSourceId() const
	{
		if (SourceName.IsNone())
		{
			return FPackageId();
		}
		return FPackageId::FromName(SourceName);
	}

private:
	FName Name;
	FName SourceName;
	FString Region;
	uint64 ExportBundlesSize = 0;
	int32 ExportCount = 0;
	int32 ExportBundleCount = 0;
	uint32 LoadOrder = 0;
	TArray<FPackageId> ImportedPackageIds;
	bool bIsRedirected = false;

	friend class FPackageStoreOptimizer;
};

class FPackageStorePackage
{
public:
	FPackageId GetId() const
	{
		return Id;
	}

	uint64 GetLoadOrder() const
	{
		return ExportBundles.Num() > 0 ? ExportBundles[0].LoadOrder : 0;
	}

	uint64 GetExportBundleCount() const
	{
		return ExportBundles.Num();
	}

	uint64 GetImportMapSize() const
	{
		return ImportMapSize;
	}

	uint64 GetExportMapSize() const
	{
		return ExportMapSize;
	}
	
	uint64 GetExportBundleArcsSize() const
	{
		return ExportBundleArcsSize;
	}

	uint64 GetExportBundleHeadersSize() const
	{
		return ExportBundleHeadersSize;
	}

	uint64 GetNameCount() const
	{
		return NameMapBuilder.GetNameMap().Num();
	}

	uint64 GetNameMapSize() const
	{
		return NameMapSize;
	}

	const TArray<FPackageId>& GetImportedPackageIds() const
	{
		return ImportedPackageIds;
	}

	const TSet<FPackageId>& GetImportedRedirectedPackageIds() const
	{
		return ImportedRedirectedPackageIds;
	}

	void RedirectFrom(FName SourcePackageName)
	{
		SourceName = SourcePackageName;
	}

private:
	struct FExportGraphNode;

	struct FExternalDependency
	{
		FPackageId PackageId;
		FPackageObjectIndex GlobalImportIndex;
		FName DebugImportFullName;
		FExportBundleEntry::EExportCommandType ExportBundleCommandType;
		FExportGraphNode* ExportGraphNode = nullptr;
		int32 ResolvedExportBundleIndex = -1;
		bool bIsConfirmedMissing = false;
	};

	struct FExportGraphNode
	{
		FPackageStorePackage* Package = nullptr;
		FExportBundleEntry BundleEntry;
		TArray<FExternalDependency> ExternalDependencies;
		int32 ExportBundleIndex = -1;
		int32 IncomingEdgeCount = 0;
	};

	struct FExport
	{
		FString FullName;
		FName ObjectName;
		FPackageObjectIndex GlobalImportIndex;
		FPackageObjectIndex OuterIndex;
		FPackageObjectIndex ClassIndex;
		FPackageObjectIndex SuperIndex;
		FPackageObjectIndex TemplateIndex;
		EObjectFlags ObjectFlags = RF_NoFlags;
		uint64 CookedSerialOffset = uint64(-1);
		uint64 CookedSerialSize = uint64(-1);
		bool bNotForClient = false;
		bool bNotForServer = false;
		bool bIsPublic = false;
		FExportGraphNode* CreateNode = nullptr;
		FExportGraphNode* SerializeNode = nullptr;
	};

	struct FExportBundle
	{
		TArray<FExportBundleEntry> Entries;
		uint32 LoadOrder = MAX_int32;
	};

	struct FImport
	{
		FString FullName;
		FPackageId PackageId;
		FPackageObjectIndex GlobalImportIndex;
		bool bIsScriptImport = false;
		bool bIsPackageImport = false;
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

	FPackageId Id;
	FName Name;
	FName SourceName;
	FString Region;

	FPackageFileSummary Summary;
	TArray<FObjectImport> ObjectImports;
	TArray<FObjectExport> ObjectExports;
	TArray<FPackageIndex> PreloadDependencies;

	FPackageStoreNameMapBuilder NameMapBuilder;
	TArray<FImport> Imports;
	TArray<FExport> Exports;
	TArray<FExportGraphNode> ExportGraphNodes;
	TMap<FPackageId, TArray<FArc>> ExternalArcs;
	TArray<FExportBundle> ExportBundles;

	TArray<FPackageId> ImportedPackageIds;
	TArray<FPackageStorePackage*> ImportedWaitingPackages;
	TArray<FPackageStorePackage*> ImportedByWaitingPackages;
	TSet<FPackageId> RedirectedToPackageIds;
	TSet<FPackageId> ImportedRedirectedPackageIds;

	FIoBuffer OptimizedHeaderBuffer;

	uint32 PackageFlags = 0;
	uint32 CookedHeaderSize = 0;
	uint64 ExportsSerialSize = 0;
	uint64 ImportMapSize = 0;
	uint64 ExportMapSize = 0;
	uint64 ExportBundleArcsSize = 0;
	uint64 ExportBundleHeadersSize = 0;
	uint64 NameMapSize = 0;
	
	uint32 UnresolvedImportedPackagesCount = 0;
	TArray<FExportGraphNode*> NodesWithNoIncomingEdges;
	bool bTemporaryMark = false;
	bool bPermanentMark = false;

	bool bIsRedirected = false;

	friend class FPackageStoreOptimizer;
};

class FPackageStoreOptimizer
{
public:
	uint64 GetTotalPackageCount() const
	{
		return TotalPackageCount;
	}

	uint64 GetTotalExportBundleCount() const
	{
		return TotalExportBundleCount;
	}

	uint64 GetTotalExportBundleEntryCount() const
	{
		return TotalExportBundleEntryCount;
	}

	uint64 GetTotalExportBundleArcCount() const
	{
		return TotalExportBundleArcCount;
	}

	uint64 GetTotalScriptObjectCount() const
	{
		return TotalScriptObjectCount;
	}

	void Initialize(const ITargetPlatform* TargetPlatform);

	void EnableIncrementalResolve()
	{
		bIncrementalResolveEnabled = true;
	}

	FPackageStorePackage* CreatePackageFromCookedHeader(const FName& Name, const FIoBuffer& CookedHeaderBuffer) const;
	void BeginResolvePackage(FPackageStorePackage* Package);
	void Flush(bool bAllowMissingImports, TFunction<void(FPackageStorePackage*)> ResolvedPackageCallback = TFunction<void(FPackageStorePackage*)>());
	FIoBuffer CreatePackageBuffer(const FPackageStorePackage* Package, const FIoBuffer& CookedExportsBuffer, TArray<FFileRegion>* InOutFileRegions) const;
	FPackageStoreContainerHeaderEntry CreateContainerHeaderEntry(const FPackageStorePackage* Package) const;
	FContainerHeader CreateContainerHeader(const FIoContainerId& ContainerId, const TArray<FPackageStoreContainerHeaderEntry>& Packages) const;
	uint64 WriteScriptObjects(FIoStoreWriter* IoStoreWriter) const; // TODO: Merge name map and return FIoBuffer instead

private:
	struct FScriptObjectData
	{
		FName ObjectName;
		FString FullName;
		FPackageObjectIndex GlobalIndex;
		FPackageObjectIndex OuterIndex;
		FPackageObjectIndex CDOClassIndex;
	};

	struct FResolvedPublicExport
	{
		FPackageObjectIndex GlobalImportIndex;
		int32 CreateExportBundleIndex = -1;
		int32 SerializeExportBundleIndex = -1;
	};

	using FExportGraphEdges = TMultiMap<FPackageStorePackage::FExportGraphNode*, FPackageStorePackage::FExportGraphNode*>;

	void LoadCookedHeader(FPackageStorePackage* Package, const FIoBuffer& CookedHeaderBuffer) const;
	void ResolveImport(FPackageStorePackage::FImport* Imports, const FObjectImport* ObjectImports, int32 LocalImportIndex) const;
	void ProcessImports(FPackageStorePackage* Package) const;
	void ResolveExport(FPackageStorePackage::FExport* Exports, const FObjectExport* ObjectExports, const int32 LocalExportIndex, const FName& PackageName) const;
	void ProcessExports(FPackageStorePackage* Package) const;
	void SortPackagesInLoadOrder(TArray<FPackageStorePackage*>& Packages) const;
	FExportGraphEdges ProcessPreloadDependencies(const TArray<FPackageStorePackage*>& Packages) const;
	TArray<FPackageStorePackage::FExportGraphNode*> SortExportGraphNodesInLoadOrder(const TArray<FPackageStorePackage*>& Packages, FExportGraphEdges& Edges) const;
	void CreateExportBundles(TArray<FPackageStorePackage*>& Packages);
	bool RedirectPackage(const FPackageStorePackage& SourcePackage, FPackageStorePackage& TargetPackage, TMap<FPackageObjectIndex, FPackageObjectIndex>& RedirectedToSourceImportIndexMap) const;
	void RedirectPackageUnverified(FPackageStorePackage& TargetPackage, TMap<FPackageObjectIndex, FPackageObjectIndex>& RedirectedToSourceImportIndexMap) const;
	void ProcessRedirects(const TArray<FPackageStorePackage*> Packages) const;
	void ResolvePackages(TArray<FPackageStorePackage*>& Packages, bool bAllowMissingImports);
	void FinalizePackageHeader(FPackageStorePackage* Package) const;
	void FinalizePackage(FPackageStorePackage* Package, bool bAllowMissingImports);
	bool IsWaitingForImportsRecursive(FPackageStorePackage* Package) const;
	void FindScriptObjectsRecursive(FPackageObjectIndex OuterIndex, UObject* Object, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform);
	void FindScriptObjects(const ITargetPlatform* TargetPlatform);

	TMap<FPackageObjectIndex, FScriptObjectData> ScriptObjectsMap;
	TMap<FPackageId, TArray<FResolvedPublicExport>> ResolvedPublicExportsMap;
	TMap<FPackageId, FPackageStorePackage*> WaitingPackagesMap;
	TMultiMap<FPackageId, FPackageStorePackage*> ImportedPackageToWaitingPackagesMap;
	TArray<FPackageStorePackage*> ResolvedPackages;
	uint64 TotalPackageCount = 0;
	uint64 TotalExportBundleCount = 0;
	uint64 TotalExportBundleEntryCount = 0;
	uint64 TotalExportBundleArcCount = 0;
	uint64 TotalScriptObjectCount = 0;
	uint32 NextLoadOrder = 0;
	bool bIncrementalResolveEnabled = false;
};