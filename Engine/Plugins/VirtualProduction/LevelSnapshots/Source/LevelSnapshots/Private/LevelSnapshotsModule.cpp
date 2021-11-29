// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsModule.h"

#include "ClassRestorationSkipper.h"
#include "LevelSnapshotsSettings.h"
#include "LevelSnapshotsLog.h"
#include "Params/PropertyComparisonParams.h"
#include "Restorability/CollisionRestoration.h"
#include "Restorability/GridPlacementRestoration.h"

#include "Algo/AllOf.h"
#include "Components/ActorComponent.h"
#include "EngineUtils.h"
#include "Algo/Transform.h"
#include "Engine/Brush.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "ISettingsModule.h"
#endif

namespace UE::LevelSnapshots::Private::Internal
{
	static void AddSoftObjectPathSupport(UE::LevelSnapshots::Private::FLevelSnapshotsModule& Module)
	{
		// FSnapshotRestorability::IsRestorableProperty requires properties to have the CPF_Edit specifier
		// FSoftObjectPath does not have this so we need to explicitly allow its properties

		UStruct* SoftObjectClassPath = FindObject<UStruct>(nullptr, TEXT("/Script/CoreUObject.SoftObjectPath"));
		if (!ensureMsgf(SoftObjectClassPath, TEXT("Investigate why this class could not be found")))
		{
			return;
		}

		TSet<const FProperty*> SoftObjectPathProperties;
		Algo::Transform(TFieldRange<const FProperty>(SoftObjectClassPath), SoftObjectPathProperties, [](const FProperty* Prop) { return Prop;} );
		Module.AddExplicitilySupportedProperties(SoftObjectPathProperties);
	}

	static void AddAttachParentSupport(UE::LevelSnapshots::Private::FLevelSnapshotsModule& Module)
	{
		// These properties are not visible by default because they're not CPF_Edit
		const FProperty* AttachParent = USceneComponent::StaticClass()->FindPropertyByName(FName("AttachParent"));
		const FProperty* AttachSocketName = USceneComponent::StaticClass()->FindPropertyByName(FName("AttachSocketName"));
		// RootComponent is usually set automatically but sometimes not... for example spawning AActor with instanced components only
		const FProperty* RootComponent = AActor::StaticClass()->FindPropertyByName(FName("RootComponent"));
		if (ensure(AttachParent && AttachSocketName))
		{
			Module.AddExplicitilySupportedProperties({ AttachParent, AttachSocketName, RootComponent });
		}
	}

	static void DisableIrrelevantBrushSubobjects(UE::LevelSnapshots::Private::FLevelSnapshotsModule& Module)
	{
#if WITH_EDITORONLY_DATA
		// ABrush::BrushBuilder is CPF_Edit but no user ever cares about it. We don't want it to make volumes to show up as changed.
		const FProperty* BrushBuilder = ABrush::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(ABrush, BrushBuilder));
		if (ensure(BrushBuilder))
		{
			Module.AddExplicitlyUnsupportedProperties({ BrushBuilder });
		}
#endif
	}

	static void DisableIrrelevantWorldSettings(UE::LevelSnapshots::Private::FLevelSnapshotsModule& Module)
	{
		// AWorldSettings::NavigationSystemConfig is CPF_Edit but no user ever cares about it.
		const FProperty* NavigationSystemConfig = AWorldSettings::StaticClass()->FindPropertyByName(FName("NavigationSystemConfig"));
		if (ensure(NavigationSystemConfig))
		{
			Module.AddExplicitlyUnsupportedProperties({ NavigationSystemConfig });
		}
	}

	static void DisableIrrelevantMaterialInstanceProperties(UE::LevelSnapshots::Private::FLevelSnapshotsModule& Module)
	{
		// This property causes diffs sometimes for unexplained reasons when creating in construction script... does not seem to be important
		const FProperty* BasePropertyOverrides = UMaterialInstance::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMaterialInstance, BasePropertyOverrides));
		if (ensure(BasePropertyOverrides))
		{
			Module.AddExplicitlyUnsupportedProperties({ BasePropertyOverrides });
		}
	}

	static void DisableIrrelevantActorProperties(UE::LevelSnapshots::Private::FLevelSnapshotsModule& Module)
	{
#if WITH_EDITORONLY_DATA
		const FProperty* ActorGuid = AActor::StaticClass()->FindPropertyByName(FName("ActorGuid"));
		if (ensure(ActorGuid))
		{
			Module.AddExplicitlyUnsupportedProperties({ ActorGuid });
		}
#endif
	}
}

UE::LevelSnapshots::Private::FLevelSnapshotsModule& UE::LevelSnapshots::Private::FLevelSnapshotsModule::GetInternalModuleInstance()
{
	static UE::LevelSnapshots::Private::FLevelSnapshotsModule& ModuleInstance = *[]() -> UE::LevelSnapshots::Private::FLevelSnapshotsModule*
	{
		UE_CLOG(!FModuleManager::Get().IsModuleLoaded("LevelSnapshots"), LogLevelSnapshots, Fatal, TEXT("You called GetInternalModuleInstance before the module was initialised."));
		return &FModuleManager::GetModuleChecked<UE::LevelSnapshots::Private::FLevelSnapshotsModule>("LevelSnapshots");
	}();
	return ModuleInstance;
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::StartupModule()
{
	// Hook up project settings
	const TSharedRef<FClassRestorationSkipper> ClassSkipper = MakeShared<FClassRestorationSkipper>(
		FClassRestorationSkipper::FGetSkippedClassList::CreateLambda([]() -> const FSkippedClassList&
		{
			ULevelSnapshotsSettings* Settings = GetMutableDefault<ULevelSnapshotsSettings>();
			return Settings->SkippedClasses;
		})
	);
	RegisterRestorabilityOverrider(ClassSkipper);

	// Enable / disable troublesome properties
	UE::LevelSnapshots::Private::Internal::AddSoftObjectPathSupport(*this);
	UE::LevelSnapshots::Private::Internal::AddAttachParentSupport(*this);
	UE::LevelSnapshots::Private::Internal::DisableIrrelevantBrushSubobjects(*this);
	UE::LevelSnapshots::Private::Internal::DisableIrrelevantWorldSettings(*this);
	UE::LevelSnapshots::Private::Internal::DisableIrrelevantMaterialInstanceProperties(*this);
	UE::LevelSnapshots::Private::Internal::DisableIrrelevantActorProperties(*this);

	// Interact with special engine features
	FCollisionRestoration::Register(*this);
	GridPlacementRestoration::Register(*this);

#if WITH_EDITOR
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		// User Project Settings
		SettingsModule.RegisterSettings("Project", "Plugins", "Level Snapshots",
			NSLOCTEXT("LevelSnapshots", "LevelSnapshotsSettingsCategoryDisplayName", "Level Snapshots"),
			NSLOCTEXT("LevelSnapshots", "LevelSnapshotsSettingsDescription", "Configure the Level Snapshots user settings"),
			GetMutableDefault<ULevelSnapshotsSettings>());
	}
#endif
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::ShutdownModule()
{
	Overrides.Reset();
	PropertyComparers.Reset();
	CustomSerializers.Reset();
	RestorationListeners.Reset();
	
#if WITH_EDITOR
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "Level Snapshots");
	}
#endif
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RegisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider)
{
	Overrides.AddUnique(Overrider);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::UnregisterRestorabilityOverrider(TSharedRef<ISnapshotRestorabilityOverrider> Overrider)
{
	Overrides.RemoveSwap(Overrider);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::AddSkippedSubobjectClasses(const TSet<UClass*>& Classes)
{
	for (UClass* Class : Classes)
	{
		check(Class);

		if (!Class
			|| !ensureAlwaysMsgf(!Class->IsChildOf(AActor::StaticClass()), TEXT("Invalid function input: Actors can never be subobjects. Check your code."))
			|| !ensureAlwaysMsgf(!Class->IsChildOf(UActorComponent::StaticClass()), TEXT("Invalid function input: Disallow components using RegisterRestorabilityOverrider and implementing ISnapshotRestorabilityOverrider::IsComponentDesirableForCapture instead.")))
		{
			continue;
		}

		SkippedSubobjectClasses.Add(Class);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RemoveSkippedSubobjectClasses(const TSet<UClass*>& Classes)
{
	for (UClass* Class : Classes)
	{
		SkippedSubobjectClasses.Remove(Class);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RegisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer)
{
	PropertyComparers.FindOrAdd(Class).AddUnique(Comparer);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::UnregisterPropertyComparer(UClass* Class, TSharedRef<IPropertyComparer> Comparer)
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

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RegisterCustomObjectSerializer(UClass* Class, TSharedRef<ICustomObjectSnapshotSerializer> CustomSerializer, bool bIncludeBlueprintChildClasses)
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

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::UnregisterCustomObjectSerializer(UClass* Class)
{
	CustomSerializers.Remove(Class);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RegisterSnapshotLoader(TSharedRef<ISnapshotLoader> Loader)
{
	SnapshotLoaders.AddUnique(Loader);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::UnregisterSnapshotLoader(TSharedRef<ISnapshotLoader> Loader)
{
	SnapshotLoaders.RemoveSingle(Loader);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RegisterRestorationListener(TSharedRef<IRestorationListener> Listener)
{
	RestorationListeners.AddUnique(Listener);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::UnregisterRestorationListener(TSharedRef<IRestorationListener> Listener)
{
	RestorationListeners.RemoveSingle(Listener);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::AddExplicitilySupportedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		SupportedProperties.Add(Property);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RemoveAdditionallySupportedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		SupportedProperties.Remove(Property);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::AddExplicitlyUnsupportedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		UnsupportedProperties.Add(Property);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RemoveExplicitlyUnsupportedProperties(const TSet<const FProperty*>& Properties)
{
	for (const FProperty* Property : Properties)
	{
		UnsupportedProperties.Remove(Property);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::AddSkippedClassDefault(const UClass* Class)
{
	SkippedCDOs.Add(Class);
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RemoveSkippedClassDefault(const UClass* Class)
{
	SkippedCDOs.Remove(Class);
}

bool UE::LevelSnapshots::Private::FLevelSnapshotsModule::ShouldSkipClassDefaultSerialization(const UClass* Class) const
{
	for (const UClass* CurrentClass = Class; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		if (SkippedCDOs.Contains(CurrentClass))
		{
			return true;
		}
	}

	return false;
}

bool UE::LevelSnapshots::Private::FLevelSnapshotsModule::ShouldSkipSubobjectClass(const UClass* Class) const
{
	if (Class->IsChildOf(UActorComponent::StaticClass()))
	{
		return false;
	}

	bool bFoundSkippedClass = false;
	for (const UClass* CurrentClass = Class; !bFoundSkippedClass && CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		bFoundSkippedClass = SkippedSubobjectClasses.Contains(CurrentClass);
	}

	return bFoundSkippedClass;
}

const TArray<TSharedRef<UE::LevelSnapshots::ISnapshotRestorabilityOverrider>>& UE::LevelSnapshots::Private::FLevelSnapshotsModule::GetOverrides() const
{
	return Overrides;
}

bool UE::LevelSnapshots::Private::FLevelSnapshotsModule::IsPropertyExplicitlySupported(const FProperty* Property) const
{
	return SupportedProperties.Contains(Property);
}

bool UE::LevelSnapshots::Private::FLevelSnapshotsModule::IsPropertyExplicitlyUnsupported(const FProperty* Property) const
{
	return UnsupportedProperties.Contains(Property);
}

UE::LevelSnapshots::Private::FPropertyComparerArray UE::LevelSnapshots::Private::FLevelSnapshotsModule::GetPropertyComparerForClass(UClass* Class) const
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

UE::LevelSnapshots::IPropertyComparer::EPropertyComparison UE::LevelSnapshots::Private::FLevelSnapshotsModule::ShouldConsiderPropertyEqual(const FPropertyComparerArray& Comparers, const FPropertyComparisonParams& Params) const
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

TSharedPtr<UE::LevelSnapshots::ICustomObjectSnapshotSerializer> UE::LevelSnapshots::Private::FLevelSnapshotsModule::GetCustomSerializerForClass(UClass* Class) const
{
    SCOPED_SNAPSHOT_CORE_TRACE(GetCustomSerializerForClass);
    
    const bool bWasInBlueprint = Class->IsInBlueprint();
	while (Class)
    {
        if (const FCustomSerializer* Result = CustomSerializers.Find(Class); Result && (!bWasInBlueprint || Result->bIncludeBlueprintChildren))
        {
            return  Result->Serializer;
        }
		
        Class = Class->GetSuperClass();
    }

	return nullptr;
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::AddCanTakeSnapshotDelegate(FName DelegateName, FCanTakeSnapshot Delegate)
{
	CanTakeSnapshotDelegates.FindOrAdd(DelegateName) = Delegate;
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::RemoveCanTakeSnapshotDelegate(FName DelegateName)
{
	CanTakeSnapshotDelegates.Remove(DelegateName);
}


bool UE::LevelSnapshots::Private::FLevelSnapshotsModule::CanTakeSnapshot(const FPreTakeSnapshotEventData& Event) const
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

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPostLoadSnapshotObject(const FPostLoadSnapshotObjectParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(SnapshotLoaders);
	
	for (const TSharedRef<ISnapshotLoader>& Loader : SnapshotLoaders)
	{
		Loader->PostLoadSnapshotObject(Params);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPreApplySnapshotProperties(const FApplySnapshotPropertiesParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PreApplySnapshotProperties(Params);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPostApplySnapshotProperties(const FApplySnapshotPropertiesParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PostApplySnapshotProperties(Params);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPreApplySnapshotToActor(const FApplySnapshotToActorParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PreApplySnapshotToActor(Params);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPostApplySnapshotToActor(const FApplySnapshotToActorParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PostApplySnapshotToActor(Params);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPreCreateActor(UWorld* World, TSubclassOf<AActor> ActorClass, FActorSpawnParameters& InOutSpawnParams)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PreRecreateActor(World, ActorClass, InOutSpawnParams);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPostRecreateActor(AActor* Actor)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PostRecreateActor(Actor);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPreRemoveActor(AActor* Actor)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PreRemoveActor(Actor);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPreRecreateComponent(const FPreRecreateComponentParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PreRecreateComponent(Params);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPostRecreateComponent(UActorComponent* RecreatedComponent)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);
	
	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PostRecreateComponent(RecreatedComponent);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPreRemoveComponent(UActorComponent* ComponentToRemove)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);

	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PreRemoveComponent(ComponentToRemove);
	}
}

void UE::LevelSnapshots::Private::FLevelSnapshotsModule::OnPostRemoveComponent(const FPostRemoveComponentParams& Params)
{
	SCOPED_SNAPSHOT_CORE_TRACE(RestorationListeners);

	for (const TSharedRef<IRestorationListener>& Listener : RestorationListeners)
	{
		Listener->PostRemoveComponent(Params);
	}
}

IMPLEMENT_MODULE(UE::LevelSnapshots::Private::FLevelSnapshotsModule, LevelSnapshots)
