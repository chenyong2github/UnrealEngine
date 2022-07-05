// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterProcessRHIToH264.h"
#include "PixelStreamingInputFrameRHI.h"
#include "PixelStreamingAdaptedOutputFrameH264.h"
#include "UtilsRender.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	FFrameAdapterProcessRHIToH264::FFrameAdapterProcessRHIToH264(float InScale)
		: Scale(InScale)
	{
		Fence = GDynamicRHI->RHICreateGPUFence(TEXT("FFrameAdapterProcessRHIToH264 Fence"));
	}

	TSharedPtr<IPixelStreamingAdaptedOutputFrame> FFrameAdapterProcessRHIToH264::CreateOutputBuffer(int32 InputWidth, int32 InputHeight)
	{
		const int32 Width = InputWidth * Scale;
		const int32 Height = InputHeight * Scale;

		FRHITextureCreateDesc TextureDesc =
			FRHITextureCreateDesc::Create2D(TEXT("FFrameDataH264 Texture"), Width, Height, EPixelFormat::PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::RenderTargetable)
			.SetInitialState(ERHIAccess::Present)
			.DetermineInititialState();

		if (RHIGetInterfaceType() == ERHIInterfaceType::Vulkan)
		{
			TextureDesc.AddFlags(ETextureCreateFlags::External);
		}
		else
		{
			TextureDesc.AddFlags(ETextureCreateFlags::Shared);
		}

		return MakeShared<FPixelStreamingAdaptedOutputFrameH264>(GDynamicRHI->RHICreateTexture(TextureDesc));
	}

	void FFrameAdapterProcessRHIToH264::BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer)
	{
		checkf(InputFrame.GetType() == EPixelStreamingInputFrameType::RHI, TEXT("Incorrect source frame coming into frame adapter process."));

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		RHICmdList.EnqueueLambda([this](FRHICommandListImmediate&) { MarkAdaptProcessStarted(); });

		const FPixelStreamingInputFrameRHI& RHISourceFrame = StaticCast<const FPixelStreamingInputFrameRHI&>(InputFrame);
		FPixelStreamingAdaptedOutputFrameH264* OutputH264Buffer = StaticCast<FPixelStreamingAdaptedOutputFrameH264*>(OutputBuffer.Get());
		CopyTexture(RHICmdList, RHISourceFrame.FrameTexture, OutputH264Buffer->GetFrameTexture(), Fence);

		// by adding this shared ref to the rhi lambda we can ensure that 'this' will not be destroyed
		// until after the rhi thread is done with it, so all the commands will still have valid references.
		TSharedRef<FFrameAdapterProcessRHIToH264> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessRHIToH264>(AsShared());
		RHICmdList.EnqueueLambda([ThisRHIRef](FRHICommandListImmediate&) { ThisRHIRef->CheckComplete(); });
	}

	void FFrameAdapterProcessRHIToH264::CheckComplete()
	{
		if (!Fence->Poll())
		{
			TSharedRef<FFrameAdapterProcessRHIToH264> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessRHIToH264>(AsShared());
			AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [ThisRHIRef]() {
				ThisRHIRef->CheckComplete();
			});
		}
		else
		{
			OnRHIStageComplete();
		}
	}

	void FFrameAdapterProcessRHIToH264::OnRHIStageComplete()
	{
		checkf(Fence->Poll(), TEXT("Fence was not set. Backbuffer copy may not have completed."));
		Fence->Clear();
		EndProcess();
	}
} // namespace UE::PixelStreaming
