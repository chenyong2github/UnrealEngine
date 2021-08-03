// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/GatherShaderStatsFromMaterialsCommandlet.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ItargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogGatherShaderStatsFromMaterialsCommandlet, Log, All);

class FLogger
{
public:
	FLogger() = delete;
	FLogger(const FString& FileName)
	{
		DebugWriter = IFileManager::Get().CreateFileWriter(*FileName);
	}
	~FLogger()
	{
		DebugWriter->Close();
		delete DebugWriter;
	}

	void Log(const FString& OutString)
	{
		//UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Log, TEXT("%s"), *OutString);
		FString OuputLine = OutString + TEXT("\n");
		DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
	}

private:
	FArchive* DebugWriter = nullptr;
};

UGatherShaderStatsFromMaterialsCommandlet::UGatherShaderStatsFromMaterialsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int GetTotalShaders(const TArray<FDebugShaderTypeInfo>& OutShaderInfo)
{
	int TotalShadersForMaterial = 0;
	for (const FDebugShaderTypeInfo& ShaderInfo : OutShaderInfo)
	{
		TotalShadersForMaterial += ShaderInfo.ShaderTypes.Num();

		for (const FDebugShaderPipelineInfo& PipelineInfo : ShaderInfo.Pipelines)
		{
			TotalShadersForMaterial += PipelineInfo.ShaderTypes.Num();
		}
	}
	return TotalShadersForMaterial;
}

void PrintDebugShaderInfo(FLogger& Output, const TArray<FDebugShaderTypeInfo>& OutShaderInfo)
{
	for (const FDebugShaderTypeInfo& ShaderInfo : OutShaderInfo)
	{
		int TotalShadersForVF = 0;
		TotalShadersForVF += ShaderInfo.ShaderTypes.Num();

		for (const FDebugShaderPipelineInfo& PipelineInfo : ShaderInfo.Pipelines)
		{
			TotalShadersForVF += PipelineInfo.ShaderTypes.Num();
		}

		Output.Log(TEXT(""));
		Output.Log(FString::Printf(TEXT("\t%s - %d shaders"), ShaderInfo.VFType->GetName(), TotalShadersForVF));

		for (FShaderType* ShaderType : ShaderInfo.ShaderTypes)
		{
			Output.Log(FString::Printf(TEXT("\t\t%s"), ShaderType->GetName()));
		}

		for (const FDebugShaderPipelineInfo& PipelineInfo : ShaderInfo.Pipelines)
		{
			Output.Log(FString::Printf(TEXT("\t\t%s"), PipelineInfo.Pipeline->GetName()));

			for (FShaderType* ShaderType : PipelineInfo.ShaderTypes)
			{
				Output.Log(FString::Printf(TEXT("\t\t\t%s"), ShaderType->GetName()));
			}
		}

		Output.Log(TEXT(""));
	}
}

int ProcessMaterials(const EShaderPlatform ShaderPlatform, FLogger& Output, TArray<FAssetData>& MaterialList)
{
	int TotalShaders = 0;

	for (const FAssetData& AssetData : MaterialList)
	{
		if (UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset()))
		{
			TArray<FDebugShaderTypeInfo> OutShaderInfo;
			Material->GetShaderTypes(ShaderPlatform, OutShaderInfo);

			const int TotalShadersForMaterial = GetTotalShaders(OutShaderInfo);
			TotalShaders += TotalShadersForMaterial;

			Output.Log(TEXT(""));
			Output.Log(FString::Printf(TEXT("Material: %s - %d shaders"), *AssetData.AssetName.ToString(), TotalShadersForMaterial));

			PrintDebugShaderInfo(Output, OutShaderInfo);
		}
	}

	Output.Log(TEXT(""));
	Output.Log(TEXT("Summary"));
	Output.Log(FString::Printf(TEXT("Total Materials: %d"), MaterialList.Num()));
	Output.Log(FString::Printf(TEXT("Total Shaders: %d"), TotalShaders));

	return TotalShaders;
}

int ProcessMaterialInstances(const EShaderPlatform ShaderPlatform, FLogger& Output, TArray<FAssetData>& MaterialInstanceList)
{
	int TotalShaders = 0;

	int StaticPermutations = 0;
	for (const FAssetData& AssetData : MaterialInstanceList)
	{
		if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AssetData.GetAsset()))
		{
			TArray<FDebugShaderTypeInfo> OutShaderInfo;
			MaterialInstance->GetShaderTypes(ShaderPlatform, OutShaderInfo);

			const int TotalShadersForMaterial = GetTotalShaders(OutShaderInfo);
			TotalShaders += TotalShadersForMaterial;

			FString StaticParameterString(TEXT(""));

			if (MaterialInstance->bHasStaticPermutationResource)
			{
				const FStaticParameterSet& ParameterSet = MaterialInstance->GetStaticParameters();
				for (int32 StaticSwitchIndex = 0; StaticSwitchIndex < ParameterSet.StaticSwitchParameters.Num(); ++StaticSwitchIndex)
				{
					const FStaticSwitchParameter& StaticSwitchParameter = ParameterSet.StaticSwitchParameters[StaticSwitchIndex];
					StaticParameterString += FString::Printf(
						TEXT(", StaticSwitch'%s'=%s"),
						*StaticSwitchParameter.ParameterInfo.ToString(),
						StaticSwitchParameter.Value ? TEXT("True") : TEXT("False")
					);
				}
			}

			Output.Log(TEXT(""));
			Output.Log(FString::Printf(TEXT("Material Instance: %s - %d shaders"), *AssetData.AssetName.ToString(), TotalShadersForMaterial));
			Output.Log(FString::Printf(TEXT("Static Parameter %s"), *StaticParameterString));
			Output.Log(FString::Printf(TEXT("Parent: %s"), MaterialInstance->Parent ? *MaterialInstance->Parent->GetName() : TEXT("NO PARENT")));

			PrintDebugShaderInfo(Output, OutShaderInfo);

			if (MaterialInstance->bHasStaticPermutationResource)
			{
				StaticPermutations++;
			}
		}
	}

	Output.Log(TEXT(""));
	Output.Log(TEXT("Summary"));
	Output.Log(FString::Printf(TEXT("Total Material Instances: %d"), MaterialInstanceList.Num()));
	Output.Log(FString::Printf(TEXT("Material Instances w/ Static Permutations: %d"), StaticPermutations));
	Output.Log(FString::Printf(TEXT("Total Shaders: %d"), TotalShaders));

	return TotalShaders;
}

int32 UGatherShaderStatsFromMaterialsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Display help
	if (Switches.Contains("help"))
	{
		UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Log, TEXT("GatherShaderStatsFromMaterials"));
		UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Log, TEXT("This commandlet will dump to a human readable plain text file of all the shaders that would be compiled for all materials in a project."));
		UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Log, TEXT("Options:"));
		UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Log, TEXT(" Required: -platform=<platform>     (Which shader platform do you want results for?)"));
		UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Log, TEXT(" Optional: -collection=<name>       (You can alternatively specify a collection of assets to run this on.)"));
		return 0;
	}

	// Parse platform
	FString PlatformName;
	ITargetPlatform* TargetPlatform = nullptr;
	{
		if (!FParse::Value(*Params, TEXT("platform="), PlatformName, true))
		{
			UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Warning, TEXT("You must include a target platform with -platform=<platform>"));
			return 1;
		}
		ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
		TargetPlatform = TPM.FindTargetPlatform(PlatformName);
		if (TargetPlatform == nullptr)
		{
			UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Display, TEXT("Target platform '%s' was not found.  Valid platforms are:"), *PlatformName);
			for (ITargetPlatform* platform : TPM.GetTargetPlatforms())
			{
				UE_LOG(LogGatherShaderStatsFromMaterialsCommandlet, Display, TEXT("\t'%s'"), *platform->PlatformName());
			}
			return 1;
		}

		TargetPlatform->RefreshSettings();
	}

	TArray<FName> DesiredShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

	IAssetRegistry& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.SearchAllAssets(true);

	TArray<FAssetData> MaterialList;
	TArray<FAssetData> MaterialInstanceList;

	// Parse collection
	FString CollectionName;
	if (FParse::Value(*Params, TEXT("collection="), CollectionName, true))
	{
		if (!CollectionName.IsEmpty())
		{
			// Get the list of materials from a collection
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(TEXT("/Game")));
			Filter.bRecursivePaths = true;
			Filter.ClassNames.Add(UMaterial::StaticClass()->GetFName());

			FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();
			CollectionManagerModule.Get().GetObjectsInCollection(FName(*CollectionName), ECollectionShareType::CST_All, Filter.ObjectPaths, ECollectionRecursionFlags::SelfAndChildren);

			AssetRegistry.GetAssets(Filter, MaterialList);

			Filter.ClassNames.Empty();
			Filter.ClassNames.Add(UMaterialInstance::StaticClass()->GetFName());
			Filter.ClassNames.Add(UMaterialInstanceConstant::StaticClass()->GetFName());

			AssetRegistry.GetAssets(Filter, MaterialInstanceList);
		}
	}
	else
	{
		if (!AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetFName(), MaterialList, true);
			AssetRegistry.GetAssetsByClass(UMaterialInstance::StaticClass()->GetFName(), MaterialInstanceList, true);
		}
	}

	const double StartTime = FPlatformTime::Seconds();

	const FString TimeNow = FDateTime::Now().ToString();
	FString FileName = FPaths::Combine(*FPaths::ProjectSavedDir(), FString::Printf(TEXT("MaterialStats/ShaderStatsFromMaterials-%s.txt"), *TimeNow));

	FLogger Output(FileName);

	int TotalShaders = 0;
	int TotalAssets = 0;

	// Cache for all the shader formats that the cooking target requires
	for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
	{
		const EShaderPlatform LegacyShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

		TotalShaders += ProcessMaterials(LegacyShaderPlatform, Output, MaterialList);
		TotalAssets += MaterialList.Num();

		TotalShaders += ProcessMaterialInstances(LegacyShaderPlatform, Output, MaterialInstanceList);
		TotalAssets += MaterialInstanceList.Num();
	}

	Output.Log(TEXT(""));
	Output.Log(TEXT("Summary"));
	Output.Log(FString::Printf(TEXT("Total Assets: %d"), TotalAssets));
	Output.Log(FString::Printf(TEXT("Total Shaders: %d"), TotalShaders));

	const double EndTime = FPlatformTime::Seconds() - StartTime;
	Output.Log(TEXT(""));
	Output.Log(FString::Printf(TEXT("Commandlet Took: %lf"), EndTime));

	return true;
}
