// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDAssetOptions.h"

#include "AnalyticsEventAttribute.h"

void UsdUtils::AddAnalyticsAttributes(
	const FUsdMaterialBakingOptions& Options,
	TArray< FAnalyticsEventAttribute >& InOutAttributes
)
{
	FString BakedPropertiesString;
	{
		const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
		for ( const FPropertyEntry& PropertyEntry : Options.Properties )
		{
			FString PropertyString = PropertyEnum->GetNameByValue( PropertyEntry.Property ).ToString();
			PropertyString.RemoveFromStart( TEXT( "MP_" ) );
			BakedPropertiesString += PropertyString + TEXT( ", " );
		}

		BakedPropertiesString.RemoveFromEnd( TEXT( ", " ) );
	}

	InOutAttributes.Emplace( TEXT( "BakedProperties" ), BakedPropertiesString );
	InOutAttributes.Emplace( TEXT( "DefaultTextureSize" ), Options.DefaultTextureSize.ToString() );
}

void UsdUtils::AddAnalyticsAttributes(
	const FUsdMeshAssetOptions& Options,
	TArray< FAnalyticsEventAttribute >& InOutAttributes
)
{
	InOutAttributes.Emplace( TEXT( "UsePayload" ), LexToString( Options.bUsePayload ) );
	if ( Options.bUsePayload )
	{
		InOutAttributes.Emplace( TEXT( "PayloadFormat" ), Options.PayloadFormat );
	}
	InOutAttributes.Emplace( TEXT( "BakeMaterials" ), Options.bBakeMaterials );
	InOutAttributes.Emplace( TEXT( "RemoveUnrealMaterials" ), Options.bRemoveUnrealMaterials );
	if ( Options.bBakeMaterials )
	{
		UsdUtils::AddAnalyticsAttributes( Options.MaterialBakingOptions, InOutAttributes );
	}
	InOutAttributes.Emplace( TEXT( "LowestMeshLOD" ), LexToString( Options.LowestMeshLOD ) );
	InOutAttributes.Emplace( TEXT( "HighestMeshLOD" ), LexToString( Options.HighestMeshLOD ) );
}