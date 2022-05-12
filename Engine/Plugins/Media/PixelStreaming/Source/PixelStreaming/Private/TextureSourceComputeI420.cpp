// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureSourceComputeI420.h"
#include "RGBToYUVShader.h"
#include "Utils.h"

namespace
{
	struct FComputeToI420Texture
	{
		FComputeToI420Texture(const FIntPoint& InPlaneYDimensions, const FIntPoint& InPlaneUVDimensions);
		~FComputeToI420Texture();

		// dimensions of the texures
		FIntPoint PlaneYDimensions;
		FIntPoint PlaneUVDimensions;

		// used as targets for the compute shader
		FTextureRHIRef TextureY;
		FTextureRHIRef TextureU;
		FTextureRHIRef TextureV;

		// the UAVs of the targets
		FUnorderedAccessViewRHIRef TextureYUAV;
		FUnorderedAccessViewRHIRef TextureUUAV;
		FUnorderedAccessViewRHIRef TextureVUAV;

		// cpu readable copies of the targets above
		FTextureRHIRef StagingTextureY;
		FTextureRHIRef StagingTextureU;
		FTextureRHIRef StagingTextureV;

		// memory mapped pointers of the staging textures
		void* MappedY = nullptr;
		void* MappedU = nullptr;
		void* MappedV = nullptr;

		int32 YStride = 0;
		int32 UStride = 0;
		int32 VStride = 0;

		// the final webrtc buffer
		rtc::scoped_refptr<webrtc::I420Buffer> Buffer;
	};

	class FI420FrameCapturer : public IPixelStreamingFrameCapturer
	{
	public:
		FGPUFenceRHIRef Fence;
		FTextureRHIRef TempFBCopy;

	public:
		virtual void CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture) override;
		virtual bool IsCaptureFinished() override;
		virtual void OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture) override;
	};

	FComputeToI420Texture::FComputeToI420Texture(const FIntPoint& InPlaneYDimensions, const FIntPoint& InPlaneUVDimensions)
		: PlaneYDimensions(InPlaneYDimensions)
		, PlaneUVDimensions(InPlaneUVDimensions)
	{
		Buffer = webrtc::I420Buffer::Create(PlaneYDimensions.X, PlaneYDimensions.Y);

		const FRHITextureCreateDesc TextureDescY =
			FRHITextureCreateDesc::Create2D(TEXT("Compute YUV Target"), PlaneYDimensions, PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::UAV)
			.SetInitialState(ERHIAccess::UAVCompute);

		const FRHITextureCreateDesc TextureDescUV =
			FRHITextureCreateDesc::Create2D(TEXT("Compute YUV Target"), PlaneUVDimensions, PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::UAV)
			.SetInitialState(ERHIAccess::UAVCompute);

		TextureY = GDynamicRHI->RHICreateTexture(TextureDescY);
		TextureU = GDynamicRHI->RHICreateTexture(TextureDescUV);
		TextureV = GDynamicRHI->RHICreateTexture(TextureDescUV);

		const FRHITextureCreateDesc StagingDescY =
			FRHITextureCreateDesc::Create2D(TEXT("YUV Output CPU Texture"), PlaneYDimensions, PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::CPUReadback);

		const FRHITextureCreateDesc StagingDescUV =
			FRHITextureCreateDesc::Create2D(TEXT("YUV Output CPU Texture"), PlaneUVDimensions, PF_R8)
			.SetClearValue(FClearValueBinding::None)
			.SetFlags(ETextureCreateFlags::CPUReadback);

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
	}

	FComputeToI420Texture::~FComputeToI420Texture()
	{
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureY);
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureU);
		GDynamicRHI->RHIUnmapStagingSurface(StagingTextureV);
	}

	void FI420FrameCapturer::CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture)
	{
		// use a compute shader to extract the yuv planes of the supplied texture
		// TODO (Aidan) move this to RDG
		
		FTextureRHIRef SourceTexture = TextureToCopy.GetTexture<FTextureRHIRef>();
		TSharedRef<FComputeToI420Texture> I420ReadbackTexture = DestinationTexture->GetTexture<TSharedRef<FComputeToI420Texture>>();
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// Todo (Luke): Add support for changing capture resolutions at runtime using frame scale etc.
		verifyf(SourceTexture->GetDesc().Extent.X == I420ReadbackTexture->PlaneYDimensions.X && SourceTexture->GetDesc().Extent.Y == I420ReadbackTexture->PlaneYDimensions.Y, TEXT("Does not support resolution changes at runtime yet."));

		if (!TempFBCopy)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create2D(TEXT("PixelStreamingBlankTexture"), SourceTexture->GetDesc().Extent, SourceTexture->GetDesc().Format)
				.SetClearValue(FClearValueBinding::None)
				.SetFlags(ETextureCreateFlags::ShaderResource);

			TempFBCopy = GDynamicRHI->RHICreateTexture(Desc);
		}
		RHICmdList.Transition({
			FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc),
			FRHITransitionInfo(TempFBCopy, ERHIAccess::Unknown, ERHIAccess::CopyDest)
		});
		RHICmdList.CopyTexture(SourceTexture, TempFBCopy, {});
		RHICmdList.Transition(FRHITransitionInfo(TempFBCopy, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		
		FRGBToYUVShaderParameters ShaderParameters;
		ShaderParameters.SourceTexture = TempFBCopy;
		ShaderParameters.DestPlaneYDimensions = I420ReadbackTexture->PlaneYDimensions;
		ShaderParameters.DestPlaneUVDimensions = I420ReadbackTexture->PlaneUVDimensions;
		ShaderParameters.DestPlaneY = I420ReadbackTexture->TextureYUAV;
		ShaderParameters.DestPlaneU = I420ReadbackTexture->TextureUUAV;
		ShaderParameters.DestPlaneV = I420ReadbackTexture->TextureVUAV;
		FRGBToYUVShader::Dispatch(RHICmdList, ShaderParameters);

		RHICmdList.CopyTexture(I420ReadbackTexture->TextureY, I420ReadbackTexture->StagingTextureY, {});
		RHICmdList.CopyTexture(I420ReadbackTexture->TextureU, I420ReadbackTexture->StagingTextureU, {});
		RHICmdList.CopyTexture(I420ReadbackTexture->TextureV, I420ReadbackTexture->StagingTextureV, {});

		RHICmdList.WriteGPUFence(Fence);
	}

	bool FI420FrameCapturer::IsCaptureFinished()
	{
		return Fence->Poll();
	}

	void FI420FrameCapturer::OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture)
	{
		// once the compute shader is complete we can copy the data from the pre mapped destination textures

		TSharedRef<FComputeToI420Texture> I420ReadbackTexture = CapturedTexture->GetTexture<TSharedRef<FComputeToI420Texture>>();
		UE::PixelStreaming::MemCpyStride(I420ReadbackTexture->Buffer->MutableDataY(), I420ReadbackTexture->MappedY, I420ReadbackTexture->Buffer->StrideY(), I420ReadbackTexture->YStride, I420ReadbackTexture->PlaneYDimensions.Y);
		UE::PixelStreaming::MemCpyStride(I420ReadbackTexture->Buffer->MutableDataU(), I420ReadbackTexture->MappedU, I420ReadbackTexture->Buffer->StrideU(), I420ReadbackTexture->UStride, I420ReadbackTexture->PlaneUVDimensions.Y);
		UE::PixelStreaming::MemCpyStride(I420ReadbackTexture->Buffer->MutableDataV(), I420ReadbackTexture->MappedV, I420ReadbackTexture->Buffer->StrideV(), I420ReadbackTexture->VStride, I420ReadbackTexture->PlaneUVDimensions.Y);
		Fence->Clear();
	}
} // namespace

namespace UE::PixelStreaming
{
	TSharedPtr<FPixelStreamingTextureWrapper> FTextureSourceComputeI420::CreateBlankStagingTexture(uint32 Width, uint32 Height)
	{
		const int32 W = static_cast<int32>(Width);
		const int32 H = static_cast<int32>(Height);
		TSharedRef<FComputeToI420Texture> Texture = MakeShared<FComputeToI420Texture>(FIntPoint(W, H), FIntPoint(W / 2, H / 2));
		return MakeShared<FPixelStreamingTextureWrapper>(Texture);
	}

	TSharedPtr<IPixelStreamingFrameCapturer> FTextureSourceComputeI420::CreateFrameCapturer()
	{
		TSharedPtr<FI420FrameCapturer> Capturer = MakeShared<FI420FrameCapturer>();
		Capturer->Fence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFence"));
		return Capturer;
	}

	rtc::scoped_refptr<webrtc::I420Buffer> FTextureSourceComputeI420::ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture)
	{
		TSharedRef<FComputeToI420Texture> I420ReadbackTexture = Texture->GetTexture<TSharedRef<FComputeToI420Texture>>();
		return I420ReadbackTexture->Buffer;
	}

} // namespace UE::PixelStreaming
