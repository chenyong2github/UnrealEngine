// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureVersePathMapperCommandlet.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryState.h"
#include "GameFeatureData.h"
#include "GameFeaturesSubsystem.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IPluginManager.h"
#include "Logging/StructuredLog.h"
#include "InstallBundleUtils.h"
#include "JsonObjectConverter.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameFeatureVersePathMapper, Log, All);

namespace GameFeatureVersePathMapper
{
	struct FArgs
	{
		FString DevARPath;

		FString OutputPath;

		const ITargetPlatform* TargetPlatform = nullptr;

		static TOptional<FArgs> Parse(const TCHAR* CmdLineParams)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Parsing command line");

			FArgs Args;

			// Optional path to dev asset registry
			FString DevARFilename;
			if (FParse::Value(CmdLineParams, TEXT("-DevAR="), DevARFilename))
			{
				if (IFileManager::Get().FileExists(*DevARFilename) && FPathViews::GetExtension(DevARFilename) == TEXTVIEW("bin"))
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Using dev asset registry path '{Path}'", DevARFilename);
					Args.DevARPath = DevARFilename;
				}
				else
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-DevAR did not specify a valid path.");
					return {};
				}
			}

			// Required output path
			if (!FParse::Value(CmdLineParams, TEXT("-Output="), Args.OutputPath))
			{
				UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-Output is required.");
				return {};
			}

			// Required target platform
			FString TargetPlatformName;
			if (FParse::Value(CmdLineParams, TEXT("-Platform="), TargetPlatformName))
			{
				if (const ITargetPlatform* TargetPlatform = GetTargetPlatformManagerRef().FindTargetPlatform(TargetPlatformName))
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Display, "Using target platform '{Platform}'", TargetPlatformName);
					Args.TargetPlatform = TargetPlatform;
				}
				else
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find target platfom '{Platform}'.", TargetPlatformName);
					return {};
				}
			}
			else
			{
				UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "-Platform is required.");
				return {};
			}

			return Args;
		}
	};

	class FInstallBundleResolver
	{
		TArray<TPair<FString, TArray<FRegexPattern>>> BundleRegexList;
		TMap<FString, FString> RegexMatchCache;

	public:
		FInstallBundleResolver(const TCHAR* IniPlatformName)
		{
			FConfigFile MaybeLoadedConfig;
			const FConfigFile* InstallBundleConfig = GConfig->FindOrLoadPlatformConfig(MaybeLoadedConfig, *GInstallBundleIni, IniPlatformName);
			BundleRegexList = InstallBundleUtil::LoadBundleRegexFromConfig(
				*InstallBundleConfig, InstallBundleUtil::IsPlatformInstallBundlePredicate);
		}

		FString Resolve(const FString& ChunkPattern)
		{
			FString InstallBundleName;
			if (!ChunkPattern.IsEmpty())
			{
				if (FString* CachedInstallBundleName = RegexMatchCache.Find(ChunkPattern))
				{
					InstallBundleName = *CachedInstallBundleName;
				}
				else if (InstallBundleUtil::MatchBundleRegex(BundleRegexList, ChunkPattern, InstallBundleName))
				{
					RegexMatchCache.Add(ChunkPattern, InstallBundleName);
				}
			}

			return InstallBundleName;
		}
	};

	FString GetChunkPattern(int32 Chunk)
	{
		FString ChunkPatternFormat;
		if (!GConfig->GetString(TEXT("GameFeaturePlugins"), TEXT("GFPBundleRegexMatchPatternFormat"), ChunkPatternFormat, GInstallBundleIni))
		{
			ChunkPatternFormat = TEXTVIEW("chunk{Chunk}.pak");
		}

		return FString::Format(*ChunkPatternFormat, FStringFormatNamedArguments{ {TEXT("Chunk"), Chunk} });
	}

	FString GetDevARPathForPlatform(FStringView PlatformName)
	{
		return FPaths::Combine(
			FPaths::ProjectSavedDir(), 
			TEXTVIEW("Cooked"), 
			PlatformName, 
			FApp::GetProjectName(), 
			TEXTVIEW("Metadata"), 
			TEXTVIEW("DevelopmentAssetRegistry.bin"));
	}

	FString GetDevARPath(const FArgs& Args)
	{
		if (!Args.DevARPath.IsEmpty())
		{
			return Args.DevARPath;
		}

		if (Args.TargetPlatform)
		{
			return GetDevARPathForPlatform(Args.TargetPlatform->PlatformName());
		}

		return {};
	}

	TMap<FString, int32> FindGFPChunks(const FAssetRegistryState& DevAR)
	{
		const IAssetRegistry& AR = IAssetRegistry::GetChecked();

		FARFilter RawFilter;
		RawFilter.bIncludeOnlyOnDiskAssets = true;
		RawFilter.bRecursiveClasses = true;
		RawFilter.ClassPaths.Add(UGameFeatureData::StaticClass()->GetClassPathName());

		FARCompiledFilter Filter;
		AR.CompileFilter(RawFilter, Filter);

		TMap<FString, int32> GFPChunks;

		FNameBuilder PackagePathBuilder;
		auto FindGFDChunks = [&PackagePathBuilder, &GFPChunks](const FAssetData& AssetData) -> bool
		{
			int32 ChunkId = -1;
			if (AssetData.GetChunkIDs().Num() > 0)
			{
				ChunkId = AssetData.GetChunkIDs()[0];
				if (AssetData.GetChunkIDs().Num() > 1)
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Warning, "Multiple Chunks found for {Package}, using chunk {Chunk}", AssetData.PackageName, ChunkId);
				}
			}
			AssetData.PackageName.ToString(PackagePathBuilder);
			FStringView PackageRoot = FPathViews::GetMountPointNameFromPath(PackagePathBuilder);

			GFPChunks.Emplace(PackageRoot, ChunkId);

			return true;
		};

		DevAR.EnumerateAssets(Filter, {}, FindGFDChunks);

		return GFPChunks;
	}
}

int32 UGameFeatureVersePathMapperCommandlet::Main(const FString& CmdLineParams)
{
	const TOptional<GameFeatureVersePathMapper::FArgs> MaybeArgs = GameFeatureVersePathMapper::FArgs::Parse(*CmdLineParams);
	if (!MaybeArgs)
	{
		// Parse function should print errors
		return 1;
	}
	const GameFeatureVersePathMapper::FArgs& Args = MaybeArgs.GetValue();

	FString DevArPath = GameFeatureVersePathMapper::GetDevARPath(Args);
	if (DevArPath.IsEmpty() && !FPaths::FileExists(DevArPath))
	{
		UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find development asset registry at '{Path}'", DevArPath);
		return 1;
	}

	FAssetRegistryState DevAR;
	if (!FAssetRegistryState::LoadFromDisk(*DevArPath, FAssetRegistryLoadOptions(), DevAR))
	{
		UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to load development asset registry from {Path}", DevArPath);
		return 1;
	}

	const TMap<FString, int32> GFPChunks = GameFeatureVersePathMapper::FindGFPChunks(DevAR);
	
	IPluginManager& PluginMan = IPluginManager::Get();

	struct FPluginMetadata
	{
		TSharedPtr<IPlugin> Plugin;
		FString ChunkPattern;
	};
	TMap<FString, TArray<FPluginMetadata>> PluginVersePaths;

	for (const TPair<FString, int32>& Pair : GFPChunks)
	{
		TSharedPtr<IPlugin> Plugin = PluginMan.FindPlugin(Pair.Key);
		if (!Plugin)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Warning, "Could not find uplugin {PluginName}, skipping", Pair.Key);
			continue;
		}

		if (!Plugin->GetVersePath().IsEmpty())
		{
			FPluginMetadata& Metadata = PluginVersePaths.FindOrAdd(Plugin->GetVersePath()).Emplace_GetRef();
			Metadata.Plugin = Plugin;

			// Chunk 0 or -1 will use file protocol
			if (Pair.Value > 0)
			{
				Metadata.ChunkPattern = GameFeatureVersePathMapper::GetChunkPattern(Pair.Value);
			}
		}
	}

	FVersePathGfpMap Output;

	// Build the URI List for each verse path
	GameFeatureVersePathMapper::FInstallBundleResolver InstallBundleResolver(*Args.TargetPlatform->IniPlatformName());
	for (const TPair<FString, TArray<FPluginMetadata>>& VersePathPair : PluginVersePaths)
	{
		FVersePathGfpMapEntry& OutputEntry = Output.MapEntries.Emplace_GetRef();
		OutputEntry.VersePath = VersePathPair.Key;

		for (const FPluginMetadata& Metadata : VersePathPair.Value)
		{
			// Resolve GFP URI
			const FString DescriptorFileName = FPaths::CreateStandardFilename(Metadata.Plugin->GetDescriptorFileName());

			const FString InstallBundleName = InstallBundleResolver.Resolve(Metadata.ChunkPattern);
			if (InstallBundleName.IsEmpty())
			{
				OutputEntry.GfpUriList.Add(UGameFeaturesSubsystem::GetPluginURL_FileProtocol(DescriptorFileName));
			}
			else
			{
				OutputEntry.GfpUriList.Add(UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(DescriptorFileName, InstallBundleName));
			}
		}
	}

	// Add dependency URIs in a second pass to make sure none are added if they are already in the GfpUriList
	check(PluginVersePaths.Num() == Output.MapEntries.Num());
	int32 iEntry = 0;
	for (const TPair<FString, TArray<FPluginMetadata>>& VersePathPair : PluginVersePaths)
	{
		FVersePathGfpMapEntry& OutputEntry = Output.MapEntries[iEntry++];

		for (const FPluginMetadata& Metadata : VersePathPair.Value)
		{
			for (const FPluginReferenceDescriptor& Dependency : Metadata.Plugin->GetDescriptor().Plugins)
			{
				// Currently GameFeatureSubsystem only checks bEnabled to determine if it should wait on a dependency, so match that logic here
				if (!Dependency.bEnabled)
				{	continue; }

				const int32* MaybeDepChunk = GFPChunks.Find(Dependency.Name);
				if(!MaybeDepChunk)
				{	continue; } // Dependency is not a GFP

				const int32 DepChunk = *MaybeDepChunk;
				const FString DepChunkPattern = DepChunk > 0 ? GameFeatureVersePathMapper::GetChunkPattern(DepChunk) : FString();

				const TSharedPtr<IPlugin> DepPlugin = PluginMan.FindPlugin(Dependency.Name);
				if (!DepPlugin)
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find dependency uplugin {DepPluginName} for {PluginName}, skipping", Dependency.Name, Metadata.Plugin->GetName());
					continue;
				}

				const FString DepDescriptorFileName = FPaths::CreateStandardFilename(DepPlugin->GetDescriptorFileName());

				const FString DepInstallBundleName = InstallBundleResolver.Resolve(DepChunkPattern);
				FString DepGfpUri = DepInstallBundleName.IsEmpty() ?
					UGameFeaturesSubsystem::GetPluginURL_FileProtocol(DepDescriptorFileName) : 
					UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(DepDescriptorFileName, DepInstallBundleName);

				if (!OutputEntry.GfpUriList.Contains(DepGfpUri))
				{
					OutputEntry.GfpUriDependencyList.Add(MoveTemp(DepGfpUri));
				}
			}
		}
	}


	TSharedPtr<FJsonObject> JsonObject = FJsonObjectConverter::UStructToJsonObject(Output);
	if (!JsonObject)
	{
		UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to to generate JSON");
		return 1;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Args.OutputPath));

	TUniquePtr<FArchive> FileWriter(IFileManager::Get().CreateFileWriter(*Args.OutputPath));
	TSharedRef<TJsonWriter<UTF8CHAR>> JsonWriter = TJsonWriterFactory<UTF8CHAR>::Create(FileWriter.Get());
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
	{
		UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Failed to save output file at {Path}", Args.OutputPath);
		return 1;
	}

	return 0;
}
