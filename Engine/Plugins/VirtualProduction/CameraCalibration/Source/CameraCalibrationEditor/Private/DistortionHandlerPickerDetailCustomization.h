// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

#include "CameraCalibrationTypes.h"
#include "CoreMinimal.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"

class UCineCameraComponent;
class ULensDistortionModelHandlerBase;

class FDistortionHandlerPickerDetailCustomization : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

public:
	//~ Begin IDetailCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	//~ End IDetailCustomization interface

protected:
	/** Construct the combo button widget to display a dropdown of the distortion sources that exist for the input picker */
	TSharedRef<SWidget> BuildDistortionHandlerPickerWidget(FDistortionHandlerPicker* InDistortionHandlerPicker);

	/** Construct a menu widget of distortion sources that exist for the input picker */
	TSharedRef<SWidget> PopulateDistortionHandlerComboButton(FDistortionHandlerPicker* InDistortionHandlerPicker) const;

	/** Delegate to run when a user makes a selection in the distortion source dropdown */
	void OnDistortionHandlerSelected(FDistortionHandlerPicker* InDistortionHandlerPicker, ULensDistortionModelHandlerBase* InHandler) const;

	/** Delegate to run to determine to determine whether the radio button should be filled in next to each dropdown option */
	bool IsDistortionHandlerSelected(FDistortionHandlerPicker* InDistortionHandlerPicker, FString InDisplayName) const;

	/** Delegate to run to get the text to display on the combo button */
	FText OnGetButtonText(FDistortionHandlerPicker* InDistortionHandlerPicker) const;

protected:
	/** Cached property handle to the struct being customized */
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
};
