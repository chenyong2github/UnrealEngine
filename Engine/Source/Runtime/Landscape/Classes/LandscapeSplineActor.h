// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "ILandscapeSplineInterface.h"
#include "LandscapeSplineActor.generated.h"

UCLASS(MinimalAPI, NotBlueprintable, NotPlaceable, hidecategories = (Display, Attachment, Physics, Debug, Lighting, Input))
class ALandscapeSplineActor : public AActor, public ILandscapeSplineInterface
{
	GENERATED_UCLASS_BODY()

protected:
	/** Guid for LandscapeInfo **/
	UPROPERTY()
	FGuid LandscapeGuid;
	
public:
	virtual FGuid GetLandscapeGuid() const override { return LandscapeGuid; }
	virtual ULandscapeSplinesComponent* GetSplinesComponent() const override;
	virtual FTransform LandscapeActorToWorld() const override;
	virtual ULandscapeInfo* GetLandscapeInfo() const override;
		
#if WITH_EDITOR
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::Bounds; }
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;

	void GetSharedProperties(ULandscapeInfo* InLandscapeInfo);

	virtual void Destroyed() override;
	virtual void PostRegisterAllComponents() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual bool EditorCanAttachTo(const AActor* InParent, FText& OutReason) const override { return false; }
	virtual AActor* GetSceneOutlinerParent() const;
	virtual bool SupportsForeignSplineMesh() const override { return false; }

	// Interface existes for backward compatibility. Should already be created since its this actor's root component.
	virtual void CreateSplineComponent() override { check(false); }
	virtual void CreateSplineComponent(const FVector& Scale3D) override { check(false); }
#endif
};

DEFINE_ACTORDESC_TYPE(ALandscapeSplineActor, FLandscapeSplineActorDesc);