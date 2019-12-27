// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/CurveEditorFilterBase.h"
#include "CurveEditorTypes.h"
#include "CurveEditorBakeFilter.generated.h"

UCLASS(DisplayName="Bake")
class CURVEEDITOR_API UCurveEditorBakeFilter : public UCurveEditorFilterBase
{
	GENERATED_BODY()
public:
	UCurveEditorBakeFilter()
	{
		bUseSnapRateForInterval = true;
		BakeInterval = 0.1f;
	}
protected:
	virtual void ApplyFilter_Impl(TSharedRef<FCurveEditor> InCurveEditor, const TMap<FCurveModelID, FKeyHandleSet>& InKeysToOperateOn, TMap<FCurveModelID, FKeyHandleSet>& OutKeysToSelect) override;

public:
	/** If true we will use the snap rate of the Curve Editor to determine how far apart keys should be when baking. If false, the interval is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	bool bUseSnapRateForInterval;

	/** The interval (in seconds) between baked keys. Only used if bUseSnapRateForInterval is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "!bUseSnapRateForInterval", UIMin = 0, UIMax=1), Category = "Settings")
	float BakeInterval;
};