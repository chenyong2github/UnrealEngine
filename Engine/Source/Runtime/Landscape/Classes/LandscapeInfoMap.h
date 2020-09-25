// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "Misc/Guid.h"

#include "LandscapeInfoMap.generated.h"

class ULandscapeInfo;

UCLASS()
class ULandscapeInfoMap : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	void Serialize(FArchive& Ar) override;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	TMap<FGuid, ULandscapeInfo*> Map;
};
