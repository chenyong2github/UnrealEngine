// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollectionTest.h"

namespace GeometryCollectionTest
{	
	
	template<class T>
	void BuildProximity();
	
	template<class T>
	void GeometryDeleteFromStart();

	template<class T>
	void GeometryDeleteFromEnd();

	template<class T>
	void GeometryDeleteFromMiddle();

	template<class T>
	void GeometryDeleteMultipleFromMiddle();

	template<class T>
	void GeometryDeleteRandom();

	template<class T>
	void GeometryDeleteRandom2();

	template<class T>
	void GeometryDeleteAll();

	template<class T>
	void GeometrySwapFlat();

	template<class T>
	void TestFracturedGeometry();
	
}
