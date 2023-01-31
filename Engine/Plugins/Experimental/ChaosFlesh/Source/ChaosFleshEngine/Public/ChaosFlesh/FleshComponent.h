// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "Chaos/Deformable/ChaosDeformableSolver.h"
#include "ChaosFlesh/ChaosDeformableSolverThreading.h"
#include "ChaosFlesh/ChaosDeformablePhysicsComponent.h"
#include "ChaosFlesh/FleshAsset.h"
#include "ChaosFlesh/FleshDynamicAsset.h"
#include "Components/MeshComponent.h"
#include "UObject/ObjectMacros.h"
#include "ProceduralMeshComponent.h"
#include "FleshComponent.generated.h"

class FFleshCollection;
class ADeformableSolverActor;
class UDeformableSolverComponent;

/**
*	FleshComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class CHAOSFLESHENGINE_API UFleshComponent : public UDeformablePhysicsComponent
{
	GENERATED_UCLASS_BODY()

public:
	typedef Chaos::Softs::FFleshThreadingProxy FFleshThreadingProxy;

	~UFleshComponent();

	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	void UpdateLocalBounds();
	virtual void EndPlay(const EEndPlayReason::Type ReasonEnd) override;
	void Invalidate();

	/** Simulation Interface*/
	virtual FThreadingProxy* NewProxy() override;
	virtual FDataMapValue NewDeformableData() override;
	virtual void UpdateFromSimualtion(const FDataMapValue* SimualtionBuffer) override;


	/** RestCollection */
	void SetRestCollection(const UFleshAsset * InRestCollection);
	const UFleshAsset* GetRestCollection() const { return RestCollection; }


	/** DynamicCollection */
	void ResetDynamicCollection();
	UFleshDynamicAsset* GetDynamicCollection() { return DynamicCollection; }
	const UFleshDynamicAsset* GetDynamicCollection() const { return DynamicCollection; }


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attributes)
	TObjectPtr<UProceduralMeshComponent> Mesh;

private:
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics")
	TObjectPtr<const UFleshAsset> RestCollection;

	UPROPERTY(EditAnywhere, Category = "ChaosPhysics")
	TObjectPtr< UFleshDynamicAsset> DynamicCollection;

	//
	// Render the Procedural Mesh
	//
	struct FFleshRenderMesh
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
	};
	FFleshRenderMesh* RenderMesh = nullptr;
	void RenderProceduralMesh();
	void ResetProceduralMesh();

	bool bBoundsNeedsUpdate = true;
	FBoxSphereBounds BoundingBox = FBoxSphereBounds(ForceInitToZero);

	FTransform PrevTransform = FTransform::Identity;
};

