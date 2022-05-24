// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingMediaTexture.h"
#include "PixelStreamingMediaTextureResource.h"
#include "Async/Async.h"
#include "WebRTCIncludes.h"

constexpr int32 DEFAULT_WIDTH = 1920;
constexpr int32 DEFAULT_HEIGHT = 1080;

// This is copy-pasted from Engine\Source\Runtime\RHI\Private\RHIUtilities.cpp, TODO: RHICreateTargetableShaderResource family of functions is deprecated,
// suggesting RHICreateTexture be used instead, so translate to that usage of the API at some point
void RHICreateTargetableShaderResource(
	const FRHITextureCreateDesc& BaseDesc,
	ETextureCreateFlags TargetableTextureFlags,
	bool bForceSeparateTargetAndShaderResource,
	bool bForceSharedTargetAndShaderResource,
	FTextureRHIRef& OutTargetableTexture,
	FTextureRHIRef& OutShaderResourceTexture
	)
{
	// Ensure none of the usage flags are passed in.
	check(!EnumHasAnyFlags(BaseDesc.Flags, ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ResolveTargetable));

	// Ensure we aren't forcing separate and shared textures at the same time.
	check(!(bForceSeparateTargetAndShaderResource && bForceSharedTargetAndShaderResource));

	// Ensure that the targetable texture is either render or depth-stencil targetable.
	check(EnumHasAnyFlags(TargetableTextureFlags, ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::DepthStencilTargetable | ETextureCreateFlags::UAV));

	if (BaseDesc.NumSamples > 1 && !bForceSharedTargetAndShaderResource)
	{
		bForceSeparateTargetAndShaderResource = RHISupportsSeparateMSAAAndResolveTextures(GMaxRHIShaderPlatform);
	}

	if (!bForceSeparateTargetAndShaderResource)
	{
		FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc(BaseDesc)
			.AddFlags(TargetableTextureFlags | ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask);

		// Create a single texture that has both TargetableTextureFlags and ETextureCreateFlags::ShaderResource set.
		OutTargetableTexture = OutShaderResourceTexture = RHICreateTexture(Desc);
	}
	else
	{
		ETextureCreateFlags ResolveTargetableTextureFlags = ETextureCreateFlags::ResolveTargetable;
		if (EnumHasAnyFlags(TargetableTextureFlags, ETextureCreateFlags::DepthStencilTargetable))
		{
			ResolveTargetableTextureFlags |= ETextureCreateFlags::DepthStencilResolveTarget;
		}

		FRHITextureCreateDesc TargetableDesc =
			FRHITextureCreateDesc(BaseDesc)
			.AddFlags(TargetableTextureFlags)
			.SetInitialState(ERHIAccess::SRVMask);

		FRHITextureCreateDesc ResourceDesc =
			FRHITextureCreateDesc(BaseDesc)
			.AddFlags(ResolveTargetableTextureFlags | ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask)
			.SetNumSamples(1);

		// Create a texture that has TargetableTextureFlags set, and a second texture that has ETextureCreateFlags::ResolveTargetable and ETextureCreateFlags::ShaderResource set.
		OutTargetableTexture = RHICreateTexture(TargetableDesc);
		OutShaderResourceTexture = RHICreateTexture(ResourceDesc);
	}
}

UPixelStreamingMediaTexture::UPixelStreamingMediaTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetResource(nullptr);
}

void UPixelStreamingMediaTexture::BeginDestroy()
{
	AsyncTask(ENamedThreads::ActualRenderingThread, [&RenderSyncContext = RenderSyncContext, &RenderTarget = RenderTarget]() {
		FScopeLock Lock(&RenderSyncContext);
		RenderTarget = nullptr;
	});

	Super::BeginDestroy();
}

float UPixelStreamingMediaTexture::GetSurfaceWidth() const
{
	return CurrentResource != nullptr ? CurrentResource->GetSizeX() : 0.0f;
}

float UPixelStreamingMediaTexture::GetSurfaceHeight() const
{
	return CurrentResource != nullptr ? CurrentResource->GetSizeY() : 0.0f;
}

void UPixelStreamingMediaTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (CurrentResource != nullptr)
	{
		CumulativeResourceSize.AddUnknownMemoryBytes(CurrentResource->GetResourceSize());
	}
}

EMaterialValueType UPixelStreamingMediaTexture::GetMaterialType() const
{
	return MCT_Texture2D;
}

void UPixelStreamingMediaTexture::OnFrame(const webrtc::VideoFrame& frame)
{
	// TODO
	// Currently this will cause all input frames to be converted to I420 (using webrtc::ConvertFromI420)
	// and then render the rgba buffer to a texture, but in some cases we might already have the native
	// texture and can use it or copy it directly. At some point it would be useful to detect and fork this
	// behaviour.
	// Additionally, we're rendering to a texture then updating the texture resource with every frame. We
	// might be able to just map a single dest texture and update it each frame.

	constexpr auto TEXTURE_PIXEL_FORMAT = PF_B8G8R8A8;
	constexpr auto WEBRTC_PIXEL_FORMAT = webrtc::VideoType::kARGB;

	AsyncTask(ENamedThreads::ActualRenderingThread, [=]() {
		FScopeLock Lock(&RenderSyncContext);

		uint32_t Size = webrtc::CalcBufferSize(WEBRTC_PIXEL_FORMAT, frame.width(), frame.height());

		if (Size > BufferSize)
		{
			delete[] Buffer;
			Buffer = new uint8_t[Size];
			BufferSize = Size;
		}

		webrtc::ConvertFromI420(frame, WEBRTC_PIXEL_FORMAT, 0, Buffer);

		FIntPoint FrameSize = FIntPoint(frame.width(), frame.height());
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		if (!RenderTargetDescriptor.IsValid() || RenderTargetDescriptor.GetSize() != FIntVector(FrameSize.X, FrameSize.Y, 0))
		{
			// Create the RenderTarget descriptor
			RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(FrameSize,
				TEXTURE_PIXEL_FORMAT,
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_RenderTargetable,
				false);

			// Update the shader resource for the 'SourceTexture'
			FRHITextureCreateDesc RenderTargetTextureDesc =
				FRHITextureCreateDesc::Create2D(TEXT(""), FrameSize.X, FrameSize.Y, TEXTURE_PIXEL_FORMAT)
				.SetClearValue(FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f)))
				.SetFlags(ETextureCreateFlags::Dynamic)
				.DetermineInititialState();

			FTextureRHIRef DummyTexture2DRHI;
			RHICreateTargetableShaderResource(RenderTargetTextureDesc, TexCreate_RenderTargetable, false, false, SourceTexture, DummyTexture2DRHI);

			// Find a free target-able texture from the render pool
			GRenderTargetPool.FindFreeElement(RHICmdList,
				RenderTargetDescriptor,
				RenderTarget,
				TEXT("PIXELSTEAMINGPLAYER"));
		}

		// Create the update region structure
		FUpdateTextureRegion2D Region(0, 0, 0, 0, FrameSize.X, FrameSize.Y);

		// Set the Pixel data of the webrtc Frame to the SourceTexture
		RHIUpdateTexture2D(SourceTexture, 0, Region, frame.width() * 4, (uint8*&)Buffer);

		UpdateTextureReference(RHICmdList, SourceTexture);
	});
}

void UPixelStreamingMediaTexture::UpdateTextureReference(FRHICommandList& RHICmdList, FTexture2DRHIRef Reference)
{
	if (CurrentResource != nullptr)
	{
		if (Reference.IsValid() && CurrentResource->TextureRHI != Reference)
		{
			CurrentResource->TextureRHI = Reference;
			RHIUpdateTextureReference(TextureReference.TextureReferenceRHI, CurrentResource->TextureRHI);
		}
		else if (!Reference.IsValid())
		{
			if (CurrentResource != nullptr)
			{
				InitializeResources();

				// Make sure RenderThread is executed before continuing
				FlushRenderingCommands();
			}
		}
	}
}

FTextureResource* UPixelStreamingMediaTexture::CreateResource()
{
	if (CurrentResource != nullptr)
	{
		SetResource(nullptr);
		delete CurrentResource;
	}

	CurrentResource = new FPixelStreamingMediaTextureResource(this);
	InitializeResources();

	return CurrentResource;
}

void UPixelStreamingMediaTexture::InitializeResources()
{
	// Set the default video texture to reference nothing
	FTextureRHIRef ShaderTexture2D;
	FTextureRHIRef RenderableTexture;

	FRHITextureCreateDesc RenderTargetTextureDesc =
		FRHITextureCreateDesc::Create2D(TEXT(""), DEFAULT_WIDTH, DEFAULT_HEIGHT, EPixelFormat::PF_B8G8R8A8)
		.SetClearValue(FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f)))
		.SetFlags(ETextureCreateFlags::Dynamic)
		.DetermineInititialState();

	RHICreateTargetableShaderResource(RenderTargetTextureDesc, TexCreate_RenderTargetable, false, false, RenderableTexture, ShaderTexture2D);

	CurrentResource->TextureRHI = RenderableTexture;

	ENQUEUE_RENDER_COMMAND(FPixelStreamingMediaTextureUpdateTextureReference)
	([this](FRHICommandListImmediate& RHICmdList) {
		RHIUpdateTextureReference(TextureReference.TextureReferenceRHI, CurrentResource->TextureRHI);
	});
}
