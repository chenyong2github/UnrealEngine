// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeParameter.h"
#include "StateTreeVariableDesc.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

#if WITH_EDITOR
bool FStateTreeParameter::IsCompatible(const FStateTreeVariableDesc& Desc) const
{
	return Name == Desc.Name && ID == Desc.ID && Variable.Type == Desc.Type && Variable.BaseClass == Desc.BaseClass;
}
#endif