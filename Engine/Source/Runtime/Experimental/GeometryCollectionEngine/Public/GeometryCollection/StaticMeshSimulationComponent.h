// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "Chaos/ChaosNotifyHandlerInterface.h"

#include "StaticMeshSimulationComponent.generated.h"

class FStaticMeshPhysicsProxy;
class AChaosSolverActor;
class UChaosPhysicalMaterial;
class FPhysScene_Chaos;

namespace Chaos
{
class FChaosPhysicsMaterial;
}

/**
*	UStaticMeshSimulationComponent
*/
UCLASS(ClassGroup = Physics, Experimental, meta = (BlueprintSpawnableComponent))
class GEOMETRYCOLLECTIONENGINE_API UStaticMeshSimulationComponent : public UActorComponent, public IChaosNotifyHandlerInterface
{
	GENERATED_UCLASS_BODY()
	UStaticMeshSimulationComponent(FVTableHelper& Helper);
	virtual ~UStaticMeshSimulationComponent();
public:

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction);

	/** When Simulating is enabled the Component will initialize its rigid bodies within the solver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	bool Simulating;

	/** If true, this component will get collision notification events (@see IChaosNotifyHandlerInterface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	bool bNotifyCollisions;

	/** ObjectType defines how to initialize the rigid collision structures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	EObjectStateTypeEnum ObjectType;

	/** Mass in Kg */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General", meta = (Units=kg))
	float Mass;

	/** CollisionType defines how to initialize the rigid collision structures.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	ECollisionTypeEnum CollisionType;

	/** CollisionType defines how to initialize the rigid collision structures.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	EImplicitTypeEnum ImplicitType;

	/*
	*  Resolution on the smallest axes for the level set. (def: 5)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|Collisions")
	int32 MinLevelSetResolution;

	/*
	*  Resolution on the smallest axes for the level set. (def: 10)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|Collisions")
	int32 MaxLevelSetResolution;

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	EInitialVelocityTypeEnum InitialVelocityType;

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	FVector InitialLinearVelocity;

	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	FVector InitialAngularVelocity;

	/** Damage threshold for clusters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	float DamageThreshold;

	/**
	* Physical Properties
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics")
	TObjectPtr<const UChaosPhysicalMaterial> PhysicalMaterial;

	/** Chaos RBD Solver */
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics", meta = (DisplayName = "Chaos Solver"))
	TObjectPtr<AChaosSolverActor> ChaosSolverActor;

	const TSharedPtr<FPhysScene_Chaos> GetPhysicsScene() const;

public:
	UPROPERTY(BlueprintAssignable, Category = "Collision")
	FOnChaosPhysicsCollision OnChaosPhysicsCollision;
	
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Physics Collision"), Category = "Collision")
	void ReceivePhysicsCollision(const FChaosPhysicsCollisionInfo& CollisionInfo);

	// IChaosNotifyHandlerInterface
	virtual void DispatchChaosPhysicsCollisionBlueprintEvents(const FChaosPhysicsCollisionInfo& CollisionInfo) override;

	/** Changes whether or not this component will get future break notifications. */
	UFUNCTION(BlueprintCallable, Category = "Physics")
	void ForceRecreatePhysicsState();

private : 

	/** List of physics objects this simulation component created. */
	TArray<FStaticMeshPhysicsProxy*> PhysicsProxies;

	/** List of component for which this simulation component created a physics object. Parallel array to PhysicsProxy, so PhysicsProxies[i] corresponds to SimulatedComponents[i] */
	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> SimulatedComponents;


	//@todo(mlentine): Don't have one per static mesh
	TUniquePtr<Chaos::FChaosPhysicsMaterial> ChaosMaterial;
	
protected:

	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;

};