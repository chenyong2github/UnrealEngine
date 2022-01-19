// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#include "DataLayerAsset.generated.h"

UENUM()
enum class EDataLayerType : uint8
{
	Runtime,
	Editor
};

UCLASS(editinlinenew)
class ENGINE_API UDataLayerAsset : public UObject
{
	GENERATED_UCLASS_BODY()

	friend class UDataLayerConversionInfo;

public:
	virtual void PostLoad() override;

#if WITH_EDITOR
	const TCHAR* GetDataLayerIconName() const;
#endif

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