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
	if(!Handle.IsValid())
	{
		if (MotionWarpingComp->GetRootMotionModifiers().Num())
		{
			TSharedPtr<FRootMotionModifier> Last = MotionWarpingComp->GetRootMotionModifiers().Last();
			if (Last.IsValid() && Last->Animation.Get() == Animation && Last->StartTime == StartTime && Last->EndTime == EndTime)
			{
				Handle = Last->GetHandle();
			}
		}
	}

	TSharedPtr<FRootMotionModifier> Modifier = MotionWarpingComp->GetRootMotionModifierByHandle(Handle);
	if (Modifier.IsValid())
	{
		if(!Modifier->OnActivateDelegate.IsBound())
		{
			Modifier->OnActivateDelegate.BindDynamic(this, &UAnimNotifyState_MotionWarping::OnRootMotionModifierActivate);
		}

		if (!Modifier->OnUpdateDelegate.IsBound())
		{
			Modifier->OnUpdateDelegate.BindDynamic(this, &UAnimNotifyState_MotionWarping::OnRootMotionModifierUpdate);
		}

		if (!Modifier->OnDeactivateDelegate.IsBound())
		{
			Modifier->OnDeactivateDelegate.BindDynamic(this, &UAnimNotifyState_MotionWarping::OnRootMotionModifierDeactivate);
		}
	}

	return Handle;
}

FRootMotionModifierHandle UAnimNotifyState_MotionWarping::AddRootMotionModifier_Implementation(UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation, float StartTime, float EndTime) const
{
	if (MotionWarpingComp && RootMotionModifierConfig)
	{
		return RootMotionModifierConfig->AddRootMotionModifierNew(MotionWarpingComp, Animation, StartTime, EndTime);
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