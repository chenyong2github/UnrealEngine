// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/Real.h"
#include "Chaos/Array.h"
namespace GeometryCollectionTest
{
	class EllipsoidGeometry
	{

	public:
		EllipsoidGeometry() {}
		~EllipsoidGeometry() {}

		static const TArray<Chaos::FReal>	RawVertexArray;
		static const TArray<int32>			RawIndicesArray;
	};
}