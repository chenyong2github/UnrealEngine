// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "RHI.h"

/*
* Interface for all texture sources in Pixel Streaming.
* These texture sources are used to populate video sources for video tracks.
*/
class IPixelStreamingTextureSource
{
public:
	IPixelStreamingTextureSource() = default;
	virtual ~IPixelStreamingTextureSource() = default;
	virtual bool IsAvailable() const = 0;
	virtual bool IsEnabled() const = 0;
	virtual void SetEnabled(bool bInEnabled) = 0;
	virtual int GetSourceHeight() const = 0;
	virtual int GetSourceWidth() const = 0;
	virtual FTexture2DRHIRef GetTexture() = 0;
	virtual FString GetName() const = 0;
};