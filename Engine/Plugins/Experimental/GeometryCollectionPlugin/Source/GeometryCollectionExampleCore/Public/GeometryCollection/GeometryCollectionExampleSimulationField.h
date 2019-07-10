// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"

namespace GeometryCollectionExample
{	
	
	template<class T>
	bool RigidBodies_Field_KinematicActivation(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_InitialLinearVelocity(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_StayDynamic(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_LinearForce(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_Torque(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_Kill(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_LinearVelocity(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_CollisionGroup(ExampleResponse&& R);
	
	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test1(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test2(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test3(ExampleResponse&& R);

	template<class T>
	bool RigidBodies_Field_ClusterBreak_StrainModel_Test4(ExampleResponse&& R);
}
