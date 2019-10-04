// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

/** This class represents an GeometryCollection Actor. */

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionDebugDrawComponent.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsActorHandle.h"
#include "UObject/ObjectMacros.h"

#include "GeometryCollectionActor.generated.h"



UCLASS()
class GEOMETRYCOLLECTIONENGINE_API AGeometryCollectionActor: public AActor
{
	GENERATED_UCLASS_BODY()

public:

	/* Game state callback */
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif

	/* GeometryCollectionComponent */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Destruction, meta = (ExposeFunctionCategories = "Components|GeometryCollection", AllowPrivateAccess = "true"))
	UGeometryCollectionComponent* GeometryCollectionComponent;
	UGeometryCollectionComponent* GetGeometryCollectionComponent() const { return GeometryCollectionComponent; }

	UPROPERTY(VisibleAnywhere, Category = Destruction, meta = (ExposeFunctionCategories = "Components|GeometryCollection", AllowPrivateAccess = "true"))
	UGeometryCollectionDebugDrawComponent* GeometryCollectionDebugDrawComponent;
	UGeometryCollectionDebugDrawComponent* GetGeometryCollectionDebugDrawComponent() const { return GeometryCollectionDebugDrawComponent; }

	UFUNCTION(BlueprintCallable, Category = "Physics")
	bool RaycastSingle(FVector Start, FVector End, FHitResult& OutHit) const;

};
