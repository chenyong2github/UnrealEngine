// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterCameraComponent.generated.h"

class UStaticMeshComponent;


UENUM()
enum class EDisplayClusterEyeStereoOffset : uint8
{
	None,
	Left,
	Right
};


/**
 * Camera component
 */
UCLASS(ClassGroup = (DisplayCluster))
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
	{
		return InterpupillaryDistance;
	}

	/**
	* Configuration of interpupillary (interocular) distance
	*
	* @param Distance - distance between eyes (meters).
	*/
	void SetInterpupillaryDistance(float Distance)
	{
		InterpupillaryDistance = Distance;
	}

	/**
	* Returns currently used eyes swap
	*
	* @return - eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	bool GetSwapEyes() const
	{
		return bSwapEyes;
	}

	/**
	* Configure eyes swap state
	*
	* @param SwapEyes - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	void SetSwapEyes(bool SwapEyes)
	{
		bSwapEyes = SwapEyes;
	}

	/**
	* Toggles eyes swap state
	*
	* @return - new eyes swap state. False - normal eyes left|right, true - swapped eyes right|left
	*/
	bool ToggleSwapEyes()
	{
		return (bSwapEyes = !bSwapEyes);
	}

	/**
	* Returns stereo offset type
	*
	* @return - -1, 0 or 1 depending on config file
	*/
	EDisplayClusterEyeStereoOffset GetStereoOffset() const
	{
		return StereoOffset;
	}

	/**
	* Set stereo offset type
	*
	* @param InStereoOffset - stereo offset type
	*/
	void SetStereoOffset(EDisplayClusterEyeStereoOffset InStereoOffset)
	{
		StereoOffset = InStereoOffset;
	}

protected:
	virtual void ApplyConfigurationData();

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	float InterpupillaryDistance;
	
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	bool bSwapEyes;
	
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	EDisplayClusterEyeStereoOffset StereoOffset;

	UPROPERTY(VisibleAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* VisCameraComponent = nullptr;

#if WITH_EDITOR 
public:
	virtual void SetNodeSelection(bool bSelect) override;
#endif
};
