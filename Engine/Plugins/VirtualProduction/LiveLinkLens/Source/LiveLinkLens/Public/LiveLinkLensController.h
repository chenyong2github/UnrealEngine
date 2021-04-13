// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkControllerBase.h"

#include "CineCameraComponent.h"
#include "LensDistortionModelHandlerBase.h"
#include "Materials/MaterialInstanceDynamic.h"

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
	virtual void Cleanup() override;
	//~ End ULiveLinkControllerBase interface

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

protected:

	/** Refresh distortion handler's, in case user deletes it  */
	void UpdateDistortionHandler(UCineCameraComponent* CineCameraComponent);

	/** Update cached focal length in case it was changed */
	void UpdateCachedFocalLength(UCineCameraComponent* CineCameraComponent);

	/** Cleanup distortion objects we could have added to camera */
	void CleanupDistortion();

protected:
	/** Whether or not to apply a post-process distortion effect directly to the attached CineCamera */
	UPROPERTY(EditAnywhere, Category = "Lens Distortion")
	bool bApplyDistortion = false;

	/** Cached distortion handler associated with attached camera component */
	UPROPERTY(EditAnywhere, Category = "Lens Distortion", Transient)
	ULensDistortionModelHandlerBase* LensDistortionHandler = nullptr;

	/** Cached distortion MID the handler produced. Used to clean up old one in case it changes */
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* LastDistortionMID = nullptr;

	/** Original focal length of the attached cinecamera component to reapply when distortion isn't applied anymore */
 	UPROPERTY()
	float UndistortedFocalLength = 50.0f;

	/** Keep track of what needs to be setup to apply distortion */
	bool bIsDistortionSetup = false;

	//Last value used to detect changes made by the user and update our original caches
	float LastFocalLength = -1.0f;
};