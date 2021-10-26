// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/LooseCookedPackageWriter.h"

#include "AssetRegistry/AssetRegistryState.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Cooker/AsyncIODelete.h"
#include "Cooker/CookTypes.h"
#include "Cooker/PackageNameCache.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CString.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/ArchiveStackTrace.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "UObject/Package.h"
#include "PackageStoreOptimizer.h"

FLooseCookedPackageWriter::FLooseCookedPackageWriter(const FString& InOutputPath, const FString& InMetadataDirectoryPath, const ITargetPlatform* InTargetPlatform,
	FAsyncIODelete& InAsyncIODelete, const FPackageNameCache& InPackageNameCache, const TArray<TSharedRef<IPlugin>>& InPluginsToRemap)
	: OutputPath(InOutputPath)
	, MetadataDirectoryPath(InMetadataDirectoryPath)
	, TargetPlatform(*InTargetPlatform)
	, PackageNameCache(InPackageNameCache)
	, PackageStoreManifest(InOutputPath)
	, PluginsToRemap(InPluginsToRemap)
	, AsyncIODelete(InAsyncIODelete)
	, bIterateSharedBuild(false)
	, bCompletedExportsArchiveForDiff(false)
{
}

FLooseCookedPackageWriter::~FLooseCookedPackageWriter()
{
}

void FLooseCookedPackageWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	Super::BeginPackage(Info);
	PackageStoreManifest.BeginPackage(Info.PackageName);
}

TFuture<FMD5Hash> FLooseCookedPackageWriter::CommitPackageInternal(const FCommitPackageInfo& Info)
{
	TFuture<FMD5Hash> CookedHash;
	if (Info.bSucceeded)
	{
		CookedHash = AsyncSave(Info);
	}
	UpdateManifest();
	return CookedHash;
}

TFuture<FMD5Hash> FLooseCookedPackageWriter::AsyncSave(const FCommitPackageInfo& Info)
{
	FCommitContext Context{ Info };

	// The order of these collection calls is important, both for ExportsBuffers (affects the meaning of offsets
	// to those buffers) and for OutputFiles (affects the calculation of the Hash for the set of PackageData)
	// The order of ExportsBuffers must match CompleteExportsArchiveForDiff.
	CollectForSavePackageData(Context);
	CollectForSaveBulkData(Context);
	CollectForSaveLinkerAdditionalDataRecords(Context);
	CollectForSaveAdditionalFileRecords(Context);
	CollectForSaveExportsFooter(Context);
	CollectForSaveExportsBuffers(Context);

	return AsyncSaveOutputFiles(Context);
}

void FLooseCookedPackageWriter::CompleteExportsArchiveForDiff(FLargeMemoryWriter& ExportsArchive)
{
	bCompletedExportsArchiveForDiff = true;

	// Add on all the attachments which are usually added on during Commit. The order must match AsyncSave.
	for (FBulkDataRecord& Record : Records.BulkDatas)
	{
		if (Record.Info.BulkDataType == FBulkDataInfo::AppendToExports)
		{
			ExportsArchive.Serialize(const_cast<void*>(Record.Buffer.GetData()), Record.Buffer.GetSize());
		}
	}
	for (FLinkerAdditionalDataRecord& Record : Records.LinkerAdditionalDatas)
	{
		ExportsArchive.Serialize(const_cast<void*>(Record.Buffer.GetData()), Record.Buffer.GetSize());
	}

	uint32 FooterData = PACKAGE_FILE_TAG;
	ExportsArchive << FooterData;
}


void FLooseCookedPackageWriter::CollectForSavePackageData(FCommitContext& Context)
{
	Context.ExportsBuffers.Add(FExportBuffer{ Records.Package->Buffer, MoveTemp(Records.Package->Regions) });
}

void FLooseCookedPackageWriter::CollectForSaveBulkData(FCommitContext& Context)
{
	for (FBulkDataRecord& Record : Records.BulkDatas)
	{
		if (Record.Info.BulkDataType == FBulkDataInfo::AppendToExports)
		{
			if (bCompletedExportsArchiveForDiff)
			{
				// Already Added in CompleteExportsArchiveForDiff
				continue;
			}
			Context.ExportsBuffers.Add(FExportBuffer{ Record.Buffer, MoveTemp(Record.Regions) });
		}
		else
		{
			FWriteFileData& OutputFile = Context.OutputFiles.Emplace_GetRef();
			OutputFile.Filename = FPaths::ChangeExtension(Records.Begin->LooseFilePath,
				LexToString(BulkDataTypeToExtension(Record.Info.BulkDataType)));
			OutputFile.Buffer = FCompositeBuffer(Record.Buffer);
			OutputFile.Regions = MoveTemp(Record.Regions);
			OutputFile.bIsSidecar = true;
		}
	}
}
void FLooseCookedPackageWriter::CollectForSaveLinkerAdditionalDataRecords(FCommitContext& Context)
{
	if (bCompletedExportsArchiveForDiff)
	{
		// Already Added in CompleteExportsArchiveForDiff
		return;
	}

	for (FLinkerAdditionalDataRecord& Record : Records.LinkerAdditionalDatas)
	{
		Context.ExportsBuffers.Add(FExportBuffer{ Record.Buffer, MoveTemp(Record.Regions) });
	}
}

void FLooseCookedPackageWriter::CollectForSaveAdditionalFileRecords(FCommitContext& Context)
{
	for (FAdditionalFileRecord& Record : Records.AdditionalFiles)
	{
		FWriteFileData& OutputFile = Context.OutputFiles.Emplace_GetRef();
		OutputFile.Filename = Record.Info.Filename;
		OutputFile.Buffer = FCompositeBuffer(Record.Buffer);
		OutputFile.bIsSidecar = true;
	}
}

void FLooseCookedPackageWriter::CollectForSaveExportsFooter(FCommitContext& Context)
{
	if (bCompletedExportsArchiveForDiff)
	{
		// Already Added in CompleteExportsArchiveForDiff
		return;
	}

	uint32 FooterData = PACKAGE_FILE_TAG;
	FSharedBuffer Buffer = FSharedBuffer::Clone(&FooterData, sizeof(FooterData));
	Context.ExportsBuffers.Add(FExportBuffer{ Buffer, TArray<FFileRegion>() });
}

void FLooseCookedPackageWriter::AddToExportsSize(int32& ExportsSize)
{
	ExportsSize += sizeof(uint32); // Footer size
}

void FLooseCookedPackageWriter::CollectForSaveExportsBuffers(FCommitContext& Context)
{
	// Split the ExportsBuffer into (1) Header and (2) Exports + AllAppendedData
	int64 HeaderSize = Records.Package->Info.HeaderSize;
	check(Context.ExportsBuffers.Num() > 0);
	FExportBuffer& HeaderAndExportsBuffer = Context.ExportsBuffers[0];
	FSharedBuffer& HeaderAndExportsData = HeaderAndExportsBuffer.Buffer;

	// Header (.uasset/.umap)
	{
		FWriteFileData& OutputFile = Context.OutputFiles.Emplace_GetRef();
		OutputFile.Filename = Records.Begin->LooseFilePath;
		OutputFile.Buffer = FCompositeBuffer(
			FSharedBuffer::MakeView(HeaderAndExportsData.GetData(), HeaderSize, HeaderAndExportsData));
		OutputFile.bIsSidecar = false;
	}

	// Exports + AllAppendedData (.uexp)
	{
		FWriteFileData& OutputFile = Context.OutputFiles.Emplace_GetRef();
		OutputFile.Filename = FPaths::ChangeExtension(Records.Begin->LooseFilePath, LexToString(EPackageExtension::Exports));
		OutputFile.bIsSidecar = false;

		int32 NumBuffers = Context.ExportsBuffers.Num();
		TArray<FSharedBuffer> BuffersForComposition;
		BuffersForComposition.Reserve(NumBuffers);

		const uint8* ExportsStart = static_cast<const uint8*>(HeaderAndExportsData.GetData()) + HeaderSize;
		BuffersForComposition.Add(FSharedBuffer::MakeView(ExportsStart, HeaderAndExportsData.GetSize() - HeaderSize,
			HeaderAndExportsData));
		OutputFile.Regions.Append(MoveTemp(HeaderAndExportsBuffer.Regions));

		for (FExportBuffer& ExportsBuffer : TArrayView<FExportBuffer>(Context.ExportsBuffers).Slice(1, NumBuffers - 1))
		{
			BuffersForComposition.Add(ExportsBuffer.Buffer);
			OutputFile.Regions.Append(MoveTemp(ExportsBuffer.Regions));
		}
		OutputFile.Buffer = FCompositeBuffer(BuffersForComposition);

		// Adjust regions so they are relative to the start of the uexp file
		for (FFileRegion& Region : OutputFile.Regions)
		{
			Region.Offset -= HeaderSize;
		}
	}
}

void FLooseCookedPackageWriter::ResetPackage()
{
	Super::ResetPackage();
	bCompletedExportsArchiveForDiff = false;
}

TFuture<FMD5Hash> FLooseCookedPackageWriter::AsyncSaveOutputFiles(FCommitContext& Context)
{
	if (!EnumHasAnyFlags(Context.Info.WriteOptions, EWriteOptions::Write | EWriteOptions::ComputeHash))
	{
		return TFuture<FMD5Hash>();
	}

	UE::SavePackageUtilities::IncrementOutstandingAsyncWrites();
	return Async(EAsyncExecution::TaskGraph,
		[OutputFiles = MoveTemp(Context.OutputFiles), WriteOptions = Context.Info.WriteOptions]() mutable
	{
		FMD5 AccumulatedHash;
		for (FWriteFileData& OutputFile : OutputFiles)
		{
			OutputFile.Write(AccumulatedHash, WriteOptions);
		}

		FMD5Hash OutputHash;
		OutputHash.Set(AccumulatedHash);
		UE::SavePackageUtilities::DecrementOutstandingAsyncWrites();
		return OutputHash;
	});
}

static void WriteToFile(const FString& Filename, const FCompositeBuffer& Buffer)
{
	IFileManager& FileManager = IFileManager::Get();

	for (int32 Tries = 0; Tries < 3; ++Tries)
	{
		if (FArchive* Ar = FileManager.CreateFileWriter(*Filename))
		{
			int64 DataSize = 0;
			for (const FSharedBuffer& Segment : Buffer.GetSegments())
			{
				int64 SegmentSize = static_cast<int64>(Segment.GetSize());
				Ar->Serialize(const_cast<void*>(Segment.GetData()), SegmentSize);
				DataSize += SegmentSize;
			}
			delete Ar;

			if (FileManager.FileSize(*Filename) != DataSize)
			{
				FileManager.Delete(*Filename);

				UE_LOG(LogSavePackage, Fatal, TEXT("Could not save to %s!"), *Filename);
			}
			return;
		}
	}

	UE_LOG(LogSavePackage, Fatal, TEXT("Could not write to %s!"), *Filename);
}

void FLooseCookedPackageWriter::FWriteFileData::Write(FMD5& AccumulatedHash, EWriteOptions WriteOptions) const
{
	if (EnumHasAnyFlags(WriteOptions, EWriteOptions::ComputeHash))
	{
		for (const FSharedBuffer& Segment : Buffer.GetSegments())
		{
			AccumulatedHash.Update(static_cast<const uint8*>(Segment.GetData()), Segment.GetSize());
		}
	}

	if ((bIsSidecar && EnumHasAnyFlags(WriteOptions, EWriteOptions::WriteSidecars)) ||
		(!bIsSidecar && EnumHasAnyFlags(WriteOptions, EWriteOptions::WritePackage)))
	{
		const FString* WriteFilename = &Filename;
		FString FilenameBuffer;
		if (EnumHasAnyFlags(WriteOptions, EWriteOptions::SaveForDiff))
		{
			FilenameBuffer = FPaths::Combine(FPaths::GetPath(Filename),
				FPaths::GetBaseFilename(Filename) + TEXT("_ForDiff") + FPaths::GetExtension(Filename, true));
			WriteFilename = &FilenameBuffer;
		}
		WriteToFile(*WriteFilename, Buffer);

		if (Regions.Num() > 0)
		{
			TArray<uint8> Memory;
			FMemoryWriter Ar(Memory);
			FFileRegion::SerializeFileRegions(Ar, const_cast<TArray<FFileRegion>&>(Regions));

			WriteToFile(*WriteFilename + FFileRegion::RegionsFileExtension,
				FCompositeBuffer(FSharedBuffer::MakeView(Memory.GetData(), Memory.Num())));
		}
	}
}

void FLooseCookedPackageWriter::UpdateManifest()
{
	FName PackageName = Records.Begin->PackageName;
	FIoChunkId ChunkId = CreateIoChunkId(FPackageId::FromName(PackageName).Value(), 0, EIoChunkType::ExportBundleData);
	PackageStoreManifest.AddPackageData(PackageName, Records.Begin->LooseFilePath, ChunkId);
}

bool FLooseCookedPackageWriter::GetPreviousCookedBytes(FName PackageName, FPreviousCookedBytesData& OutData)
{
	FArchiveStackTrace::FPackageData ExistingPackageData;
	FArchiveStackTrace::LoadPackageIntoMemory(*Records.Begin->LooseFilePath, ExistingPackageData, OutData.Data);
	OutData.Size = ExistingPackageData.Size;
	OutData.HeaderSize = ExistingPackageData.HeaderSize;
	OutData.StartOffset = ExistingPackageData.StartOffset;
	return OutData.Data.IsValid();
}

FDateTime FLooseCookedPackageWriter::GetPreviousCookTime() const
{
	const FString PreviousAssetRegistry = FPaths::Combine(MetadataDirectoryPath, GetDevelopmentAssetRegistryFilename());
	return IFileManager::Get().GetTimeStamp(*PreviousAssetRegistry);
}

void FLooseCookedPackageWriter::Initialize(const FCookInfo& Info)
{
	bIterateSharedBuild = Info.bIterateSharedBuild;
	if (Info.bFullBuild)
	{
		DeleteSandboxDirectory();
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SaveScriptObjects);
		FPackageStoreOptimizer PackageStoreOptimizer;
		PackageStoreOptimizer.Initialize();
		FIoBuffer ScriptObjectsBuffer = PackageStoreOptimizer.CreateScriptObjectsBuffer();
		FFileHelper::SaveArrayToFile(
			MakeArrayView(ScriptObjectsBuffer.Data(), ScriptObjectsBuffer.DataSize()),
			*(MetadataDirectoryPath / TEXT("scriptobjects.bin")));
	}
}

void FLooseCookedPackageWriter::BeginCook()
{
}

void FLooseCookedPackageWriter::EndCook()
{
	PackageStoreManifest.Save(*(MetadataDirectoryPath / TEXT("packagestore.manifest")));
}

void FLooseCookedPackageWriter::Flush()
{
	UPackage::WaitForAsyncFileWrites();
}

TUniquePtr<FAssetRegistryState> FLooseCookedPackageWriter::LoadPreviousAssetRegistry()
{
	// Report files from the shared build if the option is set
	FString PreviousAssetRegistryFile;
	if (bIterateSharedBuild)
	{
		// clean the sandbox
		DeleteSandboxDirectory();
		PreviousAssetRegistryFile = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("SharedIterativeBuild"),
			*TargetPlatform.PlatformName(), TEXT("Metadata"), GetDevelopmentAssetRegistryFilename());
	}
	else
	{
		PreviousAssetRegistryFile = FPaths::Combine(MetadataDirectoryPath, GetDevelopmentAssetRegistryFilename());
	}

	UncookedPathToCookedPath.Reset();

	FArrayReader SerializedAssetData;
	if (!IFileManager::Get().FileExists(*PreviousAssetRegistryFile) || !FFileHelper::LoadFileToArray(SerializedAssetData, *PreviousAssetRegistryFile))
	{ 
		RemoveCookedPackages();
		return TUniquePtr<FAssetRegistryState>();
	}

	TUniquePtr<FAssetRegistryState> PreviousState = MakeUnique<FAssetRegistryState>();
	PreviousState->Load(SerializedAssetData);

	// If we are iterating from a shared build the cooked files do not exist in the local cooked directory;
	// we assume they are packaged in the pak file (which we don't want to extract to confirm) and keep them all.
	if (!bIterateSharedBuild)
	{
		// For regular iteration, remove every file from the PreviousState that no longer exists in the cooked directory
		// and remove every cooked file from disk that is not present in the AssetRegistry
		GetAllCookedFiles();
		TSet<FName> ExistsOnlyInRegistry;
		TSet<FName> ExistsOnlyOnDisk;
		ExistsOnlyOnDisk.Reserve(UncookedPathToCookedPath.Num());
		for (TPair<FName, FName>& Pair : UncookedPathToCookedPath)
		{
			ExistsOnlyOnDisk.Add(Pair.Key);
		}
		for (const TPair<FName, const FAssetPackageData*>& Pair : PreviousState->GetAssetPackageDataMap())
		{
			FName PackageName = Pair.Key;
			const FAssetPackageData* PackageData = Pair.Value;
			const FName UncookedFilename = PackageNameCache.GetCachedStandardFileName(PackageName);
			bool bExistsOnDisk = false;
			if (!UncookedFilename.IsNone())
			{
				bExistsOnDisk = (ExistsOnlyOnDisk.Remove(UncookedFilename) == 1);
			}
			if (!bExistsOnDisk)
			{
				ExistsOnlyInRegistry.Add(PackageName);
			}
		}

		if (ExistsOnlyInRegistry.Num())
		{
			PreviousState->PruneAssetData(TSet<FName>(), ExistsOnlyInRegistry, FAssetRegistrySerializationOptions());
		}
		if (ExistsOnlyOnDisk.Num())
		{
			RemoveCookedPackagesByUncookedFilename(ExistsOnlyOnDisk.Array());
		}
	}

	return PreviousState;
}

FCbObject FLooseCookedPackageWriter::GetOplogAttachment(FName PackageName, FUtf8StringView AttachmentKey)
{
	/** Oplog attachments are not implemented by FLooseCookedPackageWriter */
	return FCbObject();
}

void FLooseCookedPackageWriter::RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove)
{
	if (UncookedPathToCookedPath.IsEmpty())
	{
		return;
	}

	if (PackageNamesToRemove.Num() > 0)
	{
		// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
		// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
		UPackage::WaitForAsyncFileWrites();

		// PackageNameCache is read-game-thread-only, so we have to read it before calling parallel-for
		TArray<FName> UncookedFileNamesToRemove;
		UncookedFileNamesToRemove.Reserve(PackageNamesToRemove.Num());
		for (FName PackageName : PackageNamesToRemove)
		{
			const FName UncookedFileName = PackageNameCache.GetCachedStandardFileName(PackageName);
			if (!UncookedFileName.IsNone())
			{
				UncookedFileNamesToRemove.Add(UncookedFileName);
			}
		}
		RemoveCookedPackagesByUncookedFilename(UncookedFileNamesToRemove);
	}

	// We no longer have a use for UncookedPathToCookedPath, after the RemoveCookedPackages call at the beginning of the cook.
	UncookedPathToCookedPath.Empty();
}

void FLooseCookedPackageWriter::RemoveCookedPackagesByUncookedFilename(const TArray<FName>& UncookedFileNamesToRemove)
{
	auto DeletePackageLambda = [&UncookedFileNamesToRemove, this](int32 PackageIndex)
	{
		FName UncookedFileName = UncookedFileNamesToRemove[PackageIndex];
		FName* CookedFileName = UncookedPathToCookedPath.Find(UncookedFileName);
		if (CookedFileName)
		{
			TStringBuilder<256> FilePath;
			CookedFileName->ToString(FilePath);
			IFileManager::Get().Delete(*FilePath, true, true, true);
		}
	};
	ParallelFor(UncookedFileNamesToRemove.Num(), DeletePackageLambda);

	for (FName UncookedFilename : UncookedFileNamesToRemove)
	{
		UncookedPathToCookedPath.Remove(UncookedFilename);
	}
}

void FLooseCookedPackageWriter::MarkPackagesUpToDate(TArrayView<const FName> UpToDatePackages)
{
}

void FLooseCookedPackageWriter::RemoveCookedPackages()
{
	DeleteSandboxDirectory();
}

void FLooseCookedPackageWriter::DeleteSandboxDirectory()
{
	// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
	// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
	UPackage::WaitForAsyncFileWrites();

	FString SandboxDirectory = OutputPath;
	FPaths::NormalizeDirectoryName(SandboxDirectory);

	AsyncIODelete.DeleteDirectory(SandboxDirectory);
}

class FPackageSearchVisitor : public IPlatformFile::FDirectoryVisitor
{
	TArray<FString>& FoundFiles;
public:
	FPackageSearchVisitor(TArray<FString>& InFoundFiles)
		: FoundFiles(InFoundFiles)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory == false)
		{
			FString Filename(FilenameOrDirectory);
			FStringView Extension(FPathViews::GetExtension(Filename, true /* bIncludeDot */));
			const TCHAR* ExtensionStr = Extension.GetData();
			check(ExtensionStr[Extension.Len()] == '\0'); // IsPackageExtension takes a null-terminated TCHAR; we should have it since GetExtension is from the end of the filename
			if (FPackageName::IsPackageExtension(ExtensionStr))
			{
				FoundFiles.Add(Filename);
			}
		}
		return true;
	}
};

void FLooseCookedPackageWriter::GetAllCookedFiles()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLooseCookedPackageWriter::GetAllCookedFiles);

	const FString& SandboxRootDir = OutputPath;
	TArray<FString> CookedFiles;
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		FPackageSearchVisitor PackageSearch(CookedFiles);
		PlatformFile.IterateDirectoryRecursively(*SandboxRootDir, PackageSearch);
	}

	const FString SandboxProjectDir = FPaths::Combine(OutputPath, FApp::GetProjectName()) + TEXT("/");
	const FString RelativeRootDir = FPaths::GetRelativePathToRoot();
	const FString RelativeProjectDir = FPaths::ProjectDir();
	FString UncookedFilename;
	UncookedFilename.Reserve(1024);

	for (const FString& CookedFile : CookedFiles)
	{
		const FName CookedFName(*CookedFile);
		const FName UncookedFName = ConvertCookedPathToUncookedPath(
			SandboxRootDir, RelativeRootDir,
			SandboxProjectDir, RelativeProjectDir,
			CookedFile, UncookedFilename);

		UncookedPathToCookedPath.Add(UncookedFName, CookedFName);
	}
}

FName FLooseCookedPackageWriter::ConvertCookedPathToUncookedPath(
	const FString& SandboxRootDir, const FString& RelativeRootDir,
	const FString& SandboxProjectDir, const FString& RelativeProjectDir,
	const FString& CookedPath, FString& OutUncookedPath) const
{
	OutUncookedPath.Reset();

	// Check for remapped plugins' cooked content
	if (PluginsToRemap.Num() > 0 && CookedPath.Contains(REMAPPED_PLUGINS))
	{
		int32 RemappedIndex = CookedPath.Find(REMAPPED_PLUGINS);
		check(RemappedIndex >= 0);
		static uint32 RemappedPluginStrLen = FCString::Strlen(REMAPPED_PLUGINS);
		// Snip everything up through the RemappedPlugins/ off so we can find the plugin it corresponds to
		FString PluginPath = CookedPath.RightChop(RemappedIndex + RemappedPluginStrLen + 1);
		// Find the plugin that owns this content
		for (const TSharedRef<IPlugin>& Plugin : PluginsToRemap)
		{
			if (PluginPath.StartsWith(Plugin->GetName()))
			{
				OutUncookedPath = Plugin->GetContentDir();
				static uint32 ContentStrLen = FCString::Strlen(TEXT("Content/"));
				// Chop off the pluginName/Content since it's part of the full path
				OutUncookedPath /= PluginPath.RightChop(Plugin->GetName().Len() + ContentStrLen);
				break;
			}
		}

		if (OutUncookedPath.Len() > 0)
		{
			return FName(*OutUncookedPath);
		}
		// Otherwise fall through to sandbox handling
	}

	auto BuildUncookedPath =
		[&OutUncookedPath](const FString& CookedPath, const FString& CookedRoot, const FString& UncookedRoot)
	{
		OutUncookedPath.AppendChars(*UncookedRoot, UncookedRoot.Len());
		OutUncookedPath.AppendChars(*CookedPath + CookedRoot.Len(), CookedPath.Len() - CookedRoot.Len());
	};

	if (CookedPath.StartsWith(SandboxRootDir))
	{
		// Optimized CookedPath.StartsWith(SandboxProjectDir) that does not compare all of SandboxRootDir again
		if (CookedPath.Len() >= SandboxProjectDir.Len() &&
			0 == FCString::Strnicmp(
				*CookedPath + SandboxRootDir.Len(),
				*SandboxProjectDir + SandboxRootDir.Len(),
				SandboxProjectDir.Len() - SandboxRootDir.Len()))
		{
			BuildUncookedPath(CookedPath, SandboxProjectDir, RelativeProjectDir);
		}
		else
		{
			BuildUncookedPath(CookedPath, SandboxRootDir, RelativeRootDir);
		}
	}
	else
	{
		FString FullCookedFilename = FPaths::ConvertRelativePathToFull(CookedPath);
		BuildUncookedPath(FullCookedFilename, SandboxRootDir, RelativeRootDir);
	}

	// Convert to a standard filename as required by FPackageNameCache where this path is used.
	FPaths::MakeStandardFilename(OutUncookedPath);

	return FName(*OutUncookedPath);
}

EPackageExtension FLooseCookedPackageWriter::BulkDataTypeToExtension(FBulkDataInfo::EType BulkDataType)
{
	switch (BulkDataType)
	{
	case FBulkDataInfo::AppendToExports:
		return EPackageExtension::Exports;
	case FBulkDataInfo::BulkSegment:
		return EPackageExtension::BulkDataDefault;
	case FBulkDataInfo::Mmap:
		return EPackageExtension::BulkDataMemoryMapped;
	case FBulkDataInfo::Optional:
		return EPackageExtension::BulkDataOptional;
	default:
		checkNoEntry();
		return EPackageExtension::Unspecified;
	}
}