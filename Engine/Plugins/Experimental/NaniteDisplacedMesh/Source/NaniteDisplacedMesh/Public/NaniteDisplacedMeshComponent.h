// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/StaticMeshComponent.h"
#include "NaniteDisplacedMesh.h"
#include "NaniteDisplacedMeshComponent.generated.h"

class FPrimitiveSceneProxy;
class UStaticMeshComponent;
class UMaterialInterface;
class UTexture;

UCLASS(ClassGroup=Rendering, hidecategories=(Object,Activation,Collision,"Components|Activation",Physics), editinlinenew, meta=(BlueprintSpawnableComponent))
class NANITEDISPLACEDMESH_API UNaniteDisplacedMeshComponent : public UStaticMeshComponent
{
public:

	GENERATED_BODY()
	UNaniteDisplacedMeshComponent(const FObjectInitializer& Init);

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnRegister() override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Displacement)
	TObjectPtr<class UNaniteDisplacedMesh> DisplacedMesh;

private:


};
