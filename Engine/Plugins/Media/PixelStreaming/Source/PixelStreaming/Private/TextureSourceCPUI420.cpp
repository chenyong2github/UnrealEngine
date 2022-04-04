// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureSourceCPUI420.h"
#include "libyuv/convert.h"
#include "Templates/SharedPointer.h"
#include "RHI.h"
#include "RHIResources.h"
#include "Utils.h"

namespace
{
	struct FGPUToCPUReadbackTexture
	{
		FGPUToCPUReadbackTexture(FTextureRHIRef InReadbackTexture)
			: ReadbackTexture(InReadbackTexture)
		{
		}

		FTextureRHIRef ReadbackTexture;
		TArray<FColor> ReadbackResult;
	};

	class FCPUMemFrameCapturer : public IPixelStreamingFrameCapturer
	{
	public:
		TSharedPtr<FRHIGPUTextureReadback> GPUTextureReadback;

	public:
		virtual void CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture) override;
		virtual bool IsCaptureFinished() override;
		virtual void OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture) override;
	};

	void FCPUMemFrameCapturer::CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture)
	{
		FTextureRHIRef SourceTexture = TextureToCopy.GetTexture<FTextureRHIRef>();
		TSharedRef<FGPUToCPUReadbackTexture> GPUToCPUReadbackTexture = DestinationTexture->GetTexture<TSharedRef<FGPUToCPUReadbackTexture>>();
		FTextureRHIRef ReadbackTexture = GPUToCPUReadbackTexture->ReadbackTexture;
		TArray<FColor>& RawPixels = GPUToCPUReadbackTexture->ReadbackResult;
		UE::PixelStreaming::CopyTextureToReadbackTexture(SourceTexture, GPUTextureReadback, RawPixels.GetData());

	}

	bool FCPUMemFrameCapturer::IsCaptureFinished()
	{
		return GPUTextureReadback->IsReady();
	}

	void FCPUMemFrameCapturer::OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture)
	{
	}
} // namespace

namespace UE::PixelStreaming
{
	TSharedPtr<FPixelStreamingTextureWrapper> FTextureSourceCPUI420::CreateBlankStagingTexture(uint32 Width, uint32 Height)
	{

		TSharedRef<FGPUToCPUReadbackTexture> GPUToCPUTexture = MakeShared<FGPUToCPUReadbackTexture>(
			UE::PixelStreaming::CreateCPUReadbackTexture(Width, Height));

		// Allocate some CPU accessible memory for the texture we intend to read back from the GPU
		GPUToCPUTexture->ReadbackResult.Init(FColor(), Width * Height * 4);

		return MakeShared<FPixelStreamingTextureWrapper>(GPUToCPUTexture);
	}

	TSharedPtr<IPixelStreamingFrameCapturer> FTextureSourceCPUI420::CreateFrameCapturer()
	{
		TSharedPtr<FCPUMemFrameCapturer> Capturer = MakeShared<FCPUMemFrameCapturer>();
		Capturer->GPUTextureReadback = MakeShared<FRHIGPUTextureReadback>(TEXT("CopyTextureToReadbackTexture"));
		return Capturer;
	}

	rtc::scoped_refptr<webrtc::I420Buffer> FTextureSourceCPUI420::ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture)
	{
		TSharedRef<FGPUToCPUReadbackTexture> GPUToCPUReadbackTexture = Texture->GetTexture<TSharedRef<FGPUToCPUReadbackTexture>>();
		FTextureRHIRef& TextureThatWasRead = GPUToCPUReadbackTexture->ReadbackTexture;
		TArray<FColor>& RawPixels = GPUToCPUReadbackTexture->ReadbackResult;

		uint32 TextureWidth = TextureThatWasRead->GetDesc().Extent.X;
		uint32 TextureHeight = TextureThatWasRead->GetDesc().Extent.Y;

		const uint8* BGRAIn = (const uint8*)RawPixels.GetData();

		rtc::scoped_refptr<webrtc::I420Buffer> Buffer = webrtc::I420Buffer::Create(TextureWidth, TextureHeight);

		libyuv::ARGBToI420(
			BGRAIn,
			TextureWidth * 4,
			Buffer->MutableDataY(),
			Buffer->StrideY(),
			Buffer->MutableDataU(),
			Buffer->StrideU(),
			Buffer->MutableDataV(),
			Buffer->StrideV(),
			TextureWidth,
			TextureHeight);

		return Buffer;
	}
} // namespace UE::PixelStreaming
