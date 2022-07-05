// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterProcessRHIToI420Compute.h"
#include "PixelStreamingInputFrameRHI.h"
#include "PixelStreamingAdaptedOutputFrameI420.h"
#include "RGBToYUVShader.h"
#include "Stats.h"
#include "Utils.h"

namespace UE::PixelStreaming
{
	FFrameAdapterProcessRHIToI420Compute::FFrameAdapterProcessRHIToI420Compute(float InScale)
		: Scale(InScale)
	{
	}

	FFrameAdapterProcessRHIToI420Compute ::~FFrameAdapterProcessRHIToI420Compute()
	{
		CleanUp();
	}

	void FFrameAdapterProcessRHIToI420Compute::Initialize(int32 InputWidth, int32 InputHeight)
	{
		const int32 Width = InputWidth * Scale;
		const int32 Height = InputHeight * Scale;

		PlaneYDimensions = { Width, Height };
		PlaneUVDimensions = { Width / 2, Height / 2 };

		FRHITextureCreateDesc TextureDescY =
			FRHITextureCreateDesc::Create2D(TEXT("Compute YUV Target"), PlaneYDimensions.X, PlaneYDimensions.Y, EPixelFormat::PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::UAV)
			.SetInitialState(ERHIAccess::UAVCompute)
			.DetermineInititialState();

		FRHITextureCreateDesc TextureDescUV =
			FRHITextureCreateDesc::Create2D(TEXT("Compute YUV Target"), PlaneUVDimensions.X, PlaneUVDimensions.Y, EPixelFormat::PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::UAV)
			.SetInitialState(ERHIAccess::UAVCompute)
			.DetermineInititialState();

		TextureY = GDynamicRHI->RHICreateTexture(TextureDescY);
		TextureU = GDynamicRHI->RHICreateTexture(TextureDescUV);
		TextureV = GDynamicRHI->RHICreateTexture(TextureDescUV);

		FRHITextureCreateDesc StagingDescY =
			FRHITextureCreateDesc::Create2D(TEXT("YUV Output CPU Texture"), PlaneYDimensions.X, PlaneYDimensions.Y, EPixelFormat::PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::CPUReadback)
			.SetInitialState(ERHIAccess::Unknown)
			.DetermineInititialState();

		FRHITextureCreateDesc StagingDescUV =
			FRHITextureCreateDesc::Create2D(TEXT("YUV Output CPU Texture"), PlaneUVDimensions.X, PlaneUVDimensions.Y, EPixelFormat::PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::CPUReadback)
			.SetInitialState(ERHIAccess::Unknown)
			.DetermineInititialState();

		StagingTextureY = GDynamicRHI->RHICreateTexture(StagingDescY);
		StagingTextureU = GDynamicRHI->RHICreateTexture(StagingDescUV);
		StagingTextureV = GDynamicRHI->RHICreateTexture(StagingDescUV);

		TextureYUAV = GDynamicRHI->RHICreateUnorderedAccessView(TextureY, 0, 0, 0);
		TextureUUAV = GDynamicRHI->RHICreateUnorderedAccessView(TextureU, 0, 0, 0);
		TextureVUAV = GDynamicRHI->RHICreateUnorderedAccessView(TextureV, 0, 0, 0);

		int32 OutWidth, OutHeight;
		GDynamicRHI->RHIMapStagingSurface(StagingTextureY, nullptr, MappedY, OutWidth, OutHeight);
		YStride = OutWidth;
		GDynamicRHI->RHIMapStagingSurface(StagingTextureU, nullptr, MappedU, OutWidth, OutHeight);
		UStride = OutWidth;
		GDynamicRHI->RHIMapStagingSurface(StagingTextureV, nullptr, MappedV, OutWidth, OutHeight);
		VStride = OutWidth;

		FPixelStreamingFrameAdapterProcess::Initialize(InputWidth, InputHeight);
	}

	TSharedPtr<IPixelStreamingAdaptedOutputFrame> FFrameAdapterProcessRHIToI420Compute::CreateOutputBuffer(int32 InputWidth, int32 InputHeight)
	{
		const int32 Width = InputWidth * Scale;
		const int32 Height = InputHeight * Scale;
		return MakeShared<FPixelStreamingAdaptedOutputFrameI420>(webrtc::I420Buffer::Create(Width, Height));
	}

	void FFrameAdapterProcessRHIToI420Compute::BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer)
	{
		checkf(InputFrame.GetType() == EPixelStreamingInputFrameType::RHI, TEXT("Incorrect source frame coming into frame adapter process."));

		const FPixelStreamingInputFrameRHI& RHISourceFrame = StaticCast<const FPixelStreamingInputFrameRHI&>(InputFrame);
		FTexture2DRHIRef SourceTexture = RHISourceFrame.FrameTexture;

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		RHICmdList.EnqueueLambda([this](FRHICommandListImmediate&) { MarkAdaptProcessStarted(); });

		FRGBToYUVShaderParameters ShaderParameters;
		ShaderParameters.SourceTexture = SourceTexture;
		ShaderParameters.DestPlaneYDimensions = PlaneYDimensions;
		ShaderParameters.DestPlaneUVDimensions = PlaneUVDimensions;
		ShaderParameters.DestPlaneY = TextureYUAV;
		ShaderParameters.DestPlaneU = TextureUUAV;
		ShaderParameters.DestPlaneV = TextureVUAV;
		FRGBToYUVShader::Dispatch(RHICmdList, ShaderParameters);

		RHICmdList.CopyTexture(TextureY, StagingTextureY, {});
		RHICmdList.CopyTexture(TextureU, StagingTextureU, {});
		RHICmdList.CopyTexture(TextureV, StagingTextureV, {});

		// by adding this shared ref to the rhi lambda we can ensure that 'this' will not be destroyed
		// until after the rhi thread is done with it, so all the commands will still have valid references.
		TSharedRef<FFrameAdapterProcessRHIToI420Compute> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessRHIToI420Compute>(AsShared());
		RHICmdList.EnqueueLambda([ThisRHIRef, OutputBuffer](FRHICommandListImmediate&) { ThisRHIRef->OnRHIStageComplete(OutputBuffer); });
	}

	void FFrameAdapterProcessRHIToI420Compute::OnRHIStageComplete(TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer)
	{
		MarkAdaptProcessFinalizing();

		FPixelStreamingAdaptedOutputFrameI420* OutputI420Buffer = StaticCast<FPixelStreamingAdaptedOutputFrameI420*>(OutputBuffer.Get());
		rtc::scoped_refptr<webrtc::I420Buffer> I420Buffer = OutputI420Buffer->GetI420Buffer();

		MemCpyStride(I420Buffer->MutableDataY(), MappedY, I420Buffer->StrideY(), YStride, PlaneYDimensions.Y);
		MemCpyStride(I420Buffer->MutableDataU(), MappedU, I420Buffer->StrideU(), UStride, PlaneUVDimensions.Y);
		MemCpyStride(I420Buffer->MutableDataV(), MappedV, I420Buffer->StrideV(), VStride, PlaneUVDimensions.Y);

		EndProcess();
	}

	void FFrameAdapterProcessRHIToI420Compute::CleanUp()
	{
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureY);
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureU);
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureV);

		MappedY = nullptr;
		MappedU = nullptr;
		MappedV = nullptr;
	}
} // namespace UE::PixelStreaming
