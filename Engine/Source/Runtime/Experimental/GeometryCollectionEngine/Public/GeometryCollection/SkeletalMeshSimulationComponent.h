// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/MeshComponent.h"
#include "Chaos/ChaosNotifyHandlerInterface.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"

#include "SkeletalMeshSimulationComponent.generated.h"

class FSkeletalMeshSimulationComponentPhysicsProxy;

namespace Chaos
{
class FChaosPhysicsMaterial;
}

class AChaosSolverActor;
class UChaosPhysicalMaterial;
class FSkeletalMeshPhysicsProxy;

/**
*	USkeletalMeshSimulationComponent
*/
class UE_DEPRECATED(4.27, "USkeletalMeshSimulationComponent is deprecated. use regular USkeletalMeshComponent") USkeletalMeshSimulationComponent;
UCLASS(ClassGroup = Physics, Experimental, meta = (BlueprintSpawnableComponent))
class GEOMETRYCOLLECTIONENGINE_API USkeletalMeshSimulationComponent : public UActorComponent, public IChaosNotifyHandlerInterface
{
	GENERATED_UCLASS_BODY()
	USkeletalMeshSimulationComponent(FVTableHelper& Helper);
	virtual ~USkeletalMeshSimulationComponent();

public:
	//
	// ChaosPhysics
	//

	/** Physical Properties */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics")
	const UChaosPhysicalMaterial* PhysicalMaterial;

	/** Chaos RBD Solver */
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics", meta = (DisplayName = "Chaos Solver"))
	AChaosSolverActor* ChaosSolverActor;

	UPROPERTY(EditAnywhere, Category = "ChaosPhysics")
	UPhysicsAsset* OverridePhysicsAsset;

	//
	// ChaosPhysics | General
	//

	/** When Simulating is enabled the Component will initialize its rigid bodies within the solver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	bool bSimulating;

	/** If true, this component will get collision notification events (@see IChaosNotifyHandlerInterface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	bool bNotifyCollisions;

	/** ObjectType defines how to initialize the rigid collision structures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	EObjectStateTypeEnum ObjectType;

	/** Density / mass. 
	 *
	 * Common densities in g/cm^3:
	 *     gold: 19.3
	 *     lead: 11.3
	 *     copper: 8.3 - 9.0
	 *     steel: 8.03
	 *     iron: 7.8
	 *     aluminium: 2.7
	 *     glass: 2.4 - 2.8
	 *     brick: 1.4 - 2.4
	 *     concrete: 0.45 - 2.4
	 *     bone: 1.7 - 2.0
	 *     muscle: 1.06
	 *     water: 1.0
	 *     fat: 0.9196
	 *     gasoline: 0.7
	 *     wood: 0.67
	 *     tree bark: 0.24
	 *     air: 0.001293
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	float Density;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	float MinMass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
	float MaxMass;

	//
	// ChaosPhysics | Collisions
	//

	/** CollisionType defines how to initialize the rigid collision structures.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	ECollisionTypeEnum CollisionType;

	/** Number of particles to generate per unit area (square cm). 0.1 would generate 1 collision particle per 10 cm^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	float ImplicitShapeParticlesPerUnitArea;
	/** Minimum number of particles for each implicit shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	int ImplicitShapeMinNumParticles;
	/** Maximum number of particles for each implicit shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	int ImplicitShapeMaxNumParticles;

	/** Resolution on the smallest axes for the level set. (def: 5) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|Collisions")
	int32 MinLevelSetResolution;
	/** Resolution on the smallest axes for the level set. (def: 10) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|Collisions")
	int32 MaxLevelSetResolution;

	/** Collision group - 0 = collides with all, -1 = none */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
	int32 CollisionGroup;

	//
	// ChaosPhysics | Clustering
	//
#if 0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	bool bEnableClustering;
	/** Maximum level for cluster breaks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	int32 ClusterGroupIndex;
	/** Maximum level for cluster breaks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	int32 MaxClusterLevel;
	/** Damage threshold for clusters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Clustering")
	float DamageThreshold;
#endif

	//
	// ChaosPhysics | Initial Velocity
	//

	/** Where to pull initial velocity from - user defined or animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	EInitialVelocityTypeEnum InitialVelocityType;
	/** Initial linear velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	FVector InitialLinearVelocity;
	/** Initial angular velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
	FVector InitialAngularVelocity;

	//
	// Collision
	//

	/** */
	UPROPERTY(BlueprintAssignable, Category = "Collision")
	FOnChaosPhysicsCollision OnChaosPhysicsCollision;
	/** */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Physics Collision"), Category = "Collision")
	void ReceivePhysicsCollision(const FChaosPhysicsCollisionInfo& CollisionInfo);

	// IChaosNotifyHandlerInterface
	virtual void DispatchChaosPhysicsCollisionBlueprintEvents(const FChaosPhysicsCollisionInfo& CollisionInfo) override;

public:
	const TSharedPtr<FPhysScene_Chaos> GetPhysicsScene() const;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

protected:
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;

private:
	void UpdateAnimTransforms();

private:
	FSkeletalMeshPhysicsProxy* PhysicsProxy;

	//@todo(mlentine): Don't have one per static mesh
	TUniquePtr<Chaos::FChaosPhysicsMaterial> ChaosMaterial;
};
