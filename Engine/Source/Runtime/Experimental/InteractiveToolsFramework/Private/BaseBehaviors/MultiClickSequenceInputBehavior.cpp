// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"



UMultiClickSequenceInputBehavior::UMultiClickSequenceInputBehavior()
{
	bInActiveSequence = false;
}


void UMultiClickSequenceInputBehavior::Initialize(IClickSequenceBehaviorTarget* TargetIn)
{
	this->Target = TargetIn;
	bInActiveSequence = false;
}


FInputCaptureRequest UMultiClickSequenceInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	check(bInActiveSequence == false);   // should not happen...
	bInActiveSequence = false;

	if ( IsPressed(input) && (ModifierCheckFunc == nullptr || ModifierCheckFunc(input) ) )
	{
		if ( Target->CanBeginClickSequence(GetDeviceRay(input)) )
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any);
		}
	}
	return FInputCaptureRequest::Ignore();
}


FInputCaptureUpdate UMultiClickSequenceInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	Modifiers.UpdateModifiers(input, Target);

	Target->OnBeginClickSequence(GetDeviceRay(input));
	bInActiveSequence = true;
	
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}


FInputCaptureUpdate UMultiClickSequenceInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	check(bInActiveSequence == true);   // should always be the case!

	Modifiers.UpdateModifiers(input, Target);

	// allow target to abort click sequence
	if (Target->RequestAbortClickSequence())
	{
		Target->OnTerminateClickSequence();
		bInActiveSequence = false;
		return FInputCaptureUpdate::End();
	}

	if (IsReleased(input)) 
	{
		bool bContinue = Target->OnNextSequenceClick(GetDeviceRay(input));
		if (bContinue == false)
		{
			bInActiveSequence = false;
			return FInputCaptureUpdate::End();
		}
	}
	else
	{
		Target->OnNextSequencePreview(GetDeviceRay(input));
	}

	return FInputCaptureUpdate::Continue();
}


void UMultiClickSequenceInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	Target->OnTerminateClickSequence();
	bInActiveSequence = false;
}


bool UMultiClickSequenceInputBehavior::WantsHoverEvents()
{
	return true;
}

void UMultiClickSequenceInputBehavior::UpdateHover(const FInputDeviceState& Input)
{
	if (Target != nullptr)
	{
		Modifiers.UpdateModifiers(Input, Target);
		Target->OnBeginSequencePreview(FInputDeviceRay(Input.Mouse.WorldRay, Input.Mouse.Position2D));
	}
}

void UMultiClickSequenceInputBehavior::EndHover(const FInputDeviceState& input)
{
}