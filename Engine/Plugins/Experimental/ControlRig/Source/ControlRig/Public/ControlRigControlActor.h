// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigControlHierarchy.h"

#include "ControlRigControlActor.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "Control Display Actor"))
class CONTROLRIG_API AControlRigControlActor : public AActor
{
	GENERATED_BODY()

public:

	AControlRigControlActor(const FObjectInitializer& ObjectInitializer);
	~AControlRigControlActor();
	// AACtor overrides
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override { Clear(); }
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual bool IsSelectable() const override { return bIsSelectable; }
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Control Actor")
	class AActor* ActorToTrack;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Control Actor")
	TSubclassOf<UControlRig> ControlRigClass;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Control Actor")
	bool bRefreshOnTick;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Control Actor")
	bool bIsSelectable;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Materials")
	UMaterialInterface* MaterialOverride;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Materials")
	FString ColorParameter;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Materials")
	bool bCastShadows;

	UFUNCTION(BlueprintCallable, Category = "Control Actor")
	void Clear();

	UFUNCTION(BlueprintCallable, Category = "Control Actor")
	void Refresh();

private:

	UPROPERTY()
	class USceneComponent* ActorRootComponent;

	UPROPERTY(transient)
	UControlRig * ControlRig;

	UPROPERTY(transient)
	TArray<FName> ControlNames;

	UPROPERTY(transient)
	TArray<FTransform> GizmoTransforms;

	UPROPERTY(transient)
	TArray<UStaticMeshComponent*> Components;

	UPROPERTY(transient)
	TArray<UMaterialInstanceDynamic*> Materials;

	UPROPERTY(transient)
	FName ColorParameterName;
	
private:
	void RemoveUnbindDelegate();
	FDelegateHandle OnUnbindDelegate;
};