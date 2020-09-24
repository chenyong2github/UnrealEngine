// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include <cstdint>

namespace DatasmithNavisworksUtilImpl {
	struct FGeometrySettings;
	class FTriangleReaderNative;
	class FGeometry;
}

namespace DatasmithNavisworksUtil {

	public ref struct GeometrySettings
	{
		explicit GeometrySettings();
		~GeometrySettings();
		!GeometrySettings();

		void SetTriangleSizeThreshold(double);
		void SetPositionThreshold(double);
		void SetNormalThreshold(double);
		
		DatasmithNavisworksUtilImpl::FGeometrySettings* Handle; // Handle to the native class holding data, so it can be released on time
	private:
		bool bIsDisposed;
	};
	
	public ref class Geometry : System::IEquatable<Geometry^>
	{
	public:

		explicit Geometry(class DatasmithNavisworksUtilImpl::FGeometry* Handle);
		~Geometry();
		!Geometry();

		bool Equals(Object^ Obj) override
		{
			return Equals(dynamic_cast<Geometry^>(Obj));
		}
		virtual bool Equals(Geometry^ Other);
		virtual int GetHashCode() override;
		void Update();

		void Optimize();

		bool Append(Geometry^ Other);
		
		uint32_t VertexCount;
		double* Coords;
		double* Normals;
		double* UVs;

		uint32_t TriangleCount;
		uint32_t* Indices;

		/**
		 * \brief Allocate empty geometry, empty but with space reserved for known vertex/triangle counts
		 */
		static Geometry^ ReserveGeometry(uint32_t VertexCount, uint32_t TriangleCount);
	private:
		DatasmithNavisworksUtilImpl::FGeometry* Handle; // Handle to the native class holding data, so it can be released on time
		bool bIsDisposed;
		
		uint64_t Hash;
	};
	
	public ref class TriangleReader
	{
	public:
		static Geometry^ ReadGeometry(Autodesk::Navisworks::Api::Interop::ComApi::InwOaFragment3^ Fragment, GeometrySettings^ Params);
	};
}
