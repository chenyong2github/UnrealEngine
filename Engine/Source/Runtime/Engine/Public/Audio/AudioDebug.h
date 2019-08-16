// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CanvasTypes.h"
#include "CoreMinimal.h"

#define ENABLE_AUDIO_DEBUG !(UE_BUILD_SHIPPING || UE_BUILD_TEST)


#if ENABLE_AUDIO_DEBUG

 // Forward Declarations
struct FActiveSound;
struct FAudioVirtualLoop;
struct FWaveInstance;

class FSoundSource;


class ENGINE_API FAudioDebugger
{
public:
	FAudioDebugger();

	static void DrawDebugInfo(const FSoundSource& SoundSource);
	static void DrawDebugInfo(const FActiveSound& ActiveSound, const TArray<FWaveInstance*>& ThisSoundsWaveInstances, const float DeltaTime);
	static void DrawDebugInfo(const FAudioVirtualLoop& VirtualLoop);
	static bool PostStatModulatorHelp(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	static int32 RenderStatCues(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	static int32 RenderStatMixes(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	static int32 RenderStatModulators(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	static int32 RenderStatReverb(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	static int32 RenderStatSounds(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	static int32 RenderStatWaves(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation);
	static void RemoveDevice(const FAudioDevice& AudioDevice);
	static void ResolveDesiredStats(FViewportClient* ViewportClient);
	static void SendUpdateResultsToGameThread(const FAudioDevice& AudioDevice, const int32 FirstActiveIndex);
	static bool ToggleStatCues(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	static bool ToggleStatMixes(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	static bool ToggleStatModulators(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	static bool ToggleStatSounds(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	static bool ToggleStatWaves(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream);
	static void UpdateAudibleInactiveSounds(const uint32 FirstIndex, const TArray<FWaveInstance*>& WaveInstances);

	void DumpActiveSounds() const;
	bool IsVisualizeDebug3dEnabled() const;
	void ToggleVisualizeDebug3dEnabled();

private:
	static bool ToggleStats(UWorld* World, const uint8 StatToToggle);
	void ToggleStats(const uint32 AudioDeviceHandle, const uint8 StatsToToggle);

	/** Whether or not 3d debug visualization is enabled. */
	uint8 bVisualize3dDebug : 1;
};
#endif // ENABLE_AUDIO_DEBUG
