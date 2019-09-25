// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollection/GeometryCollectionExample.h"

namespace GeometryCollectionExample
{	
	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API TestDeleteCoincidentVertices();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API TestDeleteCoincidentVertices2();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API TestDeleteZeroAreaFaces();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API TestDeleteHiddenFaces();

	template<class T>
	void GEOMETRYCOLLECTIONEXAMPLECORE_API TestFillHoles();
}
