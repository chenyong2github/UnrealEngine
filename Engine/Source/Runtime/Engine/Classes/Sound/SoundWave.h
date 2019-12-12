// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Playable sound object for raw wave files
 */

#include "CoreMinimal.h"
#include "Containers/IndirectArray.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Async/AsyncWork.h"
#include "Sound/SoundBase.h"
#include "Serialization/BulkData.h"
#include "Serialization/BulkDataBuffer.h"
#include "Sound/SoundGroups.h"
#include "Sound/SoundWaveLoadingBehavior.h"
#include "AudioMixerTypes.h"
#include "AudioCompressionSettings.h"
#include "PerPlatformProperties.h"
#include "ContentStreaming.h"
#include "SoundWave.generated.h"

class ITargetPlatform;
struct FActiveSound;
struct FSoundParseParameters;
struct FPlatformAudioCookOverrides;

UENUM()
enum EDecompressionType
{
	DTYPE_Setup,
	DTYPE_Invalid,
	DTYPE_Preview,
	DTYPE_Native,
	DTYPE_RealTime,
	DTYPE_Procedural,
	DTYPE_Xenon,
	DTYPE_Streaming,
	DTYPE_MAX,
};

/** Precache states */
enum class ESoundWavePrecacheState
{
	NotStarted,
	InProgress,
	Done
};

/**
 * A chunk of streamed audio.
 */
struct FStreamedAudioChunk
{
	/** Size of the chunk of data in bytes including zero padding */
	int32 DataSize;

	/** Size of the audio data. */
	int32 AudioDataSize;

	/** Bulk data if stored in the package. */
	FByteBulkData BulkData;

	/** Default constructor. */
	FStreamedAudioChunk()
	{
	}

	/** Serialization. */
	void Serialize(FArchive& Ar, UObject* Owner, int32 ChunkIndex);

#if WITH_EDITORONLY_DATA
	/** Key if stored in the derived data cache. */
	FString DerivedDataKey;

	/**
	 * Place chunk data in the derived data cache associated with the provided
	 * key.
	 */
	uint32 StoreInDerivedDataCache(const FString& InDerivedDataKey);
#endif // #if WITH_EDITORONLY_DATA
};

/**
 * Platform-specific data used streaming audio at runtime.
 */
USTRUCT()
struct FStreamedAudioPlatformData
{
	GENERATED_USTRUCT_BODY()

	/** Number of audio chunks. */
	int32 NumChunks;
	/** Format in which audio chunks are stored. */
	FName AudioFormat;
	/** audio data. */
	TIndirectArray<struct FStreamedAudioChunk> Chunks;

#if WITH_EDITORONLY_DATA
	/** The key associated with this derived data. */
	FString DerivedDataKey;
	/** Async cache task if one is outstanding. */
	struct FStreamedAudioAsyncCacheDerivedDataTask* AsyncTask;
#endif // WITH_EDITORONLY_DATA

	/** Default constructor. */
	FStreamedAudioPlatformData();

	/** Destructor. */
	~FStreamedAudioPlatformData();

	/**
	 * Try to load audio chunk from the derived data cache or build it if it isn't there.
	 * @param ChunkIndex	The Chunk index to load.
	 * @param OutChunkData	Address of pointer that will store chunk data - should
	 *						either be NULL or have enough space for the chunk
	 * @returns if > 0, the size of the chunk in bytes. If 0, the chunk failed to load.
	 */
	int32 GetChunkFromDDC(int32 ChunkIndex, uint8** OutChunkData, bool bMakeSureChunkIsLoaded = false);

	

	/** Serialization. */
	void Serialize(FArchive& Ar, class USoundWave* Owner);

#if WITH_EDITORONLY_DATA
	void Cache(class USoundWave& InSoundWave, const FPlatformAudioCookOverrides* CompressionOverrides, FName AudioFormatName, uint32 InFlags);
	void FinishCache();
	bool IsFinishedCache() const;
	ENGINE_API bool TryInlineChunkData();
	bool AreDerivedChunksAvailable() const;
#endif // WITH_EDITORONLY_DATA

private:

	/**
	 * Takes the results of a DDC operation and deserializes it into an FStreamedAudioChunk struct.
	 * @param SerializedData Serialized data resulting from DDC.GetAsynchronousResults or DDC.GetSynchronous.
	 * @param ChunkToDeserializeInto is the chunk to fill with the deserialized data.
	 * @param ChunkIndex is the index of the chunk in this instance of FStreamedAudioPlatformData.
	 * @param bCachedChunk is true if the chunk was successfully cached, false otherwise.
	 * @param OutChunkData is a pointer to a pointer to populate with the chunk itself, or if pointing to nullptr, returns an allocated buffer.
	 * @returns the size of the chunk loaded in bytes, or zero if the chunk didn't load.
	 */
	int32 DeserializeChunkFromDDC(TArray<uint8> SerializedData, FStreamedAudioChunk &ChunkToDeserializeInto, int32 ChunkIndex, uint8** &OutChunkData);
};

USTRUCT(BlueprintType)
struct FSoundWaveSpectralData
{
	GENERATED_USTRUCT_BODY()

	// The frequency (in Hz) of the spectrum value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	float FrequencyHz;

	// The magnitude of the spectrum at this frequency
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	float Magnitude;

	// The normalized magnitude of the spectrum at this frequency
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	float NormalizedMagnitude;

	FSoundWaveSpectralData()
		: FrequencyHz(0.0f)
		, Magnitude(0.0f)
		, NormalizedMagnitude(0.0f)
	{}
};

USTRUCT(BlueprintType)
struct FSoundWaveSpectralDataPerSound
{
	GENERATED_USTRUCT_BODY()

	// The array of current spectral data for this sound wave
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	TArray<FSoundWaveSpectralData> SpectralData;

	// The current playback time of this sound wave
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	float PlaybackTime;

	// The sound wave this spectral data is associated with
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	USoundWave* SoundWave;
};

USTRUCT(BlueprintType)
struct FSoundWaveEnvelopeDataPerSound
{
	GENERATED_USTRUCT_BODY()

	// The current envelope of the playing sound
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnvelopeData")
	float Envelope;

	// The current playback time of this sound wave
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnvelopeData")
	float PlaybackTime;

	// The sound wave this envelope data is associated with
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnvelopeData")
	USoundWave* SoundWave;
};

// Sort predicate for sorting spectral data by frequency (lowest first)
struct FCompareSpectralDataByFrequencyHz
{
	FORCEINLINE bool operator()(const FSoundWaveSpectralData& A, const FSoundWaveSpectralData& B) const
	{
		return A.FrequencyHz < B.FrequencyHz;
	}
};


// Struct used to store spectral data with time-stamps
USTRUCT()
struct FSoundWaveSpectralDataEntry
{
	GENERATED_USTRUCT_BODY()

	// The magnitude of the spectrum at this frequency
	UPROPERTY()
	float Magnitude;

	// The normalized magnitude of the spectrum at this frequency
	UPROPERTY()
	float NormalizedMagnitude;
};


// Struct used to store spectral data with time-stamps
USTRUCT()
struct FSoundWaveSpectralTimeData
{
	GENERATED_USTRUCT_BODY()

	// The spectral data at the given time. The array indices correspond to the frequencies set to analyze.
	UPROPERTY()
	TArray<FSoundWaveSpectralDataEntry> Data;

	// The timestamp associated with this spectral data
	UPROPERTY()
	float TimeSec;

	FSoundWaveSpectralTimeData()
		: TimeSec(0.0f)
	{}
};

// Struct used to store time-stamped envelope data
USTRUCT()
struct FSoundWaveEnvelopeTimeData
{
	GENERATED_USTRUCT_BODY()

	// The normalized linear amplitude of the audio
	UPROPERTY()
	float Amplitude;

	// The timestamp of the audio
	UPROPERTY()
	float TimeSec;

	FSoundWaveEnvelopeTimeData()
		: Amplitude(0.0f)
		, TimeSec(0.0f)
	{}
};

// The FFT size (in audio frames) to use for baked FFT analysis
UENUM(BlueprintType)
enum class ESoundWaveFFTSize : uint8
{
	VerySmall_64,
	Small_256,
	Medium_512,
	Large_1024,
	VeryLarge_2048,
};

struct ISoundWaveClient
{
	ISoundWaveClient() {}
	virtual ~ISoundWaveClient() {}
	
	virtual void OnBeginDestroy(class USoundWave* Wave) = 0;
	virtual bool OnIsReadyForFinishDestroy(class USoundWave* Wave) const = 0;
	virtual void OnFinishDestroy(class USoundWave* Wave) = 0;
};

UCLASS(hidecategories=Object, editinlinenew, BlueprintType)
class ENGINE_API USoundWave : public USoundBase
{
	GENERATED_UCLASS_BODY()
public:
	/** Platform agnostic compression quality. 1..100 with 1 being best compression and 100 being best quality. */
	UPROPERTY(EditAnywhere, Category="Format|Quality", meta=(DisplayName = "Compression", ClampMin = "1", ClampMax = "100"), AssetRegistrySearchable)
	int32 CompressionQuality;

	/** Priority of this sound when streaming (lower priority streams may not always play) */
	UPROPERTY(EditAnywhere, Category="Playback|Streaming", meta=(ClampMin=0))
	int32 StreamingPriority;

	/** Quality of sample rate conversion for platforms that opt into resampling during cook. */
	UPROPERTY(EditAnywhere, Category = "Format|Quality", meta=(DisplayName="Sample Rate"))
	ESoundwaveSampleRateSettings SampleRateQuality;

	/** Type of buffer this wave uses. Set once on load */
	TEnumAsByte<EDecompressionType> DecompressionType;

	UPROPERTY(EditAnywhere, Category=Sound, meta=(DisplayName="Group"))
	TEnumAsByte<ESoundGroup> SoundGroup;

	/** If set, when played directly (not through a sound cue) the wave will be played looping. */
	UPROPERTY(EditAnywhere, Category=Sound, AssetRegistrySearchable)
	uint8 bLooping:1;

	/** Whether this sound can be streamed to avoid increased memory usage. If using Stream Caching, use Loading Behavior instead to control memory usage. */
	UPROPERTY(EditAnywhere, Category="Playback|Streaming", meta = (DisplayName = "Force Streaming"))
	uint8 bStreaming:1;

	/** Whether this sound supports seeking. This requires recooking with a codec which supports seekability and streaming. */
	UPROPERTY(EditAnywhere, Category = "Playback|Streaming", meta = (DisplayName = "Seekable", EditCondition = "bStreaming"))
	uint8 bSeekableStreaming:1;

	/** Specifies how and when compressed audio data is loaded for asset if stream caching is enabled. */
	UPROPERTY(EditAnywhere, Category = "Loading", meta = (DisplayName = "Loading Behavior Override"))
	ESoundWaveLoadingBehavior LoadingBehavior;

	/** Set to true for programmatically generated audio. */
	uint8 bProcedural:1;

	/** Set to true of this is a bus sound source. This will result in the sound wave not generating audio for itself, but generate audio through instances. Used only in audio mixer. */
	uint8 bIsBus:1;

	/** Set to true for procedural waves that can be processed asynchronously. */
	uint8 bCanProcessAsync:1;

	/** Whether to free the resource data after it has been uploaded to the hardware */
	uint8 bDynamicResource:1;

	/** If set to true if this sound is considered to contain mature/adult content. */
	UPROPERTY(EditAnywhere, Category=Subtitles, AssetRegistrySearchable)
	uint8 bMature:1;

	/** If set to true will disable automatic generation of line breaks - use if the subtitles have been split manually. */
	UPROPERTY(EditAnywhere, Category=Subtitles )
	uint8 bManualWordWrap:1;

	/** If set to true the subtitles display as a sequence of single lines as opposed to multiline. */
	UPROPERTY(EditAnywhere, Category=Subtitles )
	uint8 bSingleLine:1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	uint8 bVirtualizeWhenSilent_DEPRECATED:1;
#endif // WITH_EDITORONLY_DATA

	/** Whether or not this source is ambisonics file format. If set, sound always uses the 
	  * 'Master Ambisonics Submix' as set in the 'Audio' category of Project Settings'
	  * and ignores submix if provided locally or in the referenced SoundClass. */
	UPROPERTY(EditAnywhere, Category = Format)
	uint8 bIsAmbisonics : 1;

	/** Whether this SoundWave was decompressed from OGG. */
	uint8 bDecompressedFromOgg : 1;

#if WITH_EDITOR
	/** The current revision of our compressed audio data. Used to tell when a chunk in the cache is stale. */
	FThreadSafeCounter CurrentChunkRevision;
#endif

private:

	// This is set to false on initialization, then set to true on non-editor platforms when we cache appropriate sample rate.
	uint8 bCachedSampleRateFromPlatformSettings:1;

	// This is set when SetSampleRate is called to invalidate our cached sample rate while not re-parsing project settings.
	uint8 bSampleRateManuallyReset:1;

#if WITH_EDITOR
	// Whether this was previously cooked with stream caching enabled.
	uint8 bWasStreamCachingEnabledOnLastCook:1;
#endif // !WITH_EDITOR

	enum class ESoundWaveResourceState : uint8
	{
		NeedsFree,
		Freeing,
		Freed
	};

	volatile ESoundWaveResourceState ResourceState;

public:

	using FSoundWaveClientPtr = ISoundWaveClient*;

#if WITH_EDITORONLY_DATA
	/** Specify a sound to use for the baked analysis. Will default to this USoundWave if not sete. */
	UPROPERTY(EditAnywhere, Category = "Analysis")
	USoundWave* OverrideSoundToUseForAnalysis;

	/**
		Whether or not we should treat the sound wave used for analysis (this or the override) as looping while performing analysis.
		A looping sound may include the end of the file for inclusion in analysis for envelope and FFT analysis.
	*/
	UPROPERTY(EditAnywhere, Category = "Analysis")
	uint8 TreatFileAsLoopingForAnalysis:1;

	/** Whether or not to enable cook-time baked FFT analysis. */
	UPROPERTY(EditAnywhere, Category = "Analysis|FFT")
	uint8 bEnableBakedFFTAnalysis : 1;

	/** Whether or not to enable cook-time amplitude envelope analysis. */
	UPROPERTY(EditAnywhere, Category = "Analysis|Envelope")
	uint8 bEnableAmplitudeEnvelopeAnalysis : 1;

	/** The FFT window size to use for fft analysis. */
	UPROPERTY(EditAnywhere, Category = "Analysis|FFT", meta = (EditCondition = "bEnableBakedFFTAnalysis"))
	ESoundWaveFFTSize FFTSize;

	/** How many audio frames analyze at a time. */
	UPROPERTY(EditAnywhere, Category = "Analysis|FFT", meta = (EditCondition = "bEnableBakedFFTAnalysis", ClampMin = "512", UIMin = "512"))
	int32 FFTAnalysisFrameSize;

	/** Attack time in milliseconds of the spectral envelope follower. */
	UPROPERTY(EditAnywhere, Category = "Analysis|FFT", meta = (EditCondition = "bEnableBakedFFTAnalysis", ClampMin = "0", UIMin = "0"))
	int32 FFTAnalysisAttackTime;

	/** Release time in milliseconds of the spectral envelope follower. */
	UPROPERTY(EditAnywhere, Category = "Analysis|FFT", meta = (EditCondition = "bEnableBakedFFTAnalysis", ClampMin = "0", UIMin = "0"))
	int32 FFTAnalysisReleaseTime;

	/** How many audio frames to average a new envelope value. Larger values use less memory for audio envelope data but will result in lower envelope accuracy. */
	UPROPERTY(EditAnywhere, Category = "Analysis|Envelope", meta = (EditCondition = "bEnableAmplitudeEnvelopeAnalysis", ClampMin = "512", UIMin = "512"))
	int32 EnvelopeFollowerFrameSize;

	/** The attack time in milliseconds. Describes how quickly the envelope analyzer responds to increasing amplitudes. */
	UPROPERTY(EditAnywhere, Category = "Analysis|Envelope", meta = (EditCondition = "bEnableAmplitudeEnvelopeAnalysis", ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerAttackTime;

	/** The release time in milliseconds. Describes how quickly the envelope analyzer responds to decreasing amplitudes. */
	UPROPERTY(EditAnywhere, Category = "Analysis|Envelope", meta = (EditCondition = "bEnableAmplitudeEnvelopeAnalysis", ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerReleaseTime;
#endif // WITH_EDITORONLY_DATA

	/** The frequencies (in hz) to analyze when doing baked FFT analysis. */
	UPROPERTY(EditAnywhere, Category = "Analysis|FFT", meta = (EditCondition = "bEnableBakedFFTAnalysis"))
	TArray<float> FrequenciesToAnalyze;

	/** The cooked spectral time data. */
	UPROPERTY()
	TArray<FSoundWaveSpectralTimeData> CookedSpectralTimeData;

	/** The cooked cooked envelope data. */
	UPROPERTY()
	TArray<FSoundWaveEnvelopeTimeData> CookedEnvelopeTimeData;

	/** Helper function to get interpolated cooked FFT data for a given time value. */
	bool GetInterpolatedCookedFFTDataForTime(float InTime, uint32& InOutLastIndex, TArray<FSoundWaveSpectralData>& OutData, bool bLoop);
	bool GetInterpolatedCookedEnvelopeDataForTime(float InTime, uint32& InOutLastIndex, float& OutAmplitude, bool bLoop);

	/** If stream caching is enabled, allows the user to retain a strong handle to the first chunk of audio in the cache. 
	 *  Please note that this USoundWave is NOT guaranteed to be still alive when OnLoadCompleted is called.
	 */
	void GetHandleForChunkOfAudio(TFunction<void(FAudioChunkHandle)> OnLoadCompleted, bool bForceSync = false, int32 ChunkIndex = 1, ENamedThreads::Type CallbackThread = ENamedThreads::GameThread);

	/** If stream caching is enabled, set this sound wave to retain a strong handle to its first chunk. 
	 *  If not called on the game thread, bForceSync must be true.
	*/
	void RetainCompressedAudio(bool bForceSync = false);

	/** If stream caching is enabled and au.streamcache.KeepFirstChunkInMemory is 1, this will release this USoundWave's first chunk, allowing it to be deleted. */
	void ReleaseCompressedAudio();

	/** Returns the loading behavior we should use for this sound wave.
	 *  If this is called within Serialize(), this should be called with bCheckSoundClasses = false,
	 *  Since there is no guarantee that the deserialized USoundClasses have been resolved yet.
	 */
	ESoundWaveLoadingBehavior GetLoadingBehavior(bool bCheckSoundClasses = true) const;

	/** Use this to override how much audio data is loaded when this USoundWave is loaded. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Loading")
	int32 InitialChunkSize;

private:

	/** Helper functions to search analysis data. Takes starting index to start query. Returns which data index the result was found at. Returns INDEX_NONE if not found. */
	uint32 GetInterpolatedCookedFFTDataForTimeInternal(float InTime, uint32 StartingIndex, TArray<FSoundWaveSpectralData>& OutData, bool bLoop);
	uint32 GetInterpolatedCookedEnvelopeDataForTimeInternal(float InTime, uint32 StartingIndex, float& OutAmplitude, bool bLoop);

	/** What state the precache decompressor is in. */
	FThreadSafeCounter PrecacheState;

	/** the number of sounds currently playing this sound wave. */
	mutable FCriticalSection SourcesPlayingCs;

	TArray<FSoundWaveClientPtr> SourcesPlaying;

	// This is the sample rate retrieved from platform settings.
	float CachedSampleRateOverride;

	// We cache a soundwave's loading behavior on the first call to USoundWave::GetLoadingBehaviorForWave(true);
	// Caches resolved loading behavior from the SoundClass graph. Must be called on the game thread.
	void CacheInheritedLoadingBehavior();
	ESoundWaveLoadingBehavior CachedSoundWaveLoadingBehavior;

public:

	/** A localized version of the text that is actually spoken phonetically in the audio. */
	UPROPERTY(EditAnywhere, Category=Subtitles )
	FString SpokenText;

	/** The priority of the subtitle. */
	UPROPERTY(EditAnywhere, Category=Subtitles)
	float SubtitlePriority;

	/** Playback volume of sound 0 to 1 - Default is 1.0. */
	UPROPERTY(Category=Sound, meta=(ClampMin = "0.0"), EditAnywhere)
	float Volume;

	/** Playback pitch for sound. */
	UPROPERTY(Category=Sound, meta=(ClampMin = "0.125", ClampMax = "4.0"), EditAnywhere)
	float Pitch;

	/** Number of channels of multichannel data; 1 or 2 for regular mono and stereo files */
	UPROPERTY(Category=Info, AssetRegistrySearchable, VisibleAnywhere)
	int32 NumChannels;

#if WITH_EDITORONLY_DATA
	/** Offsets into the bulk data for the source wav data */
	UPROPERTY()
	TArray<int32> ChannelOffsets;

	/** Sizes of the bulk data for the source wav data */
	UPROPERTY()
	TArray<int32> ChannelSizes;

#endif // WITH_EDITORONLY_DATA

protected:

	/** Cached sample rate for displaying in the tools */
	UPROPERTY(Category = Info, AssetRegistrySearchable, VisibleAnywhere)
	int32 SampleRate;

public:

	/** Resource index to cross reference with buffers */
	int32 ResourceID;

	/** Size of resource copied from the bulk data */
	int32 ResourceSize;

	/** Cache the total used memory recorded for this SoundWave to keep INC/DEC consistent */
	int32 TrackedMemoryUsage;

	/**
	 * Subtitle cues.  If empty, use SpokenText as the subtitle.  Will often be empty,
	 * as the contents of the subtitle is commonly identical to what is spoken.
	 */
	UPROPERTY(EditAnywhere, Category=Subtitles)
	TArray<struct FSubtitleCue> Subtitles;

#if WITH_EDITORONLY_DATA
	/** Provides contextual information for the sound to the translator. */
	UPROPERTY(EditAnywhere, Category=Subtitles )
	FString Comment;

#endif // WITH_EDITORONLY_DATA

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FString SourceFilePath_DEPRECATED;
	UPROPERTY()
	FString SourceFileTimestamp_DEPRECATED;

	UPROPERTY(VisibleAnywhere, Instanced, Category=ImportSettings)
	class UAssetImportData* AssetImportData;

#endif // WITH_EDITORONLY_DATA

protected:

	/** Curves associated with this sound wave */
	UPROPERTY(EditAnywhere, Category = SoundWave, AdvancedDisplay)
	class UCurveTable* Curves;

	/** Hold a reference to our internal curve so we can switch back to it if we want to */
	UPROPERTY()
	class UCurveTable* InternalCurves;

	/** Potential strong handle to the first chunk of audio data. Can be released via ReleaseCompressedAudioData. */
	FAudioChunkHandle FirstChunk;

private:

	/**
	* helper function for getting the cached name of the current platform.
	*/
	static ITargetPlatform* GetRunningPlatform();

public:
	/** Async worker that decompresses the audio data on a different thread */
	typedef FAsyncTask< class FAsyncAudioDecompressWorker > FAsyncAudioDecompress;	// Forward declare typedef
	FAsyncAudioDecompress* AudioDecompressor;

	/** Pointer to 16 bit PCM data - used to avoid synchronous operation to obtain first block of the realtime decompressed buffer */
	uint8* CachedRealtimeFirstBuffer;

	/** The number of frames which have been precached for this sound wave. */
	int32 NumPrecacheFrames;

	/** Size of RawPCMData, or what RawPCMData would be if the sound was fully decompressed */
	int32 RawPCMDataSize;

	/** Pointer to 16 bit PCM data - used to decompress data to and preview sounds */
	uint8* RawPCMData;

	/** Memory containing the data copied from the compressed bulk data */
	FOwnedBulkDataPtr* OwnedBulkDataPtr;
	const uint8* ResourceData;

	/** Zeroth Chunk of audio for sources that use Load On Demand. */
	FBulkDataBuffer<uint8> ZerothChunkData;

	/** Uncompressed wav data 16 bit in mono or stereo - stereo not allowed for multichannel data */
	FByteBulkData RawData;

	/** GUID used to uniquely identify this node so it can be found in the DDC */
	FGuid CompressedDataGuid;

	FFormatContainer CompressedFormatData;

#if WITH_EDITORONLY_DATA
	TMap<FName, uint32> AsyncLoadingDataFormats;

	/** FByteBulkData doesn't currently support readonly access from multiple threads, so we limit access to RawData with a critical section on cook. */
	FCriticalSection RawDataCriticalSection;

#endif // WITH_EDITORONLY_DATA

	/** The streaming derived data for this sound on this platform. */
	FStreamedAudioPlatformData* RunningPlatformData;

	/** cooked streaming platform data for this sound */
	TSortedMap<FString, FStreamedAudioPlatformData*> CookedPlatformData;

	//~ Begin UObject Interface.
	virtual void Serialize( FArchive& Ar ) override;
	virtual void PostInitProperties() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual void FinishDestroy() override;
	virtual void PostLoad() override;

	// When stream caching is enabled, this is called after we've successfully compressed and split the streamed audio for this file.
	void EnsureZerothChunkIsLoaded();

	// Returns the amount of chunks this soundwave contains if it's streaming,
	// or zero if it is not a streaming source.
	uint32 GetNumChunks() const;

	uint32 GetSizeOfChunk(uint32 ChunkIndex);

	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	virtual FName GetExporterName() override;
	virtual FString GetDesc() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	//~ End UObject Interface.

	//~ Begin USoundBase Interface.
	virtual bool IsPlayable() const override;
	virtual void Parse( class FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances ) override;
	virtual float GetDuration() override;
	virtual float GetSubtitlePriority() const override;
	virtual bool SupportsSubtitles() const override;
	virtual bool GetSoundWavesWithCookedAnalysisData(TArray<USoundWave*>& OutSoundWaves) override;
	virtual bool HasCookedFFTData() const override;
	virtual bool HasCookedAmplitudeEnvelopeData() const override;
	//~ End USoundBase Interface.

	// Called  when the procedural sound wave begins on the render thread. Only used in the audio mixer and when bProcedural is true.
	virtual void OnBeginGenerate() {}

	// Called when the procedural sound wave is done generating on the render thread. Only used in the audio mixer and when bProcedural is true..
	virtual void OnEndGenerate() {};

	void AddPlayingSource(const FSoundWaveClientPtr& Source);
	void RemovePlayingSource(const FSoundWaveClientPtr& Source);

	/** the number of sounds currently playing this sound wave. */
	FThreadSafeCounter NumSourcesPlaying;

	void AddPlayingSource()
	{
		NumSourcesPlaying.Increment();
	}

	void RemovePlayingSource()
	{
		check(NumSourcesPlaying.GetValue() > 0);
		NumSourcesPlaying.Decrement();
	}

	bool IsGeneratingAudio() const
	{		
		bool bIsGeneratingAudio = false;
		FScopeLock Lock(&SourcesPlayingCs);
		bIsGeneratingAudio = SourcesPlaying.Num() > 0;
		
		return bIsGeneratingAudio;
	}

	/**
	* Overwrite sample rate. Used for procedural soundwaves, as well as sound waves that are resampled on compress/decompress.
	*/

	void SetSampleRate(uint32 InSampleRate)
	{
		SampleRate = InSampleRate;
#if !WITH_EDITOR
		// Ensure that we invalidate our cached sample rate if the UProperty sample rate is changed.
		bCachedSampleRateFromPlatformSettings = false;
		bSampleRateManuallyReset = true;
#endif //WITH_EDITOR
	}

	/**
	 *	@param		Format		Format to check
	 *
	 *	@return		Sum of the size of waves referenced by this cue for the given platform.
	 */
	virtual int32 GetResourceSizeForFormat(FName Format);

	/**
	 * Frees up all the resources allocated in this class.
	 * @param bStopSoundsUsingThisResource if false, will leave any playing audio alive.
	 *        This occurs when we force a re-cook of audio while starting to play a sound.
	 */
	void FreeResources(bool bStopSoundsUsingThisResource = true);

	/** Will clean up the decompressor task if the task has finished or force it finish. Returns true if the decompressor is cleaned up. */
	bool CleanupDecompressor(bool bForceCleanup = false);

	/**
	 * Copy the compressed audio data from the bulk data
	 */
	virtual void InitAudioResource( FByteBulkData& CompressedData );

	/**
	 * Copy the compressed audio data from derived data cache
	 *
	 * @param Format to get the compressed audio in
	 * @return true if the resource has been successfully initialized or it was already initialized.
	 */
	virtual bool InitAudioResource(FName Format);

	/**
	 * Remove the compressed audio data associated with the passed in wave
	 */
	void RemoveAudioResource();

	/**
	 * Prints the subtitle associated with the SoundWave to the console
	 */
	void LogSubtitle( FOutputDevice& Ar );

	/**
	 * Handle any special requirements when the sound starts (e.g. subtitles)
	 */
	FWaveInstance& HandleStart(FActiveSound& ActiveSound, const UPTRINT WaveInstanceHash) const;

	/**
	 * This is only used for DTYPE_Procedural audio. It's recommended to use USynthComponent base class
	 * for procedurally generated sound vs overriding this function. If a new component is not feasible,
	 * consider using USoundWaveProcedural base class vs USoundWave base class since as it implements
	 * GeneratePCMData for you and you only need to return PCM data.
	 */
	virtual int32 GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded) { ensure(false); return 0; }

	/**
	* Return the format of the generated PCM data type. Used in audio mixer to allow generating float buffers and avoid unnecessary format conversions.
	* This feature is only supported in audio mixer. If your procedural sound wave needs to be used in both audio mixer and old audio engine,
	* it's best to generate int16 data as old audio engine only supports int16 formats. Or check at runtime if the audio mixer is enabled.
	* Audio mixer will convert from int16 to float internally.
	*/
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const { return Audio::EAudioMixerStreamDataFormat::Int16; }

	/**
	 * Gets the compressed data size from derived data cache for the specified format
	 *
	 * @param Format	format of compressed data
	 * @param CompressionOverrides Optional argument for compression overrides.
	 * @return			compressed data size, or zero if it could not be obtained
	 */
	int32 GetCompressedDataSize(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides = GetPlatformCompressionOverridesForCurrentPlatform())
	{
		FByteBulkData* Data = GetCompressedData(Format, CompressionOverrides);
		return Data ? Data->GetBulkDataSize() : 0;
	}

	virtual bool HasCompressedData(FName Format, ITargetPlatform* TargetPlatform = GetRunningPlatform()) const;

#if WITH_EDITOR
	/** Utility which returns imported PCM data and the parsed header for the file. Returns true if there was data, false if there wasn't. */
	bool GetImportedSoundWaveData(TArray<uint8>& OutRawPCMData, uint32& OutSampleRate, uint16& OutNumChannels);

	/**
	 * This function can be called before playing or using a SoundWave to check if any cook settings have been modified since this SoundWave was last cooked.
	 */
	void InvalidateSoundWaveIfNeccessary();
#endif //WITH_EDITOR

private:

	FName GetPlatformSpecificFormat(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides);

#if WITH_EDITOR
	void BakeFFTAnalysis();
	void BakeEnvelopeAnalysis();
#endif //WITH_EDITOR

public:

#if WITH_EDITOR
	void LogBakedData();
#endif //WITH_EDITOR

	virtual void BeginGetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides);

	/**
	 * Gets the compressed data from derived data cache for the specified platform
	 * Warning, the returned pointer isn't valid after we add new formats
	 *
	 * @param Format	format of compressed data
	 * @param PlatformName optional name of platform we are getting compressed data for.
	 * @param CompressionOverrides optional platform compression overrides
	 * @return	compressed data, if it could be obtained
	 */
	virtual FByteBulkData* GetCompressedData(FName Format, const FPlatformAudioCookOverrides* CompressionOverrides = GetPlatformCompressionOverridesForCurrentPlatform());

	/**
	 * Change the guid and flush all compressed data
	 * @param bFreeResources if true, will delete any precached compressed data as well.
	 */
	void InvalidateCompressedData(bool bFreeResources = false, bool bRebuildStreamingChunks = true);

	/** Returns curves associated with this sound wave */
	virtual class UCurveTable* GetCurveData() const override { return Curves; }

	// This function returns true if there are streamable chunks in this asset.
	bool HasStreamingChunks();

#if WITH_EDITOR
	/** These functions are required for support for some custom details/editor functionality.*/

	/** Returns internal curves associated with this sound wave */
	class UCurveTable* GetInternalCurveData() const { return InternalCurves; }

	/** Returns whether this sound wave has internal curves. */
	bool HasInternalCurves() const { return InternalCurves != nullptr; }

	/** Sets the curve data for this sound wave. */
	void SetCurveData(UCurveTable* InCurves) { Curves = InCurves; }

	/** Sets the internal curve data for this sound wave. */
	void SetInternalCurveData(UCurveTable* InCurves) { InternalCurves = InCurves; }

	/** Gets the member name for the Curves property of the USoundWave object. */
	static FName GetCurvePropertyName() { return GET_MEMBER_NAME_CHECKED(USoundWave, Curves); }
#endif // WITH_EDITOR

	/** Checks whether sound has been categorised as streaming. */
	bool IsStreaming(const FPlatformAudioCookOverrides* Overrides = nullptr) const;

	/** Checks whether sound has seekable streaming enabled. */
	bool IsSeekableStreaming() const;
	/**
	 * Checks whether we should use the load on demand cache.
	 */
	bool ShouldUseStreamCaching() const;

	/**
	 * This returns the initial chunk of compressed data for streaming data sources.
	 */
	TArrayView<const uint8> GetZerothChunk();

	/**
	 * Attempts to update the cached platform data after any changes that might affect it
	 */
	void UpdatePlatformData();

	void CleanupCachedRunningPlatformData();

	/**
	 * Serializes cooked platform data.
	 */
	void SerializeCookedPlatformData(class FArchive& Ar);

	/*
	* Returns a sample rate if there is a specific sample rate override for this platform, -1.0 otherwise.
	*/
	float GetSampleRateForCurrentPlatform();

	/**
	* Return the platform compression overrides set for the current platform.
	*/
	static const FPlatformAudioCookOverrides* GetPlatformCompressionOverridesForCurrentPlatform();

	/*
	* Returns a sample rate if there is a specific sample rate override for this platform, -1.0 otherwise.
	*/
	float GetSampleRateForCompressionOverrides(const FPlatformAudioCookOverrides* CompressionOverrides);

#if WITH_EDITORONLY_DATA

#if WITH_EDITOR
	/*
	* Returns a sample rate if there is a specific sample rate override for this platform, -1.0 otherwise.
	*/
	float GetSampleRateForTargetPlatform(const ITargetPlatform* TargetPlatform);

	/**
	 * Begins caching platform data in the background for the platform requested
	 */
	virtual void BeginCacheForCookedPlatformData(  const ITargetPlatform *TargetPlatform ) override;

	virtual bool IsCachedCookedPlatformDataLoaded( const ITargetPlatform* TargetPlatform ) override;

	/**
	 * Clear all the cached cooked platform data which we have accumulated with BeginCacheForCookedPlatformData calls
	 * The data can still be cached again using BeginCacheForCookedPlatformData again
	 */
	virtual void ClearAllCachedCookedPlatformData() override;

	virtual void ClearCachedCookedPlatformData( const ITargetPlatform* TargetPlatform ) override;

	virtual void WillNeverCacheCookedPlatformDataAgain() override;

	uint32 bNeedsThumbnailGeneration:1;
#endif // WITH_EDITOR

	/**
	 * Caches platform data for the sound.
	 */
	void CachePlatformData(bool bAsyncCache = false);

	/**
	 * Begins caching platform data in the background.
	 */
	void BeginCachePlatformData();

	/**
	 * Blocks on async cache tasks and prepares platform data for use.
	 */
	void FinishCachePlatformData();

	/**
	 * Forces platform data to be rebuilt.
	 */
	void ForceRebuildPlatformData();
#endif // WITH_EDITORONLY_DATA

	/**
	 * Get Chunk data for a specified chunk index.
	 * @param ChunkIndex	The Chunk index to cache.
	 * @param OutChunkData	Address of pointer that will store data.
	 */
	bool GetChunkData(int32 ChunkIndex, uint8** OutChunkData, bool bMakeSureChunkIsLoaded = false);

	void SetPrecacheState(ESoundWavePrecacheState InState)
	{
		PrecacheState.Set((int32)InState);
	}

	ESoundWavePrecacheState GetPrecacheState() const
	{
		return (ESoundWavePrecacheState)PrecacheState.GetValue();
	}

};



