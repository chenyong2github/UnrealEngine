// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingFrameAdapterProcess.h"
#include "FrameAdapter.h"
#include "Stats.h"

TSharedPtr<IPixelStreamingAdaptedOutputFrame> FPixelStreamingFrameAdapterProcess::ReadOutput()
{
	return Buffer.SwapAndRead();
}

void FPixelStreamingFrameAdapterProcess::Process(const IPixelStreamingInputFrame& InputFrame)
{
	if (IsBusy())
	{
		return;
	}

	bBusy = true;

	const int32 InputWidth = InputFrame.GetWidth();
	const int32 InputHeight = InputFrame.GetHeight();

	if (!IsInitialized())
	{
		Initialize(InputWidth, InputHeight);
	}

	checkf(InputWidth == ExpectedInputWidth && InputHeight == ExpectedInputHeight, TEXT("Adapter input resolution changes are not supported"));

	TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer = Buffer.GetWriteBuffer();
	OutputBuffer->Metadata = InputFrame.Metadata.Copy();
	OutputBuffer->Metadata.ProcessName = GetProcessName();
	OutputBuffer->Metadata.AdaptCallTime = FPlatformTime::Cycles64();
	bWasFinalized = false;

	BeginProcess(InputFrame, OutputBuffer);
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

void FPixelStreamingFrameAdapterProcess::Initialize(int32 InputWidth, int32 InputHeight)
{
	ExpectedInputWidth = InputWidth;
	ExpectedInputHeight = InputHeight;
	Buffer.Write(CreateOutputBuffer(InputWidth, InputHeight));
	Buffer.SwapWriteBuffers();
	Buffer.Write(CreateOutputBuffer(InputWidth, InputHeight));
	Buffer.SwapReadBuffers();
	Buffer.SwapWriteBuffers();
	Buffer.Write(CreateOutputBuffer(InputWidth, InputHeight));
	bHasOutput = false;
	bInitialized = true;
}

void FPixelStreamingFrameAdapterProcess::MarkAdaptProcessStarted()
{
	Buffer.GetWriteBuffer()->Metadata.AdaptProcessStartTime = FPlatformTime::Cycles64();
}

void FPixelStreamingFrameAdapterProcess::MarkAdaptProcessFinalizing()
{
	Buffer.GetWriteBuffer()->Metadata.AdaptProcessFinalizeTime = FPlatformTime::Cycles64();
	bWasFinalized = true;
}

void FPixelStreamingFrameAdapterProcess::EndProcess()
{
	checkf(bBusy, TEXT("Adapt process EndProcess called but we're not busy. Maybe double called?"));

	if (!bWasFinalized)
	{
		MarkAdaptProcessFinalizing();
	}
	
	Buffer.GetWriteBuffer()->Metadata.AdaptProcessEndTime = FPlatformTime::Cycles64();
	Buffer.SwapWriteBuffers();
	bBusy = false;
	bHasOutput = true;

	OnComplete.Broadcast();
}
