// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CanvasTypes.h"
#include "CoreMinimal.h"

#include "AudioDefines.h"		// For ENABLE_AUDIO_DEBUG
#include "Audio.h"

#if ENABLE_AUDIO_DEBUG

 // Forward Declarations
struct FActiveSound;
struct FAudioVirtualLoop;
struct FListener;
struct FWaveInstance;

class FSoundSource;
class UWorld;


class ENGINE_API FAudioDebugger
{
public:
	FAudioDebugger();

	/** Struct which contains debug names for run-time debugging of sounds. */
	struct FDebugNames
	{
		TArray<FName> SoloSoundClass;
		TArray<FName> SoloSoundWave;
		TArray<FName> SoloSoundCue;
		TArray<FName> MuteSoundClass;
		TArray<FName> MuteSoundWave;
		TArray<FName> MuteSoundCue;

		FString DebugAudioMixerSoundName;
		FString DebugSoundName;
		bool bDebugSoundName;

		FDebugNames()
			: bDebugSoundName(false)
		{}
	};

	static void DrawDebugInfo(const FSoundSource& SoundSource);
	static void DrawDebugInfo(const FActiveSound& ActiveSound, const TArray<FWaveInstance*>& ThisSoundsWaveInstances, const float DeltaTime);
	static void DrawDebugInfo(UWorld& World, const TArray<FListener>& Listeners, FVector& ListenerTransformOverride, bool bUseListenerTransformOverride);
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

	void ClearMutesAndSolos();
	void DumpActiveSounds() const;

	bool IsVisualizeDebug3dEnabled() const;
	void ToggleVisualizeDebug3dEnabled();
		
	// Evaluate Mute/Solos
	void QuerySoloMuteSoundClass(const FString& Name, bool& bOutIsSoloed, bool& bOutIsMuted, FString& OutReason ) const;
	void QuerySoloMuteSoundWave(const FString& Name, bool& bOutIsSoloed, bool& bOutIsMuted, FString& OutReason) const;
	void QuerySoloMuteSoundCue(const FString& Name, bool& bOutIsSoloed, bool& bOutIsMuted, FString& OutReason ) const;
   
	// Is Mute/Solos. (only audio thread).
	bool IsSoloSoundClass(FName InName) const { return DebugNames.SoloSoundClass.Contains(InName); }
	bool IsSoloSoundWave(FName InName) const  { return DebugNames.SoloSoundWave.Contains(InName); }
	bool IsSoloSoundCue(FName InName) const   { return DebugNames.SoloSoundCue.Contains(InName); }
	bool IsMuteSoundClass(FName InName) const { return DebugNames.MuteSoundClass.Contains(InName); }
	bool IsMuteSoundWave(FName InName) const  { return DebugNames.MuteSoundWave.Contains(InName); }
	bool IsMuteSoundCue(FName InName) const   { return DebugNames.MuteSoundCue.Contains(InName); }
		
	// Mute/Solos toggles. (any thread). (If exclusive, toggle-on will clear everything first, and toggle-off will clear all).
	void ToggleSoloSoundClass(FName InName, bool bExclusive = false) { ToggleNameArray(InName, DebugNames.SoloSoundClass, bExclusive); }
	void ToggleSoloSoundWave(FName InName, bool bExclusive = false)  { ToggleNameArray(InName, DebugNames.SoloSoundWave, bExclusive); }
	void ToggleSoloSoundCue(FName InName, bool bExclusive = false)   { ToggleNameArray(InName, DebugNames.SoloSoundCue, bExclusive); }
	void ToggleMuteSoundClass(FName InName, bool bExclusive = false) { ToggleNameArray(InName, DebugNames.MuteSoundClass, bExclusive); }
	void ToggleMuteSoundWave(FName InName, bool bExclusive = false)  { ToggleNameArray(InName, DebugNames.MuteSoundWave, bExclusive); }
	void ToggleMuteSoundCue(FName InName, bool bExclusive = false)   { ToggleNameArray(InName, DebugNames.MuteSoundCue,	bExclusive); }

	// Set Mute/Solo. (any thread).
	void SetMuteSoundCue(FName InName, bool bInOnOff)  { SetNameArray(InName, DebugNames.MuteSoundCue, bInOnOff); }
	void SetMuteSoundWave(FName InName, bool bInOnOff) { SetNameArray(InName, DebugNames.MuteSoundWave, bInOnOff); }
	void SetSoloSoundCue(FName InName, bool bInOnOff)  { SetNameArray(InName, DebugNames.SoloSoundCue, bInOnOff); }	
	void SetSoloSoundWave(FName InName, bool bInOnOff) { SetNameArray(InName, DebugNames.SoloSoundWave, bInOnOff); }
		   	
	void SetAudioMixerDebugSound(const TCHAR* SoundName);
	void SetAudioDebugSound(const TCHAR* SoundName);

	const FString& GetAudioMixerDebugSoundName() const;
	bool GetAudioDebugSound(FString& OutDebugSound);	  	
private:
	void SetNameArray(FName InName, TArray<FName>& InNameArray, bool bOnOff);
	void ToggleNameArray(FName InName, TArray<FName>& NameArray, bool bExclusive);
	void ExecuteCmdOnAudioThread(TFunction<void()> Cmd);
	
	void GetDebugSoloMuteStateX(
		const FString& Name, const TArray<FName>& Solos, const TArray<FName>& Mutes, 
		bool& bOutIsSoloed, bool& bOutIsMuted, FString& OutReason) const;

	static bool ToggleStats(UWorld* World, const uint8 StatToToggle);
	void ToggleStats(const uint32 AudioDeviceHandle, const uint8 StatsToToggle);

	/** Instance of the debug names struct. */
	FDebugNames DebugNames;

	/** Whether or not 3d debug visualization is enabled. */
	uint8 bVisualize3dDebug : 1;
};
#endif // ENABLE_AUDIO_DEBUG
