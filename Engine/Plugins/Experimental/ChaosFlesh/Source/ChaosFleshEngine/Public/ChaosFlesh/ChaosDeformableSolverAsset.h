// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

#include "ChaosDeformableSolverAsset.generated.h"

class UChaosDeformableSolver;

/**
* UChaosDeformableSolver (UObject)
*
*/
UCLASS(customconstructor)
class CHAOSFLESHENGINE_API UChaosDeformableSolver : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UChaosDeformableSolver(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	bool IsVisible() { return true; }
};



