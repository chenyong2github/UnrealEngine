// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/EnumAsByte.h"
#include "Containers/UnrealString.h"
#include "IImageWrapper.h"


/**
 * The abstract helper class for handling the different image formats
 */
class FImageWrapperBase
	: public IImageWrapper
{
public:

	/** Default Constructor. */
	FImageWrapperBase();

public:

	/**
	 * Gets the image's raw data.
	 *
	 * @return A read-only byte array containing the data.
	 */
	const TArray64<uint8>& GetRawData() const
	{
		return RawData;
	}

	/**
	 * Moves the image's raw data into the provided array.
	 *
	 * @param OutRawData The destination array.
	 */
	void MoveRawData(TArray64<uint8>& OutRawData)
	{
		OutRawData = MoveTemp(RawData);
	}

public:

	/**
	 * Compresses the data.
	 *
	 * @param Quality The compression quality.
	 * 
	 * returns void. call SetError() in your implementation if you fail.
	 */
	virtual void Compress(int32 Quality) = 0;

	/**
	 * Resets the local variables.
	 */
	virtual void Reset();

	/**
	 * Sets last error message.
	 *
	 * @param ErrorMessage The error message to set.
	 */
	void SetError(const TCHAR* ErrorMessage);

	/**  
	 * Function to uncompress our data 
	 *
	 * @param InFormat How we want to manipulate the RGB data
	 * 
	 * returns void. call SetError() in your implementation if you fail.
	 */
	virtual void Uncompress(const ERGBFormat InFormat, int32 InBitDepth) = 0;

public:

	//~ IImageWrapper interface

	virtual TArray64<uint8> GetCompressed(int32 Quality = 0) override;

	virtual int32 GetBitDepth() const override
	{
		return BitDepth;
	}

	virtual ERGBFormat GetFormat() const override
	{
		return Format;
	}

	virtual int32 GetHeight() const override
	{
		return Height;
	}

	virtual bool GetRaw(const ERGBFormat InFormat, int32 InBitDepth, TArray64<uint8>& OutRawData) override;

	virtual int32 GetWidth() const override
	{
		return Width;
	}

	virtual int32 GetNumFrames_DEPRECATED() const override
	{
		return NumFrames_DEPRECATED;
	}
	
	virtual int32 GetFramerate_DEPRECATED() const override
	{
		return Framerate_DEPRECATED;
	}

	virtual bool SetCompressed(const void* InCompressedData, int64 InCompressedSize) override;
	virtual bool SetRaw(const void* InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth, const int32 InBytesPerRow = 0) override;
	virtual bool SetAnimationInfo_DEPRECATED(int32 InNumFrames, int32 InFramerate) override;

protected:

	/** Arrays of compressed/raw data */
	TArray64<uint8> RawData;
	TArray64<uint8> CompressedData;

	/** Format of the raw data */
	ERGBFormat RawFormat;
	int8 RawBitDepth;

	/** Bytes per row for the raw data */
	int32 RawBytesPerRow;

	/** Format of the image */
	ERGBFormat Format;

	/** Bit depth of the image */
	int8 BitDepth;

	/** Width/Height of the image data */
	int32 Width;
	int32 Height;
	
	/** Animation information */
	int32 NumFrames_DEPRECATED;
	int32 Framerate_DEPRECATED;

	/** Last Error Message. */
	FString LastError;
};
