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

	UFUNCTION(BlueprintCallable, Category = "Physics")
	TArray<FVector> GetSkeletalMeshBindingPositions(const USkeletalMesh* InSkeletalMesh) const;

	/** USceneComponent Interface */
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	TObjectPtr<USkeletalMesh> TargetDeformationSkeleton;


private:
	// FleshAsset that describes the simulation rest state.
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics")
	TObjectPtr<const UFleshAsset> RestCollection;

	// Current simulation state.
	UPROPERTY(EditAnywhere, Category = "ChaosPhysics")
	TObjectPtr<UFleshDynamicAsset> DynamicCollection;

	//
	// Sim Space
	//

	// Space the simulation will run in.
	UPROPERTY(EditAnywhere, Category = "ChaosDeformable")
	TEnumAsByte<ChaosDeformableSimSpace> SimSpace = ChaosDeformableSimSpace::World;

	// Bone from the associated skeletal mesh (indicated by RestCollection.TargetSkeletalMesh) to use as the space the sim runs in.
	UPROPERTY(EditAnywhere, Category = "ChaosDeformable", meta = (GetOptions = "GetSimSpaceBoneNameOptions", EditCondition = "SimSpace == ChaosDeformableSimSpace::Bone"))
	FName SimSpaceBoneName;

	UPROPERTY()
	TObjectPtr<USkeletalMesh> SimSpaceSkeletalMesh;

	// Returns a list of bone names from the currently selected skeletal mesh.
	UFUNCTION(CallInEditor)
	TArray<FString> GetSimSpaceBoneNameOptions() const;

	//! Update \c SimSpaceSkeletalMesh and \c SimSpaceTransformIndex according to
	//! \c RestCollection->TargetSkeletalMesh and SimSpaceBoneName.
	//! \ret \c true if a valid sim space transform is found.
	bool UpdateSimSpaceTransformIndex();

	//! Return the rest transform to be used as the simulation space. 
	//! \c UpdateSimSpaceTransformIndex() must be called prior to calling this function.
	FTransform GetSimSpaceRestTransform() const;

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

	TArray<FVector> GetSkeletalMeshBindingPositionsInternal(const USkeletalMesh* InSkeletalMesh, TArray<bool>* OutInfluence = nullptr) const;
	void DebugDrawSkeletalMeshBindingPositions() const;

	int32 SimSpaceTransformIndex = INDEX_NONE;
	int32 SimSpaceTransformGlobalIndex = INDEX_NONE;
};

