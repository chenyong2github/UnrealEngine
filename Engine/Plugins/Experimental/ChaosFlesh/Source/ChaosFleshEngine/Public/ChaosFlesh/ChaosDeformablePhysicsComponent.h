// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "Chaos/Deformable/ChaosDeformableSolver.h"
#include "ChaosFlesh/ChaosDeformableSolverThreading.h"
#include "Components/MeshComponent.h"
#include "UObject/ObjectMacros.h"
#include "ProceduralMeshComponent.h"
#include "ChaosDeformablePhysicsComponent.generated.h"

class ADeformableSolverActor;
class UDeformableSolverComponent;

/**
*	UDeformablePhysicsComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class CHAOSFLESHENGINE_API UDeformablePhysicsComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	typedef Chaos::Softs::FDeformableSolver FDeformableSolver;
	typedef Chaos::Softs::FThreadingProxy FThreadingProxy;
	typedef Chaos::Softs::FDataMapValue FDataMapValue;

	~UDeformablePhysicsComponent() {}

	UFUNCTION(BlueprintCallable, Category = "Physics")
	void EnableSimulation(ADeformableSolverActor* Actor);


	virtual FThreadingProxy* NewProxy() { return nullptr; }
	virtual void AddProxy(Chaos::Softs::FDeformableSolver::FGameThreadAccess& GameThreadSolver);
	virtual void RemoveProxy(Chaos::Softs::FDeformableSolver::FGameThreadAccess& GameThreadSolver);
	virtual FDataMapValue NewDeformableData() { return FDataMapValue(nullptr); }
	virtual void UpdateFromSimualtion(const FDataMapValue* SimualtionBuffer) {}


	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;

#if WITH_EDITOR
	ADeformableSolverActor* PreEditChangePrimarySolver = nullptr;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** PrimarySolver */
	UDeformableSolverComponent* GetDeformableSolver();
	const UDeformableSolverComponent* GetDeformableSolver() const;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	TObjectPtr<ADeformableSolverActor> PrimarySolver;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	bool bTempEnableGravity = true;

	const FThreadingProxy* GetPhysicsProxy() const { return PhysicsProxy; }
	FThreadingProxy* GetPhysicsProxy() { return PhysicsProxy; }

protected:
	FThreadingProxy* PhysicsProxy = nullptr;

};
