// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraPlatformSet.generated.h"

class UDeviceProfile;

UENUM()
enum class ENiagaraPlatformSelectionState : uint8
{
	Default,/** Neither explicitly enabled or disabled, this platform is enabled or not based on other settings in the platform set. */
	Enabled,/** This platform is explicitly disabled. */
	Disabled,/** This platform is explicitly disabled. */
};

USTRUCT()
struct FNiagaraDeviceProfileStateEntry
{
	GENERATED_BODY();

	FNiagaraDeviceProfileStateEntry()
		: EffectsQualityMask(0)
		, SetEffectsQualityMask(0)
	{}

	UPROPERTY(EditAnywhere, Category = Profile)
	FName ProfileName;

	/** The state of each set effects quality. */
	UPROPERTY(EditAnywhere, Category = Profile)
	uint32 EffectsQualityMask;

	/** Which Effects Qualities are set. */
	UPROPERTY(EditAnywhere, Category = Profile)
	uint32 SetEffectsQualityMask;

	FORCEINLINE ENiagaraPlatformSelectionState GetState(int32 EffectsQuality)const;

	FORCEINLINE void SetState(int32 EffectsQuality, ENiagaraPlatformSelectionState State);

	FORCEINLINE bool AllDefaulted()const { return SetEffectsQualityMask == 0; }
};

UENUM()
enum class ENiagaraPlatformSetState : uint8
{
	Disabled, //This platform set is disabled.
	Enabled, //This device profile is enabled but not active.
	Active, //This device profile is enabled and active now.
	Unknown UMETA(Hidden),
};

struct FNiagaraPlatformSet;

USTRUCT()
struct FNiagaraPlatformSetConflictEntry
{
	GENERATED_BODY()

	FNiagaraPlatformSetConflictEntry()
		: ProfileName(NAME_None)
		, EffectsQualityMask(0)
	{}

	UPROPERTY()
	FName ProfileName;

	/** Mask of conflicting effects qualities for this profile. */
	UPROPERTY()
	int32 EffectsQualityMask;
};

USTRUCT()
struct FNiagaraPlatformSetConflictInfo
{
	GENERATED_BODY()

	FNiagaraPlatformSetConflictInfo()
		: SetAIndex(INDEX_NONE)
		, SetBIndex(INDEX_NONE)
	{}
	/** Index of the first conflicting set in the checked array. */
	UPROPERTY()
	int32 SetAIndex;

	/** Index of the second conflicting set in the checked array. */
	UPROPERTY()
	int32 SetBIndex;

	/** Array of all conflicts between these sets. */
	UPROPERTY()
	TArray<FNiagaraPlatformSetConflictEntry> Conflicts;
};


USTRUCT()
struct NIAGARA_API FNiagaraPlatformSet
{
	GENERATED_BODY();

	static FORCEINLINE int32 EQFromMask(int32 EQMask);
	static FORCEINLINE int32 CreateEQMask(int32 EQ);
	static FText GetEffectsQualityText(int32 EffectsQuality);
	static FText GetEffectsQualityMaskText(int32 EffectsQualityMask);
public:

	//Runtime public API

	FNiagaraPlatformSet(int32 EQMask);
	FNiagaraPlatformSet();

	/** Is this set active right now. i.e. enabled for the current device profile and effects quality. */
	bool IsActive()const;
	
	/** Is this platform set enabled on any effects quality for the passed device profile. Returns the EffectsQualityMask for all enabled effects qualities for this profile. */
	int32 IsEnabledForDeviceProfile(const UDeviceProfile* DeviceProfile)const;

	/** Is this platform set enabled at this effects quality on any device profile. */
	bool IsEnabledForEffectsQuality(int32 EffectsQuality)const;

	/** Fill OutProfiles with all device proifles that have been overriden at the passed EffectsQuality. */
	void GetOverridenDeviceProfiles(int32 EffectsQuality, TArray<UDeviceProfile*>& OutEnabledProfiles, TArray<UDeviceProfile*>& OutDisabledProfiles)const;

	/** Returns true if the current platform can modify it's niagara scalability settings at runtime. */
	static bool CanChangeScalabilityAtRuntime();

	/**Will force all platform sets to regenerate their cacehd data next time they are used.*/
	static void InvalidateCachedData();

	static int32 GetEffectQualityMaskForDeviceProfile(const UDeviceProfile* Profile);

	//Editor only public API
#if WITH_EDITOR
	/** Returns true if this set is enabled for any profiles on the specified platform. */
	bool IsEnabledForPlatform(const FString& PlatformName);

	/**
	Does the enabled set of profiles in this set conflict with those in another set.
	In some cases this is not allowed.
	*/
	//bool Conflicts(const FNiagaraPlatformSet& Other, TArray<const UDeviceProfile*>& OutConflictingProfiles, bool bIncludeProfilesWithVariableEffectsQuality=false)const;


	/** Direct state accessors for the UI. */
	bool IsEffectQualityEnabled(int32 EffectQuality)const;
	void SetEnabledForEffectQuality(int32 EffectQuality, bool bEnabled);
	void SetDeviceProfileState(UDeviceProfile* Profile, int32 EffectsQuality, ENiagaraPlatformSelectionState NewState);
	ENiagaraPlatformSelectionState GetDeviceProfileState(UDeviceProfile* Profile, int32 EffectsQuality)const;
	
	/** Invalidates any cached data on this platform set when something has changed. */
	void OnChanged();

	/** Returns true if the passed platform can modify it's niagara scalability settings at runtime. */
	static bool CanChangeScalabilityAtRuntime(const UDeviceProfile* DeviceProfile);

	/** Returns true if the passed platform can modify it's niagara scalability settings at runtime. */
	static bool CanChangeScalabilityAtRuntime(const FString& PlatformName);

	/** Returns true if the passed platform should prune emitters on cook. */
	static bool ShouldPruneEmittersOnCook(const FString& PlatformName);

	/** Inspects the passed sets and generates an array of all conflicts between these sets. Used to keep arrays of platform sets orthogonal. */
	static bool GatherConflicts(const TArray<const FNiagaraPlatformSet*>& PlatformSets, TArray<FNiagaraPlatformSetConflictInfo>& OutConflicts);
#endif

	/** Mask defining which effects qualities this set matches. */
	UPROPERTY(EditAnywhere, Category = Platforms)
	int32 EffectsQualityMask;

	/** States of specific device profiles we've set. */
	UPROPERTY(EditAnywhere, Category = Platforms)
	TArray<FNiagaraDeviceProfileStateEntry> DeviceProfileStates;
private:

	//Set from outside when we need to force all cached values to be regenerated. For example on CVar changes.
	static uint32 LastDirtiedFrame;

	//Last frame we built our cached data. 
	mutable uint32 LastBuiltFrame;

	mutable bool bEnabledForCurrentProfileAndEffectQuality;

	bool IsEnabled(const UDeviceProfile* Profile, int32 EffectsQuality)const;

#if WITH_EDITOR
	//Data we pull from platform ini files.
	struct FPlatformIniSettings
	{
		FPlatformIniSettings()
			:bCanChangeEffectsQualityAtRuntime(0), EffectsQualityMask(INDEX_NONE)
		{}
		FPlatformIniSettings(int32 InbCanChangeEffectsQualityAtRuntime, int32 InbPruneEmittersOnCook, int32 InEffectsQualityMask)
			:bCanChangeEffectsQualityAtRuntime(InbCanChangeEffectsQualityAtRuntime), bPruneEmittersOnCook(InbPruneEmittersOnCook), EffectsQualityMask(InEffectsQualityMask)
		{}
		int32 bCanChangeEffectsQualityAtRuntime;
		int32 bPruneEmittersOnCook;
		int32 EffectsQualityMask;
	};

	//Cached data read from platform ini files.
	static TMap<FName, FPlatformIniSettings> CachedPlatformIniSettings;

	//Cached final EffectsQuality setting for each device profile.
	static TMap<const UDeviceProfile*, int32> CachedEQMasksPerDeviceProfile;

	static FPlatformIniSettings GetPlatformIniSettings(const FString& PlatformName);

	static int32 GetEffectQualityMaskForPlatform(const FString& PlatformName);
#endif
};


FORCEINLINE int32 FNiagaraPlatformSet::EQFromMask(int32 EQMask)
{
	//Iterate through bits in mask to find the first set quality level.
	int32 EffectsQuality = 0;
	while (EQMask != 0)
	{
		if (EQMask & 0x1)
		{
			return EffectsQuality;
		}
		++EffectsQuality;
		EQMask >>= 1;
	}

	return INDEX_NONE;
}

FORCEINLINE int32 FNiagaraPlatformSet::CreateEQMask(int32 EQ)
{
	return EQ == INDEX_NONE ? INDEX_NONE : (1 << EQ);
}


FORCEINLINE ENiagaraPlatformSelectionState FNiagaraDeviceProfileStateEntry::GetState(int32 EffectsQuality)const
{
	int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);
	return (EQMask & SetEffectsQualityMask) == 0 ? ENiagaraPlatformSelectionState::Default : ((EQMask & EffectsQualityMask) != 0 ? ENiagaraPlatformSelectionState::Enabled : ENiagaraPlatformSelectionState::Disabled);
}

FORCEINLINE void FNiagaraDeviceProfileStateEntry::SetState(int32 EffectsQuality, ENiagaraPlatformSelectionState State)
{
	int32 EQMask = FNiagaraPlatformSet::CreateEQMask(EffectsQuality);
	if (State == ENiagaraPlatformSelectionState::Default)
	{
		SetEffectsQualityMask &= ~EQMask;
		EffectsQualityMask &= ~EQMask;
	}
	else if (State == ENiagaraPlatformSelectionState::Enabled)
	{
		SetEffectsQualityMask |= EQMask;
		EffectsQualityMask |= EQMask;
	}
	else //(State == ENiagaraPlatformSelectionState::Disabled)
	{
		SetEffectsQualityMask |= EQMask;
		EffectsQualityMask &= ~EQMask;
	}
}