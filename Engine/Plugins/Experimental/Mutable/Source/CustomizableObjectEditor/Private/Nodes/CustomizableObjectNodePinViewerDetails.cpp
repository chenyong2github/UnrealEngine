// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectNodePinViewerDetails.h"

#include "DetailLayoutBuilder.h"

#include "PinViewer/SPinViewer.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


TSharedRef<IDetailCustomization> FCustomizableObjectNodePinViewerDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableObjectNodePinViewerDetails);
}


void FCustomizableObjectNodePinViewerDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	PinViewerAttachToDetailCustomization(DetailBuilder);
}

#undef LOCTEXT_NAMESPACE