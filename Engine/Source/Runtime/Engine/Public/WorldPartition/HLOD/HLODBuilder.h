// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HLODBuilder.generated.h"

class AActor;
class AWorldPartitionHLOD;
class UPrimitiveComponent;
class UHLODLayer;

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogHLODBuilder, Log, All);


// This is the base class for all HLOD Builder settings
UCLASS()
class ENGINE_API UHLODBuilderSettings : public UObject
{
	GENERATED_UCLASS_BODY()
#if WITH_EDITOR
	virtual uint32 GetCRC() const { return 0; }
#endif // WITH_EDITOR
};


/**
 * Base class for all HLODBuilders
 */
UCLASS(Abstract)
class ENGINE_API UHLODBuilder : public UObject
{
	 GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	virtual UHLODBuilderSettings* CreateSettings(UHLODLayer* InHLODLayer) const;

	virtual bool RequiresCompiledAssets() const;

	virtual bool RequiresWarmup() const;

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) const;

	void Build(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<AActor*>& InSubActors);

	static TArray<UPrimitiveComponent*> GatherPrimitiveComponents(const TArray<AActor*>& InActors);
#endif // WITH_EDITOR
};
