// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterProcessRHIToI420CPU.h"
#include "PixelStreamingInputFrameRHI.h"
#include "PixelStreamingAdaptedOutputFrameI420.h"
#include "libyuv/convert.h"
#include "UtilsRender.h"

namespace UE::PixelStreaming
{
	FFrameAdapterProcessRHIToI420CPU::FFrameAdapterProcessRHIToI420CPU(float InScale)
		: Scale(InScale)
	{
	}

	FFrameAdapterProcessRHIToI420CPU::~FFrameAdapterProcessRHIToI420CPU()
	{
		CleanUp();
	}

	void FFrameAdapterProcessRHIToI420CPU::Initialize(int32 InputWidth, int32 InputHeight)
	{
		const int32 Width = InputWidth * Scale;
		const int32 Height = InputHeight * Scale;

		FRHITextureCreateDesc TextureDesc =
			FRHITextureCreateDesc::Create2D(TEXT("FFrameAdapterProcessRHIToI420CPU StagingTexture"), Width, Height, EPixelFormat::PF_B8G8R8A8)
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
			FRHITextureCreateDesc::Create2D(TEXT("FFrameAdapterProcessRHIToI420CPU ReadbackTexture"), Width, Height, EPixelFormat::PF_B8G8R8A8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::CPUReadback)
			.SetInitialState(ERHIAccess::CPURead)
			.DetermineInititialState();

		ReadbackTexture = GDynamicRHI->RHICreateTexture(ReadbackDesc);

		int32 BufferWidth = 0, BufferHeight = 0;
		GDynamicRHI->RHIMapStagingSurface(ReadbackTexture, nullptr, ResultsBuffer, BufferWidth, BufferHeight);
		MappedStride = BufferWidth;

		FPixelStreamingFrameAdapterProcess::Initialize(InputWidth, InputHeight);
	}

	TSharedPtr<IPixelStreamingAdaptedOutputFrame> FFrameAdapterProcessRHIToI420CPU::CreateOutputBuffer(int32 InputWidth, int32 InputHeight)
	{
		const int32 Width = InputWidth * Scale;
		const int32 Height = InputHeight * Scale;
		return MakeShared<FPixelStreamingAdaptedOutputFrameI420>(webrtc::I420Buffer::Create(Width, Height));
	}

	void FFrameAdapterProcessRHIToI420CPU::BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer)
	{
		checkf(InputFrame.GetType() == EPixelStreamingInputFrameType::RHI, TEXT("Incorrect source frame coming into frame adapter process."));

		const FPixelStreamingInputFrameRHI& RHISourceFrame = StaticCast<const FPixelStreamingInputFrameRHI&>(InputFrame);
		FTexture2DRHIRef SourceTexture = RHISourceFrame.FrameTexture;

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		RHICmdList.EnqueueLambda([this](FRHICommandListImmediate&) { MarkAdaptProcessStarted(); });

		RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
		RHICmdList.Transition(FRHITransitionInfo(StagingTexture, ERHIAccess::CopySrc, ERHIAccess::CopyDest));
		CopyTexture(RHICmdList, SourceTexture, StagingTexture, nullptr);

		RHICmdList.Transition(FRHITransitionInfo(StagingTexture, ERHIAccess::CopyDest, ERHIAccess::CopySrc));
		RHICmdList.Transition(FRHITransitionInfo(ReadbackTexture, ERHIAccess::CPURead, ERHIAccess::CopyDest));
		RHICmdList.CopyTexture(StagingTexture, ReadbackTexture, {});

		RHICmdList.Transition(FRHITransitionInfo(ReadbackTexture, ERHIAccess::CopyDest, ERHIAccess::CPURead));

		// by adding this shared ref to the rhi lambda we can ensure that 'this' will not be destroyed
		// until after the rhi thread is done with it, so all the commands will still have valid references.
		TSharedRef<FFrameAdapterProcessRHIToI420CPU> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessRHIToI420CPU>(AsShared());
		RHICmdList.EnqueueLambda([ThisRHIRef, OutputBuffer](FRHICommandListImmediate&) {
			ThisRHIRef->OnRHIStageComplete(OutputBuffer);
		});
	}

	void FFrameAdapterProcessRHIToI420CPU::OnRHIStageComplete(TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer)
	{
		MarkAdaptProcessFinalizing();

		FPixelStreamingAdaptedOutputFrameI420* OutputI420Buffer = StaticCast<FPixelStreamingAdaptedOutputFrameI420*>(OutputBuffer.Get());
		rtc::scoped_refptr<webrtc::I420Buffer> I420Buffer = OutputI420Buffer->GetI420Buffer();
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

		EndProcess();
	}

	void FFrameAdapterProcessRHIToI420CPU::CleanUp()
	{
		GDynamicRHI->RHIUnmapStagingSurface(ReadbackTexture);
		ResultsBuffer = nullptr;
	}
} // namespace UE::PixelStreaming
