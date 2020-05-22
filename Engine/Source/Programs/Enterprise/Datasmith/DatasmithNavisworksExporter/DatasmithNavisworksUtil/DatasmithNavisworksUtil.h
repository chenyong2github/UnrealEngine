// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include <cstdint>

namespace DatasmithNavisworksUtilImpl {
	class TriangleReaderNative;
}

namespace DatasmithNavisworksUtil {

	public ref class Geometry
	{
	public:
		explicit Geometry(class DatasmithNavisworksUtilImpl::TriangleReaderNative* Handle);
		~Geometry();
		!Geometry();

		int32_t VertexCount;
		double* Coords;
		double* Normals;
		double* UVs;

		int32_t TriangleCount;
		uint32_t* Indices;
		
	private:
		DatasmithNavisworksUtilImpl::TriangleReaderNative* Handle; // Handle to the native class holding data, so it can be released on time
		bool bIsDisposed;
	};
	
	public ref class TriangleReader
	{
	public:
		static Geometry^ ReadGeometry(Autodesk::Navisworks::Api::Interop::ComApi::InwOaFragment3^ Fragment);
	};
}
