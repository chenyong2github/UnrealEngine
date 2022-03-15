// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#include "WorldPartition/DataLayer/DataLayerUtils.h" 

#include "DataLayerAsset.generated.h"

UCLASS(editinlinenew)
class ENGINE_API UDataLayerAsset : public UObject
{
	GENERATED_UCLASS_BODY()

	friend class UDataLayerConversionInfo;

public:
	virtual void PostLoad() override;

#if WITH_EDITOR
	void SetType(EDataLayerType Type) { DataLayerType = Type; }
#endif

	UFUNCTION(Category = "Data Layer", BlueprintCallable)
	EDataLayerType GetType() const { return DataLayerType; }

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	bool IsRuntime() const { return DataLayerType == EDataLayerType::Runtime; }

	UFUNCTION(Category = "Data Layer|Runtime", BlueprintCallable)
	FColor GetDebugColor() const { return DebugColor; }

private:
	/** Whether the Data Layer affects actor runtime loading */
	UPROPERTY(Category = "Data Layer", EditAnywhere)
	EDataLayerType DataLayerType;

	UPROPERTY(Category = "Data Layer|Runtime", EditAnywhere, meta = (EditConditionHides, EditCondition = "DataLayerType == EDataLayerType::Runtime"))
	FColor DebugColor;
};