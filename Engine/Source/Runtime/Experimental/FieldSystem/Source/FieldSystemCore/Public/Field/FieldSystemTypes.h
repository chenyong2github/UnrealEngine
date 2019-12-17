// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "UObject/ObjectMacros.h"

/**
*
*/
UENUM(BlueprintType)
enum ESetMaskConditionType
{
	Field_Set_Always	        UMETA(DisplayName = "Always"),
	Field_Set_IFF_NOT_Interior  UMETA(DisplayName = "IFF NOT Interior"),
	Field_Set_IFF_NOT_Exterior  UMETA(DisplayName = "IFF NOT Exterior"),
	//~~~
	//256th entry
	Field_MaskCondition_Max                 UMETA(Hidden)
};


/**
*
*/
UENUM(BlueprintType)
enum EFieldOperationType
{
	Field_Multiply  UMETA(DisplayName = "Multiply"),
	Field_Divide    UMETA(DisplayName = "Divide"),
	Field_Add       UMETA(DisplayName = "Add"),
	Field_Substract UMETA(DisplayName = "Subtract"),
	//~~~
	//256th entry
	Field_Operation_Max                 UMETA(Hidden)
};

/**
*
*/
UENUM(BlueprintType)
enum EFieldCullingOperationType
{
	Field_Culling_Inside  UMETA(DisplayName = "Inside"),
	Field_Culling_Outside UMETA(DisplayName = "Outside"),
	//~~~
	//256th entry
	Field_Culling_Operation_Max                 UMETA(Hidden)
};


/**
*
*/
UENUM(BlueprintType)
enum EFieldResolutionType
{
	Field_Resolution_Minimal  UMETA(DisplayName = "Minimal"),
	Field_Resolution_DisabledParents  UMETA(DisplayName = "Minimal Plus Disabled Parents"),
	Field_Resolution_Maximum  UMETA(DisplayName = "Maximum"),
	//~~~
	//256th entry
	Field_Resolution_Max      UMETA(Hidden)
};


/**
*
*/
UENUM(BlueprintType)
enum EFieldFalloffType
{
	Field_FallOff_None			UMETA(DisplayName = "None"),
	Field_Falloff_Linear		UMETA(DisplayName = "Linear"),
	Field_Falloff_Inverse		UMETA(DisplayName = "Inverse"),
	Field_Falloff_Squared		UMETA(DisplayName = "Squared"),
	Field_Falloff_Logarithmic	UMETA(DisplayName = "Logarithmic"),
	//~~~
	//256th entry
	Field_Falloff_Max           UMETA(Hidden)
};

/**
*
*/
UENUM(BlueprintType)
enum EFieldPhysicsType
{
	Field_None						UMETA(Hidden),
	Field_DynamicState				UMETA(DisplayName = "DynamicState"),
	Field_LinearForce				UMETA(DisplayName = "LinearForce"),
	Field_ExternalClusterStrain		UMETA(DisplayName = "ExternalClusterStrain"),
	Field_Kill   					UMETA(DisplayName = "Kill"),
	Field_LinearVelocity			UMETA(DisplayName = "LinearVelocity"),
	Field_AngularVelociy			UMETA(DisplayName = "AngularVelocity"),
	Field_AngularTorque				UMETA(DisplayName = "AngularTorque"),
	Field_InternalClusterStrain		UMETA(DisplayName = "InternalClusterStrain"),
	Field_DisableThreshold			UMETA(DisplayName = "DisableThreshold"),
	Field_SleepingThreshold			UMETA(DisplayName = "SleepingThreshold"),
	Field_PositionStatic			UMETA(DisplayName = "PositionStatic"),
	Field_PositionAnimated			UMETA(DisplayName = "PositionAnimated"),
	Field_PositionTarget			UMETA(DisplayName = "PositionTarget"),
	Field_DynamicConstraint			UMETA(DisplayName = "DynamicConstraint"),
	Field_CollisionGroup			UMETA(DisplayName = "CollisionGroup"),
	Field_ActivateDisabled			UMETA(DisplayName = "ActivateDisabled"),
	//~~~
	//256th entry
	Field_PhysicsType_Max           UMETA(Hidden)
};

inline 
FName FIELDSYSTEMCORE_API GetFieldPhysicsName(EFieldPhysicsType Type)
{
	switch(Type)
	{
	case Field_DynamicState:
		return "DynamicState";
	case Field_LinearForce:
		return "LinearForce";
	case Field_ExternalClusterStrain:
		return "ExternalClusterStrain";
	case Field_Kill:
		return "Kill";
	case Field_LinearVelocity:
		return "LinearVelocity";
	case Field_AngularVelociy:
		return "AngularVelocity";
	case Field_AngularTorque:
		return "AngularTorque";
	case Field_InternalClusterStrain:
		return "InternalClusterStrain";
	case Field_DisableThreshold:
		return "DisableThreshold";
	case Field_SleepingThreshold:
		return "SleepingThreshold";
	case Field_PositionStatic:
		return "PositionStatic";
	case Field_PositionAnimated:
		return "PositionAnimated";
	case Field_PositionTarget:
		return "PositionTarget";
	case Field_DynamicConstraint:
		return "DynamicConstraint";
	case Field_CollisionGroup:
		return "CollisionGroup";
	case Field_ActivateDisabled:
		return "ActivateDisabled";
	}
	return "None";
}

inline
EFieldPhysicsType FIELDSYSTEMCORE_API GetFieldPhysicsType(const FName& Name)
{
	if(Name == "DynamicState")
	{
		return Field_DynamicState;
	}
	else if(Name == "LinearForce")
	{
		return Field_LinearForce;
	}
	else if(Name == "ExternalClusterStrain")
	{
		return Field_ExternalClusterStrain;
	}
	else if(Name == "Kill")
	{
		return Field_Kill;
	}
	else if(Name == "LinearVelocity")
	{
		return Field_LinearVelocity;
	}
	else if (Name == "AngularVelocity")
	{
		return Field_AngularVelociy;
	}
	else if(Name == "AngularTorque")
	{
		return Field_AngularTorque;
	}
	else if(Name == "InternalClusterStrain")
	{
		return Field_InternalClusterStrain;
	}
	else if(Name == "DisableThreshold")
	{
		return Field_DisableThreshold;
	}
	else if(Name == "SleepingThreshold")
	{
		return Field_SleepingThreshold;
	}
	else if(Name == "PositionStatic")
	{
		return Field_PositionStatic;
	}
	else if(Name == "PositionAnimated")
	{
		return Field_PositionAnimated;
	}
	else if(Name == "PositionTarget")
	{
		return Field_PositionTarget;
	}
	else if(Name == "DynamicConstraint")
	{
		return Field_DynamicConstraint;
	}
	else if(Name == "CollisionGroup")
	{
		return Field_CollisionGroup;
	}
	else if(Name == "ActivateDisabled")
	{
		return Field_ActivateDisabled;
	}
	else if(Name == "None")
	{
		return Field_None;
	}
	else
	{
		check(false);
	}

	return Field_None;
}


/**
*
*/
UENUM(BlueprintType)
enum EFieldPhysicsDefaultFields
{
	Field_RadialIntMask				UMETA(DisplayName = "RadialIntMask"),
	Field_RadialFalloff				UMETA(DisplayName = "RadialFalloff"),
	Field_UniformVector				UMETA(DisplayName = "UniformVector"),
	Field_RadialVector				UMETA(DisplayName = "RadialVector"),
	Field_RadialVectorFalloff		UMETA(DisplayName = "RadialVectorFalloff"),
	//~~~
	//256th entry
	Field_EFieldPhysicsDefaultFields_Max                 UMETA(Hidden)
};




