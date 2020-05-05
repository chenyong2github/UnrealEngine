// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollectionTest.h"

namespace GeometryCollectionTest
{	
	template<class T>
	void  TestDeleteCoincidentVertices();

	template<class T>
	void  TestDeleteCoincidentVertices2();

	template<class T>
	void  TestDeleteZeroAreaFaces();

	template<class T>
	void  TestDeleteHiddenFaces();

	template<class T>
	void  TestFillHoles();
}
