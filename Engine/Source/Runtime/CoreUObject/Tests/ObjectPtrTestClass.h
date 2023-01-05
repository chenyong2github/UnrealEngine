// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#if WITH_LOW_LEVEL_TESTS

#include "UObject/Object.h"


//simple test class for testing TObjectPtr resolve behavior
class UObjectPtrTestClass : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrTestClass, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:

};



//derived test class
class UObjectPtrDerrivedTestClass : public UObjectPtrTestClass
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrDerrivedTestClass, UObjectPtrTestClass, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:

};


//non lazy test class
class UObjectPtrNotLazyTestClass : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrNotLazyTestClass, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:

};


#endif