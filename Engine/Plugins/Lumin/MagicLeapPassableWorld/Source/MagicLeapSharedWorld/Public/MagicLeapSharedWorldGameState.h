// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/GameState.h"
#include "MagicLeapSharedWorldTypes.h"
#include "MagicLeapSharedWorldGameState.generated.h"

UCLASS(BlueprintType)
class MAGICLEAPSHAREDWORLD_API AMagicLeapSharedWorldGameState : public AGameState
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Calculates the transform to be used to align coordinate spaces of connected clients.
	 *
	 * The result should be set as the world transform of the parent of the camera component.
	 * This function is a BlueprintNativeEvent, override to implement a custom behavior.
	 * Default implementation -> inv(inv(AlignmentTransform) * ClientPinTransform)
	 * and uses only yaw component in rotation to ensure up vector alignes with gravity.
	 * The result is an average of the calculated transforms for each shared pin.
	 * @return Alignment transform to be applied to the camera component parent.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="AR Shared World|Magic Leap")
	FTransform CalculateXRCameraRootTransform() const;

	/**
	 * Pins which are common in this environment and replicated to all clients.
	 * @see OnSharedWorldDataUpdated
	*/
	UPROPERTY(Replicated, BlueprintReadOnly, ReplicatedUsing=OnReplicate_SharedWorldData, Category="AR Shared World|Magic Leap")
	FMagicLeapSharedWorldSharedData SharedWorldData;

	/**
	 * Alignment transforms, replicated to all clients, to be used to calculate the final transform for the camera component parent to align coordinate spaces.
	 * Order should match the pin order in SharedWorldData.PinIDs.
	 * @see CalculateXRCameraRootTransform
	 * @see OnAlignmentTransformsUpdated
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, ReplicatedUsing=OnReplicate_AlignmentTransforms, Category="AR Shared World|Magic Leap")
	FMagicLeapSharedWorldAlignmentTransforms AlignmentTransforms;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapSharedWorldEvent);

	/**
	 * Event fired when shared pins are updated on the client.
	 * @see SharedWorldData
	 */
	UPROPERTY(BlueprintAssignable, Category="AR Shared World|Magic Leap")
	FMagicLeapSharedWorldEvent OnSharedWorldDataUpdated;

	/**
	 * Event fired when alignment transforms are updated on the client.
	 * @see AlignmentTransforms
	 */
	UPROPERTY(BlueprintAssignable, Category="AR Shared World|Magic Leap")
	FMagicLeapSharedWorldEvent OnAlignmentTransformsUpdated;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnReplicate_SharedWorldData();

	UFUNCTION()
	void OnReplicate_AlignmentTransforms();
};
