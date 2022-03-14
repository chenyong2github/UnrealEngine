// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/FrameRate.h"
#include "Math/IntPoint.h"

class FString;


class OPENEXRWRAPPER_API FOpenExr
{
public:

	static void SetGlobalThreadCount(uint16 ThreadCount);
};


class OPENEXRWRAPPER_API FRgbaInputFile
{
public:

	FRgbaInputFile(const FString& FilePath);
	FRgbaInputFile(const FString& FilePath, uint16 ThreadCount);
	~FRgbaInputFile();

public:

	const TCHAR* GetCompressionName() const;
	FIntPoint GetDataWindow() const;
	FFrameRate GetFrameRate(const FFrameRate& DefaultValue) const;
	int32 GetUncompressedSize() const;
	int32 GetNumChannels() const;

	bool IsComplete() const;
	bool HasInputFile() const;
	void ReadPixels(int32 StartY, int32 EndY);
	void SetFrameBuffer(void* Buffer, const FIntPoint& Stride);

	/**
	 * Get an attribute from the image.
	 *
	 * @param Name		Name of attribute.
	 * @param Value		Will be set to the value of the attribute if the attribute is found.
	 *					Will NOT be set if the attribute is not found.
	 * @return			True if the attribute was found, false otherwise.
	 */
	bool GetIntAttribute(const FString& Name, int32& Value);

private:

	void* InputFile;
};

/**
 * Use this to write out tiled EXR images.
 * 
 * Add any attributes after construction.
 * Then you can call CreateOutputFile.
 * After that, you can write out data to the file.
 */
class OPENEXRWRAPPER_API FTiledRgbaOutputFile
{
public:
	/**
	 * Constructor.
	 * 
	 * @param DisplayWindowMin		Normally (0, 0).
	 * @param DisplayWindowMax		Normally (width - 1, height - 1).
	 * @param DataWindowMin			Normally (0, 0).
	 * @param DataWindowMax			Normally (width - 1, height - 1).
	 */
	FTiledRgbaOutputFile(
		const FIntPoint& DisplayWindowMin,
		const FIntPoint& DisplayWindowMax,
		const FIntPoint& DataWindowMin,
		const FIntPoint& DataWindowMax);

	/**
	 * Destructor.
	 */
	~FTiledRgbaOutputFile();

	/**
	 * Call this to add an attribute to the EXR file.
	 * This MUST be called before CreateOutputFile.
	 * 
	 * @param Name		Name for this attribute.
	 * @param Value		Value for this attribute.
	 */
	void AddIntAttribute(const FString& Name, int32 Value);

	/**
	 * Call this after adding any attributes BUT before doing anything else.
	 *
	 * @param FilePath			Filename to save to.
	 * @param TileWidth			Width of a tile.
	 * @param TileHeight		Height of a tile.
	 * @param NumChannels		Number of channels out write out.
	 * @param bIsMipsEnabled	True to enable mip mapping.
	 */
	void CreateOutputFile(const FString& FilePath,
		int32 TileWidth, int32 TileHeight, int32 NumChannels, bool bIsMipsEnabled);

	/**
	 * Call this to get the number of mipmap levels.
	 */
	int32 GetNumberOfMipLevels();

	/**
	 * Call this prior to WriteTile to set where the data is coming from.
	 * 
	 * @param Buffer		Source of data.
	 * @param Stride		A pixels location is calculated by Buffer + x * StrideX + y * StrideY.
	 */
	void SetFrameBuffer(void* Buffer, const FIntPoint& Stride);

	/**
	 * Call this to write data to a specific tile.
	 *
	 * @param TileX			X coordinate of tile.
	 * @param TileY			Y coordinate of tile.
	 * @param MipLevel		Mipmap level of tile.
	 */
	void WriteTile(int32 TileX, int32 TileY, int32 MipLevel);

private:
	/** Stores the EXR header object. */
	void* Header;
	/** Stores the EXR object. */
	void* OutputFile;
};
