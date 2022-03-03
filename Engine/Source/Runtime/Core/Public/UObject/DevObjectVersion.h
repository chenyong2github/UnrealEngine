// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "UObject/NameTypes.h"
#include "Misc/Guid.h"
#include "Serialization/CustomVersion.h"

struct CORE_API FDevSystemGuidRegistration
{
public:
	FDevSystemGuidRegistration(const TMap<FGuid, FGuid>& SystemGuids);
};

struct CORE_API FDevSystemGuids
{
	static FGuid GetSystemGuid(FGuid System);

	static const FGuid GLOBALSHADERMAP_DERIVEDDATA_VER;
	static const FGuid MATERIALSHADERMAP_DERIVEDDATA_VER;
	static const FGuid NIAGARASHADERMAP_DERIVEDDATA_VER;
	static const FGuid Niagara_LatestScriptCompileVersion;
	static const FGuid SkeletalMeshDerivedDataVersion;
};

class CORE_API FDevVersionRegistration :  public FCustomVersionRegistration
{
public:
	/** @param InFriendlyName must be a string literal */
	template<int N>
	FDevVersionRegistration(FGuid InKey, int32 Version, const TCHAR(&InFriendlyName)[N], CustomVersionValidatorFunc InValidatorFunc = nullptr)
		: FCustomVersionRegistration(InKey, Version, InFriendlyName, InValidatorFunc)
	{
		RecordDevVersion(InKey);
	}

	/** Dumps all registered versions to log */
	static void DumpVersionsToLog();
private:
	static void RecordDevVersion(FGuid Key);
};