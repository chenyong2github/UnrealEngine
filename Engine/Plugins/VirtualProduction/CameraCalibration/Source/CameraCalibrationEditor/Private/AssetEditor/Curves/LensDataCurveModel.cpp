// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDataCurveModel.h"

#include "LensFile.h"



FLensDataCurveModel::FLensDataCurveModel(ULensFile* InOwner)
	: FRichCurveEditorModel(InOwner)
	, LensFile(InOwner)
{
	check(InOwner);
}

void FLensDataCurveModel::AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles)
{
	//Don't support adding keys from curve editor by default. Specific models can override
}

void FLensDataCurveModel::RemoveKeys(TArrayView<const FKeyHandle> InKeys)
{
	//Don't support removing keys from curve editor by default. Specific models can override
}

void FLensDataCurveModel::SetKeyAttributes(TArrayView<const FKeyHandle> InKeys,	TArrayView<const FKeyAttributes> InAttributes, EPropertyChangeType::Type ChangeType)
{
	//Don't support changing interp attributes from curve editor by default. Specific models can override
}

bool FLensDataCurveModel::IsValid() const
{
	return bIsCurveValid;
}

FRichCurve& FLensDataCurveModel::GetRichCurve()
{
	return CurrentCurve;
}

const FRichCurve& FLensDataCurveModel::GetReadOnlyRichCurve() const
{
	return CurrentCurve;
}

