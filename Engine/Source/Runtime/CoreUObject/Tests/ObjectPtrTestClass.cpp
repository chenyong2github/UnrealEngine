// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "ObjectPtrTestClass.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrTestClass, UObject,
	{
		auto MetaData = Class->GetOutermost()->GetMetaData();
		if (MetaData)
		{
			MetaData->SetValue(Class, TEXT("LoadBehavior"), TEXT("LazyOnDemand"));
		}
	}
);

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrDerrivedTestClass, UObjectPtrTestClass, {});

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPtrNotLazyTestClass, UObject, {});

#endif