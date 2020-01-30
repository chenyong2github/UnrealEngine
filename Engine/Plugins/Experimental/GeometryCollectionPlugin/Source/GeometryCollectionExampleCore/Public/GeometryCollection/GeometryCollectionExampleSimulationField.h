// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"
#include "GeometryCollection/GeometryCollectionExampleResponse.h"

namespace GeometryCollectionExample
{	
	
	template<class T>
	void RigidBodies_Field_KinematicActivation();

	template<class T>
	void RigidBodies_Field_InitialLinearVelocity();

	template<class T>
	void RigidBodies_Field_StayDynamic();

	template<class T>
	void RigidBodies_Field_LinearForce();

	template<class T>
	void RigidBodies_Field_Torque();

	template<class T>
	void RigidBodies_Field_Kill();

	template<class T>
	void RigidBodies_Field_LinearVelocity();

	template<class T>
	void RigidBodies_Field_CollisionGroup();
	
	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test1();

	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test2();

	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test3();

	template<class T>
	void RigidBodies_Field_ClusterBreak_StrainModel_Test4();
}
