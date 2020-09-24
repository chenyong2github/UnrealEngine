// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <vector>

namespace DatasmithNavisworksUtilImpl {

	struct FGeometrySettings
	{
		double TriangleSizeThreshold = 0;
		double PositionThreshold = 0;
		double NormalThreshold = 0;
	};
	
	class FGeometry
	{
	public:
		uint32_t VertexCount = 0;
		std::vector<double> Coords;
		std::vector<double> Normals;
		std::vector<double> UVs;

		uint32_t TriangleCount = 0;
		std::vector<uint32_t> Indices;
		
		uint64_t ComputeHash();
		void Optimize();
	};
	
	class FTriangleReaderNative
	{
	public:
		FTriangleReaderNative();
		~FTriangleReaderNative();

		void Read(void* FragmentIUnknownPtr, FGeometry& Geom, FGeometrySettings& Settings);
	};
	
}
