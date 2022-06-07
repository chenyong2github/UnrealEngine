// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FDatasmithMesh;

class DATASMITHCORE_API FDatasmithClothPattern
{
public:
	TArray<FVector2f> SimPosition;
	TArray<FVector3f> SimRestPosition;
	TArray<uint32> SimTriangleIndices;

public:
	bool IsValid() { return SimRestPosition.Num() == SimPosition.Num() && SimTriangleIndices.Num() % 3 == 0; }
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

