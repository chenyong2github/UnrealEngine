// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EElectraDecoderPlatformOutputHandleType
{
	MFSample,
	DXDevice,
	DXDeviceContext,
	DXTexture,
	ImageBuffers			// IElectraDecoderVideoOutputImageBuffers interface
};

class IElectraDecoderVideoOutputImageBuffers
{
public:
	// Return the 4cc of the codec. This determines how the rest of the information must be interpreted.
	virtual uint32 GetCodec4CC() const = 0;

	// Returns the number of separate image buffers making up the frame.
	virtual int32 GetNumberOfBuffers() const = 0;

	// Returns the n'th image buffer.
	virtual TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> GetBufferByIndex(int32 InBufferIndex) const = 0;

	// Returns the n'th image buffer format, which is a specific value of the codec.
	virtual uint64 GetBufferFormatByIndex(int32 InBufferIndex) const = 0;
};
