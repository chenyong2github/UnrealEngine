// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/CurveEditorFilterBase.h"
#include "CurveEditorTypes.h"
#include "CurveEditorEulerFilter.generated.h"

UCLASS(DisplayName = "Euler")
class CURVEEDITOR_API UCurveEditorEulerFilter : public UCurveEditorFilterBase
{
	GENERATED_BODY()
public:
	UCurveEditorEulerFilter(){}
protected:
	virtual void ApplyFilter_Impl(TSharedRef<FCurveEditor> InCurveEditor, const TMap<FCurveModelID, FKeyHandleSet>& InKeysToOperateOn, TMap<FCurveModelID, FKeyHandleSet>& OutKeysToSelect) override;

public:
	
};