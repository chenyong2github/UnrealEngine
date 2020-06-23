// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataSourceFiltering.h"
#include "CoreTypes.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, SourceFilteringCore);


bool operator== (const FActorClassFilter& LHS, const FActorClassFilter& RHS)
{
	return LHS.ActorClass == RHS.ActorClass;
}