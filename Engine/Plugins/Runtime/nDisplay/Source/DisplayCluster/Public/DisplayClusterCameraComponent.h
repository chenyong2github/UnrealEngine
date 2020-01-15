// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterCameraComponent.generated.h"

class UCameraComponent;


/**
 * Camera component
 */
UCLASS( ClassGroup=(Custom) )
class DISPLAYCLUSTER_API UDisplayClusterCameraComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterCameraComponent(const FObjectInitializer& ObjectInitializer);

public:
	/**
	* Returns currently used interpupillary distance.
	*
	* @return - distance between eyes (in meters)
	*/
	float GetInterpupillaryDistance() const
	{ return EyeDist; }

	/**
	* Configuration of interpupillary (interocular) distance
	*
	* @param Distance - distance between eyes (meters).
	*/
	void  SetInterpupillaryDistance(float Distance)
	{ EyeDist = Distance; }

	/**
	* Returns currently used eyes swap
	*
	* @return - eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	bool GetEyesSwap() const
	{ return bEyeSwap; }

	/**
	* Configure eyes swap state
	*
	* @param EyesSwapped - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	void SetEyesSwap(bool EyesSwapped)
	{ bEyeSwap = EyesSwapped; }

	/**
	* Toggles eyes swap state
	*
	* @return - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	bool ToggleEyesSwap()
	{ return (bEyeSwap = !bEyeSwap); }

	/**
	* Returns force eye offset value
	*
	* @return - -1, 0 or 1 depending on config file
	*/
	int GetForceEyeOffset() const
	{ return ForceEyeOffset; }

	/**
	* Set force offset value
	*
	* @param ForceOffset - force offset value
	*/
	void SetForceEyeOffset(int ForceOffset)
	{ ForceEyeOffset = (ForceOffset == 0 ? 0 : (ForceOffset < 0 ? -1 : 1)); }

public:
	virtual void SetSettings(const FDisplayClusterConfigSceneNode* ConfigData) override;
	virtual bool ApplySettings() override;

public:
	virtual void BeginPlay() override;

private:
	float EyeDist;
	bool  bEyeSwap;
	int   ForceEyeOffset = 0;

#if 0
	UCameraComponent* CameraComponent = nullptr;
#endif
};
