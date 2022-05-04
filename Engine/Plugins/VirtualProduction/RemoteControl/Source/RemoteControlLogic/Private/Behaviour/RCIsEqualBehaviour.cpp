// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/RCIsEqualBehaviour.h"

#include "RCVirtualProperty.h"
#include "Controller/RCController.h"

URCIsEqualBehaviour::URCIsEqualBehaviour()
{
	PropertySelfContainer = CreateDefaultSubobject<URCVirtualPropertySelfContainer>(FName("VirtualPropertySelfContainer"));
}

void URCIsEqualBehaviour::Initialize()
{
	URCController* RCController = ControllerWeakPtr.Get();
	if (!RCController)
	{
		return;
	}

	PropertySelfContainer->DuplicatePropertyWithCopy(RCController);
	
	Super::Initialize();
}
