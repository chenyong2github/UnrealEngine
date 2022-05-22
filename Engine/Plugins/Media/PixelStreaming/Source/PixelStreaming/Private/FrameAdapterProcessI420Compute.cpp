// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterProcessI420Compute.h"
#include "FrameAdapterI420.h"
#include "RGBToYUVShader.h"
#include "Stats.h"
#include "Utils.h"
#include "PixelStreamingSourceFrame.h"

namespace UE::PixelStreaming
{
	FFrameAdapterProcessI420Compute::FFrameAdapterProcessI420Compute(float InScale)
		: Scale(InScale)
	{
	}

	FFrameAdapterProcessI420Compute ::~FFrameAdapterProcessI420Compute()
	{
		CleanUp();
	}

	void FFrameAdapterProcessI420Compute::Initialize(int32 SourceWidth, int32 SourceHeight)
	{
		const int32 Width = SourceWidth * Scale;
		const int32 Height = SourceHeight * Scale;

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

		FPixelStreamingFrameAdapterProcess::Initialize(SourceWidth, SourceHeight);
	}

	void FFrameAdapterProcessI420Compute::OnSourceResolutionChanged(int32 OldWidth, int32 OldHeight, int32 NewWidth, int32 NewHeight)
	{
		CleanUp();
	}

	TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> FFrameAdapterProcessI420Compute::CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight)
	{
		const int32 Width = SourceWidth * Scale;
		const int32 Height = SourceHeight * Scale;
		return MakeShared<FAdaptedVideoFrameLayerI420>(webrtc::I420Buffer::Create(Width, Height));
	}

	void FFrameAdapterProcessI420Compute::BeginProcess(const FPixelStreamingSourceFrame& SourceFrame)
	{
		CurrentOuputBuffer = StaticCast<FAdaptedVideoFrameLayerI420*>(GetWriteBuffer().Get());
		CurrentOuputBuffer->Metadata = SourceFrame.Metadata.Copy();
		CurrentOuputBuffer->Metadata.ProcessName = "I420 Compute";
		CurrentOuputBuffer->Metadata.AdaptCallTime = FPlatformTime::Cycles64();

		FTexture2DRHIRef SourceTexture = SourceFrame.FrameTexture;

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		RHICmdList.EnqueueLambda([CurrentOuputBuffer = CurrentOuputBuffer](FRHICommandListImmediate&) { CurrentOuputBuffer->Metadata.AdaptProcessStartTime = FPlatformTime::Cycles64(); });

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
		TSharedRef<FFrameAdapterProcessI420Compute> ThisRHIRef = StaticCastSharedRef<FFrameAdapterProcessI420Compute>(AsShared());
		RHICmdList.EnqueueLambda([ThisRHIRef](FRHICommandListImmediate&) { ThisRHIRef->OnRHIStageComplete(); });
	}

	void FFrameAdapterProcessI420Compute::OnRHIStageComplete()
	{
		CurrentOuputBuffer->Metadata.AdaptProcessFinalizeTime = FPlatformTime::Cycles64();

		rtc::scoped_refptr<webrtc::I420Buffer> I420Buffer = CurrentOuputBuffer->GetI420Buffer();

		MemCpyStride(I420Buffer->MutableDataY(), MappedY, I420Buffer->StrideY(), YStride, PlaneYDimensions.Y);
		MemCpyStride(I420Buffer->MutableDataU(), MappedU, I420Buffer->StrideU(), UStride, PlaneUVDimensions.Y);
		MemCpyStride(I420Buffer->MutableDataV(), MappedV, I420Buffer->StrideV(), VStride, PlaneUVDimensions.Y);

		CurrentOuputBuffer->Metadata.AdaptProcessEndTime = FPlatformTime::Cycles64();
		CurrentOuputBuffer = nullptr;

		EndProcess();
	}

	void FFrameAdapterProcessI420Compute::CleanUp()
	{
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureY);
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureU);
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureV);

		MappedY = nullptr;
		MappedU = nullptr;
		MappedV = nullptr;
	}
} // namespace UE::PixelStreaming
