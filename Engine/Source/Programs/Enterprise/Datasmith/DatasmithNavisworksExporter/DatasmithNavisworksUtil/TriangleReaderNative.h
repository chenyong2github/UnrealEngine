// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <vector>

namespace DatasmithNavisworksUtilImpl {

	class TriangleReaderNative
	{
	public:

		int VertexCount = 0;
		std::vector<double> Coords;
		std::vector<double> Normals;
		std::vector<double> UVs;

		int TriangleCount = 0;
		std::vector<uint32_t> Indices;

		TriangleReaderNative();
		~TriangleReaderNative();

		void Read(void* FragmentIUnknownPtr);
	};
	
}
