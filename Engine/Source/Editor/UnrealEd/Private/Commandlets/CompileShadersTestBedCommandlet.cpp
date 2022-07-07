// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/CompileShadersTestBedCommandlet.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CollectionManagerModule.h"
#include "CollectionManagerTypes.h"
#include "GlobalShader.h"
#include "ICollectionManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ShaderCompiler.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompileShadersTestBedCommandlet, Log, All);

UCompileShadersTestBedCommandlet::UCompileShadersTestBedCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UCompileShadersTestBedCommandlet::Main(const FString& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCompileShadersTestBedCommandlet::Main);

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Display help
	if (Switches.Contains("help"))
	{
		UE_LOG(LogCompileShadersTestBedCommandlet, Log, TEXT("CompileShadersTestBed"));
		UE_LOG(LogCompileShadersTestBedCommandlet, Log, TEXT("This commandlet compiles global and default material shaders.  Used to profile and test shader compilation."));
		UE_LOG(LogCompileShadersTestBedCommandlet, Log, TEXT(" Optional: -collection=<name>                (You can also specify a collection of assets to narrow down the results e.g. if you maintain a collection that represents the actually used in-game assets)."));
		UE_LOG(LogCompileShadersTestBedCommandlet, Log, TEXT(" Optional: -materials=<path1>+<path2>        (You can also specify a list of material asset paths separated by a '+' to narrow down the results."));
		return 0;
	}

	PRIVATE_GAllowCommandletRendering = true;

	IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.SearchAllAssets(true);

	// Optional list of materials to compile.
	TArray<FAssetData> MaterialList;

	FARFilter Filter;

	// Parse collection
	FString CollectionName;
	if (FParse::Value(*Params, TEXT("collection="), CollectionName, true))
	{
		if (!CollectionName.IsEmpty())
		{
			// Get the list of materials from a collection
			Filter.PackagePaths.Add(FName(TEXT("/Game")));
			Filter.bRecursivePaths = true;
			Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());

			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
			CollectionManagerModule.Get().GetObjectsInCollection(FName(*CollectionName), ECollectionShareType::CST_All, Filter.ObjectPaths, ECollectionRecursionFlags::SelfAndChildren);

			AssetRegistry.GetAssets(Filter, MaterialList);

			Filter.ClassPaths.Empty();
			Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());

			AssetRegistry.GetAssets(Filter, MaterialList);
		}
	}

	// Process -materials= switches separated by a '+'
	TArray<FString> CmdLineMaterialEntries;
	const TCHAR* MaterialsSwitchName = TEXT("Materials");
	if (const FString* MaterialsSwitches = ParamVals.Find(MaterialsSwitchName))
	{
		MaterialsSwitches->ParseIntoArray(CmdLineMaterialEntries, TEXT("+"));
	}

	if (CmdLineMaterialEntries.Num())
	{
		// re-use the filter and only filter based on the passed in objects.
		Filter.ClassPaths.Empty();
		Filter.ObjectPaths.Empty();
		for (const FString& MaterialPath : CmdLineMaterialEntries)
		{
			const FName MaterialPathFName(MaterialPath);
			if (!Filter.ObjectPaths.Contains(MaterialPathFName))
			{
				Filter.ObjectPaths.Add(MaterialPathFName);
			}
		}

		AssetRegistry.GetAssets(Filter, MaterialList);
	}

	// For all active platforms
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

	for (ITargetPlatform* Platform : Platforms)
	{
		// Compile default materials
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DefaultMaterials);

			for (int32 Domain = 0; Domain < MD_MAX; ++Domain)
			{
				UMaterial::GetDefaultMaterial(static_cast<EMaterialDomain>(Domain))->BeginCacheForCookedPlatformData(Platform);
			}
		}

		// Compile global shaders
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GlobalShaders);

			TArray<FName> DesiredShaderFormats;
			Platform->GetAllTargetedShaderFormats(DesiredShaderFormats);

			for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
			{
				const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);
				CompileGlobalShaderMap(ShaderPlatform, Platform, false);
			}
		}

		// Compile material shaders specified on the command line
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MaterialShaders);

			// Sort the material lists by name so the order is stable.
			Algo::SortBy(MaterialList, [](const FAssetData& AssetData) { return AssetData.ObjectPath; }, FNameLexicalLess());

			for (const FAssetData& AssetData : MaterialList)
			{
				if (UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset()))
				{
					Material->BeginCacheForCookedPlatformData(Platform);
				}
				else if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AssetData.GetAsset()))
				{
					MaterialInstance->BeginCacheForCookedPlatformData(Platform);
				}
			}
		}
	}

	// Block on all the jobs submitted above.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(BlockOnShaderCompiles);

		GShaderCompilingManager->FinishAllCompilation();
	}

	// Perform cleanup and clear cached data for cooking.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ClearCachedCookedPlatformData);

		for (const FAssetData& AssetData : MaterialList)
		{
			if (UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset()))
			{
				Material->ClearAllCachedCookedPlatformData();
			}
			else if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AssetData.GetAsset()))
			{
				MaterialInstance->ClearAllCachedCookedPlatformData();
			}
		}
	}

	return 0;
}
