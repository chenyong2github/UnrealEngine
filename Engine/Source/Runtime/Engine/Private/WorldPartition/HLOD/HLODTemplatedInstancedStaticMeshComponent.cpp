// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODTemplatedInstancedStaticMeshComponent.h"

#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/ObjectSaveOverride.h"

UHLODTemplatedInstancedStaticMeshComponent::UHLODTemplatedInstancedStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UHLODTemplatedInstancedStaticMeshComponent::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
#if WITH_EDITOR
	// Ensure transient overrides are applied before the base implementation of PreSave
	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		auto GetTransientPropertyOverride = [](FName PropertyName) -> FPropertySaveOverride
		{
			FProperty* OverrideProperty = FindFProperty<FProperty>(UStaticMeshComponent::StaticClass(), PropertyName);
			check(OverrideProperty);

			FPropertySaveOverride PropOverride;
			PropOverride.PropertyPath = FFieldPath(OverrideProperty);
			PropOverride.bMarkTransient = true;
			return PropOverride;
		};

		FObjectSaveOverride ObjectSaveOverride;
		ObjectSaveOverride.PropOverrides.Add(GetTransientPropertyOverride(GetMemberNameChecked_StaticMesh()));
		ObjectSaveOverride.PropOverrides.Add(GetTransientPropertyOverride(GET_MEMBER_NAME_CHECKED(UMeshComponent, OverrideMaterials)));
		ObjectSaveOverride.PropOverrides.Add(GetTransientPropertyOverride(GET_MEMBER_NAME_CHECKED(UMeshComponent, OverlayMaterial)));

		ObjectSaveContext.AddSaveOverride(this, ObjectSaveOverride);
	}
#endif

	Super::PreSave(ObjectSaveContext);
}

void UHLODTemplatedInstancedStaticMeshComponent::PostLoad()
{
	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
	{
		check(GetStaticMesh() == nullptr);
		check(!OverrideMaterials.ContainsByPredicate([](const UMaterialInterface* MaterialInterface) { return MaterialInterface != nullptr; }));
		check(OverlayMaterial == nullptr);

		RestoreAssetsFromActorTemplate();
	}

	Super::PostLoad();
}

void UHLODTemplatedInstancedStaticMeshComponent::SetTemplateActorClass(const TSubclassOf<AActor>& InTemplateActorClass)
{
	TemplateActorClass = InTemplateActorClass;
}

void UHLODTemplatedInstancedStaticMeshComponent::SetTemplateComponentName(const FName& InTemplatePropertyName)
{
	TemplateComponentName = InTemplatePropertyName;
}

void UHLODTemplatedInstancedStaticMeshComponent::RestoreAssetsFromActorTemplate()
{
	const UStaticMeshComponent* TemplateSMC = UBlueprint::GetActorClassDefaultComponentByName<UStaticMeshComponent>(TemplateActorClass, TemplateComponentName);
	if (TemplateSMC)
	{
		// StaticMesh
		if (UStaticMesh* TemplateStaticMesh = TemplateSMC->GetStaticMesh())
		{
			SetStaticMesh(TemplateStaticMesh);
			SetForcedLodModel(TemplateStaticMesh->GetNumLODs());
		}

		// OverrideMaterials
		OverrideMaterials = TemplateSMC->OverrideMaterials;

		// OverlayMaterial
		OverlayMaterial = TemplateSMC->OverlayMaterial;
	}
}
