// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

struct FPart;

class FAvailableExpandsFar final : public TMap<FPart*, float>
{
public:
	void Add(const FPart* const Edge, const float Value)
	{
		TMap<FPart*, float>::Add(const_cast<FPart*>(Edge), Value);
	}
};

class FContourBase : public TArray<FPart*>
{
protected:
	int32 Find(const FPart* const Edge)
	{
		return TArray<FPart*>::Find(const_cast<FPart*>(Edge));
	}
};
