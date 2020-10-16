// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraPlatformSet.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Scalability.h"
#include "Misc/ConfigCacheIni.h"
#include "Interfaces/ITargetPlatform.h"
#include "NiagaraSystem.h"
#include "NiagaraSettings.h"
#include "SystemSettings.h"

#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "PlatformInfo.h"
#endif

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
	TEXT("If > 0 this platform allows niagara scalability level changes at runtime. \n"),
	ECVF_Scalability
);

const TCHAR* PruneEmittersOnCookName = TEXT("fx.Niagara.PruneEmittersOnCook");
int32 GbPruneEmittersOnCook = 1;
static FAutoConsoleVariableRef CVarPruneEmittersOnCook(
	PruneEmittersOnCookName,
	GbPruneEmittersOnCook,
	TEXT("If > 0 this platform will prune disabled emitters during cook. \n"),
	ECVF_Scalability
);

static const int32 DefaultQualityLevel = 3;
const TCHAR* NiagaraQualityLevelName = TEXT("fx.Niagara.QualityLevel");
int32 GNiagaraQualityLevel = DefaultQualityLevel;
static FAutoConsoleVariableRef CVarNiagaraQualityLevel(
	NiagaraQualityLevelName,
	GNiagaraQualityLevel,
	TEXT("The quality level for Niagara Effects. \n"),
	FConsoleVariableDelegate::CreateStatic(&FNiagaraPlatformSet::OnQualityLevelChanged),
	ECVF_Scalability
);

// Override platform device profile
// In editor all profiles will be available
// On cooked builds only the profiles for that cooked platform will be available
TWeakObjectPtr<UDeviceProfile> GNiagaraPlatformOverride;

static int32 GNiagaraBackupQualityLevel = INDEX_NONE;

static FAutoConsoleCommand GCmdSetNiagaraPlatformOverride(
	TEXT("fx.Niagara.SetOverridePlatformName"),
	TEXT("Sets which platform we should override with, no args means reset to default"),
	FConsoleCommandWithArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args)
		{
			GNiagaraPlatformOverride.Reset();
			if (Args.Num() == 0)
			{
				if (GNiagaraBackupQualityLevel != INDEX_NONE)
				{
					OnSetCVarFromIniEntry(*GDeviceProfilesIni, NiagaraQualityLevelName, *LexToString(GNiagaraBackupQualityLevel), ECVF_SetByMask);
				}
				GNiagaraBackupQualityLevel = INDEX_NONE;
				UE_LOG(LogNiagara, Warning, TEXT("Niagara Clearing Override DeviceProfile"));
			}
			else
			{
				for (UObject* DeviceProfileObj : UDeviceProfileManager::Get().Profiles)
				{
					if (UDeviceProfile* Profile = Cast<UDeviceProfile>(DeviceProfileObj))
					{
						if (Profile->GetName() == Args[0])
						{
							GNiagaraPlatformOverride = Profile;
							break;
						}
					}
				}


				if (GNiagaraPlatformOverride.IsValid())
				{
					//Save the previous QL state the first time we enter a preview.
					if (GNiagaraBackupQualityLevel == INDEX_NONE)
					{
						GNiagaraBackupQualityLevel = GNiagaraQualityLevel;
					}

					UDeviceProfile* OverrideDP = GNiagaraPlatformOverride.Get();
					check(OverrideDP);
					int32 DPQL = FNiagaraPlatformSet::QualityLevelFromMask(FNiagaraPlatformSet::GetEffectQualityMaskForDeviceProfile(OverrideDP));
					
					OnSetCVarFromIniEntry(*GDeviceProfilesIni, NiagaraQualityLevelName, *LexToString(DPQL), ECVF_SetByMask);

					UE_LOG(LogNiagara, Warning, TEXT("Niagara Setting Override DeviceProfile '%s'"), *Args[0]);
				}
				else
				{
					UE_LOG(LogNiagara, Warning, TEXT("Niagara Failed to Find Override DeviceProfile '%s'"), *Args[0]);
				}
			}
		}
	)
);

static UDeviceProfile* NiagaraGetActiveDeviceProfile()
{
	UDeviceProfile* ActiveProfile = GNiagaraPlatformOverride.Get(); 
	if (ActiveProfile == nullptr)
	{
		ActiveProfile = UDeviceProfileManager::Get().GetActiveProfile();
	}
	return ActiveProfile;
}

int32 FNiagaraPlatformSet::CachedQualityLevel = INDEX_NONE;

int32 FNiagaraPlatformSet::GetQualityLevel()
{
	if (CachedQualityLevel == INDEX_NONE)
	{
		CachedQualityLevel = GNiagaraQualityLevel;
	}
	return CachedQualityLevel;
}

void FNiagaraPlatformSet::OnQualityLevelChanged(IConsoleVariable* Variable)
{
	int32 NewQualityLevel = Variable->GetInt();
	int32 CurrentLevel = GetQualityLevel();

	if (CurrentLevel != NewQualityLevel)
	{
		CachedQualityLevel = NewQualityLevel;
		InvalidateCachedData();

		for (TObjectIterator<UNiagaraSystem> It; It; ++It)
		{
			UNiagaraSystem* System = *It;
			check(System);
			System->OnScalabilityCVarChanged();
		}
	}
}

uint32 FNiagaraPlatformSet::LastDirtiedFrame = 0;
#if WITH_EDITOR
TMap<const UDeviceProfile*, int32> FNiagaraPlatformSet::CachedQLMasksPerDeviceProfile;
TMap<FName, FNiagaraPlatformSet::FPlatformIniSettings> FNiagaraPlatformSet::CachedPlatformIniSettings;
#endif

FText FNiagaraPlatformSet::GetQualityLevelText(int32 QualityLevel)
{
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);

	if (Settings->QualityLevels.IsValidIndex(QualityLevel))
	{
		return Settings->QualityLevels[QualityLevel];
	}	
	else
	{
		return FText::AsNumber(QualityLevel);
	}
}

FText FNiagaraPlatformSet::GetQualityLevelMaskText(int32 QualityLevelMask)
{
	if (QualityLevelMask == INDEX_NONE)
	{
		return LOCTEXT("QualityLevelAll", "All");
	}
	else if (QualityLevelMask == 0)
	{
		return LOCTEXT("QualityLevelNone", "None");
	}
	else
	{
		return GetQualityLevelText(QualityLevelFromMask(QualityLevelMask));
	}
}

FNiagaraPlatformSet::FNiagaraPlatformSet(int32 QLMask)
	: QualityLevelMask(QLMask)
	, LastBuiltFrame(0)
	, bEnabledForCurrentProfileAndEffectQuality(false)
{
	IsActive();
}

FNiagaraPlatformSet::FNiagaraPlatformSet()
	: QualityLevelMask(INDEX_NONE)
	, LastBuiltFrame(0)
	, bEnabledForCurrentProfileAndEffectQuality(false)
{
}

bool FNiagaraPlatformSet::operator==(const FNiagaraPlatformSet& Other)const
{
	return QualityLevelMask == Other.QualityLevelMask && DeviceProfileStates == Other.DeviceProfileStates;
}

bool FNiagaraPlatformSet::IsActive()const
{
	if (LastBuiltFrame <= LastDirtiedFrame || LastBuiltFrame == 0)
	{
		bEnabledForCurrentProfileAndEffectQuality = IsEnabled(NiagaraGetActiveDeviceProfile(), GetQualityLevel(), true);
		LastBuiltFrame = GFrameNumber;
	}
	return bEnabledForCurrentProfileAndEffectQuality;
}

int32 FNiagaraPlatformSet::IsEnabledForDeviceProfile(const UDeviceProfile* DeviceProfile)const
{
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);

	int32 RetQLMask = 0;
	for (int32 i = 0; i < Settings->QualityLevels.Num(); ++i)
	{
		if (IsEnabled(DeviceProfile, i))
		{
			RetQLMask |= CreateQualityLevelMask(i);
		}
	}

	return RetQLMask;
}

bool FNiagaraPlatformSet::IsEnabledForQualityLevel(int32 QualityLevel)const
{
	for (UObject* DeviceProfileObj : UDeviceProfileManager::Get().Profiles)
	{
		if (UDeviceProfile* Profile = Cast<UDeviceProfile>(DeviceProfileObj))
		{
			if (IsEnabled(Profile, QualityLevel))
			{
				return true;
			}
		}
	}

	return false;
}

void FNiagaraPlatformSet::GetOverridenDeviceProfiles(int32 QualityLevel, TArray<UDeviceProfile*>& OutEnabledProfiles, TArray<UDeviceProfile*>& OutDisabledProfiles)const
{
	int32 QLMask = CreateQualityLevelMask(QualityLevel);
	for (const FNiagaraDeviceProfileStateEntry& Entry : DeviceProfileStates)
	{
		if (UObject** DeviceProfile = UDeviceProfileManager::Get().Profiles.FindByPredicate([&](UObject* CheckProfile) {return CheckProfile->GetFName() == Entry.ProfileName;}))
		{
			UDeviceProfile* Profile = CastChecked<UDeviceProfile>(*DeviceProfile);
			//If this platform cannot change at runtime then we store all EQs in the state so that the device is still overridden if someone changes it's EQ CVar.
			//So here we must also check that this QualityLevel is the right one for the platforms current setting.
			int32 ProfileQLMask = GetEffectQualityMaskForDeviceProfile(Profile);
			if (ProfileQLMask == INDEX_NONE || (QLMask & ProfileQLMask) != 0)
			{
				ENiagaraPlatformSelectionState State = Entry.GetState(QualityLevel);
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
	if (int32* CachedQLMask = CachedQLMasksPerDeviceProfile.Find(Profile))
	{
		//We've seen this Profile before so return the cached value;
		return *CachedQLMask;
	}

	int32 QLMask = INDEX_NONE;
	FPlatformIniSettings& PlatformSettings = GetPlatformIniSettings(Profile->DeviceType);
	
	if (PlatformSettings.bCanChangeScalabilitySettingsAtRuntime)
	{
		QLMask = INDEX_NONE;
	}
	else
	{
		//Check if the DPs set Niagara quality directly.
		int32 QualityLevel = INDEX_NONE;
		if (!Profile->GetConsolidatedCVarValue(NiagaraQualityLevelName, QualityLevel))
		{
			QualityLevel = DefaultQualityLevel;
			//If not, grab it from the effects quality setting.
			int32 EffectsQuality = INDEX_NONE;
			//See if this profile overrides effects quality.
			if (!Profile->GetConsolidatedCVarValue(TEXT("sg.EffectsQuality"), EffectsQuality))
			{
				EffectsQuality = PlatformSettings.EffectsQuality;
			}
			check(EffectsQuality != INDEX_NONE);

			QualityLevel = PlatformSettings.QualityLevelsPerEffectsQuality[EffectsQuality];
		}
		check(QualityLevel != INDEX_NONE);
		QLMask = CreateQualityLevelMask(QualityLevel);
	}

	//UE_LOG(LogNiagara, Warning, TEXT("%s - DeviceProfile(%d)"), *Profile->GetName(), QLMask);

	CachedQLMasksPerDeviceProfile.Add(Profile) = QLMask;
	return QLMask;

#else

	//When not in editor we can assume we're asking about the current platform.
	check(Profile == NiagaraGetActiveDeviceProfile());
	bool bCanChangeEQAtRuntime = CanChangeScalabilityAtRuntime();

	return CreateQualityLevelMask(bCanChangeEQAtRuntime ? INDEX_NONE : GetQualityLevel());

#endif
}

bool FNiagaraPlatformSet::IsEnabled(const UDeviceProfile* Profile, int32 QualityLevel, bool bConsiderCurrentStateOnly)const
{
	checkSlow(Profile);

	//Check CVar conditions first.
	//Only apply CVars if we're checking current state or we're not on a platform that can change scalability CVars at runtime.
	if (bConsiderCurrentStateOnly || CanChangeScalabilityAtRuntime(Profile) == false)
	{
		for (const FNiagaraPlatformSetCVarCondition& CVarCondition : CVarConditions)
		{
			//Bail if any cvar condition isn't met.
			if (CVarCondition.IsEnabledForDeviceProfile(Profile, bConsiderCurrentStateOnly) == false)
			{
				return false;
			}
		}
	}

	//Does this platform set match the passed in current quality level?
	int32 TestQLMask = CreateQualityLevelMask(QualityLevel);
	bool bIsEnabledByEQ = (QualityLevelMask & TestQLMask) != 0;

	//Does it match the device profile's quality level and do we care?
	int32 ProfileQLMask = GetEffectQualityMaskForDeviceProfile(Profile);
	bIsEnabledByEQ &= bConsiderCurrentStateOnly || ((ProfileQLMask & QualityLevelMask) != 0);

	if (DeviceProfileStates.Num() > 0)
	{
		//Walk up the parent hierarchy to see if we have an explicit state for this profile set.
		const UDeviceProfile* CurrProfile = Profile;
		while (CurrProfile)
		{
			if (const FNiagaraDeviceProfileStateEntry* StateEntry = DeviceProfileStates.FindByPredicate([&](const FNiagaraDeviceProfileStateEntry& ProfileState) {return ProfileState.ProfileName == CurrProfile->GetFName(); }))
			{
				ENiagaraPlatformSelectionState SelectionState = StateEntry->GetState(QualityLevel);
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
	CachedQLMasksPerDeviceProfile.Empty();
	CachedPlatformIniSettings.Empty();
	FDeviceProfileValueCache::Empty();
#endif

	LastDirtiedFrame = GFrameNumber;
}

bool FNiagaraPlatformSet::IsEnabledForPlatform(const FString& PlatformName)const
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

bool FNiagaraPlatformSet::ShouldPruneEmittersOnCook(const FString& PlatformName)
{
#if WITH_EDITOR
	FPlatformIniSettings& Settings = GetPlatformIniSettings(PlatformName);
	return Settings.bPruneEmittersOnCook != 0;
#else
	return GbPruneEmittersOnCook != 0;
#endif
}

bool FNiagaraPlatformSet::CanChangeScalabilityAtRuntime(const UDeviceProfile* DeviceProfile)
{
	if (ensure(DeviceProfile))
	{
#if WITH_EDITOR
		FPlatformIniSettings& PlatformSettings = GetPlatformIniSettings(DeviceProfile->DeviceType);
		return PlatformSettings.bCanChangeScalabilitySettingsAtRuntime != 0;
#else
		return CanChangeScalabilityAtRuntime();
#endif
	}
	return true;//Assuming true if we fail to find the platform seems safest.
}

#if WITH_EDITOR

// bool FNiagaraPlatformSet::Conflicts(const FNiagaraPlatformSet& Other, TArray<const UDeviceProfile*>& OutConflictingProfiles, bool bIncludeProfilesWithVariableQualityLevel)const
// {
// 	for (const UObject* ProfileObj : UDeviceProfileManager::Get().Profiles)
// 	{
// 		if (const UDeviceProfile* Profile = Cast<const UDeviceProfile>(ProfileObj))
// 		{
// 			if (bIncludeProfilesWithVariableQualityLevel || CanChangeScalabilityAtRuntime(Profile) == false)
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
	return ((1 << EffectQuality) & QualityLevelMask) != 0;
}

void FNiagaraPlatformSet::SetEnabledForEffectQuality(int32 EffectQuality, bool bEnabled)
{
	int32 EQBit = (1 << EffectQuality);
	if (bEnabled)
	{
		QualityLevelMask |= EQBit;
	}
	else
	{
		QualityLevelMask &= ~EQBit;
	}
	OnChanged();
}

void FNiagaraPlatformSet::SetDeviceProfileState(UDeviceProfile* Profile, int32 QualityLevel, ENiagaraPlatformSelectionState NewState)
{
	int32 Index = INDEX_NONE;
	Index = DeviceProfileStates.IndexOfByPredicate([&](const FNiagaraDeviceProfileStateEntry& Entry) { return Entry.ProfileName == Profile->GetFName(); });

	int32 ProfileQLMask = GetEffectQualityMaskForDeviceProfile(Profile);
	if (ProfileQLMask != INDEX_NONE)
	{
		//For platforms that cannot change EQ at runtime we mark all state bits when setting state here so that if someone changes their EQ setting in the future, the state will be preserved.
		QualityLevel = INDEX_NONE;
	}

	if(Index == INDEX_NONE)
	{
		if (NewState != ENiagaraPlatformSelectionState::Default)
		{
			FNiagaraDeviceProfileStateEntry& NewEntry = DeviceProfileStates.AddDefaulted_GetRef();
			NewEntry.ProfileName = Profile->GetFName();
			NewEntry.SetState(QualityLevel, NewState);
		}
	}
	else
	{
		DeviceProfileStates[Index].SetState(QualityLevel, NewState);

		if (DeviceProfileStates[Index].AllDefaulted())
		{
			DeviceProfileStates.RemoveAtSwap(Index);//We don't need to store the default state. It's implied by no entry.
		}
	}
	OnChanged();
}

ENiagaraPlatformSelectionState FNiagaraPlatformSet::GetDeviceProfileState(UDeviceProfile* Profile, int32 QualityLevel)const
{
	if (const FNiagaraDeviceProfileStateEntry* ExistingEntry = DeviceProfileStates.FindByPredicate([&](const FNiagaraDeviceProfileStateEntry& Entry) { return Entry.ProfileName == Profile->GetFName(); }))
	{
		int32 ProfileQLMask = GetEffectQualityMaskForDeviceProfile(Profile);
		if (ProfileQLMask == INDEX_NONE || ProfileQLMask & CreateQualityLevelMask(QualityLevel))
		{
			//For profiles that cannot change scalability at runtime we store all flags in their state so that if anyone ever changes their EQ Cvar, the state setting remains valid.
			//This just means we also have to ensure this is the correct EQ here.

			return ExistingEntry->GetState(QualityLevel);
		}
	}
	return ENiagaraPlatformSelectionState::Default;
}

void FNiagaraPlatformSet::OnChanged()
{
	LastBuiltFrame = 0;
}

bool FNiagaraPlatformSet::GatherConflicts(const TArray<const FNiagaraPlatformSet*>& PlatformSets, TArray<FNiagaraPlatformSetConflictInfo>& OutConflicts)
{
	const UNiagaraSettings* Settings = GetDefault<UNiagaraSettings>();
	check(Settings);
	int32 NumLevels = Settings->QualityLevels.Num();

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
					ConflictEntry.QualityLevelMask = ConflictMask;
				}
			}
		}
	}

	return OutConflicts.Num() > 0;
}

FNiagaraPlatformSet::FPlatformIniSettings& FNiagaraPlatformSet::GetPlatformIniSettings(const FString& PlatformName)
{
	if (FPlatformIniSettings* CachedSettings = CachedPlatformIniSettings.Find(*PlatformName))
	{
		return *CachedSettings;
	}

	//Load config files in which we can reasonable expect to find fx.Niagara.QualityLevel and may be set.
	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName);//Should use BaseProfileName? Are either of these ensured to be correct? I worry this is brittle.

	FConfigFile GameSettings;
	FConfigCacheIni::LoadLocalIniFile(GameSettings, TEXT("Game"), true, *PlatformName);//Should use BaseProfileName? Are either of these ensured to be correct? I worry this is brittle.

	FConfigFile ScalabilitySettings;
	FConfigCacheIni::LoadLocalIniFile(ScalabilitySettings, TEXT("Scalability"), true, *PlatformName);//Should use BaseProfileName? Are either of these ensured to be correct? I worry this is brittle.

	auto FindCVarValue = [&](const TCHAR* Section, const TCHAR* CVarName, int32& OutVal)
	{
		bool bFound = true;
		if (!ScalabilitySettings.GetInt(Section, CVarName, OutVal))
		{
			if (!GameSettings.GetInt(Section, CVarName, OutVal))
			{
				if (!EngineSettings.GetInt(Section, CVarName, OutVal))
				{
					bFound = false;
				}
			}
		}
		return bFound;
	};

	//The effect quality for this platform.
	int32 EffectsQuality = Scalability::DefaultQualityLevel;

	//If this platform can change scalability settings at runtime then we return the full mask.
	int32 CanChangeScalabiltiySettings = 0;

	FindCVarValue(TEXT("SystemSettings"), CanChangeEQCVarName, CanChangeScalabiltiySettings);

	if (!FindCVarValue(TEXT("ScalabilityGroups"), TEXT("sg.EffectsQuality"), EffectsQuality))
	{
		FindCVarValue(TEXT("SystemSettings"), TEXT("sg.EffectsQuality"), EffectsQuality);
	}

	//Get the platforms default quality level setting.
	//This can be overridden directly in a device profile or indirectly by overriding effects quality.

	int32 PruneEmittersOnCook = GbPruneEmittersOnCook;
	FindCVarValue(TEXT("SystemSettings"), PruneEmittersOnCookName, PruneEmittersOnCook);

	FPlatformIniSettings& NewSetting = CachedPlatformIniSettings.Add(*PlatformName);
	NewSetting = FPlatformIniSettings(CanChangeScalabiltiySettings, PruneEmittersOnCook, EffectsQuality);

	//Find the Niagara Quality Levels set for each EffectsQuality Level for this platform.
	int32 NumEffectsQualities = Scalability::GetQualityLevelCounts().EffectsQuality;
	FString EQStr;
	for (int32 EQ = 0; EQ < NumEffectsQualities; ++EQ)
	{
		FString SectionName = Scalability::GetScalabilitySectionString(TEXT("EffectsQuality"), EQ, NumEffectsQualities);
		int32 NiagaraQualityLevelForEQ = DefaultQualityLevel;
		ScalabilitySettings.GetInt(*SectionName, NiagaraQualityLevelName, NiagaraQualityLevelForEQ);
		NewSetting.QualityLevelsPerEffectsQuality.Add(NiagaraQualityLevelForEQ);
		EQStr += FString::Printf(TEXT("EQ:%d = NQL:%d\n"), EQ, NiagaraQualityLevelForEQ);
	}

	//UE_LOG(LogNiagara, Warning, TEXT("\n=====================================================\n%s - PlatformSettings(%d, %d, %d)\n%s\n========================================="), *PlatformName, CanChangeEffectQuality, PruneEmittersOnCook, EffectsQuality, *EQStr);
	return NewSetting;
}

int32 FNiagaraPlatformSet::GetEffectQualityMaskForPlatform(const FString& PlatformName)
{
	FPlatformIniSettings& PlatformSettings = GetPlatformIniSettings(PlatformName);
	return PlatformSettings.bCanChangeScalabilitySettingsAtRuntime;
}
#endif

//////////////////////////////////////////////////////////////////////////

#if WITH_EDITOR
TMap<const UDeviceProfile*, FDeviceProfileValueCache::FCVarValueMap> FDeviceProfileValueCache::CachedDeviceProfileValues;
TMap<FName, FDeviceProfileValueCache::FCVarValueMap> FDeviceProfileValueCache::CachedPlatformValues;

bool FDeviceProfileValueCache::GetValueInternal(const UDeviceProfile* DeviceProfile, FName CVarName, FString& OutValue)
{
	//First look if we've asked for this CVar for this device profile before.
	if (FCVarValueMap* CVarMap = CachedDeviceProfileValues.Find(DeviceProfile))
	{
		if (FString* CVarValueString = CVarMap->Find(CVarName))
		{
			OutValue = *CVarValueString;
			return true;
		}
	}

	//If not we'll need to look for it.
	
	FCVarValueMap& CVarMap = CachedDeviceProfileValues.Add(DeviceProfile);

	//First see if the device profile has it explicitly set.
	if (DeviceProfile->GetConsolidatedCVarValue(*CVarName.ToString(), OutValue, false))
	{
		CVarMap.Add(CVarName, OutValue);
		return true;
	}

	//Otherwise we need to check the ini files for the profiles platform.
	//It'd be lovely if there were just a utility function to call that did this (or better) for us.
	
	const FString& PlatformName = DeviceProfile->DeviceType;

	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *PlatformName);

	FConfigFile GameSettings;
	FConfigCacheIni::LoadLocalIniFile(GameSettings, TEXT("Game"), true, *PlatformName);

	FConfigFile ScalabilitySettings;
	FConfigCacheIni::LoadLocalIniFile(ScalabilitySettings, TEXT("Scalability"), true, *PlatformName);

	auto FindCVarValue = [&](const TCHAR* Section, const TCHAR* CVarName, FString& OutVal)
	{
		bool bFound = true;
		if (!ScalabilitySettings.GetString(Section, CVarName, OutVal))
		{
			if (!GameSettings.GetString(Section, CVarName, OutVal))
			{
				if (!EngineSettings.GetString(Section, CVarName, OutVal))
				{
					bFound = false;
				}
			}
		}
		return bFound;
	};

	if (FindCVarValue(TEXT("SystemSettings"), *CVarName.ToString(), OutValue))
	{
		CVarMap.Add(CVarName, OutValue);
		return true;
	}

	//Failing all that we just take the default value.
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName.ToString()))
	{
		OutValue = CVar->GetString();
		CVarMap.Add(CVarName, OutValue);
		return true;
	}

	//Only really possible if the CVar doesn't exist.
	return false;
}

void FDeviceProfileValueCache::Empty()
{
	CachedDeviceProfileValues.Empty();
	CachedPlatformValues.Empty();
}

template<typename T>
bool FDeviceProfileValueCache::GetValue(const UDeviceProfile* DeviceProfile, FName CVarName, T& OutValue)
{
	FString ValString;
	if (GetValueInternal(DeviceProfile, CVarName, ValString))
	{
		LexFromString(OutValue, *ValString);
		return true;
	}
	return false;
}

#endif

//////////////////////////////////////////////////////////////////////////

TMap<FName, FDelegateHandle> FNiagaraPlatformSetCVarCondition::CVarChangedDelegateHandles;
FCriticalSection FNiagaraPlatformSetCVarCondition::ChangedDelegateHandlesCritSec;

void FNiagaraPlatformSetCVarCondition::OnCVarChanged(IConsoleVariable* CVar)
{
	//TODO: Iterate over all systems and recheck all their PlatformSet active states.
	//Any states that have changes should trigger a reinit of the system in question.

	//For right now just brute force reinit everything.
	//At least this is limited to only the CVars that these conditions are reading.
	FNiagaraPlatformSet::InvalidateCachedData();

	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* System = *It;
		check(System);
		System->OnScalabilityCVarChanged();
	}
}

FNiagaraPlatformSetCVarCondition::FNiagaraPlatformSetCVarCondition()
	: bUseMinInt(true)
	, bUseMaxInt(false)
	, bUseMinFloat(true)
	, bUseMaxFloat(false)
{

}

IConsoleVariable* FNiagaraPlatformSetCVarCondition::GetCVar()const
{
	if (CachedCVar == nullptr)
	{
		IConsoleManager& CVarMan = IConsoleManager::Get();
		CachedCVar = CVarMan.FindConsoleVariable(*CVarName.ToString());
		if(CachedCVar)
		{
			FScopeLock Lock(&ChangedDelegateHandlesCritSec);
			FDelegateHandle* Handle = CVarChangedDelegateHandles.Find(CVarName);
			if(!Handle)
			{
				//We've not bound to this cvar's change callback yet. Do so now.
				CVarChangedDelegateHandles.Add(CVarName) = CachedCVar->OnChangedDelegate().AddStatic(&FNiagaraPlatformSetCVarCondition::OnCVarChanged);
			}
		}
	}
	return CachedCVar;
}

void FNiagaraPlatformSetCVarCondition::SetCVar(FName InCVarName)
{
	CVarName = InCVarName;
	CachedCVar = nullptr;
}

bool FNiagaraPlatformSetCVarCondition::IsEnabledForPlatform(const FString& PlatformName)const
{
	for (const UObject* ProfileObj : UDeviceProfileManager::Get().Profiles)
	{
		if (const UDeviceProfile* Profile = Cast<const UDeviceProfile>(ProfileObj))
		{
			if (Profile->DeviceType == PlatformName)
			{
				if (IsEnabledForDeviceProfile(Profile, false) != 0)
				{
					return true;//At least one profile for this platform is enabled.
				}
			}
		}
	}
	return false;
}

template<> bool FNiagaraPlatformSetCVarCondition::GetCVarValue<bool>(IConsoleVariable* CVar)const { return CVar->GetBool(); }
template<> int32 FNiagaraPlatformSetCVarCondition::GetCVarValue<int32>(IConsoleVariable* CVar)const { return CVar->GetInt(); }
template<> float FNiagaraPlatformSetCVarCondition::GetCVarValue<float>(IConsoleVariable* CVar)const { return CVar->GetFloat(); }

template<typename T>
bool FNiagaraPlatformSetCVarCondition::IsEnabledForDeviceProfile_Internal(const UDeviceProfile* DeviceProfile, bool bCheckCurrentStateOnly)const
{
#if WITH_EDITOR
	T ProfileValue;
	if (bCheckCurrentStateOnly == false && FDeviceProfileValueCache::GetValue(DeviceProfile, CVarName, ProfileValue))
	{
		return CheckValue(ProfileValue);
	}
#endif

	if (IConsoleVariable* CVar = GetCVar())
	{
		return CheckValue(GetCVarValue<T>(CVar));
	}
	return false;
}

bool FNiagaraPlatformSetCVarCondition::IsEnabledForDeviceProfile(const UDeviceProfile* DeviceProfile, bool bCheckCurrentStateOnly)const
{
	if (IConsoleVariable* CVar = GetCVar())
	{
		if (CVar->IsVariableBool())
		{
			return IsEnabledForDeviceProfile_Internal<bool>(DeviceProfile, bCheckCurrentStateOnly);
		}
		else if (CVar->IsVariableInt())
		{
			return IsEnabledForDeviceProfile_Internal<int32>(DeviceProfile, bCheckCurrentStateOnly);
		}
		else if (CVar->IsVariableFloat())
		{
			return IsEnabledForDeviceProfile_Internal<float>(DeviceProfile, bCheckCurrentStateOnly);
		}
		else
		{
			ensureMsgf(false, TEXT("CVar % is of an unsupported type for FNiagaraPlatformSetCVarCondition. Supported types are Bool, Int and Float. This should not be possible unless the CVar's type has been chagned."), *CVarName.ToString());
		}
	}
	else
	{
		UE_LOG(LogNiagara, Warning, TEXT("Niagara Platform Set is trying to use a CVar that doesn't exist. %s"), *CVarName.ToString());
	}

	return false;
}

#undef LOCTEXT_NAMESPACE