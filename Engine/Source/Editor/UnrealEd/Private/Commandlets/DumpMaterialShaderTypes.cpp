// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/DumpMaterialShaderTypes.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogDumpMaterialShaderTypesCommandlet, Log, All);

class FShaderStatsGatheringContext
{
public:
	FShaderStatsGatheringContext() = delete;
	FShaderStatsGatheringContext(const FString& FileName)
	{
		DebugWriter = IFileManager::Get().CreateFileWriter(*FileName);
	}
	~FShaderStatsGatheringContext()
	{
		DebugWriter->Close();
		delete DebugWriter;
	}

	void AddToHistogram(const TCHAR* VertexFactoryName, const TCHAR* ShaderPipelineName, const TCHAR* ShaderTypeName)
	{
		FString ShaderType(ShaderTypeName);
		if (int32* Existing = ShaderTypeHistogram.Find(ShaderType))
		{
			++(*Existing);
		}
		else
		{
			ShaderTypeHistogram.FindOrAdd(ShaderType, 1);
		}

#if 0	// the output of the full list is spammy and not usable. Needs to be replaced by a [Type x VF] matrix probably
		FString AbsoluteShaderName = (ShaderPipelineName != nullptr) ? FString::Printf(TEXT("%s.%s.%s"), VertexFactoryName, ShaderPipelineName, ShaderTypeName) : FString::Printf(TEXT("%s.%s"), VertexFactoryName, ShaderTypeName);
		if (int32* Existing = FullShaderTypeHistogram.Find(AbsoluteShaderName))
		{
			++(*Existing);
		}
		else
		{
			FullShaderTypeHistogram.FindOrAdd(AbsoluteShaderName, 1);
		}
#endif // 0

		if (VertexFactoryName)
		{
			FString VFTypeName(VertexFactoryName);
			if (int32* Existing = VertexFactoryTypeHistogram.Find(VFTypeName))
			{
				++(*Existing);
			}
			else
			{
				VertexFactoryTypeHistogram.FindOrAdd(VFTypeName, 1);
			}
		}
	}

	void PrintHistogram(int TotalShaders)
	{
		if (ShaderTypeHistogram.Num() > 0)
		{
			{
				ShaderTypeHistogram.ValueSort(TGreater<int32>());
				const char ShaderTypeHeader[] = "\nSorted by count:\nShaderType, Count, Percent Total\n";
				DebugWriter->Serialize(const_cast<char*>(ShaderTypeHeader), sizeof(ShaderTypeHeader) - 1);
				for (TPair<FString, int32> ShaderUsage : ShaderTypeHistogram)
				{
					FString OuputLine = FString::Printf(TEXT("%s, %d, %.2f\n"), *ShaderUsage.Key, ShaderUsage.Value, (ShaderUsage.Value / (float)TotalShaders) * 100.0f);
					DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
				}
			}

			// sort one more time, alphabetically for easier comparison, and print again
			{
				ShaderTypeHistogram.KeySort(TLess<FString>());
				const char ShaderTypeHeader[] = "\nSorted by shader type:\nShaderType, Count, Percent Total\n";
				DebugWriter->Serialize(const_cast<char*>(ShaderTypeHeader), sizeof(ShaderTypeHeader) - 1);
				for (TPair<FString, int32> ShaderUsage : ShaderTypeHistogram)
				{
					FString OuputLine = FString::Printf(TEXT("%s, %d, %.2f\n"), *ShaderUsage.Key, ShaderUsage.Value, (ShaderUsage.Value / (float)TotalShaders) * 100.0f);
					DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
				}
			}
		}

		if (FullShaderTypeHistogram.Num() > 0)
		{
			FullShaderTypeHistogram.ValueSort(TGreater<int32>());
			const char FullShaderTypeHeader[] = "\nFullShaderType, Count, Percent Total\n";
			DebugWriter->Serialize(const_cast<char*>(FullShaderTypeHeader), sizeof(FullShaderTypeHeader) - 1);
			for (TPair<FString, int32> ShaderUsage : FullShaderTypeHistogram)
			{
				FString OuputLine = FString::Printf(TEXT("%s, %d, %.2f\n"), *ShaderUsage.Key, ShaderUsage.Value, (ShaderUsage.Value / (float)TotalShaders) * 100.0f);
				DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
			}
		}

		if (VertexFactoryTypeHistogram.Num() > 0)
		{
			{
				VertexFactoryTypeHistogram.ValueSort(TGreater<int32>());
				const char FullVFTypeHeader[] = "\nSorted by count:\nVFType, Count, Percent Total\n";
				DebugWriter->Serialize(const_cast<char*>(FullVFTypeHeader), sizeof(FullVFTypeHeader) - 1);
				for (TPair<FString, int32> VFTypeUsage : VertexFactoryTypeHistogram)
				{
					FString OuputLine = FString::Printf(TEXT("%s, %d, %.2f\n"), *VFTypeUsage.Key, VFTypeUsage.Value, (VFTypeUsage.Value / (float)TotalShaders) * 100.0f);
					DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
				}
			}

			// sort one more time, alphabetically for easier comparison, and print again
			{
				VertexFactoryTypeHistogram.KeySort(TLess<FString>());
				const char FullVFTypeHeader[] = "\nSorted by VF:\nVFType, Count, Percent Total\n";
				DebugWriter->Serialize(const_cast<char*>(FullVFTypeHeader), sizeof(FullVFTypeHeader) - 1);
				for (TPair<FString, int32> VFTypeUsage : VertexFactoryTypeHistogram)
				{
					FString OuputLine = FString::Printf(TEXT("%s, %d, %.2f\n"), *VFTypeUsage.Key, VFTypeUsage.Value, (VFTypeUsage.Value / (float)TotalShaders) * 100.0f);
					DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
				}
			}
		}
	}

	void PrintAlphabeticList()
	{
		if (ShaderTypeHistogram.Num() > 0)
		{
			ShaderTypeHistogram.KeySort(TLess<FString>());
			const char ShaderTypeAlphabeticHeader[] = "\nShaderType only\n";
			DebugWriter->Serialize(const_cast<char*>(ShaderTypeAlphabeticHeader), sizeof(ShaderTypeAlphabeticHeader) - 1);
			for (TPair<FString, int32> ShaderUsage : ShaderTypeHistogram)
			{
				// do not print numbers here as it complicates the diff
				FString OuputLine = FString::Printf(TEXT("%s\n"), *ShaderUsage.Key);
				DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
			}
		}

		if (FullShaderTypeHistogram.Num() > 0)
		{
			FullShaderTypeHistogram.KeySort(TLess<FString>());
			const char FullShaderTypeAlphabeticHeader[] = "\nFullShaderType only\n";
			DebugWriter->Serialize(const_cast<char*>(FullShaderTypeAlphabeticHeader), sizeof(FullShaderTypeAlphabeticHeader) - 1);
			for (TPair<FString, int32> ShaderUsage : FullShaderTypeHistogram)
			{
				// do not print numbers here as it complicates the diff
				FString OuputLine = FString::Printf(TEXT("%s\n"), *ShaderUsage.Key);
				DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
			}
		}

		if (VertexFactoryTypeHistogram.Num() > 0)
		{
			VertexFactoryTypeHistogram.KeySort(TLess<FString>());
			const char FullVFTypeAlphabeticHeader[] = "\nVertexFactoryType only\n";
			DebugWriter->Serialize(const_cast<char*>(FullVFTypeAlphabeticHeader), sizeof(FullVFTypeAlphabeticHeader) - 1);
			for (TPair<FString, int32> VFTypeUsage : VertexFactoryTypeHistogram)
			{
				// do not print numbers here as it complicates the diff
				FString OuputLine = FString::Printf(TEXT("%s\n"), *VFTypeUsage.Key);
				DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
			}
		}
	}

	void Log(const FString& OutString)
	{
		//UE_LOG(LogDumpMaterialShaderTypesCommandlet, Log, TEXT("%s"), *OutString);
		FString OuputLine = OutString + TEXT("\n");
		DebugWriter->Serialize(const_cast<ANSICHAR*>(StringCast<ANSICHAR>(*OuputLine).Get()), OuputLine.Len());
	}

private:
	FArchive* DebugWriter = nullptr;

	/** Map of shader type names (no matter the vertex factory) to their counts. */
	TMap<FString, int32>	ShaderTypeHistogram;

	/** Map of full shader display names to their counts. */
	TMap<FString, int32>	FullShaderTypeHistogram;

	/** Map of vertex factory display names to their counts. */
	TMap<FString, int32>	VertexFactoryTypeHistogram;
};

UDumpMaterialShaderTypesCommandlet::UDumpMaterialShaderTypesCommandlet(const FObjectInitializer& ObjectInitializer)
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

void PrintDebugShaderInfo(FShaderStatsGatheringContext& Output, const TArray<FDebugShaderTypeInfo>& OutShaderInfo)
{
	for (const FDebugShaderTypeInfo& ShaderInfo : OutShaderInfo)
	{
		Output.Log(TEXT(""));

		// FMeshMaterialShader
		if (ShaderInfo.VFType)
		{
			int TotalShadersForVF = 0;
			TotalShadersForVF += ShaderInfo.ShaderTypes.Num();

			for (const FDebugShaderPipelineInfo& PipelineInfo : ShaderInfo.Pipelines)
			{
				TotalShadersForVF += PipelineInfo.ShaderTypes.Num();
			}

			Output.Log(FString::Printf(TEXT("\t%s - %d shaders"), ShaderInfo.VFType->GetName(), TotalShadersForVF));

			for (FShaderType* ShaderType : ShaderInfo.ShaderTypes)
			{
				Output.Log(FString::Printf(TEXT("\t\t%s"), ShaderType->GetName()));
				Output.AddToHistogram(ShaderInfo.VFType->GetName(), nullptr, ShaderType->GetName());
			}

			for (const FDebugShaderPipelineInfo& PipelineInfo : ShaderInfo.Pipelines)
			{
				Output.Log(FString::Printf(TEXT("\t\t%s"), PipelineInfo.Pipeline->GetName()));

				for (FShaderType* ShaderType : PipelineInfo.ShaderTypes)
				{
					Output.Log(FString::Printf(TEXT("\t\t\t%s"), ShaderType->GetName()));
					Output.AddToHistogram(ShaderInfo.VFType->GetName(), PipelineInfo.Pipeline->GetName(), ShaderType->GetName());
				}
			}
		}
		// FMaterialShader
		else
		{
			check(ShaderInfo.Pipelines.Num() == 0);

			TMap<FString, int32> ShaderTypeMap;
			for (FShaderType* ShaderType : ShaderInfo.ShaderTypes)
			{
				FString ShaderTypeName(ShaderType->GetName());
				if (int32* Existing = ShaderTypeMap.Find(ShaderTypeName))
				{
					++(*Existing);
				}
				else
				{
					ShaderTypeMap.FindOrAdd(ShaderTypeName, 1);
				}

				Output.AddToHistogram(nullptr, nullptr, ShaderType->GetName());
			}

			if (ShaderTypeMap.Num() > 0)
			{
				ShaderTypeMap.ValueSort(TGreater<int32>());
				for (TPair<FString, int32> ShaderTypeSum : ShaderTypeMap)
				{
					Output.Log(FString::Printf(TEXT("\t%s - %d shaders"), *ShaderTypeSum.Key, ShaderTypeSum.Value));
				}
			}
		}

		Output.Log(TEXT(""));
	}
}

int ProcessMaterials(const ITargetPlatform* TargetPlatform, const EShaderPlatform ShaderPlatform, FShaderStatsGatheringContext& Output, TArray<FAssetData>& MaterialList)
{
	int TotalShaders = 0;

	for (const FAssetData& AssetData : MaterialList)
	{
		if (UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset()))
		{
			TArray<FDebugShaderTypeInfo> OutShaderInfo;
			Material->GetShaderTypes(ShaderPlatform, TargetPlatform, OutShaderInfo);

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

int ProcessMaterialInstances(const ITargetPlatform* TargetPlatform, const EShaderPlatform ShaderPlatform, FShaderStatsGatheringContext& Output, TArray<FAssetData>& MaterialInstanceList)
{
	int TotalShaders = 0;

	int StaticPermutations = 0;
	for (const FAssetData& AssetData : MaterialInstanceList)
	{
		if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(AssetData.GetAsset()))
		{
			TArray<FDebugShaderTypeInfo> OutShaderInfo;
			MaterialInstance->GetShaderTypes(ShaderPlatform, TargetPlatform, OutShaderInfo);

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

void ProcessForTargetAndShaderPlatform(const ITargetPlatform* TargetPlatform, const EShaderPlatform ShaderPlatform, const FString& Params, TArray<FAssetData>& MaterialList, TArray<FAssetData> MaterialInstanceList)
{
	const double StartTime = FPlatformTime::Seconds();

	const FString TimeNow = FDateTime::Now().ToString();
	FString FileName = FPaths::Combine(*FPaths::ProjectSavedDir(), FString::Printf(TEXT("MaterialStats/ShaderTypes-%s-%s-%s.txt"), *TargetPlatform->PlatformName(), *LexToString(ShaderPlatform), *TimeNow));

	FShaderStatsGatheringContext Output(FileName);

	int TotalShaders = 0;
	int TotalAssets = 0;

	// Cache for all the shader formats that the cooking target requires
	TotalShaders += ProcessMaterials(TargetPlatform, ShaderPlatform, Output, MaterialList);
	TotalAssets += MaterialList.Num();

	TotalShaders += ProcessMaterialInstances(TargetPlatform, ShaderPlatform, Output, MaterialInstanceList);
	TotalAssets += MaterialInstanceList.Num();

	Output.Log(TEXT(""));
	Output.Log(TEXT("Summary"));
	Output.Log(FString::Printf(TEXT("Total Assets: %d"), TotalAssets));
	Output.Log(FString::Printf(TEXT("Total Shaders: %d"), TotalShaders));
	Output.Log(FString::Printf(TEXT("Histogram:")));
	Output.PrintHistogram(TotalShaders);
	Output.Log(FString::Printf(TEXT("\nAlphabetic list of types:")));
	Output.PrintAlphabeticList();

	const double EndTime = FPlatformTime::Seconds() - StartTime;
	Output.Log(TEXT(""));
	Output.Log(FString::Printf(TEXT("Commandlet Took: %lf"), EndTime));

}

int32 UDumpMaterialShaderTypesCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Display help
	if (Switches.Contains("help"))
	{
		UE_LOG(LogDumpMaterialShaderTypesCommandlet, Log, TEXT("DumpMaterialShaderTypes"));
		UE_LOG(LogDumpMaterialShaderTypesCommandlet, Log, TEXT("This commandlet will dump to a human readable plain text file of all the shaders that would be compiled for all materials in a project."));
		UE_LOG(LogDumpMaterialShaderTypesCommandlet, Log, TEXT("Options:"));
		UE_LOG(LogDumpMaterialShaderTypesCommandlet, Log, TEXT(" Required: -targetplatform=<platform(s)>     (Which target platform do you want results, e.g. WindowsClient, WindowsEditor. Multiple shader platforms are allowed)."));
		UE_LOG(LogDumpMaterialShaderTypesCommandlet, Log, TEXT(" Optional: -collection=<name>                (You can also specify a collection of assets to narrow down the results e.g. if you maintain a collection that represents the actually used in-game assets)."));
		return 0;
	}

	UE_LOG(LogDumpMaterialShaderTypesCommandlet, Display, TEXT("Searching the asset registry for all assets..."));
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

	// For all active platforms
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

	for (int32 Index = 0; Index < Platforms.Num(); Index++)
	{
		TArray<FName> DesiredShaderFormats;
		Platforms[Index]->GetAllTargetedShaderFormats(DesiredShaderFormats);

		for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
		{
			const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

			UE_LOG(LogDumpMaterialShaderTypesCommandlet, Display, TEXT("Dumping material shader types for '%s' - '%s'..."), *Platforms[Index]->PlatformName(), *LexToString(ShaderPlatform));
			ProcessForTargetAndShaderPlatform(Platforms[Index], ShaderPlatform, Params, MaterialList, MaterialInstanceList);
		}
	}
	return 0;
}
