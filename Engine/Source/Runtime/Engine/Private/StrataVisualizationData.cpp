// Copyright Epic Games, Inc. All Rights Reserved.

#include "StrataVisualizationData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "FStrataVisualizationData"

static FStrataVisualizationData GStrataVisualizationData;

bool Engine_IsStrataEnabled();

static bool Engine_IsStrataDBufferPassEnabled()
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Strata.DBufferPass"));
	return CVar && CVar->GetValueOnAnyThread() > 0;
}

static bool Engine_IsStrataRoughRefractionEnabled()
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Strata.OpaqueMaterialRoughRefraction"));
	return CVar && CVar->GetValueOnAnyThread() > 0;
}

static bool Engine_IsStrataAdvancedDebugShaderEnabled()
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Strata.Debug.AdvancedVisualizationShaders"));
	return CVar && CVar->GetValueOnAnyThread() > 0;
}

static FString ConfigureConsoleCommand(FStrataVisualizationData::TModeMap& ModeMap)
{
	FString AvailableVisualizationModes;
	for (FStrataVisualizationData::TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FStrataVisualizationData::FModeRecord& Record = It.Value();
		AvailableVisualizationModes += FString(TEXT("\n  "));
		AvailableVisualizationModes += Record.ModeString;
	}

	FString Out;
	Out = TEXT("When the viewport view-mode is set to 'Strata Visualization', this command specifies which of the various channels to display. Values entered other than the allowed values shown below will be ignored.");
	Out += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		FStrataVisualizationData::GetVisualizeConsoleCommandName(),
		0,
		*Out,
		ECVF_Cheat);

	return Out;
}

static void AddVisualizationMode(
	FStrataVisualizationData::TModeMap& ModeMap,
	const TCHAR* ModeString,
	const FText& ModeText,
	const FText& ModeDesc,
	const FStrataVisualizationData::FViewMode Mode,
	bool DefaultComposited
)
{
	const FName ModeName = FName(ModeString);

	FStrataVisualizationData::FModeRecord& Record = ModeMap.Emplace(ModeName);
	Record.ModeString			= FString(ModeString);
	Record.ModeName				= ModeName;
	Record.ModeText				= ModeText;
	Record.ModeDesc				= ModeDesc;
	Record.Mode					= Mode;
	Record.DefaultComposited	= DefaultComposited;
}

void FStrataVisualizationData::Initialize()
{
	if (!bIsInitialized && Engine_IsStrataEnabled())
	{
		AddVisualizationMode(
			ModeMap,
			TEXT("MaterialProperties"),
			LOCTEXT("MaterialProperties", "Material Properites"),
			LOCTEXT("MaterialPropertiesDesc", "Visualizes Strata material properties under mouse cursor"),
			FViewMode::MaterialProperties,
			true);

		AddVisualizationMode(
			ModeMap,
			TEXT("MaterialCount"),
			LOCTEXT("MaterialCount", "Material Count"),
			LOCTEXT("MaterialCountDesc", "Visualizes Strata material count per pixel"),
			FViewMode::MaterialCount,
			true);

		if (Engine_IsStrataAdvancedDebugShaderEnabled())
		{
			AddVisualizationMode(
				ModeMap,
				TEXT("AdvancedMaterialProperties"),
				LOCTEXT("AdvancedMaterialProperties", "Advanced Material Properties"),
				LOCTEXT("AdvancedMaterialPropertiesDesc", "Visualizes Strata advanced material properties"),
				FViewMode::AdvancedMaterialProperties,
				true);
		}

		AddVisualizationMode(
			ModeMap,
			TEXT("MaterialClassification"),
			LOCTEXT("MaterialClassification", "Material Classification"),
			LOCTEXT("MaterialClassificationDesc", "Visualizes Strata material classification"),
			FViewMode::MaterialClassification,
			true);

		if (Engine_IsStrataDBufferPassEnabled())
		{
			AddVisualizationMode(
				ModeMap,
				TEXT("DecalClassification"),
				LOCTEXT("DecalClassification", "Decal classification"),
				LOCTEXT("DecalClassificationDesc", "Visualizes Strata decal classification"),
				FViewMode::DecalClassification,
				true);
		}

		if (Engine_IsStrataRoughRefractionEnabled())
		{
			AddVisualizationMode(
				ModeMap,
				TEXT("RoughRefractionClassification"),
				LOCTEXT("RoughRefractionClassification", "Material Count"),
				LOCTEXT("RoughRefractionClassificationDesc", "Visualizes Strata rough refraction classification"),
				FViewMode::RoughRefractionClassification,
				true);
		}

		AddVisualizationMode(
			ModeMap,
			TEXT("StrataInfo"),
			LOCTEXT("StrataInfo", "Strata Info"),
			LOCTEXT("StrataInfoDesc", "Visualizes Strata info"),
			FViewMode::StrataInfo,
			true);

		ConsoleDocumentationVisualizationMode = ConfigureConsoleCommand(ModeMap);
	}
	bIsInitialized = true;
}

FText FStrataVisualizationData::GetModeDisplayName(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->ModeText;
	}
	else
	{
		return FText::GetEmpty();
	}
}

FStrataVisualizationData::FViewMode FStrataVisualizationData::GetViewMode(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->Mode;
	}
	else
	{
		return FStrataVisualizationData::FViewMode::None;
	}
}

bool FStrataVisualizationData::GetModeDefaultComposited(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->DefaultComposited;
	}
	else
	{
		return false;
	}
}

FStrataVisualizationData& GetStrataVisualizationData()
{
	if (!GStrataVisualizationData.IsInitialized())
	{
		GStrataVisualizationData.Initialize();
	}

	return GStrataVisualizationData;
}

#undef LOCTEXT_NAMESPACE
