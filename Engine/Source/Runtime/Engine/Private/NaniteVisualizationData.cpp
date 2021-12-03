// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteVisualizationData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "FNaniteVisualizationData"

static FNaniteVisualizationData GNaniteVisualizationData;

// Nanite Visualization Modes (must match NaniteDataDecode.ush)
#define VISUALIZE_OVERVIEW							0
#define VISUALIZE_TRIANGLES							(1 << 0)
#define VISUALIZE_CLUSTERS							(1 << 1)
#define VISUALIZE_PRIMITIVES						(1 << 2)
#define VISUALIZE_INSTANCES							(1 << 3)
#define VISUALIZE_GROUPS							(1 << 4)
#define VISUALIZE_PAGES								(1 << 5)
#define VISUALIZE_OVERDRAW							(1 << 6)
#define VISUALIZE_RASTER_MODE						(1 << 7)
#define VISUALIZE_SCENE_Z_MIN						(1 << 8)
#define VISUALIZE_SCENE_Z_MAX						(1 << 9)
#define VISUALIZE_SCENE_Z_DELTA						(1 << 10)
#define VISUALIZE_MATERIAL_Z_MIN					(1 << 11)
#define VISUALIZE_MATERIAL_Z_MAX					(1 << 12)
#define VISUALIZE_MATERIAL_Z_DELTA					(1 << 13)
#define VISUALIZE_MATERIAL_MODE						(1 << 14)
#define VISUALIZE_MATERIAL_INDEX					(1 << 15)
#define VISUALIZE_MATERIAL_DEPTH					(1 << 16)
#define VISUALIZE_MATERIAL_COMPLEXITY				(1 << 17)
#define VISUALIZE_HIT_PROXY_DEPTH					(1 << 18)
#define VISUALIZE_NANITE_MASK						(1 << 19)
#define VISUALIZE_LIGHTMAP_UVS						(1 << 20)
#define VISUALIZE_LIGHTMAP_UV_INDEX					(1 << 21)
#define VISUALIZE_LIGHTMAP_DATA_INDEX				(1 << 22)
#define VISUALIZE_HIERARCHY_OFFSET					(1 << 23)
#define VISUALIZE_POSITION_BITS						(1 << 24)
#define VISUALIZE_VSM_STATIC_CACHING				(1 << 25)

void FNaniteVisualizationData::Initialize()
{
	if (!bIsInitialized)
	{
		AddVisualizationMode(TEXT("Overview"), LOCTEXT("Overview", "Overview"), FModeType::Overview, VISUALIZE_OVERVIEW, true);

		AddVisualizationMode(TEXT("Mask"), LOCTEXT("Mask", "Mask"), FModeType::Standard, VISUALIZE_NANITE_MASK, true);
		AddVisualizationMode(TEXT("Triangles"), LOCTEXT("Triangles", "Triangles"), FModeType::Standard, VISUALIZE_TRIANGLES, true);
		AddVisualizationMode(TEXT("Clusters"), LOCTEXT("Clusters", "Clusters"), FModeType::Standard, VISUALIZE_CLUSTERS, true);
		AddVisualizationMode(TEXT("Primitives"), LOCTEXT("Primitives", "Primitives"), FModeType::Standard, VISUALIZE_PRIMITIVES, true);
		AddVisualizationMode(TEXT("Instances"), LOCTEXT("Instances", "Instances"), FModeType::Standard, VISUALIZE_INSTANCES, true);
		AddVisualizationMode(TEXT("Overdraw"), LOCTEXT("Overdraw", "Overdraw"), FModeType::Standard, VISUALIZE_OVERDRAW, false);
		AddVisualizationMode(TEXT("MaterialComplexity"), LOCTEXT("MaterialComplexity", "Material Complexity"), FModeType::Standard, VISUALIZE_MATERIAL_COMPLEXITY, false);
		AddVisualizationMode(TEXT("MaterialID"), LOCTEXT("MaterialID", "Material ID"), FModeType::Standard, VISUALIZE_MATERIAL_DEPTH, true);
		AddVisualizationMode(TEXT("LightmapUV"), LOCTEXT("LightmapUV", "Lightmap UV"), FModeType::Standard, VISUALIZE_LIGHTMAP_UVS, true);

		AddVisualizationMode(TEXT("Groups"), LOCTEXT("Groups", "Groups"), FModeType::Advanced, VISUALIZE_GROUPS, true);
		AddVisualizationMode(TEXT("Pages"), LOCTEXT("Pages", "Pages"), FModeType::Advanced, VISUALIZE_PAGES, true);
		AddVisualizationMode(TEXT("Hierarchy"), LOCTEXT("Hierarchy", "Hierarchy"), FModeType::Advanced, VISUALIZE_HIERARCHY_OFFSET, true);
		AddVisualizationMode(TEXT("RasterMode"), LOCTEXT("RasterMode", "Raster Mode"), FModeType::Advanced, VISUALIZE_RASTER_MODE, true);
		AddVisualizationMode(TEXT("SceneZMin"), LOCTEXT("SceneZMin", "Scene Z Min"), FModeType::Advanced, VISUALIZE_SCENE_Z_MIN, true);
		AddVisualizationMode(TEXT("SceneZMax"), LOCTEXT("SceneZMax", "Scene Z Max"), FModeType::Advanced, VISUALIZE_SCENE_Z_MAX, true);
		AddVisualizationMode(TEXT("SceneZDelta"), LOCTEXT("SceneZDelta", "Scene Z Delta"), FModeType::Advanced, VISUALIZE_SCENE_Z_DELTA, true);
		AddVisualizationMode(TEXT("MaterialZMin"), LOCTEXT("MaterialZMin", "Material Z Min"), FModeType::Advanced, VISUALIZE_MATERIAL_Z_MIN, true);
		AddVisualizationMode(TEXT("MaterialZMax"), LOCTEXT("MaterialZMax", "Material Z Max"), FModeType::Advanced, VISUALIZE_MATERIAL_Z_MAX, true);
		AddVisualizationMode(TEXT("MaterialZDelta"), LOCTEXT("MaterialZDelta", "Material Z Delta"), FModeType::Advanced, VISUALIZE_MATERIAL_Z_DELTA, true);
		AddVisualizationMode(TEXT("MaterialMode"), LOCTEXT("MaterialMode", "Material Mode"), FModeType::Advanced, VISUALIZE_MATERIAL_MODE, true);
		AddVisualizationMode(TEXT("MaterialIndex"), LOCTEXT("MaterialIndex", "Material Index"), FModeType::Advanced, VISUALIZE_MATERIAL_INDEX, true);
		AddVisualizationMode(TEXT("HitProxyID"), LOCTEXT("HitProxyID", "Hit Proxy ID"), FModeType::Advanced, VISUALIZE_HIT_PROXY_DEPTH, true);
		AddVisualizationMode(TEXT("LightmapUVIndex"), LOCTEXT("LightmapUVIndex", "Lightmap UV Index"), FModeType::Advanced, VISUALIZE_LIGHTMAP_UV_INDEX, true);
		AddVisualizationMode(TEXT("LightmapDataIndex"), LOCTEXT("LightmapDataIndex", "Lightmap Data Index"), FModeType::Advanced, VISUALIZE_LIGHTMAP_DATA_INDEX, true);
		AddVisualizationMode(TEXT("PositionBits"), LOCTEXT("PositionBits", "Position Bits"), FModeType::Advanced, VISUALIZE_POSITION_BITS, true);
		AddVisualizationMode(TEXT("VSMStatic"), LOCTEXT("VSMStatic", "Virtual Shadow Map Static"), FModeType::Advanced, VISUALIZE_VSM_STATIC_CACHING, true);

		ConfigureConsoleCommand();

		bIsInitialized = true;
	}
}

void FNaniteVisualizationData::ConfigureConsoleCommand()
{
	FString AvailableVisualizationModes;
	for (TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FModeRecord& Record = It.Value();
		AvailableVisualizationModes += FString(TEXT("\n  "));
		AvailableVisualizationModes += Record.ModeString;
	}

	ConsoleDocumentationVisualizationMode = TEXT("When the viewport view-mode is set to 'Nanite Visualization', this command specifies which of the various channels to display. Values entered other than the allowed values shown below will be ignored.");
	ConsoleDocumentationVisualizationMode += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		GetVisualizeConsoleCommandName(),
		TEXT(""),
		*ConsoleDocumentationVisualizationMode,
		ECVF_Cheat);

	ConsoleDocumentationOverviewTargets = TEXT("Specify the list of modes that can be used in the Nanite visualization overview. Put nothing between the commas to leave a gap.\n\n\tChoose from:\n");
	ConsoleDocumentationOverviewTargets += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		GetOverviewConsoleCommandName(),
		TEXT("Triangles,Clusters,Instances,Primitives,,,,,,,,,Mask,Overdraw,MaterialID,MaterialComplexity"),
		//TEXT("Triangles,Clusters,Instances,Primitives"),
		*ConsoleDocumentationOverviewTargets,
		ECVF_Default
	);
}

void FNaniteVisualizationData::AddVisualizationMode(
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

void FNaniteVisualizationData::SetActiveMode(int32 ModeID, const FName& ModeName, bool bDefaultComposited)
{
	ActiveVisualizationModeID = ModeID;
	ActiveVisualizationModeName = ModeName;
	bActiveVisualizationModeComposited = bDefaultComposited;
}

bool FNaniteVisualizationData::IsActive() const
{
	if (!IsInitialized())
	{
		return false;
	}

	if (GetActiveModeID() == INDEX_NONE)
	{
		return false;
	}

	if (GetActiveModeID() == VISUALIZE_OVERVIEW && GetOverviewModeBitMask() == 0x0)
	{
		return false;
	}

	return true;
}

bool FNaniteVisualizationData::Update(const FName& InViewMode)
{
	bool bForceShowFlag = false;

	if (IsInitialized())
	{
		SetActiveMode(INDEX_NONE, NAME_None, true);

		// Check if overview has a configured mode list so it can be parsed and cached.
		static IConsoleVariable* ICVarOverview = IConsoleManager::Get().FindConsoleVariable(GetOverviewConsoleCommandName());
		if (ICVarOverview)
		{
			FString OverviewModeList = ICVarOverview->GetString();
			if (IsDifferentToCurrentOverviewModeList(OverviewModeList))
			{
				FString Left, Right;

				// Update our record of the list of modes we've been asked to display
				SetCurrentOverviewModeList(OverviewModeList);
				CurrentOverviewModeNames.Reset();
				CurrentOverviewModeIDs.Reset();
				CurrentOverviewModeBitMask = 0x0;

				// Extract each mode name from the comma separated string
				while (OverviewModeList.Len())
				{
					// Detect last entry in the list
					if (!OverviewModeList.Split(TEXT(","), &Left, &Right))
					{
						Left = OverviewModeList;
						Right = FString();
					}

					// Look up the mode ID for this name
					Left.TrimStartInline();

					const FName ModeName = FName(*Left);
					const int32 ModeID = GetModeID(ModeName);

					if (!Left.IsEmpty() && ModeID == INDEX_NONE)
					{
						UE_LOG(LogNaniteVisualization, Warning, TEXT("Unknown Nanite visualization mode '%s'"), *Left);
					}
					else
					{
						if (ModeID == INDEX_NONE)
						{
							// Placeholder entry to keep indices static for tile layout
							CurrentOverviewModeIDs.Emplace(0xFFFFFFFF);
						}
						else
						{
							CurrentOverviewModeIDs.Emplace(ModeID);
							CurrentOverviewModeBitMask |= ModeID;
						}

						CurrentOverviewModeNames.Emplace(ModeName);
					}

					OverviewModeList = Right;
				}
			}
		}

		// Check if the console command is set (overrides the editor)
		if (ActiveVisualizationModeID == INDEX_NONE)
		{
			static IConsoleVariable* ICVarVisualize = IConsoleManager::Get().FindConsoleVariable(GetVisualizeConsoleCommandName());
			if (ICVarVisualize)
			{
				const FString ConsoleVisualizationMode = ICVarVisualize->GetString();
				const bool bDisable = ConsoleVisualizationMode == TEXT("off") || ConsoleVisualizationMode == TEXT("none");

				if (!ConsoleVisualizationMode.IsEmpty() && !bDisable)
				{
					const FName  ModeName = FName(*ConsoleVisualizationMode);
					const int32  ModeID   = GetModeID(ModeName);
					if (ModeID == INDEX_NONE)
					{
						UE_LOG(LogNaniteVisualization, Warning, TEXT("Unknown Nanite visualization mode '%s'"), *ConsoleVisualizationMode);
					}
					else
					{
						SetActiveMode(ModeID, ModeName, GetModeDefaultComposited(ModeName));
						bForceShowFlag = true;
					}
				}
			}
		}

		// Check the view mode state (set by editor).
		if (ActiveVisualizationModeID == INDEX_NONE && InViewMode != NAME_None)
		{
			const int32 ModeID = GetModeID(InViewMode);
			if (ensure(ModeID != INDEX_NONE))
			{
				SetActiveMode(ModeID, InViewMode, GetModeDefaultComposited(InViewMode));
			}
		}
	}

	return bForceShowFlag;
}

FText FNaniteVisualizationData::GetModeDisplayName(const FName& InModeName) const
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

int32 FNaniteVisualizationData::GetModeID(const FName& InModeName) const
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

bool FNaniteVisualizationData::GetModeDefaultComposited(const FName& InModeName) const
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

void FNaniteVisualizationData::SetCurrentOverviewModeList(const FString& InNameList)
{
	CurrentOverviewModeList = InNameList;
}

bool FNaniteVisualizationData::IsDifferentToCurrentOverviewModeList(const FString& InNameList)
{
	return InNameList != CurrentOverviewModeList;
}

FNaniteVisualizationData& GetNaniteVisualizationData()
{
	if (!GNaniteVisualizationData.IsInitialized())
	{
		GNaniteVisualizationData.Initialize();
	}

	return GNaniteVisualizationData;
}

#undef LOCTEXT_NAMESPACE
