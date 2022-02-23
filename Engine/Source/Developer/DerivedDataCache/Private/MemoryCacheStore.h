// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataLegacyCacheStore.h"

namespace UE::DerivedData
{

class IMemoryCacheStore : public ILegacyCacheStore
{
public:
	using ILegacyCacheStore::LegacyDelete;

	virtual void Delete(const FCacheKey& Key) = 0;
	virtual void DeleteValue(const FCacheKey& Key) = 0;
	virtual void LegacyDelete(const FLegacyCacheKey& Key) = 0;

	virtual void Disable() = 0;
};

} // UE::DerivedData
