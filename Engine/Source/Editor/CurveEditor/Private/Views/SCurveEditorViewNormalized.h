// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SInteractiveCurveEditorView.h"

/**
 * A Normalized curve view supporting one or more curves with their own screen transform that normalizes the vertical curve range to [-1,1]
 */
class SCurveEditorViewNormalized : public SInteractiveCurveEditorView
{
public:

	void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor);

	/** Tools should use vertical snapping since grid lines to snap to will usually be visible */
	virtual bool IsValueSnapEnabled() const override { return true; }

private:

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual void GetGridLinesY(TSharedRef<const FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>* MajorGridLabels) const override;
};
