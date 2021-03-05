// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "CineCameraComponent.h"
#include "LensDistortionDataHandler.h"

#include "LiveLinkLensController.generated.h"

/**
 * LiveLink Controller for the LensRole to drive lens distortion data 
 */
UCLASS()
class LIVELINKLENS_API ULiveLinkLensController : public ULiveLinkControllerBase
{
	GENERATED_BODY()

public:
	ULiveLinkLensController() {}

	//~ Begin ULiveLinkControllerBase interface
	virtual void Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData) override;
	virtual bool IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport) override;
	virtual TSubclassOf<UActorComponent> GetDesiredComponentClass() const override;
	virtual void SetAttachedComponent(UActorComponent* ActorComponent) override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

protected:
	/** Whether or not to apply a post-process distortion effect directly to the attached CineCamera */
	UPROPERTY(EditAnywhere, Category = "Lens Distortion")
	bool bApplyDistortion = false;

	UPROPERTY(EditAnywhere, Category = "Lens Distortion", Transient)
	ULensDistortionDataHandler* LensDistortionHandler = nullptr;

private:
	/** Cached camera filmback settings used to restore the camera to its default state after disabling distortion */
	FCameraFilmbackSettings OriginalCameraFilmback;
	bool bIsDistortionSetup = false;
};