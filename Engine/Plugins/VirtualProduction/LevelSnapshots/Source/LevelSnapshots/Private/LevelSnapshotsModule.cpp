// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsModule.h"

#include "BlacklistRestorabilityOverrider.h"
#include "LevelSnapshotsEditorProjectSettings.h"
#include "Restorability/PropertyComparisonParams.h"
#include "Restorability/StaticMeshCollisionPropertyComparer.h"
#include "SnapshotRestorability.h"

#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Modules/ModuleManager.h"
#include "Restorability/StaticMeshCollisionPropertyComparer.h"

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

	FStaticMeshCollisionPropertyComparer::Register(*this);
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

void FLevelSnapshotsModule::RegisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer)
{
	PropertyComparers.FindOrAdd(Class).AddUnique(Comparer);
}

void FLevelSnapshotsModule::UnregisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer)
{
	TArray<TSharedRef<IPropertyComparer>>* Comparers = PropertyComparers.Find(Class);
	if (!Comparers)
	{
		return;
	}
	Comparers->RemoveSwap(Comparer);

	if (Comparers->Num() == 0)
	{
		PropertyComparers.Remove(Class);
	}
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

FPropertyComparerArray FLevelSnapshotsModule::GetPropertyComparerForClass(UClass* Class) const
{
	FPropertyComparerArray Result;
	for (UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		const TArray<TSharedRef<IPropertyComparer>>* Comparers = PropertyComparers.Find(CurrentClass);
		if (Comparers)
		{
			Result.Append(*Comparers);
		}
	}
	return Result;
}

IPropertyComparer::EPropertyComparison FLevelSnapshotsModule::ShouldConsiderPropertyEqual(const FPropertyComparerArray& Comparers, const FPropertyComparisonParams& Params) const
{
	for (const TSharedRef<IPropertyComparer>& Comparer : Comparers)
	{
		const IPropertyComparer::EPropertyComparison Result = Comparer->ShouldConsiderPropertyEqual(Params);
		if (Result != IPropertyComparer::EPropertyComparison::CheckNormally)
		{
			return Result;
		}
	}
	return IPropertyComparer::EPropertyComparison::CheckNormally;
}

IMPLEMENT_MODULE(FLevelSnapshotsModule, LevelSnapshots)
