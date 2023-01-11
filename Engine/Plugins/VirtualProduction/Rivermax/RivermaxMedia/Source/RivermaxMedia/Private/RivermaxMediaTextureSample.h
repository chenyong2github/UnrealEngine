// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaIOCoreTextureSampleBase.h"

#include "MediaShaders.h"
#include "RivermaxMediaSource.h"
#include "Templates/SharedPointer.h"

class FRivermaxMediaTextureSampleConverter;

/**
 * Implements a media texture sample for RivermaxMedia.
 */
class FRivermaxMediaTextureSample : public FMediaIOCoreTextureSampleBase, public TSharedFromThis<FRivermaxMediaTextureSample>
{
	using Super = FMediaIOCoreTextureSampleBase;

public:

	FRivermaxMediaTextureSample();


	//~ Begin IMediaTextureSample interface
	virtual const FMatrix& GetYUVToRGBMatrix() const override;
	virtual IMediaTextureSampleConverter* GetMediaTextureSampleConverter() override;
	virtual bool IsOutputSrgb() const override;
	//~ End IMediaTextureSample interface

	bool ConfigureSample(uint32 InWidth, uint32 InHeight, uint32 InStride, ERivermaxMediaSourcePixelFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, bool bInIsSRGBInput);

	/** Initialized a RDG buffer based on the description required. Only useful for gpudirect functionality */
	void InitializeGPUBuffer(const FIntPoint& InResolution, ERivermaxMediaSourcePixelFormat InSampleFormat);
	
	/** Returns RDG allocated buffer */
	TRefCountPtr<FRDGPooledBuffer> GetGPUBuffer() const;

private:
	

	/** Texture converted used to handle incoming 2110 formats and convert them to RGB textures the engine handles */
	TUniquePtr<FRivermaxMediaTextureSampleConverter> TextureConverter;
	
	/** Pooled buffer used for gpudirect functionality. Received content will already be on GPU when received from NIC */
	TRefCountPtr<FRDGPooledBuffer> GPUBuffer;
};

/*
 * Implements a pool for Rivermax texture sample objects.
 */
class FRivermaxMediaTextureSamplePool : public TMediaObjectPool<FRivermaxMediaTextureSample> { };
