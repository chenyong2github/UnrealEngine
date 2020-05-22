// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithNavisworksUtil.h"

#include "TriangleReaderNative.h"

DatasmithNavisworksUtil::Geometry::Geometry(DatasmithNavisworksUtilImpl::TriangleReaderNative* Reader)
	: Handle(Reader)
	, bIsDisposed(false)
{
	VertexCount = Reader->VertexCount;
	Coords = Reader->Coords.data();
	Normals = Reader->Normals.data();
	UVs = Reader->UVs.data();

	TriangleCount = Reader->TriangleCount;
	Indices = Reader->Indices.data();

}

DatasmithNavisworksUtil::Geometry::~Geometry()
{
	if (bIsDisposed)
	{
		return;
	}
	this->!Geometry();
	bIsDisposed = true;
}

void DatasmithNavisworksUtil::Geometry::!Geometry()
{
	delete reinterpret_cast<DatasmithNavisworksUtilImpl::TriangleReaderNative*>(Handle);
}

DatasmithNavisworksUtil::Geometry^ DatasmithNavisworksUtil::TriangleReader::ReadGeometry(Autodesk::Navisworks::Api::Interop::ComApi::InwOaFragment3^ Fragment)
{
	DatasmithNavisworksUtilImpl::TriangleReaderNative* Reader = new DatasmithNavisworksUtilImpl::TriangleReaderNative;
	Reader->Read(System::Runtime::InteropServices::Marshal::GetIUnknownForObject(Fragment).ToPointer());
	return gcnew Geometry(Reader);
}
