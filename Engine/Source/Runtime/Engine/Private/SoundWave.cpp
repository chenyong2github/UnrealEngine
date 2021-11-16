// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundWave.h"

#include "ActiveSound.h"
#include "Audio.h"
#include "AudioCompressionSettingsUtils.h"
#include "AudioDecompress.h"
#include "AudioDerivedData.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "SampleBuffer.h"
#include "Serialization/MemoryWriter.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundSubmix.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "EngineDefines.h"
#include "Components/AudioComponent.h"
#include "ContentStreaming.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Sound/SoundClass.h"
#include "SubtitleManager.h"
#include "DerivedDataCacheInterface.h"
#include "EditorFramework/AssetImportData.h"
#include "ProfilingDebugging/CookStats.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "DSP/SpectrumAnalyzer.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/BufferVectorOperations.h"
#include "Misc/OutputDeviceArchiveWrapper.h"
#include "Async/Async.h"
#include "Misc/CommandLine.h"

static int32 SoundWaveDefaultLoadingBehaviorCVar = 0;
FAutoConsoleVariableRef CVarSoundWaveDefaultLoadingBehavior(
	TEXT("au.streamcache.SoundWaveDefaultLoadingBehavior"),
	SoundWaveDefaultLoadingBehaviorCVar,
	TEXT("This can be set to define the default behavior when a USoundWave is loaded.\n")
	TEXT("0: Default (load on demand), 1: Retain audio data on load, 2: prime audio data on load, 3: load on demand (No audio data is loaded until a USoundWave is played or primed)."),
	ECVF_Default);

static int32 ForceNonStreamingInEditorCVar = 0;
FAutoConsoleVariableRef CVarForceNonStreamingInEditor(
	TEXT("au.editor.ForceAudioNonStreaming"),
	ForceNonStreamingInEditorCVar,
	TEXT("When set to 1, forces any audio played to be non-streaming May force a DDC miss.\n")
	TEXT("0: Honor the Play When Silent flag, 1: stop all silent non-procedural sources."),
	ECVF_Default);

static int32 DisableRetainingCVar = 0;
FAutoConsoleVariableRef CVarDisableRetaining(
	TEXT("au.streamcache.DisableRetaining"),
	DisableRetainingCVar,
	TEXT("When set to 1, USoundWaves will not retain chunks of their own audio.\n")
	TEXT("0: Don't disable retaining, 1: retaining."),
	ECVF_Default);

static int32 BlockOnChunkLoadCompletionCVar = 0;
FAutoConsoleVariableRef CVarBlockOnChunkLoadCompletion(
	TEXT("au.streamcache.BlockOnChunkLoadCompletion"),
	BlockOnChunkLoadCompletionCVar,
	TEXT("When set to 1, USoundWaves we will always attempt to synchronously load a chunk after a USoundWave request has finished.\n")
	TEXT("0: Don't try to block after a SoundWave has completed loading a chunk, 1: Block after a USoundWave's chunk request has completed."),
	ECVF_Default);

static int32 DispatchToGameThreadOnChunkRequestCVar = 1;
FAutoConsoleVariableRef CVarDispatchToGameThreadOnChunkRequest(
	TEXT("au.streamcache.DispatchToGameThreadOnChunkRequest"),
	DispatchToGameThreadOnChunkRequestCVar,
	TEXT("When set to 1, we will always dispatch a callback to the game thread whenever a USoundWave request has finished. This may cause chunks of audio to be evicted by the time we need them.\n")
	TEXT("0: as soon as the chunk is loaded, capture the audio chunk. 1: As soon as the chunk is loaded, dispatch a callback to the gamethread."),
	ECVF_Default);

#if !UE_BUILD_SHIPPING
static void DumpBakedAnalysisData(const TArray<FString>& Args)
{
	if (IsInGameThread())
	{
		if (Args.Num() == 1)
		{
			const FString& SoundWaveToDump = Args[0];
			UE_LOG(LogTemp, Log, TEXT("Foo"));
			for (TObjectIterator<USoundWave> It; It; ++It)
			{
				if (It->IsTemplate(RF_ClassDefaultObject))
				{
					continue;
				}

				if (SoundWaveToDump.Equals(It->GetName()))
				{
					UE_LOG(LogTemp, Log, TEXT("Foo"));
#if WITH_EDITOR
					It->LogBakedData();
#endif // WITH_EDITOR
				}
			}
		}
	}
}

static FAutoConsoleCommand DumpBakedAnalysisDataCmd(
	TEXT("au.DumpBakedAnalysisData"),
	TEXT("debug command to dump the baked analysis data of a sound wave to a csv file."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpBakedAnalysisData)
);
#endif

#if ENABLE_COOK_STATS
namespace SoundWaveCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("SoundWave.Usage"), TEXT(""));
	});
}
#endif

ITargetPlatform* USoundWave::GetRunningPlatform()
{
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	if (TPM)
	{
		return TPM->GetRunningTargetPlatform();
	}
	else
	{
		return nullptr;
	}
}

/*-----------------------------------------------------------------------------
	FStreamedAudioChunk
-----------------------------------------------------------------------------*/

void FStreamedAudioChunk::Serialize(FArchive& Ar, UObject* Owner, int32 ChunkIndex)
{
	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("FStreamedAudioChunk::Serialize"), STAT_StreamedAudioChunk_Serialize, STATGROUP_LoadTime );
	bool bShouldInlineAudioChunk = false;

	const ITargetPlatform* CookingTarget = Ar.CookingTarget();
	if (CookingTarget != nullptr)
	{
		const FPlatformAudioCookOverrides* Overrides = FPlatformCompressionUtilities::GetCookOverrides(*CookingTarget->IniPlatformName());
		check(Overrides);
		bShouldInlineAudioChunk = Overrides->bInlineStreamedAudioChunks;
	}

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	// ChunkIndex 0 is always inline payload, all other chunks are streamed.
	if (Ar.IsSaving())
	{
		if (ChunkIndex == 0 || (ChunkIndex == 1 && bShouldInlineAudioChunk))
		{
			BulkData.SetBulkDataFlags(BULKDATA_ForceInlinePayload);
		}
		else
		{
			BulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
		}
	}

	// streaming doesn't use memory mapped IO
	BulkData.Serialize(Ar, Owner, ChunkIndex, false);
	Ar << DataSize;
	Ar << AudioDataSize;

#if WITH_EDITORONLY_DATA
	if (!bCooked)
	{
		Ar << DerivedDataKey;
	}

	if (Ar.IsLoading() && bCooked)
	{
		bLoadedFromCookedPackage = true;
	}
#endif // #if WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
uint32 FStreamedAudioChunk::StoreInDerivedDataCache(const FString& InDerivedDataKey, const FStringView& SoundWaveName)
{
	int32 BulkDataSizeInBytes = BulkData.GetBulkDataSize();
	check(BulkDataSizeInBytes > 0);

	TArray<uint8> DerivedData;
	FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
	Ar << BulkDataSizeInBytes;
	Ar << AudioDataSize;

	{
		void* BulkChunkData = BulkData.Lock(LOCK_READ_ONLY);
		Ar.Serialize(BulkChunkData, BulkDataSizeInBytes);
		BulkData.Unlock();
	}

	const uint32 Result = DerivedData.Num();
	GetDerivedDataCacheRef().Put(*InDerivedDataKey, DerivedData, SoundWaveName);
	DerivedDataKey = InDerivedDataKey;
	BulkData.RemoveBulkData();
	return Result;
}
#endif // #if WITH_EDITORONLY_DATA

USoundWave::USoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Volume = 1.0;
	Pitch = 1.0;
	CompressionQuality = 40;
	SubtitlePriority = DEFAULT_SUBTITLE_PRIORITY;
	ResourceState = ESoundWaveResourceState::NeedsFree;
	RawPCMDataSize = 0;
	SetPrecacheState(ESoundWavePrecacheState::NotStarted);

	FrequenciesToAnalyze.Add(100.0f);
	FrequenciesToAnalyze.Add(500.0f);
	FrequenciesToAnalyze.Add(1000.0f);
	FrequenciesToAnalyze.Add(5000.0f);

#if WITH_EDITORONLY_DATA
	FFTSize = ESoundWaveFFTSize::Medium_512;
	FFTAnalysisFrameSize = 1024;
	FFTAnalysisAttackTime = 10;
	FFTAnalysisReleaseTime = 3000;
	EnvelopeFollowerFrameSize = 1024;
	EnvelopeFollowerAttackTime = 10;
	EnvelopeFollowerReleaseTime = 100;
#endif

	bCachedSampleRateFromPlatformSettings = false;
	bSampleRateManuallyReset = false;
	CachedSampleRateOverride = 0.0f;
	CachedSoundWaveLoadingBehavior = ESoundWaveLoadingBehavior::Uninitialized;

#if WITH_EDITOR
	bWasStreamCachingEnabledOnLastCook = FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching();
	bLoadedFromCookedData = false;
	RunningPlatformData = nullptr;

	OwnedBulkDataPtr = nullptr;
	ResourceData = nullptr;
#endif
}

void USoundWave::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (!GEngine)
	{
		return;
	}

	// First, add any UProperties that are on the USoundwave itself:
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(USoundWave));

	// Add all cooked spectral and envelope data:
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(FrequenciesToAnalyze.Num() * sizeof(float));
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(CookedSpectralTimeData.Num() * sizeof(FSoundWaveSpectralTimeData));

	for (FSoundWaveSpectralTimeData& Entry : CookedSpectralTimeData)
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(Entry.Data.Num() * sizeof(FSoundWaveSpectralDataEntry));
	}

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(CookedEnvelopeTimeData.Num() * sizeof(FSoundWaveEnvelopeTimeData));

	// Add zeroth chunk data, if it's used (if this USoundWave isn't streaming, this won't report).
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ZerothChunkData.GetView().Num());

	// Finally, report the actual audio memory being used, if this asset isn't using the stream cache.
	if (FAudioDevice* LocalAudioDevice = GEngine->GetMainAudioDeviceRaw())
	{
		if (LocalAudioDevice->HasCompressedAudioInfoClass(this) && DecompressionType == DTYPE_Native)
		{
			check(!RawPCMData || RawPCMDataSize);
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(RawPCMDataSize);
		}
		else
		{
			if (DecompressionType == DTYPE_RealTime && CachedRealtimeFirstBuffer)
			{
				CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MONO_PCM_BUFFER_SIZE * NumChannels);
			}

			if (!FPlatformProperties::SupportsAudioStreaming() || !IsStreaming(nullptr))
			{
				CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GetCompressedDataSize(LocalAudioDevice->GetRuntimeFormat(this)));
			}
		}
	}
}


int32 USoundWave::GetResourceSizeForFormat(FName Format)
{
	return GetCompressedDataSize(Format);
}


FName USoundWave::GetExporterName()
{
#if WITH_EDITORONLY_DATA
	if( ChannelOffsets.Num() > 0 && ChannelSizes.Num() > 0 )
	{
		return( FName( TEXT( "SoundSurroundExporterWAV" ) ) );
	}
#endif // WITH_EDITORONLY_DATA

	return( FName( TEXT( "SoundExporterWAV" ) ) );
}


FString USoundWave::GetDesc()
{
	FString Channels;

	if( NumChannels == 0 )
	{
		Channels = TEXT( "Unconverted" );
	}
#if WITH_EDITORONLY_DATA
	else if( ChannelSizes.Num() == 0 )
	{
		Channels = ( NumChannels == 1 ) ? TEXT( "Mono" ) : TEXT( "Stereo" );
	}
#endif // WITH_EDITORONLY_DATA
	else
	{
		Channels = FString::Printf( TEXT( "%d Channels" ), NumChannels );
	}

	return FString::Printf( TEXT( "%3.2fs %s" ), Duration, *Channels );
}

void USoundWave::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}
#endif
}

void USoundWave::Serialize( FArchive& Ar )
{
	LLM_SCOPE(ELLMTag::AudioSoundWaves);

	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("USoundWave::Serialize"), STAT_SoundWave_Serialize, STATGROUP_LoadTime );

	Super::Serialize( Ar );

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogAudio, Fatal, TEXT("This platform requires cooked packages, and audio data was not cooked into %s."), *GetFullName());
	}

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if (Ar.IsLoading() && (Ar.UE4Ver() >= VER_UE4_SOUND_COMPRESSION_TYPE_ADDED) && (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::RemoveSoundWaveCompressionName))
	{
		FName DummyCompressionName;
		Ar << DummyCompressionName;
	}

	bool bShouldStreamSound = false;

#if WITH_EDITORONLY_DATA
		bLoadedFromCookedData = Ar.IsLoading() && bCooked;
		if (bVirtualizeWhenSilent_DEPRECATED)
		{
			bVirtualizeWhenSilent_DEPRECATED = 0;
			VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		}
#endif // WITH_EDITORONLY_DATA

	if (Ar.IsSaving() || Ar.IsCooking())
	{
#if WITH_ENGINE
		// If there is an AutoStreamingThreshold set for the platform we're cooking to,
		// we use it to determine whether this USoundWave should be streaming:
		const ITargetPlatform* CookingTarget = Ar.CookingTarget();
		if (CookingTarget != nullptr)
		{
			bShouldStreamSound = IsStreaming(*CookingTarget->IniPlatformName());
		}
#endif
	}
	else
	{
		bShouldStreamSound = IsStreaming(nullptr);
	}

	bool bSupportsStreaming = false;
	if (Ar.IsLoading() && FPlatformProperties::SupportsAudioStreaming())
	{
		bSupportsStreaming = true;
	}
	else if (Ar.IsCooking() && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::AudioStreaming))
	{
		bSupportsStreaming = true;
	}

	if (bCooked)
	{
#if WITH_EDITOR
		// Temporary workaround for allowing editors to load data that was saved for platforms that had streaming disabled. There is nothing int
		// the serialized data that lets us know what is actually stored on disc, so we have to be explicitly told. Ideally, we'd just store something
		// on disc to say how the serialized data is arranged, but doing so would cause a major patch delta.
		static const bool bSoundWaveDataHasStreamingDisabled = FParse::Param(FCommandLine::Get(), TEXT("SoundWaveDataHasStreamingDisabled"));
		bShouldStreamSound = bShouldStreamSound && !bSoundWaveDataHasStreamingDisabled;
#endif

		// Only want to cook/load full data if we don't support streaming
		if (!bShouldStreamSound || !bSupportsStreaming)
		{
			if (Ar.IsCooking())
			{
#if WITH_ENGINE
				TArray<FName> ActualFormatsToSave;
				const ITargetPlatform* CookingTarget = Ar.CookingTarget();
				if (!CookingTarget->IsServerOnly())
				{
					// for now we only support one format per wav
					FName Format = CookingTarget->GetWaveFormat(this);
					const FPlatformAudioCookOverrides* CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides(*CookingTarget->IniPlatformName());

					GetCompressedData(Format, CompressionOverrides); // Get the data from the DDC or build it
					if (CompressionOverrides)
					{
						FString HashedString = *Format.ToString();
						FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
						FName PlatformSpecificFormat = *HashedString;
						ActualFormatsToSave.Add(PlatformSpecificFormat);
					}
					else
					{
						ActualFormatsToSave.Add(Format);
					}
				}
				bool bMapped = CookingTarget->SupportsFeature(ETargetPlatformFeatures::MemoryMappedFiles) && CookingTarget->SupportsFeature(ETargetPlatformFeatures::MemoryMappedAudio);
				CompressedFormatData.Serialize(Ar, this, &ActualFormatsToSave, true, DEFAULT_ALIGNMENT,
					!bMapped, // inline if not mapped
					bMapped);
#endif
			}
			else
			{
				if (FPlatformProperties::SupportsMemoryMappedFiles() && FPlatformProperties::SupportsMemoryMappedAudio())
				{
					CompressedFormatData.SerializeAttemptMappedLoad(Ar, this);
				}
				else
				{
					CompressedFormatData.Serialize(Ar, this);
				}
			}
		}
	}
	else
	{
		// only save the raw data for non-cooked packages
		RawData.Serialize(Ar, this, INDEX_NONE, false);
	}

	Ar << CompressedDataGuid;

	bool bBuiltStreamedAudio = false;

	if (bShouldStreamSound)
	{
		if (bCooked)
		{
			// only cook/load streaming data if it's supported
			if (bSupportsStreaming)
			{
				SerializeCookedPlatformData(Ar);
			}
		}

#if WITH_EDITORONLY_DATA
		if (Ar.IsLoading() && !Ar.IsTransacting() && !bCooked && !GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker) && FApp::CanEverRenderAudio())
		{
			CachePlatformData(false);
			bBuiltStreamedAudio = true;
		}
#endif // #if WITH_EDITORONLY_DATA
	}

	if (!(IsTemplate() || IsRunningDedicatedServer()) &&  Ar.IsLoading())
	{
		// For non-editor builds, we can immediately cache the sample rate.
		SampleRate = GetSampleRateForCurrentPlatform();

		const bool bShouldLoadChunks = bCooked || bBuiltStreamedAudio;

		// If stream caching is enabled, here we determine if we should retain or prime this wave on load.
		if (bShouldStreamSound && bShouldLoadChunks && FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching())
		{
			ESoundWaveLoadingBehavior CurrentLoadingBehavior = GetLoadingBehavior(false);

			//EnsureZerothChunkIsLoaded();
			const bool bHasFirstChunk = GetNumChunks() > 1;
			
			if (!bHasFirstChunk)
			{
				return;
			}

			if (!GIsEditor && CurrentLoadingBehavior == ESoundWaveLoadingBehavior::RetainOnLoad)
			{
				RetainCompressedAudio(true);
			}
			else if ((CurrentLoadingBehavior == ESoundWaveLoadingBehavior::PrimeOnLoad)
				|| (GIsEditor && CurrentLoadingBehavior == ESoundWaveLoadingBehavior::RetainOnLoad))
			{
				// Prime first chunk of audio.
				CachedSoundWaveLoadingBehavior = ESoundWaveLoadingBehavior::PrimeOnLoad;
				bLoadingBehaviorOverridden = true;
				IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(this, 1, [](EAudioChunkLoadResult) {});
			}
		}
	}
}

float USoundWave::GetSubtitlePriority() const
{
	return SubtitlePriority;
};

bool USoundWave::SupportsSubtitles() const
{
	return VirtualizationMode == EVirtualizationMode::PlayWhenSilent || (Subtitles.Num() > 0);
}

void USoundWave::PostInitProperties()
{
	Super::PostInitProperties();

	if(!IsTemplate())
	{
		// Don't rebuild our streaming chunks yet because we may not have loaded the RawPCMData at this point.
		InvalidateCompressedData(false, false);
	}

#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}

bool USoundWave::HasCompressedData(FName Format, ITargetPlatform* TargetPlatform) const
{
	if (IsTemplate() || IsRunningDedicatedServer())
	{
		return false;
	}

	const FPlatformAudioCookOverrides* CompressionOverrides = nullptr;

	if (GIsEditor)
	{
		if (TargetPlatform)
		{
			CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides(*TargetPlatform->IniPlatformName());
		}
	}
	else
	{
		// TargetPlatform is not available on consoles/mobile, so we have to grab it ourselves:
		CompressionOverrides = FPlatformCompressionUtilities::GetCookOverrides();
	}

	if (CompressionOverrides)
	{
#if WITH_EDITOR
		FName PlatformSpecificFormat;
		FString HashedString = *Format.ToString();
		FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
		PlatformSpecificFormat = *HashedString;
#else
		// on non-editor builds, we cache the concatenated format in a static FName.
		static FName PlatformSpecificFormat;
		static FName CachedFormat;
		if (!Format.IsEqual(CachedFormat))
		{
			FString HashedString = *Format.ToString();
			FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
			PlatformSpecificFormat = *HashedString;

			CachedFormat = Format;
		}
#endif // WITH_EDITOR
		return CompressedFormatData.Contains(PlatformSpecificFormat);
	}
	else
	{
		return CompressedFormatData.Contains(Format);
	}

}

const FPlatformAudioCookOverrides* USoundWave::GetPlatformCompressionOverridesForCurrentPlatform()
{
	return FPlatformCompressionUtilities::GetCookOverrides();
}

namespace SoundWavePrivate
{
	struct FBulkDataReadScopeLock
	{
		FBulkDataReadScopeLock(const FUntypedBulkData& InBulkData)
		:	BulkData(InBulkData)
		{
			RawPtr = BulkData.LockReadOnly();
		}

		template<typename T>
		const T* GetData() const
		{
			return static_cast<const T*>(RawPtr);
		}
			
		~FBulkDataReadScopeLock()
		{
			if (BulkData.IsLocked())
			{
				BulkData.Unlock();
			}
		}

		private:
			const FUntypedBulkData& BulkData;
			const void* RawPtr = nullptr;
	};
}

#if WITH_EDITOR
bool USoundWave::GetImportedSoundWaveData(TArray<uint8>& OutRawPCMData, uint32& OutSampleRate, uint16& OutNumChannels) const
{
	TArray<EAudioSpeakers> ChannelOrder;

	bool bResult = GetImportedSoundWaveData(OutRawPCMData, OutSampleRate, ChannelOrder);
	
	if (bResult)
	{
		OutNumChannels = ChannelOrder.Num();
	}
	else
	{
		OutNumChannels = 0;
	}

	return bResult;
}

bool USoundWave::GetImportedSoundWaveData(TArray<uint8>& OutRawPCMData, uint32& OutSampleRate, TArray<EAudioSpeakers>& OutChannelOrder) const
{
	using namespace SoundWavePrivate;

	OutRawPCMData.Reset();
	OutSampleRate = 0;
	OutChannelOrder.Reset();

	// Can only get sound wave data if there is bulk data 
	if (RawData.GetBulkDataSize() > 0)
	{
		FBulkDataReadScopeLock LockedBulkData(RawData);
		const uint8* Data = LockedBulkData.GetData<uint8>();
		int32 DataSize = RawData.GetBulkDataSize();

		if (NumChannels > 2)
		{
			static const EAudioSpeakers DefaultChannelOrder[SPEAKER_Count] = 
			{
				SPEAKER_FrontLeft,
				SPEAKER_FrontRight,
				SPEAKER_FrontCenter,
				SPEAKER_LowFrequency,
				SPEAKER_LeftSurround,
				SPEAKER_RightSurround,
				SPEAKER_LeftBack,
				SPEAKER_RightBack
			};

			check(ChannelOffsets.Num() == ChannelSizes.Num());
			check(ChannelOffsets.Num() == SPEAKER_Count);

			// Multichannel audio with more than 2 channels must be accessed by
			// inspecting the channel offsets and channel sizes of the USoundWave.

			bool bIsOutputInitialized = false;

			int32 NumFrames = 0;
			int32 NumSamples = 0;
			OutSampleRate = 0;


			// Determine which channels have data and Interleave channel data
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelOffsets.Num(); ChannelIndex++)
			{
				if (ChannelSizes[ChannelIndex] <= 0)
				{
					continue;
				}

				FWaveModInfo WaveInfo;

				// parse the wave data for a single channel
				if (!WaveInfo.ReadWaveHeader(Data, ChannelSizes[ChannelIndex], ChannelOffsets[ChannelIndex]))
				{
					UE_LOG(LogAudio, Warning, TEXT("Failed to read wave data: %s."), *GetFullName());
					return false;
				}

				// Check for valid channel count
				if (1 != *WaveInfo.pChannels)
				{
					UE_LOG(LogAudio, Warning, TEXT("Cannot audio handle format. Expected single channel audio but read %d channels"), *WaveInfo.pChannels);
					return false;
				}

				// Check for valid bit depth
				if (16 != *WaveInfo.pBitsPerSample)
				{
					UE_LOG(LogAudio, Warning, TEXT("Cannot audio handle format. Expected 16bit audio but found %d bit audio"), *WaveInfo.pBitsPerSample);
					return false;
				}

				// Set output channel type
				OutChannelOrder.Add(DefaultChannelOrder[ChannelIndex]);

				// The output info needs to be initialized from the first channel's wave info.
				if (!bIsOutputInitialized)
				{
					OutSampleRate = *WaveInfo.pSamplesPerSec;

					NumFrames = WaveInfo.SampleDataSize / sizeof(int16);
					NumSamples = NumFrames * NumChannels;

					if (NumSamples > 0)
					{
						// Translate NumSamples to bytes because OutRawPCMData is in bytes. 
						const int32 NumBytes = NumSamples * sizeof(int16);
						OutRawPCMData.AddZeroed(NumBytes);
					}
					
					bIsOutputInitialized = true;
				}


				check(OutSampleRate == *WaveInfo.pSamplesPerSec);
				const int32 ThisChannelNumFrames = WaveInfo.SampleDataSize / sizeof(int16);

				if (ensureMsgf(ThisChannelNumFrames == NumFrames, TEXT("Audio channels contain varying number of frames (%d vs %d)"), NumFrames, ThisChannelNumFrames))
				{
					TArrayView<int16> OutPCM(reinterpret_cast<int16*>(OutRawPCMData.GetData()), NumSamples);
					TArrayView<const int16> ChannelArrayView(reinterpret_cast<const int16*>(WaveInfo.SampleDataStart), NumFrames);

					int32 DestSamplePos = OutChannelOrder.Num() - 1;
					int32 SourceSamplePos = 0;

					while (DestSamplePos < NumSamples)
					{
						OutPCM[DestSamplePos] = ChannelArrayView[SourceSamplePos];

						SourceSamplePos++;
						DestSamplePos += NumChannels;
					}
				}
				else
				{
					return false;
				}
			}
		}
		else
		{
			FWaveModInfo WaveInfo;

			// parse the wave data
			if (!WaveInfo.ReadWaveHeader(Data, DataSize, 0))
			{
				UE_LOG(LogAudio, Warning, TEXT("Only mono or stereo 16 bit waves allowed: %s."), *GetFullName());
				return false;
			}

			// Copy the raw PCM data and the header info that was parsed
			OutRawPCMData.AddUninitialized(WaveInfo.SampleDataSize);
			FMemory::Memcpy(OutRawPCMData.GetData(), WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);

			OutSampleRate = *WaveInfo.pSamplesPerSec;
			
			if (1 == *WaveInfo.pChannels)
			{
				OutChannelOrder.Add(SPEAKER_FrontLeft);
			}
			else if (2 == *WaveInfo.pChannels)
			{
				OutChannelOrder.Append({SPEAKER_FrontLeft, SPEAKER_FrontRight});
			}
		}

		return true;
	}

	UE_LOG(LogAudio, Warning, TEXT("Failed to get imported raw data for sound wave '%s'"), *GetFullName());
	return false;
}
#endif

FName USoundWave::GetPlatformSpecificFormat(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides)
{
	// Platforms that require compression overrides get concatenated formats.
#if WITH_EDITOR
	FName PlatformSpecificFormat;
	if (CompressionOverrides)
	{
		FString HashedString = *Format.ToString();
		FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
		PlatformSpecificFormat = *HashedString;
	}
	else
	{
		PlatformSpecificFormat = Format;
	}
#else
	if (CompressionOverrides == nullptr)
	{
		CompressionOverrides = GetPlatformCompressionOverridesForCurrentPlatform();
	}

	// Cache the concatenated hash:
	static FName PlatformSpecificFormat;
	static FName CachedFormat;
	if (!Format.IsEqual(CachedFormat))
	{
		if (CompressionOverrides)
		{
			FString HashedString = *Format.ToString();
			FPlatformAudioCookOverrides::GetHashSuffix(CompressionOverrides, HashedString);
			PlatformSpecificFormat = *HashedString;
		}
		else
		{
			PlatformSpecificFormat = Format;
		}

		CachedFormat = Format;
	}

#endif // WITH_EDITOR

	return PlatformSpecificFormat;
}

void USoundWave::BeginGetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides)
{
#if WITH_EDITOR
	if (IsTemplate() || IsRunningDedicatedServer())
	{
		return;
	}

	// If stream caching has been enabled or disabled since the previous DDC operation, we need to invalidate the current
	InvalidateSoundWaveIfNeccessary();

	FName PlatformSpecificFormat = GetPlatformSpecificFormat(Format, CompressionOverrides);

	if (!CompressedFormatData.Contains(PlatformSpecificFormat) && !AsyncLoadingDataFormats.Contains(PlatformSpecificFormat))
	{
		if (GetDerivedDataCache())
		{
			COOK_STAT(auto Timer = SoundWaveCookStats::UsageStats.TimeSyncWork());
			COOK_STAT(Timer.TrackCyclesOnly());
			FDerivedAudioDataCompressor* DeriveAudioData = new FDerivedAudioDataCompressor(this, Format, PlatformSpecificFormat, CompressionOverrides);
			uint32 GetHandle = GetDerivedDataCacheRef().GetAsynchronous(DeriveAudioData);
			AsyncLoadingDataFormats.Add(PlatformSpecificFormat, GetHandle);
		}
		else
		{
			UE_LOG(LogAudio, Error, TEXT("Attempt to access the DDC when there is none available on sound '%s', format = %s."), *GetFullName(), *PlatformSpecificFormat.ToString());
		}
	}
#else
	// No async DDC read in non-editor, nothing to precache
#endif
}

FByteBulkData* USoundWave::GetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides)
{
	if (IsTemplate() || IsRunningDedicatedServer())
	{
		return nullptr;
	}

	FName PlatformSpecificFormat = GetPlatformSpecificFormat(Format, CompressionOverrides);

	bool bContainedValidData = CompressedFormatData.Contains(PlatformSpecificFormat);
	FByteBulkData* Result = &CompressedFormatData.GetFormat(PlatformSpecificFormat);
	if (!bContainedValidData)
	{
		if (!FPlatformProperties::RequiresCookedData() && GetDerivedDataCache())
		{
			TArray<uint8> OutData;
			bool bDataWasBuilt = false;
			bool bGetSuccessful = false;

#if WITH_EDITOR
			uint32* AsyncHandle = AsyncLoadingDataFormats.Find(PlatformSpecificFormat);
#else
			uint32* AsyncHandle = nullptr;
#endif

			COOK_STAT(auto Timer = AsyncHandle ? SoundWaveCookStats::UsageStats.TimeAsyncWait() : SoundWaveCookStats::UsageStats.TimeSyncWork());

#if WITH_EDITOR
			if (AsyncHandle)
			{
				GetDerivedDataCacheRef().WaitAsynchronousCompletion(*AsyncHandle);
				bGetSuccessful = GetDerivedDataCacheRef().GetAsynchronousResults(*AsyncHandle, OutData, &bDataWasBuilt);
				AsyncLoadingDataFormats.Remove(PlatformSpecificFormat);
			}
			else
#endif
			{
				FDerivedAudioDataCompressor* DeriveAudioData = new FDerivedAudioDataCompressor(this, Format, PlatformSpecificFormat, CompressionOverrides);
				bGetSuccessful = GetDerivedDataCacheRef().GetSynchronous(DeriveAudioData, OutData, &bDataWasBuilt);
			}

			if (bGetSuccessful)
			{
				COOK_STAT(Timer.AddHitOrMiss(bDataWasBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, OutData.Num()));
				Result->Lock(LOCK_READ_WRITE);
				FMemory::Memcpy(Result->Realloc(OutData.Num()), OutData.GetData(), OutData.Num());
				Result->Unlock();
			}
		}
		else
		{
			UE_LOG(LogAudio, Error, TEXT("Attempt to access the DDC when there is none available on sound '%s', format = %s. Should have been cooked."), *GetFullName(), *PlatformSpecificFormat.ToString());
		}
	}
	check(Result);
	return Result->GetBulkDataSize() > 0 ? Result : NULL; // we don't return empty bulk data...but we save it to avoid thrashing the DDC
}

void USoundWave::InvalidateCompressedData(bool bFreeResources, bool bRebuildStreamingChunks)
{
	CompressedDataGuid = FGuid::NewGuid();
	ZerothChunkData.Empty();
	CompressedFormatData.FlushData();

	if (bFreeResources)
	{
		FreeResources(false);
	}

#if WITH_EDITOR
	if (bRebuildStreamingChunks)
	{
		CachePlatformData();
		CurrentChunkRevision.Increment();
	}


	// If this sound wave is retained, release and re-retain the new chunk.
	if (FirstChunk.IsValid())
	{
		ReleaseCompressedAudio();
		RetainCompressedAudio(true);
	}
#endif
}

bool USoundWave::HasStreamingChunks()
{
	return RunningPlatformData != NULL && RunningPlatformData->Chunks.Num() > 0;
}

void USoundWave::PostLoad()
{
	LLM_SCOPE(ELLMTag::AudioSoundWaves);

	Super::PostLoad();

	if (GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker))
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	// Log a warning after loading if the source has effect chains but has channels greater than 2.
	if (SourceEffectChain && SourceEffectChain->Chain.Num() > 0 && NumChannels > 2)
	{
		UE_LOG(LogAudio, Warning, TEXT("Sound Wave '%s' has defined an effect chain but is not mono or stereo."), *GetName());
	}
#endif

	// Don't need to do anything in post load if this is a source bus
	if (this->IsA(USoundSourceBus::StaticClass()))
	{
		return;
	}

	CacheInheritedLoadingBehavior();

	
	// If our loading behavior is defined by a sound class, we need to update whether this sound wave actually needs to retain its audio data or not.
	ESoundWaveLoadingBehavior ActualLoadingBehavior = GetLoadingBehavior();

	if (ShouldUseStreamCaching() && ActualLoadingBehavior != GetLoadingBehavior(false))
	{
		if (!GIsEditor && !DisableRetainingCVar && ActualLoadingBehavior == ESoundWaveLoadingBehavior::RetainOnLoad)
		{
			UE_LOG(LogAudio, Display, TEXT("Sound Wave '%s' will have to load its compressed audio data async."), *GetName());
			RetainCompressedAudio(false);
		}
		else
		{
			// if a sound class defined our loading behavior as something other than Retain and our cvar default is to retain, we need to release our handle.
			ReleaseCompressedAudio();

			const bool bHasMultipleChunks = GetNumChunks() > 1;
			bool bShouldPrime = (ActualLoadingBehavior == ESoundWaveLoadingBehavior::PrimeOnLoad);
			bShouldPrime |= (GIsEditor && (ActualLoadingBehavior == ESoundWaveLoadingBehavior::RetainOnLoad)); // treat this scenario like PrimeOnLoad
			
			if (bShouldPrime && bHasMultipleChunks)
			{
				IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(this, 1, [](EAudioChunkLoadResult) {});
			}
		}

		// If DisableRetainingCVar was set after this USoundWave was loaded by the ALT, release its compressed audio here.
		if (DisableRetainingCVar)
		{
			ReleaseCompressedAudio();
		}

		if (!GIsEditor)
		{
			// In case any code accesses bStreaming directly, we fix up bStreaming based on the current platform's cook overrides.
			bStreaming = IsStreaming(nullptr);
		}
	}

	// Compress to whatever formats the active target platforms want
	// static here as an optimization
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
#if WITH_EDITORONLY_DATA
	const bool bShouldLoadCompressedData = !(bLoadedFromCookedData && IsRunningCommandlet());
#else
	const bool bShouldLoadCompressedData = true;
#endif
	if (TPM && bShouldLoadCompressedData)
	{
		const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

		for (const ITargetPlatform* Platform : Platforms)
		{
			if (!Platform->IsServerOnly())
			{
				BeginGetCompressedData(Platform->GetWaveFormat(this), FPlatformCompressionUtilities::GetCookOverrides(*Platform->IniPlatformName()));
			}
		}
	}

	// We don't precache default objects and we don't precache in the Editor as the latter will
	// most likely cause us to run out of memory.
	if (!GIsEditor && !IsTemplate(RF_ClassDefaultObject) && GEngine)
	{
		FAudioDevice* AudioDevice = GEngine->GetMainAudioDeviceRaw();
		if (AudioDevice)
		{
			// Upload the data to the hardware, but only if we've precached startup sounds already
			AudioDevice->Precache(this);
		}
		// remove bulk data if no AudioDevice is used and no sounds were initialized
		else if (IsRunningGame())
		{
			RawData.RemoveBulkData();
		}
	}

	// Only add this streaming sound if the platform supports streaming
	if (FApp::CanEverRenderAudio() && IsStreaming(nullptr) && FPlatformProperties::SupportsAudioStreaming())
	{
#if WITH_EDITORONLY_DATA
		FinishCachePlatformData();
#endif // #if WITH_EDITORONLY_DATA
		IStreamingManager::Get().GetAudioStreamingManager().AddStreamingSoundWave(this);
	}

	const bool bDoesSoundWaveHaveStreamingAudioData = HasStreamingChunks();

	if (ShouldUseStreamCaching() && bDoesSoundWaveHaveStreamingAudioData)
	{
		EnsureZerothChunkIsLoaded();
	}

#if WITH_EDITORONLY_DATA
	if (!SourceFilePath_DEPRECATED.IsEmpty() && AssetImportData)
	{
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
	}

	bNeedsThumbnailGeneration = true;
#endif // #if WITH_EDITORONLY_DATA

	INC_FLOAT_STAT_BY(STAT_AudioBufferTime, Duration);
	INC_FLOAT_STAT_BY(STAT_AudioBufferTimeChannels, NumChannels * Duration);
}

void USoundWave::EnsureZerothChunkIsLoaded()
{
	// If the zeroth chunk is already loaded, early exit.
	if (ZerothChunkData.GetView().Num() > 0 || !ShouldUseStreamCaching())
	{
		return;
	}

#if WITH_EDITOR 
	if (RunningPlatformData == nullptr)
	{
		CachePlatformData(false);
	}

	// If we're running the editor, we'll need to retrieve the chunked audio from the DDC:
	uint8* TempChunkBuffer = nullptr;
	int32 ChunkSizeInBytes = RunningPlatformData->GetChunkFromDDC(0, &TempChunkBuffer, true);
	// Since we block for the DDC in the previous call we should always have the chunk loaded.
	if (ChunkSizeInBytes == 0)
	{
		return;
	}

	ZerothChunkData.Reset(TempChunkBuffer, ChunkSizeInBytes);

#else // WITH_EDITOR
	// Otherwise, the zeroth chunk is cooked out to RunningPlatformData, and we just need to retrieve it.
	check(RunningPlatformData && RunningPlatformData->Chunks.Num() > 0);
	FStreamedAudioChunk& ZerothChunk = RunningPlatformData->Chunks[0];
	// Some sanity checks to ensure that the bulk size set up
	UE_CLOG(ZerothChunk.BulkData.GetBulkDataSize() != ZerothChunk.DataSize, LogAudio, Warning, TEXT("Bulk data serialized out had a mismatched size with the DataSize field. Soundwave: %s Bulk Data Reported Size: %d Bulk Data Actual Size: %ld"), *GetFullName(), ZerothChunk.DataSize, ZerothChunk.BulkData.GetBulkDataSize());

	ZerothChunkData = ZerothChunk.BulkData.GetCopyAsBuffer(ZerothChunk.AudioDataSize, true);
#endif // WITH_EDITOR
}

uint32 USoundWave::GetNumChunks() const
{
	if (RunningPlatformData)
	{
		return RunningPlatformData->Chunks.Num();
	}
	else if (IsTemplate() || IsRunningDedicatedServer())
	{
		return 0;
	}
	else if (GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker) || !FApp::CanEverRenderAudio())
	{
		UE_LOG(LogAudio, Warning, TEXT("USoundWave::GetNumChunks called in the cook commandlet."))
		return 0;
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Call CachePlatformData(false) before calling this function in editor. GetNumChunks() called on: %s"), *GetName());
		return 0;
	}
}

uint32 USoundWave::GetSizeOfChunk(uint32 ChunkIndex)
{
	check(ChunkIndex < GetNumChunks());

	if (RunningPlatformData)
	{
		return RunningPlatformData->Chunks[ChunkIndex].AudioDataSize;
	}
	else
	{
		return 0;
	}
}

void USoundWave::BeginDestroy()
{
	Super::BeginDestroy();

	{
		FScopeLock Lock(&SourcesPlayingCs);
		int32 CurrNumSourcesPlaying = SourcesPlaying.Num();

		for (int32 i = CurrNumSourcesPlaying - 1; i >= 0; --i)
		{
			ISoundWaveClient* SoundWaveClientPtr = SourcesPlaying[i];

			if (SoundWaveClientPtr && SoundWaveClientPtr->OnBeginDestroy(this))
			{
				// if OnBeginDestroy returned true, we are unsubscribing the SoundWaveClient...
				SourcesPlaying.RemoveAtSwap(i, 1, false);
			}
		}
	}

#if WITH_EDITOR
	// Flush any async results so we dont leak them in the DDC
	if (GetDerivedDataCache() && AsyncLoadingDataFormats.Num() > 0)
	{
		TArray<uint8> OutData;
		for (auto AsyncLoadIt = AsyncLoadingDataFormats.CreateConstIterator(); AsyncLoadIt; ++AsyncLoadIt)
		{
			uint32 AsyncHandle = AsyncLoadIt.Value();
			GetDerivedDataCacheRef().WaitAsynchronousCompletion(AsyncHandle);
			GetDerivedDataCacheRef().GetAsynchronousResults(AsyncHandle, OutData);
		}

		AsyncLoadingDataFormats.Empty();
	}
#endif

	ReleaseCompressedAudio();
}

void USoundWave::InitAudioResource(FByteBulkData& CompressedData)
{
	if (!ResourceSize)
	{
		// Grab the compressed vorbis data from the bulk data
		ResourceSize = CompressedData.GetBulkDataSize();
		if (ResourceSize > 0)
		{
#if WITH_EDITOR
			check(!ResourceData);
			CompressedData.GetCopy((void**)&ResourceData, true);
#else
			if (!OwnedBulkDataPtr)
			{
				OwnedBulkDataPtr = CompressedData.StealFileMapping();
			}
			else
			{
				UE_LOG(LogAudio, Display, TEXT("Soundwave '%s' Has already had InitAudioResource() called, and taken ownership of it's compressed data.")
					, *GetFullName());
			}

			ResourceData = (const uint8*)OwnedBulkDataPtr->GetPointer();
			if (!ResourceData)
			{
				UE_LOG(LogAudio, Error, TEXT("Soundwave '%s' was not loaded when it should have been, forcing a sync load."), *GetFullName());

				delete OwnedBulkDataPtr;
				CompressedData.ForceBulkDataResident();
				OwnedBulkDataPtr = CompressedData.StealFileMapping();
				ResourceData = (const uint8*)OwnedBulkDataPtr->GetPointer();
				if (!ResourceData)
				{
					UE_LOG(LogAudio, Fatal, TEXT("Soundwave '%s' failed to load even after forcing a sync load."), *GetFullName());
				}
			}
#endif
		}
	}
}

bool USoundWave::InitAudioResource(FName Format)
{
	if (!ResourceSize && (!FPlatformProperties::SupportsAudioStreaming() || !IsStreaming(nullptr)))
	{
		FByteBulkData* Bulk = GetCompressedData(Format, GetPlatformCompressionOverridesForCurrentPlatform());
		if (Bulk)
		{
#if WITH_EDITOR
			ResourceSize = Bulk->GetBulkDataSize();
			check(ResourceSize > 0);
			check(!ResourceData);
			Bulk->GetCopy((void**)&ResourceData, true);
#else
			InitAudioResource(*Bulk);
			check(ResourceSize > 0);
#endif
		}
	}

	return ResourceSize > 0;
}

void USoundWave::RemoveAudioResource()
{
#if WITH_EDITOR
	if (ResourceData)
	{
		FMemory::Free((void*)ResourceData);
		ResourceSize = 0;
		ResourceData = NULL;
	}
#else
	delete OwnedBulkDataPtr;
	OwnedBulkDataPtr = nullptr;
	ResourceData = nullptr;
	ResourceSize = 0;
#endif
}



#if WITH_EDITOR

void USoundWave::InvalidateSoundWaveIfNeccessary()
{
	if (bProcedural)
	{
		return;
	}

	// if stream caching was enabled since the last time we invalidated the compressed audio, force a re-cook.
	const bool bIsStreamCachingEnabled = FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching();
	if (bWasStreamCachingEnabledOnLastCook != bIsStreamCachingEnabled)
	{
		InvalidateCompressedData(true);
		bWasStreamCachingEnabledOnLastCook = bIsStreamCachingEnabled;

		// If stream caching is now turned on, recook the streaming audio if neccessary.
		if (bIsStreamCachingEnabled && IsStreaming(nullptr))
		{
			EnsureZerothChunkIsLoaded();
		}
	}
}

float USoundWave::GetSampleRateForTargetPlatform(const ITargetPlatform* TargetPlatform)
{
	const FPlatformAudioCookOverrides* Overrides = FPlatformCompressionUtilities::GetCookOverrides(*TargetPlatform->IniPlatformName());
	if (Overrides)
	{
		return GetSampleRateForCompressionOverrides(Overrides);
	}
	else
	{
		return -1.0f;
	}
}

void USoundWave::LogBakedData()
{
	const FString AnalysisPathName = *(FPaths::ProjectLogDir() + TEXT("BakedAudioAnalysisData/"));
	IFileManager::Get().MakeDirectory(*AnalysisPathName);

	FString SoundWaveName = FString::Printf(TEXT("%s.%s"), *FDateTime::Now().ToString(TEXT("%d-%H.%M.%S")), *GetName());

	if (CookedEnvelopeTimeData.Num())
	{
		FString EnvelopeFileName = FString::Printf(TEXT("%s.envelope.csv"), *SoundWaveName);
		FString FilenameFull = AnalysisPathName + EnvelopeFileName;

		FArchive* FileAr = IFileManager::Get().CreateDebugFileWriter(*FilenameFull);
		FOutputDeviceArchiveWrapper* FileArWrapper = new FOutputDeviceArchiveWrapper(FileAr);
		FOutputDevice* ReportAr = FileArWrapper;

		ReportAr->Log(TEXT("TimeStamp (Sec),Amplitude"));

		for (const FSoundWaveEnvelopeTimeData& EnvTimeData : CookedEnvelopeTimeData)
		{
			ReportAr->Logf(TEXT("%.4f,%.4f"), EnvTimeData.TimeSec, EnvTimeData.Amplitude);
		}

		// Shutdown and free archive resources
		FileArWrapper->TearDown();
		delete FileArWrapper;
		delete FileAr;
	}

	if (CookedSpectralTimeData.Num())
	{
		FString AnalysisFileName = FString::Printf(TEXT("%s.spectral.csv"), *SoundWaveName);
		FString FilenameFull = AnalysisPathName + AnalysisFileName;

		FArchive* FileAr = IFileManager::Get().CreateDebugFileWriter(*FilenameFull);
		FOutputDeviceArchiveWrapper* FileArWrapper = new FOutputDeviceArchiveWrapper(FileAr);
		FOutputDevice* ReportAr = FileArWrapper;

		// Build the header string
		FString ScratchString;
		ScratchString.Append(TEXT("Time Stamp (Sec),"));

		for (int32 i = 0; i < FrequenciesToAnalyze.Num(); ++i)
		{
			ScratchString.Append(FString::Printf(TEXT("%.2f Hz"), FrequenciesToAnalyze[i]));
			if (i != FrequenciesToAnalyze.Num() - 1)
			{
				ScratchString.Append(TEXT(","));
			}
		}

		ReportAr->Log(ScratchString);

		for (const FSoundWaveSpectralTimeData& SpectralTimeData : CookedSpectralTimeData)
		{
			ScratchString.Reset();
			ScratchString.Append(FString::Printf(TEXT("%.4f,"), SpectralTimeData.TimeSec));

			for (int32 i = 0; i < SpectralTimeData.Data.Num(); ++i)
			{
				ScratchString.Append(FString::Printf(TEXT("%.4f"), SpectralTimeData.Data[i].Magnitude));
				if (i != SpectralTimeData.Data.Num() - 1)
				{
					ScratchString.Append(TEXT(","));
				}
			}

			ReportAr->Log(*ScratchString);
		}

		// Shutdown and free archive resources
		FileArWrapper->TearDown();
		delete FileArWrapper;
		delete FileAr;
	}

}

static bool AnyFFTAnalysisPropertiesChanged(const FName& PropertyName)
{
	// List of properties which cause analysis to get triggered
	static FName OverrideSoundName = GET_MEMBER_NAME_CHECKED(USoundWave, OverrideSoundToUseForAnalysis);
	static FName EnableFFTAnalysisFName = GET_MEMBER_NAME_CHECKED(USoundWave, bEnableBakedFFTAnalysis);
	static FName FFTSizeFName = GET_MEMBER_NAME_CHECKED(USoundWave, FFTSize);
	static FName FFTAnalysisFrameSizeFName = GET_MEMBER_NAME_CHECKED(USoundWave, FFTAnalysisFrameSize);
	static FName FrequenciesToAnalyzeFName = GET_MEMBER_NAME_CHECKED(USoundWave, FrequenciesToAnalyze);
	static FName FFTAnalysisAttackTimeFName = GET_MEMBER_NAME_CHECKED(USoundWave, FFTAnalysisAttackTime);
	static FName FFTAnalysisReleaseTimeFName = GET_MEMBER_NAME_CHECKED(USoundWave, FFTAnalysisReleaseTime);

	return	PropertyName == OverrideSoundName ||
		PropertyName == EnableFFTAnalysisFName ||
		PropertyName == FFTSizeFName ||
		PropertyName == FFTAnalysisFrameSizeFName ||
		PropertyName == FrequenciesToAnalyzeFName ||
		PropertyName == FFTAnalysisAttackTimeFName ||
		PropertyName == FFTAnalysisReleaseTimeFName;
}

static bool AnyEnvelopeAnalysisPropertiesChanged(const FName& PropertyName)
{
	// List of properties which cause re-analysis to get triggered
	static FName OverrideSoundName = GET_MEMBER_NAME_CHECKED(USoundWave, OverrideSoundToUseForAnalysis);
	static FName EnableAmplitudeEnvelopeAnalysisFName = GET_MEMBER_NAME_CHECKED(USoundWave, bEnableAmplitudeEnvelopeAnalysis);
	static FName EnvelopeFollowerFrameSizeFName = GET_MEMBER_NAME_CHECKED(USoundWave, EnvelopeFollowerFrameSize);
	static FName EnvelopeFollowerAttackTimeFName = GET_MEMBER_NAME_CHECKED(USoundWave, EnvelopeFollowerAttackTime);
	static FName EnvelopeFollowerReleaseTimeFName = GET_MEMBER_NAME_CHECKED(USoundWave, EnvelopeFollowerReleaseTime);

	return	PropertyName == OverrideSoundName ||
		PropertyName == EnableAmplitudeEnvelopeAnalysisFName ||
		PropertyName == EnvelopeFollowerFrameSizeFName ||
		PropertyName == EnvelopeFollowerAttackTimeFName ||
		PropertyName == EnvelopeFollowerReleaseTimeFName;

}

void USoundWave::BakeFFTAnalysis()
{
	// Clear any existing spectral data regardless of if it's enabled. If this was enabled and is now toggled, this will clear previous data.
	CookedSpectralTimeData.Reset();

	// Perform analysis if enabled on the sound wave
	if (bEnableBakedFFTAnalysis)
	{
		// If there are no frequencies to analyze, we can't do the analysis
		if (!FrequenciesToAnalyze.Num())
		{
			UE_LOG(LogAudio, Warning, TEXT("Soundwave '%s' had baked FFT analysis enabled without specifying any frequencies to analyze."), *GetFullName());
			return;
		}

		if (ChannelSizes.Num() > 0)
		{
			UE_LOG(LogAudio, Warning, TEXT("Soundwave '%s' has multi-channel audio (channels greater than 2). Baking FFT analysis is not currently supported for this yet."), *GetFullName());
			return;
		}

		// Retrieve the raw imported data
		TArray<uint8> RawImportedWaveData;
		uint32 RawDataSampleRate = 0;
		uint16 RawDataNumChannels = 0;

		USoundWave* SoundWaveToUseForAnalysis = this;
		if (OverrideSoundToUseForAnalysis)
		{
			SoundWaveToUseForAnalysis = OverrideSoundToUseForAnalysis;
		}

		if (!SoundWaveToUseForAnalysis->GetImportedSoundWaveData(RawImportedWaveData, RawDataSampleRate, RawDataNumChannels))
		{
			return;
		}

		if (RawDataSampleRate == 0 || RawDataNumChannels == 0)
		{
			UE_LOG(LogAudio, Error, TEXT("Failed to parse the raw imported data for '%s' for baked FFT analysis."), *GetFullName());
			return;
		}

		const uint32 NumFrames = (RawImportedWaveData.Num() / sizeof(int16)) / RawDataNumChannels;
		int16* InputData = (int16*)RawImportedWaveData.GetData();

		Audio::FSpectrumAnalyzerSettings SpectrumAnalyzerSettings;
		switch (FFTSize)
		{
		case ESoundWaveFFTSize::VerySmall_64:
			SpectrumAnalyzerSettings.FFTSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Min_64;
			break;

		case ESoundWaveFFTSize::Small_256:
			SpectrumAnalyzerSettings.FFTSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Small_256;
			break;

		default:
		case ESoundWaveFFTSize::Medium_512:
			SpectrumAnalyzerSettings.FFTSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Medium_512;
			break;

		case ESoundWaveFFTSize::Large_1024:
			SpectrumAnalyzerSettings.FFTSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::Large_1024;
			break;

		case ESoundWaveFFTSize::VeryLarge_2048:
			SpectrumAnalyzerSettings.FFTSize = Audio::FSpectrumAnalyzerSettings::EFFTSize::VeryLarge_2048;
			break;

		}

		// Prepare the spectral envelope followers
		TArray<Audio::FEnvelopeFollower> SpectralEnvelopeFollowers;
		SpectralEnvelopeFollowers.AddDefaulted(FrequenciesToAnalyze.Num());

		for (Audio::FEnvelopeFollower& EnvFollower : SpectralEnvelopeFollowers)
		{
			EnvFollower.Init((float)RawDataSampleRate / FFTAnalysisFrameSize, FFTAnalysisAttackTime, FFTAnalysisReleaseTime);
		}

		// Build a new spectrum analyzer
		Audio::FSpectrumAnalyzer SpectrumAnalyzer(SpectrumAnalyzerSettings, (float)RawDataSampleRate);

		// The audio data block to use to submit audio data to the spectrum analyzer
		Audio::AlignedFloatBuffer AnalysisData;
		check(FFTAnalysisFrameSize > 256);
		AnalysisData.Reserve(FFTAnalysisFrameSize);

		float MaximumMagnitude = 0.0f;
		for (uint32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			// Get the averaged sample value of all the channels
			float SampleValue = 0.0f;
			for (uint16 ChannelIndex = 0; ChannelIndex < RawDataNumChannels; ++ChannelIndex)
			{
				SampleValue += (float)InputData[FrameIndex * RawDataNumChannels] / 32767.0f;
			}
			SampleValue /= RawDataNumChannels;

			// Accumate the samples in the scratch buffer
			AnalysisData.Add(SampleValue);

			// Until we reached the frame size
			if (AnalysisData.Num() == FFTAnalysisFrameSize)
			{
				SpectrumAnalyzer.PushAudio(AnalysisData.GetData(), AnalysisData.Num());

				// Block while the analyzer does the analysis
				SpectrumAnalyzer.PerformAnalysisIfPossible(true);

				FSoundWaveSpectralTimeData NewData;

				// Don't need to lock here since we're doing this sync, but it's here as that's the expected pattern for the Spectrum analyzer
				SpectrumAnalyzer.LockOutputBuffer();

				// Get the magntiudes for the specified frequencies
				for (int32 Index = 0; Index < FrequenciesToAnalyze.Num(); ++Index)
				{
					float Frequency = FrequenciesToAnalyze[Index];
					FSoundWaveSpectralDataEntry DataEntry;
					DataEntry.Magnitude = SpectrumAnalyzer.GetMagnitudeForFrequency(Frequency);

					// Feed the magnitude through the spectral envelope follower for this band
					DataEntry.Magnitude = SpectralEnvelopeFollowers[Index].ProcessAudioNonClamped(DataEntry.Magnitude);

					// Track the max magnitude so we can later set normalized magnitudes
					if (DataEntry.Magnitude > MaximumMagnitude)
					{
						MaximumMagnitude = DataEntry.Magnitude;
					}

					NewData.Data.Add(DataEntry);
				}

				SpectrumAnalyzer.UnlockOutputBuffer();

				// The time stamp is derived from the frame index and sample rate
				NewData.TimeSec = FMath::Max((float)(FrameIndex - FFTAnalysisFrameSize + 1) / RawDataSampleRate, 0.0f);

				/*
				// TODO: add FFTAnalysisTimeOffset
				// Don't let the time shift be more than the negative or postive duration
				float Duration = (float)NumFrames / RawDataSampleRate;
				float TimeShift = FMath::Clamp((float)FFTAnalysisTimeOffset / 1000, -Duration, Duration);

				NewData.TimeSec = NewData.TimeSec + (float)FFTAnalysisTimeOffset / 1000;

				// Truncate data if time shift is far enough to left that it's before the start of the sound
				if (TreatFileAsLoopingForAnalysis)
				{
					// Wrap the time value from endpoints if we're told this sound wave is looping
					if (NewData.TimeSec < 0.0f)
					{
						NewData.TimeSec = Duration + NewData.TimeSec;
					}
					else if (NewData.TimeSec >= Duration)
					{
						NewData.TimeSec = NewData.TimeSec - Duration;
					}
					CookedSpectralTimeData.Add(NewData);
				}
				else if (NewData.TimeSec > 0.0f)
				{
				CookedSpectralTimeData.Add(NewData);
				}
				*/

				CookedSpectralTimeData.Add(NewData);

				AnalysisData.Reset();
			}
		}

		// Sort predicate for sorting spectral data by time (lowest first)
		struct FSortSpectralDataByTime
		{
			FORCEINLINE bool operator()(const FSoundWaveSpectralTimeData& A, const FSoundWaveSpectralTimeData& B) const
			{
				return A.TimeSec < B.TimeSec;
			}
		};

		CookedSpectralTimeData.Sort(FSortSpectralDataByTime());

		// It's possible for the maximum magnitude to be 0.0 if the audio file was silent.
		if (MaximumMagnitude > 0.0f)
		{
			// Normalize all the magnitude values based on the highest magnitude
			for (FSoundWaveSpectralTimeData& SpectralTimeData : CookedSpectralTimeData)
			{
				for (FSoundWaveSpectralDataEntry& DataEntry : SpectralTimeData.Data)
				{
					DataEntry.NormalizedMagnitude = DataEntry.Magnitude / MaximumMagnitude;
				}
			}
		}

	}
}

void USoundWave::BakeEnvelopeAnalysis()
{
	// Clear any existing envelope data regardless of if it's enabled. If this was enabled and is now toggled, this will clear previous data.
	CookedEnvelopeTimeData.Reset();

	// Perform analysis if enabled on the sound wave
	if (bEnableAmplitudeEnvelopeAnalysis)
	{
		if (ChannelSizes.Num() > 0)
		{
			UE_LOG(LogAudio, Warning, TEXT("Soundwave '%s' has multi-channel audio (channels greater than 2). Baking envelope analysis is not currently supported for this yet."), *GetFullName());
			return;
		}

		// Retrieve the raw imported data
		TArray<uint8> RawImportedWaveData;
		uint32 RawDataSampleRate = 0;
		uint16 RawDataNumChannels = 0;

		USoundWave* SoundWaveToUseForAnalysis = this;
		if (OverrideSoundToUseForAnalysis)
		{
			SoundWaveToUseForAnalysis = OverrideSoundToUseForAnalysis;
		}

		if (!SoundWaveToUseForAnalysis->GetImportedSoundWaveData(RawImportedWaveData, RawDataSampleRate, RawDataNumChannels))
		{
			return;
		}

		if (RawDataSampleRate == 0 || RawDataNumChannels == 0)
		{
			UE_LOG(LogAudio, Error, TEXT("Failed to parse the raw imported data for '%s' for baked FFT analysis."), *GetFullName());
			return;
		}

		const uint32 NumFrames = (RawImportedWaveData.Num() / sizeof(int16)) / RawDataNumChannels;
		int16* InputData = (int16*)RawImportedWaveData.GetData();

		Audio::FEnvelopeFollower EnvelopeFollower;
		EnvelopeFollower.Init(RawDataSampleRate, EnvelopeFollowerAttackTime, EnvelopeFollowerReleaseTime);

		for (uint32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			// Get the averaged sample value of all the channels
			float SampleValue = 0.0f;
			for (uint16 ChannelIndex = 0; ChannelIndex < RawDataNumChannels; ++ChannelIndex)
			{
				SampleValue += (float)InputData[FrameIndex * RawDataNumChannels] / 32767.0f;
			}
			SampleValue /= RawDataNumChannels;

			float Output = EnvelopeFollower.ProcessAudio(SampleValue);

			// Until we reached the frame size
			if (FrameIndex % EnvelopeFollowerFrameSize == 0)
			{
				FSoundWaveEnvelopeTimeData NewData;
				NewData.Amplitude = Output;
				NewData.TimeSec = (float)FrameIndex / RawDataSampleRate;
				CookedEnvelopeTimeData.Add(NewData);
			}
		}
	}
}

void USoundWave::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName CompressionQualityFName = GET_MEMBER_NAME_CHECKED(USoundWave, CompressionQuality);
	static const FName SampleRateFName = GET_MEMBER_NAME_CHECKED(USoundWave,SampleRateQuality);
	static const FName StreamingFName = GET_MEMBER_NAME_CHECKED(USoundWave, bStreaming);
	static const FName SeekableStreamingFName = GET_MEMBER_NAME_CHECKED(USoundWave, bSeekableStreaming);

	// Prevent constant re-compression of SoundWave while properties are being changed interactively
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		// Regenerate on save any compressed sound formats or if analysis needs to be re-done
		if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
		{
			const FName& Name = PropertyThatChanged->GetFName();
			if (Name == CompressionQualityFName || Name == SampleRateFName || Name == StreamingFName || Name == SeekableStreamingFName)
			{
				InvalidateCompressedData();
				FreeResources();
				UpdatePlatformData();
				MarkPackageDirty();
			}

			if (AnyFFTAnalysisPropertiesChanged(Name))
			{
				BakeFFTAnalysis();
			}

			if (AnyEnvelopeAnalysisPropertiesChanged(Name))
			{
				BakeEnvelopeAnalysis();
			}
		}
	}
}
#endif // WITH_EDITOR

void USoundWave::FreeResources(bool bStopSoundsUsingThisResource)
{
	check(IsInAudioThread());

	// Housekeeping of stats
	DEC_FLOAT_STAT_BY(STAT_AudioBufferTime, Duration);
	DEC_FLOAT_STAT_BY(STAT_AudioBufferTimeChannels, NumChannels * Duration);

	// GEngine is NULL during script compilation and GEngine->Client and its audio device might be
	// destroyed first during the exit purge.
	if (GEngine && !GExitPurge)
	{
		// Notify the audio device to free the bulk data associated with this wave.
		FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
		if (bStopSoundsUsingThisResource && AudioDeviceManager)
		{
			AudioDeviceManager->StopSoundsUsingResource(this);
			AudioDeviceManager->FreeResource(this);
		}
	}

	if (CachedRealtimeFirstBuffer)
	{
		FMemory::Free(CachedRealtimeFirstBuffer);
		CachedRealtimeFirstBuffer = nullptr;
	}

	// Just in case the data was created but never uploaded
	if (RawPCMData)
	{
		FMemory::Free(RawPCMData);
		RawPCMData = nullptr;
	}

	// Remove the compressed copy of the data
	RemoveAudioResource();

	// Stat housekeeping
	DEC_DWORD_STAT_BY(STAT_AudioMemorySize, TrackedMemoryUsage);
	DEC_DWORD_STAT_BY(STAT_AudioMemory, TrackedMemoryUsage);
	TrackedMemoryUsage = 0;

	ResourceID = 0;
	bDynamicResource = false;
	DecompressionType = DTYPE_Setup;
	SetPrecacheState(ESoundWavePrecacheState::NotStarted);
	bDecompressedFromOgg = false;

	if (ResourceState == ESoundWaveResourceState::Freeing)
	{
		ResourceState = ESoundWaveResourceState::Freed;
	}
}

bool USoundWave::CleanupDecompressor(bool bForceWait)
{
	check(IsInAudioThread());

	if (!AudioDecompressor)
	{
		check(GetPrecacheState() == ESoundWavePrecacheState::Done);
		return true;
	}

	if (AudioDecompressor->IsDone())
	{
		delete AudioDecompressor;
		AudioDecompressor = nullptr;
		SetPrecacheState(ESoundWavePrecacheState::Done);
		return true;
	}

	if (bForceWait)
	{
		AudioDecompressor->EnsureCompletion();
		delete AudioDecompressor;
		AudioDecompressor = nullptr;
		SetPrecacheState(ESoundWavePrecacheState::Done);
		return true;
	}

	return false;
}

FWaveInstance& USoundWave::HandleStart(FActiveSound& ActiveSound, const UPTRINT WaveInstanceHash) const
{
	// Create a new wave instance and associate with the ActiveSound
	FWaveInstance& WaveInstance = ActiveSound.AddWaveInstance(WaveInstanceHash);

	// Add in the subtitle if they exist
	if (ActiveSound.bHandleSubtitles && Subtitles.Num() > 0)
	{
		FQueueSubtitleParams QueueSubtitleParams(Subtitles);
		{
			QueueSubtitleParams.AudioComponentID = ActiveSound.GetAudioComponentID();
			QueueSubtitleParams.WorldPtr = ActiveSound.GetWeakWorld();
			QueueSubtitleParams.WaveInstance = (PTRINT)&WaveInstance;
			QueueSubtitleParams.SubtitlePriority = ActiveSound.SubtitlePriority;
			QueueSubtitleParams.Duration = Duration;
			QueueSubtitleParams.bManualWordWrap = bManualWordWrap;
			QueueSubtitleParams.bSingleLine = bSingleLine;
			QueueSubtitleParams.RequestedStartTime = ActiveSound.RequestedStartTime;
		}

		FSubtitleManager::QueueSubtitles(QueueSubtitleParams);
	}

	return WaveInstance;
}

bool USoundWave::IsReadyForFinishDestroy()
{
	{
		FScopeLock Lock(&SourcesPlayingCs);

		for (ISoundWaveClient* SoundWaveClientPtr : SourcesPlaying)
		{
			// Should never have a null element here, inactive elements are removed from the container
			check(SoundWaveClientPtr);
			SoundWaveClientPtr->OnIsReadyForFinishDestroy(this);
		}
	}

	const bool bIsStreamingInProgress = IStreamingManager::Get().GetAudioStreamingManager().IsStreamingInProgress(this);

	check(GetPrecacheState() != ESoundWavePrecacheState::InProgress);

	// Wait till streaming and decompression finishes before deleting resource.
	if (!bIsStreamingInProgress && ResourceState == ESoundWaveResourceState::NeedsFree)
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.FreeResources"), STAT_AudioFreeResources, STATGROUP_AudioThreadCommands);

		USoundWave* SoundWave = this;
		ResourceState = ESoundWaveResourceState::Freeing;
		FAudioThread::RunCommandOnAudioThread([SoundWave]()
		{
			SoundWave->FreeResources();
		}, GET_STATID(STAT_AudioFreeResources));
	}

	return ResourceState == ESoundWaveResourceState::Freed;
}


void USoundWave::FinishDestroy()
{
	Super::FinishDestroy();

	FScopeLock Lock(&SourcesPlayingCs);
	int32 CurrNumSourcesPlaying = SourcesPlaying.Num();

	for (int32 i = CurrNumSourcesPlaying - 1; i >= 0; --i)
	{
		ISoundWaveClient* SoundWaveClientPtr = SourcesPlaying[i];

		if (SoundWaveClientPtr)
		{
			SoundWaveClientPtr->OnFinishDestroy(this);
			SourcesPlaying.RemoveAtSwap(i, 1, false);
		}
	}

	check(GetPrecacheState() != ESoundWavePrecacheState::InProgress);
	check(AudioDecompressor == nullptr);

	CleanupCachedRunningPlatformData();
#if WITH_EDITOR
	if (!GExitPurge)
	{
		ClearAllCachedCookedPlatformData();
	}
#endif

	if (FApp::CanEverRenderAudio())
	{
		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundWave(this);
	}
}

void USoundWave::Parse(FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances)
{
	check(AudioDevice);

	FWaveInstance* WaveInstance = ActiveSound.FindWaveInstance(NodeWaveInstanceHash);

	const bool bIsNewWave = WaveInstance == nullptr;

	// Create a new WaveInstance if this SoundWave doesn't already have one associated with it.
	if (!WaveInstance)
	{
		if (!ActiveSound.bRadioFilterSelected)
		{
			ActiveSound.ApplyRadioFilter(ParseParams);
		}

		WaveInstance = &HandleStart(ActiveSound, NodeWaveInstanceHash);
	}

	// Looping sounds are never actually finished
	if (bLooping || ParseParams.bLooping)
	{
		WaveInstance->bIsFinished = false;
#if !(NO_LOGGING || UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!ActiveSound.bWarnedAboutOrphanedLooping && ActiveSound.GetAudioComponentID() == 0 && ActiveSound.FadeOut == FActiveSound::EFadeOut::None)
		{
			UE_LOG(LogAudio, Warning, TEXT("Detected orphaned looping sound '%s'."), *ActiveSound.GetSound()->GetName());
			ActiveSound.bWarnedAboutOrphanedLooping = true;
		}
#endif
	}

	// Early out if finished.
	if (WaveInstance->bIsFinished)
	{
		return;
	}

	// Propagate properties and add WaveInstance to outgoing array of FWaveInstances.
	WaveInstance->SetVolume(ParseParams.Volume * Volume);
	WaveInstance->SetVolumeMultiplier(ParseParams.VolumeMultiplier);
	WaveInstance->SetDistanceAttenuation(ParseParams.DistanceAttenuation);
	WaveInstance->SetPitch(ParseParams.Pitch * Pitch);
	WaveInstance->bEnableLowPassFilter = ParseParams.bEnableLowPassFilter;
	WaveInstance->bIsOccluded = ParseParams.bIsOccluded;
	WaveInstance->LowPassFilterFrequency = ParseParams.LowPassFilterFrequency;
	WaveInstance->OcclusionFilterFrequency = ParseParams.OcclusionFilterFrequency;
	WaveInstance->AttenuationLowpassFilterFrequency = ParseParams.AttenuationLowpassFilterFrequency;
	WaveInstance->AttenuationHighpassFilterFrequency = ParseParams.AttenuationHighpassFilterFrequency;
	WaveInstance->AmbientZoneFilterFrequency = ParseParams.AmbientZoneFilterFrequency;
	WaveInstance->bApplyRadioFilter = ActiveSound.bApplyRadioFilter;
	WaveInstance->StartTime = ParseParams.StartTime;
	WaveInstance->UserIndex = ActiveSound.UserIndex;
	WaveInstance->OmniRadius = ParseParams.OmniRadius;
	WaveInstance->StereoSpread = ParseParams.StereoSpread;
	WaveInstance->AttenuationDistance = ParseParams.AttenuationDistance;
	WaveInstance->ListenerToSoundDistance = ParseParams.ListenerToSoundDistance;
	WaveInstance->ListenerToSoundDistanceForPanning = ParseParams.ListenerToSoundDistanceForPanning;
	WaveInstance->AbsoluteAzimuth = ParseParams.AbsoluteAzimuth;

	if (NumChannels <= 2)
	{
		WaveInstance->SourceEffectChain = ParseParams.SourceEffectChain;
	}

	bool bAlwaysPlay = false;

	// Properties from the sound class
	WaveInstance->SoundClass = ParseParams.SoundClass;
	if (ParseParams.SoundClass)
	{
		const FSoundClassProperties* SoundClassProperties = AudioDevice->GetSoundClassCurrentProperties(ParseParams.SoundClass);
		check(SoundClassProperties);

		// Use values from "parsed/ propagated" sound class properties
		float VolumeMultiplier = WaveInstance->GetVolumeMultiplier();
		WaveInstance->SetVolumeMultiplier(VolumeMultiplier * SoundClassProperties->Volume);
		WaveInstance->SetPitch(WaveInstance->Pitch * SoundClassProperties->Pitch);

		WaveInstance->SoundClassFilterFrequency = SoundClassProperties->LowPassFilterFrequency;
		WaveInstance->VoiceCenterChannelVolume = SoundClassProperties->VoiceCenterChannelVolume;
		WaveInstance->RadioFilterVolume = SoundClassProperties->RadioFilterVolume * ParseParams.VolumeMultiplier;
		WaveInstance->RadioFilterVolumeThreshold = SoundClassProperties->RadioFilterVolumeThreshold * ParseParams.VolumeMultiplier;
		WaveInstance->LFEBleed = SoundClassProperties->LFEBleed;

		WaveInstance->bIsUISound = ActiveSound.bIsUISound || SoundClassProperties->bIsUISound;
		WaveInstance->bIsMusic = ActiveSound.bIsMusic || SoundClassProperties->bIsMusic;
		WaveInstance->bCenterChannelOnly = ActiveSound.bCenterChannelOnly || SoundClassProperties->bCenterChannelOnly;
		WaveInstance->bReverb = ActiveSound.bReverb || SoundClassProperties->bReverb;
		WaveInstance->OutputTarget = SoundClassProperties->OutputTarget;

		if (SoundClassProperties->bApplyEffects)
		{
			UAudioSettings* Settings = GetMutableDefault<UAudioSettings>();
			WaveInstance->SoundSubmix = Cast<USoundSubmix>(FSoftObjectPtr(Settings->EQSubmix).Get());
		}
		else if (SoundClassProperties->DefaultSubmix)
		{
			WaveInstance->SoundSubmix = SoundClassProperties->DefaultSubmix;
		}

		if (SoundClassProperties->bApplyAmbientVolumes)
		{
			VolumeMultiplier = WaveInstance->GetVolumeMultiplier();
			WaveInstance->SetVolumeMultiplier(VolumeMultiplier * ParseParams.InteriorVolumeMultiplier);
			WaveInstance->RadioFilterVolume *= ParseParams.InteriorVolumeMultiplier;
			WaveInstance->RadioFilterVolumeThreshold *= ParseParams.InteriorVolumeMultiplier;
		}

		bAlwaysPlay = ActiveSound.bAlwaysPlay || SoundClassProperties->bAlwaysPlay;
	}
	else
	{
		WaveInstance->VoiceCenterChannelVolume = 0.f;
		WaveInstance->RadioFilterVolume = 0.f;
		WaveInstance->RadioFilterVolumeThreshold = 0.f;
		WaveInstance->LFEBleed = 0.f;
		WaveInstance->bIsUISound = ActiveSound.bIsUISound;
		WaveInstance->bIsMusic = ActiveSound.bIsMusic;
		WaveInstance->bReverb = ActiveSound.bReverb;
		WaveInstance->bCenterChannelOnly = ActiveSound.bCenterChannelOnly;

		bAlwaysPlay = ActiveSound.bAlwaysPlay;
	}

	WaveInstance->bIsAmbisonics = bIsAmbisonics;

	if (ParseParams.SoundSubmix)
	{
		WaveInstance->SoundSubmix = ParseParams.SoundSubmix;
	}
	else if (USoundSubmixBase* WaveSubmix = GetSoundSubmix())
	{
		WaveInstance->SoundSubmix = WaveSubmix;
	}

	// If set to bAlwaysPlay, increase the current sound's priority scale by 10x. This will still result in a possible 0-priority output if the sound has 0 actual volume
	if (bAlwaysPlay)
	{
		static constexpr float VolumeWeightedMaxPriority = TNumericLimits<float>::Max() / MAX_VOLUME;
		WaveInstance->Priority = VolumeWeightedMaxPriority;
	}
	else
	{
		WaveInstance->Priority = FMath::Clamp(ParseParams.Priority, 0.0f, 100.0f);
	}

	WaveInstance->Location = ParseParams.Transform.GetTranslation();
	WaveInstance->bIsStarted = true;
	WaveInstance->bAlreadyNotifiedHook = false;

	WaveInstance->WaveData = this;
	WaveInstance->NotifyBufferFinishedHooks = ParseParams.NotifyBufferFinishedHooks;
	WaveInstance->LoopingMode = ((bLooping || ParseParams.bLooping) ? LOOP_Forever : LOOP_Never);
	WaveInstance->bIsPaused = ParseParams.bIsPaused;

	// If we're normalizing 3d stereo spatialized sounds, we need to scale by -6 dB
	WaveInstance->SetUseSpatialization(ParseParams.bUseSpatialization);

	// Setup the spat method if we're actually spatializing (note a cvar can turn this off so we use the getter here)
	if (WaveInstance->GetUseSpatialization())
	{
		WaveInstance->SpatializationMethod = ParseParams.SpatializationMethod;

		// Check for possible HRTF-enforcement if this is a spatialized sound
		if (AudioDevice->IsHRTFEnabledForAll() && ParseParams.SpatializationMethod == ESoundSpatializationAlgorithm::SPATIALIZATION_Default)
		{
			WaveInstance->SpatializationMethod = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
		}
		else
		{
			WaveInstance->SpatializationMethod = ParseParams.SpatializationMethod;
		}

		//If this is using binaural audio, update whether its an external send
		if (WaveInstance->SpatializationMethod == ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF)
		{
			WaveInstance->SetSpatializationIsExternalSend(ParseParams.bSpatializationIsExternalSend);
		}

		// Apply stereo normalization to wave instances if enabled
		if (ParseParams.bApplyNormalizationToStereoSounds && NumChannels == 2)
		{
			float WaveInstanceVolume = WaveInstance->GetVolume();
			WaveInstance->SetVolume(WaveInstanceVolume * 0.5f);
		}
	}

	// Copy reverb send settings
	WaveInstance->ReverbSendMethod = ParseParams.ReverbSendMethod;
	WaveInstance->ManualReverbSendLevel = ParseParams.ManualReverbSendLevel;
	WaveInstance->CustomRevebSendCurve = ParseParams.CustomReverbSendCurve;
	WaveInstance->ReverbSendLevelRange = ParseParams.ReverbSendLevelRange;
	WaveInstance->ReverbSendLevelDistanceRange = ParseParams.ReverbSendLevelDistanceRange;

 	// Copy the submix send settings
 	WaveInstance->SubmixSendSettings = ParseParams.SubmixSendSettings;

	// Get the envelope follower settings
	WaveInstance->EnvelopeFollowerAttackTime = ParseParams.EnvelopeFollowerAttackTime;
	WaveInstance->EnvelopeFollowerReleaseTime = ParseParams.EnvelopeFollowerReleaseTime;

	// Copy over the submix sends.
	WaveInstance->SoundSubmixSends = ParseParams.SoundSubmixSends;

	// Copy over the source bus send and data
	if (!WaveInstance->ActiveSound->bIsPreviewSound)
	{
		//Parse the parameters of the wave instance
		WaveInstance->bEnableBusSends = ParseParams.bEnableBusSends;
		
		// HRTF rendering doesn't render their output on the base submix
		if (!((WaveInstance->SpatializationMethod == SPATIALIZATION_HRTF) && (WaveInstance->bSpatializationIsExternalSend)))
		{
			if (ActiveSound.bHasActiveMainSubmixOutputOverride)
			{
				WaveInstance->bEnableBaseSubmix = ActiveSound.bEnableMainSubmixOutputOverride;
			}
			else
			{
				WaveInstance->bEnableBaseSubmix = ParseParams.bEnableBaseSubmix;
			}
		}
		else
		{
			WaveInstance->bEnableBaseSubmix = false;
		}
		WaveInstance->bEnableSubmixSends = ParseParams.bEnableSubmixSends;

		// Active sounds can override their enablement behavior via audio components
		if (ActiveSound.bHasActiveBusSendRoutingOverride)
		{
			WaveInstance->bEnableBusSends = ActiveSound.bEnableBusSendRoutingOverride;
		}

		if (ActiveSound.bHasActiveSubmixSendRoutingOverride)
		{
			WaveInstance->bEnableSubmixSends = ActiveSound.bEnableSubmixSendRoutingOverride;
		}
	}
	else //if this is a preview sound, ignore sends and only play the base submix
	{
		WaveInstance->bEnableBaseSubmix = true;
	}

	for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
	{
		WaveInstance->BusSends[BusSendType] = ParseParams.BusSends[BusSendType];
	}

	// Pass along plugin settings to the wave instance
	WaveInstance->SpatializationPluginSettings = ParseParams.SpatializationPluginSettings;
	WaveInstance->OcclusionPluginSettings = ParseParams.OcclusionPluginSettings;
	WaveInstance->ReverbPluginSettings = ParseParams.ReverbPluginSettings;

	if (WaveInstance->IsPlaying())
	{
		WaveInstances.Add(WaveInstance);
		ActiveSound.bFinished = false;
	}
	else if (WaveInstance->LoopingMode == LOOP_Forever)
	{
		ActiveSound.bFinished = false;
	}
	// Not looping, silent, and not set to play when silent
	else
	{
		// If no wave instance added to transient array not looping, and just created, immediately delete
		// to avoid initializing on a later tick (achieved by adding to active sound's wave instance map
		// but not the passed transient WaveInstance array)
		if (bIsNewWave)
		{
			ActiveSound.RemoveWaveInstance(NodeWaveInstanceHash);
			return;
		}
	}

#if !NO_LOGGING
	// Sanity check
	if (NumChannels > 2 && !WaveInstance->bIsAmbisonics && WaveInstance->GetUseSpatialization() && !WaveInstance->bReportedSpatializationWarning)
	{
		static TSet<USoundWave*> ReportedSounds;
		if (!ReportedSounds.Contains(this))
		{
			FString SoundWarningInfo = FString::Printf(TEXT("Spatialization on sounds with channels greater than 2 is not supported. SoundWave: %s"), *GetName());
			if (ActiveSound.GetSound() != this)
			{
				SoundWarningInfo += FString::Printf(TEXT(" SoundCue: %s"), *ActiveSound.GetSound()->GetName());
			}

			const uint64 AudioComponentID = ActiveSound.GetAudioComponentID();
			if (AudioComponentID > 0)
			{
				FAudioThread::RunCommandOnGameThread([AudioComponentID, SoundWarningInfo]()
				{
					if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentID))
					{
						AActor* SoundOwner = AudioComponent->GetOwner();
						UE_LOG(LogAudio, Warning, TEXT("%s Actor: %s AudioComponent: %s"), *SoundWarningInfo, (SoundOwner ? *SoundOwner->GetName() : TEXT("None")), *AudioComponent->GetName());
					}
					else
					{
						UE_LOG(LogAudio, Warning, TEXT("%s"), *SoundWarningInfo);
					}
				});
			}
			else
			{
				UE_LOG(LogAudio, Warning, TEXT("%s"), *SoundWarningInfo);
			}

			ReportedSounds.Add(this);
		}
		WaveInstance->bReportedSpatializationWarning = true;
	}
#endif // !NO_LOGGING
}

bool USoundWave::IsPlayable() const
{
	return true;
}

float USoundWave::GetDuration()
{
	return (bLooping ? INDEFINITELY_LOOPING_DURATION : Duration);
}

bool USoundWave::IsStreaming(const TCHAR* PlatformName/* = nullptr */) const
{
	if (GIsEditor && ForceNonStreamingInEditorCVar != 0)
	{
		return false;
	}

	return IsStreaming(*FPlatformCompressionUtilities::GetCookOverrides(PlatformName));
}

bool USoundWave::IsStreaming(const FPlatformAudioCookOverrides& Overrides) const
{
	// We stream if (A) bStreaming is set to true, (B) bForceInline is false and either bUseLoadOnDemand was set to true in
	// our cook overrides, or the AutoStreamingThreshold was set and this sound is longer than the auto streaming threshold.
	if (bStreaming)
	{
		return true;
	}
	else if (LoadingBehavior == ESoundWaveLoadingBehavior::ForceInline) // TODO: Support setting ESoundWaveLoadingBehavior to ForceInline on a USoundClass.
	{
		return false;
	}
	else if (bProcedural)
	{
		return false;
	}

	// For stream caching, the auto streaming threshold is used to force sounds to be inlined:
	const bool bUsesStreamCache = Overrides.bUseStreamCaching;
	const bool bOverAutoStreamingThreshold = (Overrides.AutoStreamingThreshold > SMALL_NUMBER  && Duration > Overrides.AutoStreamingThreshold);

	return bUsesStreamCache || bOverAutoStreamingThreshold;
}

bool USoundWave::ShouldUseStreamCaching() const
{
	const bool bPlatformUsingStreamCaching = FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching();
	const bool bIsStreaming = IsStreaming(nullptr);
	return  bPlatformUsingStreamCaching && bIsStreaming;
}

TArrayView<const uint8> USoundWave::GetZerothChunk(bool bForImmediatePlayback)
{
	if(IsTemplate() || IsRunningDedicatedServer())
	{
		return TArrayView<const uint8>();
	}
	
	if (ShouldUseStreamCaching())
	{
		// In editor, we actually don't have a zeroth chunk until we try to play an audio file.
		if (GIsEditor)
		{
			EnsureZerothChunkIsLoaded();
		}

		check(ZerothChunkData.GetView().Num() > 0);

		if (GetNumChunks() > 1)
		{
			// Prime first chunk for playback.
			IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(this, 1, [](EAudioChunkLoadResult InResult) {}, ENamedThreads::AnyThread, bForImmediatePlayback);
		}

		return ZerothChunkData.GetView();
	}
	else
	{
		FAudioChunkHandle ChunkHandle = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(this, 0);
		return TArrayView<const uint8>(ChunkHandle.GetData(), ChunkHandle.Num());
	}
}

bool USoundWave::IsSeekableStreaming() const
{
	return bStreaming && bSeekableStreaming;
}

bool USoundWave::GetSoundWavesWithCookedAnalysisData(TArray<USoundWave*>& OutSoundWaves)
{
	if (CookedSpectralTimeData.Num() > 0 || CookedEnvelopeTimeData.Num() > 0)
	{
		OutSoundWaves.Add(this);
		return true;
	}
	return false;
}

bool USoundWave::HasCookedFFTData() const
{
	return CookedSpectralTimeData.Num() > 0;
}

bool USoundWave::HasCookedAmplitudeEnvelopeData() const
{
	return CookedEnvelopeTimeData.Num() > 0;
}

void USoundWave::AddPlayingSource(const FSoundWaveClientPtr& Source)
{
	check(Source);
	check(IsInAudioThread() || IsInGameThread());   // Don't allow incrementing on other threads as it's not safe (for GCing of this wave).
	if (Source)
	{
		FScopeLock Lock(&SourcesPlayingCs);
		check(!SourcesPlaying.Contains(Source));
		SourcesPlaying.Add(Source);
	}
}

void USoundWave::RemovePlayingSource(const FSoundWaveClientPtr& Source)
{
	if (Source)
	{
		FScopeLock Lock(&SourcesPlayingCs);
		check(SourcesPlaying.Contains(Source));
		SourcesPlaying.RemoveSwap(Source);
	}
}

void USoundWave::UpdatePlatformData()
{
	if (IsStreaming(nullptr))
	{
		// Make sure there are no pending requests in flight.
		while (IStreamingManager::Get().GetAudioStreamingManager().IsStreamingInProgress(this))
		{
			// Give up timeslice.
			FPlatformProcess::Sleep(0);
		}

#if WITH_EDITORONLY_DATA
		// Temporarily remove from streaming manager to release currently used data chunks
		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundWave(this);
		// Recache platform data if the source has changed.
		CachePlatformData();
		// Add back to the streaming manager to reload first chunk
		IStreamingManager::Get().GetAudioStreamingManager().AddStreamingSoundWave(this);
#endif
	}
	else
	{
		IStreamingManager::Get().GetAudioStreamingManager().RemoveStreamingSoundWave(this);
	}
}

float USoundWave::GetSampleRateForCurrentPlatform()
{
	if (bProcedural)
	{
		return SampleRate;
	}

	if (GIsEditor)
	{
		float SampleRateOverride = FPlatformCompressionUtilities::GetTargetSampleRateForPlatform(SampleRateQuality);
		return (SampleRateOverride > 0) ? FMath::Min(SampleRateOverride, (float)SampleRate) : SampleRate;
	}
	else
	{
		if (bCachedSampleRateFromPlatformSettings)
		{
			return CachedSampleRateOverride;
		}
		else if (bSampleRateManuallyReset)
		{
			CachedSampleRateOverride = SampleRate;
			bCachedSampleRateFromPlatformSettings = true;

			return CachedSampleRateOverride;
		}
		else
		{
			CachedSampleRateOverride = FPlatformCompressionUtilities::GetTargetSampleRateForPlatform(SampleRateQuality);
			if (CachedSampleRateOverride < 0 || SampleRate < CachedSampleRateOverride)
			{
				CachedSampleRateOverride = SampleRate;
			}

			bCachedSampleRateFromPlatformSettings = true;
			return CachedSampleRateOverride;
		}
	}
}

float USoundWave::GetSampleRateForCompressionOverrides(const FPlatformAudioCookOverrides* CompressionOverrides)
{
	const float* SampleRatePtr = CompressionOverrides->PlatformSampleRates.Find(SampleRateQuality);
	if (SampleRatePtr && *SampleRatePtr > 0.0f)
	{
		return FMath::Min(*SampleRatePtr, static_cast<float>(SampleRate));
	}
	else
	{
		return -1.0f;
	}
}

bool USoundWave::GetChunkData(int32 ChunkIndex, uint8** OutChunkData, bool bMakeSureChunkIsLoaded /* = false */)
{
	if (RunningPlatformData->GetChunkFromDDC(ChunkIndex, OutChunkData, bMakeSureChunkIsLoaded) == 0)
	{
#if WITH_EDITORONLY_DATA
		// Unable to load chunks from the cache. Rebuild the sound and attempt to recache it.
		UE_LOG(LogAudio, Display, TEXT("GetChunkData failed, rebuilding %s"), *GetPathName());

		ForceRebuildPlatformData();
		if (RunningPlatformData->GetChunkFromDDC(ChunkIndex, OutChunkData, bMakeSureChunkIsLoaded) == 0)
		{
			UE_LOG(LogAudio, Warning, TEXT("Failed to build sound %s."), *GetPathName());
		}
		else
		{
			// Succeeded after rebuilding platform data
			return true;
		}
#else
		// Failed to find the SoundWave chunk in the cooked package.
		UE_LOG(LogAudio, Warning, TEXT("GetChunkData failed while streaming. Ensure the following file is cooked: %s"), *GetPathName());
#endif // #if WITH_EDITORONLY_DATA
		return false;
	}
	return true;
}

uint32 USoundWave::GetInterpolatedCookedFFTDataForTimeInternal(float InTime, uint32 StartingIndex, TArray<FSoundWaveSpectralData>& OutData, bool bLoop)
{
	// Find the two entries on either side of the input time
	int32 NumDataEntries = CookedSpectralTimeData.Num();
	for (int32 Index = StartingIndex; Index < NumDataEntries; ++Index)
	{
		// Get the current data at this index
		const FSoundWaveSpectralTimeData& CurrentData = CookedSpectralTimeData[Index];

		// Get the next data, wrap if needed (i.e. if current is last index, we'll lerp to the first index)
		const FSoundWaveSpectralTimeData& NextData = CookedSpectralTimeData[(Index + 1) % NumDataEntries];

		if (InTime >= CurrentData.TimeSec && InTime < NextData.TimeSec)
		{
			// Lerping alpha is fraction from current to next data
			const float Alpha = (InTime - CurrentData.TimeSec) / (NextData.TimeSec - CurrentData.TimeSec);
			for (int32 FrequencyIndex = 0; FrequencyIndex < FrequenciesToAnalyze.Num(); ++FrequencyIndex)
			{
				FSoundWaveSpectralData InterpolatedData;
				InterpolatedData.FrequencyHz = FrequenciesToAnalyze[FrequencyIndex];
				InterpolatedData.Magnitude = FMath::Lerp(CurrentData.Data[FrequencyIndex].Magnitude, NextData.Data[FrequencyIndex].Magnitude, Alpha);
				InterpolatedData.NormalizedMagnitude = FMath::Lerp(CurrentData.Data[FrequencyIndex].NormalizedMagnitude, NextData.Data[FrequencyIndex].NormalizedMagnitude, Alpha);

				OutData.Add(InterpolatedData);
			}

			// Sort by frequency (lowest frequency first).
			OutData.Sort(FCompareSpectralDataByFrequencyHz());

			// We found cooked spectral data which maps to these indices
			return Index;
		}
	}

	return INDEX_NONE;
}

bool USoundWave::GetInterpolatedCookedFFTDataForTime(float InTime, uint32& InOutLastIndex, TArray<FSoundWaveSpectralData>& OutData, bool bLoop)
{
	if (CookedSpectralTimeData.Num() > 0)
	{
		// Handle edge cases
		if (!bLoop)
		{
			// Pointer to which data to use
			FSoundWaveSpectralTimeData* SpectralTimeData = nullptr;

			// We are past the edge
			if (InTime >= CookedSpectralTimeData.Last().TimeSec)
			{
				SpectralTimeData = &CookedSpectralTimeData.Last();
				InOutLastIndex = CookedPlatformData.Num() - 1;
			}
			// We are before the first data point
			else if (InTime < CookedSpectralTimeData[0].TimeSec)
			{
				SpectralTimeData = &CookedSpectralTimeData[0];
				InOutLastIndex = 0;
			}

			// If we were either case before we have a non-nullptr here
			if (SpectralTimeData != nullptr)
			{
				// Create an entry for this clamped output
				for (int32 FrequencyIndex = 0; FrequencyIndex < FrequenciesToAnalyze.Num(); ++FrequencyIndex)
				{
					FSoundWaveSpectralData InterpolatedData;
					InterpolatedData.FrequencyHz = FrequenciesToAnalyze[FrequencyIndex];
					InterpolatedData.Magnitude = SpectralTimeData->Data[FrequencyIndex].Magnitude;
					InterpolatedData.NormalizedMagnitude = SpectralTimeData->Data[FrequencyIndex].NormalizedMagnitude;

					OutData.Add(InterpolatedData);
				}

				return true;
			}
		}
		// We're looping
		else
		{
			// Need to check initial wrap-around case (i.e. we're reading earlier than first data point so need to lerp from last data point to first
			if (InTime >= 0.0f && InTime < CookedSpectralTimeData[0].TimeSec)
			{
				const FSoundWaveSpectralTimeData& CurrentData = CookedSpectralTimeData.Last();

				// Get the next data, wrap if needed (i.e. if current is last index, we'll lerp to the first index)
				const FSoundWaveSpectralTimeData& NextData = CookedSpectralTimeData[0];

				float TimeLeftFromLastDataToEnd = Duration - CurrentData.TimeSec;
				float Alpha = (TimeLeftFromLastDataToEnd + InTime) / (TimeLeftFromLastDataToEnd + NextData.TimeSec);

				for (int32 FrequencyIndex = 0; FrequencyIndex < FrequenciesToAnalyze.Num(); ++FrequencyIndex)
				{
					FSoundWaveSpectralData InterpolatedData;
					InterpolatedData.FrequencyHz = FrequenciesToAnalyze[FrequencyIndex];
					InterpolatedData.Magnitude = FMath::Lerp(CurrentData.Data[FrequencyIndex].Magnitude, NextData.Data[FrequencyIndex].Magnitude, Alpha);
					InterpolatedData.NormalizedMagnitude = FMath::Lerp(CurrentData.Data[FrequencyIndex].NormalizedMagnitude, NextData.Data[FrequencyIndex].NormalizedMagnitude, Alpha);

					OutData.Add(InterpolatedData);

					InOutLastIndex = 0;
				}
				return true;
			}
			// Or we've been offset a bit in the negative.
			else if (InTime < 0.0f)
			{
				// Wrap the time to the end of the sound wave file
				InTime = FMath::Clamp(Duration + InTime, 0.0f, Duration);
			}
		}

		uint32 StartingIndex = InOutLastIndex == INDEX_NONE ? 0 : InOutLastIndex;

		InOutLastIndex = GetInterpolatedCookedFFTDataForTimeInternal(InTime, StartingIndex, OutData, bLoop);
		if (InOutLastIndex == INDEX_NONE && StartingIndex != 0)
		{
			InOutLastIndex = GetInterpolatedCookedFFTDataForTimeInternal(InTime, 0, OutData, bLoop);
		}
		return InOutLastIndex != INDEX_NONE;
	}

	return false;
}

uint32 USoundWave::GetInterpolatedCookedEnvelopeDataForTimeInternal(float InTime, uint32 StartingIndex, float& OutAmplitude, bool bLoop)
{
	if (StartingIndex == INDEX_NONE || StartingIndex == CookedEnvelopeTimeData.Num())
	{
		StartingIndex = 0;
	}

	// Find the two entries on either side of the input time
	int32 NumDataEntries = CookedEnvelopeTimeData.Num();
	for (int32 Index = StartingIndex; Index < NumDataEntries; ++Index)
	{
		const FSoundWaveEnvelopeTimeData& CurrentData = CookedEnvelopeTimeData[Index];
		const FSoundWaveEnvelopeTimeData& NextData = CookedEnvelopeTimeData[(Index + 1) % NumDataEntries];

		if (InTime >= CurrentData.TimeSec && InTime < NextData.TimeSec)
		{
			// Lerping alpha is fraction from current to next data
			const float Alpha = (InTime - CurrentData.TimeSec) / (NextData.TimeSec - CurrentData.TimeSec);
			OutAmplitude = FMath::Lerp(CurrentData.Amplitude, NextData.Amplitude, Alpha);

			// We found cooked spectral data which maps to these indices
			return Index;
		}
	}

	// Did not find the data
	return INDEX_NONE;
}

bool USoundWave::GetInterpolatedCookedEnvelopeDataForTime(float InTime, uint32& InOutLastIndex, float& OutAmplitude, bool bLoop)
{
	InOutLastIndex = INDEX_NONE;
	if (CookedEnvelopeTimeData.Num() > 0 && InTime >= 0.0f)
	{
		// Handle edge cases
		if (!bLoop)
		{
			// We are past the edge
			if (InTime >= CookedEnvelopeTimeData.Last().TimeSec)
			{
				OutAmplitude = CookedEnvelopeTimeData.Last().Amplitude;
				InOutLastIndex = CookedEnvelopeTimeData.Num() - 1;
				return true;
			}
			// We are before the first data point
			else if (InTime < CookedEnvelopeTimeData[0].TimeSec)
			{
				OutAmplitude = CookedEnvelopeTimeData[0].Amplitude;
				InOutLastIndex = 0;
				return true;
			}
		}

		// Need to check initial wrap-around case (i.e. we're reading earlier than first data point so need to lerp from last data point to first
		if (InTime >= 0.0f && InTime < CookedEnvelopeTimeData[0].TimeSec)
		{
			const FSoundWaveEnvelopeTimeData& CurrentData = CookedEnvelopeTimeData.Last();
			const FSoundWaveEnvelopeTimeData& NextData = CookedEnvelopeTimeData[0];

			float TimeLeftFromLastDataToEnd = Duration - CurrentData.TimeSec;
			float Alpha = (TimeLeftFromLastDataToEnd + InTime) / (TimeLeftFromLastDataToEnd + NextData.TimeSec);

			OutAmplitude = FMath::Lerp(CurrentData.Amplitude, NextData.Amplitude, Alpha);
			InOutLastIndex = 0;
			return true;
		}
		// Or we've been offset a bit in the negative.
		else if (InTime < 0.0f)
		{
			// Wrap the time to the end of the sound wave file
			InTime = FMath::Clamp(Duration + InTime, 0.0f, Duration);
		}

		uint32 StartingIndex = InOutLastIndex == INDEX_NONE ? 0 : InOutLastIndex;

		InOutLastIndex = GetInterpolatedCookedEnvelopeDataForTimeInternal(InTime, StartingIndex, OutAmplitude, bLoop);
		if (InOutLastIndex == INDEX_NONE && StartingIndex != 0)
		{
			InOutLastIndex = GetInterpolatedCookedEnvelopeDataForTimeInternal(InTime, 0, OutAmplitude, bLoop);
		}
	}
	return InOutLastIndex != INDEX_NONE;
}

void USoundWave::GetHandleForChunkOfAudio(TFunction<void(FAudioChunkHandle&&)> OnLoadCompleted, bool bForceSync /*= false*/, int32 ChunkIndex /*= 1*/, ENamedThreads::Type CallbackThread /*= ENamedThreads::GameThread*/)
{
	ENamedThreads::Type ThreadToDispatchCallbackOn = (DispatchToGameThreadOnChunkRequestCVar != 0) ? ENamedThreads::GameThread : ENamedThreads::AnyThread;


	// if we are requesting a chunk that is out of bounds,
	// early exit.
	if (ChunkIndex >= static_cast<int32>(GetNumChunks()))
	{
		FAudioChunkHandle EmptyChunkHandle;
		OnLoadCompleted(MoveTemp(EmptyChunkHandle));
	}
	else if (bForceSync)
	{
		// For sync cases, we call GetLoadedChunk with bBlockForLoad = true, then execute the callback immediately.
		FAudioChunkHandle ChunkHandle = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(this, ChunkIndex, true);
		OnLoadCompleted(MoveTemp(ChunkHandle));
	}
	else
	{
		TWeakObjectPtr<USoundWave> WeakThis = MakeWeakObjectPtr(this);

		// For async cases, we call RequestChunk and request the loaded chunk in the completion callback.
		IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(this, ChunkIndex, [ThreadToDispatchCallbackOn, WeakThis, OnLoadCompleted, ChunkIndex, CallbackThread](EAudioChunkLoadResult LoadResult)
		{
			auto DispatchOnLoadCompletedCallback = [ThreadToDispatchCallbackOn, OnLoadCompleted, CallbackThread](FAudioChunkHandle&& InHandle)
			{
				if (CallbackThread == ThreadToDispatchCallbackOn)
				{
					OnLoadCompleted(MoveTemp(InHandle));
				}
				else
				{
					// If the callback was requested on a non-game thread, dispatch the callback to that thread.
					AsyncTask(CallbackThread, [OnLoadCompleted, InHandle]() mutable
					{
						OnLoadCompleted(MoveTemp(InHandle));
					});
				}
			};

			// If the USoundWave has been GC'd by the time this chunk finishes loading, abandon ship.
			if (WeakThis.IsValid() && (LoadResult == EAudioChunkLoadResult::Completed || LoadResult == EAudioChunkLoadResult::AlreadyLoaded))
			{
				USoundWave* ThisSoundWave = WeakThis.Get();
				FAudioChunkHandle ChunkHandle = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(ThisSoundWave, ChunkIndex, (BlockOnChunkLoadCompletionCVar != 0));

				// If we hit this, something went wrong in GetLoadedChunk.
				if (!ChunkHandle.IsValid())
				{
					UE_LOG(LogAudio, Display, TEXT("Failed to retrieve chunk %d from sound %s after successfully requesting it!"), ChunkIndex, *(WeakThis->GetName()));
				}
				DispatchOnLoadCompletedCallback(MoveTemp(ChunkHandle));
			}
			else
			{
				// Load failed. Return an invalid chunk handle.
				FAudioChunkHandle ChunkHandle;
				DispatchOnLoadCompletedCallback(MoveTemp(ChunkHandle));
			}
		}, ThreadToDispatchCallbackOn);
	}
}

void USoundWave::RetainCompressedAudio(bool bForceSync /*= false*/)
{
	// Since the zeroth chunk is always inlined and stored in memory,
	// early exit if we only have one chunk.
	if (GIsEditor || IsTemplate() || IsRunningDedicatedServer() || !IsStreaming() || DisableRetainingCVar || GetNumChunks() <= 1)
	{
		return;
	}

	// If the first chunk is already loaded and being retained,
	// don't kick off another load.
	if (FirstChunk.IsValid())
	{
		return;
	}
	else if (bForceSync)
	{
		FirstChunk = IStreamingManager::Get().GetAudioStreamingManager().GetLoadedChunk(this, 1, true);
		UE_CLOG(!FirstChunk.IsValid(), LogAudio, Display, TEXT("First chunk was invalid after synchronous load in RetainCompressedAudio(). This was likely because the cache was blown. Sound: %s"), *GetFullName());
	}
	else
	{
		GetHandleForChunkOfAudio([WeakThis = MakeWeakObjectPtr(this)](FAudioChunkHandle&& OutHandle)
		{
			if (OutHandle.IsValid())
			{
				AsyncTask(ENamedThreads::GameThread, [WeakThis, MovedHandle = MoveTemp(OutHandle)]() mutable
				{
					check(IsInGameThread());

					if (WeakThis.IsValid())
					{
						WeakThis->FirstChunk = MoveTemp(MovedHandle);
					}
				});
				
			}
		}, false, 1);
	}
}

void USoundWave::ReleaseCompressedAudio()
{
	// Here we release the USoundWave's handle to the compressed asset by resetting it.
	FirstChunk = FAudioChunkHandle();
}

bool USoundWave::IsRetainingAudio()
{
	return FirstChunk.IsValid();
}

void USoundWave::OverrideLoadingBehavior(ESoundWaveLoadingBehavior InLoadingBehavior)
{
	const ESoundWaveLoadingBehavior OldBehavior = GetLoadingBehavior(false);
	const bool bAlreadySetToRetained = (OldBehavior == ESoundWaveLoadingBehavior::RetainOnLoad);
	const bool bAlreadyLoaded = !HasAnyFlags(RF_NeedLoad);

	// already set to the most aggressive (non-inline) option
	if (bAlreadySetToRetained)
	{
		return;
	}

	// we don't want to retain in editor, we prime instead
	if (GIsEditor && InLoadingBehavior == ESoundWaveLoadingBehavior::RetainOnLoad)
	{
		InLoadingBehavior = ESoundWaveLoadingBehavior::PrimeOnLoad;
	}


	// record the new loading behavior
	// (if this soundwave isn't loaded yet, 
	// CachedSoundWaveLoadingBehavior will take precedence when it does load)
	CachedSoundWaveLoadingBehavior = InLoadingBehavior;
	bLoadingBehaviorOverridden = true;

	// If we're loading for the cook commandlet, we don't have streamed audio chunks to load.
	const bool bHasBuiltStreamedAudio = !GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker) && FApp::CanEverRenderAudio();

	// Manually perform prime/retain on already loaded sound waves
	if (bHasBuiltStreamedAudio && bAlreadyLoaded && IsStreaming())
	{
		if (InLoadingBehavior == ESoundWaveLoadingBehavior::RetainOnLoad)
		{
			ConditionalPostLoad();
			RetainCompressedAudio();
		}
		else if (InLoadingBehavior == ESoundWaveLoadingBehavior::PrimeOnLoad && GetNumChunks() > 1)
		{
			IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(this, 1, [](EAudioChunkLoadResult) {});
		}
	}
}

void USoundWave::SetCacheLookupIDForChunk(uint32 InChunkIndex, uint64 InCacheLookupID)
{
	check(FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching());
	check(RunningPlatformData && InChunkIndex < ((uint32)RunningPlatformData->Chunks.Num()));

	RunningPlatformData->Chunks[InChunkIndex].CacheLookupID = InCacheLookupID;
}

uint64 USoundWave::GetCacheLookupIDForChunk(uint32 InChunkIndex)
{
	check(FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching());
	check(RunningPlatformData && (InChunkIndex < (uint32)RunningPlatformData->Chunks.Num()));

	return RunningPlatformData->Chunks[InChunkIndex].CacheLookupID;
}

void USoundWave::CacheInheritedLoadingBehavior()
{
	check(IsInGameThread());

	// Determine this sound wave's loading behavior and cache it.
	if (LoadingBehavior != ESoundWaveLoadingBehavior::Inherited)
	{
		// If this sound wave specifies it's own loading behavior, use that.
		if (CachedSoundWaveLoadingBehavior == ESoundWaveLoadingBehavior::Uninitialized)
		{
			CachedSoundWaveLoadingBehavior = LoadingBehavior;
		}
	}
 	else if (bLoadingBehaviorOverridden)
 	{
 		ensureMsgf(CachedSoundWaveLoadingBehavior != ESoundWaveLoadingBehavior::Inherited, TEXT("SoundCue set loading behavior to Inherited on SoudWave: %s"), *GetFullName());
 	}
	else
	{
		// if this is true then the behavior should not be Inherited here
		check(!bLoadingBehaviorOverridden);

		USoundClass* CurrentSoundClass = GetSoundClass();
		ESoundWaveLoadingBehavior SoundClassLoadingBehavior = ESoundWaveLoadingBehavior::Inherited;

		// Recurse through this sound class's parents until we find an override.
		while (SoundClassLoadingBehavior == ESoundWaveLoadingBehavior::Inherited && CurrentSoundClass != nullptr)
		{
			SoundClassLoadingBehavior = CurrentSoundClass->Properties.LoadingBehavior;
			CurrentSoundClass = CurrentSoundClass->ParentClass;
		}

		// If we could not find an override in the sound class hierarchy, use the loading behavior defined by our cvar.
		if (SoundClassLoadingBehavior == ESoundWaveLoadingBehavior::Inherited)
		{
			// query the default loading behavior CVar
			ensureAlwaysMsgf(SoundWaveDefaultLoadingBehaviorCVar >= 0 && SoundWaveDefaultLoadingBehaviorCVar < 4, TEXT("Invalid default loading behavior CVar value. Use value 0, 1, 2 or 3."));
			ESoundWaveLoadingBehavior DefaultLoadingBehavior = (ESoundWaveLoadingBehavior)FMath::Clamp<int32>(SoundWaveDefaultLoadingBehaviorCVar, 0, (int32)ESoundWaveLoadingBehavior::LoadOnDemand);

			// override this loading behavior w/ our default
			SoundClassLoadingBehavior = DefaultLoadingBehavior;
			bLoadingBehaviorOverridden = true;
		}

		CachedSoundWaveLoadingBehavior = SoundClassLoadingBehavior;
	}
}

ESoundWaveLoadingBehavior USoundWave::GetLoadingBehavior(bool bCheckSoundClasses /*= true*/) const
{
	checkf(!bCheckSoundClasses || CachedSoundWaveLoadingBehavior != ESoundWaveLoadingBehavior::Uninitialized,
		TEXT("Calling GetLoadingBehavior() is only valid if bCheckSoundClasses is false (which it %s) or CacheInheritedLoadingBehavior has already been called on the game thread. (SoundWave: %s)")
		, bCheckSoundClasses? "is not":"is", *GetFullName());

	if (!bCheckSoundClasses)
	{
		if ((LoadingBehavior != ESoundWaveLoadingBehavior::Inherited && !bLoadingBehaviorOverridden))
		{
			// If this sound wave specifies it's own loading behavior, use that.
			return LoadingBehavior;
		}
		else if (bLoadingBehaviorOverridden)
		{
			// If this sound wave has already had it's loading behavior cached from soundclasses or soundcues, use that.
			return CachedSoundWaveLoadingBehavior;
		}
		else
		{
			// Otherwise, use the loading behavior defined by our cvar.
			ensureAlwaysMsgf(SoundWaveDefaultLoadingBehaviorCVar >= 0 && SoundWaveDefaultLoadingBehaviorCVar < 4, TEXT("Invalid default loading behavior CVar value. Use value 0, 1, 2 or 3."));
			return (ESoundWaveLoadingBehavior)FMath::Clamp<int32>(SoundWaveDefaultLoadingBehaviorCVar, 0, (int32)ESoundWaveLoadingBehavior::LoadOnDemand);
		}
	}
	else if (CachedSoundWaveLoadingBehavior != ESoundWaveLoadingBehavior::Uninitialized)
	{
		return CachedSoundWaveLoadingBehavior;
	}

	// We should never reach this. See check at the beginning of this function.
	return ESoundWaveLoadingBehavior::Inherited;
}
