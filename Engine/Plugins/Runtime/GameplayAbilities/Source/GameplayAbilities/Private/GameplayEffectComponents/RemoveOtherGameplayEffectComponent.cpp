// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayEffectComponents/RemoveOtherGameplayEffectComponent.h"
#include "AbilitySystemComponent.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "RemoveOtherGameplayEffectComponent"

URemoveOtherGameplayEffectComponent::URemoveOtherGameplayEffectComponent()
{
#if WITH_EDITORONLY_DATA
	EditorFriendlyName = TEXT("Remove Other Gameplay Effects");
#endif
}

bool URemoveOtherGameplayEffectComponent::OnActiveGameplayEffectAdded(FActiveGameplayEffectsContainer& ActiveGEContainer, FActiveGameplayEffect& ActiveGE) const
{
	if (ActiveGEContainer.OwnerIsNetAuthority)
	{
		OnGameplayEffectApplied(ActiveGEContainer, ActiveGE.Handle);
	}
	return true;
}

void URemoveOtherGameplayEffectComponent::OnGameplayEffectExecuted(FActiveGameplayEffectsContainer& ActiveGEContainer, FGameplayEffectSpec& GESpec, FPredictionKey& PredictionKey) const
{
	const bool bInstantEffect = (GetOwner()->DurationPolicy == EGameplayEffectDurationType::Instant);
	if (bInstantEffect && ActiveGEContainer.OwnerIsNetAuthority)
	{
		FActiveGameplayEffectHandle InvalidHandle;
		OnGameplayEffectApplied(ActiveGEContainer, InvalidHandle);
	}
}

void URemoveOtherGameplayEffectComponent::OnGameplayEffectApplied(FActiveGameplayEffectsContainer& ActiveGEContainer, const FActiveGameplayEffectHandle& ActiveGEHandle) const
{
	constexpr int32 RemoveAllStacks = -1;
	for (const FGameplayEffectQuery& RemoveQuery : RemoveGameplayEffectQueries)
	{
		if (!RemoveQuery.IsEmpty())
		{
			// If we have an ActiveGEHandle, make sure we never remove ourselves.
			// If we don't, there's no need to make a copy.
			if (!ActiveGEHandle.IsValid())
			{
				// Faster path: No copy needed
				ActiveGEContainer.RemoveActiveEffects(RemoveQuery, RemoveAllStacks);
			}
			else
			{
				FGameplayEffectQuery MutableRemoveQuery = RemoveQuery;
				MutableRemoveQuery.IgnoreHandles.Add(ActiveGEHandle);

				ActiveGEContainer.RemoveActiveEffects(MutableRemoveQuery, RemoveAllStacks);
			}
		}
	}
}

#if WITH_EDITOR
EDataValidationResult URemoveOtherGameplayEffectComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (GetOwner()->DurationPolicy != EGameplayEffectDurationType::Instant)
	{
		if (GetOwner()->Period.Value > 0.0f)
		{
			Context.AddError(FText::FormatOrdered(LOCTEXT("PeriodicEffectError", "GE is Periodic. Remove {0} and use TagRequirements (Ongoing) instead."), FText::FromString(GetClass()->GetName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
