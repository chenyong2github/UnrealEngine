// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "PhysicsInterfaceWrapperShared.h"

namespace Chaos
{
	template <typename T, int d>
	class TImplicitObject;
}

struct FActorShape
{
	int32 ActorIdx;	//todo: this needs to be a pointer or handle once we decide other parts of chaos interface
	const Chaos::TImplicitObject<float,3>* Shape;
};

struct FQueryHit : public FActorShape
{
	FQueryHit() : FaceIndex(-1) {}
	
	/**
	Face index of touched triangle, for triangle meshes, convex meshes and height fields. Defaults to -1 if face index is not available
	*/

	uint32 FaceIndex;
};

struct FLocationHit : public FQueryHit
{
	FHitFlags Flags;
	FVector WorldPosition;
	FVector WorldNormal;
	float Distance;
};

struct FRaycastHit : public FLocationHit
{
	float U;
	float V;
};

struct FOverlapHit : public FQueryHit
{
};

struct FSweepHit : public FLocationHit
{
};


FORCEINLINE float GetDistance(const FLocationHit& Hit)
{
	return Hit.Distance;
}