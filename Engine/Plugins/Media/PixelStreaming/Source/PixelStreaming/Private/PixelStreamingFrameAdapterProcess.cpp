// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingFrameAdapterProcess.h"
#include "Stats.h"
#include "PixelStreamingFrameAdapter.h"
#include "PixelStreamingSourceFrame.h"
#include "Stats.h"

TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> FPixelStreamingFrameAdapterProcess::ReadOutput()
{
	return Buffer.SwapAndRead();
}

void FPixelStreamingFrameAdapterProcess::Process(const FPixelStreamingSourceFrame& SourceFrame)
{
	if (IsBusy())
	{
		return;
	}

	bBusy = true;

	const int32 SourceWidth = SourceFrame.GetWidth();
	const int32 SourceHeight = SourceFrame.GetHeight();
	if (!IsInitialized())
	{
		Initialize(SourceWidth, SourceHeight);
	}
	else if (ResolutionChanged(SourceWidth, SourceHeight))
	{
		OnSourceResolutionChanged(ExpectedSourceWidth, ExpectedSourceHeight, SourceWidth, SourceHeight);
		Initialize(SourceWidth, SourceHeight);
	}

	BeginProcess(SourceFrame);
}

int32 FPixelStreamingFrameAdapterProcess::GetOutputLayerWidth()
{
	checkf(HasOutput(), TEXT("No output"));
	return Buffer.Read()->GetWidth();
}

int32 FPixelStreamingFrameAdapterProcess::GetOutputLayerHeight()
{
	checkf(HasOutput(), TEXT("No output"));
	return Buffer.Read()->GetHeight();
}

void FPixelStreamingFrameAdapterProcess::Initialize(int32 SourceWidth, int32 SourceHeight)
{
	Buffer.Write(CreateOutputBuffer(SourceWidth, SourceHeight));
	Buffer.SwapWriteBuffers();
	Buffer.Write(CreateOutputBuffer(SourceWidth, SourceHeight));
	Buffer.SwapReadBuffers();
	Buffer.SwapWriteBuffers();
	Buffer.Write(CreateOutputBuffer(SourceWidth, SourceHeight));
	ExpectedSourceWidth = SourceWidth;
	ExpectedSourceHeight = SourceHeight;
	bHasOutput = false;
	bInitialized = true;
}

TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> FPixelStreamingFrameAdapterProcess::GetWriteBuffer()
{
	return Buffer.GetWriteBuffer();
}

void FPixelStreamingFrameAdapterProcess::EndProcess()
{
	Buffer.SwapWriteBuffers();
	bBusy = false;
	bHasOutput = true;
}

bool FPixelStreamingFrameAdapterProcess::ResolutionChanged(int32 SourceWidth, int32 SourceHeight) const
{
	return ExpectedSourceWidth != SourceWidth || ExpectedSourceHeight != SourceHeight;
}
