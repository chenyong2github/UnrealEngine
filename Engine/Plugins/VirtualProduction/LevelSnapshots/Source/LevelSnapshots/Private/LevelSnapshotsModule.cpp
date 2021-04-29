// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsModule.h"


#include "BlacklistRestorabilityOverrider.h"
#include "EngineUtils.h"
#include "LevelSnapshotsEditorProjectSettings.h"
#include "SnapshotRestorability.h"
#include "Modules/ModuleManager.h"

FLevelSnapshotsModule& FLevelSnapshotsModule::GetInternalModuleInstance()
{
	return FModuleManager::Get().GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
}

void FLevelSnapshotsModule::StartupModule()
{
	FSnapshotRestorability::Module = this;
	
	const TSharedRef<FBlacklistRestorabilityOverrider> Blacklist = MakeShared<FBlacklistRestorabilityOverrider>(
		FBlacklistRestorabilityOverrider::FGetBlacklist::CreateLambda([]() -> const FRestorationBlacklist&
		{
			ULevelSnapshotsEditorProjectSettings* Settings = GetMutableDefault<ULevelSnapshotsEditorProjectSettings>();
			return Settings->Blacklist;
		})
	);

	RegisterRestorabilityOverrider(Blacklist);
}

void FLevelSnapshotsModule::ShutdownModule()
{
	FSnapshotRestorability::Module = nullptr;
	
	Overrides.Reset();
}

void FLevelSnapshotsModule::RegisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider)
{
	Overrides.AddUnique(Overrider);
}

void FLevelSnapshotsModule::UnregisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider)
{
	Overrides.RemoveSwap(Overrider);
}

void FLevelSnapshotsModule::AddWhitelistedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		WhitelistedProperties.Add(Property);
	}
}

void FLevelSnapshotsModule::RemoveWhitelistedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		WhitelistedProperties.Remove(Property);
	}
}

void FLevelSnapshotsModule::AddBlacklistedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		BlacklistedProperties.Add(Property);
	}
}

void FLevelSnapshotsModule::RemoveBlacklistedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		BlacklistedProperties.Remove(Property);
	}
}

const TArray<TSharedRef<ISnapshotRestorabilityOverrider>>& FLevelSnapshotsModule::GetOverrides() const
{
	return Overrides;
}

bool FLevelSnapshotsModule::IsPropertyWhitelisted(const FProperty* Property) const
{
	return WhitelistedProperties.Contains(Property);
}

bool FLevelSnapshotsModule::IsPropertyBlacklisted(const FProperty* Property) const
{
	return BlacklistedProperties.Contains(Property);
}

IMPLEMENT_MODULE(FLevelSnapshotsModule, LevelSnapshots)
