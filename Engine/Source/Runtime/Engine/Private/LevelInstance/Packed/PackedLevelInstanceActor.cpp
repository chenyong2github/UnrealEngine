// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/Packed/PackedLevelInstanceActor.h"
#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"

APackedLevelInstance::APackedLevelInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	ISMComponentClass = UInstancedStaticMeshComponent::StaticClass();
	bEditing = false;
#endif
}

bool APackedLevelInstance::SupportsLoading() const
{
#if WITH_EDITOR
	return ChildEditing > 0 || IsLoaded();
#else
	return false;
#endif
}

#if WITH_EDITOR
FName APackedLevelInstance::GetPackedComponentTag()
{
	static FName PackedComponentTag("PackedComponent");
	return PackedComponentTag;
}

void APackedLevelInstance::OnWorldAssetChanged()
{
	if (IsLevelInstancePathValid())
	{
		TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
		Builder->PackActor(this, GetWorldAsset());
	}
	else
	{
		DestroyPackedComponents();
	}
}

void APackedLevelInstance::OnWorldAssetSaved()
{
	TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
	
	if (UBlueprint* GeneratedBy = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
	{
		check(GeneratedBy == BlueprintAsset.Get());
		Builder->UpdateBlueprint(GeneratedBy);
	}
	else
	{
		Builder->PackActor(this);
	}
}

void APackedLevelInstance::OnEditChild()
{
	check(GetLevelInstanceSubsystem()->GetLevelInstanceLevel(this) != nullptr);
	ChildEditing++;
	MarkComponentsRenderStateDirty();
}

void APackedLevelInstance::OnCommitChild()
{
	check(GetLevelInstanceSubsystem()->GetLevelInstanceLevel(this) != nullptr);
	check(ChildEditing > 0);
	ChildEditing--;
	if (!ChildEditing)
	{
		UnloadLevelInstance();

		// Reflect child changes
		TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
		
		if (UBlueprint* GeneratedBy = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
		{
			check(GeneratedBy == BlueprintAsset.Get());
			Builder->UpdateBlueprint(GeneratedBy);
		}
		else
		{
			Builder->PackActor(this, GetWorldAsset());
		}

		MarkComponentsRenderStateDirty();
	}
}

void APackedLevelInstance::OnEdit()
{
	check(!bEditing);
	
	bEditing = true;
	MarkComponentsRenderStateDirty();
}

void APackedLevelInstance::OnCommit()
{
	check(bEditing);
	bEditing = false;
	MarkComponentsRenderStateDirty();
}

bool APackedLevelInstance::IsHiddenEd() const
{
	return Super::IsHiddenEd() || bEditing || (ChildEditing > 0);
}

bool APackedLevelInstance::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	// Disallow editing of the World if we are a BP instance
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(APackedLevelInstance, WorldAsset))
	{
		return GetClass()->IsNative();
	}

	return true;
}

void APackedLevelInstance::GetPackedComponents(TArray<UActorComponent*>& OutPackedComponents) const
{
	const TSet<UActorComponent*>& Components = GetComponents();
	OutPackedComponents.Reserve(Components.Num());
		
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->ComponentHasTag(GetPackedComponentTag()))
		{
			OutPackedComponents.Add(Component);
		}
	}
}

void APackedLevelInstance::DestroyPackedComponents()
{
	Modify();
	TArray<UActorComponent*> PackedComponents;
	GetPackedComponents(PackedComponents);
	for (UActorComponent* PackedComponent : PackedComponents)
	{
		PackedComponent->Modify();
		PackedComponent->DestroyComponent();
	}
}
#endif