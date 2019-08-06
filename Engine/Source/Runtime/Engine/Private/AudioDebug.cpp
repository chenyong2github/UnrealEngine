// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Audio/AudioDebug.h"

#include "ActiveSound.h"
#include "Audio.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioEffect.h"
#include "AudioVirtualLoop.h"
#include "CanvasTypes.h"
#include "DrawDebugHelpers.h"
#include "Engine/Font.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/AudioSettings.h"
#include "Sound/AudioVolume.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundSourceBus.h"
#include "Sound/SoundWave.h"
#include "UnrealEngine.h"


#if ENABLE_AUDIO_DEBUG

// Console variables
static int32 ActiveSoundVisualizeModeCVar = 1;
FAutoConsoleVariableRef CVarAudioVisualizeActiveSoundsMode(
	TEXT("au.3dVisualize.ActiveSounds"),
	ActiveSoundVisualizeModeCVar,
	TEXT("Visualization mode for active sounds. \n")
	TEXT("0: Not Enabled, 1: Volume (Lin), 2: Volume (dB), 3: Distance, 4: Random color"),
	ECVF_Default);

static int32 ActiveSoundVisualizeTypeCVar = 0;
FAutoConsoleVariableRef CVarAudioVisualizeActiveSounds(
	TEXT("au.3dVisualize.ActiveSounds.Type"),
	ActiveSoundVisualizeTypeCVar,
	TEXT("Whether to show all sounds, on AudioComponents (Components Only), or off of AudioComponents (Non-Component Only). \n")
	TEXT("0: All, 1: Components Only, 2: Non-Component Only"),
	ECVF_Default);

static int32 SpatialSourceVisualizeEnabledCVar = 1;
FAutoConsoleVariableRef CVarAudioVisualizeSpatialSourceEnabled(
	TEXT("au.3dVisualize.SpatialSources"),
	SpatialSourceVisualizeEnabledCVar,
	TEXT("Whether or not audio spatialized sources are visible when 3d visualize is enabled. \n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

static int32 VirtualLoopsVisualizeEnabledCVar = 1;
FAutoConsoleVariableRef CVarAudioVisualizeVirtualLoopsEnabled(
	TEXT("au.3dVisualize.VirtualLoops"),
	VirtualLoopsVisualizeEnabledCVar,
	TEXT("Whether or not virtualized loops are visible when 3d visualize is enabled. \n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

// Test Data
namespace ERequestedAudioStats
{
	static const uint8 SoundWaves = 0x1;
	static const uint8 SoundCues = 0x2;
	static const uint8 Sounds = 0x4;
	static const uint8 SoundMixes = 0x8;
	static const uint8 SoundModulation = 0x10;
	static const uint8 DebugSounds = 0x20;
	static const uint8 LongSoundNames = 0x40;
};

namespace
{
	const FColor HeaderColor = FColor::Green;
	const FColor BodyColor = FColor::White;
	const int32  TabWidth = 12;

	const float MinDisplayVolume = KINDA_SMALL_NUMBER; // -80 dB

	FAudioDevice* GetWorldAudio(UWorld* World)
	{
		check(IsInGameThread());

		if (!World)
		{
			return nullptr;
		}

		return World->GetAudioDevice();
	}

	struct FAudioStats
	{
		enum class EDisplayFlags : uint8
		{
			Debug = 0x01,
			Sort_Distance = 0x02,
			Sort_Class = 0x04,
			Sort_Name = 0x08,
			Sort_WavesNum = 0x10,
			Sort_Disabled = 0x20,
			Long_Names = 0x40,
		};

		struct FStatWaveInstanceInfo
		{
			FString Description;
			float Volume;
			int32 InstanceIndex;
			FName WaveInstanceName;
			uint8 bPlayWhenSilent:1;
		};

		struct FStatSoundInfo
		{
			FString SoundName;
			FName SoundClassName;
			float Distance;
			uint32 AudioComponentID;
			FTransform Transform;
			TArray<FStatWaveInstanceInfo> WaveInstanceInfos;
			TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails> ShapeDetailsMap;
		};

		struct FStatSoundMix
		{
			FString MixName;
			float InterpValue;
			int32 RefCount;
			bool bIsCurrentEQ;
		};

		uint8 DisplayFlags;
		TArray<FTransform> ListenerTransforms;
		TArray<FStatSoundInfo> StatSoundInfos;
		TArray<FStatSoundMix> StatSoundMixes;

		FAudioStats()
			: DisplayFlags(0)
		{
		}
	};

	struct FAudioStats_AudioThread
	{
		uint8 RequestedStats;

		FAudioStats_AudioThread()
			: RequestedStats(0)
		{
		}
	};

	TMap<uint32, FAudioStats> AudioDeviceStats;
	TMap<uint32, FAudioStats_AudioThread> AudioDeviceStats_AudioThread;

	void HandleDumpActiveSounds(UWorld* World)
	{
		if (GEngine)
		{
			GEngine->GetAudioDeviceManager()->GetDebugger().DumpActiveSounds();
		}
	}

	FAutoConsoleCommandWithWorld DumpActiveSounds(TEXT("Audio.DumpActiveSounds"), TEXT("Outputs data about all the currently active sounds."), FConsoleCommandWithWorldDelegate::CreateStatic(&HandleDumpActiveSounds), ECVF_Cheat);
} // namespace <>


/**
 * Audio Debugger Implementation
 */
FAudioDebugger::FAudioDebugger()
	: bVisualize3dDebug(0)
{
}

bool FAudioDebugger::IsVisualizeDebug3dEnabled() const
{
	return bVisualize3dDebug;
}

void FAudioDebugger::ToggleVisualizeDebug3dEnabled()
{
	bVisualize3dDebug = !bVisualize3dDebug;
}

void FAudioDebugger::DrawDebugInfo(const FSoundSource& SoundSource)
{
#if ENABLE_DRAW_DEBUG
	const FWaveInstance* WaveInstance = SoundSource.GetWaveInstance();
	if (!WaveInstance)
	{
		return;
	}

	const FActiveSound* ActiveSound = WaveInstance->ActiveSound;
	if (!ActiveSound)
	{
		return;
	}

	if (!SpatialSourceVisualizeEnabledCVar)
	{
		return;
	}

	FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager();
	if (DeviceManager && DeviceManager->IsVisualizeDebug3dEnabled())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.DrawSourceDebugInfo"), STAT_AudioDrawSourceDebugInfo, STATGROUP_TaskGraphTasks);

		const FSoundBuffer* Buffer = SoundSource.GetBuffer();
		const bool bSpatialized = Buffer && Buffer->NumChannels == 2 && WaveInstance->GetUseSpatialization();
		if (bSpatialized)
		{
			const FRotator Rotator = ActiveSound->Transform.GetRotation().Rotator();

			TWeakObjectPtr<UWorld> WorldPtr = WaveInstance->ActiveSound->GetWeakWorld();
			FVector LeftChannelSourceLoc;
			FVector RightChannelSourceLoc;
			SoundSource.GetChannelLocations(LeftChannelSourceLoc, RightChannelSourceLoc);
			FAudioThread::RunCommandOnGameThread([LeftChannelSourceLoc, RightChannelSourceLoc, Rotator, WorldPtr]()
			{
				if (WorldPtr.IsValid())
				{
					UWorld* World = WorldPtr.Get();
					DrawDebugCrosshairs(World, LeftChannelSourceLoc, Rotator, 20.0f, FColor::Red, false, -1.0f, SDPG_Foreground);
					DrawDebugCrosshairs(World, RightChannelSourceLoc, Rotator, 20.0f, FColor::Green, false, -1.0f, SDPG_Foreground);
				}
			}, GET_STATID(STAT_AudioDrawSourceDebugInfo));
		}
	}
#endif // ENABLE_DRAW_DEBUG
}

void FAudioDebugger::DrawDebugInfo(const FActiveSound& ActiveSound, const TArray<FWaveInstance*>& ThisSoundsWaveInstances)
{
#if ENABLE_DRAW_DEBUG
	if (!ActiveSoundVisualizeModeCVar)
	{
		return;
	}

	// Only draw spatialized sounds
	const USoundBase* Sound = ActiveSound.GetSound();
	if (!Sound || !ActiveSound.bAllowSpatialization)
	{
		return;
	}

	if (ActiveSoundVisualizeTypeCVar > 0)
	{
		if (ActiveSoundVisualizeTypeCVar == 1 && ActiveSound.GetAudioComponentID() == 0)
		{
			return;
		}

		if (ActiveSoundVisualizeTypeCVar == 2 && ActiveSound.GetAudioComponentID() > 0)
		{
			return;
		}
	}

	FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager();
	if (DeviceManager && DeviceManager->IsVisualizeDebug3dEnabled())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.DrawActiveSoundDebugInfo"), STAT_AudioDrawActiveSoundDebugInfo, STATGROUP_TaskGraphTasks);

		const FString Name = Sound->GetName();
		const FTransform CurTransform = ActiveSound.Transform;
		FColor TextColor = FColor::White;
		const float CurMaxDistance = ActiveSound.MaxDistance;
		float DisplayValue = 0.0f;
		if (ActiveSoundVisualizeModeCVar == 1 || ActiveSoundVisualizeModeCVar == 2)
		{
			for (FWaveInstance* WaveInstance : ThisSoundsWaveInstances)
			{
				DisplayValue = FMath::Max(DisplayValue, WaveInstance->GetVolumeWithDistanceAttenuation() * WaveInstance->GetDynamicVolume());
			}
		}
		else if (ActiveSoundVisualizeModeCVar == 3)
		{
			if (ActiveSound.AudioDevice)
			{
				DisplayValue = ActiveSound.AudioDevice->GetDistanceToNearestListener(ActiveSound.Transform.GetLocation()) / CurMaxDistance;
			}
		}
		else if (ActiveSoundVisualizeModeCVar == 4)
		{
			TextColor = ActiveSound.DebugColor;
		}

		TWeakObjectPtr<UWorld> WorldPtr = ActiveSound.GetWeakWorld();
		FAudioThread::RunCommandOnGameThread([Name, TextColor, CurTransform, DisplayValue, WorldPtr, CurMaxDistance]()
		{
			if (WorldPtr.IsValid())
			{
				static const float ColorRedHue = 0.0f;
				static const float ColorGreenHue = 85.0f;

				const FVector Location = CurTransform.GetLocation();
				UWorld* DebugWorld = WorldPtr.Get();
				DrawDebugSphere(DebugWorld, Location, 10.0f, 8, FColor::White, false, -1.0f, SDPG_Foreground);
				FColor Color = TextColor;

				FString Descriptor;
				if (ActiveSoundVisualizeModeCVar == 1 || ActiveSoundVisualizeModeCVar == 2)
				{
					const float DisplayDbVolume = Audio::ConvertToDecibels(DisplayValue);
					if (ActiveSoundVisualizeModeCVar == 1)
					{
						Descriptor = FString::Printf(TEXT(" (Vol: %.3f)"), DisplayValue);
					}
					else
					{
						Descriptor = FString::Printf(TEXT(" (Vol: %.3f dB)"), DisplayDbVolume);
					}
					static const float DbColorMinVol = -30.0f;
					const float DbVolume = FMath::Clamp(DisplayDbVolume, DbColorMinVol, 0.0f);
					const float Hue = FMath::Lerp(ColorRedHue, ColorGreenHue, (-1.0f * DbVolume / DbColorMinVol) + 1.0f);
					Color = FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue), 255u, 255u).ToFColor(true);
				}
				else if (ActiveSoundVisualizeModeCVar == 3)
				{
					Descriptor = FString::Printf(TEXT(" (Dist: %.3f, Max: %.3f)"), DisplayValue * CurMaxDistance, CurMaxDistance);
					const float Hue = FMath::Lerp(ColorGreenHue, ColorRedHue, DisplayValue);
					Color = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Clamp(Hue, 0.0f, 255.f)), 255u, 255u).ToFColor(true);
				}

				const FString Description = FString::Printf(TEXT("%s%s"), *Name, *Descriptor);
				DrawDebugString(DebugWorld, Location + FVector(0, 0, 32), *Description, nullptr, Color, 0.03f, false);
			}
		}, GET_STATID(STAT_AudioDrawActiveSoundDebugInfo));
	}
#endif // ENABLE_DRAW_DEBUG
}

void FAudioDebugger::DrawDebugInfo(const FAudioVirtualLoop& VirtualLoop)
{
#if ENABLE_DRAW_DEBUG
	if (!GEngine)
	{
		return;
	}

	if (!VirtualLoopsVisualizeEnabledCVar)
	{
		return;
	}

	FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager();
	if (DeviceManager && DeviceManager->IsVisualizeDebug3dEnabled())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.DrawVirtualLoopDebugInfo"), STAT_AudioDrawVirtualLoopDebugInfo, STATGROUP_TaskGraphTasks);

		const FActiveSound& ActiveSound = VirtualLoop.GetActiveSound();
		USoundBase* Sound = ActiveSound.GetSound();
		check(Sound);

		const FTransform Transform = ActiveSound.Transform;
		const TWeakObjectPtr<UWorld> World = ActiveSound.GetWeakWorld();
		const FString Name = Sound->GetName();
		const float DrawInterval = VirtualLoop.GetUpdateInterval();
		FAudioThread::RunCommandOnGameThread([World, Transform, Name, DrawInterval]()
		{
			if (World.IsValid())
			{
				const FString Description = FString::Printf(TEXT("%s [V]"), *Name);
				FVector Location = Transform.GetLocation();
				FRotator Rotation = Transform.GetRotation().Rotator();
				DrawDebugCrosshairs(World.Get(), Location, Rotation, 20.0f, FColor::Blue, false, DrawInterval, SDPG_Foreground);
				DrawDebugString(World.Get(), Location + FVector(0, 0, 32), *Description, nullptr, FColor::Blue, DrawInterval, false);
			}
		}, GET_STATID(STAT_AudioDrawVirtualLoopDebugInfo));
	}
#endif // ENABLE_DRAW_DEBUG
}

void FAudioDebugger::DumpActiveSounds() const
{
	if (!GEngine)
	{
		return;
	}

	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.DumpActiveSounds"), STAT_AudioDumpActiveSounds, STATGROUP_TaskGraphTasks);
		FAudioThread::RunCommandOnAudioThread([this]()
		{
			DumpActiveSounds();
		}, GET_STATID(STAT_AudioDumpActiveSounds));
		return;
	}

	FAudioDevice* AudioDevice = GEngine->GetAudioDeviceManager()->GetActiveAudioDevice();
	if (!AudioDevice)
	{
		return;
	}

	const TArray<FActiveSound*>& ActiveSounds = AudioDevice->GetActiveSounds();
	UE_LOG(LogAudio, Display, TEXT("Active Sound Count: %d"), ActiveSounds.Num());
	UE_LOG(LogAudio, Display, TEXT("------------------------"), ActiveSounds.Num());

	for (const FActiveSound* ActiveSound : ActiveSounds)
	{
		if (ActiveSound)
		{
			UE_LOG(LogAudio, Display, TEXT("%s (%.3g) - %s"), *ActiveSound->GetSound()->GetName(), ActiveSound->GetSound()->GetDuration(), *ActiveSound->GetAudioComponentName());

			for (const TPair<UPTRINT, FWaveInstance*>& WaveInstancePair : ActiveSound->GetWaveInstances())
			{
				const FWaveInstance* WaveInstance = WaveInstancePair.Value;
				UE_LOG(LogAudio, Display, TEXT("   %s (%.3g) (%d) - %.3g"),
					*WaveInstance->GetName(), WaveInstance->WaveData->GetDuration(),
					WaveInstance->WaveData->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal),
					WaveInstance->GetVolumeWithDistanceAttenuation() * WaveInstance->GetDynamicVolume());
			}
		}
	}
}

void FAudioDebugger::ResolveDesiredStats(FViewportClient* ViewportClient)
{
	if (!ViewportClient)
	{
		return;
	}

	FAudioDevice* AudioDevice = GetWorldAudio(ViewportClient->GetWorld());
	if (!AudioDevice)
	{
		return;
	}

	uint8 SetStats = 0;
	uint8 ClearStats = 0;

	if (ViewportClient->IsStatEnabled(TEXT("SoundCues")))
	{
		SetStats |= ERequestedAudioStats::SoundCues;
	}
	else
	{
		ClearStats |= ERequestedAudioStats::SoundCues;
	}

	if (ViewportClient->IsStatEnabled(TEXT("SoundWaves")))
	{
		SetStats |= ERequestedAudioStats::SoundWaves;
	}
	else
	{
		ClearStats |= ERequestedAudioStats::SoundWaves;
	}

	if (ViewportClient->IsStatEnabled(TEXT("SoundMixes")))
	{
		SetStats |= ERequestedAudioStats::SoundMixes;
	}
	else
	{
		ClearStats |= ERequestedAudioStats::SoundMixes;
	}

	if (ViewportClient->IsStatEnabled(TEXT("Sounds")))
	{
		FAudioStats& Stats = AudioDeviceStats.FindOrAdd(AudioDevice->DeviceHandle);
		SetStats |= ERequestedAudioStats::Sounds;

		if (Stats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Debug))
		{
			SetStats |= ERequestedAudioStats::DebugSounds;
		}
		else
		{
			ClearStats |= ERequestedAudioStats::DebugSounds;
		}

		if (Stats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Long_Names))
		{
			SetStats |= ERequestedAudioStats::LongSoundNames;
		}
		else
		{
			ClearStats |= ERequestedAudioStats::LongSoundNames;
		}
	}
	else
	{
		ClearStats |= ERequestedAudioStats::Sounds;
		ClearStats |= ERequestedAudioStats::DebugSounds;
		ClearStats |= ERequestedAudioStats::LongSoundNames;
	}

	DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.ResolveDesiredStats"), STAT_AudioResolveDesiredStats, STATGROUP_TaskGraphTasks);

	const uint32 DeviceID = AudioDevice->DeviceHandle;
	if (IsInAudioThread())
	{
		FAudioStats_AudioThread& Stats = AudioDeviceStats_AudioThread.FindOrAdd(DeviceID);
		Stats.RequestedStats |= SetStats;
		Stats.RequestedStats &= ~ClearStats;
	}
	else
	{
		FAudioThread::RunCommandOnAudioThread([SetStats, ClearStats, DeviceID]()
		{
			FAudioStats_AudioThread& Stats = AudioDeviceStats_AudioThread.FindOrAdd(DeviceID);
			Stats.RequestedStats |= SetStats;
			Stats.RequestedStats &= ~ClearStats;
		}, GET_STATID(STAT_AudioResolveDesiredStats));
	}
}

int32 FAudioDebugger::RenderStatCues(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return Y;
	}

	const int32 FontHeight = GetStatsFont()->GetMaxCharHeight() + 2;
	Canvas->DrawShadowedString(X, Y, TEXT("Active Sound Cues:"), GetStatsFont(), HeaderColor);
	Y += FontHeight;

	int32 ActiveSoundCount = 0;
	FAudioStats& AudioStats = AudioDeviceStats.FindOrAdd(AudioDevice->DeviceHandle);
	for (const FAudioStats::FStatSoundInfo& StatSoundInfo : AudioStats.StatSoundInfos)
	{
		for (const FAudioStats::FStatWaveInstanceInfo& WaveInstanceInfo : StatSoundInfo.WaveInstanceInfos)
		{
			if (WaveInstanceInfo.Volume >= MinDisplayVolume)
			{
				const FString TheString = FString::Printf(TEXT("%4i. %s %s"), ActiveSoundCount++, *StatSoundInfo.SoundName, *StatSoundInfo.SoundClassName.ToString());
				Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FColor::White);
				Y += FontHeight;
				break;
			}
		}
	}

	Canvas->DrawShadowedString(X, Y, *FString::Printf(TEXT("Total: %i"), ActiveSoundCount), GetStatsFont(), BodyColor);
	Y += FontHeight;

	return Y;
}

int32 FAudioDebugger::RenderStatMixes(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return Y;
	}

	const int32 FontHeight = GetStatsFont()->GetMaxCharHeight() + 2;
	Canvas->DrawShadowedString(X, Y, TEXT("Active Sound Mixes:"), GetStatsFont(), HeaderColor);
	Y += FontHeight;

	bool bDisplayedSoundMixes = false;

	FAudioStats& AudioStats = AudioDeviceStats.FindOrAdd(AudioDevice->DeviceHandle);
	if (AudioStats.StatSoundMixes.Num() > 0)
	{
		bDisplayedSoundMixes = true;

		for (const FAudioStats::FStatSoundMix& StatSoundMix : AudioStats.StatSoundMixes)
		{
			const FString TheString = FString::Printf(TEXT("%s - Fade Proportion: %1.2f - Total Ref Count: %i"), *StatSoundMix.MixName, StatSoundMix.InterpValue, StatSoundMix.RefCount);

			const FColor& TextColour = (StatSoundMix.bIsCurrentEQ ? FColor::Yellow : FColor::White);

			Canvas->DrawShadowedString(X + TabWidth, Y, *TheString, GetStatsFont(), TextColour);
			Y += FontHeight;
		}
	}

	if (!bDisplayedSoundMixes)
	{
		Canvas->DrawShadowedString(X + TabWidth, Y, TEXT("None"), GetStatsFont(), FColor::White);
		Y += FontHeight;
	}

	return Y;
}

int32 FAudioDebugger::RenderStatModulators(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return Y;
	}

	const int32 FontHeight = GetStatsFont()->GetMaxCharHeight() + 2;
	Canvas->DrawShadowedString(X, Y, TEXT("Active Sound Modulation:"), GetStatsFont(), HeaderColor);
	Y += FontHeight;

	bool bDisplayedSoundModulationInfo = false;

	if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
	{
		const int32 YInit = Y;
		Y = Modulation->OnRenderStat(Viewport, Canvas, X, Y, *GetStatsFont(), ViewLocation, ViewRotation);
		bDisplayedSoundModulationInfo = Y != YInit;
	}

	if (!bDisplayedSoundModulationInfo)
	{
		Canvas->DrawShadowedString(X + TabWidth, Y, TEXT("None"), GetStatsFont(), FColor::White);
		Y += FontHeight;
	}

	return Y;
}

int32 FAudioDebugger::RenderStatReverb(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return Y;
	}

	const int32 Height = static_cast<int32>(GetStatsFont()->GetMaxCharHeight() + 2);

	FString TheString;
	if (UReverbEffect* ReverbEffect = AudioDevice->GetCurrentReverbEffect())
	{
		TheString = FString::Printf(TEXT("Active Reverb Effect: %s"), *ReverbEffect->GetName());
		Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FLinearColor::White);
		Y += Height;

		AAudioVolume* CurrentAudioVolume = nullptr;
		for (const FTransform& Transform : AudioDevice->ListenerTransforms)
		{
			AAudioVolume* PlayerAudioVolume = World->GetAudioSettings(Transform.GetLocation(), nullptr, nullptr);
			if (PlayerAudioVolume && ((CurrentAudioVolume == nullptr) || (PlayerAudioVolume->GetPriority() > CurrentAudioVolume->GetPriority())))
			{
				CurrentAudioVolume = PlayerAudioVolume;
			}
		}
		if (CurrentAudioVolume && CurrentAudioVolume->GetReverbSettings().ReverbEffect)
		{
			TheString = FString::Printf(TEXT("  Audio Volume Reverb Effect: %s (Priority: %g Volume Name: %s)"), *CurrentAudioVolume->GetReverbSettings().ReverbEffect->GetName(), CurrentAudioVolume->GetPriority(), *CurrentAudioVolume->GetName());
		}
		else
		{
			TheString = TEXT("  Audio Volume Reverb Effect: None");
		}
		Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FLinearColor::White);
		Y += Height;

		const TMap<FName, FActivatedReverb>& ActivatedReverbs = AudioDevice->GetActiveReverb();
		if (ActivatedReverbs.Num() == 0)
		{
			TheString = TEXT("  Activated Reverb: None");
			Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FLinearColor::White);
			Y += Height;
		}
		else if (ActivatedReverbs.Num() == 1)
		{
			auto It = ActivatedReverbs.CreateConstIterator();
			TheString = FString::Printf(TEXT("  Activated Reverb Effect: %s (Priority: %g Tag: '%s')"), *It.Value().ReverbSettings.ReverbEffect->GetName(), It.Value().Priority, *It.Key().ToString());
			Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FLinearColor::White);
			Y += Height;
		}
		else
		{
			Canvas->DrawShadowedString(X, Y, TEXT("  Activated Reverb Effects:"), GetStatsFont(), FLinearColor::White);
			Y += Height;
			TMap<int32, FString> PrioritySortedActivatedReverbs;
			for (auto It = ActivatedReverbs.CreateConstIterator(); It; ++It)
			{
				TheString = FString::Printf(TEXT("    %s (Priority: %g Tag: '%s')"), *It.Value().ReverbSettings.ReverbEffect->GetName(), It.Value().Priority, *It.Key().ToString());
				PrioritySortedActivatedReverbs.Add(It.Value().Priority, TheString);
			}
			for (auto It = PrioritySortedActivatedReverbs.CreateConstIterator(); It; ++It)
			{
				Canvas->DrawShadowedString(X, Y, *It.Value(), GetStatsFont(), FLinearColor::White);
				Y += Height;
			}
		}
	}
	else
	{
		TheString = TEXT("Active Reverb Effect: None");
		Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FLinearColor::White);
		Y += Height;
	}

	return Y;
}

int32 FAudioDebugger::RenderStatSounds(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return Y;
	}

	const int32 FontHeight = GetStatsFont()->GetMaxCharHeight() + 2;
	Y += FontHeight;

	FAudioStats& AudioStats = AudioDeviceStats.FindOrAdd(AudioDevice->DeviceHandle);

	const uint8 bDebug = AudioStats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Debug);

	FString SortingName = TEXT("disabled");

	// Sort the list.
	if (AudioStats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Name))
	{
		AudioStats.StatSoundInfos.Sort([](const FAudioStats::FStatSoundInfo& A, const FAudioStats::FStatSoundInfo& B) { return A.SoundName < B.SoundName; });
		SortingName = TEXT("pathname");
	}
	else if (AudioStats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Distance))
	{
		AudioStats.StatSoundInfos.Sort([](const FAudioStats::FStatSoundInfo& A, const FAudioStats::FStatSoundInfo& B) { return A.Distance < B.Distance; });
		SortingName = TEXT("distance");
	}
	else if (AudioStats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Class))
	{
		AudioStats.StatSoundInfos.Sort([](const FAudioStats::FStatSoundInfo& A, const FAudioStats::FStatSoundInfo& B) { return A.SoundClassName.LexicalLess(B.SoundClassName); });
		SortingName = TEXT("class");
	}
	else if (AudioStats.DisplayFlags & static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_WavesNum))
	{
		AudioStats.StatSoundInfos.Sort([](const FAudioStats::FStatSoundInfo& A, const FAudioStats::FStatSoundInfo& B) { return A.WaveInstanceInfos.Num() > B.WaveInstanceInfos.Num(); });
		SortingName = TEXT("waves' num");
	}

	Canvas->DrawShadowedString(X, Y, TEXT("Active Sounds:"), GetStatsFont(), HeaderColor);
	Y += FontHeight;

	const FString InfoText = FString::Printf(TEXT(" Sorting: %s, Debug: %s"), *SortingName, bDebug ? TEXT("enabled") : TEXT("disabled"));
	Canvas->DrawShadowedString(X, Y, *InfoText, GetStatsFont(), FColor(128, 255, 128));
	Y += FontHeight;

	Canvas->DrawShadowedString(X, Y, TEXT("Index Path (Class) Distance"), GetStatsFont(), BodyColor);
	Y += FontHeight;

	int32 TotalSoundWavesNum = 0;
	for (int32 SoundIndex = 0; SoundIndex < AudioStats.StatSoundInfos.Num(); ++SoundIndex)
	{
		const FAudioStats::FStatSoundInfo& StatSoundInfo = AudioStats.StatSoundInfos[SoundIndex];
		const int32 WaveInstancesNum = StatSoundInfo.WaveInstanceInfos.Num();
		if (WaveInstancesNum > 0)
		{
			{
				const FString TheString = FString::Printf(TEXT("%4i. %s (%s) %6.2f"), SoundIndex, *StatSoundInfo.SoundName, *StatSoundInfo.SoundClassName.ToString(), StatSoundInfo.Distance);
				Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FColor::White);
				Y += FontHeight;
			}

			TotalSoundWavesNum += WaveInstancesNum;

			// Get the active sound waves.
			for (int32 WaveIndex = 0; WaveIndex < WaveInstancesNum; WaveIndex++)
			{
				const FString TheString = *FString::Printf(TEXT("    %4i. %s"), WaveIndex, *StatSoundInfo.WaveInstanceInfos[WaveIndex].Description);
				Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), FColor(205, 205, 205));
				Y += FontHeight;
			}
		}
	}

	Canvas->DrawShadowedString(X, Y, *FString::Printf(TEXT("Audio Device ID: %u"), AudioDevice->DeviceHandle), GetStatsFont(), HeaderColor);
	Y += FontHeight;

	Canvas->DrawShadowedString(X, Y, *FString::Printf(TEXT("Total Sounds: %i, Sound Waves: %i"), AudioStats.StatSoundInfos.Num(), TotalSoundWavesNum), GetStatsFont(), HeaderColor);
	Y += FontHeight;

	for (int32 i = 0; i < AudioStats.ListenerTransforms.Num(); ++i)
	{
		FString LocStr = AudioStats.ListenerTransforms[i].GetLocation().ToString();
		Canvas->DrawShadowedString(X, Y, *FString::Printf(TEXT("Listener '%d' Position: %s"), i, *LocStr), GetStatsFont(), HeaderColor);
		Y += FontHeight;
	}

	if (!bDebug)
	{
		return Y;
	}

	// Draw sound cue's sphere only in debug.
	for (const FAudioStats::FStatSoundInfo& StatSoundInfo : AudioStats.StatSoundInfos)
	{
		const FTransform& SoundTransform = StatSoundInfo.Transform;
		const int32 WaveInstancesNum = StatSoundInfo.WaveInstanceInfos.Num();

		if (StatSoundInfo.Distance > 100.0f && WaveInstancesNum > 0)
		{
			float SphereRadius = 0.f;
			float SphereInnerRadius = 0.f;

			if (StatSoundInfo.ShapeDetailsMap.Num() > 0)
			{
				DrawDebugString(World, SoundTransform.GetTranslation(), StatSoundInfo.SoundName, NULL, FColor::White, 0.01f);

				for (auto ShapeDetailsIt = StatSoundInfo.ShapeDetailsMap.CreateConstIterator(); ShapeDetailsIt; ++ShapeDetailsIt)
				{
					const FBaseAttenuationSettings::AttenuationShapeDetails& ShapeDetails = ShapeDetailsIt.Value();
					switch (ShapeDetailsIt.Key())
					{
					case EAttenuationShape::Sphere:
						if (ShapeDetails.Falloff > 0.f)
						{
							DrawDebugSphere(World, SoundTransform.GetTranslation(), ShapeDetails.Extents.X + ShapeDetails.Falloff, 10, FColor(155, 155, 255));
							DrawDebugSphere(World, SoundTransform.GetTranslation(), ShapeDetails.Extents.X, 10, FColor(55, 55, 255));
						}
						else
						{
							DrawDebugSphere(World, SoundTransform.GetTranslation(), ShapeDetails.Extents.X, 10, FColor(155, 155, 255));
						}
						break;

					case EAttenuationShape::Box:
						if (ShapeDetails.Falloff > 0.f)
						{
							DrawDebugBox(World, SoundTransform.GetTranslation(), ShapeDetails.Extents + FVector(ShapeDetails.Falloff), SoundTransform.GetRotation(), FColor(155, 155, 255));
							DrawDebugBox(World, SoundTransform.GetTranslation(), ShapeDetails.Extents, SoundTransform.GetRotation(), FColor(55, 55, 255));
						}
						else
						{
							DrawDebugBox(World, SoundTransform.GetTranslation(), ShapeDetails.Extents, SoundTransform.GetRotation(), FColor(155, 155, 255));
						}
						break;

					case EAttenuationShape::Capsule:

						if (ShapeDetails.Falloff > 0.f)
						{
							DrawDebugCapsule(World, SoundTransform.GetTranslation(), ShapeDetails.Extents.X + ShapeDetails.Falloff, ShapeDetails.Extents.Y + ShapeDetails.Falloff, SoundTransform.GetRotation(), FColor(155, 155, 255));
							DrawDebugCapsule(World, SoundTransform.GetTranslation(), ShapeDetails.Extents.X, ShapeDetails.Extents.Y, SoundTransform.GetRotation(), FColor(55, 55, 255));
						}
						else
						{
							DrawDebugCapsule(World, SoundTransform.GetTranslation(), ShapeDetails.Extents.X, ShapeDetails.Extents.Y, SoundTransform.GetRotation(), FColor(155, 155, 255));
						}
						break;

					case EAttenuationShape::Cone:
					{
						const FVector Origin = SoundTransform.GetTranslation() - (SoundTransform.GetUnitAxis(EAxis::X) * ShapeDetails.ConeOffset);

						if (ShapeDetails.Falloff > 0.f || ShapeDetails.Extents.Z > 0.f)
						{
							const float OuterAngle = FMath::DegreesToRadians(ShapeDetails.Extents.Y + ShapeDetails.Extents.Z);
							const float InnerAngle = FMath::DegreesToRadians(ShapeDetails.Extents.Y);
							DrawDebugCone(World, Origin, SoundTransform.GetUnitAxis(EAxis::X), ShapeDetails.Extents.X + ShapeDetails.Falloff + ShapeDetails.ConeOffset, OuterAngle, OuterAngle, 10, FColor(155, 155, 255));
							DrawDebugCone(World, Origin, SoundTransform.GetUnitAxis(EAxis::X), ShapeDetails.Extents.X + ShapeDetails.ConeOffset, InnerAngle, InnerAngle, 10, FColor(55, 55, 255));
						}
						else
						{
							const float Angle = FMath::DegreesToRadians(ShapeDetails.Extents.Y);
							DrawDebugCone(World, Origin, SoundTransform.GetUnitAxis(EAxis::X), ShapeDetails.Extents.X + ShapeDetails.ConeOffset, Angle, Angle, 10, FColor(155, 155, 255));
						}
						break;
					}

					default:
						check(false);
					}
				}
			}
		}
	}

	return Y;
}

int32 FAudioDebugger::RenderStatWaves(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return Y;
	}

	const int32 FontHeight = GetStatsFont()->GetMaxCharHeight() + 2;

	FAudioStats& AudioStats = AudioDeviceStats.FindOrAdd(AudioDevice->DeviceHandle);
	Canvas->DrawShadowedString(X, Y, TEXT("Active Sound Waves:"), GetStatsFont(), FLinearColor::White);
	Y += TabWidth;

	using FWaveInstancePair = TPair<const FAudioStats::FStatWaveInstanceInfo*, const FAudioStats::FStatSoundInfo*>;
	TArray<FWaveInstancePair> WaveInstances;
	for (const FAudioStats::FStatSoundInfo& StatSoundInfo : AudioStats.StatSoundInfos)
	{
		for (const FAudioStats::FStatWaveInstanceInfo& WaveInstanceInfo : StatSoundInfo.WaveInstanceInfos)
		{
			if (WaveInstanceInfo.Volume >= MinDisplayVolume || WaveInstanceInfo.bPlayWhenSilent != 0)
			{
				WaveInstances.Emplace(&WaveInstanceInfo, &StatSoundInfo);
			}
		}
	}

	WaveInstances.Sort([](const FWaveInstancePair& A, const FWaveInstancePair& B) { return A.Key->InstanceIndex < B.Key->InstanceIndex; });

	for (const FWaveInstancePair& WaveInstanceInfo : WaveInstances)
	{
		UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(WaveInstanceInfo.Value->AudioComponentID);
		AActor* SoundOwner = AudioComponent ? AudioComponent->GetOwner() : nullptr;

		FString TheString = *FString::Printf(TEXT("%4i.    %6.2f  %s   Owner: %s   SoundClass: %s"),
			WaveInstanceInfo.Key->InstanceIndex,
			WaveInstanceInfo.Key->Volume,
			*WaveInstanceInfo.Key->WaveInstanceName.ToString(),
			SoundOwner ? *SoundOwner->GetName() : TEXT("None"),
			*WaveInstanceInfo.Value->SoundClassName.ToString());
		Canvas->DrawShadowedString(X, Y, *TheString, GetStatsFont(), WaveInstanceInfo.Key->bPlayWhenSilent == 0 ? FColor::White : FColor::Yellow);
		Y += FontHeight;
	}

	const int32 ActiveInstances = WaveInstances.Num();

	const int32 Max = AudioDevice->GetMaxChannels() / 2;
	float f = FMath::Clamp<float>((float)(ActiveInstances - Max) / (float)Max, 0.f, 1.f);
	const int32 R = FMath::TruncToInt(f * 255);

	if (ActiveInstances > Max)
	{
		f = FMath::Clamp<float>((float)(Max - ActiveInstances) / (float)Max, 0.5f, 1.f);
	}
	else
	{
		f = 1.0f;
	}
	const int32 G = FMath::TruncToInt(f * 255);
	const int32 B = 0;

	Canvas->DrawShadowedString(X, Y, *FString::Printf(TEXT(" Total: %i"), ActiveInstances), GetStatsFont(), FColor(R, G, B));
	Y += FontHeight;

	return Y;
}

void FAudioDebugger::RemoveDevice(const FAudioDevice& AudioDevice)
{
	AudioDeviceStats.Remove(AudioDevice.DeviceHandle);
	AudioDeviceStats_AudioThread.Remove(AudioDevice.DeviceHandle);
}

bool FAudioDebugger::ToggleStats(UWorld* World, const uint8 StatToToggle)
{
	if (!GEngine)
	{
		return false;
	}

	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return false;
	}

	if (FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager())
	{
		DeviceManager->GetDebugger().ToggleStats(AudioDevice->DeviceHandle, StatToToggle);
	}

	return true;
}

void FAudioDebugger::ToggleStats(const uint32 AudioDeviceHandle, const uint8 StatsToToggle)
{
	if (!IsInAudioThread())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.ToggleStats"), STAT_AudioToggleStats, STATGROUP_TaskGraphTasks);

		FAudioThread::RunCommandOnAudioThread([this, AudioDeviceHandle, StatsToToggle]()
		{
			ToggleStats(AudioDeviceHandle, StatsToToggle);
		}, GET_STATID(STAT_AudioToggleStats));
		return;
	}

	FAudioStats_AudioThread& Stats = AudioDeviceStats_AudioThread.FindOrAdd(AudioDeviceHandle);
	Stats.RequestedStats ^= StatsToToggle;
}

bool FAudioDebugger::ToggleStatCues(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	return ToggleStats(World, ERequestedAudioStats::SoundCues);
}

bool FAudioDebugger::ToggleStatMixes(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	return ToggleStats(World, ERequestedAudioStats::SoundMixes);
}

bool FAudioDebugger::ToggleStatModulators(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	if (!GEngine)
	{
		return false;
	}

	FAudioDevice* AudioDevice = GetWorldAudio(World);
	if (!AudioDevice)
	{
		return false;
	}

	if (AudioDevice->IsModulationPluginEnabled())
	{
		if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
		{
			if (!Modulation->OnToggleStat(ViewportClient, Stream))
			{
				return false;
			}
		}
	}

	return true;
}

bool FAudioDebugger::PostStatModulatorHelp(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	// Ignore if all Viewports are closed.
	if (!ViewportClient)
	{
		return false;
	}

	if (FAudioDevice* AudioDevice = World->GetAudioDevice())
	{
		if (AudioDevice->IsModulationPluginEnabled())
		{
			if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
			{
				if (!Modulation->OnPostHelp(ViewportClient, Stream))
				{
					return false;
				}
			}
		}
	}

	return true;
}

bool FAudioDebugger::ToggleStatSounds(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	// Ignore if all Viewports are closed.
	if (!ViewportClient)
	{
		return false;
	}

	if (!ToggleStats(World, ERequestedAudioStats::Sounds))
	{
		return false;
	}

	const bool bHelp = Stream ? FCString::Stristr(Stream, TEXT("?")) != nullptr : false;
	if (bHelp)
	{
		GLog->Logf(TEXT("stat sounds description"));
		GLog->Logf(TEXT("  stat sounds off - Disables drawing stat sounds"));
		GLog->Logf(TEXT("  stat sounds sort=distance|class|name|waves|default"));
		GLog->Logf(TEXT("      distance - sort list by distance to player"));
		GLog->Logf(TEXT("      class - sort by sound class name"));
		GLog->Logf(TEXT("      name - sort by cue pathname"));
		GLog->Logf(TEXT("      waves - sort by waves' num"));
		GLog->Logf(TEXT("      default - sorting is disabled"));
		GLog->Logf(TEXT("  stat sounds -debug - enables debugging mode like showing sound radius sphere and names, but only for cues with enabled property bDebug"));
		GLog->Logf(TEXT("  stat sounds -smalltext - use large text in debug output (default)"));
		GLog->Logf(TEXT("  stat sounds -largetext - use large text in debug output"));
		GLog->Logf(TEXT(""));
		GLog->Logf(TEXT("Ex. stat sounds sort=class -debug"));
		GLog->Logf(TEXT(" This will show only debug sounds sorted by sound class"));
	}

	uint8 ShowSounds = 0;
	if (Stream)
	{
		const bool bHide = FParse::Command(&Stream, TEXT("off"));
		if (!bHide)
		{
			const bool bDebug = FParse::Param(Stream, TEXT("debug"));
			if (bDebug)
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Debug);
			}

			const bool bLongNames = FParse::Param(Stream, TEXT("longnames"));
			if (bLongNames)
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Long_Names);
			}

			FString SortStr;
			FParse::Value(Stream, TEXT("sort="), SortStr);
			if (SortStr == TEXT("distance"))
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Distance);
			}
			else if (SortStr == TEXT("class"))
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Class);
			}
			else if (SortStr == TEXT("name"))
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Name);
			}
			else if (SortStr == TEXT("waves"))
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_WavesNum);
			}
			else
			{
				ShowSounds |= static_cast<uint8>(FAudioStats::EDisplayFlags::Sort_Disabled);
			}
		}
	}

	FAudioDevice* AudioDevice = GetWorldAudio(World);
	check(AudioDevice);
	FAudioStats& Stats = AudioDeviceStats.FindOrAdd(AudioDevice->DeviceHandle);
	Stats.DisplayFlags = ShowSounds;

	ResolveDesiredStats(ViewportClient);

	return true;
}

bool FAudioDebugger::ToggleStatWaves(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
{
	return ToggleStats(World, ERequestedAudioStats::SoundWaves);
}

void FAudioDebugger::SendUpdateResultsToGameThread(const FAudioDevice& AudioDevice, const int32 FirstActiveIndex)
{
	check(IsInAudioThread());

	FAudioStats_AudioThread* Stats_AudioThread = AudioDeviceStats_AudioThread.Find(AudioDevice.DeviceHandle);
	if (!Stats_AudioThread)
	{
		return;
	}

	TArray<FAudioStats::FStatSoundInfo> StatSoundInfos;
	TArray<FAudioStats::FStatSoundMix> StatSoundMixes;

	const uint8 RequestedStats = Stats_AudioThread->RequestedStats;
	TMap<FActiveSound*, int32> ActiveSoundToInfoIndex;

	const bool bDebug = (RequestedStats & ERequestedAudioStats::DebugSounds) != 0;

	static const uint8 SoundMask = ERequestedAudioStats::Sounds | ERequestedAudioStats::SoundCues | ERequestedAudioStats::SoundWaves;
	if (RequestedStats & SoundMask)
	{
		for (FActiveSound* ActiveSound : AudioDevice.GetActiveSounds())
		{
			if (USoundBase* SoundBase = ActiveSound->GetSound())
			{
				if (!bDebug || ActiveSound->GetSound()->bDebug)
				{
					ActiveSoundToInfoIndex.Add(ActiveSound, StatSoundInfos.AddDefaulted());
					FAudioStats::FStatSoundInfo& StatSoundInfo = StatSoundInfos.Last();
					StatSoundInfo.SoundName = ActiveSound->GetSound()->GetPathName();
					StatSoundInfo.Distance = AudioDevice.GetDistanceToNearestListener(ActiveSound->Transform.GetTranslation());

					if (USoundClass* SoundClass = ActiveSound->GetSoundClass())
					{
						StatSoundInfo.SoundClassName = SoundClass->GetFName();
					}
					else
					{
						StatSoundInfo.SoundClassName = NAME_None;
					}
					StatSoundInfo.Transform = ActiveSound->Transform;
					StatSoundInfo.AudioComponentID = ActiveSound->GetAudioComponentID();

					if (bDebug && ActiveSound->GetSound()->bDebug)
					{
						ActiveSound->CollectAttenuationShapesForVisualization(StatSoundInfo.ShapeDetailsMap);
					}
				}
			}
		}


		// Iterate through all wave instances.
		const TArray<FWaveInstance*>& WaveInstances = AudioDevice.GetActiveWaveInstances();
		auto WaveInstanceSourceMap = AudioDevice.GetWaveInstanceSourceMap();
		for (int32 InstanceIndex = FirstActiveIndex; InstanceIndex < WaveInstances.Num(); ++InstanceIndex)
		{
			const FWaveInstance* WaveInstance = WaveInstances[InstanceIndex];
			int32* SoundInfoIndex = ActiveSoundToInfoIndex.Find(WaveInstance->ActiveSound);
			if (SoundInfoIndex)
			{
				FAudioStats::FStatWaveInstanceInfo WaveInstanceInfo;
				FSoundSource* Source = WaveInstanceSourceMap.FindRef(WaveInstance);
				WaveInstanceInfo.Description = Source ? Source->Describe((RequestedStats & ERequestedAudioStats::LongSoundNames) != 0) : FString(TEXT("No source"));
				WaveInstanceInfo.Volume = WaveInstance->GetVolumeWithDistanceAttenuation() * WaveInstance->GetDynamicVolume();
				WaveInstanceInfo.InstanceIndex = InstanceIndex;
				WaveInstanceInfo.WaveInstanceName = *WaveInstance->GetName();
				WaveInstanceInfo.bPlayWhenSilent = WaveInstance->ActiveSound->IsPlayWhenSilent() ? 1 : 0;
				StatSoundInfos[*SoundInfoIndex].WaveInstanceInfos.Add(MoveTemp(WaveInstanceInfo));
			}
		}
	}

	if (RequestedStats & ERequestedAudioStats::SoundMixes)
	{
		if (const FAudioEffectsManager* Effects = AudioDevice.GetEffects())
		{
			const USoundMix* CurrentEQMix = Effects->GetCurrentEQMix();

			for (const TPair<USoundMix*, FSoundMixState>& SoundMixPair : AudioDevice.GetSoundMixModifiers())
			{
				StatSoundMixes.AddDefaulted();
				FAudioStats::FStatSoundMix& StatSoundMix = StatSoundMixes.Last();
				StatSoundMix.MixName = SoundMixPair.Key->GetName();
				StatSoundMix.InterpValue = SoundMixPair.Value.InterpValue;
				StatSoundMix.RefCount = SoundMixPair.Value.ActiveRefCount + SoundMixPair.Value.PassiveRefCount;
				StatSoundMix.bIsCurrentEQ = (SoundMixPair.Key == CurrentEQMix);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FGameThreadAudioTask.AudioSendResults"), STAT_AudioSendResults, STATGROUP_TaskGraphTasks);

	const uint32 AudioDeviceID = AudioDevice.DeviceHandle;

	TArray<FTransform> ListenerTransforms;
	for (const FListener& Listener : AudioDevice.GetListeners())
	{
		ListenerTransforms.Add(Listener.Transform);
	}
	FAudioThread::RunCommandOnGameThread([AudioDeviceID, ListenerTransforms, StatSoundInfos, StatSoundMixes]()
	{
		FAudioStats& Stats = AudioDeviceStats.FindOrAdd(AudioDeviceID);
		Stats.ListenerTransforms = ListenerTransforms;
		Stats.StatSoundInfos = StatSoundInfos;
		Stats.StatSoundMixes = StatSoundMixes;
	}, GET_STATID(STAT_AudioSendResults));
}

void FAudioDebugger::UpdateAudibleInactiveSounds(const uint32 FirstActiveIndex, const TArray<FWaveInstance*>& WaveInstances)
{
#if STATS
	uint32 AudibleInactiveSounds = 0;
	// Count how many sounds are not being played but were audible
	for (uint32 InstanceIndex = 0; InstanceIndex < FirstActiveIndex; ++InstanceIndex)
	{
		const FWaveInstance* WaveInstance = WaveInstances[InstanceIndex];
		const float WaveInstanceVol = WaveInstance->GetVolumeWithDistanceAttenuation() * WaveInstance->GetDynamicVolume();
		if (WaveInstanceVol > MinDisplayVolume)
		{
			AudibleInactiveSounds++;
		}
	}
	SET_DWORD_STAT(STAT_AudibleWavesDroppedDueToPriority, AudibleInactiveSounds);
#endif
}
#endif // ENABLE_AUDIO_DEBUG
