// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/ObjectMacros.h"
#include "Chaos/PBDRigidParticles.h"

#include "GeometryCollectionSimulationTypes.generated.h"

UENUM()
enum class ECollisionTypeEnum : uint8
{
	Chaos_Volumetric         UMETA(DisplayName = "Implicit-Implicit"),
	Chaos_Surface_Volumetric UMETA(DisplayName = "Particle-Implicit"),
	//
	Chaos_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EImplicitTypeEnum : uint8
{
	Chaos_Implicit_Box UMETA(DisplayName = "Box"),
	Chaos_Implicit_Sphere UMETA(DisplayName = "Sphere"),
	Chaos_Implicit_Capsule UMETA(DisplayName = "Capsule"),
	Chaos_Implicit_LevelSet UMETA(DisplayName = "Level Set"),
	Chaos_Implicit_None UMETA(DisplayName = "None"),
	//
	Chaos_Max                UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EObjectStateTypeEnum : uint8
{
	Chaos_Object_Sleeping  = 1 /*Chaos::EObjectStateType::Sleeping*/   UMETA(DisplayName = "Sleeping"),
	Chaos_Object_Kinematic = 2 /*Chaos::EObjectStateType::Kinematic*/  UMETA(DisplayName = "Kinematic"),
	Chaos_Object_Static = 3    /*Chaos::EObjectStateType::Static*/     UMETA(DisplayName = "Static"),
	Chaos_Object_Dynamic   = 4 /*Chaos::EObjectStateType::Dynamic*/    UMETA(DisplayName = "Dynamic"),
	Chaos_Object_UserDefined     = 100                                 UMETA(DisplayName = "User Defined"),
	//
	Chaos_Max                UMETA(Hidden)
};

UENUM()
enum class EGeometryCollectionPhysicsTypeEnum : uint8
{
	Chaos_AngularVelocity          UMETA(DisplayName = "AngularVelocity"),
	Chaos_DynamicState             UMETA(DisplayName = "DynamicState"),
	Chaos_LinearVelocity           UMETA(DisplayName = "LinearVelocity"),
	Chaos_InitialAngularVelocity   UMETA(DisplayName = "InitialAngularVelocity"),
	Chaos_InitialLinearVelocity    UMETA(DisplayName = "InitialLinearVelocity"),
	Chaos_CollisionGroup           UMETA(DisplayName = "CollisionGroup"),
	Chaos_LinearForce              UMETA(DisplayName = "LinearForce"),
	Chaos_AngularTorque            UMETA(DisplayName = "AngularTorque"),
	//
	Chaos_Max						UMETA(Hidden)
};
inline
FName GEOMETRYCOLLECTIONSIMULATIONCORE_API 
GetGeometryCollectionPhysicsTypeName(EGeometryCollectionPhysicsTypeEnum Attribute)
{
	switch (Attribute)
	{
	case EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity:
		return "AngularVelocity";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_DynamicState:
		return "DynamicState";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity:
		return "LinearVelocity";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_InitialLinearVelocity:
		return "InitialLinearVelocity";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_InitialAngularVelocity:
		return "InitialAngularVelocity";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_CollisionGroup:
		return "CollisionGroup";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_LinearForce:
		return "LinearForce";
	case EGeometryCollectionPhysicsTypeEnum::Chaos_AngularTorque:
		return "AngularTorque";
	}
	return "None";
}


UENUM(BlueprintType)
enum class EInitialVelocityTypeEnum : uint8
{
	//Chaos_Initial_Velocity_Animation UMETA(DisplayName = "Animation"),
	Chaos_Initial_Velocity_User_Defined UMETA(DisplayName = "User Defined"),
	//Chaos_Initial_Velocity_Field UMETA(DisplayName = "Field"),
	Chaos_Initial_Velocity_None UMETA(DisplayName = "None"),
	//
	Chaos_Max                UMETA(Hidden)
};


UENUM(BlueprintType)
enum class EEmissionPatternTypeEnum : uint8
{
	Chaos_Emission_Pattern_First_Frame UMETA(DisplayName = "First Frame"),
	Chaos_Emission_Pattern_On_Demand UMETA(DisplayName = "On Demand"),
	//
	Chaos_Max                UMETA(Hidden)
};