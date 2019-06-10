// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterCameraComponent.generated.h"


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
	* Get camera frustum near culling
	*
	* @return - near culling plane distance
	*/
	float GetNearCullingDistance() const
	{ return NearClipPlane; }

	/**
	* Set camera frustum near culling
	*
	* @param NCP - near culling plane distance
	*/
	void SetNearCullingDistance(float NCP)
	{
		NearClipPlane = NCP;
	}

	/**
	* Get camera frustum far culling
	*
	* @return - far culling plane distance
	*/
	float GetFarCullingDistance() const
	{ return FarClipPlane; }

	/**
	* Set camera frustum far culling
	*
	* @param FCP - far culling plane distance
	*/
	void SetFarCullingDistance(float FCP)
	{ FarClipPlane = FCP; }

	/**
	* Get camera frustum culling
	*
	* @param OutNCP - near culling plane distance
	* @param OutFCP - far culling plane distance
	*/
	void GetCullingDistance(float& OutNCP, float& OutFCP) const
	{
		OutNCP = NearClipPlane;
		OutFCP = FarClipPlane;
	}

	/**
	* Set camera frustum culling
	*
	* @param NCP - near culling plane distance
	* @param FCP - far culling plane distance
	*/
	void SetCullingDistance(float NCP, float FCP)
	{
		NearClipPlane = NCP;
		FarClipPlane = FCP;
	}

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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float EyeDist;
	bool  bEyeSwap;
	int   ForceEyeOffset = 0;
	float NearClipPlane;
	float FarClipPlane;
};
