// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimSubsystem_Base.h"

void FAnimSubsystem_Base::OnPostLoadDefaults(FAnimSubsystemPostLoadDefaultsContext& InContext)
{
	FExposedValueHandler::ClassInitialization(ExposedValueHandlers, InContext.DefaultAnimInstance->GetClass());
}
