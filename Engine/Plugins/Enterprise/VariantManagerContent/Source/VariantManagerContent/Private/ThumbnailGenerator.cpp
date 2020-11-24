// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThumbnailGenerator.h"

#include "VariantManagerContentLog.h"

#include "CanvasTypes.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineModule.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "LegacyScreenPercentageDriver.h"
#include "Rendering/Texture2DResource.h"
#include "RenderUtils.h"

#if WITH_EDITOR
#include "ObjectTools.h"
#include "LevelEditor.h"
#endif

namespace ThumbnailGeneratorImpl
{
    void RenderSceneToTexture(
		FSceneInterface* Scene,
		const FVector& ViewOrigin,
		const FMatrix& ViewRotationMatrix,
		const FMatrix& ProjectionMatrix,
		FIntPoint TargetSize,
		float TargetGamma,
		TArray<FColor>& OutSamples)
	{
		UTextureRenderTarget2D* RenderTargetTexture = NewObject<UTextureRenderTarget2D>();
		RenderTargetTexture->AddToRoot();
		RenderTargetTexture->ClearColor = FLinearColor::Transparent;
		RenderTargetTexture->TargetGamma = TargetGamma;
		RenderTargetTexture->InitCustomFormat(TargetSize.X, TargetSize.Y, PF_B8G8R8A8, false);

		FTextureRenderTargetResource* RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource();

		FSceneViewFamilyContext ViewFamily(
			FSceneViewFamily::ConstructionValues(RenderTargetResource, Scene, FEngineShowFlags(ESFIM_Game))
			.SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime)
		);

		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
			ViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false));

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, TargetSize.X, TargetSize.Y));
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewOrigin = ViewOrigin;
		ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;

		FSceneView* NewView = new FSceneView(ViewInitOptions);
		ViewFamily.Views.Add(NewView);

		FCanvas Canvas(RenderTargetResource, NULL, FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime, Scene->GetFeatureLevel());
		Canvas.Clear(FLinearColor::Transparent);
		GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

		// Copy the contents of the remote texture to system memory
		OutSamples.SetNumUninitialized(TargetSize.X * TargetSize.Y);
		RenderTargetResource->ReadPixelsPtr(OutSamples.GetData(), FReadSurfaceDataFlags(), FIntRect(0, 0, TargetSize.X, TargetSize.Y));
		FlushRenderingCommands();

		RenderTargetTexture->RemoveFromRoot();
		RenderTargetTexture = nullptr;
	}

	bool IsPixelFormatResizeable( EPixelFormat PixelFormat )
	{
		return
			PixelFormat == PF_A8R8G8B8 ||
			PixelFormat == PF_R8G8B8A8 ||
			PixelFormat == PF_B8G8R8A8 ||
			PixelFormat == PF_R8G8B8A8_SNORM ||
			PixelFormat == PF_R8G8B8A8_UINT;
	}

	// This function works like ImageUtils::CreateTexture, except that it should also work at runtime
	// Note that it's entirely possible that Bytes is a compressed (e.g. DXT1) byte buffer, although we won't be able to copy source data in that case
    UTexture2D* CreateTextureFromBulkData(uint32 Width, uint32 Height, void* Bytes, uint64 NumBytes, EPixelFormat PixelFormat, bool bSetSourceData = false)
    {
        UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PixelFormat);
        if (Texture)
        {
#if WITH_EDITOR
			if ( bSetSourceData && IsPixelFormatResizeable(PixelFormat) )
			{
				// Set via Source or else it won't be saved to disk. This is adapted from FImageUtils::CreateTexture2D,
				// so we don't have to match it's signature with a TArray<FColor>
				Texture->Source.Init( Width, Height, /*NumSlices=*/ 1, /*NumMips=*/ 1, TSF_BGRA8 );

				uint8* MipData = Texture->Source.LockMip( 0 );
				for ( uint32 Y = 0; Y < Height; Y++ )
				{
					uint64 Index = ( Height - 1 - Y ) * Width * sizeof( FColor );
					uint8* DestPtr = &MipData[ Index ];

					FMemory::Memcpy( DestPtr, reinterpret_cast< uint8* >( Bytes ) + Index, Width * sizeof( FColor ) );
				}
				Texture->Source.UnlockMip( 0 );

				Texture->SRGB = true;
				Texture->CompressionSettings = TC_EditorIcon;
				Texture->MipGenSettings = TMGS_FromTextureGroup;
				Texture->DeferCompression = true;
				Texture->PostEditChange();
			}
			else
#endif // WITH_EDITOR
			{
				void* Data = Texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
				{
					FMemory::Memcpy(Data, Bytes, NumBytes);
				}
				Texture->PlatformData->Mips[0].BulkData.Unlock();

				Texture->PlatformData->SetNumSlices(1);
				Texture->UpdateResource();
			}
		}

        return Texture;
    }
}

UTexture2D* ThumbnailGenerator::GenerateThumbnailFromTexture(UTexture2D* Texture)
{
	if ( !Texture )
	{
		return nullptr;
	}

    // Force all mips to stream in, as we may need to use mip 0 for the thumbnail
	Texture->SetForceMipLevelsToBeResident(30.0f);
	Texture->WaitForStreaming();

	// Prepare command data: We'll pull the data directly from the GPU resource, as the format in
	// BulkData could be anything
	struct FCommandData
	{
		int32 SourceWidth;
		int32 SourceHeight;
		UTexture2D* Texture;
		TPromise<void> Promise;

		TArray<uint8> PackedSourceBytes;
		EPixelFormat PackedPixelFormat = EPixelFormat::PF_Unknown;
		bool bCanResize;
	};
	using FCommandDataPtr = TSharedPtr<FCommandData, ESPMode::ThreadSafe>;
	FCommandDataPtr CommandData = MakeShared<FCommandData, ESPMode::ThreadSafe>();
	CommandData->Texture = Texture;
	CommandData->SourceWidth = Texture->GetSizeX();
	CommandData->SourceHeight = Texture->GetSizeY();

	TFuture<void> CompletionFuture = CommandData->Promise.GetFuture();

	int32 TargetWidth = FMath::Min(CommandData->SourceWidth, VARIANT_MANAGER_THUMBNAIL_SIZE);
	int32 TargetHeight = FMath::Min(CommandData->SourceHeight, VARIANT_MANAGER_THUMBNAIL_SIZE);

	ENQUEUE_RENDER_COMMAND(RetrieveTextureDataForThumbnail)(
		[CommandData, TargetWidth, TargetHeight](FRHICommandListImmediate& RHICmdList)
		{
			if (!CommandData->Texture->Resource)
			{
				CommandData->Promise.SetValue();
				return;
			}

			FTexture2DRHIRef Texture2DRHI = CommandData->Texture->Resource->TextureRHI->GetTexture2D();

			CommandData->PackedPixelFormat = Texture2DRHI->GetFormat();
			CommandData->bCanResize = ThumbnailGeneratorImpl::IsPixelFormatResizeable( CommandData->PackedPixelFormat );

			// We can only resize FColor-like formats, otherwise we'll just copy the full data.
			// Let's at least choose the smallest MIP we can reasonably take
			// We start at CurrentFirstMip instead of 0 because Texture2DRHI will always match the CurrentFirstMip, so
			// even if the texture is 256x256, we may be dealing with CurrentFirstMip 2, and so a 64x64 resource.
			// This shouldn't happen if we wait for streaming (that we do), but just to be safe
			int32 TargetMipIndex = ((FTexture2DResource*)CommandData->Texture->Resource)->GetCurrentFirstMip();
			if (!CommandData->bCanResize)
			{
				for(int32 MipIndex = 0; MipIndex < CommandData->Texture->PlatformData->Mips.Num(); ++MipIndex)
				{
					const FTexture2DMipMap& Mip = CommandData->Texture->PlatformData->Mips[MipIndex];
					if (Mip.SizeX < TargetWidth || Mip.SizeY < TargetHeight)
					{
						break;
					}

					TargetMipIndex = MipIndex;
					CommandData->SourceWidth = Mip.SizeX;
					CommandData->SourceHeight = Mip.SizeY;
				}
			}

			uint32 BlockBytes = GPixelFormats[CommandData->PackedPixelFormat].BlockBytes;
			uint32 BlockSizeX = GPixelFormats[CommandData->PackedPixelFormat].BlockSizeX;
			uint32 BlockSizeY = GPixelFormats[CommandData->PackedPixelFormat].BlockSizeY;

			uint32 NumBlocksX = CommandData->SourceWidth / BlockSizeX;
			uint32 NumBlocksY = CommandData->SourceHeight / BlockSizeY;

			uint32 DestStride = NumBlocksX * BlockBytes;
			uint32 SourceStride = 0;
			const uint8* SourceBytes = reinterpret_cast<const uint8*>(
				RHILockTexture2D(
					Texture2DRHI, TargetMipIndex, EResourceLockMode::RLM_ReadOnly, SourceStride, false
			));

			// Pack texture data if needed
			CommandData->PackedSourceBytes.SetNumUninitialized(DestStride * NumBlocksY);
			if (SourceStride == DestStride)
			{
				FMemory::Memcpy(CommandData->PackedSourceBytes.GetData(), SourceBytes, CommandData->PackedSourceBytes.Num());
			}
			else
			{
				uint8* DestBytes = CommandData->PackedSourceBytes.GetData();

				for (uint32 Row = 0; Row < NumBlocksY; ++Row)
				{
					FMemory::Memcpy(DestBytes, SourceBytes, DestStride);
					SourceBytes += SourceStride;
					DestBytes += DestStride;
				}
			}

			RHIUnlockTexture2D(Texture2DRHI, TargetMipIndex, false);
			CommandData->Promise.SetValue();
		}
	);

	CompletionFuture.Get();

	if (CommandData->PackedPixelFormat == EPixelFormat::PF_Unknown)
	{
		UE_LOG(LogVariantContent, Error, TEXT("Failed create a thumbnail from texture '%s'"), *Texture->GetName());
		return nullptr;
	}

	UTexture2D* Thumbnail = nullptr;
	TArray<uint8> DestBytes;
	EPixelFormat SourceDataPixelFormat = CommandData->Texture->GetPixelFormat();

	// Resize image if we need to (and can)
	if ((TargetWidth != CommandData->SourceWidth || TargetHeight != CommandData->SourceHeight) && CommandData->bCanResize)
	{
		DestBytes.SetNumUninitialized(TargetWidth * TargetHeight * sizeof(FColor));

		TArrayView<const FColor> SourceColors = TArrayView<const FColor>(reinterpret_cast<const FColor*>(CommandData->PackedSourceBytes.GetData()), CommandData->SourceWidth* CommandData->SourceHeight);
		TArrayView<FColor> DestColors = TArrayView<FColor>(reinterpret_cast<FColor*>(DestBytes.GetData()), TargetWidth * TargetHeight);

		FImageUtils::ImageResize(CommandData->SourceWidth, CommandData->SourceHeight, SourceColors, TargetWidth, TargetHeight, DestColors, CommandData->Texture->SRGB);

		const bool bSetSourceData = true;
		Thumbnail = ThumbnailGeneratorImpl::CreateTextureFromBulkData(
			TargetWidth,
			TargetHeight,
			DestBytes.GetData(),
			DestBytes.Num(),
			SourceDataPixelFormat,
			bSetSourceData
		);
	}
	else
	{
		int32 NumBytes = CommandData->PackedSourceBytes.Num();

		// Let the user know if the thumbnail ends up significantly larger than expected
		if (NumBytes > 5 * VARIANT_MANAGER_THUMBNAIL_SIZE * VARIANT_MANAGER_THUMBNAIL_SIZE * sizeof(FColor))
		{
			UE_LOG(LogVariantContent, Warning, TEXT("Thumbnail created from texture '%s' will store a thumbnail that is %d by %d in size (%d KB), because it failed to resize the received thumbnail effectively. Better results could be achieved with a texture that has more Mips, or an uncompressed pixel format."), *Texture->GetName(), TargetWidth, TargetHeight, NumBytes / 1000);
		}

#if WITH_EDITOR
		// DuplicateObject will copy the texture Source data when in the editor, which is important to have it persist when saved
		Thumbnail = DuplicateObject<UTexture2D>( Texture, GetTransientPackage() );
#else
		// At runtime the mip data won't be copied with DuplicateObject, so we copy it manually.
		// There is no 'Source' at runtime though, so we just copy the mip data directly
		const bool bSetSourceData = false;
		Thumbnail = ThumbnailGeneratorImpl::CreateTextureFromBulkData(
			TargetWidth,
			TargetHeight,
			CommandData->PackedSourceBytes.GetData(),
			NumBytes,
			SourceDataPixelFormat,
			bSetSourceData
		);
#endif // WITH_EDITOR
	}

    if (Thumbnail == nullptr)
	{
		UE_LOG(LogVariantContent, Warning, TEXT("Failed to generate thumbnail from texture '%s'"), *Texture->GetName());
	}

    return Thumbnail;
}

UTexture2D* ThumbnailGenerator::GenerateThumbnailFromFile(FString FilePath)
{
    if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath))
	{
		if (UTexture2D* OriginalTexture = FImageUtils::ImportFileAsTexture2D(FilePath))
		{
			return GenerateThumbnailFromTexture(OriginalTexture);
		}
	}

	return nullptr;
}

UTexture2D* ThumbnailGenerator::GenerateThumbnailFromCamera(UObject* WorldContextObject, const FTransform& CameraTransform, float FOVDegrees, float MinZ, float Gamma)
{
    if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FSceneInterface* Scene = World->Scene;

	TArray<FColor> CapturedImage;
	CapturedImage.SetNumUninitialized(VARIANT_MANAGER_THUMBNAIL_SIZE * VARIANT_MANAGER_THUMBNAIL_SIZE);

	FIntPoint Size{VARIANT_MANAGER_THUMBNAIL_SIZE, VARIANT_MANAGER_THUMBNAIL_SIZE};

	ThumbnailGeneratorImpl::RenderSceneToTexture(
		Scene,
		CameraTransform.GetTranslation(),
		FInverseRotationMatrix(CameraTransform.Rotator()) * FInverseRotationMatrix(FRotator(0, -90, 90)),
		FReversedZPerspectiveMatrix(FOVDegrees * 2, 1, 1, MinZ),
		Size,
		Gamma,
		CapturedImage);

	const bool bSetSourceData = true;
	UTexture2D* Thumbnail = ThumbnailGeneratorImpl::CreateTextureFromBulkData(
		VARIANT_MANAGER_THUMBNAIL_SIZE,
		VARIANT_MANAGER_THUMBNAIL_SIZE,
		( void* ) CapturedImage.GetData(),
		CapturedImage.Num() * sizeof( FColor ),
		PF_B8G8R8A8,
		bSetSourceData
	);

    if (Thumbnail == nullptr)
	{
		UE_LOG(LogVariantContent, Warning, TEXT("Failed to resize texture thumbnail texture from camera!"));
	}

    return Thumbnail;
}

UTexture2D* ThumbnailGenerator::GenerateThumbnailFromEditorViewport()
{
#if WITH_EDITOR
	FViewport* Viewport = GEditor->GetActiveViewport();

	if (!GCurrentLevelEditingViewportClient || !Viewport)
	{
		return nullptr;
	}

	FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
	// Remove selection box around client during render
	GCurrentLevelEditingViewportClient = nullptr;
	Viewport->Draw();

	uint32 SrcWidth = Viewport->GetSizeXY().X;
	uint32 SrcHeight = Viewport->GetSizeXY().Y;
	TArray<FColor> OrigBitmap;
	if (!Viewport->ReadPixels(OrigBitmap) || OrigBitmap.Num() != SrcWidth * SrcHeight)
	{
		return nullptr;
	}

	// Pre-resize the image because we already it in FColor array form anyway, which should make SetThumbnailFromTexture skip most of its processing
	TArray<FColor> ScaledBitmap;
	FImageUtils::CropAndScaleImage(SrcWidth, SrcHeight, VARIANT_MANAGER_THUMBNAIL_SIZE, VARIANT_MANAGER_THUMBNAIL_SIZE, OrigBitmap, ScaledBitmap);

	// Redraw viewport to have the yellow highlight again
	GCurrentLevelEditingViewportClient = OldViewportClient;
	Viewport->Draw();

	const bool bSetSourceData = true;
	UTexture2D* Thumbnail = ThumbnailGeneratorImpl::CreateTextureFromBulkData(
		VARIANT_MANAGER_THUMBNAIL_SIZE,
		VARIANT_MANAGER_THUMBNAIL_SIZE,
		(void*)ScaledBitmap.GetData(),
		ScaledBitmap.Num() * sizeof(FColor),
		PF_B8G8R8A8,
		bSetSourceData
	);

    if (Thumbnail == nullptr)
	{
		UE_LOG(LogVariantContent, Warning, TEXT("Failed to create thumbnail texture from viewport!"));
	}

    return Thumbnail;
#endif
    return nullptr;
}

UTexture2D* ThumbnailGenerator::GenerateThumbnailFromObjectThumbnail(UObject* Object)
{
#if WITH_EDITOR
	if (!Object)
	{
		return nullptr;
	}

    // Try to convert old thumbnails to a new thumbnail
    FName ObjectName = *Object->GetFullName();
    FThumbnailMap Map;
    ThumbnailTools::ConditionallyLoadThumbnailsForObjects({ObjectName}, Map);

    FObjectThumbnail* OldThumbnail = Map.Find(ObjectName);
    if (OldThumbnail && !OldThumbnail->IsEmpty())
    {
        const TArray<uint8>& OldBytes = OldThumbnail->GetUncompressedImageData();

        TArray<FColor> OldColors;
        OldColors.SetNumUninitialized(OldBytes.Num() / sizeof(FColor));
        FMemory::Memcpy(OldColors.GetData(), OldBytes.GetData(), OldBytes.Num());

        // Resize if needed
        int32 Width = OldThumbnail->GetImageWidth();
        int32 Height = OldThumbnail->GetImageHeight();
        if (Width != VARIANT_MANAGER_THUMBNAIL_SIZE || Height != VARIANT_MANAGER_THUMBNAIL_SIZE)
        {
            TArray<FColor> ResizedColors;
            ResizedColors.SetNum(VARIANT_MANAGER_THUMBNAIL_SIZE * VARIANT_MANAGER_THUMBNAIL_SIZE);

            FImageUtils::ImageResize(Width, Height, OldColors, VARIANT_MANAGER_THUMBNAIL_SIZE, VARIANT_MANAGER_THUMBNAIL_SIZE, ResizedColors, false);

            OldColors = MoveTemp(ResizedColors);
        }

        FCreateTexture2DParameters Params;
        Params.bDeferCompression = true;

		const bool bSetSourceData = true;
		UTexture2D* Thumbnail = ThumbnailGeneratorImpl::CreateTextureFromBulkData(
			VARIANT_MANAGER_THUMBNAIL_SIZE,
			VARIANT_MANAGER_THUMBNAIL_SIZE,
			(void*)OldColors.GetData(),
			OldColors.Num() * sizeof(FColor),
			PF_B8G8R8A8,
			bSetSourceData
		);

		if (Thumbnail == nullptr)
		{
			UE_LOG(LogVariantContent, Warning, TEXT("Failed to create thumbnail texture from object '%s'!"), *Object->GetName());
		}

        if (UPackage* Package = Object->GetOutermost())
        {
            // After this our thumbnail will be empty, and we won't get in here ever again for this variant
            ThumbnailTools::CacheEmptyThumbnail(Object->GetFullName(), Package);

            // Updated the thumbnail in the package, so we need to notify that it changed
            Package->MarkPackageDirty();
        }

        return Thumbnail;
    }
#endif
    return nullptr;
}