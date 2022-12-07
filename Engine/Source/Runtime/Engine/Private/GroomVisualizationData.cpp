// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomVisualizationData.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"
#include "SceneView.h"

#define LOCTEXT_NAMESPACE "FGroomVisualizationData"

static int32 GHairStrandsPluginEnable = 0;

static TAutoConsoleVariable<int32> CVarHairStrandsGlobalEnable(
	TEXT("r.HairStrands.Enable"), 1,
	TEXT("Enable/Disable the entire hair strands system. This affects all geometric representations (i.e., strands, cards, and meshes)."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

bool IsGroomEnabled()
{
	return GHairStrandsPluginEnable > 0 && CVarHairStrandsGlobalEnable.GetValueOnAnyThread() > 0;
}

void SetGroomEnabled(bool In)
{
	GHairStrandsPluginEnable = In ? 1 : 0;
}

EGroomViewMode GetGroomViewMode(const FSceneView& View)
{
	EGroomViewMode Out = EGroomViewMode::None;
	if (IsGroomEnabled())
	{
		static const auto CVarGroomViewMode = IConsoleManager::Get().FindConsoleVariable(FGroomVisualizationData::GetVisualizeConsoleCommandName());
		const uint32 ViewMode = CVarGroomViewMode && CVarGroomViewMode->AsVariableInt() ? CVarGroomViewMode->AsVariableInt()->GetValueOnRenderThread() : 0;
		switch (ViewMode)
		{
		case 1:  return EGroomViewMode::MacroGroups;
		case 2:  return EGroomViewMode::LightBounds;
		case 3:  return EGroomViewMode::MacroGroupScreenRect;
		case 4:  return EGroomViewMode::DeepOpacityMaps;
		case 5:  return EGroomViewMode::SamplePerPixel;
		case 6:  return EGroomViewMode::TAAResolveType;
		case 7:  return EGroomViewMode::CoverageType;
		case 8:  return EGroomViewMode::VoxelsDensity;
		case 9:	 return EGroomViewMode::None;
		case 10: return EGroomViewMode::None;
		case 11: return EGroomViewMode::None;
		case 12: return EGroomViewMode::MeshProjection;
		case 13: return EGroomViewMode::Coverage;
		case 14: return EGroomViewMode::MaterialDepth;
		case 15: return EGroomViewMode::MaterialBaseColor;
		case 16: return EGroomViewMode::MaterialRoughness;
		case 17: return EGroomViewMode::MaterialSpecular;
		case 18: return EGroomViewMode::MaterialTangent;
		case 19: return EGroomViewMode::Tile;
		case 20: return EGroomViewMode::None;
		case 21: return EGroomViewMode::SimHairStrands;
		case 22: return EGroomViewMode::RenderHairStrands;
		case 23: return EGroomViewMode::RenderHairRootUV;
		case 24: return EGroomViewMode::RenderHairRootUDIM;
		case 25: return EGroomViewMode::RenderHairUV;
		case 26: return EGroomViewMode::RenderHairSeed;
		case 27: return EGroomViewMode::RenderHairDimension;
		case 28: return EGroomViewMode::RenderHairRadiusVariation;
		case 29: return EGroomViewMode::RenderHairBaseColor;
		case 30: return EGroomViewMode::RenderHairRoughness;
		case 31: return EGroomViewMode::RenderVisCluster;
		case 32: return EGroomViewMode::RenderVisClusterAABB;
		case 33: return EGroomViewMode::RenderHairTangent;
		case 34: return EGroomViewMode::RenderHairControlPoints;
		case 35: return EGroomViewMode::RenderHairGroup;
		case 36: return EGroomViewMode::RenderLODColoration;
		case 37: return EGroomViewMode::CardGuides;
		default: break;
		}

		const FGroomVisualizationData& VisualizationData = GetGroomVisualizationData();
		if (View.Family && View.Family->EngineShowFlags.VisualizeGroom)
		{
			Out = VisualizationData.GetViewMode(View.CurrentGroomVisualizationMode);
		}
		else if (View.Family && View.Family->EngineShowFlags.LODColoration)
		{
			Out = EGroomViewMode::RenderLODColoration;
		}
	}
	return Out;
}

const TCHAR* GetGroomViewModeName(EGroomViewMode In)
{
	switch (In)
	{
	case EGroomViewMode::None:						return TEXT("NoneDebug");
	case EGroomViewMode::MacroGroups:				return TEXT("MacroGroups");
	case EGroomViewMode::LightBounds:				return TEXT("LightBounds");
	case EGroomViewMode::MacroGroupScreenRect:		return TEXT("MacroGroupScreenRect");
	case EGroomViewMode::DeepOpacityMaps:			return TEXT("DeepOpacityMaps");
	case EGroomViewMode::SamplePerPixel:			return TEXT("SamplePerPixel");
	case EGroomViewMode::TAAResolveType:			return TEXT("TAAResolveType");
	case EGroomViewMode::CoverageType:				return TEXT("CoverageType");
	case EGroomViewMode::VoxelsDensity:				return TEXT("VoxelsDensity");
	case EGroomViewMode::MeshProjection:			return TEXT("MeshProjection");
	case EGroomViewMode::Coverage:					return TEXT("Coverage");
	case EGroomViewMode::MaterialDepth:				return TEXT("MaterialDepth");
	case EGroomViewMode::MaterialBaseColor:			return TEXT("MaterialBaseColor");
	case EGroomViewMode::MaterialRoughness:			return TEXT("MaterialRoughness");
	case EGroomViewMode::MaterialSpecular:			return TEXT("MaterialSpecular");
	case EGroomViewMode::MaterialTangent:			return TEXT("MaterialTangent");
	case EGroomViewMode::Tile:						return TEXT("Tile");
	case EGroomViewMode::SimHairStrands:			return TEXT("SimHairStrands");
	case EGroomViewMode::RenderHairStrands:			return TEXT("RenderHairStrands");
	case EGroomViewMode::RenderHairRootUV:			return TEXT("RenderHairRootUV");
	case EGroomViewMode::RenderHairRootUDIM:		return TEXT("RenderHairRootUDIM");
	case EGroomViewMode::RenderHairUV:				return TEXT("RenderHairUV");
	case EGroomViewMode::RenderHairSeed:			return TEXT("RenderHairSeed");
	case EGroomViewMode::RenderHairDimension:		return TEXT("RenderHairDimension");
	case EGroomViewMode::RenderHairRadiusVariation:	return TEXT("RenderHairRadiusVariation");
	case EGroomViewMode::RenderHairBaseColor:		return TEXT("RenderHairBaseColor");
	case EGroomViewMode::RenderHairRoughness:		return TEXT("RenderHairRoughness");
	case EGroomViewMode::RenderVisCluster:			return TEXT("RenderVisCluster");
	case EGroomViewMode::RenderVisClusterAABB:		return TEXT("RenderVisClusterAABB");
	case EGroomViewMode::RenderHairTangent:			return TEXT("RenderHairTangent");
	case EGroomViewMode::RenderHairControlPoints:	return TEXT("RenderHairControlPoints");
	case EGroomViewMode::RenderHairGroup:			return TEXT("RenderHairGroup");
	case EGroomViewMode::RenderLODColoration:		return TEXT("RenderLODColoration");
	case EGroomViewMode::CardGuides:				return TEXT("CardGuides");
	}
	return TEXT("None");
}

static FGroomVisualizationData GGroomVisualizationData;

static FString ConfigureConsoleCommand(FGroomVisualizationData::TModeMap& ModeMap)
{
	FString AvailableVisualizationModes;
	for (FGroomVisualizationData::TModeMap::TConstIterator It = ModeMap.CreateConstIterator(); It; ++It)
	{
		const FGroomVisualizationData::FModeRecord& Record = It.Value();
		AvailableVisualizationModes += FString(TEXT("\n  "));
		AvailableVisualizationModes += Record.ModeString;
	}

	FString Out;
	Out = TEXT("When the viewport view-mode is set to 'Groom Visualization', this command specifies which of the various channels to display. Values entered other than the allowed values shown below will be ignored.");
	Out += AvailableVisualizationModes;

	IConsoleManager::Get().RegisterConsoleVariable(
		FGroomVisualizationData::GetVisualizeConsoleCommandName(),
		0,
		*Out,
		ECVF_Cheat);

	return Out;
}

static void AddVisualizationMode(
	FGroomVisualizationData::TModeMap& ModeMap,
	bool DefaultComposited,
	const EGroomViewMode Mode,
	const FText& ModeText,
	const FText& ModeDesc)
{
	const TCHAR* ModeString = GetGroomViewModeName(Mode);
	const FName ModeName = FName(ModeString);

	FGroomVisualizationData::FModeRecord& Record = ModeMap.Emplace(ModeName);
	Record.ModeString			= FString(ModeString);
	Record.ModeName				= ModeName;
	Record.ModeText				= ModeText;
	Record.ModeDesc				= ModeDesc;
	Record.Mode					= Mode;
	Record.DefaultComposited	= DefaultComposited;
}

void FGroomVisualizationData::Initialize()
{
	if (!bIsInitialized && IsGroomEnabled())
	{
		AddVisualizationMode(ModeMap, true, EGroomViewMode::None,						LOCTEXT("NoneDebug", "None"),								LOCTEXT("NoneDebugDesc", "No debug mode"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MacroGroups,				LOCTEXT("MacroGroups", "Instances"),						LOCTEXT("MacroGroupsDesc", "Instances info"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::LightBounds,				LOCTEXT("LightBounds", "Light Bound"),						LOCTEXT("LightBoundsDesc", "All DOMs light bounds"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MacroGroupScreenRect,		LOCTEXT("MacroGroupScreenRect", "screen Bounds"),			LOCTEXT("MacroGroupScreenRectDesc", "Screen projected instances"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::DeepOpacityMaps,			LOCTEXT("DeepOpacityMaps", "Deep Shadows"),					LOCTEXT("DeepOpacityMapsDesc", "Deep opacity maps"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::SamplePerPixel,				LOCTEXT("SamplePerPixel", "Sample Per Pixel"),				LOCTEXT("SamplePerPixelDesc", "Sub-pixel sample count"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::TAAResolveType,				LOCTEXT("TAAResolveType", "AA Type"),						LOCTEXT("TAAResolveTypeDesc", "TAA resolve type (regular/responsive)"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::CoverageType,				LOCTEXT("CoverageType", "Coverage Type"),					LOCTEXT("CoverageTypeDesc", "Type of hair coverage - Fully covered : Green / Partially covered : Red"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::VoxelsDensity,				LOCTEXT("VoxelsDensity", "Voxels"),							LOCTEXT("VoxelsDensityDesc", "Hair density volume"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MeshProjection,				LOCTEXT("MeshProjection", "Root Bindings"),					LOCTEXT("MeshProjectionDesc", "Hair mesh projection"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::Coverage,					LOCTEXT("Coverage", "Coverage"),							LOCTEXT("CoverageDesc", "Hair coverage"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MaterialDepth,				LOCTEXT("MaterialDepth", "Depth"),							LOCTEXT("MaterialDepthDesc", "Hair material depth"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MaterialBaseColor,			LOCTEXT("MaterialBaseColor", "BaseColor"),					LOCTEXT("MaterialBaseColorDesc", "Hair material base color"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MaterialRoughness,			LOCTEXT("MaterialRoughness", "Roughness"),					LOCTEXT("MaterialRoughnessDesc", "Hair material roughness"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MaterialSpecular,			LOCTEXT("MaterialSpecular", "Specular"),					LOCTEXT("MaterialSpecularDesc", "Hair material specular"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::MaterialTangent,			LOCTEXT("MaterialTangent", "Tangent"),						LOCTEXT("MaterialTangentDesc", "Hair material tangent"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::Tile,						LOCTEXT("Tile", "Tile"),									LOCTEXT("TileDesc", "Hair tile cotegorization"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::SimHairStrands,				LOCTEXT("SimHairStrands", "Guides"),						LOCTEXT("SimHairStrandsDesc", "Simulation strands"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairStrands,			LOCTEXT("RenderHairStrands", "Stands Guides Influences"),	LOCTEXT("RenderHairStrandsDesc", "Rendering strands influences"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::CardGuides, 				LOCTEXT("CardGuides", "Cards Guides"),						LOCTEXT("CardGuidesDesc", "Cards Guides"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairRootUV,			LOCTEXT("RenderHairRootUV", "Root UV"),						LOCTEXT("RenderHairRootUVDesc", "Roots UV"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairRootUDIM,			LOCTEXT("RenderHairRootUDIM", "Root UDIM"),					LOCTEXT("RenderHairRootUDIMDesc", "Roots UV UDIM texture index"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairUV,				LOCTEXT("RenderHairUV", "UV"),								LOCTEXT("RenderHairUVDesc", "Hair UV"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairSeed,				LOCTEXT("RenderHairSeed", "Seed"),							LOCTEXT("RenderHairSeedDesc", "Hair seed"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairDimension,		LOCTEXT("RenderHairDimension", "Dimension"),				LOCTEXT("RenderHairDimensionDesc", "Hair dimensions"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairRadiusVariation,	LOCTEXT("RenderHairRadiusVariation", "Radius Variation"),	LOCTEXT("RenderHairRadiusVariationDesc", "Hair radius variation"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairTangent,			LOCTEXT("RenderHairTangent", "Tangent"),					LOCTEXT("RenderHairTangentDesc", "Hair tangent"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairControlPoints,	LOCTEXT("RenderHairControlPoints", "Control Points"),		LOCTEXT("RenderHairControlPointsDesc", "Hair control points"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairBaseColor,		LOCTEXT("RenderHairBaseColor", "Per-CV Color"),				LOCTEXT("RenderHairBaseColorDesc", "CV color"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairRoughness,		LOCTEXT("RenderHairRoughness", "Per-CV Roughness"),			LOCTEXT("RenderHairRoughnessDesc", "CV roughness"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderVisCluster,			LOCTEXT("RenderVisCluster", "Clusters"),					LOCTEXT("RenderVisClusterDesc", "Hair visility clusters"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderVisClusterAABB,		LOCTEXT("RenderVisClusterAABB", "Clusters Bounds"),			LOCTEXT("RenderVisClusterAABBDesc", "Hair visility clusters AABBs"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderHairGroup,			LOCTEXT("RenderHairGroup", "Groups"),						LOCTEXT("RenderHairGroupDesc", "Hair hair groups"));
		AddVisualizationMode(ModeMap, true, EGroomViewMode::RenderLODColoration,		LOCTEXT("RenderLODColoration", "LOD Color"),				LOCTEXT("RenderLODColorationDesc", "Hair LOD coloring"));

		ConsoleDocumentationVisualizationMode = ConfigureConsoleCommand(ModeMap);
	}
	bIsInitialized = true;
}

FText FGroomVisualizationData::GetModeDisplayName(const FName& InModeName) const
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

EGroomViewMode FGroomVisualizationData::GetViewMode(const FName& InModeName) const
{
	if (const FModeRecord* Record = ModeMap.Find(InModeName))
	{
		return Record->Mode;
	}
	else
	{
		return EGroomViewMode::None;
	}
}

bool FGroomVisualizationData::GetModeDefaultComposited(const FName& InModeName) const
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

FGroomVisualizationData& GetGroomVisualizationData()
{
	if (!GGroomVisualizationData.IsInitialized())
	{
		GGroomVisualizationData.Initialize();
	}

	return GGroomVisualizationData;
}

#undef LOCTEXT_NAMESPACE
