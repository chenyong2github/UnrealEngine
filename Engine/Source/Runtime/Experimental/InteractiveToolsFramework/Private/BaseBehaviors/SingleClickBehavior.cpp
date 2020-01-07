// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/SingleClickBehavior.h"



USingleClickInputBehavior::USingleClickInputBehavior()
{
	HitTestOnRelease = true;
}


void USingleClickInputBehavior::Initialize(IClickBehaviorTarget* TargetIn)
{
	this->Target = TargetIn;
}


FInputCaptureRequest USingleClickInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	if (IsPressed(input) && (ModifierCheckFunc == nullptr || ModifierCheckFunc(input)) )
	{
		if ( Target->IsHitByClick(GetDeviceRay(input)) )
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any);
		}
	}
	return FInputCaptureRequest::Ignore();
}


FInputCaptureUpdate USingleClickInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}


FInputCaptureUpdate USingleClickInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	if (IsReleased(input)) 
	{
		if (HitTestOnRelease == false || 
			Target->IsHitByClick(GetDeviceRay(input)) )
		{
			Clicked(input, data);
		}

		return FInputCaptureUpdate::End();
	}

	return FInputCaptureUpdate::Continue();
}


void USingleClickInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	// nothing to do
}


void USingleClickInputBehavior::Clicked(const FInputDeviceState& input, const FInputCaptureData& data)
{
	Target->OnClicked(GetDeviceRay(input));
}

