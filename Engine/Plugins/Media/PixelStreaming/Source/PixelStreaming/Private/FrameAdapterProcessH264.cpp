// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterProcessH264.h"
#include "PixelStreamingSourceFrame.h"
#include "FrameAdapterH264.h"
#include "UtilsRender.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	FFrameAdapterProcessH264::FFrameAdapterProcessH264(float InScale)
		: Scale(InScale)
	{
		Fence = GDynamicRHI->RHICreateGPUFence(TEXT("FFrameAdapterProcessH264 Fence"));
	}

	TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> FFrameAdapterProcessH264::CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight)
	{
		const int32 Width = SourceWidth * Scale;
		const int32 Height = SourceHeight * Scale;

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

		return MakeShared<FAdaptedVideoFrameLayerH264>(GDynamicRHI->RHICreateTexture(TextureDesc));
	}

	void FFrameAdapterProcessH264::BeginProcess(const FPixelStreamingSourceFrame& SourceFrame)
	{
		CurrentOuputBuffer = StaticCast<FAdaptedVideoFrameLayerH264*>(GetWriteBuffer().Get());
		CurrentOuputBuffer->Metadata = SourceFrame.Metadata.Copy();
		CurrentOuputBuffer->Metadata.ProcessName = "H264";
		CurrentOuputBuffer->Metadata.AdaptCallTime = FPlatformTime::Cycles64();

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		RHICmdList.EnqueueLambda([CurrentOuputBuffer = CurrentOuputBuffer](FRHICommandListImmediate&) { CurrentOuputBuffer->Metadata.AdaptProcessStartTime = FPlatformTime::Cycles64(); });

		CopyTexture(RHICmdList, SourceFrame.FrameTexture, CurrentOuputBuffer->GetFrameTexture(), Fence);

		// by adding this shared ref to the rhi lambda we can ensure that 'this' will not be destroyed
		// until after the rhi thread is done with it, so all the commands will still have valid references.
		TSharedRef<FFrameAdapterProcessH264> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessH264>(AsShared());
		RHICmdList.EnqueueLambda([ThisRHIRef](FRHICommandListImmediate&) { ThisRHIRef->CheckComplete(); });
	}

	void FFrameAdapterProcessH264::CheckComplete()
	{
		if (!Fence->Poll())
		{
			TSharedRef<FFrameAdapterProcessH264> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessH264>(AsShared());
			AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [ThisRHIRef]() {
				ThisRHIRef->CheckComplete();
			});
		}
		else
		{
			OnRHIStageComplete();
		}
	}

	void FFrameAdapterProcessH264::OnRHIStageComplete()
	{
		checkf(Fence->Poll(), TEXT("Fence was not set. Backbuffer copy may not have completed."));
		Fence->Clear();
		CurrentOuputBuffer->Metadata.AdaptProcessFinalizeTime = FPlatformTime::Cycles64();
		CurrentOuputBuffer->Metadata.AdaptProcessEndTime = FPlatformTime::Cycles64();
		CurrentOuputBuffer = nullptr;
		EndProcess();
	}
} // namespace UE::PixelStreaming
