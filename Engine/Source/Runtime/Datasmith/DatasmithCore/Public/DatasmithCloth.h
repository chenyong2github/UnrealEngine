// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
// #include "Templates/PimplPtr.h"
// #include "Containers/Array.h"

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
};

