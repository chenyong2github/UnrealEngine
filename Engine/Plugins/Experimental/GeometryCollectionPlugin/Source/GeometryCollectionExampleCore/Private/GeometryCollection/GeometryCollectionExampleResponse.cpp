// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleResponse.h"

namespace GeometryCollectionExample
{

	void ExampleResponse::ExpectTrue(bool Condition, FString Reason) 
	{
		ErrorFlag |= !Condition;
		if (!Condition)
		{
			Reasons.Add(Reason);
		}
	}
}