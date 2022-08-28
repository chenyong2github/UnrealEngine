// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FGLTFObjectArrayScopeGuard : public FGCObject, public TArray<UObject*>
{
public:

	FGLTFObjectArrayScopeGuard()
	{
	}

	using TArray<UObject*>::TArray;

	/** Non-copyable */
	FGLTFObjectArrayScopeGuard(const FGLTFObjectArrayScopeGuard&) = delete;
	FGLTFObjectArrayScopeGuard& operator=(const FGLTFObjectArrayScopeGuard&) = delete;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObjects(*this);
	}
};
