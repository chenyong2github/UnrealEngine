// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "UObject/ObjectMacros.h"
#include "Components/SceneComponent.h"
#include "Serialization/BulkData.h"
#include "Components/SkyAtmosphereComponent.h"
#include "AtmosphericFogComponent.generated.h"

/**
 *	Used to create fogging effects such as clouds.
 */
UCLASS(ClassGroup=Rendering, collapsecategories, hidecategories=(Object, Mobility, Activation, "Components|Activation"), editinlinenew, meta=(BlueprintSpawnableComponent), MinimalAPI, notplaceable)
class UE_DEPRECATED(4.26, "Please use the SkyAtmosphere component instead.") UAtmosphericFogComponent : public USkyAtmosphereComponent
{
	GENERATED_UCLASS_BODY()

	~UAtmosphericFogComponent();

protected:

public:

protected:

public:
	
	//~ Begin UObject Interface. 
	virtual bool IsPostLoadThreadSafe() const override;

	virtual void PostInterpChange(FProperty* PropertyThatChanged) override;
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface;

private:

};
