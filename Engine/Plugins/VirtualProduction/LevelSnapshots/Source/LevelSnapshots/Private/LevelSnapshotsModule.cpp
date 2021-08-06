// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsModule.h"

#include "BlacklistRestorabilityOverrider.h"
#include "LevelSnapshotsEditorProjectSettings.h"
#include "Restorability/PropertyComparisonParams.h"
#include "Restorability/StaticMeshCollisionPropertyComparer.h"

#include "EngineUtils.h"
#include "LevelSnapshotsLog.h"
#include "Algo/AllOf.h"
#include "Modules/ModuleManager.h"

namespace
{
	void AddSoftObjectPathSupport(FLevelSnapshotsModule& Module)
	{
		// By default FSnapshotRestorability::IsRestorableProperty requires properties to have the CPF_Edit specifier
		// FSoftObjectPath does not have this so we need to whitelist its properties

		UStruct* SoftObjectPath = FindObject<UStruct>(nullptr, TEXT("/Script/CoreUObject.SoftObjectPath"));
		if (!ensureMsgf(SoftObjectPath, TEXT("Investigate why this class could not be found")))
		{
			return;
		}

		TSet<const FProperty*> Properties;
		for (TFieldIterator<FProperty> FieldIt(SoftObjectPath); FieldIt; ++FieldIt)
		{
			Properties.Add(*FieldIt);
		}

		Module.AddWhitelistedProperties(Properties);
	}
}

FLevelSnapshotsModule& FLevelSnapshotsModule::GetInternalModuleInstance()
{
	static FLevelSnapshotsModule& ModuleInstance = *[]() -> FLevelSnapshotsModule*
	{
		UE_CLOG(!FModuleManager::Get().IsModuleLoaded("LevelSnapshots"), LogLevelSnapshots, Fatal, TEXT("You called GetInternalModuleInstance before the module was initialised."));
		return &FModuleManager::GetModuleChecked<FLevelSnapshotsModule>("LevelSnapshots");
	}();
	return ModuleInstance;
}

void FLevelSnapshotsModule::StartupModule()
{
	const TSharedRef<FBlacklistRestorabilityOverrider> Blacklist = MakeShared<FBlacklistRestorabilityOverrider>(
		FBlacklistRestorabilityOverrider::FGetBlacklist::CreateLambda([]() -> const FRestorationBlacklist&
		{
			ULevelSnapshotsEditorProjectSettings* Settings = GetMutableDefault<ULevelSnapshotsEditorProjectSettings>();
			return Settings->Blacklist;
		})
	);
	RegisterRestorabilityOverrider(Blacklist);
	
	AddSoftObjectPathSupport(*this);

	FStaticMeshCollisionPropertyComparer::Register(*this);
}

void FLevelSnapshotsModule::ShutdownModule()
{
	Overrides.Reset();
}

void FLevelSnapshotsModule::AddCanTakeSnapshotDelegate(FName DelegateName, FCanTakeSnapshot Delegate)
{
	CanTakeSnapshotDelegates.FindOrAdd(DelegateName) = Delegate;
}

void FLevelSnapshotsModule::RemoveCanTakeSnapshotDelegate(FName DelegateName)
{
	CanTakeSnapshotDelegates.Remove(DelegateName);
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

void FLevelSnapshotsModule::RegisterCustomObjectSerializer(UClass* Class, TSharedRef<ICustomObjectSnapshotSerializer> CustomSerializer, bool bIncludeBlueprintChildClasses)
{
	if (!ensureAlways(Class))
	{
		return;
	}
	
	const bool bIsBlueprint = Class->IsInBlueprint();
	if (!ensureAlwaysMsgf(!bIsBlueprint, TEXT("Registering to Blueprint classes is unsupported because they can be reinstanced at any time")))
	{
		return;
	}

	FCustomSerializer* ExistingSerializer = CustomSerializers.Find(Class);
	if (!ensureAlwaysMsgf(!ExistingSerializer, TEXT("Class already registered")))
	{
		return;
	}

	CustomSerializers.Add(Class, { CustomSerializer, bIncludeBlueprintChildClasses });
}

void FLevelSnapshotsModule::UnregisterCustomObjectSerializer(UClass* Class)
{
	CustomSerializers.Remove(Class);
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

bool FLevelSnapshotsModule::CanTakeSnapshot(const FPreTakeSnapshotEventData& Event) const
{
	return Algo::AllOf(CanTakeSnapshotDelegates, [&Event](const TTuple<FName,FCanTakeSnapshot>& Pair)
		{
			if (Pair.Get<1>().IsBound())
			{
				return Pair.Get<1>().Execute(Event);
			}
			return true;
		});
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

TSharedPtr<ICustomObjectSnapshotSerializer> FLevelSnapshotsModule::GetCustomSerializerForClass(UClass* Class) const
{
	// Walk to first native parent
	const bool bPassedInBlueprint = Class->IsInBlueprint();
	while (Class && Class->IsInBlueprint())
	{
		Class = Class->GetSuperClass();
	}

	if (ensureAlways(Class))
	{
		const FCustomSerializer* Result = CustomSerializers.Find(Class);
		return (Result && (!bPassedInBlueprint || Result->bIncludeBlueprintChildren)) ? Result->Serializer : TSharedPtr<ICustomObjectSnapshotSerializer>();
	}

	return nullptr;
}

IMPLEMENT_MODULE(FLevelSnapshotsModule, LevelSnapshots)
