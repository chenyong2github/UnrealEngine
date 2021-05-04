// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/CommonGenericInputActionDataTable.h"
#include "DataDrivenInputActionProcessor.generated.h"

class UCommonGenericInputActionDataTable;

struct FGenericDataDrivenInputInfo
{
	bool bSwapForwardAndBackButtons = false;
};

/**
 * Derive from to process common input action datatable
 */
UCLASS(Transient)
class COMMONUI_API UDataDrivenInputActionProcessor : public UCommonInputActionDataProcessor
{
	GENERATED_BODY()

public:
	virtual void ProcessInputActions(UCommonGenericInputActionDataTable* InputActionDataTable) override;

private:
	static const TMap<FName, FGenericDataDrivenInputInfo> GetDataDrivenInputInfo();
};