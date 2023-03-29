// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageStoreOptimizer.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/BufferWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/NameBatchSerialization.h"
#include "Containers/Map.h"
#include "UObject/UObjectHash.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/Object.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"
#include "Misc/SecureHash.h"
#include "Serialization/LargeMemoryReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogPackageStoreOptimizer, Log, All);

// modified copy from SavePackage
EObjectMark GetExcludedObjectMarksForTargetPlatform(const ITargetPlatform* TargetPlatform)
{
	EObjectMark Marks = OBJECTMARK_NotForTargetPlatform;
	if (!TargetPlatform->AllowsEditorObjects())
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
#if WITH_ENGINE
	// NotForServer && NotForClient implies EditorOnly
	const bool bIsEditorOnlyObject = (Marks & OBJECTMARK_NotForServer) && (Marks & OBJECTMARK_NotForClient);
	const bool bTargetAllowsEditorObjects = TargetPlatform->AllowsEditorObjects();

	// no need to query the target platform if the object is editoronly and the targetplatform doesn't allow editor objects 
	const bool bCheckTargetPlatform = !bIsEditorOnlyObject || bTargetAllowsEditorObjects;
	if (bCheckTargetPlatform && (!Object->NeedsLoadForTargetPlatform(TargetPlatform) || !TargetPlatform->AllowObject(Object)))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForTargetPlatform);
	}
#endif
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

void FPackageStoreOptimizer::Initialize()
{
	FindScriptObjects();
}

void FPackageStoreOptimizer::Initialize(const FIoBuffer& ScriptObjectsBuffer)
{
	LoadScriptObjectsBuffer(ScriptObjectsBuffer);
}

FPackageStorePackage* FPackageStoreOptimizer::CreateMissingPackage(const FName& Name) const
{
	FPackageStorePackage* Package = new FPackageStorePackage();
	Package->Name = Name;
	Package->Id = FPackageId::FromName(Name);
	return Package;
}

FPackageStorePackage* FPackageStoreOptimizer::CreatePackageFromCookedHeader(const FName& Name, const FIoBuffer& CookedHeaderBuffer) const
{
	FPackageStorePackage* Package = new FPackageStorePackage();
	Package->Id = FPackageId::FromName(Name);
	Package->Name = Name;
	
	FCookedHeaderData CookedHeaderData = LoadCookedHeader(CookedHeaderBuffer);
	if (!CookedHeaderData.Summary.bUnversioned)
	{
		FZenPackageVersioningInfo& VersioningInfo = Package->VersioningInfo.Emplace();
		VersioningInfo.ZenVersion = EZenPackageVersion::Latest;
		VersioningInfo.PackageVersion = CookedHeaderData.Summary.GetFileVersionUE();
		VersioningInfo.LicenseeVersion = CookedHeaderData.Summary.GetFileVersionLicenseeUE();
		VersioningInfo.CustomVersions = CookedHeaderData.Summary.GetCustomVersionContainer();
	}
	Package->PackageFlags = CookedHeaderData.Summary.GetPackageFlags();
	Package->CookedHeaderSize = CookedHeaderData.Summary.TotalHeaderSize;
	for (int32 I = 0; I < CookedHeaderData.Summary.NamesReferencedFromExportDataCount; ++I)
	{
		Package->NameMapBuilder.AddName(CookedHeaderData.SummaryNames[I]);
	}

	TArray<FPackageStorePackage::FUnresolvedImport> Imports;
	ProcessImports(CookedHeaderData, Package, Imports);
	ProcessExports(CookedHeaderData, Package, Imports.GetData());
	ProcessPreloadDependencies(CookedHeaderData, Package);
	ProcessDataResources(CookedHeaderData, Package);

	CreateExportBundles(Package);

	return Package;
}

FPackageStorePackage* FPackageStoreOptimizer::CreatePackageFromZenPackageHeader(const FName& Name, const FIoBuffer& Buffer, int32 ExportBundleCount, const TArrayView<const FPackageId>& ImportedPackageIds) const
{
	FPackageStorePackage* Package = new FPackageStorePackage();
	Package->Id = FPackageId::FromName(Name);

	// The package id should be generated from the original name
	// Support for optional package is not implemented when loading from the package store yet however
	FString NameStr = Name.ToString();
	int32 Index = NameStr.Find(FPackagePath::GetOptionalSegmentExtensionModifier());
	if (Index != INDEX_NONE)
	{
		unimplemented();
	}

	Package->Name = Name;
	
	FZenPackageHeaderData ZenHeaderData = LoadZenPackageHeader(Buffer, ExportBundleCount, ImportedPackageIds);
	if (ZenHeaderData.VersioningInfo.IsSet())
	{
		Package->VersioningInfo.Emplace(ZenHeaderData.VersioningInfo.GetValue());
	}
	Package->PackageFlags = ZenHeaderData.Summary.PackageFlags;
	Package->CookedHeaderSize = ZenHeaderData.Summary.CookedHeaderSize;
	for (FDisplayNameEntryId DisplayId : ZenHeaderData.NameMap)
	{
		Package->NameMapBuilder.AddName(DisplayId);
	}
	Package->BulkDataEntries = MoveTemp(ZenHeaderData.BulkDataEntries);
	ProcessImports(ZenHeaderData, Package);
	ProcessExports(ZenHeaderData, Package);
	ProcessPreloadDependencies(ZenHeaderData, Package);
	CreateExportBundles(Package);

	return Package;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FPackageStoreOptimizer::FCookedHeaderData FPackageStoreOptimizer::LoadCookedHeader(const FIoBuffer& CookedHeaderBuffer) const
{
	FCookedHeaderData CookedHeaderData;
	TArrayView<const uint8> MemView(CookedHeaderBuffer.Data(), CookedHeaderBuffer.DataSize());
	FMemoryReaderView Ar(MemView);

	FPackageFileSummary& Summary = CookedHeaderData.Summary;
	{
		TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);
		Ar << Summary;
	}

	Ar.SetFilterEditorOnly((CookedHeaderData.Summary.GetPackageFlags() & EPackageFlags::PKG_FilterEditorOnly) != 0);

	if (Summary.NameCount > 0)
	{
		Ar.Seek(Summary.NameOffset);

		FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);

		CookedHeaderData.SummaryNames.Reserve(Summary.NameCount);
		for (int32 I = 0; I < Summary.NameCount; ++I)
		{
			Ar << NameEntry;
			CookedHeaderData.SummaryNames.Add(NameEntry);
		}
	}

	class FNameReaderProxyArchive
		: public FArchiveProxy
	{
	public:
		using FArchiveProxy::FArchiveProxy;

		FNameReaderProxyArchive(FArchive& InAr, const TArray<FName>& InNameMap)
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

			const FName& MappedName = NameMap[NameIndex];
			Name = FName::CreateFromDisplayId(MappedName.GetDisplayIndex(), Number);

			return *this;
		}

	private:
		const TArray<FName>& NameMap;
	};
	FNameReaderProxyArchive ProxyAr(Ar, CookedHeaderData.SummaryNames);

	if (Summary.ImportCount > 0)
	{
		CookedHeaderData.ObjectImports.Reserve(Summary.ImportCount);
		ProxyAr.Seek(Summary.ImportOffset);
		for (int32 I = 0; I < Summary.ImportCount; ++I)
		{
			ProxyAr << CookedHeaderData.ObjectImports.AddDefaulted_GetRef();
		}
	}

	if (Summary.PreloadDependencyCount > 0)
	{
		CookedHeaderData.PreloadDependencies.Reserve(Summary.PreloadDependencyCount);
		ProxyAr.Seek(Summary.PreloadDependencyOffset);
		for (int32 I = 0; I < Summary.PreloadDependencyCount; ++I)
		{
			ProxyAr << CookedHeaderData.PreloadDependencies.AddDefaulted_GetRef();
		}
	}

	if (Summary.ExportCount > 0)
	{
		CookedHeaderData.ObjectExports.Reserve(Summary.ExportCount);
		ProxyAr.Seek(Summary.ExportOffset);
		for (int32 I = 0; I < Summary.ExportCount; ++I)
		{
			FObjectExport& ObjectExport = CookedHeaderData.ObjectExports.AddDefaulted_GetRef();
			ProxyAr << ObjectExport;
		}
	}
	
	if (Summary.DataResourceOffset > 0)
	{
		ProxyAr.Seek(Summary.DataResourceOffset);
		FObjectDataResource::Serialize(ProxyAr, CookedHeaderData.DataResources);
	}

	return CookedHeaderData;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

FPackageStoreOptimizer::FZenPackageHeaderData FPackageStoreOptimizer::LoadZenPackageHeader(const FIoBuffer& HeaderBuffer, int32 ExportBundleCount, const TArrayView<const FPackageId>& ImportedPackageIds) const
{
	FZenPackageHeaderData ZenPackageHeaderData;

	const uint8* HeaderData = HeaderBuffer.Data();

	FZenPackageSummary& Summary = ZenPackageHeaderData.Summary;
	Summary = *reinterpret_cast<const FZenPackageSummary*>(HeaderData);
	check(HeaderBuffer.DataSize() == Summary.HeaderSize);

	TArrayView<const uint8> HeaderDataView(HeaderData + sizeof(FZenPackageSummary), Summary.HeaderSize - sizeof(FZenPackageSummary));
	FMemoryReaderView HeaderDataReader(HeaderDataView);
	
	if (Summary.bHasVersioningInfo)
	{
		FZenPackageVersioningInfo& VersioningInfo = ZenPackageHeaderData.VersioningInfo.Emplace();
		HeaderDataReader << VersioningInfo;
	}

	TArray<FDisplayNameEntryId>& NameMap = ZenPackageHeaderData.NameMap;
	NameMap = LoadNameBatch(HeaderDataReader);

	const FZenPackageVersioningInfo* VersioningInfo = ZenPackageHeaderData.VersioningInfo.GetPtrOrNull();
	if (VersioningInfo == nullptr || VersioningInfo->PackageVersion >= EUnrealEngineObjectUE5Version::DATA_RESOURCES)
	{
		int64 BulkDataMapSize = 0;
		HeaderDataReader << BulkDataMapSize;
		const uint8* BulkDataMapData = HeaderData + sizeof(FZenPackageSummary) + HeaderDataReader.Tell();
		ZenPackageHeaderData.BulkDataEntries = MakeArrayView(reinterpret_cast<const FBulkDataMapEntry*>(BulkDataMapData), BulkDataMapSize / sizeof(FBulkDataMapEntry));
	}

	ZenPackageHeaderData.ImportedPackageIds = ImportedPackageIds;


	ZenPackageHeaderData.ImportedPublicExportHashes =
		MakeArrayView<const uint64>(
			reinterpret_cast<const uint64*>(HeaderData + Summary.ImportedPublicExportHashesOffset),
			(Summary.ImportMapOffset - Summary.ImportedPublicExportHashesOffset) / sizeof(uint64));

	ZenPackageHeaderData.Imports =
		MakeArrayView<const FPackageObjectIndex>(
			reinterpret_cast<const FPackageObjectIndex*>(HeaderData + Summary.ImportMapOffset),
			(Summary.ExportMapOffset - Summary.ImportMapOffset) / sizeof(FPackageObjectIndex));

	ZenPackageHeaderData.Exports =
		MakeArrayView<const FExportMapEntry>(
			reinterpret_cast<const FExportMapEntry*>(HeaderData + Summary.ExportMapOffset),
			(Summary.ExportBundleEntriesOffset - Summary.ExportMapOffset) / sizeof(FExportMapEntry));

	ZenPackageHeaderData.ExportBundleHeaders =
		MakeArrayView<const FExportBundleHeader>(
			reinterpret_cast<const FExportBundleHeader*>(HeaderData + Summary.GraphDataOffset),
			ExportBundleCount);

	ZenPackageHeaderData.ExportBundleEntries =
		MakeArrayView<const FExportBundleEntry>(
			reinterpret_cast<const FExportBundleEntry*>(HeaderData + Summary.ExportBundleEntriesOffset),
			(Summary.GraphDataOffset - Summary.ExportBundleEntriesOffset) / sizeof(FExportBundleEntry));

	const uint64 ExportBundleHeadersSize = sizeof(FExportBundleHeader) * ExportBundleCount;
	const uint64 ArcsDataOffset = Summary.GraphDataOffset + ExportBundleHeadersSize;
	const uint64 ArcsDataSize = Summary.HeaderSize - ArcsDataOffset;

	FMemoryReaderView ArcsAr(MakeArrayView<const uint8>(HeaderData + ArcsDataOffset, ArcsDataSize));

	int32 InternalArcsCount = 0;
	ArcsAr << InternalArcsCount;

	for (int32 Idx = 0; Idx < InternalArcsCount; ++Idx)
	{
		FPackageStorePackage::FInternalArc& InternalArc = ZenPackageHeaderData.InternalArcs.AddDefaulted_GetRef();
		ArcsAr << InternalArc.FromExportBundleIndex;
		ArcsAr << InternalArc.ToExportBundleIndex;
	}

	for (FPackageId ImportedPackageId : ZenPackageHeaderData.ImportedPackageIds)
	{
		int32 ExternalArcsCount = 0;
		ArcsAr << ExternalArcsCount;

		for (int32 Idx = 0; Idx < ExternalArcsCount; ++Idx)
		{
			FPackageStorePackage::FExternalArc ExternalArc;
			ArcsAr << ExternalArc.FromImportIndex;
			uint8 FromCommandType = 0;
			ArcsAr << FromCommandType;
			ExternalArc.FromCommandType = static_cast<FExportBundleEntry::EExportCommandType>(FromCommandType);
			ArcsAr << ExternalArc.ToExportBundleIndex;

			ZenPackageHeaderData.ExternalArcs.Add(ExternalArc);
		}
	}

	return ZenPackageHeaderData;
}

void FPackageStoreOptimizer::ResolveImport(FPackageStorePackage::FUnresolvedImport* Imports, const FObjectImport* ObjectImports, int32 LocalImportIndex) const
{
	FPackageStorePackage::FUnresolvedImport* Import = Imports + LocalImportIndex;
	if (Import->FullName.Len() == 0)
	{
		Import->FullName.Reserve(256);

		const FObjectImport* ObjectImport = ObjectImports + LocalImportIndex;
		if (ObjectImport->OuterIndex.IsNull())
		{
			FName PackageName = ObjectImport->ObjectName;
			PackageName.AppendString(Import->FullName);
			Import->FullName.ToLowerInline();
			Import->FromPackageName = PackageName;
			Import->FromPackageNameLen = Import->FullName.Len();
			Import->bIsScriptImport = Import->FullName.StartsWith(TEXT("/Script/"));
			Import->bIsImportOfPackage = true;
		}
		else
		{
			const int32 OuterIndex = ObjectImport->OuterIndex.ToImport();
			ResolveImport(Imports, ObjectImports, OuterIndex);
			FPackageStorePackage::FUnresolvedImport* OuterImport = Imports + OuterIndex;
			check(OuterImport->FullName.Len() > 0);
			Import->bIsScriptImport = OuterImport->bIsScriptImport;
			Import->FullName.Append(OuterImport->FullName);
			Import->FullName.AppendChar(TEXT('/'));
			ObjectImport->ObjectName.AppendString(Import->FullName);
			Import->FullName.ToLowerInline();
			Import->bIsImportOptional = ObjectImport->bImportOptional;
			Import->FromPackageName = OuterImport->FromPackageName;
			Import->FromPackageNameLen = OuterImport->FromPackageNameLen;
		}
	}
}

uint64 FPackageStoreOptimizer::GetPublicExportHash(FStringView PackageRelativeExportPath)
{
	check(PackageRelativeExportPath.Len() > 1);
	check(PackageRelativeExportPath[0] == '/');
	return CityHash64(reinterpret_cast<const char*>(PackageRelativeExportPath.GetData() + 1), (PackageRelativeExportPath.Len() - 1) * sizeof(TCHAR));
}

void FPackageStoreOptimizer::ProcessImports(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package, TArray<FPackageStorePackage::FUnresolvedImport>& UnresolvedImports) const
{
	int32 ImportCount = CookedHeaderData.ObjectImports.Num();
	UnresolvedImports.SetNum(ImportCount);
	Package->Imports.SetNum(ImportCount);

	TSet<FName> ImportedPackageNames;
	for (int32 ImportIndex = 0; ImportIndex < ImportCount; ++ImportIndex)
	{
		ResolveImport(UnresolvedImports.GetData(), CookedHeaderData.ObjectImports.GetData(), ImportIndex);
		FPackageStorePackage::FUnresolvedImport& UnresolvedImport = UnresolvedImports[ImportIndex];
		if (!UnresolvedImport.bIsScriptImport )
		{
			if (UnresolvedImport.bIsImportOfPackage)
			{
				ImportedPackageNames.Add(UnresolvedImport.FromPackageName);
			}
		}
	}
	Package->ImportedPackages.Reserve(ImportedPackageNames.Num());
	for (FName ImportedPackageName : ImportedPackageNames)
	{
		Package->ImportedPackages.Emplace(ImportedPackageName);
	}
	Algo::Sort(Package->ImportedPackages);

	for (int32 ImportIndex = 0; ImportIndex < ImportCount; ++ImportIndex)
	{
		FPackageStorePackage::FUnresolvedImport& UnresolvedImport = UnresolvedImports[ImportIndex];
		if (UnresolvedImport.bIsScriptImport)
		{
			FPackageObjectIndex ScriptObjectIndex = FPackageObjectIndex::FromScriptPath(UnresolvedImport.FullName);
			if (!ScriptObjectsMap.Contains(ScriptObjectIndex))
			{
				UE_LOG(LogPackageStoreOptimizer, Warning, TEXT("Package '%s' is referencing missing script import '%s'"), *Package->Name.ToString(), *UnresolvedImport.FullName);
			}
			Package->Imports[ImportIndex] = ScriptObjectIndex;
		}
		else if (!UnresolvedImport.bIsImportOfPackage)
		{
			bool bFoundPackageIndex = false;
			for (uint32 PackageIndex = 0, PackageCount = static_cast<uint32>(Package->ImportedPackages.Num()); PackageIndex < PackageCount; ++PackageIndex)
			{
				if (UnresolvedImport.FromPackageName == Package->ImportedPackages[PackageIndex].Name)
				{
					FStringView PackageRelativeName = FStringView(UnresolvedImport.FullName).RightChop(UnresolvedImport.FromPackageNameLen);
					check(PackageRelativeName.Len());
					FPackageImportReference PackageImportRef(PackageIndex, Package->ImportedPublicExportHashes.Num());
					Package->Imports[ImportIndex] = FPackageObjectIndex::FromPackageImportRef(PackageImportRef);
					uint64 ExportHash = GetPublicExportHash(PackageRelativeName);
					Package->ImportedPublicExportHashes.Add(ExportHash);
					bFoundPackageIndex = true;
					break;
				}
			}
			check(bFoundPackageIndex);
		}
	}
}

void FPackageStoreOptimizer::ProcessImports(const FZenPackageHeaderData& ZenHeaderData, FPackageStorePackage* Package) const
{
	Package->ImportedPackages.Reserve(ZenHeaderData.ImportedPackageIds.Num());
	for (FPackageId ImportedPackageId : ZenHeaderData.ImportedPackageIds)
	{
		Package->ImportedPackages.Emplace(ImportedPackageId);
	}
	Package->ImportedPublicExportHashes = ZenHeaderData.ImportedPublicExportHashes;
	Package->Imports = ZenHeaderData.Imports;
}

void FPackageStoreOptimizer::ResolveExport(
	FPackageStorePackage::FUnresolvedExport* Exports,
	const FObjectExport* ObjectExports,
	const int32 LocalExportIndex,
	const FName& PackageName,
	FPackageStorePackage::FUnresolvedImport* Imports,
	const FObjectImport* ObjectImports) const
{
	FPackageStorePackage::FUnresolvedExport* Export = Exports + LocalExportIndex;
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
			FString* OuterName = nullptr;
			if (ObjectExport->OuterIndex.IsExport())
			{
				int32 OuterExportIndex = ObjectExport->OuterIndex.ToExport();
				ResolveExport(Exports, ObjectExports, OuterExportIndex, PackageName, Imports, ObjectImports);
				OuterName = &Exports[OuterExportIndex].FullName;
			}
			else
			{
				check(Imports && ObjectImports);
				int32 OuterImportIndex = ObjectExport->OuterIndex.ToImport();
				ResolveImport(Imports, ObjectImports, OuterImportIndex);
				OuterName = &Imports[OuterImportIndex].FullName;

			}
			check(OuterName && OuterName->Len() > 0);
			Export->FullName.Append(*OuterName);
			Export->FullName.AppendChar(TEXT('/'));
			ObjectExport->ObjectName.AppendString(Export->FullName);
			Export->FullName.ToLowerInline();
		}
	}
}

void FPackageStoreOptimizer::ProcessExports(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package, FPackageStorePackage::FUnresolvedImport* Imports) const
{
	int32 ExportCount = CookedHeaderData.ObjectExports.Num();

	TArray<FPackageStorePackage::FUnresolvedExport> UnresolvedExports;
	UnresolvedExports.SetNum(ExportCount);
	Package->Exports.SetNum(ExportCount);
	Package->ExportGraphNodes.Reserve(ExportCount * 2);

	auto PackageObjectIdFromPackageIndex =
		[](const TArray<FPackageObjectIndex>& Imports, const FPackageIndex& PackageIndex) -> FPackageObjectIndex
	{
		if (PackageIndex.IsImport())
		{
			return Imports[PackageIndex.ToImport()];
		}
		if (PackageIndex.IsExport())
		{
			return FPackageObjectIndex::FromExportIndex(PackageIndex.ToExport());
		}
		return FPackageObjectIndex();
	};

	FString PackageNameStr = Package->Name.ToString();
	TMap<uint64, const FPackageStorePackage::FUnresolvedExport*> SeenPublicExportHashes;
	for (int32 ExportIndex = 0; ExportIndex < ExportCount; ++ExportIndex)
	{
		const FObjectExport& ObjectExport = CookedHeaderData.ObjectExports[ExportIndex];
		Package->ExportsSerialSize += ObjectExport.SerialSize;

		FPackageStorePackage::FExport& Export = Package->Exports[ExportIndex];
		FPackageStorePackage::FUnresolvedExport& UnresolvedExport = UnresolvedExports[ExportIndex];
		Export.ObjectName = ObjectExport.ObjectName;
		Export.ObjectFlags = ObjectExport.ObjectFlags;
		Export.CookedSerialOffset = ObjectExport.SerialOffset;
		Export.SerialOffset = ObjectExport.SerialOffset - CookedHeaderData.Summary.TotalHeaderSize;
		Export.SerialSize = ObjectExport.SerialSize;
		Export.bNotForClient = ObjectExport.bNotForClient;
		Export.bNotForServer = ObjectExport.bNotForServer;
		Export.bIsPublic = (Export.ObjectFlags & RF_Public) > 0 || ObjectExport.bGeneratePublicHash;
		ResolveExport(UnresolvedExports.GetData(), CookedHeaderData.ObjectExports.GetData(), ExportIndex, Package->Name, Imports, CookedHeaderData.ObjectImports.GetData());
		if (Export.bIsPublic)
		{
			check(UnresolvedExport.FullName.Len() > 0);
			FStringView PackageRelativeName = FStringView(UnresolvedExport.FullName).RightChop(PackageNameStr.Len());
			check(PackageRelativeName.Len());
			Export.PublicExportHash = GetPublicExportHash(PackageRelativeName);
			const FPackageStorePackage::FUnresolvedExport* FindCollidingExport = SeenPublicExportHashes.FindRef(Export.PublicExportHash);
			if (FindCollidingExport)
			{
				UE_LOG(LogPackageStoreOptimizer, Fatal, TEXT("Export hash collision in package \"%s\": \"%s\" and \"%s"), *PackageNameStr, PackageRelativeName.GetData(), *FindCollidingExport->FullName.RightChop(PackageNameStr.Len()));
			}
			SeenPublicExportHashes.Add(Export.PublicExportHash, &UnresolvedExport);
		}

		Export.OuterIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.OuterIndex);
		Export.ClassIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.ClassIndex);
		Export.SuperIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.SuperIndex);
		Export.TemplateIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.TemplateIndex);

		for (uint8 CommandType = 0; CommandType < FExportBundleEntry::ExportCommandType_Count; ++CommandType)
		{
			FPackageStorePackage::FExportGraphNode& Node = Package->ExportGraphNodes.AddDefaulted_GetRef();
			Node.BundleEntry.CommandType = FExportBundleEntry::EExportCommandType(CommandType);
			Node.BundleEntry.LocalExportIndex = ExportIndex;
			Node.bIsPublic = Export.bIsPublic;
			Export.Nodes[CommandType] = &Node;
		}
	}
}

void FPackageStoreOptimizer::ProcessExports(const FZenPackageHeaderData& ZenHeaderData, FPackageStorePackage* Package) const
{
	const int32 ExportCount = ZenHeaderData.Exports.Num();
	Package->Exports.SetNum(ExportCount);
	Package->ExportGraphNodes.Reserve(ExportCount * 2);

	const TArray<FDisplayNameEntryId>& NameMap = ZenHeaderData.NameMap;

	Package->ImportedPublicExportHashes = ZenHeaderData.ImportedPublicExportHashes;
	for (int32 ExportIndex = 0; ExportIndex < ExportCount; ++ExportIndex)
	{
		const FExportMapEntry& ExportEntry = ZenHeaderData.Exports[ExportIndex];
		Package->ExportsSerialSize += ExportEntry.CookedSerialSize;

		FPackageStorePackage::FExport& Export = Package->Exports[ExportIndex];
		Export.ObjectName = ExportEntry.ObjectName.ResolveName(NameMap);
		Export.PublicExportHash = ExportEntry.PublicExportHash;
		Export.OuterIndex = ExportEntry.OuterIndex;
		Export.ClassIndex = ExportEntry.ClassIndex;
		Export.SuperIndex = ExportEntry.SuperIndex;
		Export.TemplateIndex = ExportEntry.TemplateIndex;
		Export.ObjectFlags = ExportEntry.ObjectFlags;
		Export.CookedSerialOffset = ExportEntry.CookedSerialOffset;
		Export.SerialSize = ExportEntry.CookedSerialSize;
		Export.bNotForClient = ExportEntry.FilterFlags == EExportFilterFlags::NotForClient;
		Export.bNotForServer = ExportEntry.FilterFlags == EExportFilterFlags::NotForServer;
		Export.bIsPublic = (ExportEntry.ObjectFlags & RF_Public) > 0;

		for (uint8 CommandType = 0; CommandType < FExportBundleEntry::ExportCommandType_Count; ++CommandType)
		{
			FPackageStorePackage::FExportGraphNode& Node = Package->ExportGraphNodes.AddDefaulted_GetRef();
			Node.BundleEntry.CommandType = FExportBundleEntry::EExportCommandType(CommandType);
			Node.BundleEntry.LocalExportIndex = ExportIndex;
			Node.bIsPublic = Export.bIsPublic;
			Export.Nodes[CommandType] = &Node;
		}

		Package->NameMapBuilder.MarkNameAsReferenced(Export.ObjectName);
	}

	uint64 ExportSerialOffset = 0;
	for (const FExportBundleHeader& ExportBundleHeader : ZenHeaderData.ExportBundleHeaders)
	{
		int32 ExportEntryIndex = ExportBundleHeader.FirstEntryIndex;
		for (uint32 Idx = 0; Idx < ExportBundleHeader.EntryCount; ++Idx)
		{
			const FExportBundleEntry& BundleEntry = ZenHeaderData.ExportBundleEntries[ExportEntryIndex++];
			if (BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				FPackageStorePackage::FExport& Export = Package->Exports[BundleEntry.LocalExportIndex];
				Export.SerialOffset = ExportSerialOffset;
				ExportSerialOffset += Export.SerialSize;
			}
		}
	}
	check(ExportSerialOffset == Package->ExportsSerialSize);
}

TArray<FPackageStorePackage*> FPackageStoreOptimizer::SortPackagesInLoadOrder(const TMap<FPackageId, FPackageStorePackage*>& PackagesMap) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SortPackagesInLoadOrder);
	TArray<FPackageStorePackage*> Packages;
	PackagesMap.GenerateValueArray(Packages);
	Algo::Sort(Packages, [](const FPackageStorePackage* A, const FPackageStorePackage* B)
	{
		return A->Id < B->Id;
	});

	TMap<FPackageStorePackage*, TArray<FPackageStorePackage*>> SortedEdges;
	for (FPackageStorePackage* Package : Packages)
	{
		for (const FPackageStorePackage::FImportedPackageRef& ImportedPackage : Package->ImportedPackages)
		{
			FPackageStorePackage* FindImportedPackage = PackagesMap.FindRef(ImportedPackage.Id);
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
	return Packages;
}

TArray<FPackageStorePackage::FExportBundleGraphNode*> FPackageStoreOptimizer::SortExportBundleGraphNodesInLoadOrder(const TArray<FPackageStorePackage*>& Packages, FExportBundleGraphEdges& Edges) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SortExportBundleGraphNodesInLoadOrder);
	int32 NodeCount = 0;
	for (FPackageStorePackage* Package : Packages)
	{
		NodeCount += Package->ExportGraphNodes.Num();
	}
	for (auto& KV : Edges)
	{
		FPackageStorePackage::FExportBundleGraphNode* ToNode = KV.Value;
		++ToNode->IncomingEdgeCount;
	}

	auto NodeSorter = [](const FPackageStorePackage::FExportBundleGraphNode& A, const FPackageStorePackage::FExportBundleGraphNode& B)
	{
		return A.Index < B.Index;
	};

	for (FPackageStorePackage* Package : Packages)
	{
		for (FPackageStorePackage::FExportBundleGraphNode& Node : Package->ExportBundleGraphNodes)
		{
			if (Node.IncomingEdgeCount == 0)
			{
				Package->NodesWithNoIncomingEdges.HeapPush(&Node, NodeSorter);
			}
		}
	}
	TArray<FPackageStorePackage::FExportBundleGraphNode*> LoadOrder;
	LoadOrder.Reserve(NodeCount);
	while (LoadOrder.Num() < NodeCount)
	{
		bool bMadeProgress = false;
		for (FPackageStorePackage* Package : Packages)
		{
			while (Package->NodesWithNoIncomingEdges.Num())
			{
				FPackageStorePackage::FExportBundleGraphNode* RemovedNode;
				Package->NodesWithNoIncomingEdges.HeapPop(RemovedNode, NodeSorter, false);
				LoadOrder.Add(RemovedNode);
				bMadeProgress = true;
				for (auto EdgeIt = Edges.CreateKeyIterator(RemovedNode); EdgeIt; ++EdgeIt)
				{
					FPackageStorePackage::FExportBundleGraphNode* ToNode = EdgeIt.Value();
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

void FPackageStoreOptimizer::OptimizeExportBundles(const TMap<FPackageId, FPackageStorePackage*>& PackagesMap)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(OptimizeExportBundles);
	TArray<FPackageStorePackage*> Packages = SortPackagesInLoadOrder(PackagesMap);
	for (FPackageStorePackage* Package : Packages)
	{
		Package->ExportBundleGraphNodes.Reserve(Package->GraphData.ExportBundles.Num());
		for (FPackageStorePackage::FExportBundle& ExportBundle : Package->GraphData.ExportBundles)
		{
			FPackageStorePackage::FExportBundleGraphNode& Node = Package->ExportBundleGraphNodes.AddDefaulted_GetRef();
			Node.Package = Package;
			Node.Index = Package->ExportBundleGraphNodes.Num() - 1;
			Node.ExportGraphNodes.Reserve(ExportBundle.Entries.Num());
			for (FExportBundleEntry& ExportBundleEntry : ExportBundle.Entries)
			{
				FPackageStorePackage::FExport& Export = Package->Exports[ExportBundleEntry.LocalExportIndex];
				Node.ExportGraphNodes.Add(Export.Nodes[ExportBundleEntry.CommandType]);
			}
		}
		Package->GraphData.ExportBundles.Empty();
	}

	FExportBundleGraphEdges Edges;
	TSet<FPackageStorePackage::FExportBundleGraphNode*> Dependencies;
	for (FPackageStorePackage* Package : Packages)
	{
		for (FPackageStorePackage::FExportBundleGraphNode& ExportBundleGraphNode : Package->ExportBundleGraphNodes)
		{
			for (FPackageStorePackage::FExportGraphNode* ExportGraphNode : ExportBundleGraphNode.ExportGraphNodes)
			{
				check(ExportGraphNode->ExportBundleIndex >= 0);
				for (FPackageStorePackage::FExportGraphNode* InternalDependency : ExportGraphNode->InternalDependencies)
				{
					check(InternalDependency->ExportBundleIndex >= 0);
					FPackageStorePackage::FExportBundleGraphNode* FromNode = &Package->ExportBundleGraphNodes[InternalDependency->ExportBundleIndex];
					Dependencies.Add(FromNode);
				}
				for (FPackageStorePackage::FExternalDependency& ExternalDependency : ExportGraphNode->ExternalDependencies)
				{
					FPackageObjectIndex FromImport = Package->Imports[ExternalDependency.ImportIndex];
					check(FromImport.IsPackageImport());
					FPackageImportReference FromPackageImportRef = FromImport.ToPackageImportRef();
					FPackageId FromPackageId = Package->ImportedPackages[FromPackageImportRef.GetImportedPackageIndex()].Id;
					uint64 FromPublicExportHash = Package->ImportedPublicExportHashes[FromPackageImportRef.GetImportedPublicExportHashIndex()];
					FPackageStorePackage* FindFromPackage = PackagesMap.FindRef(FromPackageId);
					if (FindFromPackage)
					{
						bool bFoundExport = false;
						for (int32 ExportIndex = 0; ExportIndex < FindFromPackage->Exports.Num(); ++ExportIndex)
						{
							FPackageStorePackage::FExport& FromExport = FindFromPackage->Exports[ExportIndex];
							if (FromExport.PublicExportHash == FromPublicExportHash)
							{
								FPackageStorePackage::FExportGraphNode* FromExportGraphNode = FromExport.Nodes[ExternalDependency.ExportBundleCommandType];
								check(FromExportGraphNode->ExportBundleIndex >= 0);
								FPackageStorePackage::FExportBundleGraphNode* FromNode = &FindFromPackage->ExportBundleGraphNodes[FromExportGraphNode->ExportBundleIndex];
								Dependencies.Add(FromNode);
								bFoundExport = true;
								break;
							}
						}
					}
				}
			}
			for (FPackageStorePackage::FExportBundleGraphNode* FromNode : Dependencies)
			{
				Edges.Add(FromNode, &ExportBundleGraphNode);
			}
			Dependencies.Reset();
		}
	}

	TArray<FPackageStorePackage::FExportBundleGraphNode*> LoadOrder = SortExportBundleGraphNodesInLoadOrder(Packages, Edges);

	FPackageStorePackage* PreviousPackage = nullptr;
	for (const FPackageStorePackage::FExportBundleGraphNode* Node : LoadOrder)
	{
		check(Node);
		FPackageStorePackage* Package = Node->Package;
		check(Package);
		if (Package->CurrentBundle == nullptr || Package != PreviousPackage)
		{
			Package->CurrentBundle = &Package->GraphData.ExportBundles.AddDefaulted_GetRef();
		}
		for (FPackageStorePackage::FExportGraphNode* ExportGraphNode : Node->ExportGraphNodes)
		{
			Package->CurrentBundle->Entries.Add(ExportGraphNode->BundleEntry);
			ExportGraphNode->ExportBundleIndex = Package->GraphData.ExportBundles.Num() - 1;
		}
		PreviousPackage = Package;
	}
}

void FPackageStoreOptimizer::ProcessPreloadDependencies(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPreloadDependencies);

	auto AddInternalDependency = [](FPackageStorePackage* Package, int32 FromExportIndex, FExportBundleEntry::EExportCommandType FromExportBundleCommandType, FPackageStorePackage::FExportGraphNode* ToNode)
	{
		FPackageStorePackage::FExport& FromExport = Package->Exports[FromExportIndex];
		FPackageStorePackage::FExportGraphNode* FromNode = FromExport.Nodes[FromExportBundleCommandType];
		ToNode->InternalDependencies.Add(FromNode);
	};

	auto AddExternalDependency = [](FPackageStorePackage* Package, int32 FromImportIndex, FExportBundleEntry::EExportCommandType FromExportBundleCommandType, FPackageStorePackage::FExportGraphNode* ToNode)
	{
		FPackageObjectIndex FromImport = Package->Imports[FromImportIndex];
		if (FromImport.IsScriptImport())
		{
			return;
		}

		FPackageStorePackage::FExternalDependency& ExternalDependency = ToNode->ExternalDependencies.AddDefaulted_GetRef();
		ExternalDependency.ImportIndex = FromImportIndex;
		ExternalDependency.ExportBundleCommandType = FromExportBundleCommandType;
	};

	for (int32 ExportIndex = 0; ExportIndex < Package->Exports.Num(); ++ExportIndex)
	{
		FPackageStorePackage::FExport& Export = Package->Exports[ExportIndex];
		const FObjectExport& ObjectExport = CookedHeaderData.ObjectExports[ExportIndex];

		AddInternalDependency(Package, ExportIndex, FExportBundleEntry::ExportCommandType_Create, Export.Nodes[FExportBundleEntry::ExportCommandType_Serialize]);

		if (ObjectExport.FirstExportDependency >= 0)
		{
			int32 RunningIndex = ObjectExport.FirstExportDependency;
			for (int32 Index = ObjectExport.SerializationBeforeSerializationDependencies; Index > 0; Index--)
			{
				FPackageIndex Dep = CookedHeaderData.PreloadDependencies[RunningIndex++];
				if (Dep.IsExport())
				{
					AddInternalDependency(Package, Dep.ToExport(), FExportBundleEntry::ExportCommandType_Serialize, Export.Nodes[FExportBundleEntry::ExportCommandType_Serialize]);
				}
				else
				{
					AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Serialize, Export.Nodes[FExportBundleEntry::ExportCommandType_Serialize]);
				}
			}

			for (int32 Index = ObjectExport.CreateBeforeSerializationDependencies; Index > 0; Index--)
			{
				FPackageIndex Dep = CookedHeaderData.PreloadDependencies[RunningIndex++];
				if (Dep.IsExport())
				{
					AddInternalDependency(Package, Dep.ToExport(), FExportBundleEntry::ExportCommandType_Create, Export.Nodes[FExportBundleEntry::ExportCommandType_Serialize]);
				}
				else
				{
					AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Create, Export.Nodes[FExportBundleEntry::ExportCommandType_Serialize]);
				}
			}

			for (int32 Index = ObjectExport.SerializationBeforeCreateDependencies; Index > 0; Index--)
			{
				FPackageIndex Dep = CookedHeaderData.PreloadDependencies[RunningIndex++];
				if (Dep.IsExport())
				{
					AddInternalDependency(Package, Dep.ToExport(), FExportBundleEntry::ExportCommandType_Serialize, Export.Nodes[FExportBundleEntry::ExportCommandType_Create]);
				}
				else
				{
					AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Serialize, Export.Nodes[FExportBundleEntry::ExportCommandType_Create]);
				}
			}

			for (int32 Index = ObjectExport.CreateBeforeCreateDependencies; Index > 0; Index--)
			{
				FPackageIndex Dep = CookedHeaderData.PreloadDependencies[RunningIndex++];
				if (Dep.IsExport())
				{
					AddInternalDependency(Package, Dep.ToExport(), FExportBundleEntry::ExportCommandType_Create, Export.Nodes[FExportBundleEntry::ExportCommandType_Create]);
				}
				else
				{
					AddExternalDependency(Package, Dep.ToImport(), FExportBundleEntry::ExportCommandType_Create, Export.Nodes[FExportBundleEntry::ExportCommandType_Create]);
				}
			}
		}
	}
}

void FPackageStoreOptimizer::ProcessDataResources(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package) const
{
	for (const FObjectDataResource& DataResource : CookedHeaderData.DataResources)
	{
		FBulkDataMapEntry& Entry = Package->BulkDataEntries.AddDefaulted_GetRef();
		checkf(DataResource.SerialSize == DataResource.RawSize, TEXT("Compressed bulk data is not supported in cooked builds"));

		Entry.SerialOffset = DataResource.SerialOffset;
		Entry.DuplicateSerialOffset = DataResource.DuplicateSerialOffset;
		Entry.SerialSize = DataResource.SerialSize;
		Entry.Flags = DataResource.LegacyBulkDataFlags;
	}
}

void FPackageStoreOptimizer::ProcessPreloadDependencies(const FZenPackageHeaderData& ZenHeaderData, FPackageStorePackage* Package) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPreloadDependencies);

	for (const FPackageStorePackage::FInternalArc& InternalArc : ZenHeaderData.InternalArcs)
	{
		const FExportBundleHeader& FromExportBundle = ZenHeaderData.ExportBundleHeaders[InternalArc.FromExportBundleIndex];
		const FExportBundleHeader& ToExportBundle = ZenHeaderData.ExportBundleHeaders[InternalArc.ToExportBundleIndex];

		uint32 FromBundleEntryIndex = FromExportBundle.FirstEntryIndex;
		const uint32 LastFromBundleEntryIndex = FromBundleEntryIndex + FromExportBundle.EntryCount;
		while (FromBundleEntryIndex < LastFromBundleEntryIndex)
		{
			const FExportBundleEntry& FromBundleEntry = ZenHeaderData.ExportBundleEntries[FromBundleEntryIndex++];
			FPackageStorePackage::FExport& FromExport = Package->Exports[FromBundleEntry.LocalExportIndex];
			FPackageStorePackage::FExportGraphNode* FromNode = FromExport.Nodes[FromBundleEntry.CommandType];

			uint32 ToBundleEntryIndex = ToExportBundle.FirstEntryIndex;
			const uint32 LastToBundleEntryIndex = ToBundleEntryIndex + ToExportBundle.EntryCount;
			while (ToBundleEntryIndex < LastToBundleEntryIndex)
			{
				const FExportBundleEntry& ToBundleEntry = ZenHeaderData.ExportBundleEntries[ToBundleEntryIndex++];
				FPackageStorePackage::FExport& ToExport = Package->Exports[ToBundleEntry.LocalExportIndex];
				FPackageStorePackage::FExportGraphNode* ToNode = ToExport.Nodes[ToBundleEntry.CommandType];
				ToNode->InternalDependencies.Add(FromNode);
			}
		}
	}

	for (const FPackageStorePackage::FExternalArc& ExternalArc : ZenHeaderData.ExternalArcs)
	{
		const FExportBundleHeader& ToExportBundle = ZenHeaderData.ExportBundleHeaders[ExternalArc.ToExportBundleIndex];
		uint32 ToBundleEntryIndex = ToExportBundle.FirstEntryIndex;
		const uint32 LastToBundleEntryIndex = ToBundleEntryIndex + ToExportBundle.EntryCount;
		while (ToBundleEntryIndex < LastToBundleEntryIndex)
		{
			const FExportBundleEntry& ToBundleEntry = ZenHeaderData.ExportBundleEntries[ToBundleEntryIndex++];
			FPackageStorePackage::FExport& ToExport = Package->Exports[ToBundleEntry.LocalExportIndex];
			FPackageStorePackage::FExportGraphNode* ToNode = ToExport.Nodes[ToBundleEntry.CommandType];
			FPackageStorePackage::FExternalDependency& ExternalDependency = ToNode->ExternalDependencies.AddDefaulted_GetRef();
			ExternalDependency.ImportIndex = ExternalArc.FromImportIndex;
			ExternalDependency.ExportBundleCommandType = ExternalArc.FromCommandType;
		}
	}
}

TArray<FPackageStorePackage::FExportGraphNode*> FPackageStoreOptimizer::SortExportGraphNodesInLoadOrder(FPackageStorePackage* Package, FExportGraphEdges& Edges) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SortExportGraphNodesInLoadOrder);
	int32 NodeCount = Package->ExportGraphNodes.Num();
	for (auto& KV : Edges)
	{
		FPackageStorePackage::FExportGraphNode* ToNode = KV.Value;
		++ToNode->IncomingEdgeCount;
	}

	auto NodeSorter = [](const FPackageStorePackage::FExportGraphNode& A, const FPackageStorePackage::FExportGraphNode& B)
	{
		if (A.bIsPublic != B.bIsPublic)
		{
			return A.bIsPublic;
		}
		if (A.BundleEntry.CommandType != B.BundleEntry.CommandType)
		{
			return A.BundleEntry.CommandType < B.BundleEntry.CommandType;
		}
		return A.BundleEntry.LocalExportIndex < B.BundleEntry.LocalExportIndex;
	};

	TArray<FPackageStorePackage::FExportGraphNode*> NodesWithNoIncomingEdges;
	NodesWithNoIncomingEdges.Reserve(NodeCount);
	for (FPackageStorePackage::FExportGraphNode& Node : Package->ExportGraphNodes)
	{
		if (Node.IncomingEdgeCount == 0)
		{
			NodesWithNoIncomingEdges.HeapPush(&Node, NodeSorter);
		}
	}

	TArray<FPackageStorePackage::FExportGraphNode*> LoadOrder;
	LoadOrder.Reserve(NodeCount);
	while (NodesWithNoIncomingEdges.Num())
	{
		FPackageStorePackage::FExportGraphNode* RemovedNode;
		NodesWithNoIncomingEdges.HeapPop(RemovedNode, NodeSorter, false);
		LoadOrder.Add(RemovedNode);
		for (auto EdgeIt = Edges.CreateKeyIterator(RemovedNode); EdgeIt; ++EdgeIt)
		{
			FPackageStorePackage::FExportGraphNode* ToNode = EdgeIt.Value();
			check(ToNode->IncomingEdgeCount > 0);
			if (--ToNode->IncomingEdgeCount == 0)
			{
				NodesWithNoIncomingEdges.HeapPush(ToNode, NodeSorter);
			}
			EdgeIt.RemoveCurrent();
		}
	}
	check(LoadOrder.Num() == NodeCount);
	return LoadOrder;
}

void FPackageStoreOptimizer::CreateExportBundles(FPackageStorePackage* Package) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateExportBundles);
	FExportGraphEdges Edges;
	for (FPackageStorePackage::FExportGraphNode& ExportGraphNode : Package->ExportGraphNodes)
	{
		for (FPackageStorePackage::FExportGraphNode* InternalDependency : ExportGraphNode.InternalDependencies)
		{
			Edges.Add(InternalDependency, &ExportGraphNode);
		}
	}
	TArray<FPackageStorePackage::FExportGraphNode*> LoadOrder = SortExportGraphNodesInLoadOrder(Package, Edges);
	FPackageStorePackage::FExportBundle* CurrentBundle = nullptr;
	for (FPackageStorePackage::FExportGraphNode* Node : LoadOrder)
	{
		if (!CurrentBundle)
		{
			Package->CurrentBundle = &Package->GraphData.ExportBundles.AddDefaulted_GetRef();
		}
		Package->CurrentBundle->Entries.Add(Node->BundleEntry);
		Node->ExportBundleIndex = Package->GraphData.ExportBundles.Num() - 1;
		if (Node->bIsPublic)
		{
			CurrentBundle = nullptr;
		}
	}
}

void FPackageStoreOptimizer::SerializeGraphData(const TArray<FPackageStorePackage::FImportedPackageRef>& ImportedPackages, FPackageStorePackage::FGraphData& GraphData, FBufferWriter& GraphArchive) const
{
	uint32 ExportBundleEntryIndex = 0;
	for (const FPackageStorePackage::FExportBundle& ExportBundle : GraphData.ExportBundles)
	{
		const uint32 EntryCount = ExportBundle.Entries.Num();
		FExportBundleHeader ExportBundleHeader{ ExportBundle.SerialOffset, ExportBundleEntryIndex, EntryCount };
		GraphArchive << ExportBundleHeader;
		ExportBundleEntryIndex += EntryCount;
	}
	Algo::Sort(GraphData.InternalArcs, [](const FPackageStorePackage::FInternalArc& A, const FPackageStorePackage::FInternalArc& B)
	{
		if (A.ToExportBundleIndex == B.ToExportBundleIndex)
		{
			return A.FromExportBundleIndex < B.FromExportBundleIndex;
		}
		return A.ToExportBundleIndex < B.ToExportBundleIndex;
	});
	int32 InternalArcsCount = GraphData.InternalArcs.Num();
	GraphArchive << InternalArcsCount;
	for (FPackageStorePackage::FInternalArc& InternalArc : GraphData.InternalArcs)
	{
		GraphArchive << InternalArc.FromExportBundleIndex;
		GraphArchive << InternalArc.ToExportBundleIndex;
	}

	for (const FPackageStorePackage::FImportedPackageRef& ImportedPackage : ImportedPackages)
	{
		TArray<FPackageStorePackage::FExternalArc>* FindArcsFromImportedPackage = GraphData.ExternalArcs.Find(ImportedPackage.Id);
		if (!FindArcsFromImportedPackage)
		{
			int32 ExternalArcCount = 0;
			GraphArchive << ExternalArcCount;
		}
		else
		{
			Algo::Sort(*FindArcsFromImportedPackage, [](const FPackageStorePackage::FExternalArc& A, const FPackageStorePackage::FExternalArc& B)
			{
				if (A.ToExportBundleIndex == B.ToExportBundleIndex)
				{
					if (A.FromImportIndex == B.FromImportIndex)
					{
						return A.FromCommandType < B.FromCommandType;
					}
					return A.FromImportIndex < B.FromImportIndex;
				}
				return A.ToExportBundleIndex < B.ToExportBundleIndex;
			});
			int32 ExternalArcCount = FindArcsFromImportedPackage->Num();
			GraphArchive << ExternalArcCount;
			for (FPackageStorePackage::FExternalArc& Arc : *FindArcsFromImportedPackage)
			{
				GraphArchive << Arc.FromImportIndex;
				uint8 FromCommandType = uint8(Arc.FromCommandType);
				GraphArchive << FromCommandType;
				GraphArchive << Arc.ToExportBundleIndex;
			}
		}
	}
}

void FPackageStoreOptimizer::FinalizePackageHeader(FPackageStorePackage* Package) const
{
	FBufferWriter ImportedPublicExportHashesArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (uint64 ImportedPublicExportHash : Package->ImportedPublicExportHashes)
	{
		ImportedPublicExportHashesArchive << ImportedPublicExportHash;
	}
	uint64 ImportedPublicExportHashesSize = ImportedPublicExportHashesArchive.Tell();

	FBufferWriter ImportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (FPackageObjectIndex Import : Package->Imports)
	{
		ImportMapArchive << Import;
	}
	uint64 ImportMapSize = ImportMapArchive.Tell();

	FBufferWriter ExportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (const FPackageStorePackage::FExport& Export : Package->Exports)
	{
		FExportMapEntry ExportMapEntry;
		ExportMapEntry.CookedSerialOffset = Export.CookedSerialOffset;
		ExportMapEntry.CookedSerialSize = Export.SerialSize;
		Package->NameMapBuilder.MarkNameAsReferenced(Export.ObjectName);
		ExportMapEntry.ObjectName = Package->NameMapBuilder.MapName(Export.ObjectName);
		ExportMapEntry.PublicExportHash = Export.PublicExportHash;
		ExportMapEntry.OuterIndex = Export.OuterIndex;
		ExportMapEntry.ClassIndex = Export.ClassIndex;
		ExportMapEntry.SuperIndex = Export.SuperIndex;
		ExportMapEntry.TemplateIndex = Export.TemplateIndex;
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
	uint64 ExportMapSize = ExportMapArchive.Tell();

	FBufferWriter ExportBundleEntriesArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (const FPackageStorePackage::FExportBundle& ExportBundle : Package->GraphData.ExportBundles)
	{
		for (FExportBundleEntry BundleEntry : ExportBundle.Entries)
		{
			ExportBundleEntriesArchive << BundleEntry;
		}
	}
	uint64 ExportBundleEntriesSize = ExportBundleEntriesArchive.Tell();

	FBufferWriter GraphArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	SerializeGraphData(Package->ImportedPackages, Package->GraphData, GraphArchive);
	uint64 GraphDataSize = GraphArchive.Tell();

	Package->NameMapBuilder.MarkNameAsReferenced(Package->Name);
	FMappedName MappedPackageName = Package->NameMapBuilder.MapName(Package->Name);

	FBufferWriter NameMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	SaveNameBatch(Package->NameMapBuilder.GetNameMap(), NameMapArchive);
	uint64 NameMapSize = NameMapArchive.Tell();

	FBufferWriter VersioningInfoArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	if (Package->VersioningInfo.IsSet())
	{
		VersioningInfoArchive << Package->VersioningInfo.GetValue();
	}
	uint64 VersioningInfoSize = VersioningInfoArchive.Tell();

	
	FBufferWriter BulkDataMapAr(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
	for (FBulkDataMapEntry& Entry : Package->BulkDataEntries)
	{
		BulkDataMapAr << Entry;
	}
	uint64 BulkDataMapSize = BulkDataMapAr.Tell();

	Package->HeaderSize =
		sizeof(FZenPackageSummary)
		+ VersioningInfoSize
		+ NameMapSize
		+ ImportedPublicExportHashesSize
		+ ImportMapSize
		+ ExportMapSize
		+ ExportBundleEntriesSize
		+ GraphDataSize
		+ BulkDataMapSize + sizeof(int64);

	Package->HeaderBuffer = FIoBuffer(Package->HeaderSize);
	uint8* HeaderData = Package->HeaderBuffer.Data();
	FMemory::Memzero(HeaderData, Package->HeaderSize);
	FZenPackageSummary* PackageSummary = reinterpret_cast<FZenPackageSummary*>(HeaderData);
	PackageSummary->HeaderSize = Package->HeaderSize;
	PackageSummary->Name = MappedPackageName;
	PackageSummary->PackageFlags = Package->PackageFlags;
	PackageSummary->CookedHeaderSize = Package->CookedHeaderSize;
	FBufferWriter HeaderArchive(HeaderData, Package->HeaderSize);
	HeaderArchive.Seek(sizeof(FZenPackageSummary));

	if (Package->VersioningInfo.IsSet())
	{
		PackageSummary->bHasVersioningInfo = 1;
		HeaderArchive.Serialize(VersioningInfoArchive.GetWriterData(), VersioningInfoArchive.Tell());
	}
	else
	{
		PackageSummary->bHasVersioningInfo = 0;
	}

	HeaderArchive.Serialize(NameMapArchive.GetWriterData(), NameMapArchive.Tell());

	HeaderArchive << BulkDataMapSize;
	HeaderArchive.Serialize(BulkDataMapAr.GetWriterData(), BulkDataMapSize);

	PackageSummary->ImportedPublicExportHashesOffset = HeaderArchive.Tell();
	HeaderArchive.Serialize(ImportedPublicExportHashesArchive.GetWriterData(), ImportedPublicExportHashesArchive.Tell());
	PackageSummary->ImportMapOffset = HeaderArchive.Tell();
	HeaderArchive.Serialize(ImportMapArchive.GetWriterData(), ImportMapArchive.Tell());
	PackageSummary->ExportMapOffset = HeaderArchive.Tell();
	HeaderArchive.Serialize(ExportMapArchive.GetWriterData(), ExportMapArchive.Tell());
	PackageSummary->ExportBundleEntriesOffset = HeaderArchive.Tell();
	HeaderArchive.Serialize(ExportBundleEntriesArchive.GetWriterData(), ExportBundleEntriesArchive.Tell());
	PackageSummary->GraphDataOffset = HeaderArchive.Tell();
	HeaderArchive.Serialize(GraphArchive.GetWriterData(), GraphArchive.Tell());
	check(HeaderArchive.Tell() == PackageSummary->HeaderSize)
}

void FPackageStoreOptimizer::FinalizePackage(FPackageStorePackage* Package) const
{
	for (FPackageStorePackage::FExportGraphNode& Node : Package->ExportGraphNodes)
	{
		check(Node.ExportBundleIndex >= 0);
		TSet<FPackageStorePackage::FInternalArc> InternalArcsSet;
		for (FPackageStorePackage::FExportGraphNode* InternalDependency : Node.InternalDependencies)
		{
			FPackageStorePackage::FInternalArc Arc;
			check(InternalDependency->ExportBundleIndex >= 0);
			Arc.FromExportBundleIndex = InternalDependency->ExportBundleIndex;
			Arc.ToExportBundleIndex = Node.ExportBundleIndex;
			if (Arc.FromExportBundleIndex != Arc.ToExportBundleIndex)
			{
				bool bIsAlreadyInSet = false;
				InternalArcsSet.Add(Arc, &bIsAlreadyInSet);
				if (!bIsAlreadyInSet)
				{
					Package->GraphData.InternalArcs.Add(Arc);
				}
			}
		}

		for (FPackageStorePackage::FExternalDependency& ExternalDependency : Node.ExternalDependencies)
		{
			check(ExternalDependency.ImportIndex >= 0);
			const FPackageObjectIndex& Import = Package->Imports[ExternalDependency.ImportIndex];
			check(Import.IsPackageImport());
			FPackageImportReference PackageImportRef = Import.ToPackageImportRef();
			TArray<FPackageStorePackage::FExternalArc>& ArcsFromImportedPackage = Package->GraphData.ExternalArcs.FindOrAdd(Package->ImportedPackages[PackageImportRef.GetImportedPackageIndex()].Id);
			FPackageStorePackage::FExternalArc Arc;
			Arc.FromImportIndex = ExternalDependency.ImportIndex;
			Arc.FromCommandType = ExternalDependency.ExportBundleCommandType;
			Arc.ToExportBundleIndex = Node.ExportBundleIndex;
			if (!ArcsFromImportedPackage.Contains(Arc))
			{
				ArcsFromImportedPackage.Add(Arc);
			}
		}
	}
	uint64 SerialOffset = 0;
	for (FPackageStorePackage::FExportBundle& ExportBundle : Package->GraphData.ExportBundles)
	{
		ExportBundle.SerialOffset = SerialOffset;
		for (const FExportBundleEntry& BundleEntry : ExportBundle.Entries)
		{
			if (BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FPackageStorePackage::FExport& Export = Package->Exports[BundleEntry.LocalExportIndex];
				SerialOffset += Export.SerialSize;
			}
		}
	}

	FinalizePackageHeader(Package);
}

FIoBuffer FPackageStoreOptimizer::CreatePackageBuffer(const FPackageStorePackage* Package, const FIoBuffer& CookedExportsDataBuffer, TArray<FFileRegion>* InOutFileRegions) const
{
	check(Package->HeaderBuffer.DataSize() > 0);
	check(Package->HeaderBuffer.DataSize() == Package->HeaderSize);
	const uint64 BundleBufferSize = Package->HeaderSize + Package->ExportsSerialSize;
	FIoBuffer BundleBuffer(BundleBufferSize);
	FMemory::Memcpy(BundleBuffer.Data(), Package->HeaderBuffer.Data(), Package->HeaderSize);
	uint64 BundleBufferOffset = Package->HeaderSize;

	TArray<FFileRegion> OutputRegions;

	for (const FPackageStorePackage::FExportBundle& ExportBundle : Package->GraphData.ExportBundles)
	{
		for (const FExportBundleEntry& BundleEntry : ExportBundle.Entries)
		{
			if (BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FPackageStorePackage::FExport& Export = Package->Exports[BundleEntry.LocalExportIndex];
				check(Export.SerialOffset + Export.SerialSize <= CookedExportsDataBuffer.DataSize());
				FMemory::Memcpy(BundleBuffer.Data() + BundleBufferOffset, CookedExportsDataBuffer.Data() + Export.SerialOffset, Export.SerialSize);

				if (InOutFileRegions)
				{
					// Find overlapping regions and adjust them to match the new offset of the export data
					for (const FFileRegion& Region : *InOutFileRegions)
					{
						if (Export.SerialOffset <= Region.Offset && Region.Offset + Region.Length <= Export.SerialOffset + Export.SerialSize)
						{
							FFileRegion NewRegion = Region;
							NewRegion.Offset -= Export.SerialOffset;
							NewRegion.Offset += BundleBufferOffset;
							OutputRegions.Add(NewRegion);
						}
					}
				}
				BundleBufferOffset += Export.SerialSize;
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

void FPackageStoreOptimizer::FindScriptObjectsRecursive(FPackageObjectIndex OuterIndex, UObject* Object)
{
	if (!Object->HasAllFlags(RF_Public))
	{
		UE_LOG(LogPackageStoreOptimizer, Verbose, TEXT("Skipping script object: %s (!RF_Public)"), *Object->GetFullName());
		return;
	}

	FString OuterFullName;
	FPackageObjectIndex OuterCDOClassIndex;
	{
		const FScriptObjectData* Outer = ScriptObjectsMap.Find(OuterIndex);
		check(Outer);
		OuterFullName = Outer->FullName;
		OuterCDOClassIndex = Outer->CDOClassIndex;
	}

	FName ObjectName = Object->GetFName();

	FString TempFullName = OuterFullName;
	TempFullName.AppendChar(TEXT('/'));
	ObjectName.AppendString(TempFullName);

	TempFullName.ToLowerInline();
	FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(TempFullName);

	FScriptObjectData* ScriptImport = ScriptObjectsMap.Find(GlobalImportIndex);
	if (ScriptImport)
	{
		UE_LOG(LogPackageStoreOptimizer, Fatal, TEXT("Import name hash collision \"%s\" and \"%s"), *TempFullName, *ScriptImport->FullName);
	}

	FPackageObjectIndex CDOClassIndex = OuterCDOClassIndex;
	if (CDOClassIndex.IsNull())
	{
		TCHAR NameBuffer[FName::StringBufferSize];
		uint32 Len = ObjectName.ToString(NameBuffer);
		if (FCString::Strncmp(NameBuffer, TEXT("Default__"), 9) == 0)
		{
			FString CDOClassFullName = OuterFullName;
			CDOClassFullName.AppendChar(TEXT('/'));
			CDOClassFullName.AppendChars(NameBuffer + 9, Len - 9);
			CDOClassFullName.ToLowerInline();

			CDOClassIndex = FPackageObjectIndex::FromScriptPath(CDOClassFullName);
		}
	}

	ScriptImport = &ScriptObjectsMap.Add(GlobalImportIndex);
	ScriptImport->GlobalIndex = GlobalImportIndex;
	ScriptImport->FullName = MoveTemp(TempFullName);
	ScriptImport->OuterIndex = OuterIndex;
	ScriptImport->ObjectName = ObjectName;
	ScriptImport->CDOClassIndex = CDOClassIndex;

	TArray<UObject*> InnerObjects;
	GetObjectsWithOuter(Object, InnerObjects, /*bIncludeNestedObjects*/false);
	for (UObject* InnerObject : InnerObjects)
	{
		FindScriptObjectsRecursive(GlobalImportIndex, InnerObject);
	}
};

void FPackageStoreOptimizer::FindScriptObjects()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindScriptObjects);
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
			FindScriptObjectsRecursive(GlobalImportIndex, InnerObject);
		}
	}

	TotalScriptObjectCount = ScriptObjectsMap.Num();
}

FIoBuffer FPackageStoreOptimizer::CreateScriptObjectsBuffer() const
{
	TArray<FScriptObjectData> ScriptObjectsAsArray;
	ScriptObjectsMap.GenerateValueArray(ScriptObjectsAsArray);
	Algo::Sort(ScriptObjectsAsArray, [](const FScriptObjectData& A, const FScriptObjectData& B)
	{
		return A.FullName < B.FullName;
	});
	
	TArray<FScriptObjectEntry> ScriptObjectEntries;
	ScriptObjectEntries.Reserve(ScriptObjectsAsArray.Num());
	FPackageStoreNameMapBuilder NameMapBuilder;
	NameMapBuilder.SetNameMapType(FMappedName::EType::Global);
	for (const FScriptObjectData& ImportData : ScriptObjectsAsArray)
	{
		NameMapBuilder.MarkNameAsReferenced(ImportData.ObjectName);
		FScriptObjectEntry& Entry = ScriptObjectEntries.AddDefaulted_GetRef();
		Entry.Mapped = NameMapBuilder.MapName(ImportData.ObjectName);
		Entry.GlobalIndex = ImportData.GlobalIndex;
		Entry.OuterIndex = ImportData.OuterIndex;
		Entry.CDOClassIndex = ImportData.CDOClassIndex;
	}

	FLargeMemoryWriter ScriptObjectsArchive(0, true);
	SaveNameBatch(NameMapBuilder.GetNameMap(), ScriptObjectsArchive);
	int32 NumScriptObjects = ScriptObjectEntries.Num();
	ScriptObjectsArchive << NumScriptObjects;
	for (FScriptObjectEntry& Entry : ScriptObjectEntries)
	{
		ScriptObjectsArchive << Entry;
	}

	int64 DataSize = ScriptObjectsArchive.TotalSize();
	return FIoBuffer(FIoBuffer::AssumeOwnership, ScriptObjectsArchive.ReleaseOwnership(), DataSize);
}

void FPackageStoreOptimizer::LoadScriptObjectsBuffer(const FIoBuffer& ScriptObjectsBuffer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LoadScriptObjectsBuffer);
	FLargeMemoryReader ScriptObjectsArchive(ScriptObjectsBuffer.Data(), ScriptObjectsBuffer.DataSize());
	TArray<FDisplayNameEntryId> NameMap = LoadNameBatch(ScriptObjectsArchive);
	int32 NumScriptObjects;
	ScriptObjectsArchive << NumScriptObjects;
	for (int32 Index = 0; Index < NumScriptObjects; ++Index)
	{
		FScriptObjectEntry Entry{};
		ScriptObjectsArchive << Entry;
		FScriptObjectData& ImportData = ScriptObjectsMap.Add(Entry.GlobalIndex);
		FMappedName MappedName = Entry.Mapped;
		ImportData.ObjectName = NameMap[MappedName.GetIndex()].ToName(MappedName.GetNumber());
		ImportData.GlobalIndex = Entry.GlobalIndex;
		ImportData.OuterIndex = Entry.OuterIndex;
		ImportData.CDOClassIndex = Entry.CDOClassIndex;
	}
}

FPackageStoreEntryResource FPackageStoreOptimizer::CreatePackageStoreEntry(const FPackageStorePackage* Package, const FPackageStorePackage* OptionalSegmentPackage) const
{
	FPackageStoreEntryResource Result;
	Result.Flags = EPackageStoreEntryFlags::None;

	if (OptionalSegmentPackage)
	{
		Result.Flags |= EPackageStoreEntryFlags::OptionalSegment;
		if (OptionalSegmentPackage->HasEditorData())
		{
			// AutoOptional packages are saved with editor data included
			Result.Flags |= EPackageStoreEntryFlags::AutoOptional;
		}
	}
	
	Result.PackageName = Package->Name;
	Result.PackageId = FPackageId::FromName(Package->Name);
	Result.ExportInfo.ExportCount = Package->Exports.Num();
	Result.ExportInfo.ExportBundleCount = Package->GraphData.ExportBundles.Num();
	Result.ImportedPackageIds.Reserve(Package->ImportedPackages.Num());
	for (const FPackageStorePackage::FImportedPackageRef& ImportedPackage : Package->ImportedPackages)
	{
		Result.ImportedPackageIds.Add(ImportedPackage.Id);
	}
	
	if (OptionalSegmentPackage)
	{
		Result.OptionalSegmentExportInfo.ExportCount = OptionalSegmentPackage->Exports.Num();
		Result.OptionalSegmentExportInfo.ExportBundleCount = OptionalSegmentPackage->GraphData.ExportBundles.Num();
		Result.OptionalSegmentImportedPackageIds.Reserve(OptionalSegmentPackage->ImportedPackages.Num());
		for (const FPackageStorePackage::FImportedPackageRef& ImportedPackage : OptionalSegmentPackage->ImportedPackages)
		{
			Result.OptionalSegmentImportedPackageIds.Add(ImportedPackage.Id);
		}
	}
	return Result;
}
