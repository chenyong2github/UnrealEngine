// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IO/IoHash.h"
#include "Templates/TypeCompatibleBytes.h"

namespace UE::DerivedData
{

struct FCacheKey;
struct FCachePayloadKey;

namespace Private
{
	struct FCacheKeyDummy
	{
		const ANSICHAR* BucketNamePtrDummy = nullptr;
		FIoHash HashDummy;
	};

	struct FCachePayloadKeyDummy
	{
		FCacheKeyDummy CacheKeyDummy;
		alignas(uint32) uint8 PayloadBytesDummy[12];
	};
} // Private

struct FCacheKeyProxy : private TAlignedBytes<sizeof(UE::DerivedData::Private::FCacheKeyDummy), alignof(UE::DerivedData::Private::FCacheKeyDummy)>
{
	FCacheKeyProxy(const FCacheKey& InKey);
	~FCacheKeyProxy();
	FCacheKey* AsCacheKey() { return (FCacheKey*)this;  }
	const FCacheKey* AsCacheKey() const { return (const FCacheKey*)this; }
};

struct FCachePayloadKeyProxy : private TAlignedBytes<sizeof(UE::DerivedData::Private::FCachePayloadKeyDummy), alignof(UE::DerivedData::Private::FCachePayloadKeyDummy)>
{
	FCachePayloadKeyProxy(const FCachePayloadKey& InKey);
	~FCachePayloadKeyProxy();
	FCachePayloadKey* AsCachePayloadKey() { return (FCachePayloadKey*)this;  }
	const FCachePayloadKey* AsCachePayloadKey() const { return (const FCachePayloadKey*)this; }
};

} // UE::DerivedData
