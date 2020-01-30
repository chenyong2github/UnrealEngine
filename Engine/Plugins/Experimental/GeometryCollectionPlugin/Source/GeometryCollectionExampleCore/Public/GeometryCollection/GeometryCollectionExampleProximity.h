// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"

namespace GeometryCollectionExample
{	
	
	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API BuildProximity();
	
	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteFromStart();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteFromEnd();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteFromMiddle();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteMultipleFromMiddle();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteRandom();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteRandom2();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometryDeleteAll();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API GeometrySwapFlat();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API TestFracturedGeometry();
	
}
