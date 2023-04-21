// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"

#define LOCTEXT_NAMESPACE "AssetTagsGameplayEffectComponent"

void UAssetTagsGameplayEffectComponent::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITORONLY_DATA
	if (GetOwner())
	{
		EditorFriendlyName = FText::FormatOrdered(LOCTEXT("AssetTagsOnObj", "Asset Tags (on {0})"), GetOwner()->GetClass()->GetDisplayNameText()).ToString();
	}
	else
	{
		EditorFriendlyName = LOCTEXT("AssetTagsOnGE", "Asset Tags (on Gameplay Effect)").ToString();
	}
#endif // WITH_EDITORONLY_DATA
}

void UAssetTagsGameplayEffectComponent::OnOwnerPostLoad()
{
	Super::OnOwnerPostLoad();
	SetAndApplyAssetTagChanges(InheritableAssetTags);
}

#if WITH_EDITOR
void UAssetTagsGameplayEffectComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName()  == GetInheritableAssetTagsName())
	{
		SetAndApplyAssetTagChanges(InheritableAssetTags);
	}
}
#endif // WITH_EDITOR

void UAssetTagsGameplayEffectComponent::SetAndApplyAssetTagChanges(const FInheritedTagContainer& TagContainerMods)
{
	InheritableAssetTags = TagContainerMods;

	// Try to find the parent and update the inherited tags
	const UAssetTagsGameplayEffectComponent* Parent = FindParentComponent(*this);
	InheritableAssetTags.UpdateInheritedTagProperties(Parent ? &Parent->InheritableAssetTags : nullptr);

	// Apply to the owning Gameplay Effect Component
	UGameplayEffect* Owner = GetOwner();
	InheritableAssetTags.ApplyTo(Owner->CachedAssetTags);
}

#undef LOCTEXT_NAMESPACE