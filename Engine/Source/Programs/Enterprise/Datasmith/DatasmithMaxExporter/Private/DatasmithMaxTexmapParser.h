// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DatasmithDefinitions.h"

#include "Containers/Array.h"

class Texmap;
class PBBitmap;

namespace DatasmithMaxTexmapParser
{
	struct FMapParameter
	{
		FMapParameter()
			: Map( nullptr )
			, bEnabled( true )
			, Weight( 1.f )
		{
		}

		Texmap* Map;
		bool bEnabled;
		float Weight;
	};

	struct FWeightedColorParameter
	{
		FWeightedColorParameter()
			: Value( FLinearColor::White )
			, Weight( 1.f )
		{
		}

		FLinearColor Value;
		float Weight;
	};

	struct FCompositeTexmapParameters
	{
		struct FLayer
		{
			FLayer()
				: CompositeMode( EDatasmithCompositeCompMode::Alpha )
			{
			}

			EDatasmithCompositeCompMode CompositeMode;

			FMapParameter Map;
			FMapParameter Mask;
		};

		TArray< FLayer > Layers;
	};

	FCompositeTexmapParameters ParseCompositeTexmap( Texmap* InTexmap );

	struct FNormalMapParameters
	{
		FMapParameter NormalMap;
		FMapParameter BumpMap;

		bool bFlipGreen = false;
		bool bFlipRed = false;
		bool bSwapRedAndGreen = false;
	};

	FNormalMapParameters ParseNormalMap( Texmap* InTexmap );

	struct FAutodeskBitmapParameters
	{
		PBBitmap* SourceFile = nullptr;
		float Brightness = 1;
		bool InvertImage = false;
		FVector2D Position = FVector2D(0,0);
		float Rotation = 0;
		FVector2D Scale = FVector2D(1, 1);
		bool RepeatHorizontal = true;
		bool RepeatVertical = true;
		float BlurValue = 0;
		float BlurOffset = 0;
		float FilteringValue = 0;
		int MapChannel = 1;
	};

	FAutodeskBitmapParameters ParseAutodeskBitmap(Texmap* InTexmap);
}