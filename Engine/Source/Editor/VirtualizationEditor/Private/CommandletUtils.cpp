// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommandletUtils.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "Containers/Set.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/PackageTrailer.h"

namespace UE::Virtualization
{

TArray<FString> FindAllPackages()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindAllPackages);

	TArray<FString> PackagePaths;

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Do an async search even though we immediately block on it. This will result in the asset registry cache
	// being saved to disk on a background thread which is an operation we don't need to wait on. This can 
	// save a fair amount of time on larger projects.
	const bool bSynchronousSearch = false;
	AssetRegistry.Get().SearchAllAssets(bSynchronousSearch);
	AssetRegistry.Get().WaitForCompletion();

	const FString EnginePath = FPaths::EngineDir();

	AssetRegistry.Get().EnumerateAllPackages([&PackagePaths, EnginePath](FName PackageName, const FAssetPackageData& PackageData)
		{
			FString RelFileName;
			if (PackageData.Extension != EPackageExtension::Unspecified && PackageData.Extension != EPackageExtension::Custom)
			{
				const FString Extension = LexToString(PackageData.Extension);
				if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), RelFileName, Extension))
				{
					FString StdFileName = FPaths::CreateStandardFilename(RelFileName);
				
					// Now we have the absolute file path we can filter out engine packages
					if (!StdFileName.StartsWith(EnginePath))
					{
						PackagePaths.Emplace(MoveTemp(StdFileName));
					}
				}
			}
		});

	return PackagePaths;
}

TArray<FString> FindPackagesInDirectory(const FString& DirectoryToSearch)
{
	TArray<FString> FilesInPackageFolder;
	FPackageName::FindPackagesInDirectory(FilesInPackageFolder, DirectoryToSearch);

	TArray<FString> PackageNames;
	PackageNames.Reserve(FilesInPackageFolder.Num());

	for (const FString& BasePath : FilesInPackageFolder)
	{
		PackageNames.Add(FPaths::CreateStandardFilename(BasePath));
	}

	return PackageNames;
}

TArray<FString> DiscoverPackages(const FString& CmdlineParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DiscoverPackages);

	FString PackageDir;
	if (FParse::Value(*CmdlineParams, TEXT("PackageDir="), PackageDir) || FParse::Value(*CmdlineParams, TEXT("PackageFolder="), PackageDir))
	{
		return FindPackagesInDirectory(PackageDir);
	}
	else
	{
		return FindAllPackages();
	}
}

TArray<FIoHash> FindVirtualizedPayloads(const TArray<FString>& PackagePaths)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindVirtualizedPayloads);

	// Each task will write out to its own TSet so we don't have to lock anything, we
	// will combine the sets at the end.
	TArray<TSet<FIoHash>> PayloadsPerTask;

	ParallelForWithTaskContext(PayloadsPerTask, PackagePaths.Num(),
		[&PackagePaths](TSet<FIoHash>& Context, int32 Index)
		{
			const FString& PackageName = PackagePaths[Index];

			UE::FPackageTrailer Trailer;
			if (UE::FPackageTrailer::TryLoadFromFile(PackageName, Trailer))
			{
				TArray<FIoHash> VirtualizedPayloads = Trailer.GetPayloads(UE::EPayloadStorageType::Virtualized);
				Context.Append(VirtualizedPayloads);
			}
		});

	// Combine the results into a final set
	TSet<FIoHash> AllPayloads;
	for (const TSet<FIoHash>& Payloads : PayloadsPerTask)
	{
		AllPayloads.Append(Payloads);
	}

	return AllPayloads.Array();
}

void FindVirtualizedPayloadsAndTrailers(const TArray<FString>& PackagePaths, TMap<FString, UE::FPackageTrailer>& OutPackages, TSet<FIoHash>& OutPayloads)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindVirtualizedPayloadsAndTrailers);

	struct FTaskContext
	{
		TMap<FString, UE::FPackageTrailer> Packages;
		TSet<FIoHash> Payloads;
	};

	TArray<FTaskContext> TaskContext;

	ParallelForWithTaskContext(TaskContext, PackagePaths.Num(),
		[&PackagePaths](FTaskContext& Context, int32 Index)
		{
			const FString& PackageName = PackagePaths[Index];

	UE::FPackageTrailer Trailer;
	if (UE::FPackageTrailer::TryLoadFromFile(PackageName, Trailer))
	{
		const TArray<FIoHash> VirtualizedPayloads = Trailer.GetPayloads(UE::EPayloadStorageType::Virtualized);
		if (!VirtualizedPayloads.IsEmpty())
		{
			Context.Packages.Emplace(PackageName, MoveTemp(Trailer));
			Context.Payloads.Append(VirtualizedPayloads);
		}
	}
		});

	for (const FTaskContext& Context : TaskContext)
	{
		OutPackages.Append(Context.Packages);
		OutPayloads.Append(Context.Payloads);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CompactStable);
		OutPayloads.CompactStable();
	}
}

} //namespace UE::Virtualization
