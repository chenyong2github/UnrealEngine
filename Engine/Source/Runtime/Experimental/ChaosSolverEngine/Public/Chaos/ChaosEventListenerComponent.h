// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


// #include "Chaos/ChaosSolver.h"
// #include "CoreMinimal.h"
// #include "GameFramework/Actor.h"
// #include "Physics/Experimental/PhysScene_Chaos.h"
// #include "UObject/ObjectMacros.h"

#include "Components/ActorComponent.h"
#include "Chaos/Declares.h"
#include "ChaosEventListenerComponent.generated.h"

class FPhysScene_Chaos;
class AChaosSolverActor;

/** 
 * Base class for listeners that query and respond to a frame's physics data (collision events, break events, etc).
 */

UCLASS(BlueprintType, ClassGroup = Chaos, meta = (BlueprintSpawnableComponent))
class CHAOSSOLVERENGINE_API UChaosEventListenerComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UChaosEventListenerComponent();

	//~ Begin UActorComponent interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//~ End UActorComponent interface


protected:

#if INCLUDE_CHAOS
	const Chaos::FPhysicsSolver* GetSolver() const;
	const TSharedPtr<FPhysScene_Chaos> GetPhysicsScene() const;
#endif

	const AChaosSolverActor* GetSolverActor() const;

	/** Used to know when the physics thread has updated the collision info for processing on the game thread */
	float LastCollisionTickTime;
};

