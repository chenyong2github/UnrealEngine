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
#include "ProfilingDebugging/MiscTrace.h"
#include "Trace/Trace.inl"

static void TraceScreenshotCommandCallback(const TArray<FString>& Args)
{
	FString Name;
	if (Args.Num() > 0)
	{
		Name = Args[0];
	}

	FTraceScreenshot::RequestScreenshot(Name);
}

static FAutoConsoleCommand TraceScreenshotCmd(
	TEXT("Trace.Screenshot"),
	TEXT("[Name] Takes a screenshot and saves it in the trace."
		" [Name] is the name of the screenshot."
	),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceScreenshotCommandCallback)
);

FString FTraceScreenshot::RequestedScreenshotName;
bool FTraceScreenshot::bSuppressWritingToFile = false;

void FTraceScreenshot::RequestScreenshot(FString Name)
{
	RequestedScreenshotName = Name;
	bool bShowUI = false;
	bSuppressWritingToFile = true;
	FScreenshotRequest::RequestScreenshot(bShowUI);
}

void FTraceScreenshot::TraceScreenshot(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData, const FString& InScreenshotName, int32 DesiredX)
{
	FString ScreenshotName = InScreenshotName;
	if (!RequestedScreenshotName.IsEmpty())
	{
		ScreenshotName = RequestedScreenshotName;
	}
	else if (ScreenshotName.IsEmpty())
	{
		ScreenshotName = FDateTime::Now().ToString(TEXT("Screenshot_%Y%m%d_%H%M%S"));
	}

	uint64 Cycles = FPlatformTime::Cycles64();
	UE_LOG(LogCore, Display, TEXT("Tracing Screenshot \"%s\" taken with size: %d x %d"), *ScreenshotName, InSizeX, InSizeY);

	TArray64<uint8> CompressedBitmap;
	if (DesiredX > 0 && InSizeX != DesiredX)
	{
		int32 ResizedX = FMath::Min(640, InSizeX);
		int32 ResizedY = (InSizeY * ResizedX) / InSizeX;

		TArray<FColor> ResizedImage;
		ResizedImage.SetNum(ResizedX * ResizedY);
		FImageUtils::ImageResize(InSizeX, InSizeY, InImageData, ResizedX, ResizedY, ResizedImage, false);

		FImageUtils::PNGCompressImageArray(ResizedX, ResizedY, TArrayView64<const FColor>(ResizedImage.GetData(), ResizedImage.Num()), CompressedBitmap);
		TRACE_SCREENSHOT(*ScreenshotName, Cycles, ResizedX, ResizedY, CompressedBitmap);
	}
	else
	{
		FImageUtils::PNGCompressImageArray(InSizeX, InSizeY, TArrayView64<const FColor>(InImageData.GetData(), InImageData.Num()), CompressedBitmap);
		TRACE_SCREENSHOT(*ScreenshotName, Cycles, InSizeX, InSizeY, CompressedBitmap);
	}

	Reset();
}

void FTraceScreenshot::Reset()
{
	RequestedScreenshotName.Empty();
	bSuppressWritingToFile = false;
}
