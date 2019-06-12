// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Field/FieldSystem.h"
#include "Math/Vector.h"

namespace Field
{
	// Basic scalar implementation of Perlin's improved noise, copied from VectorVM to remove the engine dependency.
	// http://mrl.nyu.edu/~perlin/noise/
	namespace PerlinNoise
	{
		void FIELDSYSTEMCORE_API Sample(float* Dst, float X, float Y, float Z);
	}
}