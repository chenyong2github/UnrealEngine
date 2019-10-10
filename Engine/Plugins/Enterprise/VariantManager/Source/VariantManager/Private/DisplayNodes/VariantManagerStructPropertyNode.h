// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/VariantManagerPropertyNode.h"

class SVariantManagerTableRow;
class UPropertyValue;
class UNumericProperty;

class FVariantManagerStructPropertyNode
	: public FVariantManagerPropertyNode
{
public:

	FVariantManagerStructPropertyNode(TArray<UPropertyValue*> InPropertyValues, TWeakPtr<FVariantManager> InVariantManager);

protected:

	virtual TSharedPtr<SWidget> GetPropertyValueWidget() override;

private:

	void OnFloatPropCommitted(double InValue, ETextCommit::Type InCommitType, UNumericProperty* Prop, int32 Offset);
	void OnSignedPropCommitted(int64 InValue, ETextCommit::Type InCommitType, UNumericProperty* Prop, int32 Offset);
	void OnUnsignedPropCommitted(uint64 InValue, ETextCommit::Type InCommitType, UNumericProperty* Prop, int32 Offset);

	TOptional<double> GetFloatValueFromPropertyValue(UNumericProperty* Prop, int32 Offset) const;
	TOptional<int64> GetSignedValueFromPropertyValue(UNumericProperty* Prop, int32 Offset) const;
	TOptional<uint64> GetUnsignedValueFromPropertyValue(UNumericProperty* Prop, int32 Offset) const;

	TOptional<double> GetFloatValueFromCache(UNumericProperty* Prop) const;
	TOptional<int64> GetSignedValueFromCache(UNumericProperty* Prop) const;
	TOptional<uint64> GetUnsignedValueFromCache(UNumericProperty* Prop) const;

	void OnBeginSliderMovement(UNumericProperty* Prop);

	void OnFloatEndSliderMovement(double LastValue, UNumericProperty* Prop, int32 Offset);
	void OnSignedEndSliderMovement(int64 LastValue, UNumericProperty* Prop, int32 Offset);
	void OnUnsignedEndSliderMovement(uint64 LastValue, UNumericProperty* Prop, int32 Offset);

	void OnFloatValueChanged(double NewValue, UNumericProperty* Prop);
	void OnSignedValueChanged(int64 NewValue, UNumericProperty* Prop);
	void OnUnsignedValueChanged(uint64 NewValue, UNumericProperty* Prop);

	template <typename F>
	TSharedRef<SWidget> GenerateFloatEntryBox(UNumericProperty* Prop, int32 Offset);
	template <typename S>
	TSharedRef<SWidget> GenerateSignedEntryBox(UNumericProperty* Prop, int32 Offset);
	template <typename U>
	TSharedRef<SWidget> GenerateUnsignedEntryBox(UNumericProperty* Prop, int32 Offset);

	bool bIsUsingSlider = false;

	// Cached values to be used for GetXValue and OnXValueChanged
	TMap<UNumericProperty*, TOptional<double>> FloatValues;
	TMap<UNumericProperty*, TOptional<int64>> SignedValues;
	TMap<UNumericProperty*, TOptional<uint64>> UnsignedValues;
};
