// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/ManagedArrayCollection.h"
#include "Components/MeshComponent.h"
#include "Dataflow/DataflowObject.h"
#include "UObject/ObjectMacros.h"
#include "ProceduralMeshComponent.h"
#include "DataflowRenderingComponent.generated.h"

/**
*	UDataflowRenderingComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class DATAFLOWENGINEPLUGIN_API UDataflowRenderingComponent : public UMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	~UDataflowRenderingComponent();

	void Invalidate();
	void UpdateLocalBounds();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	/** Render Targets*/
	void ResetRenderTargets(); 
	void AddRenderTarget(const UDataflowEdNode* InTarget); 
	const TArray<const UDataflowEdNode*>& GetRenderTargets() const {return RenderTargets;}

	/** Context */
	void SetContext(TSharedPtr<Dataflow::FContext> InContext) { Context = InContext; }

	/** RenderCollection */
	void ResetRenderingCollection();
	void SetRenderingCollection(FManagedArrayCollection&& InCollection);

	/** Dataflow */
	void SetDataflow(const UDataflow* InDataflow) { Dataflow = InDataflow; }
	const UDataflow* GetDataflow() const { return Dataflow; }

private:
	TSharedPtr<Dataflow::FContext> Context;
	TArray<const UDataflowEdNode*> RenderTargets;
	TObjectPtr< const UDataflow> Dataflow;
	FManagedArrayCollection RenderCollection;
	TObjectPtr<UProceduralMeshComponent> Mesh;

	//
	// Render the Procedural Mesh
	//
	struct FRenderMesh
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
	};
	FRenderMesh* RenderMesh = nullptr;
	void RenderProceduralMesh();
	void ResetProceduralMesh();

	bool bUpdateRender = true;
	bool bBoundsNeedsUpdate = true;
	FBoxSphereBounds BoundingBox = FBoxSphereBounds(ForceInitToZero);
};

