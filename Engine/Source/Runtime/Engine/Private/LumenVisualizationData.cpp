// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenVisualizationData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "FLumenVisualizationData"

static FLumenVisualizationData GLumenVisualizationData;

// Must match appropriate values in r.Lumen.Visualize.Mode
#define VISUALIZE_LUMEN_SCENE		1
#define VISUALIZE_REFLECTION_VIEW	2
#define VISUALIZE_SURFACE_CACHE		3
#define VISUALIZE_OVERVIEW			4

void FLumenVisualizationData::Initialize()
{
	if (!bIsInitialized)
	{
		AddVisualizationMode(TEXT("Overview"), LOCTEXT("Overview", "Overview"), FModeType::Overview, VISUALIZE_OVERVIEW, true);

		AddVisualizationMode(TEXT("LumenScene"), LOCTEXT("LumenScene", "Lumen Scene"), FModeType::Standard, VISUALIZE_LUMEN_SCENE, true);
		AddVisualizationMode(TEXT("ReflectionView"), LOCTEXT("ReflectionView", "Reflection View"), FModeType::Standard, VISUALIZE_REFLECTION_VIEW, true);
		AddVisualizationMode(TEXT("SurfaceCache"), LOCTEXT("SurfaceCache", "Surface Cache"), FModeType::Standard, VISUALIZE_SURFACE_CACHE, true);

		ConfigureConsoleCommand();

		bIsInitialized = true;
	}
}

void FLumenVisualizationData::ConfigureConsoleCommand()
{
	FString AvailableVisualizationModes;
	for (TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FModeRecord& Record = It.Value();
		AvailableVisualizationModes += FString(TEXT("\n  "));
		AvailableVisualizationModes += Record.ModeString;
	}

	ConsoleDocumentationVisualizationMode = TEXT("When the viewport view-mode is set to 'Lumen Visualization', this command specifies which of the various channels to display. Values entered other than the allowed values shown below will be ignored.");
	ConsoleDocumentationVisualizationMode += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		GetVisualizeConsoleCommandName(),
		TEXT(""),
		*ConsoleDocumentationVisualizationMode,
		ECVF_Cheat);
}

void FLumenVisualizationData::AddVisualizationMode(
	const TCHAR* ModeString,
	const FText& ModeText,
	const FModeType ModeType,
	int32 ModeID,
	bool DefaultComposited
)
{
	const FName ModeName = FName(ModeString);

	FModeRecord& Record	= ModeMap.Emplace(ModeName);
	Record.ModeString			= FString(ModeString);
	Record.ModeName				= ModeName;
	Record.ModeText				= ModeText;
	Record.ModeDesc				= FText::GetEmpty();
	Record.ModeType				= ModeType;
	Record.ModeID				= ModeID;
	Record.DefaultComposited	= DefaultComposited;
}

FText FLumenVisualizationData::GetModeDisplayName(const FName& InModeName) const
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

int32 FLumenVisualizationData::GetModeID(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->ModeID;
	}
	else
	{
		return INDEX_NONE;
	}
}

bool FLumenVisualizationData::GetModeDefaultComposited(const FName& InModeName) const
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

FLumenVisualizationData& GetLumenVisualizationData()
{
	if (!GLumenVisualizationData.IsInitialized())
	{
		GLumenVisualizationData.Initialize();
	}

	return GLumenVisualizationData;
}

#undef LOCTEXT_NAMESPACE
