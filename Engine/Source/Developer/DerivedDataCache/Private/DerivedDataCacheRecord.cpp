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
	check(Value);
	Package.Reset();
	Binary = MoveTemp(Value);
	BinaryHash = Binary.GetSize() ? FIoHash::HashBuffer(Binary) : FIoHash();
	Type = ECacheRecordType::Binary;
}

void FCacheRecord::SetBinary(FSharedBuffer Value, const FIoHash& ValueHash)
{
	check(Value);
	if (Value.GetSize())
	{
		checkSlow(ValueHash == FIoHash::HashBuffer(Value));
	}
	else
	{
		checkfSlow(ValueHash.IsZero(), TEXT("A null or empty buffer must use a hash of zero."));
	}
	Package.Reset();
	Binary = MoveTemp(Value);
	BinaryHash = ValueHash;
	Type = ECacheRecordType::Binary;
}

void FCacheRecord::SetObject(FCbObjectRef Value)
{
	Binary.Reset();
	BinaryHash.Reset();
	Package = FCbPackage(MoveTemp(Value));
	Type = ECacheRecordType::Object;
}

void FCacheRecord::SetObject(FCbObjectRef Value, const FIoHash& ValueHash)
{
	Binary.Reset();
	BinaryHash.Reset();
	Package = FCbPackage(MoveTemp(Value), ValueHash);
	Type = ECacheRecordType::Object;
}

void FCacheRecord::SetPackage(FCbPackage Value)
{
	Binary.Reset();
	BinaryHash.Reset();
	Package = MoveTemp(Value);
	Type = ECacheRecordType::Package;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE
