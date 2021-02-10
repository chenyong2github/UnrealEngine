// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageStoreOptimizer.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/BufferWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "UObject/NameBatchSerialization.h"
#include "Containers/Map.h"
#include "UObject/UObjectHash.h"
#include "Serialization/PackageStore.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/Object.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogPackageStoreOptimizer, Log, All);

// modified copy from SavePackage
EObjectMark GetExcludedObjectMarksForTargetPlatform(const ITargetPlatform* TargetPlatform)
{
	EObjectMark Marks = OBJECTMARK_NOMARKS;
	if (!TargetPlatform->HasEditorOnlyData())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	if (TargetPlatform->IsServerOnly())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForServer);
	}
	if (TargetPlatform->IsClientOnly())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient);
	}
	return Marks;
}

// modified copy from SavePackage
EObjectMark GetExcludedObjectMarksForObject(const UObject* Object, const ITargetPlatform* TargetPlatform)
{
	EObjectMark Marks = OBJECTMARK_NOMARKS;
	if (!Object->NeedsLoadForClient())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient);
	}
	if (!Object->NeedsLoadForServer())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForServer);
	}
	if (!Object->NeedsLoadForTargetPlatform(TargetPlatform))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}
	if (Object->IsEditorOnly())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	if ((Marks & OBJECTMARK_NotForClient) && (Marks & OBJECTMARK_NotForServer))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	return Marks;
}

// modified copy from PakFileUtilities
static FString RemapLocalizationPathIfNeeded(const FString& Path, FString* OutRegion)
{
	static constexpr TCHAR L10NString[] = TEXT("/L10N/");
	static constexpr int32 L10NPrefixLength = sizeof(L10NString) / sizeof(TCHAR) - 1;

	int32 BeginL10NOffset = Path.Find(L10NString, ESearchCase::IgnoreCase);
	if (BeginL10NOffset >= 0)
	{
		int32 EndL10NOffset = BeginL10NOffset + L10NPrefixLength;
		int32 NextSlashIndex = Path.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, EndL10NOffset);
		int32 RegionLength = NextSlashIndex - EndL10NOffset;
		if (RegionLength >= 2)
		{
			FString NonLocalizedPath = Path.Mid(0, BeginL10NOffset) + Path.Mid(NextSlashIndex);
			if (OutRegion)
			{
				*OutRegion = Path.Mid(EndL10NOffset, RegionLength);
				OutRegion->ToLowerInline();
			}
			return NonLocalizedPath;
		}
	}
	return Path;
}

void FPackageStoreOptimizer::Initialize(const ITargetPlatform* TargetPlatform)
{
	FindScriptObjects(TargetPlatform);
}

FPackageStorePackage* FPackageStoreOptimizer::CreatePackageFromCookedHeader(const FName& Name, const FIoBuffer& CookedHeaderBuffer) const
{
	FPackageStorePackage* Package = new FPackageStorePackage();
	Package->Name = Name;
	Package->Id = FPackageId::FromName(Name);

	Package->SourceName = *RemapLocalizationPathIfNeeded(Name.ToString(), &Package->Region);

	LoadCookedHeader(Package, CookedHeaderBuffer);
	ProcessImports(Package);
	ProcessExports(Package);

	return Package;
}

void FPackageStoreOptimizer::LoadCookedHeader(FPackageStorePackage* Package, const FIoBuffer& CookedHeaderBuffer) const
{
	TArrayView<const uint8> MemView(CookedHeaderBuffer.Data(), CookedHeaderBuffer.DataSize());
	FMemoryReaderView Ar(MemView);

	{
		TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);
		Ar << Package->Summary;
	}

	Package->PackageFlags = Package->Summary.GetPackageFlags();
	Package->CookedHeaderSize = Package->Summary.TotalHeaderSize;

	Ar.SetFilterEditorOnly((Package->PackageFlags & EPackageFlags::PKG_FilterEditorOnly) != 0);

	if (Package->Summary.NameCount > 0)
	{
		Ar.Seek(Package->Summary.NameOffset);

		FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);

		//Package.SummaryNames.Reserve(Summary.NameCount);
		for (int32 I = 0; I < Package->Summary.NameCount; ++I)
		{
			Ar << NameEntry;
			FName Name(NameEntry);
			//Package.SummaryNames.Add(Name);
			Package->NameMapBuilder.AddName(Name);
		}
	}

	class FNameReaderProxyArchive
		: public FArchiveProxy
	{
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
			int32 NameIndex = 0;
			int32 Number = 0;
			InnerArchive << NameIndex << Number;

			if (!NameMap.IsValidIndex(NameIndex))
			{
				UE_LOG(LogPackageStoreOptimizer, Fatal, TEXT("Bad name index %i/%i"), NameIndex, NameMap.Num());
			}

			const FNameEntryId MappedName = NameMap[NameIndex];
			Name = FName::CreateFromDisplayId(MappedName, Number);

			return *this;
		}

	private:
		const TArray<FNameEntryId>& NameMap;
	};
	FNameReaderProxyArchive ProxyAr(Ar, Package->NameMapBuilder.GetNameMap());

	if (Package->Summary.ImportCount > 0)
	{
		Package->ObjectImports.Reserve(Package->Summary.ImportCount);
		ProxyAr.Seek(Package->Summary.ImportOffset);
		for (int32 I = 0; I < Package->Summary.ImportCount; ++I)
		{
			ProxyAr << Package->ObjectImports.AddDefaulted_GetRef();
		}
	}

	if (Package->Summary.PreloadDependencyCount > 0)
	{
		Package->PreloadDependencies.Reserve(Package->Summary.PreloadDependencyCount);
		ProxyAr.Seek(Package->Summary.PreloadDependencyOffset);
		for (int32 I = 0; I < Package->Summary.PreloadDependencyCount; ++I)
		{
			ProxyAr << Package->PreloadDependencies.AddDefaulted_GetRef();
		}
	}

	if (Package->Summary.ExportCount > 0)
	{
		Package->ObjectExports.Reserve(Package->Summary.ExportCount);
		ProxyAr.Seek(Package->Summary.ExportOffset);
		for (int32 I = 0; I < Package->Summary.ExportCount; ++I)
		{
			FObjectExport& ObjectExport = Package->ObjectExports.AddDefaulted_GetRef();
			ProxyAr << ObjectExport;
			Package->ExportsSerialSize += ObjectExport.SerialSize;
		}
	}
}

void FPackageStoreOptimizer::ResolveImport(FPackageStorePackage::FImport* Imports, const FObjectImport* ObjectImports, int32 LocalImportIndex) const
{
	FPackageStorePackage::FImport* Import = Imports + LocalImportIndex;
	if (Import->FullName.Len() == 0)
	{
		Import->FullName.Reserve(256);

		const FObjectImport* ObjectImport = ObjectImports + LocalImportIndex;
		if (ObjectImport->OuterIndex.IsNull())
		{
			FName PackageName = ObjectImport->ObjectName;
			PackageName.AppendString(Import->FullName);
			Import->FullName.ToLowerInline();
			Import->PackageId = FPackageId::FromName(PackageName);
			Import->bIsScriptImport = Import->FullName.StartsWith(TEXT("/Script/"));
			Import->bIsPackageImport = true;
		}
		else
		{
			const int32 OuterIndex = ObjectImport->OuterIndex.ToImport();
			ResolveImport(Imports, ObjectImports, OuterIndex);
			FPackageStorePackage::FImport* OuterImport = Imports + OuterIndex;
			check(OuterImport->FullName.Len() > 0);
			Import->bIsScriptImport = OuterImport->bIsScriptImport;
			Import->FullName.Append(OuterImport->FullName);
			Import->FullName.AppendChar(TEXT('/'));
			ObjectImport->ObjectName.AppendString(Import->FullName);
			Import->FullName.ToLowerInline();
			Import->PackageId = OuterImport->PackageId;
		}
	}
}

void FPackageStoreOptimizer::ProcessImports(FPackageStorePackage* Package) const
{
	int32 ImportCount = Package->ObjectImports.Num();
	Package->Imports.SetNum(ImportCount);

	for (int32 ImportIndex = 0; ImportIndex < ImportCount; ++ImportIndex)
	{
		ResolveImport(Package->Imports.GetData(), Package->ObjectImports.GetData(), ImportIndex);
		FPackageStorePackage::FImport& Import = Package->Imports[ImportIndex];
		if (Import.bIsScriptImport)
		{
			Import.GlobalImportIndex = FPackageObjectIndex::FromScriptPath(Import.FullName);
		}
		else if (!Import.bIsPackageImport)
		{
			Import.GlobalImportIndex = FPackageObjectIndex::FromPackagePath(Import.FullName);
		}
		else
		{
			Package->ImportedPackageIds.Add(Import.PackageId);
		}
	}
}

void FPackageStoreOptimizer::ResolveExport(
	FPackageStorePackage::FExport* Exports,
	const FObjectExport* ObjectExports,
	const int32 LocalExportIndex,
	const FName& PackageName) const
{
	FPackageStorePackage::FExport* Export = Exports + LocalExportIndex;
	if (Export->FullName.Len() == 0)
	{
		Export->FullName.Reserve(256);
		const FObjectExport* ObjectExport = ObjectExports + LocalExportIndex;
		if (ObjectExport->OuterIndex.IsNull())
		{
			PackageName.AppendString(Export->FullName);
			Export->FullName.AppendChar(TEXT('/'));
			ObjectExport->ObjectName.AppendString(Export->FullName);
			Export->FullName.ToLowerInline();
			check(Export->FullName.Len() > 0);
		}
		else
		{
			check(ObjectExport->OuterIndex.IsExport());

			int32 OuterExportIndex = ObjectExport->OuterIndex.ToExport();
			ResolveExport(Exports, ObjectExports, OuterExportIndex, PackageName);
			FString& OuterName = Exports[OuterExportIndex].FullName;
			check(OuterName.Len() > 0);
			Export->FullName.Append(OuterName);
			Export->FullName.AppendChar(TEXT('/'));
			ObjectExport->ObjectName.AppendString(Export->FullName);
			Export->FullName.ToLowerInline();
		}
	}
}

void FPackageStoreOptimizer::ProcessExports(FPackageStorePackage* Package) const
{
	int32 ExportCount = Package->ObjectExports.Num();
	Package->Exports.SetNum(ExportCount);
	Package->ExportGraphNodes.Reserve(ExportCount * 2);

	auto PackageObjectIdFromPackageIndex =
		[](const TArray<FPackageStorePackage::FImport>& Imports, const FPackageIndex& PackageIndex) -> FPackageObjectIndex
	{
		if (PackageIndex.IsImport())
		{
			return Imports[PackageIndex.ToImport()].GlobalImportIndex;
		}
		if (PackageIndex.IsExport())
		{
			return FPackageObjectIndex::FromExportIndex(PackageIndex.ToExport());
		}
		return FPackageObjectIndex();
	};

	for (int32 ExportIndex = 0; ExportIndex < ExportCount; ++ExportIndex)
	{
		const FObjectExport& ObjectExport = Package->ObjectExports[ExportIndex];
		FPackageStorePackage::FExport& Export = Package->Exports[ExportIndex];
		Export.ObjectName = ObjectExport.ObjectName;
		Export.ObjectFlags = ObjectExport.ObjectFlags;
		Export.CookedSerialOffset = ObjectExport.SerialOffset;
		Export.CookedSerialSize = ObjectExport.SerialSize;
		Export.bNotForClient = ObjectExport.bNotForClient;
		Export.bNotForServer = ObjectExport.bNotForServer;
		Export.bIsPublic = (Export.ObjectFlags & RF_Public) > 0;
		ResolveExport(Package->Exports.GetData(), Package->ObjectExports.GetData(), ExportIndex, Package->Name);
		if (Export.bIsPublic)
		{
			check(Export.FullName.Len() > 0);
			Export.GlobalImportIndex = FPackageObjectIndex::FromPackagePath(Export.FullName);
		}

		Export.OuterIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.OuterIndex);
		Export.ClassIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.ClassIndex);
		Export.SuperIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.SuperIndex);
		Export.TemplateIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.TemplateIndex);

		Export.CreateNode = &Package->ExportGraphNodes.AddDefaulted_GetRef();
		Export.CreateNode->Package = Package;
		Export.CreateNode->BundleEntry.CommandType = FExportBundleEntry::ExportCommandType_Create;
		Export.CreateNode->BundleEntry.LocalExportIndex = ExportIndex;

		Export.SerializeNode = &Package->ExportGraphNodes.AddDefaulted_GetRef();
		Export.SerializeNode->Package = Package;
		Export.SerializeNode->BundleEntry.CommandType = FExportBundleEntry::ExportCommandType_Serialize;
		Export.SerializeNode->BundleEntry.LocalExportIndex = ExportIndex;
	}
}

void FPackageStoreOptimizer::SortPackagesInLoadOrder(TArray<FPackageStorePackage*>& Packages) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SortPackagesInLoadOrder);
	Algo::Sort(Packages, [](const FPackageStorePackage* A, const FPackageStorePackage* B)
	{
		return A->Id < B->Id;
	});

	TMap<FPackageStorePackage*, TArray<FPackageStorePackage*>> SortedEdges;
	for (FPackageStorePackage* Package : Packages)
	{
		for (FPackageStorePackage* ImportedPackage : Package->ImportedWaitingPackages)
		{
			TArray<FPackageStorePackage*>& SourceArray = SortedEdges.FindOrAdd(ImportedPackage);
			SourceArray.Add(Package);
		}
		for (FPackageId ImportedRedirectedPackageId : Package->ImportedRedirectedPackageIds)
		{
			FPackageStorePackage* FindImportedPackage = WaitingPackagesMap.FindRef(ImportedRedirectedPackageId);
			if (FindImportedPackage)
			{
				TArray<FPackageStorePackage*>& SourceArray = SortedEdges.FindOrAdd(FindImportedPackage);
				SourceArray.Add(Package);
			}
		}
	}
	for (auto& KV : SortedEdges)
	{
		TArray<FPackageStorePackage*>& SourceArray = KV.Value;
		Algo::Sort(SourceArray, [](const FPackageStorePackage* A, const FPackageStorePackage* B)
		{
			return A->Id < B->Id;
		});
	}

	TArray<FPackageStorePackage*> Result;
	Result.Reserve(Packages.Num());
	struct
	{
		void Visit(FPackageStorePackage* Package)
		{
			if (Package->bPermanentMark || Package->bTemporaryMark)
			{
				return;
			}
			Package->bTemporaryMark = true;
			TArray<FPackageStorePackage*>* TargetNodes = Edges.Find(Package);
			if (TargetNodes)
			{
				for (FPackageStorePackage* ToNode : *TargetNodes)
				{
					Visit(ToNode);
				}
			}
			Package->bTemporaryMark = false;
			Package->bPermanentMark = true;
			Result.Add(Package);
		}

		TMap<FPackageStorePackage*, TArray<FPackageStorePackage*>>& Edges;
		TArray<FPackageStorePackage*>& Result;
	} Visitor{ SortedEdges, Result };

	for (FPackageStorePackage* Package : Packages)
	{
		Visitor.Visit(Package);
	}
	check(Result.Num() == Packages.Num());
	Algo::Reverse(Result);
	Swap(Result, Packages);
}

FPackageStoreOptimizer::FExportGraphEdges FPackageStoreOptimizer::ProcessPreloadDependencies(const TArray<FPackageStorePackage*>& Packages) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPreloadDependencies);
	FExportGraphEdges Edges;
	auto AddExternalDependency = [this, &Edges, &Packages](FPackageStorePackage* Package, int32 FromImportIndex, FExportBundleEntry::EExportCommandType FromExportBundleCommandType, FPackageStorePackage::FExportGraphNode* ToNode)
	{
		const FPackageStorePackage::FImport& FromImport = Package->Imports[FromImportIndex];
		if (FromImport.GlobalImportIndex.IsScriptImport())
		{
			return;
		}

		FPackageStorePackage::FExternalDependency& ExternalDependency = ToNode->ExternalDependencies.AddDefaulted_GetRef();
		ExternalDependency.PackageId = FromImport.PackageId;
		ExternalDependency.GlobalImportIndex = FromImport.GlobalImportIndex;
		ExternalDependency.DebugImportFullName = FName(FromImport.FullName);
		ExternalDependency.ExportBundleCommandType = FromExportBundleCommandType;

		const TArray<FResolvedPublicExport>* FindResolvedPublicExports = ResolvedPublicExportsMap.Find(FromImport.PackageId);
		if (FindResolvedPublicExports)
		{
			bool bFoundExport = false;
			for (const FResolvedPublicExport& PublicExport : *FindResolvedPublicExports)
			{
				if (PublicExport.GlobalImportIndex == FromImport.GlobalImportIndex)
				{
					if (FromExportBundleCommandType == FExportBundleEntry::ExportCommandType_Create)
					{
						check(PublicExport.CreateExportBundleIndex >= 0);
						ExternalDependency.ResolvedExportBundleIndex = PublicExport.CreateExportBundleIndex;
					}
					else
					{
						check(PublicExport.SerializeExportBundleIndex >= 0);
						ExternalDependency.ResolvedExportBundleIndex = PublicExport.SerializeExportBundleIndex;
					}
					bFoundExport = true;
					break;
				}
			}
			if (!bFoundExport)
			{
				ExternalDependency.bIsConfirmedMissing = true;
			}
		}
		else
		{
			FPackageStorePackage* FindWaitingFromPackage = WaitingPackagesMap.FindRef(FromImport.PackageId);
			if (FindWaitingFromPackage)
			{
				bool bFoundExport = false;
				for (int32 ExportIndex = 0; ExportIndex < FindWaitingFromPackage->Exports.Num(); ++ExportIndex)
				{
					FPackageStorePackage::FExport& FromExport = FindWaitingFromPackage->Exports[ExportIndex];
					if (FromExport.GlobalImportIndex == FromImport.GlobalImportIndex)
					{
						FPackageStorePackage::FExportGraphNode* FromNode = FromExportBundleCommandType == FExportBundleEntry::ExportCommandType_Create ? FromExport.CreateNode : FromExport.SerializeNode;
						Edges.Add(FromNode, ToNode);
						ExternalDependency.ExportGraphNode = FromNode;

						for (const FPackageId& FromRedirectedPackageId : FindWaitingFromPackage->RedirectedToPackageIds)
						{
							FPackageStorePackage* FindRedirectedFromPackage = WaitingPackagesMap.FindRef(FromRedirectedPackageId);
							if (FindRedirectedFromPackage)
							{
								FPackageStorePackage::FExport& FromRedirectedExport = FindRedirectedFromPackage->Exports[ExportIndex];
								FPackageStorePackage::FExportGraphNode* FromRedirectedNode = FromExportBundleCommandType == FExportBundleEntry::ExportCommandType_Create ? FromRedirectedExport.CreateNode : FromRedirectedExport.SerializeNode;
								Edges.Add(FromRedirectedNode, ToNode);

								FPackageStorePackage::FExternalDependency& RedirectedExternalDependency = ToNode->ExternalDependencies.AddDefaulted_GetRef();
								RedirectedExternalDependency = ExternalDependency;
								RedirectedExternalDependency.PackageId = FromRedirectedPackageId;
								RedirectedExternalDependency.ExportGraphNode = FromRedirectedNode;
							}
						}

						bFoundExport = true;
						break;
					}
				}
				if (!bFoundExport)
				{
					ExternalDependency.bIsConfirmedMissing = true;
				}

			}
		}
	};

	for (FPackageStorePackage* Package : Packages)
	{
		for (int32 ExportIndex = 0; ExportIndex < Package->Exports.Num(); ++ExportIndex)
		{
			FPackageStorePackage::FExport& Export = Package->Exports[ExportIndex];
			const FObjectExport& ObjectExport = Package->ObjectExports[ExportIndex];

			Edges.Add(Export.CreateNode, Export.SerializeNode);

			if (ObjectExport.FirstExportDependency >= 0)
			{
				int32 RunningIndex = ObjectExport.FirstExportDependency;
				for (int32 Index = ObjectExport.SerializationBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = Package->PreloadDependencies[RunningIndex++];
					if (Dep.IsExport())
					{
						Edges.Add(Package->Exports[Dep.ToExport()].SerializeNode, Export.SerializeNode);
					}
					else
					{
						AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Serialize, Export.SerializeNode);
					}
				}

				for (int32 Index = ObjectExport.CreateBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = Package->PreloadDependencies[RunningIndex++];
					if (Dep.IsExport())
					{
						Edges.Add(Package->Exports[Dep.ToExport()].CreateNode, Export.SerializeNode);
					}
					else
					{
						AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Create, Export.SerializeNode);
					}
				}

				for (int32 Index = ObjectExport.SerializationBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = Package->PreloadDependencies[RunningIndex++];
					if (Dep.IsExport())
					{
						Edges.Add(Package->Exports[Dep.ToExport()].SerializeNode, Export.CreateNode);
					}
					else
					{
						AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Serialize, Export.CreateNode);
					}
				}

				for (int32 Index = ObjectExport.CreateBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = Package->PreloadDependencies[RunningIndex++];
					if (Dep.IsExport())
					{
						Edges.Add(Package->Exports[Dep.ToExport()].CreateNode, Export.CreateNode);
					}
					else
					{
						AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Create, Export.CreateNode);
					}
				}
			}
		}
	}
	return Edges;
}

TArray<FPackageStorePackage::FExportGraphNode*> FPackageStoreOptimizer::SortExportGraphNodesInLoadOrder(const TArray<FPackageStorePackage*>& Packages, FExportGraphEdges& Edges) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SortExportGraphNodesInLoadOrder);
	int32 NodeCount = 0;
	for (FPackageStorePackage* Package : Packages)
	{
		NodeCount += Package->ExportGraphNodes.Num();
	}
	for (auto& KV : Edges)
	{
		FPackageStorePackage::FExportGraphNode* ToNode = KV.Value;
		++ToNode->IncomingEdgeCount;
	}

	auto NodeSorter = [](const FPackageStorePackage::FExportGraphNode& A, const FPackageStorePackage::FExportGraphNode& B)
	{
		if (A.BundleEntry.LocalExportIndex == B.BundleEntry.LocalExportIndex)
		{
			return A.BundleEntry.CommandType < B.BundleEntry.CommandType;
		}
		return A.BundleEntry.LocalExportIndex < B.BundleEntry.LocalExportIndex;
	};

	for (FPackageStorePackage* Package : Packages)
	{
		for (FPackageStorePackage::FExportGraphNode& Node : Package->ExportGraphNodes)
		{
			if (Node.IncomingEdgeCount == 0)
			{
				Package->NodesWithNoIncomingEdges.HeapPush(&Node, NodeSorter);
			}
		}
	}
	TArray<FPackageStorePackage::FExportGraphNode*> LoadOrder;
	LoadOrder.Reserve(NodeCount);
	while (LoadOrder.Num() < NodeCount)
	{
		bool bMadeProgress = false;
		for (FPackageStorePackage* Package : Packages)
		{
			while (Package->NodesWithNoIncomingEdges.Num())
			{
				FPackageStorePackage::FExportGraphNode* RemovedNode;
				Package->NodesWithNoIncomingEdges.HeapPop(RemovedNode, NodeSorter, false);
				LoadOrder.Add(RemovedNode);
				bMadeProgress = true;
				for (auto EdgeIt = Edges.CreateKeyIterator(RemovedNode); EdgeIt; ++EdgeIt)
				{
					FPackageStorePackage::FExportGraphNode* ToNode = EdgeIt.Value();
					check(ToNode->IncomingEdgeCount > 0);
					if (--ToNode->IncomingEdgeCount == 0)
					{
						ToNode->Package->NodesWithNoIncomingEdges.HeapPush(ToNode, NodeSorter);
					}
					EdgeIt.RemoveCurrent();
				}
			}
		}
		check(bMadeProgress);
	}
	return LoadOrder;
}

void FPackageStoreOptimizer::CreateExportBundles(TArray<FPackageStorePackage*>& Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateExportBundles);
	SortPackagesInLoadOrder(Packages);
	FExportGraphEdges Edges = ProcessPreloadDependencies(Packages);
	TArray<FPackageStorePackage::FExportGraphNode*> LoadOrder = SortExportGraphNodesInLoadOrder(Packages, Edges);

	FPackageStorePackage* LastPackage = nullptr;
	for (FPackageStorePackage::FExportGraphNode* Node : LoadOrder)
	{
		check(Node && Node->Package);
		if (!Node || !Node->Package)
		{
			// Needed to please static analysis
			continue;
		}
		FPackageStorePackage::FExportBundle* Bundle;
		if (Node->Package != LastPackage)
		{
			Node->ExportBundleIndex = Node->Package->ExportBundles.Num();
			Bundle = &Node->Package->ExportBundles.AddDefaulted_GetRef();
			Bundle->LoadOrder = NextLoadOrder++;
			++TotalExportBundleCount;
			LastPackage = Node->Package;
		}
		else
		{
			Node->ExportBundleIndex = Node->Package->ExportBundles.Num() - 1;
			Bundle = &Node->Package->ExportBundles[Node->ExportBundleIndex];
		}
		Bundle->Entries.Add(Node->BundleEntry);
		++TotalExportBundleEntryCount;

		for (FPackageStorePackage::FExternalDependency& ExternalDependency : Node->ExternalDependencies)
		{
			if (ExternalDependency.ExportGraphNode)
			{
				check(ExternalDependency.ExportGraphNode->ExportBundleIndex >= 0);
				ExternalDependency.ResolvedExportBundleIndex = ExternalDependency.ExportGraphNode->ExportBundleIndex;
				ExternalDependency.ExportGraphNode = nullptr;
			}
		}
	}
}

static const TCHAR* GetExportNameSafe(const FString& ExportFullName, const FName& PackageName, int32 PackageNameLen)
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
			UE_CLOG(!bValidNameFormat, LogPackageStoreOptimizer, Warning,
				TEXT("Export name '%s' should start with '/' at position %d, i.e. right after package prefix '%s'"),
				*ExportFullName,
				PackageNameLen,
				*PackageName.ToString());
		}
	}
	else
	{
		UE_CLOG(!bValidNameLen, LogPackageStoreOptimizer, Warning,
			TEXT("Export name '%s' with length %d should be longer than package name '%s' with length %d"),
			*ExportFullName,
			PackageNameLen,
			*PackageName.ToString());
	}

	return nullptr;
};

bool FPackageStoreOptimizer::RedirectPackage(
	const FPackageStorePackage& SourcePackage,
	FPackageStorePackage& TargetPackage,
	TMap<FPackageObjectIndex, FPackageObjectIndex>& RedirectedToSourceImportIndexMap) const
{
	const int32 ExportCount =
		SourcePackage.Exports.Num() < TargetPackage.Exports.Num() ?
		SourcePackage.Exports.Num() :
		TargetPackage.Exports.Num();

	UE_CLOG(SourcePackage.Exports.Num() != TargetPackage.Exports.Num(), LogPackageStoreOptimizer, Verbose,
		TEXT("Redirection target package '%s' (0x%llX) for source package '%s' (0x%llX)  - Has ExportCount %d vs. %d"),
		*TargetPackage.Name.ToString(),
		TargetPackage.Id.ValueForDebugging(),
		*SourcePackage.Name.ToString(),
		SourcePackage.Id.ValueForDebugging(),
		TargetPackage.Exports.Num(),
		SourcePackage.Exports.Num());

	auto AppendMismatchMessage = [&TargetPackage, &SourcePackage](
		const TCHAR* Text, FName ExportName, FPackageObjectIndex TargetIndex, FPackageObjectIndex SourceIndex, FString& FailReason)
	{
		FailReason.Appendf(TEXT("Public export '%s' has %s %s vs. %s"),
			*ExportName.ToString(),
			Text,
			*TargetPackage.Exports[TargetIndex.ToExport()].FullName,
			*SourcePackage.Exports[SourceIndex.ToExport()].FullName);
	};

	const int32 TargetPackageNameLen = TargetPackage.Name.GetStringLength();
	const int32 SourcePackageNameLen = SourcePackage.Name.GetStringLength();

	TArray <TPair<int32, int32>, TInlineAllocator<64>> NewPublicExports;
	NewPublicExports.Reserve(ExportCount);

	bool bSuccess = true;
	int32 TargetIndex = 0;
	int32 SourceIndex = 0;
	while (TargetIndex < ExportCount && SourceIndex < ExportCount)
	{
		FString FailReason;
		const FPackageStorePackage::FExport& TargetExport = TargetPackage.Exports[TargetIndex];
		const FPackageStorePackage::FExport& SourceExport = SourcePackage.Exports[SourceIndex];

		const TCHAR* TargetExportStr = GetExportNameSafe(
			TargetExport.FullName, TargetPackage.Name, TargetPackageNameLen);
		const TCHAR* SourceExportStr = GetExportNameSafe(
			SourceExport.FullName, SourcePackage.Name, SourcePackageNameLen);

		if (!TargetExportStr || !SourceExportStr)
		{
			UE_LOG(LogPackageStoreOptimizer, Error,
				TEXT("Redirection target package '%s' (0x%llX) for source package '%s' (0x%llX) - Has some bad data from an earlier phase."),
				*TargetPackage.Name.ToString(),
				TargetPackage.Id.ValueForDebugging(),
				*SourcePackage.Name.ToString(),
				SourcePackage.Id.ValueForDebugging())
				return false;
		}

		int32 CompareResult = FCString::Stricmp(TargetExportStr, SourceExportStr);
		if (CompareResult < 0)
		{
			++TargetIndex;

			if (TargetExport.bIsPublic)
			{
				// public localized export is missing in the source package, so just keep it as it is
				NewPublicExports.Emplace(TargetIndex - 1, 1);
			}
		}
		else if (CompareResult > 0)
		{
			++SourceIndex;

			if (SourceExport.bIsPublic)
			{
				FailReason.Appendf(TEXT("Public source export '%s' is missing in the localized package"),
					*SourceExport.ObjectName.ToString());
			}
		}
		else
		{
			++TargetIndex;
			++SourceIndex;

			if (SourceExport.bIsPublic)
			{
				if (!TargetExport.bIsPublic)
				{
					FailReason.Appendf(TEXT("Public source export '%s' exists in the localized package")
						TEXT(", but is not a public localized export."),
						*SourceExport.ObjectName.ToString());
				}
				else if (TargetExport.ClassIndex != SourceExport.ClassIndex)
				{
					AppendMismatchMessage(TEXT("class"), TargetExport.ObjectName,
						TargetExport.ClassIndex, SourceExport.ClassIndex, FailReason);
				}
				else if (TargetExport.TemplateIndex != SourceExport.TemplateIndex)
				{
					AppendMismatchMessage(TEXT("template"), TargetExport.ObjectName,
						TargetExport.TemplateIndex, SourceExport.TemplateIndex, FailReason);
				}
				else if (TargetExport.SuperIndex != SourceExport.SuperIndex)
				{
					AppendMismatchMessage(TEXT("super"), TargetExport.ObjectName,
						TargetExport.SuperIndex, SourceExport.SuperIndex, FailReason);
				}
				else
				{
					NewPublicExports.Emplace(TargetIndex - 1, SourceIndex - 1);
				}
			}
			else if (TargetExport.bIsPublic)
			{
				FailReason.Appendf(TEXT("Export '%s' exists in the source package")
					TEXT(", but is not a public source export."),
					*TargetExport.ObjectName.ToString());
			}
		}

		if (FailReason.Len() > 0)
		{
			UE_LOG(LogPackageStoreOptimizer, Warning,
				TEXT("Redirection target package '%s' (0x%llX) for '%s' (0x%llX) - %s"),
				*TargetPackage.Name.ToString(),
				TargetPackage.Id.ValueForDebugging(),
				*SourcePackage.Name.ToString(),
				SourcePackage.Id.ValueForDebugging(),
				*FailReason);
			bSuccess = false;
		}
	}

	if (bSuccess)
	{
		for (TPair<int32, int32>& Pair : NewPublicExports)
		{
			FPackageStorePackage::FExport& TargetExport = TargetPackage.Exports[Pair.Key];
			if (Pair.Value != -1)
			{
				const FPackageStorePackage::FExport& SourceExport = SourcePackage.Exports[Pair.Value];

				RedirectedToSourceImportIndexMap.Add(TargetExport.GlobalImportIndex, SourceExport.GlobalImportIndex);
				TargetExport.GlobalImportIndex = SourceExport.GlobalImportIndex;
			}
		}
	}

	return bSuccess;
}

void FPackageStoreOptimizer::RedirectPackageUnverified(FPackageStorePackage& TargetPackage, TMap<FPackageObjectIndex, FPackageObjectIndex>& RedirectedToSourceImportIndexMap) const
{
	const int32 TargetPackageNameLen = TargetPackage.Name.GetStringLength();
	TStringBuilder<256> NameStringBuilder;
	for (FPackageStorePackage::FExport& Export : TargetPackage.Exports)
	{
		if (Export.bIsPublic)
		{
			const TCHAR* ExportNameStr = GetExportNameSafe(Export.FullName, TargetPackage.Name, TargetPackageNameLen);
			NameStringBuilder.Reset();
			TargetPackage.SourceName.AppendString(NameStringBuilder);
			NameStringBuilder.Append(TEXT("/"));
			NameStringBuilder.Append(ExportNameStr);
			FPackageObjectIndex SourceGlobalImportIndex = FPackageObjectIndex::FromPackagePath(NameStringBuilder);
			RedirectedToSourceImportIndexMap.Add(Export.GlobalImportIndex, SourceGlobalImportIndex);
			Export.GlobalImportIndex = SourceGlobalImportIndex;
		}
	}
}

void FPackageStoreOptimizer::ProcessRedirects(const TArray<FPackageStorePackage*> Packages) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessRedirects);
	TMap<FPackageObjectIndex, FPackageObjectIndex> RedirectedToSourceImportIndexMap;
	TMap<FPackageId, FPackageId> SourceToRedirectedPackageMap;

	for (FPackageStorePackage* Package : Packages)
	{
		if (Package->SourceName.IsNone() || Package->Name == Package->SourceName)
		{
			continue;
		}
		
		FPackageId SourcePackageId = FPackageId::FromName(Package->SourceName);
		FPackageStorePackage* SourcePackage = WaitingPackagesMap.FindRef(SourcePackageId);
		
		if (SourcePackage)
		{
			Package->bIsRedirected = RedirectPackage(*SourcePackage, *Package, RedirectedToSourceImportIndexMap);
		}
		else
		{
			if (Package->Region.Len() > 0)
			{
				// no update or verification required
				UE_LOG(LogPackageStoreOptimizer, Verbose,
					TEXT("For culture '%s': Localized package '%s' (0x%llX) is unique and does not override a source package."),
					*Package->Region,
					*Package->Name.ToString(),
					Package->Id.ValueForDebugging());
				continue;
			}

			// Assume DLC base game redirection
			// TODO: Unify these two redirection paths
			RedirectPackageUnverified(*Package, RedirectedToSourceImportIndexMap);
			SourceToRedirectedPackageMap.Add(SourcePackageId, Package->Id);
			Package->bIsRedirected = true;
			for (const FPackageStorePackage::FExport& Export : Package->Exports)
			{
				if (!Export.SuperIndex.IsNull() && Export.OuterIndex.IsNull())
				{
					UE_LOG(LogPackageStoreOptimizer, Warning, TEXT("Skipping redirect to package '%s' due to presence of UStruct '%s'"), *Package->Name.ToString(), *Export.ObjectName.ToString());
					Package->bIsRedirected = false;
					break;
				}
			}
		}
		
		if (Package->bIsRedirected)
		{
			UE_LOG(LogPackageStoreOptimizer, Verbose, TEXT("Adding package redirect from '%s' (0x%llX) to '%s' (0x%llX)."),
				*Package->SourceName.ToString(),
				SourcePackageId.ValueForDebugging(),
				*Package->Name.ToString(),
				Package->Id.ValueForDebugging());
			if (SourcePackage)
			{
				SourcePackage->RedirectedToPackageIds.Add(Package->Id);
			}
		}
		else
		{
			UE_LOG(LogPackageStoreOptimizer, Display,
				TEXT("Skipping package redirect from '%s' (0x%llX) to '%s' (0x%llX) due to mismatching public exports."),
				*Package->SourceName.ToString(),
				SourcePackageId.ValueForDebugging(),
				*Package->Name.ToString(),
				Package->Id.ValueForDebugging());
		}
	}

	for (FPackageStorePackage* Package : Packages)
	{
		for (FPackageStorePackage* ImportedPackage : Package->ImportedWaitingPackages)
		{
			Package->ImportedRedirectedPackageIds.Append(ImportedPackage->RedirectedToPackageIds);
		}

		for (FPackageStorePackage::FImport& Import : Package->Imports)
		{
			if (Import.GlobalImportIndex.IsPackageImport())
			{
				const FPackageObjectIndex* FindSourceGlobalImportIndex = RedirectedToSourceImportIndexMap.Find(Import.GlobalImportIndex);
				if (FindSourceGlobalImportIndex)
				{
					Import.GlobalImportIndex = *FindSourceGlobalImportIndex;
				}

				const FPackageId* FindRedirectedPackageId = SourceToRedirectedPackageMap.Find(Import.PackageId);
				if (FindRedirectedPackageId)
				{
					Import.PackageId = *FindRedirectedPackageId;
				}
			}
		}
	}
}

void FPackageStoreOptimizer::ResolvePackages(TArray<FPackageStorePackage*>& Packages, bool bAllowMissingImports)
{
	ProcessRedirects(Packages);
	CreateExportBundles(Packages);
	for (FPackageStorePackage* Package : Packages)
	{
		FinalizePackage(Package, bAllowMissingImports);
	}
}

void FPackageStoreOptimizer::FinalizePackageHeader(FPackageStorePackage* Package) const
{
	// Temporary Archive for serializing ImportMap
	FBufferWriter ImportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (const FPackageStorePackage::FImport& Import : Package->Imports)
	{
		FPackageObjectIndex GlobalImportIndex = Import.GlobalImportIndex;
		ImportMapArchive << GlobalImportIndex;
	}
	Package->ImportMapSize = ImportMapArchive.Tell();

	// Temporary Archive for serializing EDL graph data
	FBufferWriter GraphArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	int32 ReferencedPackagesCount = Package->ExternalArcs.Num();
	GraphArchive << ReferencedPackagesCount;
	TArray<TTuple<FPackageId, TArray<FPackageStorePackage::FArc>>> SortedExternalArcs;
	SortedExternalArcs.Reserve(Package->ExternalArcs.Num());
	for (auto& KV : Package->ExternalArcs)
	{
		FPackageId ImportedPackageId = KV.Key;
		TArray<FPackageStorePackage::FArc> SortedArcs = KV.Value;
		Algo::Sort(SortedArcs, [](const FPackageStorePackage::FArc& A, const FPackageStorePackage::FArc& B)
		{
			if (A.FromNodeIndex == B.FromNodeIndex)
			{
				return A.ToNodeIndex < B.ToNodeIndex;
			}
			return A.FromNodeIndex < B.FromNodeIndex;
		});
		SortedExternalArcs.Emplace(ImportedPackageId, MoveTemp(SortedArcs));
	}
	Algo::Sort(SortedExternalArcs, [](const TTuple<FPackageId, TArray<FPackageStorePackage::FArc>>& A, const TTuple<FPackageId, TArray<FPackageStorePackage::FArc>>& B)
	{
		return A.Key < B.Key;
	});
	for (auto& KV : SortedExternalArcs)
	{
		FPackageId ImportedPackageId = KV.Key;
		TArray<FPackageStorePackage::FArc>& Arcs = KV.Value;
		int32 ExternalArcCount = Arcs.Num();

		GraphArchive << ImportedPackageId;
		GraphArchive << ExternalArcCount;
		GraphArchive.Serialize(Arcs.GetData(), ExternalArcCount * sizeof(FPackageStorePackage::FArc));
	}
	Package->ExportBundleArcsSize = GraphArchive.Tell();

	// Temporary Archive for serializing export map data
	FBufferWriter ExportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (const FPackageStorePackage::FExport& Export : Package->Exports)
	{
		FExportMapEntry ExportMapEntry;
		ExportMapEntry.CookedSerialOffset = Export.CookedSerialOffset;
		ExportMapEntry.CookedSerialSize = Export.CookedSerialSize;
		ExportMapEntry.ObjectName = Package->NameMapBuilder.MapName(Export.ObjectName);
		ExportMapEntry.OuterIndex = Export.OuterIndex;
		ExportMapEntry.ClassIndex = Export.ClassIndex;
		ExportMapEntry.SuperIndex = Export.SuperIndex;
		ExportMapEntry.TemplateIndex = Export.TemplateIndex;
		ExportMapEntry.GlobalImportIndex = Export.GlobalImportIndex;
		ExportMapEntry.ObjectFlags = Export.ObjectFlags;
		ExportMapEntry.FilterFlags = EExportFilterFlags::None;
		if (Export.bNotForClient)
		{
			ExportMapEntry.FilterFlags = EExportFilterFlags::NotForClient;
		}
		else if (Export.bNotForServer)
		{
			ExportMapEntry.FilterFlags = EExportFilterFlags::NotForServer;
		}

		ExportMapArchive << ExportMapEntry;
	}
	Package->ExportMapSize = ExportMapArchive.Tell();

	// Temporary archive for serializing export bundle data
	FBufferWriter ExportBundlesArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	uint32 ExportBundleEntryIndex = 0;
	for (const FPackageStorePackage::FExportBundle& ExportBundle : Package->ExportBundles)
	{
		const uint32 EntryCount = ExportBundle.Entries.Num();
		FExportBundleHeader ExportBundleHeader{ ExportBundleEntryIndex, EntryCount };
		ExportBundlesArchive << ExportBundleHeader;

		ExportBundleEntryIndex += EntryCount;
	}
	for (const FPackageStorePackage::FExportBundle& ExportBundle : Package->ExportBundles)
	{
		for (FExportBundleEntry BundleEntry : ExportBundle.Entries)
		{
			ExportBundlesArchive << BundleEntry;
		}
	}
	Package->ExportBundleHeadersSize = ExportBundlesArchive.Tell();

	Package->NameMapBuilder.MarkNameAsReferenced(Package->Name);
	FMappedName MappedPackageName = Package->NameMapBuilder.MapName(Package->Name);
	Package->NameMapBuilder.MarkNameAsReferenced(Package->SourceName);
	FMappedName MappedPackageSourceName = Package->NameMapBuilder.MapName(Package->SourceName);

	TArray<uint8> NamesBuffer;
	TArray<uint8> NameHashesBuffer;
	SaveNameBatch(Package->NameMapBuilder.GetNameMap(), NamesBuffer, NameHashesBuffer);
	Package->NameMapSize = Align(NamesBuffer.Num(), 8) + NameHashesBuffer.Num();

	uint64 HeaderSerialSize =
		sizeof(FPackageSummary)
		+ Package->NameMapSize
		+ Package->ImportMapSize
		+ Package->ExportMapSize
		+ Package->ExportBundleHeadersSize
		+ Package->ExportBundleArcsSize;

	Package->OptimizedHeaderBuffer = FIoBuffer(HeaderSerialSize);
	uint8* HeaderData = Package->OptimizedHeaderBuffer.Data();
	FMemory::Memzero(HeaderData, HeaderSerialSize);
	FPackageSummary* PackageSummary = reinterpret_cast<FPackageSummary*>(HeaderData);

	PackageSummary->Name = MappedPackageName;
	PackageSummary->SourceName = MappedPackageSourceName;
	PackageSummary->PackageFlags = Package->PackageFlags;
	PackageSummary->CookedHeaderSize = Package->CookedHeaderSize;
	FBufferWriter HeaderArchive(HeaderData, HeaderSerialSize);
	HeaderArchive.Seek(sizeof(FPackageSummary));

	// NameMap data
	{
		PackageSummary->NameMapNamesOffset = HeaderArchive.Tell();
		check(PackageSummary->NameMapNamesOffset % 8 == 0);
		PackageSummary->NameMapNamesSize = NamesBuffer.Num();
		HeaderArchive.Serialize(NamesBuffer.GetData(), NamesBuffer.Num());
		PackageSummary->NameMapHashesOffset = Align(HeaderArchive.Tell(), 8);
		int32 PaddingByteCount = PackageSummary->NameMapHashesOffset - HeaderArchive.Tell();
		if (PaddingByteCount)
		{
			check(PaddingByteCount < 8);
			uint8 PaddingBytes[8]{ 0 };
			HeaderArchive.Serialize(PaddingBytes, PaddingByteCount);
		}
		PackageSummary->NameMapHashesSize = NameHashesBuffer.Num();
		HeaderArchive.Serialize(NameHashesBuffer.GetData(), NameHashesBuffer.Num());
	}

	// ImportMap data
	{
		check(ImportMapArchive.Tell() == Package->ImportMapSize);
		PackageSummary->ImportMapOffset = HeaderArchive.Tell();
		HeaderArchive.Serialize(ImportMapArchive.GetWriterData(), ImportMapArchive.Tell());
	}

	// ExportMap data
	{
		check(ExportMapArchive.Tell() == Package->ExportMapSize);
		PackageSummary->ExportMapOffset = HeaderArchive.Tell();
		HeaderArchive.Serialize(ExportMapArchive.GetWriterData(), ExportMapArchive.Tell());
	}

	// ExportBundle data
	{
		check(ExportBundlesArchive.Tell() == Package->ExportBundleHeadersSize);
		PackageSummary->ExportBundlesOffset = HeaderArchive.Tell();
		HeaderArchive.Serialize(ExportBundlesArchive.GetWriterData(), ExportBundlesArchive.Tell());
	}

	// Graph data
	{
		check(GraphArchive.Tell() == Package->ExportBundleArcsSize);
		PackageSummary->GraphDataOffset = HeaderArchive.Tell();
		PackageSummary->GraphDataSize = Package->ExportBundleArcsSize;
		HeaderArchive.Serialize(GraphArchive.GetWriterData(), GraphArchive.Tell());
	}
}

void FPackageStoreOptimizer::FinalizePackage(FPackageStorePackage* Package, bool bAllowMissingImports)
{
	for (FPackageStorePackage::FExportGraphNode& Node : Package->ExportGraphNodes)
	{
		check(Node.ExportBundleIndex >= 0);
		for (FPackageStorePackage::FExternalDependency& ExternalDependency : Node.ExternalDependencies)
		{
			if (ExternalDependency.bIsConfirmedMissing)
			{
				UE_LOG(LogPackageStoreOptimizer, Warning, TEXT("Package '%s' missing import '%s'"), *Package->Name.ToString(), *ExternalDependency.DebugImportFullName.ToString());
				continue;
			}
			TArray<FPackageStorePackage::FArc>& ArcsFromImportedPackage = Package->ExternalArcs.FindOrAdd(ExternalDependency.PackageId);
			FPackageStorePackage::FArc Arc;
			Arc.FromNodeIndex = ExternalDependency.ResolvedExportBundleIndex;
			Arc.ToNodeIndex = Node.ExportBundleIndex;
			if (!ArcsFromImportedPackage.Contains(Arc))
			{
				ArcsFromImportedPackage.Add(Arc);
				++TotalExportBundleArcCount;
			}
		}
	}

	FinalizePackageHeader(Package);

	for (FPackageStorePackage* ImportedPackage : Package->ImportedWaitingPackages)
	{
		ImportedPackage->ImportedByWaitingPackages.Remove(Package);
	}
	for (FPackageStorePackage* ImportedByPackage : Package->ImportedByWaitingPackages)
	{
		ImportedByPackage->ImportedWaitingPackages.Remove(Package);
	}
	WaitingPackagesMap.Remove(Package->Id);

	TArray<FResolvedPublicExport>& ResolvedPublicExports = ResolvedPublicExportsMap.FindOrAdd(Package->Id);
	check(ResolvedPublicExports.IsEmpty());
	for (const FPackageStorePackage::FExport& Export : Package->Exports)
	{
		if (Export.bIsPublic)
		{
			FResolvedPublicExport& PublicExport = ResolvedPublicExports.AddDefaulted_GetRef();
			PublicExport.GlobalImportIndex = Export.GlobalImportIndex;
			check(!PublicExport.GlobalImportIndex.IsNull());
			PublicExport.CreateExportBundleIndex = Export.CreateNode->ExportBundleIndex;
			check(PublicExport.CreateExportBundleIndex >= 0);
			PublicExport.SerializeExportBundleIndex = Export.SerializeNode->ExportBundleIndex;
			check(PublicExport.SerializeExportBundleIndex >= 0);
		}
	}

	ResolvedPackages.Add(Package);
}

bool FPackageStoreOptimizer::IsWaitingForImportsRecursive(FPackageStorePackage* Package) const
{
	if (Package->UnresolvedImportedPackagesCount)
	{
		return true;
	}

	if (Package->bTemporaryMark)
	{
		return false;
	}
	Package->bTemporaryMark = true;

	for (FPackageStorePackage* ImportedPackage : Package->ImportedWaitingPackages)
	{
		if (IsWaitingForImportsRecursive(ImportedPackage))
		{
			Package->bTemporaryMark = false;
			return true;
		}
	}
	Package->bTemporaryMark = false;
	return false;
}

void FPackageStoreOptimizer::BeginResolvePackage(FPackageStorePackage* Package)
{
	++TotalPackageCount;
	for (const FPackageId& ImportedPackageId : Package->ImportedPackageIds)
	{
		check(ImportedPackageId != Package->Id);
		if (!ResolvedPublicExportsMap.Contains(ImportedPackageId))
		{
			FPackageStorePackage* FindWaitingPackage = WaitingPackagesMap.FindRef(ImportedPackageId);
			if (FindWaitingPackage)
			{
				Package->ImportedWaitingPackages.Add(FindWaitingPackage);
				FindWaitingPackage->ImportedByWaitingPackages.Add(Package);
			}
			else
			{
				++Package->UnresolvedImportedPackagesCount;
				ImportedPackageToWaitingPackagesMap.Add(ImportedPackageId, Package);
			}
		}
		else
		{
			check(!WaitingPackagesMap.Contains(ImportedPackageId));
		}
	}

	WaitingPackagesMap.Add(Package->Id, Package);
	
	for (auto WaitingPackageIt = ImportedPackageToWaitingPackagesMap.CreateKeyIterator(Package->Id); WaitingPackageIt; ++WaitingPackageIt)
	{
		FPackageStorePackage* WaitingPackage = WaitingPackageIt.Value();
		WaitingPackage->ImportedWaitingPackages.Add(Package);
		Package->ImportedByWaitingPackages.Add(WaitingPackage);
		check(WaitingPackage->UnresolvedImportedPackagesCount > 0);
		WaitingPackageIt.RemoveCurrent();
	}

	if (!bIncrementalResolveEnabled)
	{
		return;
	}
	TArray<FPackageStorePackage*> PackagesReadyToResolve;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateWaitList);
		for (auto& KV : WaitingPackagesMap)
		{
			FPackageStorePackage* WaitingPackage = KV.Value;
			if (!IsWaitingForImportsRecursive(WaitingPackage))
			{
				PackagesReadyToResolve.Add(WaitingPackage);
			}
		}
	}

	if (PackagesReadyToResolve.Num())
	{
		ResolvePackages(PackagesReadyToResolve, false);
	}
}

FIoBuffer FPackageStoreOptimizer::CreatePackageBuffer(const FPackageStorePackage* Package, const FIoBuffer& CookedExportsDataBuffer, TArray<FFileRegion>* InOutFileRegions) const
{
	check(Package->OptimizedHeaderBuffer.DataSize() > 0);
	const uint64 BundleBufferSize = Package->OptimizedHeaderBuffer.DataSize() + Package->ExportsSerialSize;
	FIoBuffer BundleBuffer(BundleBufferSize);
	FMemory::Memcpy(BundleBuffer.Data(), Package->OptimizedHeaderBuffer.Data(), Package->OptimizedHeaderBuffer.DataSize());
	uint64 BundleBufferOffset = Package->OptimizedHeaderBuffer.DataSize();

	TArray<FFileRegion> OutputRegions;

	for (const FPackageStorePackage::FExportBundle& ExportBundle : Package->ExportBundles)
	{
		for (const FExportBundleEntry& BundleEntry : ExportBundle.Entries)
		{
			if (BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FPackageStorePackage::FExport& Export = Package->Exports[BundleEntry.LocalExportIndex];
				uint64 AdjustedSerialOffset = Export.CookedSerialOffset - Package->CookedHeaderSize;
				check(AdjustedSerialOffset + Export.CookedSerialSize <= CookedExportsDataBuffer.DataSize());
				FMemory::Memcpy(BundleBuffer.Data() + BundleBufferOffset, CookedExportsDataBuffer.Data() + AdjustedSerialOffset, Export.CookedSerialSize);

				if (InOutFileRegions)
				{
					// Find overlapping regions and adjust them to match the new offset of the export data
					for (const FFileRegion& Region : *InOutFileRegions)
					{
						if (AdjustedSerialOffset <= Region.Offset && Region.Offset + Region.Length <= AdjustedSerialOffset + Export.CookedSerialSize)
						{
							FFileRegion NewRegion = Region;
							NewRegion.Offset -= AdjustedSerialOffset;
							NewRegion.Offset += BundleBufferOffset;
							OutputRegions.Add(NewRegion);
						}
					}
				}
				BundleBufferOffset += Export.CookedSerialSize;
			}
		}
	}
	check(BundleBufferOffset == BundleBuffer.DataSize());

	if (InOutFileRegions)
	{
		*InOutFileRegions = OutputRegions;
	}

	return BundleBuffer;
}

void FPackageStoreOptimizer::FindScriptObjectsRecursive(FPackageObjectIndex OuterIndex, UObject* Object, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform)
{
	if (!Object->HasAllFlags(RF_Public))
	{
		UE_LOG(LogPackageStoreOptimizer, Verbose, TEXT("Skipping script object: %s (!RF_Public)"), *Object->GetFullName());
		return;
	}

	const UObject* ObjectForExclusion = Object->HasAnyFlags(RF_ClassDefaultObject) ? (const UObject*)Object->GetClass() : Object;
	const EObjectMark ObjectMarks = GetExcludedObjectMarksForObject(ObjectForExclusion, TargetPlatform);

	if (ObjectMarks & ExcludedObjectMarks)
	{
		UE_LOG(LogPackageStoreOptimizer, Verbose, TEXT("Skipping script object: %s (Excluded for target platform)"), *Object->GetFullName());
		return;
	}

	const FScriptObjectData* Outer = ScriptObjectsMap.Find(OuterIndex);
	check(Outer);

	FName ObjectName = Object->GetFName();

	FString TempFullName = ScriptObjectsMap.FindRef(OuterIndex).FullName;
	TempFullName.AppendChar(TEXT('/'));
	ObjectName.AppendString(TempFullName);

	TempFullName.ToLowerInline();
	FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(TempFullName);

	FScriptObjectData* ScriptImport = ScriptObjectsMap.Find(GlobalImportIndex);
	if (ScriptImport)
	{
		UE_LOG(LogPackageStoreOptimizer, Fatal, TEXT("Import name hash collision \"%s\" and \"%s"), *TempFullName, *ScriptImport->FullName);
	}

	FPackageObjectIndex CDOClassIndex = Outer->CDOClassIndex;
	if (CDOClassIndex.IsNull())
	{
		TCHAR NameBuffer[FName::StringBufferSize];
		uint32 Len = ObjectName.ToString(NameBuffer);
		if (FCString::Strncmp(NameBuffer, TEXT("Default__"), 9) == 0)
		{
			FString CDOClassFullName = Outer->FullName;
			CDOClassFullName.AppendChar(TEXT('/'));
			CDOClassFullName.AppendChars(NameBuffer + 9, Len - 9);
			CDOClassFullName.ToLowerInline();

			CDOClassIndex = FPackageObjectIndex::FromScriptPath(CDOClassFullName);
		}
	}

	ScriptImport = &ScriptObjectsMap.Add(GlobalImportIndex);
	ScriptImport->GlobalIndex = GlobalImportIndex;
	ScriptImport->FullName = MoveTemp(TempFullName);
	ScriptImport->OuterIndex = Outer->GlobalIndex;
	ScriptImport->ObjectName = ObjectName;
	ScriptImport->CDOClassIndex = CDOClassIndex;

	TArray<UObject*> InnerObjects;
	GetObjectsWithOuter(Object, InnerObjects, /*bIncludeNestedObjects*/false);
	for (UObject* InnerObject : InnerObjects)
	{
		FindScriptObjectsRecursive(GlobalImportIndex, InnerObject, ExcludedObjectMarks, TargetPlatform);
	}
};

void FPackageStoreOptimizer::FindScriptObjects(const ITargetPlatform* TargetPlatform)
{
	const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform(TargetPlatform);

	TArray<UPackage*> ScriptPackages;
	FindAllRuntimeScriptPackages(ScriptPackages);

	TArray<UObject*> InnerObjects;
	for (UPackage* Package : ScriptPackages)
	{
		FName ObjectName = Package->GetFName();
		FString FullName = Package->GetName();

		FullName.ToLowerInline();
		FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(FullName);

		FScriptObjectData* ScriptImport = ScriptObjectsMap.Find(GlobalImportIndex);
		checkf(!ScriptImport, TEXT("Import name hash collision \"%s\" and \"%s"), *FullName, *ScriptImport->FullName);

		ScriptImport = &ScriptObjectsMap.Add(GlobalImportIndex);
		ScriptImport->GlobalIndex = GlobalImportIndex;
		ScriptImport->FullName = FullName;
		ScriptImport->OuterIndex = FPackageObjectIndex();
		ScriptImport->ObjectName = ObjectName;

		InnerObjects.Reset();
		GetObjectsWithOuter(Package, InnerObjects, /*bIncludeNestedObjects*/false);
		for (UObject* InnerObject : InnerObjects)
		{
			FindScriptObjectsRecursive(GlobalImportIndex, InnerObject, ExcludedObjectMarks, TargetPlatform);
		}
	}

	FLargeMemoryWriter ScriptObjectsArchive(0, true);

	int32 NumScriptObjects = ScriptObjectsMap.Num();
	ScriptObjectsArchive << NumScriptObjects;
	TotalScriptObjectCount = ScriptObjectsMap.Num();
}

void FPackageStoreOptimizer::Flush(bool bAllowMissingImports, TFunction<void(FPackageStorePackage*)> ResolvedPackageCallback)
{
	if (bAllowMissingImports)
	{
		TArray<FPackageStorePackage*> PackagesToResolve;
		WaitingPackagesMap.GenerateValueArray(PackagesToResolve);
		ResolvePackages(PackagesToResolve, true);
		
		check(WaitingPackagesMap.IsEmpty());
		ImportedPackageToWaitingPackagesMap.Empty();
	}

	if (ResolvedPackageCallback)
	{
		for (FPackageStorePackage* ResolvedPackage : ResolvedPackages)
		{
			ResolvedPackageCallback(ResolvedPackage);
		}
	}
	ResolvedPackages.Empty();
}

uint64 FPackageStoreOptimizer::WriteScriptObjects(FIoStoreWriter* IoStoreWriter) const
{
	TArray<FScriptObjectData> ScriptObjectsAsArray;
	ScriptObjectsMap.GenerateValueArray(ScriptObjectsAsArray);
	Algo::Sort(ScriptObjectsAsArray, [](const FScriptObjectData& A, const FScriptObjectData& B)
	{
		return A.FullName < B.FullName;
	});
	
	FLargeMemoryWriter ScriptObjectsArchive(0, true);
	int32 NumScriptObjects = ScriptObjectsAsArray.Num();
	ScriptObjectsArchive << NumScriptObjects;
	FPackageStoreNameMapBuilder NameMapBuilder;
	NameMapBuilder.SetNameMapType(FMappedName::EType::Global);
	for (const FScriptObjectData& ImportData : ScriptObjectsAsArray)
	{
		NameMapBuilder.MarkNameAsReferenced(ImportData.ObjectName);
		FScriptObjectEntry Entry;
		Entry.ObjectName = NameMapBuilder.MapName(ImportData.ObjectName).ToUnresolvedMinimalName();
		Entry.GlobalIndex = ImportData.GlobalIndex;
		Entry.OuterIndex = ImportData.OuterIndex;
		Entry.CDOClassIndex = ImportData.CDOClassIndex;

		ScriptObjectsArchive << Entry;
	}

	FIoWriteOptions WriteOptions;
	WriteOptions.DebugName = TEXT("LoaderInitialLoadMeta");
	IoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoBuffer(FIoBuffer::Wrap, ScriptObjectsArchive.GetData(), ScriptObjectsArchive.TotalSize()), WriteOptions);

	TArray<uint8> Names;
	TArray<uint8> Hashes;
	SaveNameBatch(NameMapBuilder.GetNameMap(), /* out */ Names, /* out */ Hashes);
	WriteOptions.DebugName = TEXT("LoaderGlobalNames");
	IoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames),
		FIoBuffer(FIoBuffer::Wrap, Names.GetData(), Names.Num()), WriteOptions);
	WriteOptions.DebugName = TEXT("LoaderGlobalNameHashes");
	IoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes),
		FIoBuffer(FIoBuffer::Wrap, Hashes.GetData(), Hashes.Num()), WriteOptions);

	return ScriptObjectsArchive.TotalSize() + Names.Num() + Hashes.Num();
}

FPackageStoreContainerHeaderEntry FPackageStoreOptimizer::CreateContainerHeaderEntry(const FPackageStorePackage* Package) const
{
	FPackageStoreContainerHeaderEntry Result;
	Result.Name = Package->Name;
	Result.SourceName = Package->SourceName;
	Result.Region = Package->Region;
	Result.ExportBundlesSize = Package->OptimizedHeaderBuffer.DataSize() + Package->ExportsSerialSize;
	Result.ExportCount = Package->Exports.Num();
	Result.ExportBundleCount = Package->ExportBundles.Num();
	Result.LoadOrder = Package->GetLoadOrder();
	Result.ImportedPackageIds = Package->ImportedPackageIds;
	Result.bIsRedirected = Package->bIsRedirected;
	return Result;
}

FContainerHeader FPackageStoreOptimizer::CreateContainerHeader(const FIoContainerId& ContainerId, const TArray<FPackageStoreContainerHeaderEntry>& Packages) const
{
	FContainerHeader Header;
	Header.ContainerId = ContainerId;
	Header.PackageCount = Packages.Num();
	int32 StoreTocSize = Header.PackageCount * sizeof(FPackageStoreEntry);
	FLargeMemoryWriter StoreTocArchive(0, true);
	FLargeMemoryWriter StoreDataArchive(0, true);

	auto SerializePackageEntryCArrayHeader = [&StoreTocSize, &StoreTocArchive, &StoreDataArchive](int32 Count)
	{
		const int32 RemainingTocSize = StoreTocSize - StoreTocArchive.Tell();
		const int32 OffsetFromThis = RemainingTocSize + StoreDataArchive.Tell();
		uint32 ArrayNum = Count > 0 ? Count : 0;
		uint32 OffsetToDataFromThis = ArrayNum > 0 ? OffsetFromThis : 0;

		StoreTocArchive << ArrayNum;
		StoreTocArchive << OffsetToDataFromThis;
	};

	TArray<const FPackageStoreContainerHeaderEntry*> SortedPackages;
	SortedPackages.Reserve(Packages.Num());
	for (const FPackageStoreContainerHeaderEntry& Package : Packages)
	{
		SortedPackages.Add(&Package);
	}
	Algo::Sort(SortedPackages, [](const FPackageStoreContainerHeaderEntry* A, const FPackageStoreContainerHeaderEntry* B)
	{
		return A->GetId() < B->GetId();
	});

	Header.PackageIds.Reserve(SortedPackages.Num());
	for (const FPackageStoreContainerHeaderEntry* Package : SortedPackages)
	{
		Header.PackageIds.Add(Package->GetId());
		if (Package->bIsRedirected)
		{
			if (Package->Region.Len() > 0)
			{
				Header.CulturePackageMap.FindOrAdd(Package->Region).Emplace(Package->GetSourceId(), Package->GetId());
			}
			else
			{
				Header.PackageRedirects.Add(MakeTuple(Package->GetSourceId(), Package->GetId()));
			}
		}
		
		// StoreEntries
		uint64 ExportBundlesSize = Package->ExportBundlesSize;
		int32 ExportCount = Package->ExportCount;
		int32 ExportBundleCount = Package->ExportBundleCount;
		uint32 LoadOrder = Package->LoadOrder;
		uint32 Pad = 0;

		StoreTocArchive << ExportBundlesSize;
		StoreTocArchive << ExportCount;
		StoreTocArchive << ExportBundleCount;
		StoreTocArchive << LoadOrder;
		StoreTocArchive << Pad;

		// ImportedPackages
		const TArray<FPackageId>& ImportedPackageIds = Package->ImportedPackageIds;
		SerializePackageEntryCArrayHeader(ImportedPackageIds.Num());
		for (FPackageId ImportedPackageId : ImportedPackageIds)
		{
			check(ImportedPackageId.IsValid());
			StoreDataArchive << ImportedPackageId;
		}
	}

	check(StoreTocArchive.TotalSize() == StoreTocSize);

	const int32 StoreByteCount = StoreTocArchive.TotalSize() + StoreDataArchive.TotalSize();
	Header.StoreEntries.AddUninitialized(StoreByteCount);
	FBufferWriter PackageStoreArchive(Header.StoreEntries.GetData(), StoreByteCount);
	PackageStoreArchive.Serialize(StoreTocArchive.GetData(), StoreTocArchive.TotalSize());
	PackageStoreArchive.Serialize(StoreDataArchive.GetData(), StoreDataArchive.TotalSize());
	FPackageStoreNameMapBuilder DummyNameMapBuilder;
	SaveNameBatch(DummyNameMapBuilder.GetNameMap(), Header.Names, Header.NameHashes);

	return Header;
}
