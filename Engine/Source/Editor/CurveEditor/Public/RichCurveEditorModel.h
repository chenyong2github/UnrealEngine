// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "CurveModel.h"
#include "IBufferedCurveModel.h"

struct FRichCurve;

class CURVEEDITOR_API FRichCurveEditorModel : public FCurveModel
{
public:
	FRichCurveEditorModel(FRichCurve* InRichCurve, UObject* InOwner);

	virtual const void* GetCurve() const override;

	virtual void Modify() override;

	virtual void DrawCurve(const FCurveEditor& CurveEditor, const FCurveEditorScreenSpace& ScreenSpace, TArray<TTuple<double, double>>& InterpolatingPoints) const override;
	virtual void GetKeys(const FCurveEditor& CurveEditor, double MinTime, double MaxTime, double MinValue, double MaxValue, TArray<FKeyHandle>& OutKeyHandles) const override;
	virtual void GetKeyDrawInfo(ECurvePointType PointType, const FKeyHandle InKeyHandle, FKeyDrawInfo& OutDrawInfo) const override;

	virtual void GetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyPosition> OutKeyPositions) const override;
	virtual void SetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyPosition> InKeyPositions) override;

	virtual void GetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyAttributes> OutAttributes) const override;
	virtual void SetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyAttributes> InAttributes) override;

	virtual void GetCurveAttributes(FCurveAttributes& OutCurveAttributes) const override;
	virtual void SetCurveAttributes(const FCurveAttributes& InCurveAttributes) override;
	virtual void GetTimeRange(double& MinTime, double& MaxTime) const override;
	virtual void GetValueRange(double& MinValue, double& MaxValue) const override;
	virtual int32 GetNumKeys() const override;
	virtual void GetNeighboringKeys(const FKeyHandle InKeyHandle, TOptional<FKeyHandle>& OutPreviousKeyHandle, TOptional<FKeyHandle>& OutNextKeyHandle) const override;
	virtual bool Evaluate(double ProspectiveTime, double& OutValue) const override;
	virtual void AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles) override;
	virtual void RemoveKeys(TArrayView<const FKeyHandle> InKeys) override;

	virtual void CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects) override;

	virtual TUniquePtr<IBufferedCurveModel> CreateBufferedCurveCopy() const override;

private:

	FRichCurve* RichCurve;

	TWeakObjectPtr<> WeakOwner;
};