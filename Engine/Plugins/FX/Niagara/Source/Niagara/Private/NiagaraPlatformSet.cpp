// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraPlatformSet.h"
#include "NiagaraModule.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Scalability.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "NiagaraPlatformSet"

/**
Whether a platform can change it's scalability settings at runtime.
Defaults to false for all platforms and is explicitly enabled for desktop platforms.
*/
const TCHAR* CanChangeEQCVarName = TEXT("fx.NiagaraAllowRuntimeScalabilityChanges");
int32 GbNiagaraAllowRuntimeScalabilityChanges = 0;
static FAutoConsoleVariableRef CVarNiagaraAllowRuntimeScalabilityChanges(
	CanChangeEQCVarName,
	GbNiagaraAllowRuntimeScalabilityChanges,
	TEXT("If > 0 this platform allows scalability level changes at runtime. \n"),
	ECVF_ReadOnly
);

uint32 FNiagaraPlatformSet::LastDirtiedFrame = 0;
#if WITH_EDITOR
TMap<const UDeviceProfile*, int32> FNiagaraPlatformSet::CachedEQMasksPerDeviceProfile;
TMap<FName, FNiagaraPlatformSet::FPlatformIniSettings> FNiagaraPlatformSet::CachedPlatformIniSettings;
#endif

FText FNiagaraPlatformSet::GetEffectsQualityText(int32 EffectsQuality)
{
	Scalability::FQualityLevels Counts = Scalability::GetQualityLevelCounts();
	return Scalability::GetQualityLevelText(EffectsQuality, Counts.EffectsQuality);
}

FText FNiagaraPlatformSet::GetEffectsQualityMaskText(int32 EffectsQualityMask)
{
	if (EffectsQualityMask == INDEX_NONE)
	{
		return LOCTEXT("EffectsQualityAll", "All");
	}
	else if (EffectsQualityMask == 0)
	{
		return LOCTEXT("EffectsQualityNone", "None");
	}
	else
	{
		Scalability::FQualityLevels Counts = Scalability::GetQualityLevelCounts();
		return Scalability::GetQualityLevelText(EQFromMask(EffectsQualityMask), Counts.EffectsQuality);
	}
}

FNiagaraPlatformSet::FNiagaraPlatformSet(int32 EQMask)
	: EffectsQualityMask(EQMask)
	, LastBuiltFrame(0)
	, bEnabledForCurrentProfileAndEffectQuality(false)
{
	IsActive();
}

FNiagaraPlatformSet::FNiagaraPlatformSet()
	: EffectsQualityMask(INDEX_NONE)
	, LastBuiltFrame(0)
	, bEnabledForCurrentProfileAndEffectQuality(false)
{
}

bool FNiagaraPlatformSet::IsActive()const
{
	if (LastBuiltFrame <= LastDirtiedFrame || LastBuiltFrame == 0)
	{
		bEnabledForCurrentProfileAndEffectQuality = IsEnabled(UDeviceProfileManager::Get().GetActiveProfile(), INiagaraModule::GetEffectsQuality());
		LastBuiltFrame = GFrameNumber;
	}
	return bEnabledForCurrentProfileAndEffectQuality;
}

int32 FNiagaraPlatformSet::IsEnabledForDeviceProfile(const UDeviceProfile* DeviceProfile)const
{
	Scalability::FQualityLevels NumLevels = Scalability::GetQualityLevelCounts();
	int32 RetEQMask = 0;
	for (int32 i = 0; i < NumLevels.EffectsQuality; ++i)
	{
		if (IsEnabled(DeviceProfile, i))
		{
			RetEQMask |= CreateEQMask(i);
		}
	}

	return RetEQMask;
}

bool FNiagaraPlatformSet::IsEnabledForEffectsQuality(int32 EffectsQuality)const
{
	for (UObject* DeviceProfileObj : UDeviceProfileManager::Get().Profiles)
	{
		if (UDeviceProfile* Profile = Cast<UDeviceProfile>(DeviceProfileObj))
		{
			if (IsEnabled(Profile, EffectsQuality))
			{
				return true;
			}
		}
	}

	return false;
}

void FNiagaraPlatformSet::GetOverridenDeviceProfiles(int32 EffectsQuality, TArray<UDeviceProfile*>& OutEnabledProfiles, TArray<UDeviceProfile*>& OutDisabledProfiles)const
{
	int32 EQMask = CreateEQMask(EffectsQuality);
	for (const FNiagaraDeviceProfileStateEntry& Entry : DeviceProfileStates)
	{
		if (UObject** DeviceProfile = UDeviceProfileManager::Get().Profiles.FindByPredicate([&](UObject* CheckProfile) {return CheckProfile->GetFName() == Entry.ProfileName;}))
		{
			UDeviceProfile* Profile = CastChecked<UDeviceProfile>(*DeviceProfile);
			//If this platform cannot change at runtime then we store all EQs in the state so that the device is still overridden if someone changes it's EQ CVar.
			//So here we must also check that this EffectsQuality is the right one for the platforms current setting.
			int32 ProfileEQMask = GetEffectQualityMaskForDeviceProfile(Profile);
			if (ProfileEQMask == INDEX_NONE || (EQMask & ProfileEQMask) != 0)
			{
				ENiagaraPlatformSelectionState State = Entry.GetState(EffectsQuality);
				if (State == ENiagaraPlatformSelectionState::Enabled)
				{
					OutEnabledProfiles.Add(Profile);
				}
				else if (State == ENiagaraPlatformSelectionState::Disabled)
				{
					OutDisabledProfiles.Add(Profile);
				}
			}
		}
	}
}

bool FNiagaraPlatformSet::CanChangeScalabilityAtRuntime()
{
	//For the current platform we can just read direct as this CVar is readonly.
	return GbNiagaraAllowRuntimeScalabilityChanges != 0;
}

int32 FNiagaraPlatformSet::GetEffectQualityMaskForDeviceProfile(const UDeviceProfile* Profile)
{
#if WITH_EDITOR

	//When in the editor we may be asking for the EQ of a platform other than the current one.
	//So we have to look in ini files and device profiles to find it.

	if (int32* CachedEQMask = CachedEQMasksPerDeviceProfile.Find(Profile))
	{
		//We've seen this Profile before so return the cached value;
		return *CachedEQMask;
	}

	int32 EQMask = INDEX_NONE;
	FPlatformIniSettings PlatformSettings = GetPlatformIniSettings(Profile->DeviceType);
	
	if (PlatformSettings.bCanChangeEffectsQualityAtRuntime)
	{
		EQMask = INDEX_NONE;
	}
	else
	{
		//Default to the platform EQ but allow device profiles to override.
		EQMask = PlatformSettings.EffectsQualityMask;

		//Check if the DPs set EQ directly.
		int32 EffectsQuality = INDEX_NONE;
		if (Profile->GetConsolidatedCVarValue(TEXT("sg.EffectsQuality"), EffectsQuality))
		{
			EQMask = CreateEQMask(EffectsQuality);
		}
	}

	CachedEQMasksPerDeviceProfile.Add(Profile) = EQMask;
	return EQMask;

#else

	//When not in editor we can assume we're asking about the current platform.
	check(Profile == UDeviceProfileManager::Get().GetActiveProfile());
	bool bCanChangeEQAtRuntime = CanChangeScalabilityAtRuntime();

	return CreateEQMask(bCanChangeEQAtRuntime ? INDEX_NONE : INiagaraModule::GetEffectsQuality());

#endif
}

bool FNiagaraPlatformSet::IsEnabled(const UDeviceProfile* Profile, int32 EffectsQuality)const
{
	checkSlow(Profile);

	int32 TestEQMask = CreateEQMask(EffectsQuality);
	int32 ProfileEQMask = GetEffectQualityMaskForDeviceProfile(Profile);
	bool bIsEnabledByEQ = (EffectsQualityMask & ProfileEQMask & TestEQMask) != 0;

	if (DeviceProfileStates.Num() > 0)
	{
		//Walk up the parent hierarchy to see if we have an explicit state for this profile set.
		const UDeviceProfile* CurrProfile = Profile;
		while (CurrProfile)
		{
			if (const FNiagaraDeviceProfileStateEntry* StateEntry = DeviceProfileStates.FindByPredicate([&](const FNiagaraDeviceProfileStateEntry& ProfileState) {return ProfileState.ProfileName == CurrProfile->GetFName(); }))
			{
				ENiagaraPlatformSelectionState SelectionState = StateEntry->GetState(EffectsQuality);
				if (SelectionState != ENiagaraPlatformSelectionState::Default)
				{
					return SelectionState == ENiagaraPlatformSelectionState::Enabled;
				}
			}
			CurrProfile = Cast<UDeviceProfile>(CurrProfile->Parent);
		}
	}

	return bIsEnabledByEQ;
}


void FNiagaraPlatformSet::InvalidateCachedData()
{
#if WITH_EDITOR
	CachedEQMasksPerDeviceProfile.Empty();
	CachedPlatformIniSettings.Empty();
#endif

	LastDirtiedFrame = GFrameNumber;
}

#if WITH_EDITOR

bool FNiagaraPlatformSet::IsEnabledForPlatform(const FString& PlatformName)
{
	for (const UObject* ProfileObj : UDeviceProfileManager::Get().Profiles)
	{
		if (const UDeviceProfile* Profile = Cast<const UDeviceProfile>(ProfileObj))
		{
			if (Profile->DeviceType == PlatformName)
			{
				if (IsEnabledForDeviceProfile(Profile) != 0)
				{
					return true;//At least one profile for this platform is enabled.
				}
			}
		}
	}

	//No enabled profiles for this platform.
	return false;
}

// bool FNiagaraPlatformSet::Conflicts(const FNiagaraPlatformSet& Other, TArray<const UDeviceProfile*>& OutConflictingProfiles, bool bIncludeProfilesWithVariableEffectsQuality)const
// {
// 	for (const UObject* ProfileObj : UDeviceProfileManager::Get().Profiles)
// 	{
// 		if (const UDeviceProfile* Profile = Cast<const UDeviceProfile>(ProfileObj))
// 		{
// 			if (bIncludeProfilesWithVariableEffectsQuality || CanChangeScalabilityAtRuntime(Profile) == false)
// 			{
// 				if (IsEnabledForDeviceProfile(Profile) && Other.IsEnabledForDeviceProfile(Profile))
// 				{
// 					OutConflictingProfiles.AddUnique(Profile);
// 				}
// 			}
// 		}
// 	}
// 
// 	return OutConflictingProfiles.Num() > 0;
// }

bool FNiagaraPlatformSet::IsEffectQualityEnabled(int32 EffectQuality)const
{
	return ((1 << EffectQuality) & EffectsQualityMask) != 0;
}

void FNiagaraPlatformSet::SetEnabledForEffectQuality(int32 EffectQuality, bool bEnabled)
{
	int32 EQBit = (1 << EffectQuality);
	if (bEnabled)
	{
		EffectsQualityMask |= EQBit;
	}
	else
	{
		EffectsQualityMask &= ~EQBit;
	}
	OnChanged();
}

void FNiagaraPlatformSet::SetDeviceProfileState(UDeviceProfile* Profile, int32 EffectsQuality, ENiagaraPlatformSelectionState NewState)
{
	int32 Index = INDEX_NONE;
	Index = DeviceProfileStates.IndexOfByPredicate([&](const FNiagaraDeviceProfileStateEntry& Entry) { return Entry.ProfileName == Profile->GetFName(); });

	int32 ProfileEQMask = GetEffectQualityMaskForDeviceProfile(Profile);
	if (ProfileEQMask != INDEX_NONE)
	{
		//For platforms that cannot change EQ at runtime we mark all state bits when setting state here so that if someone changes their EQ setting in the future, the state will be preserved.
		EffectsQuality = INDEX_NONE;
	}

	if(Index == INDEX_NONE)
	{
		if (NewState != ENiagaraPlatformSelectionState::Default)
		{
			FNiagaraDeviceProfileStateEntry& NewEntry = DeviceProfileStates.AddDefaulted_GetRef();
			NewEntry.ProfileName = Profile->GetFName();
			NewEntry.SetState(EffectsQuality, NewState);
		}
	}
	else
	{
		DeviceProfileStates[Index].SetState(EffectsQuality, NewState);

		if (DeviceProfileStates[Index].AllDefaulted())
		{
			DeviceProfileStates.RemoveAtSwap(Index);//We don't need to store the default state. It's implied by no entry.
		}
	}
	OnChanged();
}

ENiagaraPlatformSelectionState FNiagaraPlatformSet::GetDeviceProfileState(UDeviceProfile* Profile, int32 EffectsQuality)const
{
	if (const FNiagaraDeviceProfileStateEntry* ExistingEntry = DeviceProfileStates.FindByPredicate([&](const FNiagaraDeviceProfileStateEntry& Entry) { return Entry.ProfileName == Profile->GetFName(); }))
	{
		int32 ProfileEQMask = GetEffectQualityMaskForDeviceProfile(Profile);
		if (ProfileEQMask == INDEX_NONE || ProfileEQMask & CreateEQMask(EffectsQuality))
		{
			//For profiles that cannot change scalability at runtime we store all flags in their state so that if anyone ever changes their EQ Cvar, the state setting remains valid.
			//This just means we also have to ensure this is the correct EQ here.

			return ExistingEntry->GetState(EffectsQuality);
		}
	}
	return ENiagaraPlatformSelectionState::Default;
}

void FNiagaraPlatformSet::OnChanged()
{
	LastBuiltFrame = 0;
}

bool FNiagaraPlatformSet::CanChangeScalabilityAtRuntime(const FString& PlatformName)
{
	int32 EQMask = GetEffectQualityMaskForPlatform(PlatformName);
	//If we're on a platform that can change EQ then it will have the full mask.
	return EQMask == INDEX_NONE;
}

bool FNiagaraPlatformSet::CanChangeScalabilityAtRuntime(const UDeviceProfile* DeviceProfile)
{
	if (ensure(DeviceProfile))
	{
		int32 EQMask = GetEffectQualityMaskForDeviceProfile(DeviceProfile);
		//If we're on a platform that can change EQ then it will have the full mask.
		return EQMask == INDEX_NONE;
	}
	return true;//Assuming true if we fail to find the platform seems safest.
}

bool FNiagaraPlatformSet::ShouldPruneEmittersOnCook(const FString& PlatformName)
{
	FPlatformIniSettings Settings = GetPlatformIniSettings(PlatformName);
	return Settings.bPruneEmittersOnCook != 0;
}

bool FNiagaraPlatformSet::GatherConflicts(const TArray<const FNiagaraPlatformSet*>& PlatformSets, TArray<FNiagaraPlatformSetConflictInfo>& OutConflicts)
{
	Scalability::FQualityLevels NumLevels = Scalability::GetQualityLevelCounts();

	FNiagaraPlatformSetConflictInfo* CurrentConflict = nullptr;
	for (int32 A = 0; A < PlatformSets.Num(); ++A)
	{
		for (int32 B = A + 1; B < PlatformSets.Num(); ++B)
		{
			check(A != B);

			if (PlatformSets[A] == nullptr || PlatformSets[B] == nullptr) continue;

			const FNiagaraPlatformSet& SetA = *PlatformSets[A];
			const FNiagaraPlatformSet& SetB = *PlatformSets[B];

			CurrentConflict = nullptr;
			for (UObject* DPObj : UDeviceProfileManager::Get().Profiles)
			{
				UDeviceProfile* Profile = CastChecked<UDeviceProfile>(DPObj);
				int32 AEnabledMask = SetA.IsEnabledForDeviceProfile(Profile);
				int32 BEnabledMask = SetB.IsEnabledForDeviceProfile(Profile);
				int32 ConflictMask = AEnabledMask & BEnabledMask;

				if (ConflictMask != 0)
				{
					//We have a conflict so add it to the output.
					if (CurrentConflict == nullptr)
					{
						CurrentConflict = &OutConflicts.AddDefaulted_GetRef();
						CurrentConflict->SetAIndex = A;
						CurrentConflict->SetBIndex = B;
					}
					FNiagaraPlatformSetConflictEntry& ConflictEntry = CurrentConflict->Conflicts.AddDefaulted_GetRef();
					ConflictEntry.ProfileName = Profile->GetFName();
					ConflictEntry.EffectsQualityMask = ConflictMask;
				}
			}
		}
	}

	return OutConflicts.Num() > 0;
}

FNiagaraPlatformSet::FPlatformIniSettings FNiagaraPlatformSet::GetPlatformIniSettings(const FString& PlatformName)
{
	if (FPlatformIniSettings* CachedSettings = CachedPlatformIniSettings.Find(*PlatformName))
	{
		return *CachedSettings;
	}

	//Load config files in which we can reasonable expect to find sg.EffectQuality and may be set.
	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName);//Should use BaseProfileName? Are either of these ensured to be correct? I worry this is brittle.

	FConfigFile GameSettings;
	FConfigCacheIni::LoadLocalIniFile(GameSettings, TEXT("Game"), true, *PlatformName);//Should use BaseProfileName? Are either of these ensured to be correct? I worry this is brittle.

	FConfigFile ScalabilitySettings;
	FConfigCacheIni::LoadLocalIniFile(ScalabilitySettings, TEXT("Scalability"), true, *PlatformName);//Should use BaseProfileName? Are either of these ensured to be correct? I worry this is brittle.

	auto FindCVarValue = [&](const TCHAR* Section, const TCHAR* CVarName, int32& OutVal)
	{
		if (!ScalabilitySettings.GetInt(Section, CVarName, OutVal))
		{
			if (!GameSettings.GetInt(Section, CVarName, OutVal))
			{
				EngineSettings.GetInt(Section, CVarName, OutVal);
			}
		}
	};

	int32 EffectsQuality = INDEX_NONE;
	int32 EQMask = INDEX_NONE;

	//If this platform can change quality settings at runtime then we return the full mask.
	int32 CanChangeEffectQuality = 0;

	FindCVarValue(TEXT("SystemSettings"), CanChangeEQCVarName, CanChangeEffectQuality);
	if (CanChangeEffectQuality != 0)
	{
		//If this platform can change it's EffectsQuality at runtime we return the ALL mask.
		EQMask = INDEX_NONE;
	}
	else
	{
		FindCVarValue(TEXT("ScalabilityGroups"), TEXT("sg.EffectsQuality"), EffectsQuality);
		EQMask = CreateEQMask(EffectsQuality);
	}

	int32 PruneEmittersOnCook = 0;
	FindCVarValue(TEXT("SystemSettings"), TEXT("fx.PruneEmittersOnCook"), PruneEmittersOnCook);

	FPlatformIniSettings& NewSetting = CachedPlatformIniSettings.Add(*PlatformName);
	NewSetting = FPlatformIniSettings(CanChangeEffectQuality, PruneEmittersOnCook, EQMask);
	return NewSetting;
}

int32 FNiagaraPlatformSet::GetEffectQualityMaskForPlatform(const FString& PlatformName)
{
	FPlatformIniSettings PlatformSettings = GetPlatformIniSettings(PlatformName);
	return PlatformSettings.EffectsQualityMask;
}
#endif

#undef LOCTEXT_NAMESPACE