// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

#include "ImageWrapperBase.h"

#if WITH_UNREALEXR

THIRD_PARTY_INCLUDES_START
	#include "OpenEXR/include/ImfIO.h"
	#include "OpenEXR/include/ImathBox.h"
	#include "OpenEXR/include/ImfChannelList.h"
	#include "OpenEXR/include/ImfInputFile.h"
	#include "OpenEXR/include/ImfOutputFile.h"
	#include "OpenEXR/include/ImfArray.h"
	#include "OpenEXR/include/ImfHeader.h"
	#include "OpenEXR/include/ImfStdIO.h"
	#include "OpenEXR/include/ImfChannelList.h"
	#include "OpenEXR/include/ImfRgbaFile.h"
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

	virtual void Compress(int32 Quality) override;
	virtual void Uncompress(const ERGBFormat InFormat, int32 InBitDepth) override;
	virtual bool SetCompressed(const void* InCompressedData, int64 InCompressedSize) override;

protected:

	template <Imf::PixelType OutputFormat, typename sourcetype>
	void WriteFrameBufferChannel(Imf::FrameBuffer& ImfFrameBuffer, const char* ChannelName, const sourcetype* SrcData, TArray64<uint8>& ChannelBuffer);

	template <Imf::PixelType OutputFormat, typename sourcetype>
	void CompressRaw(const sourcetype* SrcData, bool bIgnoreAlpha);

	const char* GetRawChannelName(int ChannelIndex) const;

private:

	bool bUseCompression;
};

#endif // WITH_UNREALEXR
