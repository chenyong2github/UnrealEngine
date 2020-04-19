// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PerParticleEtherDrag.h"

namespace Chaos
{

float LinearEtherDragOverride = -1.f;
FAutoConsoleVariableRef CVarLinearEtherDragOverride(TEXT("p.LinearEtherDragOverride"), LinearEtherDragOverride, TEXT("Set an override linear ether drag value. -1.f to disable"));

float AngularEtherDragOverride = -1.f;
FAutoConsoleVariableRef CVarAngularEtherDragOverride(TEXT("p.AngularEtherDragOverride"), AngularEtherDragOverride, TEXT("Set an override angular ether drag value. -1.f to disable"));

}
