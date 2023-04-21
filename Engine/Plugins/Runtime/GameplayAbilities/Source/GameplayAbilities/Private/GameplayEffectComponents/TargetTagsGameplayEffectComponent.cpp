// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UTargetTagsGameplayEffectComponent::UTargetTagsGameplayEffectComponent()
{
#if WITH_EDITORONLY_DATA
	EditorFriendlyName = TEXT("Target Tags (Granted to Actor)");
#endif // WITH_EDITORONLY_DATA
}

void UTargetTagsGameplayEffectComponent::OnOwnerPostLoad()
{
	Super::OnOwnerPostLoad();
	SetAndApplyTargetTagChanges(InheritableGrantedTagsContainer);
}

#if WITH_EDITOR
void UTargetTagsGameplayEffectComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GetInheritableGrantedTagsContainerName())
	{
		SetAndApplyTargetTagChanges(InheritableGrantedTagsContainer);
	}
}
#endif // WITH_EDITOR

void UTargetTagsGameplayEffectComponent::SetAndApplyTargetTagChanges(const FInheritedTagContainer& TagContainerMods)
{
	InheritableGrantedTagsContainer = TagContainerMods;

	// Try to find the parent and update the inherited tags
	const UTargetTagsGameplayEffectComponent* Parent = FindParentComponent(*this);
	InheritableGrantedTagsContainer.UpdateInheritedTagProperties(Parent ? &Parent->InheritableGrantedTagsContainer : nullptr);

	// Apply to the owning Gameplay Effect Component
	UGameplayEffect* Owner = GetOwner();
	InheritableGrantedTagsContainer.ApplyTo(Owner->CachedGrantedTags);
}
