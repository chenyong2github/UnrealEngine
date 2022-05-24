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
	if (Args.Num() > 1)
	{
		Name = Args[0];
	}

	if (FTraceScreenshot::Get() == nullptr)
	{
		FTraceScreenshot::CreateInstance();
	}

	FTraceScreenshot::Get()->RequestScreenshot(Name);
}

static FAutoConsoleCommand TraceScreenshotCmd(
	TEXT("Trace.Screenshot"),
	TEXT("[Name] Takes a screenshot and saves it in the trace."
		" [Name] is the name of the screenshot."
	),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceScreenshotCommandCallback)
);

FTraceScreenshot* FTraceScreenshot::Instance = nullptr;

void FTraceScreenshot::CreateInstance()
{
	if (Instance == nullptr)
	{
		Instance = new FTraceScreenshot();
	}
}

FTraceScreenshot::FTraceScreenshot()
{
	if (GEngine && GEngine->GameViewport)
	{
		FlushRenderingCommands();
		GEngine->GameViewport->OnScreenshotCaptured().AddRaw(this, &FTraceScreenshot::HandleScreenshotData);
		FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FTraceScreenshot::WorldDestroyed);
	}
}

FTraceScreenshot::~FTraceScreenshot()
{
}

void FTraceScreenshot::RequestScreenshot(FString Name)
{
	ScreenshotName = Name;
	bool bShowUI = false;
	FScreenshotRequest::RequestScreenshot(bShowUI);
}

void FTraceScreenshot::HandleScreenshotData(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData)
{
	if (SHOULD_TRACE_SCREENSHOT())
	{
		TraceScreenshot(InSizeX, InSizeY, InImageData, ScreenshotName, 640);
	}
}

void FTraceScreenshot::WorldDestroyed(ULevel* InLevel, UWorld* InWorld)
{
	if (InLevel == nullptr && InWorld == World.Get())
	{
		UE_LOG(LogCore, Display, TEXT("Tracing screenshot \"%s\" skipped - level was removed from world before we got our screenshot"), *ScreenshotName);

		if (Instance)
		{
			delete Instance;
			Instance = nullptr;
			GEngine->GameViewport->OnScreenshotCaptured().RemoveAll(this);
		}
		FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
	}
}

void FTraceScreenshot::TraceScreenshot(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData, const FString& InScreenshotName, int32 DesiredX)
{
	FString ScreenshotName = InScreenshotName;
	if (ScreenshotName.IsEmpty())
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

	ScreenshotName.Empty();
}
