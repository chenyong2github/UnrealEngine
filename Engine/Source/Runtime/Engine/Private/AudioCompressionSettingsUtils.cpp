// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioCompressionSettingsUtils.h"
#include "AudioCompressionSettings.h"

#define ENABLE_PLATFORM_COMPRESSION_OVERRIDES 1
#define PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES (PLATFORM_ANDROID && !PLATFORM_LUMIN) || PLATFORM_IOS || PLATFORM_SWITCH || PLATFORM_XBOXONE || PLATFORM_PS4 || PLATFORM_WINDOWS

#if PLATFORM_ANDROID && !PLATFORM_LUMIN && ENABLE_PLATFORM_COMPRESSION_OVERRIDES
#include "AndroidRuntimeSettings.h"
#endif

#if PLATFORM_IOS && ENABLE_PLATFORM_COMPRESSION_OVERRIDES
#include "IOSRuntimeSettings.h"
#endif

#if PLATFORM_SWITCH && ENABLE_PLATFORM_COMPRESSION_OVERRIDES
#include "SwitchRuntimeSettings.h"
#endif

#include "Misc/ConfigCacheIni.h"

static float CookOverrideCachingIntervalCvar = 1.0f;
FAutoConsoleVariableRef CVarCookOverrideCachingIntervalCVar(
	TEXT("au.editor.CookOverrideCachingInterval"),
	CookOverrideCachingIntervalCvar,
	TEXT("This sets the max latency between when a cook override is changed in the project settings and when it is applied to new audio sources.\n")
	TEXT("n: Time between caching intervals, in seconds."),
	ECVF_Default);


/**
 * This value is the minimum potential usage of the stream cache we feasibly want to support.
 * Setting this to 0.25, for example, cause us to potentially be using 25% of our cache size when we start evicting chunks, worst cast scenario.
 * The trade off is that when this is increased, we add more elements to our cache, thus linearly increasing the CPU complexity of finding a chunk.
 * A minimum cache usage of 1.0f is impossible, because it would require an infinite amount of chunks.
 */
static float MinimumCacheUsageCvar = 0.75f;
FAutoConsoleVariableRef CVarMinimumCacheUsage(
	TEXT("au.streamcaching.MinimumCacheUsage"),
	MinimumCacheUsageCvar,
	TEXT("This value is the minimum potential usage of the stream cache we feasibly want to support. Setting this to 0.25, for example, cause us to potentially be using 25% of our cache size when we start evicting chunks, worst cast scenario.\n")
	TEXT("0.0: limit the number of chunks to our (Cache Size / Max Chunk Size) [0.01-0.99]: Increase our number of chunks to limit disk IO when we have lots of small sounds playing."),
	ECVF_Default);

const FPlatformRuntimeAudioCompressionOverrides* FPlatformCompressionUtilities::GetRuntimeCompressionOverridesForCurrentPlatform()
{
#if PLATFORM_ANDROID && !PLATFORM_LUMIN && ENABLE_PLATFORM_COMPRESSION_OVERRIDES
	static const UAndroidRuntimeSettings* Settings = GetDefault<UAndroidRuntimeSettings>();
	if (Settings)
	{
		return &(Settings->CompressionOverrides);
	}

#elif PLATFORM_IOS && ENABLE_PLATFORM_COMPRESSION_OVERRIDES
	static const UIOSRuntimeSettings* Settings = GetDefault<UIOSRuntimeSettings>();

	if (Settings)
	{
		return &(Settings->CompressionOverrides);
	}

#elif PLATFORM_SWITCH && ENABLE_PLATFORM_COMPRESSION_OVERRIDES
	static const USwitchRuntimeSettings* Settings = GetDefault<USwitchRuntimeSettings>();

	if (Settings)
	{
		return &(Settings->CompressionOverrides);
	}

#endif // PLATFORM_ANDROID && !PLATFORM_LUMIN
	return nullptr;
}

void CacheCurrentPlatformAudioCookOverrides(FPlatformAudioCookOverrides& OutOverrides)
{
#if PLATFORM_ANDROID && !PLATFORM_LUMIN
	const TCHAR* CategoryName = TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");
#elif PLATFORM_IOS
	const TCHAR* CategoryName = TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings");
#elif PLATFORM_SWITCH
	const TCHAR* CategoryName = TEXT("/Script/SwitchRuntimeSettings.SwitchRuntimeSettings");
#elif PLATFORM_WINDOWS
	const TCHAR* CategoryName = TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");
#elif PLATFORM_PS4
	const TCHAR* CategoryName = TEXT("/Script/PS4PlatformEditor.PS4TargetSettings");
#elif PLATFORM_XBOXONE
	const TCHAR* CategoryName = TEXT("/Script/XboxOnePlatformEditor.XboxOneTargetSettings");
#else
	const TCHAR* CategoryName = TEXT("");
#endif

	int32 SoundCueCookQualityIndex = INDEX_NONE;
	if (GConfig->GetInt(CategoryName, TEXT("SoundCueCookQualityIndex"), SoundCueCookQualityIndex, GEngineIni))
	{
		OutOverrides.SoundCueCookQualityIndex = SoundCueCookQualityIndex;
	}
	
	GConfig->GetBool(CategoryName, TEXT("bResampleForDevice"), OutOverrides.bResampleForDevice, GEngineIni);

	GConfig->GetFloat(CategoryName, TEXT("CompressionQualityModifier"), OutOverrides.CompressionQualityModifier, GEngineIni);

	GConfig->GetFloat(CategoryName, TEXT("AutoStreamingThreshold"), OutOverrides.AutoStreamingThreshold, GEngineIni);

	GConfig->GetBool(CategoryName, TEXT("bUseAudioStreamCaching"), OutOverrides.bUseStreamCaching, GEngineIni);

	/** Memory Load On Demand Settings */
	if (OutOverrides.bUseStreamCaching)
	{
		// Cache size:
		int32 RetrievedCacheSize = 32 * 1024;
		GConfig->GetInt(CategoryName, TEXT("CacheSizeKB"), RetrievedCacheSize, GEngineIni);
		OutOverrides.StreamCachingSettings.CacheSizeKB = RetrievedCacheSize;
	}

	//Cache sample rate map.
	OutOverrides.PlatformSampleRates.Reset();

	float RetrievedSampleRate = -1.0f;

	GConfig->GetFloat(CategoryName, TEXT("MaxSampleRate"), RetrievedSampleRate, GEngineIni);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Max, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	GConfig->GetFloat(CategoryName, TEXT("HighSampleRate"), RetrievedSampleRate, GEngineIni);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::High, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	GConfig->GetFloat(CategoryName, TEXT("MedSampleRate"), RetrievedSampleRate, GEngineIni);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Medium, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	GConfig->GetFloat(CategoryName, TEXT("LowSampleRate"), RetrievedSampleRate, GEngineIni);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Low, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	GConfig->GetFloat(CategoryName, TEXT("MinSampleRate"), RetrievedSampleRate, GEngineIni);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Min, RetrievedSampleRate);
}

#if PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES
static FPlatformAudioCookOverrides OutOverrides = FPlatformAudioCookOverrides();
#endif



#if PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES
static FCriticalSection CookOverridesCriticalSection;
#endif // PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES

void FPlatformCompressionUtilities::RecacheCookOverrides()
{
#if PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES
	FScopeLock ScopeLock(&CookOverridesCriticalSection);
	CacheCurrentPlatformAudioCookOverrides(OutOverrides);
#endif // PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES
}

const FPlatformAudioCookOverrides* FPlatformCompressionUtilities::GetCookOverridesForCurrentPlatform(bool bForceRecache)
{
#if PLATFORM_SUPPORTS_COMPRESSION_OVERRIDES
	static bool bCachedCookOverrides = false;

#if !WITH_EDITOR
	// In non-editor situations, we only need to cache the cook overrides once.
	if (!bCachedCookOverrides)
	{
#else
	// In editor situations, the settings can change at any time, so we need to retrieve them.
	FScopeLock ScopeLock(&CookOverridesCriticalSection);
	
	static double LastCacheTime = 0.0;
	double CurrentTime = FPlatformTime::Seconds();
	double TimeSinceLastCache = CurrentTime - LastCacheTime;

	if (bForceRecache || TimeSinceLastCache > CookOverrideCachingIntervalCvar)
	{
		LastCacheTime = CurrentTime;
#endif // WITH_EDITOR
	
		CacheCurrentPlatformAudioCookOverrides(OutOverrides);
		bCachedCookOverrides = true;
	}
	return &OutOverrides;
#else 
	return nullptr;
#endif
}

bool FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching()
{
	const FPlatformAudioCookOverrides* Settings = GetCookOverridesForCurrentPlatform();
	return Settings && Settings->bUseStreamCaching;
}

const FAudioStreamCachingSettings& FPlatformCompressionUtilities::GetStreamCachingSettingsForCurrentPlatform()
{
	const FPlatformAudioCookOverrides* Settings = GetCookOverridesForCurrentPlatform();
	checkf(Settings, TEXT("Please only use this function if FPlatformCompressionUtilities::IsCurrentPlatformUsingLoadOnDemand() returns true."));
	return Settings->StreamCachingSettings;
}

FCachedAudioStreamingManagerParams FPlatformCompressionUtilities::BuildCachedStreamingManagerParams()
{
	const FAudioStreamCachingSettings& CacheSettings = GetStreamCachingSettingsForCurrentPlatform();
	int32 MaxChunkSize = GetMaxChunkSizeForCookOverrides(GetCookOverridesForCurrentPlatform());

	// Our number of elements is tweakable based on the minimum cache usage we want to support.
	const float MinimumCacheUsage = FMath::Clamp(MinimumCacheUsageCvar, 0.0f, 0.95f);
	int32 MinChunkSize = (1.0f - MinimumCacheUsage) * MaxChunkSize;
	int32 NumElements = (CacheSettings.CacheSizeKB * 1024) / MinChunkSize;

	FCachedAudioStreamingManagerParams Params;
	FCachedAudioStreamingManagerParams::FCacheDimensions CacheDimensions;

	// Primary cache defined here:
	CacheDimensions.MaxChunkSize = MaxChunkSize;
	CacheDimensions.MaxMemoryInBytes = CacheSettings.CacheSizeKB * 1024;
	CacheDimensions.NumElements = NumElements;
	Params.Caches.Add(CacheDimensions);

	// TODO: When settings are added to support multiple sub-caches, add it here.

	return Params;
}

uint32 FPlatformCompressionUtilities::GetMaxChunkSizeForCookOverrides(const FPlatformAudioCookOverrides* InCompressionOverrides)
{
	check(InCompressionOverrides);

	// TODO: We can fine tune this to platform-specific voice counts, but in the meantime we target 32 voices as an average-case.
	// If the game runs with higher than 32 voices, that means we will potentially have a larger cache than what was set in the target settings.
	// In that case we log a warning on application launch.
	const int32 MinimumNumChunks = 32;
	const int32 DefaultMaxChunkSizeKB = 256;
	
	int32 CacheSizeKB = InCompressionOverrides->StreamCachingSettings.CacheSizeKB;
	
	if (CacheSizeKB == 0)
	{
		CacheSizeKB = FAudioStreamCachingSettings::DefaultCacheSize;
	}

	// If we won't have a large enough cache size to fit enough chunks to play 32 different sources at once, 
	// we truncate the chunk size to fit at least that many 
	if (CacheSizeKB / DefaultMaxChunkSizeKB < MinimumNumChunks)
	{
		return (CacheSizeKB / MinimumNumChunks) * 1024;
	}
	else
	{
		return DefaultMaxChunkSizeKB * 1024;
	}
}

float FPlatformCompressionUtilities::GetCompressionDurationForCurrentPlatform()
{
	float Threshold = -1.0f;

	const FPlatformRuntimeAudioCompressionOverrides* Settings = GetRuntimeCompressionOverridesForCurrentPlatform();
	if (Settings && Settings->bOverrideCompressionTimes)
	{
		Threshold = Settings->DurationThreshold;
	}

	return Threshold;
}

float FPlatformCompressionUtilities::GetTargetSampleRateForPlatform(ESoundwaveSampleRateSettings InSampleRateLevel /*= ESoundwaveSampleRateSettings::High*/)
{
	float SampleRate = -1.0f;
	const FPlatformAudioCookOverrides* Settings = GetCookOverridesForCurrentPlatform();
	if (Settings && Settings->bResampleForDevice)
	{
		const float* FoundSampleRate = Settings->PlatformSampleRates.Find(InSampleRateLevel);

		if (FoundSampleRate)
		{
			SampleRate = *FoundSampleRate;
		}
		else
		{
			ensureMsgf(false, TEXT("Warning: Could not find a matching sample rate for this platform. Check your project settings."));
		}
	}

	return SampleRate;
}

int32 FPlatformCompressionUtilities::GetMaxPreloadedBranchesForCurrentPlatform()
{
	const FPlatformRuntimeAudioCompressionOverrides* Settings = GetRuntimeCompressionOverridesForCurrentPlatform();

	if (Settings)
	{
		return FMath::Max(Settings->MaxNumRandomBranches, 0);
	}
	else
	{
		return 0;
	}
}

int32 FPlatformCompressionUtilities::GetQualityIndexOverrideForCurrentPlatform()
{
	const FPlatformRuntimeAudioCompressionOverrides* Settings = GetRuntimeCompressionOverridesForCurrentPlatform();

	if (Settings)
	{
		return Settings->SoundCueQualityIndex;
	}
	else
	{
		return INDEX_NONE;
	}
}