// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "LensDataCurveModel.h"


// Forward Declarations
class ULensFile;

/** Type of supported encoders */
enum class EEncoderType : uint8
{
	Focus,
	Iris
};

/**
 * Handles curves associated to encoder mapping
 */
class FLensEncodersCurveModel : public FLensDataCurveModel
{
public:

	FLensEncodersCurveModel(ULensFile* InOwner, EEncoderType InType);

	//~ Begin FRichCurveEditorModel
	virtual void SetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyAttributes> InAttributes, EPropertyChangeType::Type ChangeType = EPropertyChangeType::Unspecified) override;
	//~ End FRichCurveEditorModel

	
protected:
	void OnCurveModifiedCallback() const;

private:

	/** Type of encoder for which to display curve */
	EEncoderType EncoderType = EEncoderType::Focus;
};
