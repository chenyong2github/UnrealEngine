// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MediaSourceManagerChannel.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MediaSourceManager.generated.h"

class AMediaPlate;
class UMaterialInterface;

/**
* Manager to handle media sources and their connections.
*/
UCLASS(BlueprintType)
class MEDIASOURCEMANAGER_API UMediaSourceManager : public UObject
{
	GENERATED_BODY()

public:

	UMediaSourceManager(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject interface.
	virtual void BeginDestroy() override;
	//~ End UObject interface.

	/** Our channels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channels")
	TArray<TObjectPtr<UMediaSourceManagerChannel>> Channels;

	/**
	 * Call this to make sure everything is set up.
	 */
	void Validate();

private:

	/**
	 * Call this to see if a material is ours and should not be modified.
	 */
	void OnMediaPlateApplyMaterial(UMaterialInterface* Material, AMediaPlate* MediaPlate, bool& bCanModify);

};
