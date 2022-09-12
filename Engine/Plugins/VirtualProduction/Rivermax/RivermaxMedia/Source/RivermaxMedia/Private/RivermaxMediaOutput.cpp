// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaOutput.h"

#include "MediaOutput.h"
#include "RivermaxMediaCapture.h"


/* URivermaxMediaOutput
*****************************************************************************/

bool URivermaxMediaOutput::Validate(FString& OutFailureReason) const
{
	if (!Super::Validate(OutFailureReason))
	{
		return false;
	}
	
	return true;
}

FIntPoint URivermaxMediaOutput::GetRequestedSize() const
{
	return Resolution;
}

EPixelFormat URivermaxMediaOutput::GetRequestedPixelFormat() const
{
	EPixelFormat Result = EPixelFormat::PF_A2B10G10R10;
	switch (PixelFormat)
	{
	case ERivermaxMediaOutputPixelFormat::PF_8BIT_YUV422:
		Result = EPixelFormat::PF_B8G8R8A8; //To be updated to use compute shader
		break;
	default: // All pixel formats use output buffer as their output so no need for a texture format
		Result = EPixelFormat::PF_A2B10G10R10;
		break;
	}
	return Result;
}

EMediaCaptureConversionOperation URivermaxMediaOutput::GetConversionOperation(EMediaCaptureSourceType InSourceType) const
{
	EMediaCaptureConversionOperation Result = EMediaCaptureConversionOperation::CUSTOM;
	switch (PixelFormat)
	{
	case ERivermaxMediaOutputPixelFormat::PF_8BIT_YUV422:
		Result = EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT;
		break;
	case ERivermaxMediaOutputPixelFormat::PF_10BIT_YUV422:
	case ERivermaxMediaOutputPixelFormat::PF_8BIT_RGB:
	case ERivermaxMediaOutputPixelFormat::PF_10BIT_RGB:
	case ERivermaxMediaOutputPixelFormat::PF_FLOAT16_RGB:
	default:
		Result = EMediaCaptureConversionOperation::CUSTOM; //We handle all conversion for rivermax since it's really tied to endianness of 2110
		break;
	}
	return Result;
}

UMediaCapture* URivermaxMediaOutput::CreateMediaCaptureImpl()
{
	UMediaCapture* Result = NewObject<URivermaxMediaCapture>();
	if (Result)
	{
		Result->SetMediaOutput(this);
	}
	return Result;
}

#if WITH_EDITOR
bool URivermaxMediaOutput::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}

void URivermaxMediaOutput::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif //WITH_EDITOR

