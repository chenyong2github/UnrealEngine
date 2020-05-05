// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

class IHoleFiller
{
public:
	TArray<int32> NewTriangles;

	virtual ~IHoleFiller() = default;

	virtual bool Fill(int32 GroupID = -1) = 0;	
};
