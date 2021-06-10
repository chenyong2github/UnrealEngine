// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CurveModel.h"
#include "Curves/RichCurve.h"
#include "RichCurveEditorModel.h"
#include "UObject/StrongObjectPtr.h"


// Forward Declarations
class ULensFile;


/**
 * Base class to handle displaying curves for various lens data types
 */
class FLensDataCurveModel : public FRichCurveEditorModel
{
public:

	FLensDataCurveModel(ULensFile* InOwner);

	//~ Begin FRichCurveEditorModel
	virtual void AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles) override;
	virtual void RemoveKeys(TArrayView<const FKeyHandle> InKeys) override;
	virtual void SetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyAttributes> InAttributes, EPropertyChangeType::Type ChangeType = EPropertyChangeType::Unspecified) override;
	virtual bool IsValid() const override;
	virtual FRichCurve& GetRichCurve() override;
	virtual const FRichCurve& GetReadOnlyRichCurve() const override;
	//~ End FRichCurveEditorModel

protected:

	/** LensFile we are operating on */
	TStrongObjectPtr<ULensFile> LensFile;

	/** Active curve pointer */
	FRichCurve CurrentCurve;

	/** Wheter a valid curve was built from lens data */
	bool bIsCurveValid = false;
};
