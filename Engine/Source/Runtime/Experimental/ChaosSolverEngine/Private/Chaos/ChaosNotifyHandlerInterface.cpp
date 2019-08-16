// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosNotifyHandlerInterface.h"
#include "Components/PrimitiveComponent.h"

UChaosNotifyHandlerInterface::UChaosNotifyHandlerInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void IChaosNotifyHandlerInterface::HandlePhysicsCollision(const FChaosPhysicsCollisionInfo& CollisionInfo)
{
	// native
	NotifyPhysicsCollision(CollisionInfo);

	// bp
	DispatchChaosPhysicsCollisionBlueprintEvents(CollisionInfo);
}

FHitResult UChaosSolverEngineBlueprintLibrary::ConvertPhysicsCollisionToHitResult(const FChaosPhysicsCollisionInfo& PhysicsCollision)
{
	FHitResult Hit(0.f);
	
	Hit.Component = PhysicsCollision.OtherComponent;
	Hit.Actor = Hit.Component.IsValid() ? Hit.Component->GetOwner() : nullptr;
	Hit.bBlockingHit = true;
	Hit.Normal = PhysicsCollision.Normal;
	Hit.ImpactNormal = PhysicsCollision.Normal;
	Hit.Location = PhysicsCollision.Location;
	Hit.ImpactPoint = PhysicsCollision.Location;
	//Hit.PhysMaterial = 

	return Hit;
}
