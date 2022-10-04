// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/TraceScreenshot.h"

#include "Containers/Map.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/TlsAutoCleanup.h"
#include "Misc/DateTime.h"
#include "Misc/CString.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Tasks/Task.h"
#include "Trace/Trace.inl"

static void TraceScreenshotCommandCallback(const TArray<FString>& Args)
{
	FString Name;
	bool bShowUI = false;

	if (Args.Num() > 0)
	{
		Name = Args[0];
	}

	if (Args.Num() > 1)
	{
		bShowUI = FCString::ToBool(*Args[1]);
	}

	FTraceScreenshot::RequestScreenshot(Name, bShowUI, LogConsoleResponse);
}

static FAutoConsoleCommand TraceScreenshotCmd(
	TEXT("Trace.Screenshot"),
	TEXT("[Name] [ShowUI] Takes a screenshot and saves it in the trace. Ex: Trace.Screenshot ScreenshotName true"),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceScreenshotCommandCallback)
);

bool FTraceScreenshot::bSuppressWritingToFile = false;

void FTraceScreenshot::RequestScreenshot(FString Name, bool bShowUI, const FLogCategoryAlias& LogCategory)
{
	if (!SHOULD_TRACE_SCREENSHOT())
	{
		UE_LOG_REF(LogCategory, Error, TEXT("Could not trace screenshot because the screenshot trace channel is off. Turn it on using \"Trace.Enable Screenshot\"."));
		return;
	}

	bSuppressWritingToFile = true;

	if (Name.IsEmpty())
	{
		Name = FDateTime::Now().ToString(TEXT("Screenshot_%Y%m%d_%H%M%S"));
	}
	constexpr bool bAddUniqueSuffix = false;
	FScreenshotRequest::RequestScreenshot(Name, bShowUI, bAddUniqueSuffix);
}

void ImageUtilsImageResize(int32 SrcWidth, int32 SrcHeight, const TArray<FColor>& SrcData, int32 DstWidth, int32 DstHeight, TArray<FColor>& DstData, bool bLinearSpace)
{
	FImageUtils::ImageResize(SrcWidth, SrcHeight, SrcData, DstWidth, DstHeight, DstData, bLinearSpace);
}

void ImageUtilsCompressImageArrayWrapper(int32 ImageWidth, int32 ImageHeight, const TArrayView64<const FColor>& SrcData, TArray64<uint8>& DstData)
{
	FImageUtils::PNGCompressImageArray(ImageWidth, ImageHeight, SrcData, DstData);
}

void ImageUtilsImageResize(int32 SrcWidth, int32 SrcHeight, const TArray64<FLinearColor>& SrcData, int32 DstWidth, int32 DstHeight, TArray64<FLinearColor>& DstData, bool /*bUnused*/)
{
	FImageUtils::ImageResize(SrcWidth, SrcHeight, SrcData, DstWidth, DstHeight, DstData);
}

void ImageUtilsCompressImageArrayWrapper(int32 ImageWidth, int32 ImageHeight, const TArrayView64<const FLinearColor>& SrcData, TArray64<uint8>& DstData)
{
	FImageView TmpImageView(SrcData.GetData(), ImageWidth, ImageHeight);
	FImageUtils::CompressImage(DstData, TEXT(".exr"), TmpImageView);
}


template <class FColorType, class FResizedColorArrayType, typename TChannelType>
void TraceScreenshotInternal(int32 InSizeX, int32 InSizeY, const TArray<FColorType>& InImageData, const FString& InScreenshotName, int32 DesiredX, 
							 TChannelType OpaqueAlphaValue)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ScreenshotTracing_Prepare);
	FString ScreenshotName = FPaths::GetBaseFilename(InScreenshotName);

	if (ScreenshotName.IsEmpty())
	{
		ScreenshotName = FDateTime::Now().ToString(TEXT("Screenshot_%Y%m%d_%H%M%S"));
	}

	UE_LOG(LogCore, Display, TEXT("Tracing Screenshot \"%s\" taken with size: %d x %d"), *ScreenshotName, InSizeX, InSizeY);

	uint64 Cycles = FPlatformTime::Cycles64();
	FResizedColorArrayType* ImageCopy = new FResizedColorArrayType(InImageData);

	UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[ImageCopy, Cycles, ScreenshotName, InSizeX, InSizeY, DesiredX, OpaqueAlphaValue]
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ScreenshotTracing_Execute);

			// Set full alpha on the bitmap
			for (FColorType& Pixel : *ImageCopy)
			{
				Pixel.A = OpaqueAlphaValue;
			}

			TArray64<uint8> CompressedBitmap;
			if (DesiredX > 0 && InSizeX != DesiredX)
			{
				int32 ResizedX = FMath::Min(640, InSizeX);
				int32 ResizedY = (InSizeY * ResizedX) / InSizeX;

				FResizedColorArrayType ResizedImage;
				ResizedImage.SetNum(ResizedX * ResizedY);
				ImageUtilsImageResize(InSizeX, InSizeY, *ImageCopy, ResizedX, ResizedY, ResizedImage, false);

				ImageUtilsCompressImageArrayWrapper(ResizedX, ResizedY, TArrayView64<const FColorType>(ResizedImage.GetData(), ResizedImage.Num()), CompressedBitmap);
				TRACE_SCREENSHOT(*ScreenshotName, Cycles, ResizedX, ResizedY, CompressedBitmap);
			}
			else
			{
				ImageUtilsCompressImageArrayWrapper(InSizeX, InSizeY, TArrayView64<const FColorType>(ImageCopy->GetData(), ImageCopy->Num()), CompressedBitmap);
				TRACE_SCREENSHOT(*ScreenshotName, Cycles, InSizeX, InSizeY, CompressedBitmap);
			}

			delete ImageCopy;
		});

}

void FTraceScreenshot::TraceScreenshot(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData, const FString& InScreenshotName, int32 DesiredX)
{
	TraceScreenshotInternal<FColor, TArray<FColor>, uint8>(InSizeX, InSizeY, InImageData, InScreenshotName, DesiredX, 255);
	Reset();
}

void FTraceScreenshot::TraceScreenshot(int32 InSizeX, int32 InSizeY, const TArray<FLinearColor>& InImageData, const FString& InScreenshotName, int32 DesiredX)
{
	TraceScreenshotInternal<FLinearColor, TArray64<FLinearColor>, float>(InSizeX, InSizeY, InImageData, InScreenshotName, DesiredX, 1.0f);
	Reset();
}

void FTraceScreenshot::Reset()
{
	bSuppressWritingToFile = false;
}
