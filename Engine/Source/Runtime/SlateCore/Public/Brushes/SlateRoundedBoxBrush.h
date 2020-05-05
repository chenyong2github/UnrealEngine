// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

struct SLATECORE_API FSlateRoundedBoxBrush
	: public FSlateBrush
{

	/** 
	 * Creates and initializes a new instance with the specified color and rounds based on height
	 *
	 * @param InColor 		Linear Fill Color 
	 * @param InRadius      Corner Radius
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FLinearColor& InColor,  const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize, 
					  InColor,
					  nullptr,
					  false
					 )
	{ 
	}


	/** 
	 * Creates and initializes a new instance with the specified color and corner radius
	 *
	 * @param InColor 		Linear Fill Color 
	 * @param InRadius      Corner Radius
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FLinearColor& InColor, float InRadius , const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor,
					  nullptr,
					  false
					 )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InRadius);
	}

	/** 
	 * Creates and initializes a new instance with the specified color and rounds based on height
	 *
	 * @param InColor 		 Linear Fill Color 
	 * @param InOutlineColor Outline Color 
	 * @param InOutlineWidth Outline Width or Thickness
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FLinearColor& InColor, const FLinearColor& InOutlineColor, float InOutlineWidth, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor,
					  nullptr,
					  false
					  )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InOutlineColor, InOutlineWidth);
	}



	/** 
	 * Creates and initializes a new instance with the specified color and corner radius
	 *
	 * @param InColor 		 Linear Fill Color 
	 * @param InRadius       Corner Radius
	 * @param InOutlineColor Outline Color 
	 * @param InOutlineWidth Outline Width or Thickness
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FLinearColor& InColor, float InRadius, const FLinearColor& InOutlineColor, float InOutlineWidth, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor,
					  nullptr,
					  false
					  )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InRadius, InOutlineColor, InOutlineWidth);
	}


	/** 
	 * Creates and initializes a new instance with the specified color and rounds based on height
	 *
	 * @param InColor 		Linear Fill Color 
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FColor& InColor, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.ReinterpretAsLinear(),
					  nullptr,
					  false
					 )
	{ 
	}

	/** 
	 * Creates and initializes a new instance with the specified color and corner radius
	 *
	 * @param InColor 		Linear Fill Color 
	 * @param InRadius      Corner Radius
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FColor& InColor, float InRadius , const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.ReinterpretAsLinear(),
					  nullptr,
					  false
					 )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InRadius);
	}

	/** 
	 * Creates and initializes a new instance with the specified color and rounds based on height
	 *
	 * @param InColor 		 Linear Fill Color 
	 * @param InOutlineColor Outline Color 
	 * @param InOutlineWidth Outline Width or Thickness
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FColor& InColor, const FColor& InOutlineColor, float InOutlineWidth, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.ReinterpretAsLinear(),
					  nullptr,
					  false
					  )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InOutlineColor.ReinterpretAsLinear(), InOutlineWidth);
	}



	/** 
	 * Creates and initializes a new instance with the specified color and corner radius
	 *
	 * @param InColor 		 Linear Fill Color 
	 * @param InRadius       Corner Radius
	 * @param InOutlineColor Outline Color 
	 * @param InOutlineWidth Outline Width or Thickness
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FColor& InColor, float InRadius, const FColor& InOutlineColor, float InOutlineWidth, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.ReinterpretAsLinear(),
					  nullptr,
					  false
					  )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InRadius, InOutlineColor.ReinterpretAsLinear(), InOutlineWidth);
	}

	/** 
	 * Creates and initializes a new instance with the specified color and rounds based on height
	 *
	 * @param InColor 		Linear Fill Color 
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FSlateColor& InColor , const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.GetSpecifiedColor(),
					  nullptr,
					  false
					 )
	{ 
	}


	/** 
	 * Creates and initializes a new instance with the specified color and corner radius
	 *
	 * @param InColor 		Linear Fill Color 
	 * @param InRadius      Corner Radius
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FSlateColor& InColor, float InRadius , const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.GetSpecifiedColor(),
					  nullptr,
					  false
					 )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InRadius);
	}

	/** 
	 * Creates and initializes a new instance with the specified color and rounds based on height
	 *
	 * @param InColor 		 Linear Fill Color 
	 * @param InOutlineColor Outline Color 
	 * @param InOutlineWidth Outline Width or Thickness
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FSlateColor& InColor, const FSlateColor& InOutlineColor, float InOutlineWidth, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.GetSpecifiedColor(),
					  nullptr,
					  false
					  )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InOutlineColor.GetSpecifiedColor(), InOutlineWidth);
	}

	/** 
	 * Creates and initializes a new instance with the specified color and corner radius
	 *
	 * @param InColor 		 Linear Fill Color 
	 * @param InRadius       Corner Radius
	 * @param InOutlineColor Outline Color 
	 * @param InOutlineWidth Outline Width or Thickness
	 */
	FORCENOINLINE FSlateRoundedBoxBrush( const FSlateColor& InColor, float InRadius, const FSlateColor& InOutlineColor, float InOutlineWidth, const FVector2D& InImageSize = FVector2D::ZeroVector)
		: FSlateBrush(ESlateBrushDrawType::RoundedBox, 
					  NAME_None, 
					  FMargin(0.0f), 
					  ESlateBrushTileType::NoTile, 
					  ESlateBrushImageType::NoImage, 
					  InImageSize,
					  InColor.GetSpecifiedColor(),
					  nullptr,
					  false
					  )
	{ 
		OutlineSettings = FSlateBrushOutlineSettings(InRadius, InOutlineColor.GetSpecifiedColor(), InOutlineWidth);
	}

};
