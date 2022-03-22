// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"

class ULevel;

class FTraceScreenshot
{
public:
	static FTraceScreenshot* Get() { return Instance; }
	static void CreateInstance();
	void RequestScreenshot(FString Name);
	void HandleScreenshotData(int32 InSizeX, int32 InSizeY, const TArray<FColor>& InImageData);
	void WorldDestroyed(ULevel* InLevel, UWorld* InWorld);

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