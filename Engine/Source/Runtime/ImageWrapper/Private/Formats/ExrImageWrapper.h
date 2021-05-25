// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

#include "ImageWrapperBase.h"

#if WITH_UNREALEXR

THIRD_PARTY_INCLUDES_START
	#include "openexr/ImfIO.h"
	#include "openexr/ImathBox.h"
	#include "openexr/ImfChannelList.h"
	#include "openexr/ImfInputFile.h"
	#include "openexr/ImfOutputFile.h"
	#include "openexr/ImfArray.h"
	#include "openexr/ImfHeader.h"
	#include "openexr/ImfStdIO.h"
	#include "openexr/ImfChannelList.h"
	#include "openexr/ImfRgbaFile.h"
THIRD_PARTY_INCLUDES_END


/**
 * OpenEXR implementation of the helper class
 */
class FExrImageWrapper
	: public FImageWrapperBase
{
public:

	/**
	 * Default Constructor.
	 */
	FExrImageWrapper();

public:

	//~ FImageWrapper interface

	virtual bool SetRaw(const void* InRawData, int64 InRawSize, const int32 InWidth, const int32 InHeight, const ERGBFormat InFormat, const int32 InBitDepth) override;
	virtual bool SetCompressed(const void* InCompressedData, int64 InCompressedSize) override;
	virtual void Compress(int32 Quality) override;
	virtual void Uncompress(const ERGBFormat InFormat, int32 InBitDepth) override;
};

#endif // WITH_UNREALEXR
