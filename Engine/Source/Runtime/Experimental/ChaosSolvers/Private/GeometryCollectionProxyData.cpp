// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
GeometryCollectionProxyData.cpp: 
=============================================================================*/

#include "GeometryCollectionProxyData.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/TransformCollection.h"

/*
* FTransformDynamicCollection (FManagedArrayCollection)
*/

FTransformDynamicCollection::FTransformDynamicCollection()
	: FManagedArrayCollection()
{
	Construct();
}


void FTransformDynamicCollection::Construct()
{
	FManagedArrayCollection::FConstructionParameters TransformDependency(FTransformCollection::TransformGroup);

	// Transform Group
	AddExternalAttribute<FTransform>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup, Transform);
	AddExternalAttribute<int32>(FTransformCollection::ParentAttribute, FTransformCollection::TransformGroup, Parent);
	AddExternalAttribute<TSet<int32>>(FTransformCollection::ChildrenAttribute, FTransformCollection::TransformGroup, Children);
	AddExternalAttribute<int32>(FGeometryCollection::SimulationTypeAttribute, FTransformCollection::TransformGroup, SimulationType);
	AddExternalAttribute<int32>(FGeometryCollection::StatusFlagsAttribute, FTransformCollection::TransformGroup, StatusFlags);
}

/*
* FGeometryDynamicCollection (FTransformDynamicCollection)
*/

const FName FGeometryDynamicCollection::ActiveAttribute("Active");
const FName FGeometryDynamicCollection::CollisionGroupAttribute("CollisionGroup");
const FName FGeometryDynamicCollection::DynamicStateAttribute("DynamicState");


FGeometryDynamicCollection::FGeometryDynamicCollection()
	: FTransformDynamicCollection()
{
	// Transform Group
	AddExternalAttribute<int32>(FGeometryDynamicCollection::DynamicStateAttribute, FTransformCollection::TransformGroup, DynamicState);
	AddExternalAttribute<bool>(FGeometryDynamicCollection::ActiveAttribute, FTransformCollection::TransformGroup, Active);
	AddExternalAttribute<int32>(FGeometryDynamicCollection::CollisionGroupAttribute, FTransformCollection::TransformGroup, CollisionGroup);
}
