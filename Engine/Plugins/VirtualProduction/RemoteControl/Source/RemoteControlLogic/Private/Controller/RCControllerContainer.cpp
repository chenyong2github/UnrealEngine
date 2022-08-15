// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controller/RCControllerContainer.h"
#include "Controller/RCController.h"

#if WITH_EDITOR
void URCControllerContainer::OnModifyPropertyValue(const FPropertyChangedEvent& Event)
{
	if (const FProperty* FinalProperty = (Event.Property == Event.MemberProperty) ? Event.Property : Event.MemberProperty)
	{
		for (URCVirtualPropertyBase* VirtualProperty : VirtualProperties)
		{
			if (VirtualProperty && VirtualProperty->PropertyName == FinalProperty->GetFName())
			{
				if (URCController* Controller = Cast<URCController>(VirtualProperty))
				{
					Controller->OnModifyPropertyValue();
				}
			}
		}
	}

	Super::OnModifyPropertyValue(Event);
}
#endif
