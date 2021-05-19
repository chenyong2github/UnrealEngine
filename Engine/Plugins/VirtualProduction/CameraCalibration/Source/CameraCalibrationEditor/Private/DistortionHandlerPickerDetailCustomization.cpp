// Copyright Epic Games, Inc. All Rights Reserved.

#include "DistortionHandlerPickerDetailCustomization.h"

#include "CameraCalibrationSubsystem.h"
#include "CameraCalibrationTypes.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPropertyUtilities.h"
#include "Misc/Guid.h"


#define LOCTEXT_NAMESPACE "DistortionHandlerPickerDetailCustomization"


TSharedRef<IPropertyTypeCustomization> FDistortionHandlerPickerDetailCustomization::MakeInstance()
{
	return MakeShared<FDistortionHandlerPickerDetailCustomization>();
}

void FDistortionHandlerPickerDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructPropertyHandle = InPropertyHandle;
	TSharedPtr<IPropertyUtilities> PropertyUtils = CustomizationUtils.GetPropertyUtilities();
	
	StructPropertyHandle->MarkResetToDefaultCustomized();

	TArray<void*> RawData;
	StructPropertyHandle->AccessRawData(RawData);

	// Multi-select is not supported for this property
	if (RawData.Num() == 1)
	{
		FDistortionHandlerPicker* DistortionHandlerPicker = reinterpret_cast<FDistortionHandlerPicker*>(RawData[0]);

		HeaderRow.NameContent()
			[
				StructPropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				BuildDistortionHandlerPickerWidget(DistortionHandlerPicker)
			]
			.IsEnabled(MakeAttributeLambda([=] { return !InPropertyHandle->IsEditConst() && PropertyUtils->IsPropertyEditingEnabled(); }));
	}
}

TSharedRef<SWidget> FDistortionHandlerPickerDetailCustomization::BuildDistortionHandlerPickerWidget(FDistortionHandlerPicker* InDistortionHandlerPicker)
{
	TSharedRef<SWidget> ValueWidget =
		SNew(SHorizontalBox)
	 	+ SHorizontalBox::Slot()
 		.AutoWidth()
 		.Padding(FMargin(0.0f, 0.0f, 0.0f, 0.0f))
 		[
 			SNew(SComboButton)
 			.OnGetMenuContent(this, &FDistortionHandlerPickerDetailCustomization::PopulateDistortionHandlerComboButton, InDistortionHandlerPicker)
	 		.ContentPadding(FMargin(4.0, 2.0))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FDistortionHandlerPickerDetailCustomization::OnGetButtonText, InDistortionHandlerPicker)
			]
		]
	;

	return ValueWidget;
}

FText FDistortionHandlerPickerDetailCustomization::OnGetButtonText(FDistortionHandlerPicker* InDistortionHandlerPicker) const
{
	if (InDistortionHandlerPicker->HandlerDisplayName.IsEmpty())
	{
		return FText::FromName(NAME_None);
	}
	return FText::FromString(InDistortionHandlerPicker->HandlerDisplayName);
}

TSharedRef<SWidget> FDistortionHandlerPickerDetailCustomization::PopulateDistortionHandlerComboButton(FDistortionHandlerPicker* InDistortionHandlerPicker) const
{
	// Generate menu
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.BeginSection("DistortionHandlers", LOCTEXT("DistortionHandlers", "Distortion Sources For Target Camera"));
	{
		TArray<ULensDistortionModelHandlerBase*> Handlers;
		UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
		if (SubSystem)
		{
			Handlers = SubSystem->GetDistortionModelHandlers(InDistortionHandlerPicker->TargetCameraComponent);
		}

		if (Handlers.Num() == 0)
		{
			ULensDistortionModelHandlerBase* NullHandler = nullptr;
			FString EmptyString;

			//Always add a None entry
			MenuBuilder.AddMenuEntry(
				FText::FromName(NAME_None),
				FText::FromName(NAME_None),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FDistortionHandlerPickerDetailCustomization::OnDistortionHandlerSelected, InDistortionHandlerPicker, NullHandler),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &FDistortionHandlerPickerDetailCustomization::IsDistortionHandlerSelected, InDistortionHandlerPicker, EmptyString)
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}

		for (ULensDistortionModelHandlerBase* Handler : Handlers)
		{
			MenuBuilder.AddMenuEntry(
				FText::Format(LOCTEXT("DistortionHandlerLabel", "{0}"), FText::FromString(Handler->GetDisplayName())),
				FText::Format(LOCTEXT("DistortionHandlerTooltip", "{0}"), FText::FromString(Handler->GetDisplayName())),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FDistortionHandlerPickerDetailCustomization::OnDistortionHandlerSelected, InDistortionHandlerPicker, Handler),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &FDistortionHandlerPickerDetailCustomization::IsDistortionHandlerSelected, InDistortionHandlerPicker, Handler->GetDisplayName())
				),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FDistortionHandlerPickerDetailCustomization::OnDistortionHandlerSelected(FDistortionHandlerPicker* InDistortionHandlerPicker, ULensDistortionModelHandlerBase* InHandler) const
{
	const FScopedTransaction Transaction(LOCTEXT("SetDistortionHandler", "Set Distortion Source"));

	TArray<UObject*> OuterObjects;
	StructPropertyHandle->GetOuterObjects(OuterObjects);
	for (UObject* Outer : OuterObjects)
	{
		Outer->Modify();
	}

	FGuid DistortionProducerID;
	FString HandlerDisplayName;

	if (InHandler)
	{
		DistortionProducerID = InHandler->GetDistortionProducerID();
		HandlerDisplayName = InHandler->GetDisplayName();
	}

	InDistortionHandlerPicker->DistortionProducerID = DistortionProducerID;
	InDistortionHandlerPicker->HandlerDisplayName = HandlerDisplayName;

	StructPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

bool FDistortionHandlerPickerDetailCustomization::IsDistortionHandlerSelected(FDistortionHandlerPicker* InDistortionHandlerPicker, FString InDisplayName) const
{
	return (InDistortionHandlerPicker->HandlerDisplayName.Equals(InDisplayName));
}

#undef LOCTEXT_NAMESPACE