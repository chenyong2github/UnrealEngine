// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppleARKitTextures.h"
#include "AppleARKitModule.h"
#include "ExternalTexture.h"
#include "RenderingThread.h"
#include "Containers/DynamicRHIResourceArray.h"

#if PLATFORM_MAC || PLATFORM_IOS
	#import <Metal/Metal.h>
#endif

/** Resource class to do all of the setup work on the render thread */
class FARKitCameraImageResource :
	public FTextureResource
{
public:
	FARKitCameraImageResource(UAppleARKitTextureCameraImage* InOwner)
		: LastFrameNumber(0)
		, Owner(InOwner)
	{
#if PLATFORM_MAC || PLATFORM_IOS
		CameraImage = Owner->GetCameraImage();
		if (CameraImage != nullptr)
		{
			CFRetain(CameraImage);
		}
#endif
		bSRGB = false;
	}

	virtual ~FARKitCameraImageResource()
	{
#if PLATFORM_MAC || PLATFORM_IOS
		if (ImageContext)
		{
			CFRelease(ImageContext);
			ImageContext = nullptr;
		}
#endif
	}

	virtual void InitRHI() override
	{
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

#if PLATFORM_IOS
		if (CameraImage != nullptr)
		{
			SCOPED_AUTORELEASE_POOL;

			CGColorSpaceRef ColorSpaceRef = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGBLinear);
			CIImage* Image = [[CIImage alloc] initWithCVPixelBuffer: CameraImage];

			// Textures always need to be rotated so to a sane orientation (and mirrored because of differing coord system)
			CIImage* RotatedImage = [Image imageByApplyingOrientation: GetRotationFromDeviceOrientation()];
			// Get the sizes from the rotated image
			CGRect ImageExtent = RotatedImage.extent;

			// Don't reallocate the texture if the sizes match
			if (Size.X != ImageExtent.size.width || Size.Y != ImageExtent.size.height)
			{
				Size.X = ImageExtent.size.width;
				Size.Y = ImageExtent.size.height;
				
				// Let go of the last texture
				RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
				DecodedTextureRef.SafeRelease();
				
				// Create the target texture that we'll update into
				FRHIResourceCreateInfo CreateInfo;
				DecodedTextureRef = RHICreateTexture2D(Size.X, Size.Y, PF_B8G8R8A8, 1, 1, TexCreate_Dynamic | TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
			}

			// Get the underlying metal texture so we can render to it
			id<MTLTexture> UnderlyingMetalTexture = (id<MTLTexture>)DecodedTextureRef->GetNativeResource();

			// Do the conversion on the GPU
			if (!ImageContext)
			{
				ImageContext = [CIContext context];
				CFRetain(ImageContext);
			}
			[ImageContext render: RotatedImage toMTLTexture: UnderlyingMetalTexture commandBuffer: nil bounds: ImageExtent colorSpace: ColorSpaceRef];

			// Now that the conversion is done, we can get rid of our refs
			[Image release];
			CGColorSpaceRelease(ColorSpaceRef);
			CFRelease(CameraImage);
			CameraImage = nullptr;
		}
		else
#endif
		{
			// Default to an empty 1x1 texture if we don't have a camera image
			FRHIResourceCreateInfo CreateInfo;
			Size.X = Size.Y = 1;
			DecodedTextureRef = RHICreateTexture2D(Size.X, Size.Y, PF_B8G8R8A8, 1, 1, TexCreate_ShaderResource, CreateInfo);
		}

		TextureRHI = DecodedTextureRef;
		TextureRHI->SetName(Owner->GetFName());
		RHIBindDebugLabelName(TextureRHI, *Owner->GetName());
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, TextureRHI);
	}

	virtual void ReleaseRHI() override
	{
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
#if PLATFORM_MAC || PLATFORM_IOS
		if (CameraImage != nullptr)
		{
			CFRelease(CameraImage);
		}
		CameraImage = nullptr;
#endif
		DecodedTextureRef.SafeRelease();
		FTextureResource::ReleaseRHI();
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return Size.X;
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return Size.Y;
	}

#if PLATFORM_MAC || PLATFORM_IOS
	/** Render thread update of the texture so we don't get 2 updates per frame on the render thread */
	void Init_RenderThread(CVPixelBufferRef InCameraImage)
	{
		check(IsInRenderingThread());
		check(InCameraImage != nullptr);

		if (LastFrameNumber != GFrameNumber)
		{
			LastFrameNumber = GFrameNumber;
			CameraImage = InCameraImage;
			CFRetain(CameraImage);
			InitRHI();
		}
	}
#endif

private:
#if PLATFORM_IOS
	/** @return the rotation to use to rotate the texture to the proper direction */
	int32 GetRotationFromDeviceOrientation()
	{
		// NOTE: The texture we are reading from is in device space and mirrored, because Apple hates us
		EDeviceScreenOrientation ScreenOrientation = FPlatformMisc::GetDeviceOrientation();
		switch (ScreenOrientation)
		{
			case EDeviceScreenOrientation::Portrait:
			{
				return kCGImagePropertyOrientationRightMirrored;
			}

			case EDeviceScreenOrientation::LandscapeLeft:
			{
				return kCGImagePropertyOrientationUpMirrored;
			}

			case EDeviceScreenOrientation::PortraitUpsideDown:
			{
				return kCGImagePropertyOrientationLeftMirrored;
			}

			case EDeviceScreenOrientation::LandscapeRight:
			{
				return kCGImagePropertyOrientationDownMirrored;
			}
		}
		// Don't know so don't rotate
		return kCGImagePropertyOrientationUp;
	}
#endif
	
	/** The size we get from the incoming camera image */
	FIntPoint Size;

#if PLATFORM_MAC || PLATFORM_IOS
	/** The raw camera image from ARKit which is to be converted into a RGBA image */
	CVPixelBufferRef CameraImage;
	
	/** The cached image context that's reused between frames */
	CIContext* ImageContext = nullptr;
#endif
	/** The texture that we actually render with which is populated via the GPU conversion process */
	FTexture2DRHIRef DecodedTextureRef;
	/** The last frame we were updated on */
	uint32 LastFrameNumber;

	const UAppleARKitTextureCameraImage* Owner;
};

UAppleARKitTextureCameraImage::UAppleARKitTextureCameraImage(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if PLATFORM_MAC || PLATFORM_IOS
	, CameraImage(nullptr)
    , NewCameraImage(nullptr)
#endif
{
	ExternalTextureGuid = FGuid::NewGuid();
	SRGB = false;
}

FTextureResource* UAppleARKitTextureCameraImage::CreateResource()
{
#if PLATFORM_MAC || PLATFORM_IOS
	return new FARKitCameraImageResource(this);
#endif
	return nullptr;
}

void UAppleARKitTextureCameraImage::BeginDestroy()
{
#if PLATFORM_MAC || PLATFORM_IOS
	if (CameraImage != nullptr)
	{
		CFRelease(CameraImage);
		CameraImage = nullptr;
	}
    
    {
        FScopeLock ScopeLock(&PendingImageLock);
        if (NewCameraImage != nullptr)
        {
            CFRelease(NewCameraImage);
            NewCameraImage = nullptr;
        }
    }
#endif
	Super::BeginDestroy();
}

#if PLATFORM_MAC || PLATFORM_IOS

void UAppleARKitTextureCameraImage::Init(float InTimestamp, CVPixelBufferRef InCameraImage)
{
	check(IsInGameThread());

	// Handle the case where this UObject is being reused
	if (CameraImage != nullptr)
	{
		CFRelease(CameraImage);
		CameraImage = nullptr;
	}

	if (InCameraImage != nullptr)
	{
		Timestamp = InTimestamp;
		CameraImage = InCameraImage;
		CFRetain(CameraImage);
		Size.X = CVPixelBufferGetWidth(CameraImage);
		Size.Y = CVPixelBufferGetHeight(CameraImage);
	}

	if (Resource == nullptr)
	{
		// Initial update. All others will be queued on the render thread
		UpdateResource();
	}
}

void UAppleARKitTextureCameraImage::Init_RenderThread()
{
	if (Resource != nullptr)
	{
		FARKitCameraImageResource* ARKitResource = static_cast<FARKitCameraImageResource*>(Resource);
		ENQUEUE_RENDER_COMMAND(Init_RenderThread)(
			[ARKitResource, this](FRHICommandListImmediate&)
		{
			FScopeLock ScopeLock(&PendingImageLock);
			if (NewCameraImage != nullptr)
			{
				ARKitResource->Init_RenderThread(NewCameraImage);
				CFRelease(NewCameraImage);
				NewCameraImage = nullptr;
			}
		});
	}
}

void UAppleARKitTextureCameraImage::EnqueueNewCameraImage(CVPixelBufferRef InCameraImage)
{
	FScopeLock ScopeLock(&PendingImageLock);
	if (NewCameraImage != nullptr)
	{
		CFRelease(NewCameraImage);
	}

	NewCameraImage = InCameraImage;
	CFRetain(NewCameraImage);
}
#endif

UAppleARKitTextureCameraDepth::UAppleARKitTextureCameraDepth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if PLATFORM_MAC || PLATFORM_IOS
	, CameraDepth(nullptr)
#endif
{
	ExternalTextureGuid = FGuid::NewGuid();
}

FTextureResource* UAppleARKitTextureCameraDepth::CreateResource()
{
	// @todo joeg -- hook this up for rendering
	return nullptr;
}

void UAppleARKitTextureCameraDepth::BeginDestroy()
{
#if PLATFORM_MAC || PLATFORM_IOS
	if (CameraDepth != nullptr)
	{
		CFRelease(CameraDepth);
		CameraDepth = nullptr;
	}
#endif
	Super::BeginDestroy();
}

#if SUPPORTS_ARKIT_1_0

void UAppleARKitTextureCameraDepth::Init(float InTimestamp, AVDepthData* InCameraDepth)
{
// @todo joeg -- finish this
	Timestamp = InTimestamp;
}

#endif

UAppleARKitEnvironmentCaptureProbeTexture::UAppleARKitEnvironmentCaptureProbeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if PLATFORM_MAC || PLATFORM_IOS
	, MetalTexture(nullptr)
#endif
{
	ExternalTextureGuid = FGuid::NewGuid();
	SRGB = false;
}

#if PLATFORM_MAC || PLATFORM_IOS
void UAppleARKitEnvironmentCaptureProbeTexture::Init(float InTimestamp, id<MTLTexture> InEnvironmentTexture)
{
	if (Resource == nullptr)
	{
		UpdateResource();
	}
	
	// Do nothing if the textures are the same
	// They will change as the data comes in but the textures themselves may stay the same between updates
	if (MetalTexture == InEnvironmentTexture)
	{
		return;
	}
	
	// Handle the case where this UObject is being reused
	if (MetalTexture != nullptr)
	{
		CFRelease(MetalTexture);
		MetalTexture = nullptr;
	}
	
	if (InEnvironmentTexture != nullptr)
	{
		Timestamp = InTimestamp;
		MetalTexture = InEnvironmentTexture;
		CFRetain(MetalTexture);
		Size.X = MetalTexture.width;
		Size.Y = MetalTexture.height;
	}
	// Force an update to our external texture on the render thread
	if (Resource != nullptr)
	{
		ENQUEUE_RENDER_COMMAND(UpdateEnvironmentCapture)(
			[InResource = Resource](FRHICommandListImmediate& RHICmdList)
			{
				InResource->InitRHI();
			});
	}
}

/**
 * Passes a metaltexture through to the RHI to wrap in an RHI texture without traversing system memory.
 */
class FAppleARKitMetalTextureResourceWrapper :
	public FResourceBulkDataInterface
{
public:
	FAppleARKitMetalTextureResourceWrapper(id<MTLTexture> InImageBuffer)
		: ImageBuffer(InImageBuffer)
	{
		check(ImageBuffer);
		CFRetain(ImageBuffer);
	}
	
	virtual ~FAppleARKitMetalTextureResourceWrapper()
	{
		CFRelease(ImageBuffer);
		ImageBuffer = nullptr;
	}

	/**
	 * @return ptr to the resource memory which has been preallocated
	 */
	virtual const void* GetResourceBulkData() const override
	{
		return ImageBuffer;
	}
	
	/**
	 * @return size of resource memory
	 */
	virtual uint32 GetResourceBulkDataSize() const override
	{
		return 0;
	}
	
	/**
	 * @return the type of bulk data for special handling
	 */
	virtual EBulkDataType GetResourceType() const override
	{
		return EBulkDataType::MediaTexture;
	}
	
	/**
	 * Free memory after it has been used to initialize RHI resource
	 */
	virtual void Discard() override
	{
		delete this;
	}
	
	id<MTLTexture> ImageBuffer;
};

class FARMetalResource :
	public FTextureResource
{
public:
	FARMetalResource(UAppleARKitEnvironmentCaptureProbeTexture* InOwner)
		: Owner(InOwner)
	{
		bGreyScaleFormat = false;
		bSRGB = InOwner->SRGB;
	}
	
	virtual ~FARMetalResource()
	{
		if (ImageContext)
		{
			CFRelease(ImageContext);
			ImageContext = nullptr;
		}
	}
	
	/**
	 * Called when the resource is initialized. This is only called by the rendering thread.
	 */
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;

		id<MTLTexture> MetalTexture = Owner->GetMetalTexture();
		if (MetalTexture != nullptr)
		{
			Size.X = Size.Y = Owner->Size.X;

			const uint32 CreateFlags = TexCreate_SRGB;
			EnvCubemapTextureRHIRef = RHICreateTextureCube(Size.X, PF_B8G8R8A8, 1, CreateFlags, CreateInfo);

			/**
			 * To map their texture faces into our space we need:
			 *	 +X	to +Y	Down Mirrored
			 *	 -X to -Y	Up Mirrored
			 *	 +Y to +Z	Left Mirrored
			 *	 -Y to -Z	Left Mirrored
			 *	 +Z to -X	Left Mirrored
			 *	 -Z to +X	Right Mirrored
			 */
			CopyCubeFace(MetalTexture, EnvCubemapTextureRHIRef, kCGImagePropertyOrientationDownMirrored, 0, 2);
			CopyCubeFace(MetalTexture, EnvCubemapTextureRHIRef, kCGImagePropertyOrientationUpMirrored, 1, 3);
			CopyCubeFace(MetalTexture, EnvCubemapTextureRHIRef, kCGImagePropertyOrientationLeftMirrored, 2, 4);
			CopyCubeFace(MetalTexture, EnvCubemapTextureRHIRef, kCGImagePropertyOrientationLeftMirrored, 3, 5);
			CopyCubeFace(MetalTexture, EnvCubemapTextureRHIRef, kCGImagePropertyOrientationLeftMirrored, 4, 1);
			CopyCubeFace(MetalTexture, EnvCubemapTextureRHIRef, kCGImagePropertyOrientationRightMirrored, 5, 0);
		}
		else
		{
			Size.X = Size.Y = 1;
			// Start with a 1x1 texture
			EnvCubemapTextureRHIRef = RHICreateTextureCube(Size.X, PF_B8G8R8A8, 1, 0, CreateInfo);
		}


		TextureRHI = EnvCubemapTextureRHIRef;
		TextureRHI->SetName(Owner->GetFName());
		RHIBindDebugLabelName(TextureRHI, *Owner->GetName());
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, TextureRHI);

		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
	}

	void CopyCubeFace(id<MTLTexture> MetalTexture, FTextureCubeRHIRef Cubemap, uint32 Rotation, int32 MetalCubeIndex, int32 OurCubeIndex)
	{
		// Rotate the image we need to get a view into the face as a new slice
		id<MTLTexture> CubeFaceMetalTexture = [MetalTexture newTextureViewWithPixelFormat: MTLPixelFormatBGRA8Unorm textureType: MTLTextureType2D levels: NSMakeRange(0, 1) slices: NSMakeRange(MetalCubeIndex, 1)];
		CIImage* CubefaceImage = [[CIImage alloc] initWithMTLTexture: CubeFaceMetalTexture options: nil];
		CIImage* RotatedCubefaceImage = [CubefaceImage imageByApplyingOrientation: Rotation];
		CIImage* ImageTransform = nullptr;
		if (Rotation != kCGImagePropertyOrientationUp)
		{
			ImageTransform = RotatedCubefaceImage;
		}
		else
		{
			// We don't need to rotate it so just use a copy instead
			ImageTransform = CubefaceImage;
		}

		// Make a new view into our texture and directly render to that to avoid the CPU copy
		id<MTLTexture> UnderlyingMetalTexture = (id<MTLTexture>)Cubemap->GetNativeResource();
		id<MTLTexture> OurCubeFaceMetalTexture = [UnderlyingMetalTexture newTextureViewWithPixelFormat: MTLPixelFormatBGRA8Unorm textureType: MTLTextureType2D levels: NSMakeRange(0, 1) slices: NSMakeRange(OurCubeIndex, 1)];

		if (!ImageContext)
		{
			ImageContext = [CIContext context];
			CFRetain(ImageContext);
		}
		[ImageContext render: RotatedCubefaceImage toMTLTexture: OurCubeFaceMetalTexture commandBuffer: nil bounds: CubefaceImage.extent colorSpace: CubefaceImage.colorSpace];

		[CubefaceImage release];
		[CubeFaceMetalTexture release];
		[OurCubeFaceMetalTexture release];
	}
	
	virtual void ReleaseRHI() override
	{
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
		EnvCubemapTextureRHIRef.SafeRelease();
		FTextureResource::ReleaseRHI();
		FExternalTextureRegistry::Get().UnregisterExternalTexture(Owner->ExternalTextureGuid);
	}
	
	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return Size.X;
	}
	
	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return Size.Y;
	}
	
private:
	FIntPoint Size;
	
	FTextureCubeRHIRef EnvCubemapTextureRHIRef;
	
	const UAppleARKitEnvironmentCaptureProbeTexture* Owner;
	
	CIContext* ImageContext = nullptr;
};

#endif

FTextureResource* UAppleARKitEnvironmentCaptureProbeTexture::CreateResource()
{
#if PLATFORM_MAC || PLATFORM_IOS
	return new FARMetalResource(this);
#endif
	return nullptr;
}

void UAppleARKitEnvironmentCaptureProbeTexture::BeginDestroy()
{
#if PLATFORM_MAC || PLATFORM_IOS
	if (MetalTexture != nullptr)
	{
		CFRelease(MetalTexture);
		MetalTexture = nullptr;
	}
#endif
	Super::BeginDestroy();
}

UAppleARKitOcclusionTexture::UAppleARKitOcclusionTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SRGB = false;
}

void UAppleARKitOcclusionTexture::BeginDestroy()
{
#if PLATFORM_MAC || PLATFORM_IOS
	if (MetalTexture)
	{
		CFRelease(MetalTexture);
		MetalTexture = nullptr;
	}
#endif
	
	Super::BeginDestroy();
}

#if PLATFORM_MAC || PLATFORM_IOS
class FOcclusionTextureResource : public FTextureResource
{
public:
	FOcclusionTextureResource(UAppleARKitOcclusionTexture* InOwner)
		: Owner(InOwner)
	{
		bGreyScaleFormat = false;
		bSRGB = InOwner->SRGB;
	}
	
	virtual ~FOcclusionTextureResource()
	{
		if (ImageContext)
		{
			CFRelease(ImageContext);
			ImageContext = nullptr;
		}
	}
	
	virtual void InitRHI() override
	{
		FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
		
#if PLATFORM_IOS
		id<MTLTexture> MetalTexture = Owner->GetMetalTexture();
		if (MetalTexture)
		{
			SCOPED_AUTORELEASE_POOL;
			
			CFRetain(MetalTexture);
			
			CGColorSpaceRef ColorSpaceRef = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGBLinear);
			CIImage* Image = [[CIImage alloc] initWithMTLTexture: MetalTexture options: nil];

			// Textures always need to be rotated so to a sane orientation (and mirrored because of differing coord system)
			CIImage* RotatedImage = [Image imageByApplyingOrientation: GetRotationFromDeviceOrientation()];
			
			// Get the sizes from the rotated image
			CGRect ImageExtent = RotatedImage.extent;
			
			FIntPoint DesiredSize(ImageExtent.size.width, ImageExtent.size.height);
			
			if (!TextureRHI || DesiredSize != Size)
			{
				// Let go of the last texture
				RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
				TextureRHIRef.SafeRelease();
				
				Size = DesiredSize;
				
				MTLPixelFormat MetalPixelFormat = MetalTexture.pixelFormat;
				EPixelFormat PixelFormat = EPixelFormat::PF_Unknown;
				if (MetalPixelFormat == MTLPixelFormatR8Unorm)
				{
					PixelFormat = EPixelFormat::PF_G8;
				}
				else if (MetalPixelFormat == MTLPixelFormatR16Float)
				{
					PixelFormat = EPixelFormat::PF_R16F;
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("FMetalTextureResource::InitRHI: Metal pixel format is not supported: %d"), (int32)MetalPixelFormat);
				}
				
				if (PixelFormat != EPixelFormat::PF_Unknown)
				{
					FRHIResourceCreateInfo CreateInfo;
					TextureRHIRef = RHICreateTexture2D(Size.X, Size.Y, PixelFormat, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
				}
			}
			
			if (TextureRHIRef)
			{
				// Get the underlying metal texture so we can render to it
				id<MTLTexture> UnderlyingMetalTexture = (id<MTLTexture>)TextureRHIRef->GetNativeResource();

				// Do the conversion on the GPU
				if (!ImageContext)
				{
					ImageContext = [CIContext context];
					CFRetain(ImageContext);
				}
				[ImageContext render: RotatedImage toMTLTexture: UnderlyingMetalTexture commandBuffer: nil bounds: ImageExtent colorSpace: ColorSpaceRef];
			}
			
			// Now that the conversion is done, we can get rid of our refs
			[Image release];
			CGColorSpaceRelease(ColorSpaceRef);
			CFRelease(MetalTexture);
			MetalTexture = nullptr;
		}
#endif
		
		if (!TextureRHIRef)
		{
			// Default to an empty 1x1 texture if we don't have a camera image
			FRHIResourceCreateInfo CreateInfo;
			Size.X = Size.Y = 1;
			TextureRHIRef = RHICreateTexture2D(Size.X, Size.Y, PF_B8G8R8A8, 1, 1, TexCreate_ShaderResource, CreateInfo);
		}

		TextureRHI = TextureRHIRef;
		TextureRHI->SetName(Owner->GetFName());
		RHIBindDebugLabelName(TextureRHI, *Owner->GetName());
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, TextureRHI);
	}
	
#if PLATFORM_IOS
	/** @return the rotation to use to rotate the texture to the proper direction */
	int32 GetRotationFromDeviceOrientation()
	{
		// NOTE: The texture we are reading from is in device space and mirrored, because Apple hates us
		EDeviceScreenOrientation ScreenOrientation = FPlatformMisc::GetDeviceOrientation();
		switch (ScreenOrientation)
		{
			case EDeviceScreenOrientation::Portrait:
			{
				return kCGImagePropertyOrientationLeft;
			}

			case EDeviceScreenOrientation::LandscapeLeft:
			{
				return kCGImagePropertyOrientationDown;
			}

			case EDeviceScreenOrientation::PortraitUpsideDown:
			{
				return kCGImagePropertyOrientationUp;
			}

			case EDeviceScreenOrientation::LandscapeRight:
			{
				return kCGImagePropertyOrientationUp;
			}
		}
		
		// Don't know so don't rotate
		return kCGImagePropertyOrientationUp;
	}
#endif
	
	virtual void ReleaseRHI() override
	{
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
		TextureRHIRef.SafeRelease();
		FTextureResource::ReleaseRHI();
	}
	
	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		return Size.X;
	}
	
	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		return Size.Y;
	}
	
private:
	FIntPoint Size;
	FTexture2DRHIRef TextureRHIRef;
	const UAppleARKitOcclusionTexture* Owner = nullptr;
	CIContext* ImageContext = nullptr;
};

void UAppleARKitOcclusionTexture::SetMetalTexture(float InTimestamp, id<MTLTexture> InMetalTexture)
{
	{
		FScopeLock ScopeLock(&MetalTextureLock);
		Timestamp = InTimestamp;
		
		if (MetalTexture != InMetalTexture)
		{
			if (MetalTexture)
			{
				CFRelease(MetalTexture);
				MetalTexture = nullptr;
			}
			
			MetalTexture = InMetalTexture;
			
			if (MetalTexture)
			{
				CFRetain(MetalTexture);
				Size = FVector2D(MetalTexture.width, MetalTexture.height);
			}
		}
	}
	
	if (Resource == nullptr)
	{
		UpdateResource();
	}
	
	if (Resource)
	{
		ENQUEUE_RENDER_COMMAND(UpdateMetalTextureResource)
		([InResource = Resource](FRHICommandListImmediate& RHICmdList)
		{
			InResource->InitRHI();
		});
	}
}

id<MTLTexture> UAppleARKitOcclusionTexture::GetMetalTexture() const
{
	FScopeLock ScopeLock(&MetalTextureLock);
	return MetalTexture;
}

FTextureResource* UAppleARKitOcclusionTexture::CreateResource()
{
	return new FOcclusionTextureResource(this);
}

#else

FTextureResource* UAppleARKitOcclusionTexture::CreateResource()
{
	return nullptr;
}

#endif
