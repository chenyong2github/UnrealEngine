// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimBlueprintClassSubsystem.h"
#include "IPropertyAccess.h"
#include "PropertyAccess.h"

#include "AnimBlueprintClassSubsystem_PropertyAccess.generated.h"

UCLASS()
class PROPERTYACCESS_API UAnimBlueprintClassSubsystem_PropertyAccess : public UAnimBlueprintClassSubsystem, public IPropertyAccess
{
	GENERATED_BODY()

public:
	// UAnimBlueprintClassSubsystem interface
	virtual void OnUpdateAnimation(UAnimInstance* InAnimInstance, FAnimInstanceSubsystemData& InSubsystemData, float InDeltaTime) override;
	virtual void OnParallelUpdateAnimation(FAnimInstanceProxy& InProxy, FAnimInstanceSubsystemData& InSubsystemData, float InDeltaTime) override;
	virtual void PostLoadSubsystem() override;

	// IPropertyAccess interface
	virtual void ProcessCopies(UObject* InObject, EPropertyAccessCopyBatch InBatchType) const override;
	virtual void ProcessCopy(UObject* InObject, EPropertyAccessCopyBatch InBatchType, int32 InCopyIndex, TFunctionRef<void(const FProperty*, void*)> InPostCopyOperation) const override;
	virtual void BindEvents(UObject* InObject) const override;
	virtual int32 GetEventId(const UClass* InClass, TArrayView<const FName> InPath) const override;

	// Get the library that holds property access data
	FPropertyAccessLibrary& GetLibrary() { return PropertyAccessLibrary; }

private:
	// The library holding the property access data
	UPROPERTY()
	FPropertyAccessLibrary PropertyAccessLibrary;
};