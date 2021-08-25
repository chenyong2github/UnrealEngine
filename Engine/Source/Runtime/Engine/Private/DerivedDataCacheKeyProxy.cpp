// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheKeyProxy.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataCacheKey.h"

namespace UE::DerivedData
{

static_assert(sizeof(FCacheKeyProxy) == sizeof(FCacheKeyProxy));
static_assert(alignof(FCacheKeyProxy) == alignof(FCacheKeyProxy));

static_assert(sizeof(FCachePayloadKeyProxy) == sizeof(FCachePayloadKey));
static_assert(alignof(FCachePayloadKeyProxy) == alignof(FCachePayloadKey));

FCacheKeyProxy::FCacheKeyProxy(const FCacheKey& InKey)
{
	new(AsCacheKey()) FCacheKey(InKey);
}

FCacheKeyProxy::~FCacheKeyProxy()
{
	AsCacheKey()->~FCacheKey();
}

FCachePayloadKeyProxy::FCachePayloadKeyProxy(const FCachePayloadKey& InKey)
{
	new(AsCachePayloadKey()) FCachePayloadKey(InKey);
}

FCachePayloadKeyProxy::~FCachePayloadKeyProxy()
{
	AsCachePayloadKey()->~FCachePayloadKey();
}

}

#endif // WITH_EDITORONLY_DATA