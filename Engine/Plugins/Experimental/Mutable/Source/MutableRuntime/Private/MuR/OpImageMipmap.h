// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ImagePrivate.h"

namespace mu
{
	/** Support struct to keep preallocated data required for some mipmap operations. */
	struct FScratchImageMipmap
	{
		Ptr<Image> Uncompressed;
		Ptr<Image> UncompressedMips;
		Ptr<Image> CompressedMips;
	};

    /** Generate the mipmaps for images.
	* if bGenerateOnlyTail is true, generates the mips missing from Base to LevelCount and sets
	* them in Dest (the full chain is spit in two images). Otherwise generate the mips missing 
	* from Base up to LevelCount and append them in Dest to the already generated Base's mips.
    */
	void ImageMipmap(struct FWorkingMemoryManager&, int32 CompressionQuality, Image* Dest, const Image* Base,
		int32 LevelCount,
		const FMipmapGenerationSettings&, bool bGenerateOnlyTail = false);

	/** Mipmap separating the worst case treatment in 3 steps to manage allocations of temp data. */
	void ImageMipmap_PrepareScratch(FWorkingMemoryManager&, Image* Dest, const Image* Base, int32 LevelCount, FScratchImageMipmap&);
	void ImageMipmap(FScratchImageMipmap&, int32 CompressionQuality, Image* Dest, const Image* Base,
		int32 LevelCount,
		const FMipmapGenerationSettings&, bool bGenerateOnlyTail=false);
	void ImageMipmap_ReleaseScratch(FWorkingMemoryManager&, FScratchImageMipmap&);

	/** Update all the mipmaps in the image from the data in the base one. 
	* Only the mipmaps already existing in the image are updated.
	*/
	void ImageMipmapInPlace(int32 CompressionQuality, Image* Base, const FMipmapGenerationSettings& );

}
