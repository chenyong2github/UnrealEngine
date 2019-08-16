// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/MouseHoverBehavior.h"


UMouseHoverBehavior::UMouseHoverBehavior()
{
	Target = nullptr;
}

EInputDevices UMouseHoverBehavior::GetSupportedDevices()
{
	return EInputDevices::Mouse;
}

void UMouseHoverBehavior::Initialize(IHoverBehaviorTarget* TargetIn)
{
	this->Target = TargetIn;
}

bool UMouseHoverBehavior::WantsHoverEvents()
{
	return true;
}

void UMouseHoverBehavior::UpdateHover(const FInputDeviceState& input)
{
	if (Target != nullptr)
	{
		Modifiers.UpdateModifiers(input, Target);

		Target->OnUpdateHover( FInputDeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D) );
	}
}

void UMouseHoverBehavior::EndHover(const FInputDeviceState& input)
{
}

