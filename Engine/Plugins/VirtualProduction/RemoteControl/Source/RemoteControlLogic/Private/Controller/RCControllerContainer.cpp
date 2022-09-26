// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controller/RCControllerContainer.h"
#include "Controller/RCController.h"

#if WITH_EDITOR

URCController* URCControllerContainer::GetControllerFromChangeEvent(const FPropertyChangedEvent& Event)
{
	if (const FProperty* FinalProperty = (Event.Property == Event.MemberProperty) ? Event.Property : Event.MemberProperty)
	{
		const FName PropertyName = FinalProperty->GetFName();

		URCVirtualPropertyBase* VirtualProperty = GetVirtualProperty(PropertyName);

		return Cast<URCController>(VirtualProperty);
	}

	return nullptr;
}

void URCControllerContainer::OnPreChangePropertyValue(const FPropertyChangedEvent& Event)
{
	if (URCController* Controller = GetControllerFromChangeEvent(Event))
	{
		Controller->OnPreChangePropertyValue();
	}

	Super::OnPreChangePropertyValue(Event);
}

void URCControllerContainer::OnModifyPropertyValue(const FPropertyChangedEvent& Event)
{
	if (URCController* Controller = GetControllerFromChangeEvent(Event))
	{
		Controller->OnModifyPropertyValue();
	}

	Super::OnModifyPropertyValue(Event);
}
#endif
