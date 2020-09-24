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
const FName FGeometryDynamicCollection::CollisionMaskAttribute("CollisionMask");
const FName FGeometryDynamicCollection::DynamicStateAttribute("DynamicState");
const FName FGeometryDynamicCollection::ImplicitsAttribute("Implicits");
const FName FGeometryDynamicCollection::ShapesQueryDataAttribute("ShapesQueryData");
const FName FGeometryDynamicCollection::ShapesSimDataAttribute("ShapesSimData");
const FName FGeometryDynamicCollection::SimplicialsAttribute("CollisionParticles");
const FName FGeometryDynamicCollection::SimulatableParticlesAttribute("SimulatableParticlesAttribute");
const FName FGeometryDynamicCollection::SharedImplicitsAttribute("SharedImplicits");

FGeometryDynamicCollection::FGeometryDynamicCollection()
	: FTransformDynamicCollection()
{
	// Transform Group
	AddExternalAttribute<bool>(FGeometryDynamicCollection::ActiveAttribute, FTransformCollection::TransformGroup, Active);
	AddExternalAttribute<int32>(FGeometryDynamicCollection::CollisionGroupAttribute, FTransformCollection::TransformGroup, CollisionGroup);
	AddExternalAttribute<int32>(FGeometryDynamicCollection::CollisionMaskAttribute, FTransformCollection::TransformGroup, CollisionMask);
	AddExternalAttribute("CollisionStructureID", FTransformCollection::TransformGroup, CollisionStructureID);
	AddExternalAttribute<int32>(FGeometryDynamicCollection::DynamicStateAttribute, FTransformCollection::TransformGroup, DynamicState);
	AddExternalAttribute(ImplicitsAttribute, FTransformCollection::TransformGroup, Implicits);
	AddExternalAttribute("InitialAngularVelocity", FTransformCollection::TransformGroup, InitialAngularVelocity);
	AddExternalAttribute("InitialLinearVelocity", FTransformCollection::TransformGroup, InitialLinearVelocity);
	AddExternalAttribute("MassToLocal", FTransformCollection::TransformGroup, MassToLocal);
	//AddExternalAttribute(ShapesQueryDataAttribute, FTransformCollection::TransformGroup, ShapeQueryData);
	//AddExternalAttribute(ShapesSimDataAttribute, FTransformCollection::TransformGroup, ShapeSimData);
	AddExternalAttribute(SimplicialsAttribute, FTransformCollection::TransformGroup, Simplicials);
	AddExternalAttribute(SimulatableParticlesAttribute, FGeometryCollection::TransformGroup, SimulatableParticles);

}
