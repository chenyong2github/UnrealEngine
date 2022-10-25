// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOColorSpaceConversionCustomization.h"

#include "Containers/StringConv.h"
#include "DetailWidgetRow.h"
#include "Engine/EngineTypes.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "OpenColorIOColorSpace.h"
#include "OpenColorIOConfiguration.h"
#include "PropertyHandle.h"
#include "Widgets/SOpenColorIOColorSpacePicker.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OpenColorIOColorSpaceConversionCustomization"

void FOpenColorIOColorConversionSettingsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (InPropertyHandle->GetNumPerObjectValues() == 1 && InPropertyHandle->IsValidHandle())
	{
		void* StructData = nullptr;
		if (InPropertyHandle->GetValueData(StructData) == FPropertyAccess::Success)
		{
			ColorSpaceConversion = reinterpret_cast<FOpenColorIOColorConversionSettings*>(StructData);
			check(ColorSpaceConversion);

			TSharedPtr<IPropertyUtilities> PropertyUtils = CustomizationUtils.GetPropertyUtilities();

			HeaderRow.NameContent()
				[
					InPropertyHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MaxDesiredWidth(512)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(MakeAttributeLambda([=]
							{
								ColorSpaceConversion->ValidateColorSpaces();

								if (ColorSpaceConversion->IsValid())
								{
									return FText::FromString(*ColorSpaceConversion->ToString());
								}

								return FText::FromString(TEXT("<Invalid Conversion>"));

							}))
					]
				].IsEnabled(MakeAttributeLambda([=] { return !InPropertyHandle->IsEditConst() && PropertyUtils->IsPropertyEditingEnabled(); }));
		}
	}
}

namespace
{
	void UpdateColorSettingsStructProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle, const void* Value, const void* Defaults)
	{
		if (PropertyHandle.IsValid())
		{
			if (FStructProperty* StructProperty = CastField<FStructProperty>(PropertyHandle->GetProperty()))
			{
				FString TextValue;
				StructProperty->Struct->ExportText(TextValue, Value, Defaults, nullptr, EPropertyPortFlags::PPF_None, nullptr);

				// Note: using set value is preferable for built-in change propagation
				PropertyHandle->SetValueFromFormattedString(TextValue);
			}
		}
	}
}

void FOpenColorIOColorConversionSettingsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (ColorSpaceConversion == nullptr)
	{
		return;
	}

	uint32 NumberOfChild;
	if (InStructPropertyHandle->GetNumChildren(NumberOfChild) == FPropertyAccess::Success)
	{
		for (uint32 Index = 0; Index < NumberOfChild; ++Index)
		{
			TSharedRef<IPropertyHandle> ChildHandle = InStructPropertyHandle->GetChildHandle(Index).ToSharedRef();

			if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FOpenColorIOColorConversionSettings, ConfigurationSource))
			{
				StructBuilder.AddProperty(ChildHandle).IsEnabled(true).ShowPropertyButtons(false);

				ChildHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]
					{
						TransformSourcePicker->SetConfiguration(ColorSpaceConversion->ConfigurationSource);
						TransformDestinationPicker->SetConfiguration(ColorSpaceConversion->ConfigurationSource);
					}));
			}
			else if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FOpenColorIOColorConversionSettings, SourceColorSpace))
			{
				SourceColorSpaceProperty = ChildHandle;
			}
			else if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FOpenColorIOColorConversionSettings, DestinationColorSpace))
			{
				DestinationColorSpaceProperty = ChildHandle;
			}
			else if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FOpenColorIOColorConversionSettings, DestinationDisplayView))
			{
				DestinationDisplayViewProperty = ChildHandle;
			}
		}

		// Note: ChildHandle->SetOnPropertyValueChanged isn't automatically called on parent changes, so we explicitely reset the configuration here.
		InStructPropertyHandle->SetOnPropertyResetToDefault(FSimpleDelegate::CreateSP(this, &FOpenColorIOColorConversionSettingsCustomization::OnConfigurationReset));
		if (TSharedPtr<IPropertyHandle> ParentHandle = InStructPropertyHandle->GetParentHandle())
		{
			ParentHandle->SetOnPropertyResetToDefault(FSimpleDelegate::CreateSP(this, &FOpenColorIOColorConversionSettingsCustomization::OnConfigurationReset));
		}

		TransformSourcePicker = SNew(SOpenColorIOColorSpacePicker)
			.Config(ColorSpaceConversion->ConfigurationSource)
			.InitialColorSpace(ColorSpaceConversion->SourceColorSpace)
			.RestrictedColor(ColorSpaceConversion->DestinationColorSpace)
			.InitialDisplayView(FOpenColorIODisplayView())
			.IsDestination(false)
			.OnColorSpaceChanged(FOnColorSpaceChanged::CreateLambda([this](const FOpenColorIOColorSpace& NewColorSpace, const FOpenColorIODisplayView& NewDisplayView)
				{
					check(SourceColorSpaceProperty.IsValid());

					UpdateColorSettingsStructProperty(SourceColorSpaceProperty, &NewColorSpace, &ColorSpaceConversion->SourceColorSpace);
					
					TransformDestinationPicker->SetRestrictedColorSpace(NewColorSpace);
				}));

		TransformDestinationPicker = SNew(SOpenColorIOColorSpacePicker)
			.Config(ColorSpaceConversion->ConfigurationSource)
			.InitialColorSpace(ColorSpaceConversion->DestinationColorSpace)
			.RestrictedColor(ColorSpaceConversion->SourceColorSpace)
			.InitialDisplayView(ColorSpaceConversion->DestinationDisplayView)
			.IsDestination(true)
			.OnColorSpaceChanged(FOnColorSpaceChanged::CreateLambda([this](const FOpenColorIOColorSpace& NewColorSpace, const FOpenColorIODisplayView& NewDisplayView)
				{
					check(DestinationColorSpaceProperty.IsValid());
					check(DestinationDisplayViewProperty.IsValid());

					UpdateColorSettingsStructProperty(DestinationColorSpaceProperty, &NewColorSpace, &ColorSpaceConversion->DestinationColorSpace);
					UpdateColorSettingsStructProperty(DestinationDisplayViewProperty, &NewDisplayView, &ColorSpaceConversion->DestinationDisplayView);
					
					TransformSourcePicker->SetRestrictedColorSpace(NewColorSpace);
				}));

		// Source color space picker widget
		StructBuilder.AddCustomRow(LOCTEXT("TransformSource", "Transform Source"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TransformSource", "Transform Source"))
			.ToolTipText(LOCTEXT("TransformSource_Tooltip", "The source color space used for the transform."))
			.Font(StructCustomizationUtils.GetRegularFont())
		]
		.ValueContent()
		.MaxDesiredWidth(512)
		[
			TransformSourcePicker.ToSharedRef()
		];

		StructBuilder.AddCustomRow(LOCTEXT("TransformDestination", "Transform Destination"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TransformDestination", "Transform Destination"))
			.ToolTipText(LOCTEXT("TransformDestination_Tooltip", "The destination color space used for the transform."))
			.Font(StructCustomizationUtils.GetRegularFont())
		]
		.ValueContent()
		.MaxDesiredWidth(512)
		[
			TransformDestinationPicker.ToSharedRef()
		];
	}

	InStructPropertyHandle->MarkHiddenByCustomization();
}

void FOpenColorIOColorConversionSettingsCustomization::OnConfigurationReset()
{
	TransformSourcePicker->SetConfiguration(nullptr);
	TransformDestinationPicker->SetConfiguration(nullptr);
}

#undef LOCTEXT_NAMESPACE
