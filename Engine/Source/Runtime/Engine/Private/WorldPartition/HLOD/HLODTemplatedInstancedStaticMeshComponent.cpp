// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODTemplatedInstancedStaticMeshComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/StaticMesh.h"
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

// Note: this is greatly inspired by a similar function in AIHelpers.h/.cpp
template <class TComp>
static const TComp* GetActorClassDefaultComponentByName(const TSubclassOf<AActor>& ActorClass, FName InComponentName)
{
	auto IsMatchingComponent = [InComponentName](const UActorComponent* InComponent, bool bStripGenSuffix = false)
	{
		if (const TComp* TypedComponent = Cast<TComp>(InComponent))
		{
			if (bStripGenSuffix)
			{
				FString StrippedName = InComponent->GetName();
				StrippedName.RemoveFromEnd(TEXT("_GEN_VARIABLE"));

				if (StrippedName == InComponentName.ToString())
				{
					return true;
				}
			}
			else if (TypedComponent->GetFName() == InComponentName)
			{
				return true;
			}
		}

		return false;
	};

	if (!ActorClass.Get())
	{
		return nullptr;
	}

	// Test the components defined on the native class.
	const AActor* CDO = ActorClass->GetDefaultObject<AActor>();
	check(CDO);

	for (const UActorComponent* Component : CDO->GetComponents())
	{
		if (IsMatchingComponent(Component))
		{
			return CastChecked<TComp>(Component);
		}
	}

	// Try to get the components off the BP class.
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(*ActorClass);
	if (BPClass)
	{
		// A BlueprintGeneratedClass has a USimpleConstructionScript member. This member has an array of RootNodes
		// which contains the SCSNode for the root SceneComponent and non-SceneComponents. For the SceneComponent
		// hierarchy, each SCSNode knows its children SCSNodes. Each SCSNode stores the component template that will
		// be created when the Actor is spawned.
		//
		// WARNING: This may change in future engine versions!

		// Check added components.
		USimpleConstructionScript* ConstructionScript = BPClass->SimpleConstructionScript;
		if (ConstructionScript)
		{
			for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
			{
				if (IsMatchingComponent(Node->ComponentTemplate, true))
				{
					return CastChecked<TComp>(Node->ComponentTemplate);
				}
			}
		}
		// Check modified inherited components.
		UInheritableComponentHandler* InheritableComponentHandler = BPClass->InheritableComponentHandler;
		if (InheritableComponentHandler)
		{
			for (TArray<FComponentOverrideRecord>::TIterator It = InheritableComponentHandler->CreateRecordIterator(); It; ++It)
			{
				if (IsMatchingComponent(It->ComponentTemplate, true))
				{
					return CastChecked<TComp>(It->ComponentTemplate);
				}
			}
		}
	}

	return nullptr;
}

void UHLODTemplatedInstancedStaticMeshComponent::RestoreAssetsFromActorTemplate()
{
	const UStaticMeshComponent* TemplateSMC = GetActorClassDefaultComponentByName<UStaticMeshComponent>(TemplateActorClass, TemplateComponentName);
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
