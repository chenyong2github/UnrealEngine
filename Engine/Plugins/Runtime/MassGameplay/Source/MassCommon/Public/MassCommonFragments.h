// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "AI/Navigation/NavigationTypes.h"
#include "MassCommonFragments.generated.h"


USTRUCT()
struct MASSCOMMON_API FDataFragment_Transform : public FLWComponentData
{
	GENERATED_BODY()

	const FTransform& GetTransform() const { return Transform; }
	void SetTransform(const FTransform& InTransform) { Transform = InTransform; }
	FTransform& GetMutableTransform() { return Transform; }

protected:
	UPROPERTY(Transient)
	FTransform Transform;
};

USTRUCT()
struct MASSCOMMON_API FDataFragment_NavLocation : public FLWComponentData
{
	GENERATED_BODY()
	NavNodeRef NodeRef = INVALID_NAVNODEREF;
};

/** this component is a kind of an experiment. I've made FMassVelocityFragment and FMassAvoidanceFragment extend it and 
 *  use URandomizeVectorProcessor to initialize it to random variables at spawn time.
 */
USTRUCT()
struct MASSCOMMON_API FVectorComponent : public FLWComponentData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "")
	FVector Value = FVector::ZeroVector;
};

USTRUCT()
struct MASSCOMMON_API FDataFragment_AgentRadius : public FLWComponentData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "")
	float Radius = 40.f;
};

/** This is a common type for all the wrappers pointing at UObjects used to copy data from them or set data based on
 *	Mass simulation..
 */
USTRUCT()
struct FDataFragment_ObjectWrapper : public FLWComponentData
{
	GENERATED_BODY()
};
