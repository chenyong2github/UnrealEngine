// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualShadowMapVisualizationData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "FVirtualShadowMapVisualizationData"

static FVirtualShadowMapVisualizationData GVirtualShadowMapVisualizationData;

// Must match values in Shadows/VirtualShadowMaps/Visualize.ush
#define VISUALIZE_NONE						0
#define VISUALIZE_SHADOW_FACTOR				(1 << 0)
#define VISUALIZE_CLIPMAP_OR_MIP			(1 << 1)
#define VISUALIZE_VIRTUAL_PAGE				(1 << 2)
#define VISUALIZE_CACHED_PAGE				(1 << 3)
#define VISUALIZE_SMRT_RAY_COUNT			(1 << 4)


void FVirtualShadowMapVisualizationData::Initialize()
{
	if (!bIsInitialized)
	{
		// NOTE: The first parameter determines the console command parameter. "none", "off" and "list" are reserved
		AddVisualizationMode(
			TEXT("mask"),
			LOCTEXT("ShadowMask", "Shadow Mask"),
			LOCTEXT("ShadowMaskDesc", "The final shadow mask that is used by shading"),
			FModeType::ProjectionStandard,
			VISUALIZE_SHADOW_FACTOR);

		AddVisualizationMode(
			TEXT("mip"),
			LOCTEXT("ClipmapOrMip", "Clipmap/Mip Level"),
			LOCTEXT("ClipmapOrMipDesc", "The chosen clipmap (for directional lights) or mip (for local lights) level"),
			FModeType::ProjectionStandard,
			VISUALIZE_CLIPMAP_OR_MIP);

		AddVisualizationMode(
			TEXT("vpage"),
			LOCTEXT("VirtualPage", "Virtual Page"),
			LOCTEXT("VirtualPageDesc", "Visualization of the virtual page address"),
			FModeType::ProjectionStandard,
			VISUALIZE_VIRTUAL_PAGE);

		AddVisualizationMode(
			TEXT("cache"),
			LOCTEXT("CachedPage", "Cached Page"),
			LOCTEXT("CachedPageDesc", "Cached pages are tinted green, uncached are red. Pages where only the static page is cached (dynamic uncached) are blue."),
			FModeType::ProjectionStandard,
			VISUALIZE_CACHED_PAGE);

		AddVisualizationMode(
			TEXT("raycount"),
			LOCTEXT("SMRTRayCount", "SMRT Ray Count"),
			LOCTEXT("SMRTRayCountDesc", "Rays evaluated per pixel: red is more, green is fewer. Penumbra regions require more rays and are more expensive."),
			FModeType::ProjectionAdvanced,
			VISUALIZE_SMRT_RAY_COUNT);

		ConfigureConsoleCommand();

		bIsInitialized = true;
	}
}

void FVirtualShadowMapVisualizationData::ConfigureConsoleCommand()
{
	FString AvailableVisualizationModes;
	for (TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FModeRecord& Record = It.Value();
		AvailableVisualizationModes += FString(TEXT("\n  "));
		AvailableVisualizationModes += Record.ModeString;
	}

	ConsoleDocumentationVisualizationMode = TEXT("When the viewport view-mode is set to 'Virtual Shadow Map Visualization', this command specifies which of the various channels to display. Values entered other than the allowed values shown below will be ignored.");
	ConsoleDocumentationVisualizationMode += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		GetVisualizeConsoleCommandName(),
		TEXT(""),
		*ConsoleDocumentationVisualizationMode,
		ECVF_Cheat);
}

void FVirtualShadowMapVisualizationData::SetActiveMode(int32 ModeID, const FName& ModeName)
{
	ActiveVisualizationModeID = ModeID;
	ActiveVisualizationModeName = ModeName;
}

bool FVirtualShadowMapVisualizationData::IsActive() const
{
	if (!IsInitialized())
	{
		return false;
	}

	if (GetActiveModeID() == INDEX_NONE)
	{
		return false;
	}

	return true;
}

bool FVirtualShadowMapVisualizationData::Update(const FName& InViewMode)
{
	bool bForceShowFlag = false;

	if (!IsInitialized())
	{
		return false;
	}

	SetActiveMode(INDEX_NONE, NAME_None);
			
	// Check if the console command is set (overrides the editor)
	{
		static IConsoleVariable* ICVarVisualize = IConsoleManager::Get().FindConsoleVariable(GetVisualizeConsoleCommandName());
		if (ICVarVisualize)
		{
			const FString ConsoleVisualizationMode = ICVarVisualize->GetString();
			if (!ConsoleVisualizationMode.IsEmpty())
			{
				 if (ConsoleVisualizationMode == TEXT("off") || ConsoleVisualizationMode == TEXT("none"))
				{
					// Disable visualization
				}
				else
				{
					const FName  ModeName = FName(*ConsoleVisualizationMode);
					const int32  ModeID   = GetModeID(ModeName);
					if (ModeID == INDEX_NONE)
					{
						UE_LOG(LogVirtualShadowMapVisualization, Warning, TEXT("Unknown virtual shadow map visualization mode '%s'"), *ConsoleVisualizationMode);
					}
					else
					{
						SetActiveMode(ModeID, ModeName);
						bForceShowFlag = true;
					}
				}
			}
		}
	}

	// Check the view mode state (set by editor).
	if (ActiveVisualizationModeID == INDEX_NONE && InViewMode != NAME_None)
	{
		const int32 ModeID = GetModeID(InViewMode);
		if (ModeID != INDEX_NONE)
		{
			SetActiveMode(ModeID, InViewMode);
		}
	}

	return bForceShowFlag;
}

void FVirtualShadowMapVisualizationData::AddVisualizationMode(
	const TCHAR* ModeString,
	const FText& ModeText,
	const FText& ModeDesc,
	const FModeType ModeType,
	int32 ModeID
)
{
	const FName ModeName = FName(ModeString);

	FModeRecord& Record	= ModeMap.Emplace(ModeName);
	Record.ModeString			= FString(ModeString);
	Record.ModeName				= ModeName;
	Record.ModeText				= ModeText;
	Record.ModeDesc				= ModeDesc;
	Record.ModeType				= ModeType;
	Record.ModeID				= ModeID;
}

FText FVirtualShadowMapVisualizationData::GetModeDisplayName(const FName& InModeName) const
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

int32 FVirtualShadowMapVisualizationData::GetModeID(const FName& InModeName) const
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

FVirtualShadowMapVisualizationData& GetVirtualShadowMapVisualizationData()
{
	if (!GVirtualShadowMapVisualizationData.IsInitialized())
	{
		GVirtualShadowMapVisualizationData.Initialize();
	}

	return GVirtualShadowMapVisualizationData;
}

#undef LOCTEXT_NAMESPACE
