// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNotifyState_MotionWarping.h"
#include "GameFramework/Actor.h"
#include "MotionWarpingComponent.h"

UAnimNotifyState_MotionWarping::UAnimNotifyState_MotionWarping(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FRootMotionModifierHandle UAnimNotifyState_MotionWarping::OnBecomeRelevant(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	FRootMotionModifierHandle Handle = AddRootMotionModifier(MotionWarpingComp, Animation, StartTime, EndTime);

	//@TODO: Temp, to handle the case where the blueprint notify does not return the handle. Will be removed shortly
	if (!Handle.IsValid())
	{
		if (MotionWarpingComp->GetModifiers().Num())
		{
			URootMotionModifier* Last = MotionWarpingComp->GetModifiers().Last();
			if (Last && Last->Animation.Get() == Animation && Last->StartTime == StartTime && Last->EndTime == EndTime)
			{
				Handle = Last->GetHandle();
			}
		}
	}

	if (URootMotionModifier* RootMotionModifierNew = MotionWarpingComp->GetModifierByHandle(Handle))
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

		return Handle;
	}

	return Handle;
}

FRootMotionModifierHandle UAnimNotifyState_MotionWarping::AddRootMotionModifier_Implementation(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	if (MotionWarpingComp)
	{
		if (RootMotionModifier)
		{
			URootMotionModifier* NewRootMotionModifier = MotionWarpingComp->AddModifierFromTemplate(RootMotionModifier, Animation, StartTime, EndTime);
			if (NewRootMotionModifier)
			{
				return NewRootMotionModifier->GetHandle();
			}
		}
	}

	return FRootMotionModifierHandle::InvalidHandle;
}

void UAnimNotifyState_MotionWarping::OnRootMotionModifierActivate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle)
{
	// Notify blueprint
	OnWarpBegin(MotionWarpingComp, Handle);
}

void UAnimNotifyState_MotionWarping::OnRootMotionModifierUpdate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle)
{
	// Notify blueprint
	OnWarpUpdate(MotionWarpingComp, Handle);
}

void UAnimNotifyState_MotionWarping::OnRootMotionModifierDeactivate(UMotionWarpingComponent* MotionWarpingComp, const FRootMotionModifierHandle& Handle)
{
	// Notify blueprint
	OnWarpEnd(MotionWarpingComp, Handle);
}