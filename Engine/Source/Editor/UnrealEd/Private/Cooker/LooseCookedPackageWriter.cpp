// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/LooseCookedPackageWriter.h"

#include "AssetRegistry/AssetRegistryState.h"
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
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/ArrayReader.h"
#include "UObject/Package.h"

const static FName NAME_DummyCookedFilename(TEXT("DummyCookedFilename"));

FLooseCookedPackageWriter::FLooseCookedPackageWriter(const FString& InOutputPath, const FString& InMetadataDirectoryPath, const ITargetPlatform* InTargetPlatform,
	FAsyncIODelete& InAsyncIODelete, const FPackageNameCache& InPackageNameCache, const TArray<TSharedRef<IPlugin>>& InPluginsToRemap)
	: OutputPath(InOutputPath)
	, MetadataDirectoryPath(InMetadataDirectoryPath)
	, TargetPlatform(*InTargetPlatform)
	, PackageNameCache(InPackageNameCache)
	, PluginsToRemap(InPluginsToRemap)
	, AsyncIODelete(InAsyncIODelete)
	, bIterateSharedBuild(false)
{
}

FLooseCookedPackageWriter::~FLooseCookedPackageWriter()
{
}

void FLooseCookedPackageWriter::BeginPackage(const FBeginPackageInfo& Info)
{
}

void FLooseCookedPackageWriter::CommitPackage(const FCommitPackageInfo& Info)
{
}

void FLooseCookedPackageWriter::WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions)
{
	// Not yet implemented
	checkNoEntry();
}

void FLooseCookedPackageWriter::WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	// Not yet implemented
	checkNoEntry();
}

bool FLooseCookedPackageWriter::WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData)
{
	// Not yet implemented
	checkNoEntry();
	return false;
}

void FLooseCookedPackageWriter::WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	// Should not be called because IsLinkerAdditionalDataInSeparateArchive returned false
	checkNoEntry();
}

FDateTime FLooseCookedPackageWriter::GetPreviousCookTime() const
{
	const FString PreviousAssetRegistry = FPaths::Combine(MetadataDirectoryPath, TEXT("DevelopmentAssetRegistry.bin"));
	return IFileManager::Get().GetTimeStamp(*PreviousAssetRegistry);
}

void FLooseCookedPackageWriter::BeginCook(const FCookInfo& Info)
{
	bIterateSharedBuild = Info.bIterateSharedBuild;
	if (Info.bCleanBuild)
	{
		DeleteSandboxDirectory();
	}
}

void FLooseCookedPackageWriter::EndCook()
{
}

void FLooseCookedPackageWriter::Flush()
{
	UPackage::WaitForAsyncFileWrites();
}

void FLooseCookedPackageWriter::GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages)
{
	// GetCookedPackages is currently only called in BeginCook to read the list of previous cooked files
	// Read that list from the platform cooked asset registry file

	// Report files from the shared build if the option is set
	FString PreviousAssetRegistryFile;
	if (bIterateSharedBuild)
	{
		// clean the sandbox
		DeleteSandboxDirectory();
		PreviousAssetRegistryFile = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("SharedIterativeBuild"),
			*TargetPlatform.PlatformName(), TEXT("Metadata"), GetDevelopmentAssetRegistryFilename());

		// We use NAME_DummyCookedFilename as a placeholder in UncookedPathToCookedPath to indicate the file comes from the shared pak file
		// If an actual filename of this name exists, our placeholder system will break
		check(!IFileManager::Get().FileExists(*NAME_DummyCookedFilename.ToString()));
	}
	else
	{
		PreviousAssetRegistryFile = FPaths::Combine(MetadataDirectoryPath, GetDevelopmentAssetRegistryFilename());
	}

	UncookedPathToCookedPath.Reset();
	GetAllCookedFiles();

	FArrayReader SerializedAssetData;
	if (IFileManager::Get().FileExists(*PreviousAssetRegistryFile) && FFileHelper::LoadFileToArray(SerializedAssetData, *PreviousAssetRegistryFile))
	{
		PreviousState = MakeUnique<FAssetRegistryState>();
		PreviousState->Load(SerializedAssetData);

		const TMap<FName, const FAssetPackageData*>& DataMap = PreviousState->GetAssetPackageDataMap();
		OutCookedPackages.Reserve(OutCookedPackages.Num() + DataMap.Num());
		FString UncookedFileNameStr;

		for (const TPair<FName, const FAssetPackageData*>& Pair : DataMap)
		{
			FName PackageName = Pair.Key;
			const FAssetPackageData* PackageData = Pair.Value;
			bool bExistsOnDisk = false;
			if (bIterateSharedBuild)
			{
				// if we are iterating of a shared build the cooked files might not exist in the cooked directory because we assume they are packaged in the pak file (which we don't want to extract)
				// Overwrite the value in UncookedPathToCookpath with a placeholder that indicates the file exists even if it's not present in the Cooked directory
				if (FPackageName::DoesPackageExist(PackageName.ToString(), nullptr, &UncookedFileNameStr))
				{
					UncookedPathToCookedPath.Add(FName(*UncookedFileNameStr), NAME_DummyCookedFilename);
				}
				bExistsOnDisk = true;
			}
			else
			{
				const FName UncookedFilename = PackageNameCache.GetCachedStandardFileName(PackageName);
				if (!UncookedFilename.IsNone())
				{
					FName* CookedFilename = UncookedPathToCookedPath.Find(UncookedFilename);
					bExistsOnDisk = CookedFilename != nullptr;
				}
			}
			if (bExistsOnDisk)
			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS;
				OutCookedPackages.Add(FCookedPackageInfo{ PackageName, PackageData->CookedHash,PackageData->PackageGuid,
					PackageData->DiskSize, FIoHash() /** TargetDomainDependencies are not implemented by FLooseCookedPackageWriter */ });
				PRAGMA_ENABLE_DEPRECATION_WARNINGS;
			}
		}
	}
}

FCbObject FLooseCookedPackageWriter::GetTargetDomainDependencies(FName PackageName)
{
	/** TargetDomainDependencies are not implemented by FLooseCookedPackageWriter */
	return FCbObject();
}

void FLooseCookedPackageWriter::RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove)
{
	if (UncookedPathToCookedPath.IsEmpty())
	{
		return;
	}

	// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
	// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
	UPackage::WaitForAsyncFileWrites();
	
	if (PackageNamesToRemove.Num() > 0)
	{
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
	}
	UncookedPathToCookedPath.Empty();
}

void FLooseCookedPackageWriter::RemoveCookedPackages()
{
	DeleteSandboxDirectory();
}

FAssetRegistryState* FLooseCookedPackageWriter::ReleasePreviousAssetRegistry()
{
	return PreviousState.Release();
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
