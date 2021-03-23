// Copyright Epic Games, Inc. All Rights Reserved.

#include "RefAssetReferenceFilter.h"
#include "Toolkits/ToolkitManager.h"
#include "Toolkits/IToolkit.h"
#include "Engine/AssetManager.h"
#include "Interfaces/IPluginManager.h"
#include "AssetRegistryModule.h"
#include "AssetReferencingDomains.h"

#define LOCTEXT_NAMESPACE "AssetReferencingPolicy_ZZZ"

TArray<FString> RestrictedFolders = { TEXT("/Game/Cinematics/"), TEXT("/Game/Developers/"), TEXT("/Game/NeverCook/") };
TArray<FString> TestMapsFolders = { TEXT("/Game/Maps/Test_Maps/"), TEXT("/Game/Athena/Maps/Test/"), TEXT("/Game/Athena/Apollo/Maps/Test/") };

static bool UFortEditorValidator_IsInUncookedFolder(const FString& PackageName, FString* OutUncookedFolderName = nullptr)
{
	for (const FString& RestrictedFolder : RestrictedFolders)
	{
		if (PackageName.StartsWith(RestrictedFolder))
		{
			if (OutUncookedFolderName)
			{
				FString FolderToReport = RestrictedFolder.StartsWith(TEXT("/Game/")) ? RestrictedFolder.RightChop(6) : RestrictedFolder;
				if (FolderToReport.EndsWith(TEXT("/")))
				{
					*OutUncookedFolderName = FolderToReport.LeftChop(1);
				}
				else
				{
					*OutUncookedFolderName = FolderToReport;
				}
			}
			return true;
		}
	}

	return false;

}

static bool UFortEditorValidator_IsInTestMapsFolder(const FString& PackageName, FString* OutTestMapsFolderName = nullptr)
{
	for (const FString& TestMapsFolder : TestMapsFolders)
	{
		if (PackageName.StartsWith(TestMapsFolder))
		{
			if (OutTestMapsFolderName)
			{
				FString FolderToReport = TestMapsFolder.StartsWith(TEXT("/Game/")) ? TestMapsFolder.RightChop(6) : TestMapsFolder;
				if (FolderToReport.EndsWith(TEXT("/")))
				{
					*OutTestMapsFolderName = FolderToReport.LeftChop(1);
				}
				else
				{
					*OutTestMapsFolderName = TestMapsFolder;
				}
			}
			return true;
		}
	}

	return false;
}



FRefAssetReferenceFilter::FRefAssetReferenceFilter(const FAssetReferenceFilterContext& Context)
	: IAssetReferenceFilter()
	, EnginePath(TEXT("/Engine/"))
	, GamePath(TEXT("/Game/"))
	, TempPath(TEXT("/Temp/"))
	, ScriptPath(TEXT("/Script/"))
	, ScriptEnginePath(TEXT("/Script/Engine"))
	, ScriptGamePath(TEXT("/Script/FortniteGame"))
	, EngineTransientPackageName(TEXT("/Engine/Transient"))
	, ReferencingAssetLayer(EReferencingLayer::AllowAll)
	, bAllowAssetsInRestrictedFolders(true)
{
	QUICK_SCOPE_CYCLE_COUNTER(QQQ_Reference_Ctor);

	// Populate AllGameFeaturePluginPaths
	{
		TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
		FString BuiltInGameFeaturePluginsFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() + TEXT("GameFeatures/"));
		for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
		{
			const FString& PluginDescriptorFilename = Plugin->GetDescriptorFileName();
			if (!PluginDescriptorFilename.IsEmpty() && FPaths::ConvertRelativePathToFull(PluginDescriptorFilename).StartsWith(BuiltInGameFeaturePluginsFolder))
			{
				const FString GameFeaturePluginContentRoot = FString::Printf(TEXT("/%s/"), *FPaths::GetBaseFilename(PluginDescriptorFilename));
				AllGameFeaturePluginPaths.Add(GameFeaturePluginContentRoot);
			}
		}
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	// Determine the referencing layer (and if all assets are in restricted folders)
	TArray<FAssetData> DerivedReferencingAssets;
	for (const FAssetData& ReferencingAsset : Context.ReferencingAssets)
	{
		if (ReferencingAsset.IsRedirector())
		{
			// Skip redirectors if they are themselves unreferenced
			TArray<FName> RedirectorRefs;
			AssetRegistryModule.Get().GetReferencers(ReferencingAsset.PackageName, RedirectorRefs);
			if (RedirectorRefs.Num() == 0)
			{
				continue;
			}
		}
		ProcessReferencingAsset(ReferencingAsset, DerivedReferencingAssets);
		if (HasMostRestrictiveFilter())
		{
			// No reason to keep iterating, we have the most restrictive filter
			break;
		}
	}

	if (!HasMostRestrictiveFilter())
	{
		for (const FAssetData& ReferencingAsset : DerivedReferencingAssets)
		{
			// @todo possibly support even more derived assets recursively? Needs loop detection
			TArray<FAssetData> Unused;
			ProcessReferencingAsset(ReferencingAsset, Unused);
			if (HasMostRestrictiveFilter())
			{
				// No reason to keep iterating, we have the most restrictive filter
				break;
			}
		}
	}

	FString GameFeaturePluginAllowedPluginPathsDisplayString = ReferencingAssetPluginPath.LeftChop(1);
	// CrossPluginAllowedReferences. Populated by plugin dependencies.
	if (ReferencingAssetLayer == EReferencingLayer::Plugin || ReferencingAssetLayer == EReferencingLayer::GameFeaturePlugin)
	{
		// Trim the leading and trailing slash for the name (i.e. /MyPlugin/ -> MyPlugin)
		FString PluginName = ReferencingAssetPluginPath.LeftChop(1).RightChop(1);

		// Populate cross plugin allowed refs
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		for (const FPluginReferenceDescriptor& Dependencies : Plugin->GetDescriptor().Plugins)
		{
			if (Dependencies.bEnabled)
			{
				const FString DependencyPath = FString::Printf(TEXT("/%s/"), *Dependencies.Name);
				CrossPluginAllowedReferences.Add(DependencyPath);

				if (AllGameFeaturePluginPaths.Contains(DependencyPath))
				{
					GameFeaturePluginAllowedPluginPathsDisplayString += TEXT(", ") + DependencyPath.LeftChop(1);
				}
			}
		}
	}

	// Filling out the Plugin
	Failure_RestrictedFolder = LOCTEXT("FailureRestrictedFolder", "You cannot reference assets in {0} here. It is a restricted folder.");
	Failure_Engine = LOCTEXT("FailureEngine", "You may only reference assets from /Engine here.");
	Failure_Game = LOCTEXT("FailureGame", "You may only reference assets from /Engine, /Game, and non-GameFeature plugins here.");
	Failure_GameFeaturePlugin = FText::Format(LOCTEXT("FailureGameFeaturePlugin", "You may only reference assets from /Engine, /Game, {0}, and non-GameFeature plugins here."), FText::FromString(GameFeaturePluginAllowedPluginPathsDisplayString));
	Failure_Plugin = FText::Format(LOCTEXT("FailurePlugin", "You may only reference assets from /Engine, {0}, or any of that plugin's dependencies here."), FText::FromString(ReferencingAssetPluginPath.LeftChop(1)));
}

bool FRefAssetReferenceFilter::PassesFilter(const FAssetData& AssetData, FText* OutOptionalFailureReason) const
{
	QUICK_SCOPE_CYCLE_COUNTER(QQQ_Reference_PassesFilter);

	const FString ReferencedAssetPath = AssetData.PackageName.ToString();
	if (!bAllowAssetsInRestrictedFolders)
	{
		if (OutOptionalFailureReason)
		{
			FString FolderName;
			if (UFortEditorValidator_IsInUncookedFolder(ReferencedAssetPath, &FolderName))
			{
				*OutOptionalFailureReason = FText::Format(Failure_RestrictedFolder, FText::FromString(FolderName));
				return false;
			}
		}
		else
		{
			if (UFortEditorValidator_IsInUncookedFolder(ReferencedAssetPath))
			{
				return false;
			}
		}
	}

	switch (ReferencingAssetLayer)
	{
	case EReferencingLayer::Engine:
		if (!ReferencedAssetPath.StartsWith(EnginePath) && !ReferencedAssetPath.Equals(ScriptEnginePath))
		{
			if (OutOptionalFailureReason)
			{
				*OutOptionalFailureReason = Failure_Engine;
			}
			return false;
		}
		break;
	case EReferencingLayer::Game:
	case EReferencingLayer::GameFeaturePlugin:
	{
		bool bIsIllegalGameFeaturePluginPath = false;
		for (const FString& GameFeaturePluginPath : AllGameFeaturePluginPaths)
		{
			if (ReferencedAssetPath.StartsWith(GameFeaturePluginPath) && (ReferencingAssetLayer == EReferencingLayer::Game || !ReferencedAssetPath.StartsWith(ReferencingAssetPluginPath)))
			{
				bool bIsCrossPluginRefThatIsAllowed = false;
				if (ReferencingAssetLayer == EReferencingLayer::GameFeaturePlugin)
				{
					for (const FString& Plugin : CrossPluginAllowedReferences)
					{
						if (ReferencedAssetPath.StartsWith(Plugin))
						{
							bIsCrossPluginRefThatIsAllowed = true;
							break;
						}
					}
				}
				if (!bIsCrossPluginRefThatIsAllowed)
				{
					bIsIllegalGameFeaturePluginPath = true;
				}
				break;
			}
		}
		if (bIsIllegalGameFeaturePluginPath)
		{
			if (OutOptionalFailureReason)
			{
				*OutOptionalFailureReason = (ReferencingAssetLayer == EReferencingLayer::GameFeaturePlugin) ? Failure_GameFeaturePlugin : Failure_Game;
			}
			return false;
		}
	}
	break;
	case EReferencingLayer::Plugin:
		if (!ReferencedAssetPath.StartsWith(EnginePath) && !ReferencedAssetPath.Equals(ScriptEnginePath) && !ReferencedAssetPath.StartsWith(GamePath) && !ReferencedAssetPath.Equals(ScriptGamePath) && !ReferencedAssetPath.StartsWith(ReferencingAssetPluginPath))
		{
			bool bIsCrossPluginRefThatIsAllowed = false;
			for (const FString& Plugin : CrossPluginAllowedReferences)
			{
				if (ReferencedAssetPath.StartsWith(Plugin))
				{
					bIsCrossPluginRefThatIsAllowed = true;
					break;
				}
			}
			if (!bIsCrossPluginRefThatIsAllowed)
			{
				if (OutOptionalFailureReason)
				{
					*OutOptionalFailureReason = Failure_Plugin;
				}
				return false;
			}
		}
		break;
	case EReferencingLayer::AllowAll:
	default:
		break;
	}

	return true;
}

bool FRefAssetReferenceFilter::HasMostRestrictiveFilter() const
{
	return ReferencingAssetLayer == EReferencingLayer::Engine && !bAllowAssetsInRestrictedFolders;
}

void FRefAssetReferenceFilter::ProcessReferencingAsset(const FAssetData& ReferencingAsset, TArray<FAssetData>& OutDerivedReferencingAssets)
{
	const FString ReferencingAssetPath = ReferencingAsset.PackageName.ToString();
	if (ReferencingAssetPath.StartsWith(EnginePath))
	{
		if (ReferencingAsset.PackageName == EngineTransientPackageName)
		{
			// Check if this is possibly a preview material in a material editor
			FAssetData NonPreviewAsset;
			if (GetAssetDataFromPossiblyPreviewObject(ReferencingAsset, NonPreviewAsset))
			{
				OutDerivedReferencingAssets.Add(NonPreviewAsset);
			}
		}
		else
		{
			ReferencingAssetLayer = EReferencingLayer::Engine;
			bAllowAssetsInRestrictedFolders = false;
		}
	}
	else
	{
		check((uint8)ReferencingAssetLayer >= (uint8)EReferencingLayer::Game);
		if (ReferencingAssetPath.StartsWith(GamePath))
		{
			if ((bAllowAssetsInRestrictedFolders || ReferencingAssetLayer != EReferencingLayer::Game) && (!UFortEditorValidator_IsInUncookedFolder(ReferencingAssetPath) && !UFortEditorValidator_IsInTestMapsFolder(ReferencingAssetPath)))
			{
				ReferencingAssetLayer = EReferencingLayer::Game;
				bAllowAssetsInRestrictedFolders = false;
			}
		}
		else if (ReferencingAssetLayer == EReferencingLayer::Plugin || ReferencingAssetLayer == EReferencingLayer::GameFeaturePlugin)
		{
			if (!ReferencingAssetPath.StartsWith(ReferencingAssetPluginPath))
			{
				// Multiple plugins. Only allow references to the game layer
				ReferencingAssetLayer = EReferencingLayer::Game;
			}
			bAllowAssetsInRestrictedFolders = false;
		}
		else if (ReferencingAssetLayer == EReferencingLayer::AllowAll)
		{
			// /Temp and /Script are not plugins
			if (!ReferencingAssetPath.StartsWith(TempPath) && !ReferencingAssetPath.StartsWith(ScriptPath))
			{
				// Plugin
				check(ReferencingAssetPluginPath.IsEmpty());
				if (UAssetManager::GetContentRootPathFromPackageName(ReferencingAssetPath, ReferencingAssetPluginPath))
				{
					if (AllGameFeaturePluginPaths.Contains(ReferencingAssetPluginPath))
					{
						ReferencingAssetLayer = EReferencingLayer::GameFeaturePlugin;
					}
					else
					{
						ReferencingAssetLayer = EReferencingLayer::Plugin;
					}
				}

				bAllowAssetsInRestrictedFolders = false;
			}
		}
		else
		{
			check(false); // Unknown layer
		}
	}
}

bool FRefAssetReferenceFilter::GetAssetDataFromPossiblyPreviewObject(const FAssetData& PossiblyPreviewObject, FAssetData& OriginalAsset) const
{
	UObject* Obj = PossiblyPreviewObject.IsAssetLoaded() ? PossiblyPreviewObject.GetAsset() : nullptr;

	// Find the object within the outermost upackage
	while (Obj && Obj->GetOuter() && !Obj->GetOuter()->IsA(UPackage::StaticClass()))
	{
		Obj = Obj->GetOuter();
	}

	if (Obj)
	{
		TSharedPtr<IToolkit> FoundToolkit = FToolkitManager::Get().FindEditorForAsset(Obj);
		if (FoundToolkit.IsValid())
		{
			if (const TArray<UObject*>* EditedObjects = FoundToolkit->GetObjectsCurrentlyBeingEdited())
			{
				for (UObject* EditedObject : *EditedObjects)
				{
					if ((EditedObject != Obj) && (EditedObject->GetOutermost() != GetTransientPackage()))
					{
						// Found an asset from this toolkit that was not the preview object, use it instead.
						OriginalAsset = FAssetData(EditedObject);
						return true;
					}
				}
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
