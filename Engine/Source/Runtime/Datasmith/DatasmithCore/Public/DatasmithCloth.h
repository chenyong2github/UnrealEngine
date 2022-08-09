// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "Math/UnrealMathSSE.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "UObject/NameTypes.h"

class FDatasmithMesh;

class DATASMITHCORE_API FDatasmithClothPattern
{
public:
	TArray<FVector2f> SimPosition;
	TArray<FVector3f> SimRestPosition;
	TArray<uint32> SimTriangleIndices;

public:
	bool IsValid() { return SimRestPosition.Num() == SimPosition.Num() && SimTriangleIndices.Num() % 3 == 0 && SimTriangleIndices.Num(); }
};


class FDatasmithClothPresetProperty
{
public:
	FName Name;
	double Value;
};

class DATASMITHCORE_API FDatasmithClothPresetPropertySet
{
public:
	FString SetName;
	TArray<FDatasmithClothPresetProperty> Properties;
};

/*
 * Structure of a Cloth
 * - One Thin Mesh:
 * - base for physics simulation
 *
 * - One Thick Mesh
 * - aka. render mesh
 */
class DATASMITHCORE_API FDatasmithCloth
{
public:
	TArray<FDatasmithClothPattern> Patterns;
	TArray<FDatasmithClothPresetPropertySet> PropertySets;
};

