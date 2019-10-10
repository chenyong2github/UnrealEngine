// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "Field/FieldSystem.h"
#include "Field/FieldSystemObjects.h"
#include "Field/FieldSystemAsset.h"
#include "Field/FieldSystemComponentTypes.h"
#include "Chaos/ChaosSolverActor.h"

#include "FieldSystemComponent.generated.h"

struct FFieldSystemSampleData;
class FFieldSystemPhysicsProxy;
class FChaosSolversModule;

/**
*	FieldSystemComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class FIELDSYSTEMENGINE_API UFieldSystemComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()
	friend class FFieldSystemEditorCommands;

public:



	//~ Begin USceneComponent Interface.
	virtual bool HasAnySockets() const override { return false; }
	//~ Begin USceneComponent Interface.


	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.


	/** FieldSystem @todo(remove the field system, we dont need the asset*/
	void SetFieldSystem(UFieldSystem * FieldSystemIn) { FieldSystem = FieldSystemIn; }
	FORCEINLINE const UFieldSystem* GetFieldSystem() const { return FieldSystem; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Field")
	UFieldSystem* FieldSystem;


	//
	// Blueprint based field interface
	//

	/**
	*  ApplyLinearForce
	*    This function will dispatch a command to the physics thread to apply
	*    a uniform linear force on each particle within the simulation.
	*
	*    @param Enabled : Is this force enabled for evaluation. 
	*    @param Direction : The direction of the linear force
	*    @param Magnitude : The size of the linear force.
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyLinearForce(bool Enabled, FVector Direction, float Magnitude);


	/**
	*  ApplyStayDynamicField
	*    This function will dispatch a command to the physics thread to apply
	*    a kinematic to dynamic state change for the particles within the field.
	*
	*    @param Enabled : Is this force enabled for evaluation.
	*    @param Position : The location of the command
	*    @param Radius : Radial influence from the position
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyStayDynamicField(bool Enabled, FVector Position, float Radius);

	/**
	*  ApplyRadialForce
	*    This function will dispatch a command to the physics thread to apply
	*    a linear force that points away from a position.
	*
	*    @param Enabled : Is this force enabled for evaluation.
	*    @param Position : The origin point of the force
	*    @param Magnitude : The size of the linear force.
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyRadialForce(bool Enabled, FVector Position, float Magnitude);

	/**
	*  ApplyRadialVectorFalloffForce
	*    This function will dispatch a command to the physics thread to apply
	*    a linear force from a position in space. The force vector is weaker as
	*    it moves away from the center. 
	*
	*    @param Enabled : Is this force enabled for evaluation.
	*    @param Position : The origin point of the force
	*    @param Radius : Radial influence from the position, positions further away are weaker.
	*    @param Magnitude : The size of the linear force.
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyRadialVectorFalloffForce(bool Enabled, FVector Position, float Radius, float Magnitude);

	/**
	*  ApplyUniformVectorFalloffForce
	*    This function will dispatch a command to the physics thread to apply
	*    a linear force in a uniform direction. The force vector is weaker as
	*    it moves away from the center.
	*
	*    @param Enabled : Is this force enabled for evaluation.
	*    @param Position : The origin point of the force
	*    @param Direction : The direction of the linear force
	*    @param Radius : Radial influence from the position, positions further away are weaker.
	*    @param Magnitude : The size of the linear force.
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyUniformVectorFalloffForce(bool Enabled, FVector Position, FVector Direction, float Radius, float Magnitude);

	/**
	*  ApplyStrainField
	*    This function will dispatch a command to the physics thread to apply
	*    a strain field on a clustered set of geometry. This is used to trigger a 
	*    breaking even within the solver.
	*
	*    @param Enabled : Is this force enabled for evaluation.
	*    @param Position : The origin point of the force
	*    @param Radius : Radial influence from the position, positions further away are weaker.
	*    @param Magnitude : The size of the linear force.
	*    @param Iterations : Levels of evaluation into the cluster hierarchy.
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyStrainField(bool Enabled, FVector Position, float Radius, float Magnitude, int32 Iterations);

	/**
	*  ApplyPhysicsField
	*    This function will dispatch a command to the physics thread to apply
	*    a generic evaluation of a user defined field network. See documentation,
	*    for examples of how to recreate variations of the above generic
	*    fields using field networks
	*
	*    (https://wiki.it.epicgames.net/display/~Brice.Criswell/Fields)
	*
	*    @param Enabled : Is this force enabled for evaluation.
	*    @param EFieldPhysicsType : Type of field supported by the solver.
	*    @param UFieldSystemMetaData : Meta data used to assist in evaluation
	*    @param UFieldNodeBase : Base evaluation node for the field network.
	*
	*/
	UFUNCTION(BlueprintCallable, Category = "Field")
	void ApplyPhysicsField(bool Enabled, EFieldPhysicsType Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field);



	//
	// Blueprint Construction based field interface
	//

	/**
	*  ClearFieldSystem
	*/
	UFUNCTION(BlueprintCallable, Category = "Field Construction")
	void ResetFieldSystem();

	/**
	*  ApplyPhysicsField
	*/
	UFUNCTION(BlueprintCallable, Category = "Field Construction")
	void AddFieldCommand(bool Enabled, EFieldPhysicsType Target, UFieldSystemMetaData* MetaData, UFieldNodeBase* Field);
	TArray< FFieldSystemCommand > BlueprintBufferedCommands;

	/** List of solvers this field will affect. An empty list makes this field affect all solvers. */
	UPROPERTY(EditAnywhere, Category = Field)
	TArray<TSoftObjectPtr<AChaosSolverActor>> SupportedSolvers;
	
protected:

	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;

	void DispatchCommand(const FFieldSystemCommand& InCommand);

	FFieldSystemPhysicsProxy* PhysicsProxy;
	FChaosSolversModule* ChaosModule;

	bool bHasPhysicsState;

};
