// Copyright Epic Games, Inc. All Rights Reserved.

#include "EngineTypesRestorationFence.h"

#include "ActorGroupRestoration.h"
#include "Restorability/CollisionRestoration.h"
#include "Restorability/GridPlacementRestoration.h"

#include "EngineUtils.h"
#include "Algo/Transform.h"
#include "Editor/GroupActor.h"
#include "Engine/Brush.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstance.h"


namespace UE::LevelSnapshots::Private::EngineTypesRestorationFence
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
	
	void RegisterSpecialEngineTypeSupport(FLevelSnapshotsModule& Module)
	{
		// Enable / disable troublesome properties
		AddSoftObjectPathSupport(Module);
		AddAttachParentSupport(Module);
		DisableIrrelevantBrushSubobjects(Module);
		DisableIrrelevantWorldSettings(Module);
		DisableIrrelevantMaterialInstanceProperties(Module);
		DisableIrrelevantActorProperties(Module);

		// Interact with special engine features
		FCollisionRestoration::Register(Module);
		GridPlacementRestoration::Register(Module);
		ActorGroupRestoration::Register(Module);
	}
}
