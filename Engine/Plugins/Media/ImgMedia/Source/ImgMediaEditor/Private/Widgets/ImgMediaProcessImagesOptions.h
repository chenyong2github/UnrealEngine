// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains options for processing image sequences.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ImgMediaProcessImagesOptions.generated.h"

UCLASS(MinimalAPI)
class UImgMediaProcessImagesOptions : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	/** The directory that contains the image sequence files. */
	UPROPERTY(EditAnywhere, Category = Sequence)
	FFilePath SequencePath;

	/** The directory to output the processed image sequence files to. */
	UPROPERTY(EditAnywhere, Category = Sequence)
	FDirectoryPath OutputPath;

	/** If checked, then use our custom format with a single file for all tiles. */
	UPROPERTY(EditAnywhere, Category = Sequence)
	bool bUseCustomFormat;

	/** If checked, then enable mip mapping. */
	UPROPERTY(EditAnywhere, Category = Sequence)
	bool bEnableMipMapping;
	
	/** Width of a tile in pixels. If 0, then do not make tiles. */
	UPROPERTY(EditAnywhere, Transient, Category = Tiles)
	int32 TileSizeX = 0;

	/** Height of a tile in pixels. If 0, then do not make tiles. */
	UPROPERTY(EditAnywhere, Transient, Category = Tiles)
	int32 TileSizeY = 0;

	/** Number of pixels to duplicate along each tile border. */
	UPROPERTY(EditAnywhere, Transient, Category = Tiles, meta = (ClampMin = "0.0"))
	int32 TileBorder = 0;

	/** Number of tiles in the X direction. If 0, then there are no tiles. */
	UPROPERTY(VisibleAnywhere, Transient, Category = Tiles)
	int32 NumTilesX = 0;

	/** Number of tiles in the Y direction. If 0, then there are no tiles. */
	UPROPERTY(VisibleAnywhere, Transient, Category = Tiles)
	int32 NumTilesY = 0;
};
