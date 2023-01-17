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
	static const auto CVarStrataGBufferFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GBufferFormat")); 
	const uint32 StrataNormalQuality = CVarStrataGBufferFormat && CVarStrataGBufferFormat->GetValueOnAnyThread() > 1 ? 1 : 0;

	// DBuffer pass is only available if high quality normal is disabled, or if we are on a console.
	// That is because in high quality normals requires a uint2 UAV which is only supported on some graphic cards on PC 
	// and this is an unknown when compiling shaders at this stage.
	// !!! If this is changed, please update all sites reading r.Strata.DBufferPass !!!
	const bool bDBufferPassSupported = StrataNormalQuality == 0;	// We do not have access to platform to check if we are on console here. Visualize is likely used on PC editor so we can only visualize DBuffer if NormalQuality is 0.

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Strata.DBufferPass"));
	return CVar && CVar->GetValueOnAnyThread() > 0 && bDBufferPassSupported;
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
		AvailableVisualizationModes += FString(TEXT("\n  Value="));
		AvailableVisualizationModes += FString::Printf(TEXT("%d: "), uint8(Record.Mode));
		AvailableVisualizationModes += Record.ModeString;
		if (!Record.bAvailableCommand)
		{
			AvailableVisualizationModes += FString::Printf(TEXT(" --- Unavailable, reason: %s"), *Record.UnavailableReason.ToString());
		}
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
	bool bDefaultComposited,
	bool bAvailableCommand,
	const FText& UnavailableReason
)
{
	const FName ModeName = FName(ModeString);

	FStrataVisualizationData::FModeRecord& Record = ModeMap.Emplace(ModeName);
	Record.ModeString			= FString(ModeString);
	Record.ModeName				= ModeName;
	Record.ModeText				= ModeText;
	Record.ModeDesc				= ModeDesc;
	Record.Mode					= Mode;
	Record.bDefaultComposited	= bDefaultComposited;
	Record.bAvailableCommand	= bAvailableCommand;
	Record.UnavailableReason	= UnavailableReason;
}

void FStrataVisualizationData::Initialize()
{
	if (!bIsInitialized && Engine_IsStrataEnabled())
	{
		TModeMap AllModeMap;

		AddVisualizationMode(
			AllModeMap,
			TEXT("MaterialProperties"),
			LOCTEXT("MaterialProperties", "Material Properties"),
			LOCTEXT("MaterialPropertiesDesc", "Visualizes Strata material properties under mouse cursor"),
			FViewMode::MaterialProperties,
			true,
			true,
			LOCTEXT("None", "None"));

		AddVisualizationMode(
			AllModeMap,
			TEXT("MaterialCount"),
			LOCTEXT("MaterialCount", "Material Count"),
			LOCTEXT("MaterialCountDesc", "Visualizes Strata material count per pixel"),
			FViewMode::MaterialCount,
			true,
			true,
			LOCTEXT("None", "None"));

		AddVisualizationMode(
			AllModeMap,
			TEXT("AdvancedMaterialProperties"),
			LOCTEXT("AdvancedMaterialProperties", "Advanced Material Properties"),
			LOCTEXT("AdvancedMaterialPropertiesDesc", "Visualizes Strata advanced material properties"),
			FViewMode::AdvancedMaterialProperties,
			true,
			Engine_IsStrataAdvancedDebugShaderEnabled(),
			LOCTEXT("IsStrataAdvancedDebugShaderEnabled", "Strata advanced debugging r.Strata.Debug.AdvancedVisualizationShaders is disabled"));

		AddVisualizationMode(
			AllModeMap,
			TEXT("MaterialClassification"),
			LOCTEXT("MaterialClassification", "Material Classification"),
			LOCTEXT("MaterialClassificationDesc", "Visualizes Strata material classification"),
			FViewMode::MaterialClassification,
			true,
			true,
			LOCTEXT("None", "None"));

		AddVisualizationMode(
			AllModeMap,
			TEXT("DecalClassification"),
			LOCTEXT("DecalClassification", "Decal classification"),
			LOCTEXT("DecalClassificationDesc", "Visualizes Strata decal classification"),
			FViewMode::DecalClassification,
			true,
			Engine_IsStrataDBufferPassEnabled(),
			LOCTEXT("IsStrataDBufferPassEnabled", "Strata tiled DBuffer pass (r.Strata.DBufferPass and r.Strata.DBufferPass.DedicatedTiles) is disabled"));

		AddVisualizationMode(
			AllModeMap,
			TEXT("RoughRefractionClassification"),
			LOCTEXT("RoughRefractionClassification", "Material Count"),
			LOCTEXT("RoughRefractionClassificationDesc", "Visualizes Strata rough refraction classification"),
			FViewMode::RoughRefractionClassification,
			true,
			Engine_IsStrataRoughRefractionEnabled(),
			LOCTEXT("IsStrataRoughRefractionEnabled", "Strata rough refraction r.Strata.OpaqueMaterialRoughRefraction is disabled"));

		AddVisualizationMode(
			AllModeMap,
			TEXT("StrataInfo"),
			LOCTEXT("StrataInfo", "Strata Info"),
			LOCTEXT("StrataInfoDesc", "Visualizes Strata info"),
			FViewMode::StrataInfo,
			true,
			true,
			LOCTEXT("None", "None"));

		ConsoleDocumentationVisualizationMode = ConfigureConsoleCommand(AllModeMap);

		// Now only copy the available modes for the menu to not overload it with useless entries.
		for (auto& Mode : AllModeMap)
		{
			if(Mode.Value.bAvailableCommand)
			{
				ModeMap.Emplace(Mode.Key) = Mode.Value;
			}
		}
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
		return Record->bDefaultComposited;
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
