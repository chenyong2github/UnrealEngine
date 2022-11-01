// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Volume.h"

#include "PCGVolume.generated.h"

class UPCGComponent;

UCLASS(BlueprintType)
class PCG_API APCGVolume : public AVolume
{
	GENERATED_BODY()

public:
	APCGVolume(const FObjectInitializer& ObjectInitializer);

	//~ Begin AActor Interface
#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif // WITH_EDITOR
	//~ End AActor Interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = PCG)
	TObjectPtr<UPCGComponent> PCGComponent;
};
