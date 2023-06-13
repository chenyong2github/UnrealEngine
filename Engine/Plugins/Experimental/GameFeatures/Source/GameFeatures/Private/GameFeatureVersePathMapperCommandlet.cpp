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
		FInstallBundleResolver(const TCHAR* IniPlatformName = nullptr)
		{
			FConfigFile MaybeLoadedConfig;
			const FConfigFile* InstallBundleConfig = IniPlatformName ?
				GConfig->FindOrLoadPlatformConfig(MaybeLoadedConfig, *GInstallBundleIni, IniPlatformName) :
				GConfig->FindConfigFile(GInstallBundleIni);

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

	template<class EnumeratorFunc>
	TMap<FString, int32> FindGFPChunksImpl(const EnumeratorFunc& Enumerator)
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

		Enumerator(Filter, FindGFDChunks);

		return GFPChunks;
	}

	TMap<FString, int32> FindGFPChunks(const FAssetRegistryState& DevAR)
	{
		return FindGFPChunksImpl([&DevAR](const FARCompiledFilter& Filter, TFunctionRef<bool(const FAssetData&)> Callback)
		{
			DevAR.EnumerateAssets(Filter, {}, Callback);
		});
	}

	TMap<FString, int32> FindGFPChunks()
	{
		const IAssetRegistry& AR = IAssetRegistry::GetChecked();
		return FindGFPChunksImpl([&AR](const FARCompiledFilter& Filter, TFunctionRef<bool(const FAssetData&)> Callback)
		{
			AR.EnumerateAssets(Filter, Callback);
		});
	}

	class FDepthFirstPluginSorter
	{
		enum class EVisitState : uint8
		{
			None,
			Visiting,
			Visited
		};

		const TMap<FString, int32>& GFPChunks;
		TMap<TSharedPtr<IPlugin>, EVisitState> VisitedPlugins;

		bool Visit(const TSharedPtr<IPlugin>& Plugin, TArray<TSharedPtr<IPlugin>>& OutPlugins)
		{
			// Add a scope here to make sure VisitState isn't used later. It can become invalid if VisitedPlugins is resized
			{
				EVisitState& VisitState = VisitedPlugins.FindOrAdd(Plugin, EVisitState::None);
				if (VisitState == EVisitState::Visiting)
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Cycle detected in plugin dependencies with {PluginName}", Plugin->GetName());
					return false;
				}

				if (VisitState == EVisitState::Visited)
				{
					return true;
				}

				VisitState = EVisitState::Visiting;
			}

			IPluginManager& PluginMan = IPluginManager::Get();

			for (const FPluginReferenceDescriptor& Dependency : Plugin->GetDescriptor().Plugins)
			{
				// Currently GameFeatureSubsystem only checks bEnabled to determine if it should wait on a dependency, so match that logic here
				if (!Dependency.bEnabled)
				{
					continue;
				}

				if (!GFPChunks.Contains(Dependency.Name))
				{
					continue;
				} // Dependency is not a GFP

				const TSharedPtr<IPlugin> DepPlugin = PluginMan.FindPlugin(Dependency.Name);
				if (!DepPlugin)
				{
					UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find dependency uplugin {DepPluginName} for {PluginName}, skipping", Dependency.Name, Plugin->GetName());
					return false;
				}

				if (!Visit(DepPlugin, OutPlugins))
				{
					return false;
				}
			}

			VisitedPlugins.Add(Plugin, EVisitState::Visited);
			OutPlugins.Add(Plugin);
			return true;
		};

	public:
		// InGFPChunks is used to determine if dependencies are actually GFPs
		// Non-GFP dependencies are ignored
		FDepthFirstPluginSorter(const TMap<FString, int32>& InGFPChunks) : GFPChunks(InGFPChunks) {}

		bool Sort(const TArray<TSharedPtr<IPlugin>>& RootPlugins, TArray<TSharedPtr<IPlugin>>& OutPlugins)
		{
			for (const TSharedPtr<IPlugin>& RootPlugin : RootPlugins)
			{
				if (!Visit(RootPlugin, OutPlugins))
				{
					return false;
				}
			}
			return true;
		}
	};
}

TOptional<TMap<FString, TArray<FString>>> UGameFeatureVersePathMapperCommandlet::BuildLookup(
	const ITargetPlatform* TargetPlatform /*= nullptr*/, const FAssetRegistryState* DevAR /*= nullptr*/)
{
	TOptional<TMap<FString, TArray<FString>>> Output;

	const TMap<FString, int32> GFPChunks = DevAR ?
		GameFeatureVersePathMapper::FindGFPChunks(*DevAR) :
		GameFeatureVersePathMapper::FindGFPChunks();

	IPluginManager& PluginMan = IPluginManager::Get();

	TMap<FString, TArray<TSharedPtr<IPlugin>>> PluginRootSets;

	// Add the root plugins for each verse paths
	for (const TPair<FString, int32>& Pair : GFPChunks)
	{
		TSharedPtr<IPlugin> Plugin = PluginMan.FindPlugin(Pair.Key);
		if (!Plugin)
		{
			UE_LOGFMT(LogGameFeatureVersePathMapper, Error, "Could not find uplugin {PluginName}, skipping", Pair.Key);
			return Output;
		}

		if (!Plugin->GetVersePath().IsEmpty())
		{
			PluginRootSets.FindOrAdd(Plugin->GetVersePath()).Add(Plugin);
		}
	}

	// Discover and sort all dependencies
	TMap<FString, TArray<TSharedPtr<IPlugin>>> SortedPluginSets;
	SortedPluginSets.Reserve(PluginRootSets.Num());
	for (const TPair<FString, TArray<TSharedPtr<IPlugin>>>& RootSetPair : PluginRootSets)
	{
		TArray<TSharedPtr<IPlugin>>& SortedPlugins = SortedPluginSets.Add(RootSetPair.Key);

		GameFeatureVersePathMapper::FDepthFirstPluginSorter Sorter(GFPChunks);
		if (!Sorter.Sort(RootSetPair.Value, SortedPlugins))
		{
			return Output;
		}
	}

	Output.Emplace();
	Output->Reserve(SortedPluginSets.Num());

	// Create URIs for each GFP
	GameFeatureVersePathMapper::FInstallBundleResolver InstallBundleResolver(TargetPlatform ? *TargetPlatform->IniPlatformName() : nullptr);
	for (const TPair<FString, TArray<TSharedPtr<IPlugin>>>& SortedPair : SortedPluginSets)
	{
		TArray<FString>& UriList = Output->Add(SortedPair.Key);
		UriList.Reserve(SortedPair.Value.Num());

		for (const TSharedPtr<IPlugin>& Plugin : SortedPair.Value)
		{
			const FString DescriptorFileName = FPaths::CreateStandardFilename(Plugin->GetDescriptorFileName());

			const int32 Chunk = GFPChunks.FindChecked(Plugin->GetName()); // Must exist
			const FString ChunkPattern = Chunk > 0 ? GameFeatureVersePathMapper::GetChunkPattern(Chunk) : FString();
			const FString InstallBundleName = InstallBundleResolver.Resolve(ChunkPattern);
			if (InstallBundleName.IsEmpty())
			{
				UriList.Add(UGameFeaturesSubsystem::GetPluginURL_FileProtocol(DescriptorFileName));
			}
			else
			{
				UriList.Add(UGameFeaturesSubsystem::GetPluginURL_InstallBundleProtocol(DescriptorFileName, InstallBundleName));
			}
		}
	}

	return Output;
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

	FJsonVersePathGfpMap Output;
	{
		TOptional<TMap<FString, TArray<FString>>> MaybeLookup = BuildLookup(Args.TargetPlatform, &DevAR);
		if (!MaybeLookup)
		{
			// BuildLookup will emit errors
			return 1;
		}

		Output.MapEntries.Reserve(MaybeLookup->Num());
		for(TPair<FString, TArray<FString>>& VersePathPair : *MaybeLookup)
		{
			FJsonVersePathGfpMapEntry& OutputEntry = Output.MapEntries.Emplace_GetRef();
			OutputEntry.VersePath = VersePathPair.Key;
			OutputEntry.GfpUriList = MoveTemp(VersePathPair.Value);
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
