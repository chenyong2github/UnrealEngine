// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "TransformTypes.h"

class UBodySetup;
class UStaticMesh;
struct FKAggregateGeom;

namespace UE
{
namespace Geometry
{

struct FSimpleShapeSet3d;

/**
 * Component/BodySetup collision settings (eg StaticMeshComponent) we might need to pass through the functions below
 */
struct MODELINGCOMPONENTS_API FComponentCollisionSettings
{
	int32 CollisionTypeFlag = 0;		// this is ECollisionTraceFlag
	bool bIsGeneratedCollision = true;
};

/**
 * @return true if the component type supports collision settings
 */
MODELINGCOMPONENTS_API bool ComponentTypeSupportsCollision(
	const UPrimitiveComponent* Component);


/**
 * @return current Component collision settings
 */
MODELINGCOMPONENTS_API FComponentCollisionSettings GetCollisionSettings(
	const UPrimitiveComponent* Component);


/**
 * Apply Transform to any Simple Collision geometry owned by Component.
 * Note that Nonuniform scaling support is very limited and will generally enlarge collision volumes.
 */
MODELINGCOMPONENTS_API bool TransformSimpleCollision(
	UPrimitiveComponent* Component,
	const FTransform3d& Transform);

/**
 * Replace existing Simple Collision geometry in Component with that defined by ShapeSet,
 * and update the Component/BodySetup collision settings
 */
MODELINGCOMPONENTS_API bool SetSimpleCollision(
	UPrimitiveComponent* Component,
	const FSimpleShapeSet3d* ShapeSet,
	FComponentCollisionSettings CollisionSettings = FComponentCollisionSettings() );


/**
 * Apply Transform to the existing Simple Collision geometry in Component and then append to to ShapeSetOut
 */
MODELINGCOMPONENTS_API bool AppendSimpleCollision(
	const UPrimitiveComponent* SourceComponent,
	FSimpleShapeSet3d* ShapeSetOut,
	const FTransform3d& Transform);

/**
 * Apply TransformSequence (in-order) to the existing Simple Collision geometry in Component and then append to to ShapeSetOut
 */
MODELINGCOMPONENTS_API bool AppendSimpleCollision(
	const UPrimitiveComponent* SourceComponent,
	FSimpleShapeSet3d* ShapeSetOut,
	const TArray<FTransform3d>& TransformSeqeuence);

/**
 * Replace existing Simple Collision geometry in BodySetup with that defined by NewGeometry,
 * and update the Component/BodySetup collision settings. Optional StaticMesh argument allows
 * for necessary updates to it and any active UStaticMeshComponent that reference it
 */
MODELINGCOMPONENTS_API void UpdateSimpleCollision(
	UBodySetup* BodySetup,
	const FKAggregateGeom* NewGeometry,
	UStaticMesh* StaticMesh = nullptr,
	FComponentCollisionSettings CollisionSettings = FComponentCollisionSettings());



}  // end namespace Geometry
}  // end namespace UE