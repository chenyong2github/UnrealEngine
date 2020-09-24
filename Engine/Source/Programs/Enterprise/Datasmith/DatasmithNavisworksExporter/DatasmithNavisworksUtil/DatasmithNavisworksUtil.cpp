// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithNavisworksUtil.h"

#include "TriangleReaderNative.h"

DatasmithNavisworksUtil::GeometrySettings::GeometrySettings()
	: Handle(new DatasmithNavisworksUtilImpl::FGeometrySettings)
	, bIsDisposed(false)
{
}

DatasmithNavisworksUtil::GeometrySettings::~GeometrySettings()
{
	if (bIsDisposed)
	{
		return;
	}
	this->!GeometrySettings();
	bIsDisposed = true;
}

void DatasmithNavisworksUtil::GeometrySettings::!GeometrySettings()
{
	delete Handle;
}

void DatasmithNavisworksUtil::GeometrySettings::SetTriangleSizeThreshold(double Value)
{
	Handle->TriangleSizeThreshold = Value;
}

void DatasmithNavisworksUtil::GeometrySettings::SetPositionThreshold(double Value)
{
	Handle->PositionThreshold = Value;
}

void DatasmithNavisworksUtil::GeometrySettings::SetNormalThreshold(double Value)
{
	Handle->NormalThreshold = Value;
}

DatasmithNavisworksUtil::Geometry::Geometry(DatasmithNavisworksUtilImpl::FGeometry* Reader)
	: Handle(Reader)
	, bIsDisposed(false)
{
	Update();

	Hash = Reader->ComputeHash();
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
	delete Handle;
}

bool DatasmithNavisworksUtil::Geometry::Equals(Geometry^ Other)
{
	return (Handle->VertexCount == Other->Handle->VertexCount)
		&& (Handle->Coords == Other->Handle->Coords)
		&& (Handle->Normals == Other->Handle->Normals)
		&& (Handle->UVs == Other->Handle->UVs)
		&& (Handle->TriangleCount == Other->Handle->TriangleCount)
		&& (Handle->Indices == Other->Handle->Indices)
		;
}

int DatasmithNavisworksUtil::Geometry::GetHashCode()
{
	return Hash % INT_MAX;
}

void DatasmithNavisworksUtil::Geometry::Update()
{
	VertexCount = Handle->VertexCount;
	Coords = Handle->Coords.data();
	Normals = Handle->Normals.data();
	UVs = Handle->UVs.data();

	TriangleCount = Handle->TriangleCount;
	Indices = Handle->Indices.data();
}

void DatasmithNavisworksUtil::Geometry::Optimize()
{
	Handle->Optimize();

	Update();
}

bool DatasmithNavisworksUtil::Geometry::Append(Geometry^ Other)
{
	if (Handle->Coords.size() + Other->Handle->VertexCount * 3 > Handle->Coords.capacity())
	{
		return false;
	}
	if (Handle->Indices.size() + Other->Handle->TriangleCount * 3 > Handle->Indices.capacity())
	{
		return false;
	}

	Handle->Coords.insert(Handle->Coords.end(), Other->Handle->Coords.begin(), Other->Handle->Coords.end());
	Handle->Normals.insert(Handle->Normals.end(), Other->Handle->Normals.begin(), Other->Handle->Normals.end());
	Handle->UVs.insert(Handle->UVs.end(), Other->Handle->UVs.begin(), Other->Handle->UVs.end());

	for (uint32_t Index : Other->Handle->Indices)
	{
		Handle->Indices.push_back(Index + Handle->VertexCount);
	}

	Handle->VertexCount += Other->Handle->VertexCount;
	Handle->TriangleCount += Other->Handle->TriangleCount;

	Update();
	return true;
}

DatasmithNavisworksUtil::Geometry^ DatasmithNavisworksUtil::Geometry::ReserveGeometry(uint32_t VertexCount, uint32_t TriangleCount)
{
	DatasmithNavisworksUtilImpl::FGeometry* Geom = new DatasmithNavisworksUtilImpl::FGeometry;
	Geom->Coords.reserve(VertexCount * 3);
	Geom->Normals.reserve(VertexCount * 3);
	Geom->UVs.reserve(VertexCount * 2);
	Geom->Indices.reserve(TriangleCount * 3);
	return gcnew Geometry(Geom);
}


DatasmithNavisworksUtil::Geometry^ DatasmithNavisworksUtil::TriangleReader::ReadGeometry(Autodesk::Navisworks::Api::Interop::ComApi::InwOaFragment3^ Fragment, GeometrySettings^ Settings)
{
	DatasmithNavisworksUtilImpl::FTriangleReaderNative Reader;
	DatasmithNavisworksUtilImpl::FGeometry* Geom = new DatasmithNavisworksUtilImpl::FGeometry;
	Reader.Read(System::Runtime::InteropServices::Marshal::GetIUnknownForObject(Fragment).ToPointer(), *Geom, *Settings->Handle);
	return gcnew Geometry(Geom);
}

