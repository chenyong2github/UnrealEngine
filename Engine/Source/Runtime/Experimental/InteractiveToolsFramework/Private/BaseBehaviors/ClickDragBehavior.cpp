// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/ClickDragBehavior.h"



UClickDragInputBehavior::UClickDragInputBehavior()
{
}


void UClickDragInputBehavior::Initialize(IClickDragBehaviorTarget* TargetIn)
{
	check(TargetIn != nullptr);
	this->Target = TargetIn;
}


FInputCaptureRequest UClickDragInputBehavior::WantsCapture(const FInputDeviceState& Input)
{
	if (IsPressed(Input) && (ModifierCheckFunc == nullptr || ModifierCheckFunc(Input)) )
	{
		FInputRayHit HitResult = Target->CanBeginClickDragSequence(GetDeviceRay(Input));
		if (HitResult.bHit)
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, HitResult.HitDepth);
		}
	}
	return FInputCaptureRequest::Ignore();
}


FInputCaptureUpdate UClickDragInputBehavior::BeginCapture(const FInputDeviceState& Input, EInputCaptureSide Side)
{
	Modifiers.UpdateModifiers(Input, Target);
	OnClickPress(Input, Side);
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}


FInputCaptureUpdate UClickDragInputBehavior::UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& Data)
{
	Modifiers.UpdateModifiers(Input, Target);

	if (IsReleased(Input)) 
	{
		OnClickRelease(Input, Data);
		return FInputCaptureUpdate::End();
	}
	else
	{
		OnClickDrag(Input, Data);
		return FInputCaptureUpdate::Continue();
	}
}


void UClickDragInputBehavior::ForceEndCapture(const FInputCaptureData& Data)
{
	Target->OnTerminateDragSequence();
}



void UClickDragInputBehavior::OnClickPress(const FInputDeviceState& Input, EInputCaptureSide Side)
{
	Target->OnClickPress(GetDeviceRay(Input));
}

void UClickDragInputBehavior::OnClickDrag(const FInputDeviceState& Input, const FInputCaptureData& Data)
{
	Target->OnClickDrag(GetDeviceRay(Input));
}

void UClickDragInputBehavior::OnClickRelease(const FInputDeviceState& Input, const FInputCaptureData& Data)
{
	Target->OnClickRelease(GetDeviceRay(Input));
}
