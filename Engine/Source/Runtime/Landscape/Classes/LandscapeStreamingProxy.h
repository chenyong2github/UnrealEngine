// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "LandscapeProxy.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionHandle.h"
#endif

#include "LandscapeStreamingProxy.generated.h"

class ALandscape;
class UMaterialInterface;

UCLASS(MinimalAPI, notplaceable)
class ALandscapeStreamingProxy : public ALandscapeProxy
{
	GENERATED_BODY()

public:
	ALandscapeStreamingProxy(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category=LandscapeProxy)
	TLazyObjectPtr<ALandscape> LandscapeActor;

#if WITH_EDITORONLY_DATA
	/** hard refs to actors that need to be loaded when this proxy is loaded */
	TSet<FWorldPartitionReference> ActorDescReferences;
#endif

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual bool ShouldExport() override { return false;  }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostRegisterAllComponents() override;
	virtual AActor* GetSceneOutlinerParent() const;
#endif
	//~ End UObject Interface

	//~ Begin ALandscapeBase Interface
	virtual ALandscape* GetLandscapeActor() override;
	virtual const ALandscape* GetLandscapeActor() const override;
#if WITH_EDITOR
	virtual UMaterialInterface* GetLandscapeMaterial(int8 InLODIndex = INDEX_NONE) const override;
	virtual UMaterialInterface* GetLandscapeHoleMaterial() const override;
#endif
	//~ End ALandscapeBase Interface

	// Check input Landscape actor is match for this LandscapeProxy (by GUID)
	bool IsValidLandscapeActor(ALandscape* Landscape);
};
