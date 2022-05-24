// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"

class ULevel;

class ENGINE_API FTraceScreenshot
{
public:
	static FTraceScreenshot* Get() { return Instance; }
	static void CreateInstance();
	void RequestScreenshot(FString Name);
	void HandleScreenshotData(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData);
	void WorldDestroyed(ULevel* InLevel, UWorld* InWorld);

	/* 
	* Add the provided screenshot to the trace.
	* @param InSizeX - The width of the screenshot.
	* @param InSizeY - The heigth of the screenshot.
	* @param InImageData - The data of the screenshot.
	* @param InScreenshotName - The name of the screenshot.
	* @param DesiredX - Optionally resize the image to the desired width before tracing. Aspect ratio is preserved.
	*/
	static void TraceScreenshot(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData, const FString& InScreenshotName, int32 DesiredX = -1);
private:
	FTraceScreenshot();
	FTraceScreenshot(const FTraceScreenshot& Other) {}
	FTraceScreenshot(const FTraceScreenshot&& Other) {}
	void operator =(const FTraceScreenshot& Other) {}
	~FTraceScreenshot();
	void Unbind();

private:
	static FTraceScreenshot* Instance;

	FString ScreenshotName;
	TWeakObjectPtr<UWorld> World;
};