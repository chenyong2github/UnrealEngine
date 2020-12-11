// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterMeshActor.generated.h"

class UWaterMeshComponent;

UCLASS(Blueprintable)
class WATER_API AWaterMeshActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	const UWaterMeshComponent* GetWaterMeshComponent() const { return WaterMesh; }

	void MarkWaterMeshComponentForRebuild();
	void Update();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Texture)
	UTexture2D* WaterVelocityTexture;

	// HACK [jonathan.bard] : See UWaterMeshComponent, r : emove ASAP
	void SetLandscapeInfo(const FVector& InRTWorldLocation, const FVector& InRTWorldSizeVector);

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif
	
	virtual void PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph) override;
private:

#if WITH_EDITOR
	void OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh);
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<class AWaterBody>> SelectedWaterBodies;

	UPROPERTY(Transient)
	UBillboardComponent* ActorIcon;
#endif

	/** The water mesh component */
	UPROPERTY(VisibleAnywhere, Category = Water, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UWaterMeshComponent* WaterMesh;
};
