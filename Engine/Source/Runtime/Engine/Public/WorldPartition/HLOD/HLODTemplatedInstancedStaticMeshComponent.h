// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Templates/SubclassOf.h"
#include "HLODTemplatedInstancedStaticMeshComponent.generated.h"

class AActor;

UCLASS(Hidden, NotPlaceable)
class ENGINE_API UHLODTemplatedInstancedStaticMeshComponent : public UInstancedStaticMeshComponent
{
	GENERATED_UCLASS_BODY()

public: 
	//~ Begin UObject Interface.
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;
	//~ End UObject Interface.
	
	void SetTemplateActorClass(const TSubclassOf<AActor>& InTemplateActorClass);
	void SetTemplateComponentName(const FName& InTemplateComponentName);
	
private:
	void RestoreAssetsFromActorTemplate();

private:
	UPROPERTY()
	TSubclassOf<AActor> TemplateActorClass;

	UPROPERTY()
	FName TemplateComponentName;
};
