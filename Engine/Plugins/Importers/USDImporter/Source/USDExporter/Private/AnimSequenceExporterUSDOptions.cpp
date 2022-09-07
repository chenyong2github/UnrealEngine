// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimSequenceExporterUSDOptions.h"

#include "AnalyticsEventAttribute.h"

void UsdUtils::AddAnalyticsAttributes(
	const UAnimSequenceExporterUSDOptions& Options,
	TArray< FAnalyticsEventAttribute >& InOutAttributes
)
{
	UsdUtils::AddAnalyticsAttributes( Options.StageOptions, InOutAttributes );
	InOutAttributes.Emplace( TEXT( "ExportPreviewMesh" ), LexToString( Options.bExportPreviewMesh ) );
	if ( Options.bExportPreviewMesh )
	{
		UsdUtils::AddAnalyticsAttributes( Options.PreviewMeshOptions, InOutAttributes );
	}
	InOutAttributes.Emplace( TEXT( "ReExportIdenticalAssets" ), Options.bReExportIdenticalAssets );
}
