// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "FractureSettings.generated.h"

/** Settings specifically related to viewing fractured meshes **/
UCLASS()
class UFractureSettings: public UObject
{

	GENERATED_BODY()
public:
	UFractureSettings(const FObjectInitializer& ObjInit);

	/** How much to seperate the drawing of the bones  */
	UPROPERTY(EditAnywhere, Category = ViewSettings, meta = (DisplayName = "Explode Amount", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float ExplodeAmount;

	/** Current level of the geometry collection displayed  */
	UPROPERTY(EditAnywhere, Category = ViewSettings, meta = (DisplayName = "Fracture Level", UIMin = "-1" ))
	int32 FractureLevel;

};