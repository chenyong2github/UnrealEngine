// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Camera modifier that provides support for code-based oscillating camera shakes.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Camera/CameraTypes.h"
#include "Camera/CameraModifier.h"
#include "CameraModifier_CameraShake.generated.h"

class UCameraShake;
class UCameraShakeSourceComponent;

USTRUCT()
struct FPooledCameraShakes
{
	GENERATED_BODY()
		
	UPROPERTY()
	TArray<UCameraShake*> PooledShakes;
};

USTRUCT()
struct FActiveCameraShakeInfo
{
	GENERATED_BODY()

	FActiveCameraShakeInfo() : ShakeInstance(nullptr), ShakeSource(nullptr) {}

	UPROPERTY()
	UCameraShake* ShakeInstance;

	UPROPERTY()
	TWeakObjectPtr<const UCameraShakeSourceComponent> ShakeSource;
};

struct FAddCameraShakeParams
{
	float Scale;
	ECameraAnimPlaySpace::Type PlaySpace;
	FRotator UserPlaySpaceRot;
	const UCameraShakeSourceComponent* SourceComponent;

	FAddCameraShakeParams() 
		: Scale(1.f), PlaySpace(ECameraAnimPlaySpace::CameraLocal), UserPlaySpaceRot(FRotator::ZeroRotator), SourceComponent(nullptr)
	{}
	FAddCameraShakeParams(float InScale, ECameraAnimPlaySpace::Type InPlaySpace = ECameraAnimPlaySpace::CameraLocal, FRotator InUserPlaySpaceRot = FRotator::ZeroRotator, const UCameraShakeSourceComponent* InSourceComponent = nullptr)
		: Scale(InScale), PlaySpace(InPlaySpace), UserPlaySpaceRot(InUserPlaySpaceRot), SourceComponent(InSourceComponent)
	{}
};

//~=============================================================================
/**
 * A UCameraModifier_CameraShake is a camera modifier that can apply a UCameraShake to 
 * the owning camera.
 */
UCLASS(config=Camera)
class ENGINE_API UCameraModifier_CameraShake : public UCameraModifier
{
	GENERATED_BODY()

public:
	UCameraModifier_CameraShake(const FObjectInitializer& ObjectInitializer);

	/** 
	 * Adds a new active screen shake to be applied. 
	 * @param NewShake - The class of camera shake to instantiate.
	 * @param Params - The parameters for the new camera shake.
	 */
	virtual UCameraShake* AddCameraShake(TSubclassOf<UCameraShake> NewShake, const FAddCameraShakeParams& Params);

	/** 
	 * Adds a new active screen shake to be applied. 
	 * @param NewShake - The class of camera shake to instantiate.
	 * @param Scale - The scalar intensity to play the shake.
	 * @param PlaySpace - Which coordinate system to play the shake in.
	 * @param UserPlaySpaceRot - Coordinate system to play shake when PlaySpace == CAPS_UserDefined.
	 */
	UE_DEPRECATED(4.25, "Please use the new AddCameraShake method that takes a parameter struct.")
	virtual class UCameraShake* AddCameraShake(TSubclassOf<UCameraShake> NewShake, float Scale, ECameraAnimPlaySpace::Type PlaySpace=ECameraAnimPlaySpace::CameraLocal, FRotator UserPlaySpaceRot = FRotator::ZeroRotator)
	{
		return AddCameraShake(NewShake, FAddCameraShakeParams(Scale, PlaySpace, UserPlaySpaceRot));
	}

	/**
	 * Returns a list of currently active camera shakes.
	 * @param ActiveCameraShakes - The array to fill up with shake information.
	 */
	virtual void GetActiveCameraShakes(TArray<FActiveCameraShakeInfo>& ActiveCameraShakes) const;
	
	/**
	 * Stops and removes the camera shake of the given class from the camera.
	 * @param Shake - the camera shake class to remove.
	 * @param bImmediately		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveCameraShake(UCameraShake* ShakeInst, bool bImmediately = true);

	/**
	 * Stops and removes all camera shakes of the given class from the camera. 
	 * @param bImmediately		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveAllCameraShakesOfClass(TSubclassOf<UCameraShake> ShakeClass, bool bImmediately = true);

	virtual void RemoveAllCameraShakesFromSource(const UCameraShakeSourceComponent* SourceComponent, bool bImmediately = true);

	/** 
	 * Stops and removes all camera shakes from the camera. 
	 * @param bImmediately		If true, shake stops right away regardless of blend out settings. If false, shake may blend out according to its settings.
	 */
	virtual void RemoveAllCameraShakes(bool bImmediately = true);
	
	//~ Begin UCameraModifer Interface
	virtual bool ModifyCamera(float DeltaTime, struct FMinimalViewInfo& InOutPOV) override;
	//~ End UCameraModifer Interface

protected:

	/** List of active CameraShake instances */
	UPROPERTY()
	TArray<FActiveCameraShakeInfo> ActiveShakes;

	UPROPERTY()
	TMap<TSubclassOf<UCameraShake>, FPooledCameraShakes> ExpiredPooledShakesMap;

	void SaveShakeInExpiredPool(UCameraShake* ShakeInst);
	UCameraShake* ReclaimShakeFromExpiredPool(TSubclassOf<UCameraShake> CameraShakeClass);

	/** Scaling factor applied to all camera shakes in when in splitscreen mode. Normally used to reduce shaking, since shakes feel more intense in a smaller viewport. */
	UPROPERTY(EditAnywhere, Category = CameraModifier_CameraShake)
	float SplitScreenShakeScale;
};
