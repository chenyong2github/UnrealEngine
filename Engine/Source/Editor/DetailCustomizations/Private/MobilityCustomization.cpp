// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/MobilityCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobilityCustomization" 

static FName GetOptionName(EComponentMobility::Type InMobility)
{
	switch (InMobility)
	{
		case EComponentMobility::Static: return FName("Static");
		case EComponentMobility::Stationary: return FName("Stationary");
		case EComponentMobility::Movable: return FName("Movable");
	}

	check(false);
	return NAME_None;
}

static EComponentMobility::Type GetOptionValue(FName InMobilityName)
{
	if (InMobilityName.IsNone())
	{
		return EComponentMobility::Static;
	}

	if (InMobilityName == GetOptionName(EComponentMobility::Static))
	{
		return EComponentMobility::Static;
	}
	else if (InMobilityName == GetOptionName(EComponentMobility::Movable))
	{
		return EComponentMobility::Stationary;
	}
	else if (InMobilityName == GetOptionName(EComponentMobility::Stationary))
	{
		return EComponentMobility::Movable;
	}

	check(false);
	return EComponentMobility::Static;
}

static FText GetOptionText(EComponentMobility::Type InMobility)
{
	switch (InMobility)
	{
	case EComponentMobility::Static:
		return LOCTEXT("Static", "Static");
	case EComponentMobility::Movable:
		return LOCTEXT("Movable", "Movable");
	case EComponentMobility::Stationary:
		return LOCTEXT("Stationary", "Stationary");
	}

	check(false);
	return FText::GetEmpty();
}

static FText GetOptionToolTip(EComponentMobility::Type InMobility, const bool bForLight)
{
	switch (InMobility)
	{
	case EComponentMobility::Static:
		return bForLight
			? LOCTEXT("Mobility_Static_Light_Tooltip", "A static light can't be changed in game.\n* Fully Baked Lighting\n* Fastest Rendering")
			: LOCTEXT("Mobility_Static_Tooltip", "A static object can't be changed in game.\n* Allows Baked Lighting\n* Fastest Rendering");;
	case EComponentMobility::Movable:
		return bForLight
			? LOCTEXT("Mobility_Movable_Light_Tooltip", "Movable lights can be moved and changed in game.\n* Totally Dynamic\n* Whole Scene Dynamic Shadows\n* Slowest Rendering")
			: LOCTEXT("Mobility_Movable_Tooltip", "Movable objects can be moved and changed in game.\n* Totally Dynamic\n* Casts a Dynamic Shadow \n* Slowest Rendering");
	case EComponentMobility::Stationary:
		return bForLight
			? LOCTEXT("Mobility_Stationary_Tooltip", "A stationary light will only have its shadowing and bounced lighting from static geometry baked by Lightmass, all other lighting will be dynamic.  It can change color and intensity in game.\n* Can't Move\n* Allows Partially Baked Lighting\n* Dynamic Shadows from Movable objects")
			: LOCTEXT("Mobility_Stationary_Object_Tooltip", "A stationary object can be changed in game but not moved, and enables cached lighting methods. \n* Cached Dynamic Shadows.");
	}
	check(false);
	return FText::GetEmpty();
}

FMobilityCustomization::FMobilityCustomization(TSharedPtr<IPropertyHandle> InMobilityHandle, uint8 InRestrictedMobilityBits, bool InForLight)
{
	MobilityHandle = InMobilityHandle;
	RestrictedMobilityBits = InRestrictedMobilityBits;
	bForLight = InForLight;
}

FName FMobilityCustomization::GetName() const
{
	const FProperty* Property = MobilityHandle->GetProperty();
	if (Property != nullptr)
	{
		return Property->GetFName();
	}
	return NAME_None;
}

void FMobilityCustomization::GenerateHeaderRowContent(FDetailWidgetRow& WidgetRow)
{
	AllowedOptions.Reset();

	const bool bShowStatic = !(RestrictedMobilityBits & StaticMobilityBitMask);
	if (bShowStatic)
	{
		AllowedOptions.Add(GetOptionName(EComponentMobility::Static));
	}

	const bool bShowStationary = !(RestrictedMobilityBits & StationaryMobilityBitMask);
	if (bShowStationary)
	{
		AllowedOptions.Add(GetOptionName(EComponentMobility::Stationary));
	}

	const bool bShowMovable = !(RestrictedMobilityBits & MovableMobilityBitMask);
	if (bShowMovable)
	{
		AllowedOptions.Add(GetOptionName(EComponentMobility::Movable));
	}

	TSharedRef<SComboBox<FName>> ComboBox =
		SNew(SComboBox<FName>)
		.OptionsSource(&AllowedOptions)
		.InitiallySelectedItem(GetOptionName(GetActiveMobility()))
		.OnSelectionChanged(this, &FMobilityCustomization::OnMobilityChanged)
		.OnGenerateWidget(this, &FMobilityCustomization::OnGenerateWidget)
		.Content()
		[
			SNew(STextBlock)
			.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.MobilityFont"))
			.Text(this, &FMobilityCustomization::GetActiveMobilityText)
			.ToolTipText(this, &FMobilityCustomization::GetActiveMobilityToolTip)
		];
		
	WidgetRow
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("Mobility", "Mobility"))
		.ToolTipText(this, &FMobilityCustomization::GetMobilityToolTip)
	]
	.ValueContent()
	.MaxDesiredWidth(0)
	[
		ComboBox
	];
}

TSharedRef<SWidget> FMobilityCustomization::OnGenerateWidget(const FName InMobilityName) const
{
	const EComponentMobility::Type Mobility = GetOptionValue(InMobilityName);
	const FText Text = GetOptionText(Mobility);
	const FText ToolTip = GetOptionToolTip(Mobility, bForLight);

	return SNew(STextBlock)
		.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.MobilityFont"))
		.Text(Text)
		.ToolTipText(ToolTip);
}

void FMobilityCustomization::OnMobilityChanged(FName InMobilityName, ESelectInfo::Type)
{
	if (MobilityHandle.IsValid() && !InMobilityName.IsNone())
	{
		MobilityHandle->SetValue((uint8) GetOptionValue(InMobilityName));
	}
}

EComponentMobility::Type FMobilityCustomization::GetActiveMobility() const
{
	if (MobilityHandle.IsValid())
	{
		uint8 MobilityByte;
		MobilityHandle->GetValue(MobilityByte);

		return (EComponentMobility::Type) MobilityByte;
	}

	return EComponentMobility::Static;
}

FText FMobilityCustomization::GetActiveMobilityText() const
{
	return GetOptionText(GetActiveMobility());
}

FText FMobilityCustomization::GetActiveMobilityToolTip() const
{
	return GetOptionToolTip(GetActiveMobility(), bForLight);
}

FText FMobilityCustomization::GetMobilityToolTip() const
{
	if (MobilityHandle.IsValid())
	{
		return MobilityHandle->GetToolTipText();
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE

