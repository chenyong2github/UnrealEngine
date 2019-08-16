// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "InputCoreTypes.h"

/*
* Controller button name definitions
*/
#define LeftMenuName "WindowsSpatialInput_LeftMenu"
#define LeftMenuFriendlyName "Windows Spatial Input (L) Menu"
#define RightMenuName "WindowsSpatialInput_RightMenu"
#define RightMenuFriendlyName "Windows Spatial Input (R) Menu"

#define LeftTouchpadPressName "WindowsSpatialInput_LeftTouchpad"
#define LeftTouchpadPressFriendlyName "Windows Spatial Input (L) Touchpad"
#define RightTouchpadPressName "WindowsSpatialInput_RightTouchpad"
#define RightTouchpadPressFriendlyName "Windows Spatial Input (R) Touchpad"

#define LeftTouchpadIsTouchedName "WindowsSpatialInput_LeftTouchpadIsTouched"
#define LeftTouchpadIsTouchedFriendlyName "Windows Spatial Input (L) Touchpad Is Touched"
#define RightTouchpadIsTouchedName "WindowsSpatialInput_RightTouchpadIsTouched"	
#define RightTouchpadIsTouchedFriendlyName "Windows Spatial Input (R) Touchpad Is Touched"

/*
* Controller axis name definitions
*/

#define LeftTouchpadXName "WindowsSpatialInput_LeftTouchpad_X"
#define LeftTouchpadXFriendlyName "Windows Spatial Input (L) Touchpad X"
#define RightTouchpadXName "WindowsSpatialInput_RightTouchpad_X"
#define RightTouchpadXFriendlyName "Windows Spatial Input (R) Touchpad X"

#define LeftTouchpadYName "WindowsSpatialInput_LeftTouchpad_Y"
#define LeftTouchpadYFriendlyName "Windows Spatial Input (L) Touchpad Y"
#define RightTouchpadYName "WindowsSpatialInput_RightTouchpad_Y"
#define RightTouchpadYFriendlyName "Windows Spatial Input (R) Touchpad Y"

#define TapGestureName "WindowsSpatialInput_TapGesture"	
#define DoubleTapGestureName "WindowsSpatialInput_DoubleTapGesture"	
#define HoldGestureName "WindowsSpatialInput_HoldGesture"	

#define LeftTapGestureName "WindowsSpatialInput_LeftTapGesture"	
#define LeftDoubleTapGestureName "WindowsSpatialInput_LeftDoubleTapGesture"	
#define LeftHoldGestureName "WindowsSpatialInput_LeftHoldGesture"	

#define RightTapGestureName "WindowsSpatialInput_RightTapGesture"	
#define RightDoubleTapGestureName "WindowsSpatialInput_RightDoubleTapGesture"	
#define RightHoldGestureName "WindowsSpatialInput_RightHoldGesture"	

#define LeftManipulationGestureName "WindowsSpatialInput_LeftManipulationGesture"	
#define LeftManipulationXGestureName "WindowsSpatialInput_LeftManipulationXGesture"	
#define LeftManipulationYGestureName "WindowsSpatialInput_LeftManipulationYGesture"	
#define LeftManipulationZGestureName "WindowsSpatialInput_LeftManipulationZGesture"	

#define LeftNavigationGestureName "WindowsSpatialInput_LeftNavigationGesture"	
#define LeftNavigationXGestureName "WindowsSpatialInput_LeftNavigationXGesture"	
#define LeftNavigationYGestureName "WindowsSpatialInput_LeftNavigationYGesture"	
#define LeftNavigationZGestureName "WindowsSpatialInput_LeftNavigationZGesture"	


#define RightManipulationGestureName "WindowsSpatialInput_RightManipulationGesture"	
#define RightManipulationXGestureName "WindowsSpatialInput_RightManipulationXGesture"	
#define RightManipulationYGestureName "WindowsSpatialInput_RightManipulationYGesture"	
#define RightManipulationZGestureName "WindowsSpatialInput_RightManipulationZGesture"	

#define RightNavigationGestureName "WindowsSpatialInput_RightNavigationGesture"	
#define RightNavigationXGestureName "WindowsSpatialInput_RightNavigationXGesture"	
#define RightNavigationYGestureName "WindowsSpatialInput_RightNavigationYGesture"	
#define RightNavigationZGestureName "WindowsSpatialInput_RightNavigationZGesture"	

/*
* Keys struct
*/

struct FSpatialInputKeys
{
	static const FKey LeftGrasp;
	static const FKey RightGrasp;

	static const FKey LeftMenu;
	static const FKey RightMenu;

	static const FKey LeftTouchpadPress;
	static const FKey RightTouchpadPress;

	static const FKey LeftTouchpadIsTouched;
	static const FKey RightTouchpadIsTouched;

	static const FKey LeftTouchpadX;
	static const FKey RightTouchpadX;

	static const FKey LeftTouchpadY;
	static const FKey RightTouchpadY;
	
	static const FKey TapGesture;
	static const FKey DoubleTapGesture;

	static const FKey HoldGesture;


	static const FKey LeftTapGesture;
	static const FKey LeftDoubleTapGesture;
	static const FKey LeftHoldGesture;

	static const FKey RightTapGesture;
	static const FKey RightDoubleTapGesture;
	static const FKey RightHoldGesture;


	static const FKey LeftManipulationGesture;
	static const FKey LeftManipulationXGesture;
	static const FKey LeftManipulationYGesture;
	static const FKey LeftManipulationZGesture;

	static const FKey LeftNavigationGesture;
	static const FKey LeftNavigationXGesture;
	static const FKey LeftNavigationYGesture;
	static const FKey LeftNavigationZGesture;


	static const FKey RightManipulationGesture;
	static const FKey RightManipulationXGesture;
	static const FKey RightManipulationYGesture;
	static const FKey RightManipulationZGesture;

	static const FKey RightNavigationGesture;
	static const FKey RightNavigationXGesture;
	static const FKey RightNavigationYGesture;
	static const FKey RightNavigationZGesture;
};
