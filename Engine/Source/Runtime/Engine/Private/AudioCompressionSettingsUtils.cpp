// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioCompressionSettingsUtils.h"
#include "AudioCompressionSettings.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"

#define ENABLE_PLATFORM_COMPRESSION_OVERRIDES 1

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

void CacheAudioCookOverrides(FPlatformAudioCookOverrides& OutOverrides, const TCHAR* InPlatformName=nullptr)
{
	// if the platform was passed in, use it, otherwise, get the runtime platform's name for looking up DDPI
	FString PlatformName = InPlatformName ? FString(InPlatformName) : FString(FPlatformProperties::IniPlatformName());
	
	// now use that platform name to get the ini section out of DDPI
	FString CategoryName = FDataDrivenPlatformInfoRegistry::GetPlatformInfo(PlatformName).AudioCompressionSettingsIniSectionName;

	// if we don't support platform overrides, then return 
	if (CategoryName.Len() == 0)
	{
		OutOverrides = FPlatformAudioCookOverrides();
		return;
	}
	
	FConfigFile PlatformFile;
	FConfigCacheIni::LoadLocalIniFile(PlatformFile, TEXT("Engine"), true, *PlatformName);


	int32 SoundCueQualityIndex = INDEX_NONE;
	if (PlatformFile.GetInt(*CategoryName, TEXT("SoundCueCookQualityIndex"), SoundCueQualityIndex))
	{
		OutOverrides.SoundCueCookQualityIndex = SoundCueQualityIndex;
	}

	PlatformFile.GetBool(*CategoryName, TEXT("bUseAudioStreamCaching"), OutOverrides.bUseStreamCaching);

	/** Memory Load On Demand Settings */
	if (OutOverrides.bUseStreamCaching)
	{
		// Cache size:
		int32 RetrievedCacheSize = 32 * 1024;
		PlatformFile.GetInt(*CategoryName, TEXT("CacheSizeKB"), RetrievedCacheSize);
		OutOverrides.StreamCachingSettings.CacheSizeKB = RetrievedCacheSize;

		bool bForceLegacyStreamChunking = false;
		PlatformFile.GetBool(*CategoryName, TEXT("bForceLegacyStreamChunking"), bForceLegacyStreamChunking);
		OutOverrides.StreamCachingSettings.bForceLegacyStreamChunking = bForceLegacyStreamChunking;

		int32 ZerothChunkSizeForLegacyStreamChunking = 0;
		PlatformFile.GetInt(*CategoryName, TEXT("ZerothChunkSizeForLegacyStreamChunking"), ZerothChunkSizeForLegacyStreamChunking);
		OutOverrides.StreamCachingSettings.ZerothChunkSizeForLegacyStreamChunkingKB = ZerothChunkSizeForLegacyStreamChunking;
	}

	PlatformFile.GetBool(*CategoryName, TEXT("bResampleForDevice"), OutOverrides.bResampleForDevice);

	PlatformFile.GetFloat(*CategoryName, TEXT("CompressionQualityModifier"), OutOverrides.CompressionQualityModifier);

	PlatformFile.GetFloat(*CategoryName, TEXT("AutoStreamingThreshold"), OutOverrides.AutoStreamingThreshold);

#if 1
	//Cache sample rate map:
	float RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("MaxSampleRate"), RetrievedSampleRate);
	float* FoundSampleRate = OutOverrides.PlatformSampleRates.Find(ESoundwaveSampleRateSettings::Max);

	if (FoundSampleRate)
	{
		if (!FMath::IsNearlyEqual(*FoundSampleRate, RetrievedSampleRate))
		{
			*FoundSampleRate = RetrievedSampleRate;
		}

	}
	else
	{
		OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Max, RetrievedSampleRate);
	}

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("HighSampleRate"), RetrievedSampleRate);
	FoundSampleRate = OutOverrides.PlatformSampleRates.Find(ESoundwaveSampleRateSettings::High);

	if (FoundSampleRate)
	{
		if (!FMath::IsNearlyEqual(*FoundSampleRate, RetrievedSampleRate))
		{
			*FoundSampleRate = RetrievedSampleRate;
		}

	}
	else
	{
		OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::High, RetrievedSampleRate);
	}


	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("MedSampleRate"), RetrievedSampleRate);
	FoundSampleRate = OutOverrides.PlatformSampleRates.Find(ESoundwaveSampleRateSettings::Medium);

	if (FoundSampleRate)
	{
		if (!FMath::IsNearlyEqual(*FoundSampleRate, RetrievedSampleRate))
		{
			*FoundSampleRate = RetrievedSampleRate;
		}
	}
	else
	{
		OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Medium, RetrievedSampleRate);
	}

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("LowSampleRate"), RetrievedSampleRate);
	FoundSampleRate = OutOverrides.PlatformSampleRates.Find(ESoundwaveSampleRateSettings::Low);

	if (FoundSampleRate)
	{
		if (!FMath::IsNearlyEqual(*FoundSampleRate, RetrievedSampleRate))
		{
			*FoundSampleRate = RetrievedSampleRate;
		}
	}
	else
	{
		OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Low, RetrievedSampleRate);
	}

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("MinSampleRate"), RetrievedSampleRate);
	FoundSampleRate = OutOverrides.PlatformSampleRates.Find(ESoundwaveSampleRateSettings::Min);

	if (FoundSampleRate)
	{
		if (!FMath::IsNearlyEqual(*FoundSampleRate, RetrievedSampleRate))
		{
			*FoundSampleRate = RetrievedSampleRate;
		}
	}
	else
	{
		OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Min, RetrievedSampleRate);
	}

#else

	//Cache sample rate map.
	OutOverrides.PlatformSampleRates.Reset();

	float RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("MaxSampleRate"), RetrievedSampleRate);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Max, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("HighSampleRate"), RetrievedSampleRate);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::High, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("MedSampleRate"), RetrievedSampleRate);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Medium, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("LowSampleRate"), RetrievedSampleRate);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Low, RetrievedSampleRate);

	RetrievedSampleRate = -1.0f;

	PlatformFile.GetFloat(*CategoryName, TEXT("MinSampleRate"), RetrievedSampleRate);
	OutOverrides.PlatformSampleRates.Add(ESoundwaveSampleRateSettings::Min, RetrievedSampleRate);
#endif
}


static bool PlatformSupportsCompressionOverrides(const FString& PlatformName)
{
	return FDataDrivenPlatformInfoRegistry::GetPlatformInfo(PlatformName).AudioCompressionSettingsIniSectionName.Len() > 0;
}

static inline FString GetCookOverridePlatformName(const TCHAR* PlatformName)
{
	return PlatformName ? FString(PlatformName) : FString(FPlatformProperties::IniPlatformName());
}

static bool PlatformSupportsCompressionOverrides(const TCHAR* PlatformName=nullptr)
{
	return PlatformSupportsCompressionOverrides(GetCookOverridePlatformName(PlatformName));
}

static FCriticalSection CookOverridesCriticalSection;

static FPlatformAudioCookOverrides& GetCacheableOverridesByPlatform(const TCHAR* InPlatformName, bool& bNeedsToBeInitialized)
{
	FScopeLock ScopeLock(&CookOverridesCriticalSection);

	// registry of overrides by platform name, for cooking, etc that may need multiple platforms worth
	static TMap<FString, FPlatformAudioCookOverrides> OverridesByPlatform;

	// make sure we don't reallocate the memory later
	if (OverridesByPlatform.Num() == 0)
	{
		// give enough space for all known platforms
		OverridesByPlatform.Reserve(FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles());
	}

	FString PlatformName = GetCookOverridePlatformName(InPlatformName);
	// return one, or make one
	FPlatformAudioCookOverrides* ExistingOverrides = OverridesByPlatform.Find(PlatformName);
	if (ExistingOverrides != nullptr)
	{
		bNeedsToBeInitialized = false;
		return *ExistingOverrides;
	}

	bNeedsToBeInitialized = true;
	return OverridesByPlatform.Add(PlatformName, FPlatformAudioCookOverrides());
}

void FPlatformCompressionUtilities::RecacheCookOverrides()
{
	if (PlatformSupportsCompressionOverrides())
	{
		FScopeLock ScopeLock(&CookOverridesCriticalSection);
		bool bNeedsToBeInitialized;
		CacheAudioCookOverrides(GetCacheableOverridesByPlatform(nullptr, bNeedsToBeInitialized));
	}
}

const FPlatformAudioCookOverrides* FPlatformCompressionUtilities::GetCookOverrides(const TCHAR* PlatformName, bool bForceRecache)
{
	bool bNeedsToBeInitialized;
	FPlatformAudioCookOverrides& Overrides = GetCacheableOverridesByPlatform(PlatformName, bNeedsToBeInitialized);

#if WITH_EDITOR
	// In editor situations, the settings can change at any time, so we need to retrieve them.
	
	static double LastCacheTime = 0.0;
	double CurrentTime = FPlatformTime::Seconds();
	double TimeSinceLastCache = CurrentTime - LastCacheTime;

	if (bForceRecache || TimeSinceLastCache > CookOverrideCachingIntervalCvar)
	{
		bNeedsToBeInitialized = true;
		LastCacheTime = CurrentTime;
	}
#endif
	
	if (bNeedsToBeInitialized)
	{
		FScopeLock ScopeLock(&CookOverridesCriticalSection);
		CacheAudioCookOverrides(Overrides, PlatformName);
	}

	return &Overrides;
}

bool FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching()
{
	const FPlatformAudioCookOverrides* Settings = GetCookOverrides();
	return Settings && Settings->bUseStreamCaching;
}

const FAudioStreamCachingSettings& FPlatformCompressionUtilities::GetStreamCachingSettingsForCurrentPlatform()
{
	const FPlatformAudioCookOverrides* Settings = GetCookOverrides();
	checkf(Settings, TEXT("Please only use this function if FPlatformCompressionUtilities::IsCurrentPlatformUsingLoadOnDemand() returns true."));
	return Settings->StreamCachingSettings;
}

FCachedAudioStreamingManagerParams FPlatformCompressionUtilities::BuildCachedStreamingManagerParams()
{
	const FAudioStreamCachingSettings& CacheSettings = GetStreamCachingSettingsForCurrentPlatform();
	int32 MaxChunkSize = GetMaxChunkSizeForCookOverrides(GetCookOverrides());

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
	const FPlatformAudioCookOverrides* Settings = GetCookOverrides();
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