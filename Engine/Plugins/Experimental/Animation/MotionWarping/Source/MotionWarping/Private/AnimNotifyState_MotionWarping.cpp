// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNotifyState_MotionWarping.h"
#include "GameFramework/Actor.h"
#include "MotionWarpingComponent.h"

UAnimNotifyState_MotionWarping::UAnimNotifyState_MotionWarping(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimNotifyState_MotionWarping::OnBecomeRelevant(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	URootMotionModifier* RootMotionModifierNew = AddRootMotionModifier(MotionWarpingComp, Animation, StartTime, EndTime);

	if (RootMotionModifierNew)
	{
		if (!RootMotionModifierNew->OnActivateDelegate.IsBound())
		{
			RootMotionModifierNew->OnActivateDelegate.BindDynamic(this, &UAnimNotifyState_MotionWarping::OnRootMotionModifierActivate);
		}

		if (!RootMotionModifierNew->OnUpdateDelegate.IsBound())
		{
			RootMotionModifierNew->OnUpdateDelegate.BindDynamic(this, &UAnimNotifyState_MotionWarping::OnRootMotionModifierUpdate);
		}

		if (!RootMotionModifierNew->OnDeactivateDelegate.IsBound())
		{
			RootMotionModifierNew->OnDeactivateDelegate.BindDynamic(this, &UAnimNotifyState_MotionWarping::OnRootMotionModifierDeactivate);
		}
	}
}

URootMotionModifier* UAnimNotifyState_MotionWarping::AddRootMotionModifier_Implementation(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	if (MotionWarpingComp && RootMotionModifier)
	{
		return MotionWarpingComp->AddModifierFromTemplate(RootMotionModifier, Animation, StartTime, EndTime);
	}

	return nullptr;
}

void UAnimNotifyState_MotionWarping::OnRootMotionModifierActivate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier)
{
	// Notify blueprint
	OnWarpBegin(MotionWarpingComp, Modifier);
}

void UAnimNotifyState_MotionWarping::OnRootMotionModifierUpdate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier)
{
	// Notify blueprint
	OnWarpUpdate(MotionWarpingComp, Modifier);
}

void UAnimNotifyState_MotionWarping::OnRootMotionModifierDeactivate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier)
{
	// Notify blueprint
	OnWarpEnd(MotionWarpingComp, Modifier);
}