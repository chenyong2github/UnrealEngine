// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
GeometryCollection.cpp: FGeometryCollection methods.
=============================================================================*/

#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/TransformCollection.h"

const FName FGeometryDynamicCollection::ActiveAttribute("Active");
const FName FGeometryDynamicCollection::CollisionGroupAttribute("CollisionGroup");
const FName FGeometryDynamicCollection::DynamicStateAttribute("DynamicState");


FGeometryDynamicCollection::FGeometryDynamicCollection( )
	: FTransformDynamicCollection()
{
	// Transform Group
	AddExternalAttribute<int32>(FGeometryDynamicCollection::DynamicStateAttribute, FTransformCollection::TransformGroup, DynamicState);
	AddExternalAttribute<bool>(FGeometryDynamicCollection::ActiveAttribute, FTransformCollection::TransformGroup, Active);
	AddExternalAttribute<int32>(FGeometryDynamicCollection::CollisionGroupAttribute, FTransformCollection::TransformGroup, CollisionGroup);
}