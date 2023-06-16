// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreTextureSampleBase.h"

#include "MediaIOCoreTextureSampleConverter.h"

#include "RHI.h"
#include "RHIResources.h"

FMediaIOCoreTextureSampleBase::FMediaIOCoreTextureSampleBase()
	: Duration(FTimespan::Zero())
	, SampleFormat(EMediaTextureSampleFormat::Undefined)
	, Time(FTimespan::Zero())
	, Stride(0)
	, Width(0)
	, Height(0)
{
}


bool FMediaIOCoreTextureSampleBase::Initialize(const void* InVideoBuffer, uint32 InBufferSize, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs)
{
	FreeSample();

	if (!SetProperties(InStride, InWidth, InHeight, InSampleFormat, InTime, InFrameRate, InTimecode, InColorFormatArgs))
	{
		return false;
	}

	return SetBuffer(InVideoBuffer, InBufferSize);
}


bool FMediaIOCoreTextureSampleBase::Initialize(const TArray<uint8>& InVideoBuffer, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs)
{
	FreeSample();

	if (!SetProperties(InStride, InWidth, InHeight, InSampleFormat, InTime, InFrameRate, InTimecode, InColorFormatArgs))
	{
		return false;
	}

	return SetBuffer(InVideoBuffer);
}


bool FMediaIOCoreTextureSampleBase::Initialize(TArray<uint8>&& InVideoBuffer, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs)
{
	FreeSample();

	if (!SetProperties(InStride, InWidth, InHeight, InSampleFormat, InTime, InFrameRate, InTimecode, InColorFormatArgs))
	{
		return false;
	}

	return SetBuffer(MoveTemp(InVideoBuffer));
}


bool FMediaIOCoreTextureSampleBase::SetBuffer(const void* InVideoBuffer, uint32 InBufferSize)
{
	if (InVideoBuffer == nullptr)
	{
		return false;
	}

	Buffer.Reset(InBufferSize);
	Buffer.Append(reinterpret_cast<const uint8*>(InVideoBuffer), InBufferSize);

	return true;
}


bool FMediaIOCoreTextureSampleBase::SetBuffer(const TArray<uint8>& InVideoBuffer)
{
	if (InVideoBuffer.Num() == 0)
	{
		return false;
	}

	Buffer = InVideoBuffer;

	return true;
}

bool FMediaIOCoreTextureSampleBase::SetBuffer(TArray<uint8>&& InVideoBuffer)
{
	if (InVideoBuffer.Num() == 0)
	{
		return false;
	}

	Buffer = MoveTemp(InVideoBuffer);

	return true;
}

bool FMediaIOCoreTextureSampleBase::SetProperties(uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs)
{
	if (InSampleFormat == EMediaTextureSampleFormat::Undefined)
	{
		return false;
	}

	Stride = InStride;
	Width = InWidth;
	Height = InHeight;
	SampleFormat = InSampleFormat;
	Time = InTime;
	Duration = FTimespan(ETimespan::TicksPerSecond * InFrameRate.AsInterval());
	Timecode = InTimecode;
	Encoding = InColorFormatArgs.Encoding;
	ColorSpace = InColorFormatArgs.ColorSpace;
	ColorSpaceStruct = UE::Color::FColorSpace(ColorSpace);

	return true;
}

bool FMediaIOCoreTextureSampleBase::InitializeWithEvenOddLine(bool bUseEvenLine, const void* InVideoBuffer, uint32 InBufferSize, uint32 InStride, uint32 InWidth, uint32 InHeight, EMediaTextureSampleFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, const UE::MediaIOCore::FColorFormatArgs& InColorFormatArgs)
{
	FreeSample();

	if (!SetProperties(InStride, InWidth, InHeight/2, InSampleFormat, InTime, InFrameRate, InTimecode, InColorFormatArgs))
	{
		return false;
	}

	return SetBufferWithEvenOddLine(bUseEvenLine, InVideoBuffer, InBufferSize, InStride, InHeight);
}

bool FMediaIOCoreTextureSampleBase::SetBufferWithEvenOddLine(bool bUseEvenLine, const void* InVideoBuffer, uint32 InBufferSize, uint32 InStride, uint32 InHeight)
{
	Buffer.Reset(InBufferSize / 2);

	for (uint32 IndexY = (bUseEvenLine ? 0 : 1); IndexY < InHeight; IndexY += 2)
	{
		const uint8* Source = reinterpret_cast<const uint8*>(InVideoBuffer) + (IndexY*InStride);
		Buffer.Append(Source, InStride);
	}

	return true;
}

void* FMediaIOCoreTextureSampleBase::RequestBuffer(uint32 InBufferSize)
{
	FreeSample();
	Buffer.SetNumUninitialized(InBufferSize); // Reset the array without shrinking (Does not destruct items, does not de-allocate memory).
	return Buffer.GetData();
}

TSharedPtr<FMediaIOCorePlayerBase> FMediaIOCoreTextureSampleBase::GetPlayer() const
{
	return Player.Pin();
}

bool FMediaIOCoreTextureSampleBase::InitializeJITR(const FMediaIOCoreSampleJITRConfigurationArgs& Args)
{
	if (!Args.Player|| !Args.Converter)
	{
		return false;
	}

	// Native sample data
	Width  = Args.Width;
	Height = Args.Height;
	Time   = Args.Time;
	Timecode = Args.Timecode;

	// JITR data
	Player    = Args.Player;
	Converter = Args.Converter;
	EvaluationOffsetInSeconds = Args.EvaluationOffsetInSeconds;

	return true;
}

void FMediaIOCoreTextureSampleBase::CopyConfiguration(const TSharedPtr<FMediaIOCoreTextureSampleBase>& SourceSample)
{
	if (!SourceSample.IsValid())
	{
		return;
	}

	// Copy configuration parameters
	Stride = SourceSample->Stride;
	Width  = SourceSample->Width;
	Height = SourceSample->Height;
	SampleFormat = SourceSample->SampleFormat;
	Time = SourceSample->Time;
	Timecode = SourceSample->Timecode;
	Encoding = SourceSample->Encoding;
	ColorSpace = SourceSample->ColorSpace;
	ColorSpaceStruct = SourceSample->ColorSpaceStruct;

	// Save original sample
	OriginalSample = SourceSample;
}

#if WITH_ENGINE
IMediaTextureSampleConverter* FMediaIOCoreTextureSampleBase::GetMediaTextureSampleConverter()
{
	return Converter.Get();
}

FRHITexture* FMediaIOCoreTextureSampleBase::GetTexture() const
{
	return Texture.GetReference();
}
#endif

void FMediaIOCoreTextureSampleBase::SetTexture(TRefCountPtr<FRHITexture> InRHITexture)
{
	Texture = MoveTemp(InRHITexture);
}

void FMediaIOCoreTextureSampleBase::SetDestructionCallback(TFunction<void(TRefCountPtr<FRHITexture>)> InDestructionCallback)
{
	DestructionCallback = InDestructionCallback;
}

void FMediaIOCoreTextureSampleBase::ShutdownPoolable()
{
	if (DestructionCallback)
	{
		DestructionCallback(Texture);
	}

	FreeSample();
}

const FMatrix& FMediaIOCoreTextureSampleBase::GetYUVToRGBMatrix() const
{
	switch (ColorSpace)
	{
	case UE::Color::EColorSpace::sRGB:
		return MediaShaders::YuvToRgbRec709Scaled;
	case UE::Color::EColorSpace::Rec2020:
		return MediaShaders::YuvToRgbRec2020Scaled;
	default:
		return MediaShaders::YuvToRgbRec709Scaled;
	}
}

bool FMediaIOCoreTextureSampleBase::IsOutputSrgb() const
{
	return Encoding == UE::Color::EEncoding::sRGB;
}

FMatrix44f FMediaIOCoreTextureSampleBase::GetGamutToXYZMatrix() const
{
	return FMatrix44f(ColorSpaceStruct.GetRgbToXYZ().GetTransposed());
}

FVector2f FMediaIOCoreTextureSampleBase::GetWhitePoint() const
{
	return FVector2f(ColorSpaceStruct.GetWhiteChromaticity());
}

FVector2f FMediaIOCoreTextureSampleBase::GetDisplayPrimaryRed() const
{
	return FVector2f(ColorSpaceStruct.GetRedChromaticity());
}

FVector2f FMediaIOCoreTextureSampleBase::GetDisplayPrimaryGreen() const
{
	return FVector2f(ColorSpaceStruct.GetGreenChromaticity());
}

FVector2f FMediaIOCoreTextureSampleBase::GetDisplayPrimaryBlue() const
{
	return FVector2f(ColorSpaceStruct.GetBlueChromaticity());
}

UE::Color::EEncoding FMediaIOCoreTextureSampleBase::GetEncodingType() const
{
	return Encoding;
}

float FMediaIOCoreTextureSampleBase::GetHDRNitsNormalizationFactor() const
{
	return (GetEncodingType() == UE::Color::EEncoding::sRGB || GetEncodingType() == UE::Color::EEncoding::Linear) ? 1.0f : kMediaSample_HDR_NitsNormalizationFactor;
}
