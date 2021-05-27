// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileMediaCapture.h"

#include "FileMediaOutput.h"
#include "ImageWriteQueue.h"
#include "ImageWriteTask.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "EngineAnalytics.h"
#endif

#if WITH_EDITOR
namespace FileMediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.FileMediaCaptureStarted
	 * @Trigger Triggered when a file media capture of the viewport or render target is started.
	 * @Type Client
	 * @Owner MediaIO Team
	 */
	void SendCaptureEvent(const EImageFormat ImageFormat, int32 CompressionQuality, const FString& CaptureType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FString ImageFormatString;
			switch (ImageFormat)
			{
				case EImageFormat::PNG:
					ImageFormatString = TEXT("PNG");
					break;
				case EImageFormat::JPEG:
					ImageFormatString = TEXT("JPEG");
					break;
				case EImageFormat::GrayscaleJPEG:
					ImageFormatString = TEXT("GrayscaleJPEG");
					break;
				case EImageFormat::BMP:
					ImageFormatString = TEXT("BMP");
					break;
				case EImageFormat::ICO:
					ImageFormatString = TEXT("ICO");
					break;
				case EImageFormat::EXR:
					ImageFormatString = TEXT("EXR");
					break;
				case EImageFormat::ICNS:
					ImageFormatString = TEXT("ICNS");
					break;
				default:
					ImageFormatString = TEXT("Unknown");
					break;
			}

			TArray<FAnalyticsEventAttribute> EventAttributes;
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CaptureType"), CaptureType));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ImageFormat"), MoveTemp(ImageFormatString)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CompresionQuality"), FString::Printf(TEXT("%d"), CompressionQuality)));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.FileMediaCaptureStarted"), EventAttributes);
		}
	}
}
#endif

void UFileMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height)
{
	IImageWriteQueueModule* ImageWriteQueueModule = FModuleManager::Get().GetModulePtr<IImageWriteQueueModule>("ImageWriteQueue");
	if (ImageWriteQueueModule == nullptr)
	{
		SetState(EMediaCaptureState::Error);
		return;
	}

	TUniquePtr<FImageWriteTask> ImageTask = MakeUnique<FImageWriteTask>();
	ImageTask->Format = ImageFormat;
	ImageTask->Filename = FString::Printf(TEXT("%s%05d"), *BaseFilePathName, InBaseData.SourceFrameNumber);
	ImageTask->bOverwriteFile = bOverwriteFile;
	ImageTask->CompressionQuality = CompressionQuality;
	ImageTask->OnCompleted = OnCompleteWrapper;

	EPixelFormat PixelFormat = GetDesiredPixelFormat();
	if (PixelFormat == PF_B8G8R8A8)
	{
		TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(FIntPoint(Width, Height),
			TArray<FColor, FDefaultAllocator64>(reinterpret_cast<FColor*>(InBuffer), Width * Height));
		ImageTask->PixelData = MoveTemp(PixelData);
	}
	else if (PixelFormat == PF_FloatRGBA)
	{
		TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(FIntPoint(Width, Height), 
			TArray<FFloat16Color, FDefaultAllocator64>(reinterpret_cast<FFloat16Color*>(InBuffer), Width * Height));
		ImageTask->PixelData = MoveTemp(PixelData);
	}
	else
	{
		check(false);
	}

	TFuture<bool> DispatchedTask = ImageWriteQueueModule->GetWriteQueue().Enqueue(MoveTemp(ImageTask));

	if (!bAsync)
	{
		// If not async, wait for the dispatched task to complete.
		if (DispatchedTask.IsValid())
		{
			DispatchedTask.Wait();
		}
	}
}

bool UFileMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue");
	CacheMediaOutputValues();

	SetState(EMediaCaptureState::Capturing);
#if WITH_EDITOR
	FileMediaCaptureAnalytics::SendCaptureEvent(ImageFormat, CompressionQuality, TEXT("SceneViewport"));
#endif
	return true;
}


bool UFileMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue");
	CacheMediaOutputValues();

	SetState(EMediaCaptureState::Capturing);
#if WITH_EDITOR
	FileMediaCaptureAnalytics::SendCaptureEvent(ImageFormat, CompressionQuality, TEXT("RenderTarget"));
#endif
	return true;
}


void UFileMediaCapture::CacheMediaOutputValues()
{
	UFileMediaOutput* FileMediaOutput = CastChecked<UFileMediaOutput>(MediaOutput);
	BaseFilePathName = FPaths::Combine(FileMediaOutput->FilePath.Path, FileMediaOutput->BaseFileName);
	ImageFormat = ImageFormatFromDesired(FileMediaOutput->WriteOptions.Format);
	CompressionQuality = FileMediaOutput->WriteOptions.CompressionQuality;
	bOverwriteFile = FileMediaOutput->WriteOptions.bOverwriteFile;
	bAsync = FileMediaOutput->WriteOptions.bAsync;

	OnCompleteWrapper = [NativeCB = FileMediaOutput->WriteOptions.NativeOnComplete, DynamicCB = FileMediaOutput->WriteOptions.OnComplete](bool bSuccess)
	{
		if (NativeCB)
		{
			NativeCB(bSuccess);
		}
		DynamicCB.ExecuteIfBound(bSuccess);
	};
}
