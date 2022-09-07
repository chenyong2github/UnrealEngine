// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelExporterUSDOptions.h"

#include "USDAssetOptions.h"

#include "AnalyticsEventAttribute.h"

TArray<FString> ULevelExporterUSDOptions::GetUsdExtensions()
{
	TArray<FString> Extensions = UnrealUSDWrapper::GetNativeFileFormats();
	Extensions.Remove( TEXT( "usdz" ) );
	return Extensions;
}

void UsdUtils::AddAnalyticsAttributes(
	const FLevelExporterUSDOptionsInner& Options,
	TArray< FAnalyticsEventAttribute >& InOutAttributes
)
{
	InOutAttributes.Emplace( TEXT( "SelectionOnly" ), Options.bSelectionOnly );
	InOutAttributes.Emplace( TEXT( "ExportActorFolders" ), Options.bExportActorFolders );
	InOutAttributes.Emplace( TEXT( "IgnoreSequencerAnimations" ), Options.bIgnoreSequencerAnimations );
	InOutAttributes.Emplace( TEXT( "ExportFoliageOnActorsLayer" ), Options.bExportFoliageOnActorsLayer );
	UsdUtils::AddAnalyticsAttributes( Options.AssetOptions, InOutAttributes );
	InOutAttributes.Emplace( TEXT( "LowestLandscapeLOD" ), LexToString( Options.LowestLandscapeLOD ) );
	InOutAttributes.Emplace( TEXT( "HighestLandscapeLOD" ), LexToString( Options.HighestLandscapeLOD ) );
	InOutAttributes.Emplace( TEXT( "LandscapeBakeResolution" ), Options.LandscapeBakeResolution.ToString() );
	InOutAttributes.Emplace( TEXT( "ExportSublayers" ), LexToString( Options.bExportSublayers ) );
	InOutAttributes.Emplace( TEXT( "NumLevelsToIgnore" ), LexToString( Options.LevelsToIgnore.Num() ) );
}

void UsdUtils::AddAnalyticsAttributes(
	const ULevelExporterUSDOptions& Options,
	TArray< FAnalyticsEventAttribute >& InOutAttributes
)
{
	UsdUtils::AddAnalyticsAttributes( Options.StageOptions, InOutAttributes );
	InOutAttributes.Emplace( TEXT( "StartTimeCode" ), LexToString( Options.StartTimeCode ) );
	InOutAttributes.Emplace( TEXT( "EndTimeCode" ), LexToString( Options.EndTimeCode ) );
	UsdUtils::AddAnalyticsAttributes( Options.Inner, InOutAttributes );
	InOutAttributes.Emplace( TEXT( "EndTimeCode" ), LexToString( Options.EndTimeCode ) );
	InOutAttributes.Emplace(
		TEXT( "ReExportIdenticalLevelsAndSequences" ),
		Options.bReExportIdenticalLevelsAndSequences
	);
	InOutAttributes.Emplace( TEXT( "ReExportIdenticalAssets" ), Options.bReExportIdenticalAssets );
}

