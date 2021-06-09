// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/Packed/PackedLevelInstanceActor.h"
#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LevelInstance/LevelInstancePrivate.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

APackedLevelInstance::APackedLevelInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, ISMComponentClass(UInstancedStaticMeshComponent::StaticClass())
	, bEditing(false)
	, ChildEditing(0)
	, bChildChanged(false)
#endif
{
#if WITH_EDITORONLY_DATA
	// Packed Level Instances don't support level streaming or sub actors
	DesiredRuntimeBehavior = ELevelInstanceRuntimeBehavior::None;
#endif
}

void APackedLevelInstance::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	Super::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	// We want to make sure we serialize that property so we can compare to the CDO
	if (!Ar.IsFilterEditorOnly() && Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::PackedLevelInstanceVersion)
	{
		Ar << PackedVersion;
	}
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

void APackedLevelInstance::PostLoad()
{
	Super::PostLoad();

	// Non CDO: Set the Guid to something different then the default value so that we actually run the construction script on actors that haven't been resaved against their latest BP
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && GetLinkerCustomVersion(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::PackedLevelInstanceVersion)
	{
		const static FGuid NoVersionGUID(0x50817615, 0x74A547A3, 0x9295D655, 0x8A852C0F);
		PackedVersion = NoVersionGUID;
	}
}

void APackedLevelInstance::RerunConstructionScripts()
{
	bool bShouldRerunConstructionScript = true;

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		bShouldRerunConstructionScript = false;

		// Only rerun if version mismatchs
		if (PackedVersion != GetClass()->GetDefaultObject<APackedLevelInstance>()->PackedVersion)
		{
			bShouldRerunConstructionScript = true;
			UE_LOG(LogLevelInstance, Verbose, TEXT("RerunConstructionScript was executed on %s (%s) because its version (%s) doesn't match latest version (%s). Resaving this actor will fix this"),
				*GetPathName(),
				*GetPackage()->GetPathName(),
				*PackedVersion.ToString(),
				*GetClass()->GetDefaultObject<APackedLevelInstance>()->PackedVersion.ToString());
		}
	}
	
	if(bShouldRerunConstructionScript)
	{
		Super::RerunConstructionScripts();
		PackedVersion = GetClass()->GetDefaultObject<APackedLevelInstance>()->PackedVersion;
	}
}

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

void APackedLevelInstance::OnWorldAssetSaved(bool bPromptForSave)
{
	TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
	
	if (UBlueprint* GeneratedBy = Cast<UBlueprint>(GetClass()->ClassGeneratedBy))
	{
		check(GeneratedBy == BlueprintAsset.Get());
		const bool bCheckoutAndSave = true;
		Builder->UpdateBlueprint(GeneratedBy, bCheckoutAndSave, bPromptForSave);
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

void APackedLevelInstance::OnCommitChild(bool bChanged)
{
	check(GetLevelInstanceSubsystem()->GetLevelInstanceLevel(this) != nullptr);
	check(ChildEditing > 0);
	ChildEditing--;
	bChildChanged |= bChanged;
	if (!ChildEditing)
	{
		UnloadLevelInstance();

		if (bChildChanged)
		{
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
			bChildChanged = false;
		}

		MarkComponentsRenderStateDirty();
	}
}

void APackedLevelInstance::OnEdit()
{
	Super::OnEdit();
	check(!bEditing);
	
	bEditing = true;
	MarkComponentsRenderStateDirty();
}

void APackedLevelInstance::OnCommit()
{
	Super::OnCommit();
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

	// PackedLevelInstance doesn't support a runtime behavior
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(APackedLevelInstance, DesiredRuntimeBehavior))
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