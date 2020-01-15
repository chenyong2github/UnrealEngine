// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/CurveEditorFilterBase.h"
#include "CurveEditorReduceFilter.generated.h"

UCLASS(DisplayName="Simplify")
class UCurveEditorReduceFilter : public UCurveEditorFilterBase
{
	GENERATED_BODY()
public:
	UCurveEditorReduceFilter() 
	{
		Tolerance = 0.1f;
	}

protected:
	void ApplyFilter_Impl(TSharedRef<FCurveEditor> InCurveEditor, const TMap<FCurveModelID, FKeyHandleSet>& InKeysToOperateOn, TMap<FCurveModelID, FKeyHandleSet>& OutKeysToSelect) override;

public:
	/** Minimum change in values required for a key to be considered distinct enough to keep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float Tolerance;
};