// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceExporterUSDOptions.h"

#include "LevelExporterUSDOptions.h"

#include "AnalyticsEventAttribute.h"

void UsdUtils::AddAnalyticsAttributes(
	const ULevelSequenceExporterUsdOptions& Options,
	TArray< struct FAnalyticsEventAttribute >& InOutAttributes
)
{
	InOutAttributes.Emplace( TEXT( "TimeCodesPerSecond" ), LexToString( Options.TimeCodesPerSecond ) );
	InOutAttributes.Emplace( TEXT( "OverrideExportRange" ), Options.bOverrideExportRange );
	if ( Options.bOverrideExportRange )
	{
		InOutAttributes.Emplace( TEXT( "StartFrame" ), LexToString( Options.StartFrame ) );
		InOutAttributes.Emplace( TEXT( "EndFrame" ), LexToString( Options.EndFrame ) );
	}
	InOutAttributes.Emplace( TEXT( "ExportSubsequencesAsLayers" ), Options.bExportSubsequencesAsLayers );
	InOutAttributes.Emplace( TEXT( "ExportLevel" ), Options.bExportLevel );
	if ( Options.bExportLevel )
	{
		InOutAttributes.Emplace( TEXT( "UseExportedLevelAsSublayer" ), Options.bUseExportedLevelAsSublayer );
	}
	InOutAttributes.Emplace(
        TEXT( "ReExportIdenticalLevelsAndSequences" ),
        Options.bReExportIdenticalLevelsAndSequences
    );
	InOutAttributes.Emplace( TEXT( "ReExportIdenticalAssets" ), Options.bReExportIdenticalAssets );
	if ( Options.bExportLevel )
	{
		UsdUtils::AddAnalyticsAttributes( Options.LevelExportOptions, InOutAttributes );
	}
}