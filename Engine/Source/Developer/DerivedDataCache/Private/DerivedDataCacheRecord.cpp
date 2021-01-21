// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecord::FCacheRecord() = default;

void FCacheRecord::Reset()
{
	*this = FCacheRecord();
}

void FCacheRecord::SetBinary(FSharedBuffer Value)
{
	Object.Reset();
	Package.Reset();
	Binary = MoveTemp(Value);
	Type = ECacheRecordType::Binary;
}

void FCacheRecord::SetObject(FCbObjectRef Value)
{
	Binary.Reset();
	Package.Reset();
	Object = MoveTemp(Value);
	Type = ECacheRecordType::Object;
}

void FCacheRecord::SetPackage(FCbPackage Value)
{
	Binary.Reset();
	Object.Reset();
	Package = MoveTemp(Value);
	Type = ECacheRecordType::Package;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE
