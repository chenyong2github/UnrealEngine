// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterProcessI420CPU.h"
#include "FrameAdapterI420.h"
#include "libyuv/convert.h"
#include "UtilsRender.h"
#include "PixelStreamingSourceFrame.h"

namespace UE::PixelStreaming
{
	FFrameAdapterProcessI420CPU::FFrameAdapterProcessI420CPU(float InScale)
		: Scale(InScale)
	{
	}

	FFrameAdapterProcessI420CPU::~FFrameAdapterProcessI420CPU()
	{
		CleanUp();
	}

	void FFrameAdapterProcessI420CPU::Initialize(int32 SourceWidth, int32 SourceHeight)
	{
		const int32 Width = SourceWidth * Scale;
		const int32 Height = SourceHeight * Scale;

		FRHITextureCreateDesc TextureDesc =
			FRHITextureCreateDesc::Create2D(TEXT("FFrameAdapterProcessI420CPU StagingTexture"), Width, Height, EPixelFormat::PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::RenderTargetable)
			.SetInitialState(ERHIAccess::CopySrc)
			.DetermineInititialState();

		if (RHIGetInterfaceType() == ERHIInterfaceType::Vulkan)
		{
			TextureDesc.AddFlags(ETextureCreateFlags::External);
		}
		else
		{
			TextureDesc.AddFlags(ETextureCreateFlags::Shared);
		}

		StagingTexture = GDynamicRHI->RHICreateTexture(TextureDesc);

		FRHITextureCreateDesc ReadbackDesc =
			FRHITextureCreateDesc::Create2D(TEXT("FFrameAdapterProcessI420CPU ReadbackTexture"), Width, Height, EPixelFormat::PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::CPUReadback)
			.SetInitialState(ERHIAccess::CPURead)
			.DetermineInititialState();

		ReadbackTexture = GDynamicRHI->RHICreateTexture(ReadbackDesc);

		int32 BufferWidth = 0, BufferHeight = 0;
		GDynamicRHI->RHIMapStagingSurface(ReadbackTexture, nullptr, ResultsBuffer, BufferWidth, BufferHeight);
		MappedStride = BufferWidth;

		FPixelStreamingFrameAdapterProcess::Initialize(SourceWidth, SourceHeight);
	}

	void FFrameAdapterProcessI420CPU::OnSourceResolutionChanged(int32 OldWidth, int32 OldHeight, int32 NewWidth, int32 NewHeight)
	{
		CleanUp();
	}

	TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> FFrameAdapterProcessI420CPU::CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight)
	{
		const int32 Width = SourceWidth * Scale;
		const int32 Height = SourceHeight * Scale;
		return MakeShared<FAdaptedVideoFrameLayerI420>(webrtc::I420Buffer::Create(Width, Height));
	}

	void FFrameAdapterProcessI420CPU::BeginProcess(const FPixelStreamingSourceFrame& SourceFrame)
	{
		CurrentOuputBuffer = StaticCast<FAdaptedVideoFrameLayerI420*>(GetWriteBuffer().Get());
		CurrentOuputBuffer->Metadata = SourceFrame.Metadata.Copy();
		CurrentOuputBuffer->Metadata.ProcessName = "I420 CPU";
		CurrentOuputBuffer->Metadata.AdaptCallTime = FPlatformTime::Cycles64();

		FTexture2DRHIRef SourceTexture = SourceFrame.FrameTexture;

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		RHICmdList.EnqueueLambda([CurrentOuputBuffer = CurrentOuputBuffer](FRHICommandListImmediate&) { CurrentOuputBuffer->Metadata.AdaptProcessStartTime = FPlatformTime::Cycles64(); });

		RHICmdList.Transition(FRHITransitionInfo(SourceFrame.FrameTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
		RHICmdList.Transition(FRHITransitionInfo(StagingTexture, ERHIAccess::CopySrc, ERHIAccess::CopyDest));
		CopyTexture(RHICmdList, SourceFrame.FrameTexture, StagingTexture, nullptr);

		RHICmdList.Transition(FRHITransitionInfo(StagingTexture, ERHIAccess::CopyDest, ERHIAccess::CopySrc));
		RHICmdList.Transition(FRHITransitionInfo(ReadbackTexture, ERHIAccess::CPURead, ERHIAccess::CopyDest));
		RHICmdList.CopyTexture(StagingTexture, ReadbackTexture, {});

		RHICmdList.Transition(FRHITransitionInfo(ReadbackTexture, ERHIAccess::CopyDest, ERHIAccess::CPURead));

		// by adding this shared ref to the rhi lambda we can ensure that 'this' will not be destroyed
		// until after the rhi thread is done with it, so all the commands will still have valid references.
		TSharedRef<FFrameAdapterProcessI420CPU> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessI420CPU>(AsShared());
		RHICmdList.EnqueueLambda([ThisRHIRef](FRHICommandListImmediate&) {
			ThisRHIRef->OnRHIStageComplete();
		});
	}

	void FFrameAdapterProcessI420CPU::OnRHIStageComplete()
	{
		CurrentOuputBuffer->Metadata.AdaptProcessFinalizeTime = FPlatformTime::Cycles64();

		rtc::scoped_refptr<webrtc::I420Buffer> I420Buffer = CurrentOuputBuffer->GetI420Buffer();
		libyuv::ARGBToI420(
			static_cast<uint8*>(ResultsBuffer),
			MappedStride * 4,
			I420Buffer->MutableDataY(),
			I420Buffer->StrideY(),
			I420Buffer->MutableDataU(),
			I420Buffer->StrideU(),
			I420Buffer->MutableDataV(),
			I420Buffer->StrideV(),
			I420Buffer->width(),
			I420Buffer->height());

		CurrentOuputBuffer->Metadata.AdaptProcessEndTime = FPlatformTime::Cycles64();
		CurrentOuputBuffer = nullptr;

		EndProcess();
	}

	void FFrameAdapterProcessI420CPU::CleanUp()
	{
		GDynamicRHI->RHIUnmapStagingSurface(ReadbackTexture);
		ResultsBuffer = nullptr;
	}
} // namespace UE::PixelStreaming
