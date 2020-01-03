// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "MagicLeapImageTrackerTypes.h"
#include "MagicLeapImageTrackerComponent.generated.h"

/**
  The MLImageTrackerComponent will keep track of whether the image that it has been provided is currently
  visible to the headset camera.
  @note Currently only R8G8B8A8 and B8G8R8A8 textures are supported.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPIMAGETRACKER_API UMagicLeapImageTrackerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMagicLeapImageTrackerComponent();

	/**
		Initiates the setting of the image target (if TargetImageTexture is not null).
	*/
	//void BeginPlay() override;

	/**
		Initiates the removal of the current image target (if valid).
	*/
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
		Attempts to retrieve and set the relative pose of the tracked image.
	*/
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	/**
		Attempts to change the currently tracked target.  Initiates an asynchronous call on a worker thread.
		When the task completes, the instigating blueprint will be notified by either a FSetImageTargetSucceeded
		or FSetImageTargetFailed event.
		@param ImageTarget The new texture to be tracked.
		@return True if the initiation of the target change was successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "ImageTracking|MagicLeap")
	bool SetTargetAsync(UTexture2D* ImageTarget);

	/**
		Attempts to remove the currently tracked target.  Initiates an asynchronous call on a worker thread.
		@return True if the initiation of the target removal was successful, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "ImageTracking|MagicLeap")
	bool RemoveTargetAsync();

	/** The texture that will be tracked by this image tracker instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	UTexture2D* TargetImageTexture;

	/**
	  The name of the target.
	  This name has to be unique across all instances of the ImageTrackerComponent class.
	  If empty, the name of the component will be used.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	FString Name;

	/** LongerDimension refers to the size of the longer dimension (width or height) of the physical image target in Unreal units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	float LongerDimension;

	/** Set this to true to improve detection for stationary targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	bool bIsStationary;

	/** If false, the pose will not be updated when tracking is unreliable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImageTracking|MagicLeap")
	bool bUseUnreliablePose;

//private:
	/** Activated when the target image is successfully set. */
	UPROPERTY(BlueprintAssignable, Category = "ImageTracking | MagicLeap", meta = (AllowPrivateAccess = true))
	FSetImageTargetSucceededMulti OnSetImageTargetSucceeded;

	/** Activated when the target image fails to be set. */
	UPROPERTY(BlueprintAssignable, Category = "ImageTracking | MagicLeap", meta = (AllowPrivateAccess = true))
	FSetImageTargetFailedMulti OnSetImageTargetFailed;

	/** Activated when the target image becomes visible to the camera */
	UPROPERTY(BlueprintAssignable, Category = "ImageTracking | MagicLeap", meta = (AllowPrivateAccess = true))
	FImageTargetFoundMulti OnImageTargetFound;

	/** Activated the target image becomes invisible to the camera */
	UPROPERTY(BlueprintAssignable, Category = "ImageTracking | MagicLeap", meta = (AllowPrivateAccess = true))
	FImageTargetLostMulti OnImageTargetLost;

	/**
	  Activated when the target image is tracked with low confidence.

	  The Image Tracker system will still provide a 6 DOF pose. But this
	  pose might be inaccurate and might have jitter. When the tracking is
	  unreliable one of the folling two events will happen quickly : Either
	  the tracking will recover to Tracked or tracking will be lost and the
	  status will change to NotTracked.
	*/
	UPROPERTY(BlueprintAssignable, Category = "ImageTracking | MagicLeap", meta = (AllowPrivateAccess = true))
	FImageTargetUnreliableTrackingMulti OnImageTargetUnreliableTracking;

private:
	bool bIsTracking;
#if WITH_EDITOR
	UTexture2D* TextureBeforeEdit;
public:
	void PreEditChange(FProperty* PropertyAboutToChange) override;
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
