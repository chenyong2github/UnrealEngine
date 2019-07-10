// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/** 
 * Convenience header for working with object storage in physics code.
 * When using FSolverObjectStorage::ForEachSolverObject a lambda in the 
 * generic for [](auto* Obj) {} is expected which will require all solver
 * objects are fully defined when used, including this header will include
 * the currently supported set of objects.
 */

#include "BodyInstancePhysicsObject.h"
#include "FieldSystemPhysicsObject.h"
#include "GeometryCollectionPhysicsObject.h"
#include "SkeletalMeshPhysicsObject.h"
#include "StaticMeshPhysicsObject.h"
