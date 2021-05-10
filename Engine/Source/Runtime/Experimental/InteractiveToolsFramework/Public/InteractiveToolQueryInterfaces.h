// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractiveToolQueryInterfaces.generated.h"


//
// Below are various interfaces that a UInteractiveTool can implement to allow
// higher-level code (eg like an EdMode) to query into the Tool.
//



// UInterface for IInteractiveToolCameraFocusAPI
UINTERFACE(MinimalAPI)
class UInteractiveToolCameraFocusAPI : public UInterface
{
	GENERATED_BODY()
};

/**
 * IInteractiveToolCameraFocusAPI provides two functions that can be
 * used to extract "Focus" / "Region of Interest" information about an
 * active Tool:
 * 
 * GetWorldSpaceFocusBox() - provides a bounding box for an "active region" if one is known.
 *   An example of using the FocusBox would be to center/zoom the camera in a 3D viewport
 *   onto this box when the user hits a hotkey (eg 'f' in the Editor).
 *   Should default to the entire active object, if no subregion is available.
 * 
 * GetWorldSpaceFocusPoint() - provides a "Focus Point" at the cursor ray if one is known.
 *   This can be used to (eg) center the camera at the focus point.
 * 
 * The above functions should not be called unless the corresponding SupportsX() function returns true.
 */
class IInteractiveToolCameraFocusAPI
{
	GENERATED_BODY()
public:

	/**
	 * @return true if the implementation can provide a Focus Box
	 */
	virtual bool SupportsWorldSpaceFocusBox() { return false; }
	
	/**
	 * @return the current Focus Box
	 */
	virtual FBox GetWorldSpaceFocusBox() { return FBox(); }

	/**
	 * @return true if the implementation can provide a Focus Point
	 */
	virtual bool SupportsWorldSpaceFocusPoint() { return false; }

	/**
	 * @param WorldRay 3D Ray that should be used to find the focus point, generally ray under cursor
	 * @param PointOut computed Focus Point
	 * @return true if a Focus Point was found, can return false if (eg) the ray missed the target objects
	 */
	virtual bool GetWorldSpaceFocusPoint(const FRay& WorldRay, FVector& PointOut) { return false; }


};
