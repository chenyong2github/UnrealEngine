// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/SingleClickBehavior.h"



USingleClickToolBehavior::USingleClickToolBehavior()
{
	HitTestOnRelease = true;
}


void USingleClickToolBehavior::Initialize(IClickBehaviorTarget* TargetIn)
{
	this->Target = TargetIn;
}


FInputCaptureRequest USingleClickToolBehavior::WantsCapture(const FInputDeviceState& input)
{
	if (IsPressed(input))
	{
		if ( Target->IsHitByClick(GetDeviceRay(input)) )
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any);
		}
	}
	return FInputCaptureRequest::Ignore();
}


FInputCaptureUpdate USingleClickToolBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}


FInputCaptureUpdate USingleClickToolBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
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


void USingleClickToolBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	// nothing to do
}


void USingleClickToolBehavior::Clicked(const FInputDeviceState& input, const FInputCaptureData& data)
{
	Target->OnClicked(GetDeviceRay(input));
}

