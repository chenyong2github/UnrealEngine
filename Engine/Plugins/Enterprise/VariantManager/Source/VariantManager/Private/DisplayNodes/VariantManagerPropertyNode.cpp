// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayNodes/VariantManagerPropertyNode.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "EditorStyleSet.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "VariantManager.h"
#include "VariantManagerLog.h"
#include "VariantObjectBinding.h"
#include "ISinglePropertyView.h"
#include "PropertyValue.h"
#include "PropertyHandle.h"
#include "PropertyTemplateObject.h"
#include "VariantManagerEditorCommands.h"
#include "PropertyCustomizationHelpers.h"
#include "SVariantManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Input/Reply.h"
#include "Widgets/Images/SImage.h"
#include "EditorFontGlyphs.h"

#define LOCTEXT_NAMESPACE "FVariantManagerPropertyNode"

FVariantManagerPropertyNode::FVariantManagerPropertyNode(TArray<UPropertyValue*> InPropertyValues, TWeakPtr<FVariantManager> InVariantManager)
	: FVariantManagerDisplayNode(nullptr, nullptr)
	, PropertyValues(InPropertyValues)
	, VariantManager(InVariantManager)
{
}

void FVariantManagerPropertyNode::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	FVariantManagerDisplayNode::BuildContextMenu(MenuBuilder);

	MenuBuilder.BeginSection(TEXT("Captured property"), LOCTEXT("CapturedPropertyText", "Captured property"));
	MenuBuilder.AddMenuEntry(FVariantManagerEditorCommands::Get().ApplyProperty);
	MenuBuilder.AddMenuEntry(FVariantManagerEditorCommands::Get().RecordProperty);
	MenuBuilder.AddMenuEntry(FVariantManagerEditorCommands::Get().RemoveCapture);
	MenuBuilder.EndSection();
}

FText FVariantManagerPropertyNode::GetDisplayNameToolTipText() const
{
	if (PropertyValues.Num() < 1 || !PropertyValues[0].IsValid())
	{
		return FText();
	}

	return PropertyValues[0].Get()->GetPropertyTooltip();
}

const FSlateBrush* FVariantManagerPropertyNode::GetIconOverlayBrush() const
{
	return nullptr;
}

FSlateColor FVariantManagerPropertyNode::GetNodeBackgroundTint() const
{
	return FLinearColor(FColor(62, 62, 62, 255));
}

FText FVariantManagerPropertyNode::GetIconToolTipText() const
{
	return FText();
}

EVariantManagerNodeType FVariantManagerPropertyNode::GetType() const
{
	return EVariantManagerNodeType::Property;
}

bool FVariantManagerPropertyNode::IsReadOnly() const
{
	return true;
}

FText FVariantManagerPropertyNode::GetDisplayName() const
{
	if (PropertyValues.Num() < 1 || !PropertyValues[0].IsValid())
	{
		return FText();
	}

	return FText::FromString(PropertyValues[0].Get()->GetFullDisplayString());
}

void FVariantManagerPropertyNode::SetDisplayName(const FText& NewDisplayName)
{
	return;
}

bool FVariantManagerPropertyNode::IsSelectable() const
{
	return true;
}

// Without this, SImages (used for example for the browse and useselected buttons next to
// object property value widgets) will have set SetColorAndOpacity(FSlateColor::UseForeground()).
// That will cause them to automatically go black when the row is selected, which we don't want.
// This unbinds that attribute and just places white as ColorAndOpacity for all SImages
void RecursiveResetColorAndOpacityAttribute(TSharedPtr<SWidget> Widget)
{
	FString TypeString = Widget->GetTypeAsString();
	if (TypeString == TEXT("SImage"))
	{
		if (TSharedPtr<SImage> WidgetAsImage = StaticCastSharedPtr<SImage>(Widget))
		{
			WidgetAsImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f));
		}
	}

	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		RecursiveResetColorAndOpacityAttribute(Children->GetChildAt(Index));
	}
}

// We replace the standard reset button with our own. No easy way to do this other than to dig
// around for it in the widget hierarchy
bool FVariantManagerPropertyNode::RecursiveDisableOldResetButton(TSharedPtr<SWidget> Widget)
{
	FString TypeString = Widget->GetTypeAsString();
	if (TypeString == TEXT("SResetToDefaultPropertyEditor"))
	{
		Widget->SetVisibility(EVisibility::Collapsed);
		return true;
	}

	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (RecursiveDisableOldResetButton(Children->GetChildAt(Index)))
		{
			return true;
		}
	}

	return false;
}

TSharedPtr<SWidget> FVariantManagerPropertyNode::GetPropertyValueWidget()
{
	if (PropertyValues.Num() < 1)
	{
		UE_LOG(LogVariantManager, Error, TEXT("PropertyNode has no UPropertyValues!"));
		return SNew(SBox);
	}

	// Check to see if we have all valid, equal UPropertyValues
	UPropertyValue* FirstPropertyValue = PropertyValues[0].Get();
	uint32 FirstPropHash = FirstPropertyValue->GetPropertyPathHash();
	for (TWeakObjectPtr<UPropertyValue> PropertyValue : PropertyValues)
	{
		if (!PropertyValue.IsValid())
		{
			UE_LOG(LogVariantManager, Error, TEXT("PropertyValue was invalid!"));
			return SNew(SBox);
		}

		if (PropertyValue.Get()->GetPropertyPathHash() != FirstPropHash)
		{
			UE_LOG(LogVariantManager, Error, TEXT("A PropertyNode's PropertyValue array describes properties with different paths!"));
			return SNew(SBox);
		}
	}

	// If all properties fail to resolve, just give back a "Failed to resolve" text block
	bool bAtLeastOneResolved = false;
	bool bSomeFailedToResolve = false;
	for (TWeakObjectPtr<UPropertyValue> WeakPropertyValue : PropertyValues)
	{
		if (WeakPropertyValue.IsValid() && WeakPropertyValue->Resolve())
		{
			if (!WeakPropertyValue->HasRecordedData())
			{
				WeakPropertyValue->RecordDataFromResolvedObject();
			}

			bAtLeastOneResolved = true;
		}
		else
		{
			bSomeFailedToResolve = true;
		}
	}
	if(!bAtLeastOneResolved)
	{
		UObject* ActorAsObj = FirstPropertyValue->GetParent()->GetObject();
		FString ActorName;
		if (AActor* Actor = Cast<AActor>(ActorAsObj))
		{
			ActorName = Actor->GetActorLabel();
		}
		else
		{
			ActorName = ActorAsObj->GetName();
		}

		return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(FMargin(3.0f, 0.0f, 0.0f, 0.0f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FailedToResolveText", "Failed to resolve!"))
			.Font(FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.RegularFont"))
			.ColorAndOpacity(this, &FVariantManagerDisplayNode::GetDisplayNameColor)
			.ToolTipText(FText::Format(
				LOCTEXT("FailedToResolveTooltip", "Make sure actor '{0}' has a property with path '{1}'"),
				FText::FromString(ActorName),
				FText::FromString(FirstPropertyValue->GetFullDisplayString())))
		];
	}

	if (bSomeFailedToResolve)
	{
		UE_LOG(LogVariantManager, Warning, TEXT("Some properties of capture '%s' failed to resolve!"), *GetDisplayName().ToString());
	}

	if (!PropertiesHaveSameValue())
	{
		return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MultipleValuesLabel", "Multiple Values"))
			.Font(FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.RegularFont"))
			.ColorAndOpacity(this, &FVariantManagerDisplayNode::GetDisplayNameColor)
			.ToolTipText(LOCTEXT("MultipleValuesTooltip", "The selected actors have different values for this property"))
		];
	}

	FSinglePropertyParams InitParams;
	InitParams.NamePlacement = EPropertyNamePlacement::Hidden;

	UPropertyTemplateObject* Template = NewObject<UPropertyTemplateObject>(GetTransientPackage());
	SinglePropertyViewTemplate.Reset(Template);

	// Find the property responsible for Template's UObject*
	// Assumes it has only one
	FObjectProperty* TemplateObjectProp = nullptr;
	if (FirstPropertyValue->GetPropertyClass() == FObjectProperty::StaticClass())
	{
		for (TFieldIterator<FObjectProperty> PropertyIterator(Template->GetClass()); PropertyIterator; ++PropertyIterator)
		{
			TemplateObjectProp = *PropertyIterator;
		}
	}

	// HACK to cause the widget to display an FObjectProperty editor restricted to objects of our desired class
	// Note that we undo this right aftewards, so that other property value widgets can do the same to different
	// classes. The template's property itself will then be free to be set with whatever object, but the created
	// widget is already locked in place
	if (TemplateObjectProp)
	{
		TemplateObjectProp->PropertyClass = FirstPropertyValue->GetObjectPropertyObjectClass();
	}

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedPtr<ISinglePropertyView> SinglePropView = PropertyEditorModule.CreateSingleProperty(
		SinglePropertyViewTemplate.Get(),
		UPropertyTemplateObject::GetPropertyNameFromClass(FirstPropertyValue->GetPropertyClass()),
		InitParams);

	if (TemplateObjectProp)
	{
		TemplateObjectProp->PropertyClass = UObject::StaticClass();
	}

	if (!SinglePropView)
	{
		return SNew(SBox)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("UnsupportedPropertyType", "Unsupported property type!"))
			.Font(FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.RegularFont"))
			.ColorAndOpacity(this, &FVariantManagerDisplayNode::GetDisplayNameColor)
			.ToolTipText(FText::Format(
				LOCTEXT("UnsupportedPropertyTypeTooltip", "Properties of class '{0}' can't be captured yet!"),
				FText::FromString(FirstPropertyValue->GetPropertyClass()->GetName())))
		];
	}

	RecursiveResetColorAndOpacityAttribute(SinglePropView);
	RecursiveDisableOldResetButton(SinglePropView);

	TSharedPtr<IPropertyHandle> PropHandle = SinglePropView->GetPropertyHandle();

	// Update widget with recorded data
	TArray<void*> RawDataForEachObject;
	PropHandle->AccessRawData(RawDataForEachObject);
	void* SinglePropWidgetDataPtr = RawDataForEachObject[0]; // We'll always pass just a single object
	const TArray<uint8>& FirstRecordedData = FirstPropertyValue->GetRecordedData();
	FMemory::Memcpy((uint8*)SinglePropWidgetDataPtr, FirstRecordedData.GetData(), FirstPropertyValue->GetValueSizeInBytes());

	// Update recorded data when user modifies the widget (modifying the widget will modify the
	// property value of the object the widget is looking at e.g. the class metadata object)
	PropHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FVariantManagerPropertyNode::UpdateRecordedDataFromSinglePropView, SinglePropView));

	return SinglePropView;
}

TSharedRef<SWidget> FVariantManagerPropertyNode::GetCustomOutlinerContent(TSharedPtr<SVariantManagerTableRow> InTableRow)
{
	// Using this syncs all splitters between property nodes and also the header
	FPropertyColumnSizeData& ColumnSizeData = GetVariantManager().Pin()->GetVariantManagerWidget()->GetPropertyColumnSizeData();

	return SNew(SBox)
	[
		SNew(SBorder)
		.VAlign(VAlign_Center)
		.BorderImage(this, &FVariantManagerDisplayNode::GetNodeBorderImage)
		.BorderBackgroundColor(this, &FVariantManagerDisplayNode::GetNodeBackgroundTint)
		.Padding(FMargin(2.0f, 0.0f, 2.0f, 0.0f))
		[
			SNew(SSplitter)
			.Style(FEditorStyle::Get(), "DetailsView.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f)
			.Orientation(Orient_Horizontal)

			+ SSplitter::Slot()
			.Value(ColumnSizeData.LeftColumnWidth)
			.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([](float InNewWidth)
			{
				 //This has to be bound or the splitter will take it upon itself to determine the size
				 //We do nothing here because it is handled by the column size data
			}))
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.Padding(FMargin( 4.0, 0.0f, 0.0f, 0.0f ))
				.HeightOverride(26) // Sum of paddings for the rows used in a details view like this
				[
					SAssignNew(EditableLabel, SInlineEditableTextBlock)
					.IsReadOnly(true)
					.Font(FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.RegularFont"))
					.ColorAndOpacity(this, &FVariantManagerDisplayNode::GetDisplayNameColor)
					.OnTextCommitted(this, &FVariantManagerDisplayNode::HandleNodeLabelTextChanged)
					.Text(this, &FVariantManagerDisplayNode::GetDisplayName)
					.ToolTipText(this, &FVariantManagerDisplayNode::GetDisplayNameToolTipText)
					.Clipping(EWidgetClipping::ClipToBounds)
				]
			]

			+ SSplitter::Slot()
			.Value(ColumnSizeData.RightColumnWidth)
			.OnSlotResized(ColumnSizeData.OnWidthChanged)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(SBox)
					.Padding(FMargin( 3.0, 0.0f, 3.0f, 0.0f ))
					.HeightOverride(26)
					[
						GetPropertyValueWidget().ToSharedRef()
					]
				]

				+SHorizontalBox::Slot()
				.Padding( FMargin(0.0f, 0.0f, 3.0f, 0.0) )
				.AutoWidth()
				.VAlign( VAlign_Center )
				[
					SNew(SBox)
					.HeightOverride(21)
					.WidthOverride(21)
					[
						SAssignNew(RecordButton, SButton)
						.IsFocusable(false)
						.ToolTipText(LOCTEXT("UseCurrentTooltip", "Record the current value for this property"))
						.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
						.ContentPadding(0.0f)
						.OnClicked(this, &FVariantManagerPropertyNode::RecordMultipleValues)
						.Visibility(FVariantManagerPropertyNode::GetRecordButtonVisibility())
						.Content()
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
							.ShadowColorAndOpacity(FVector::ZeroVector)
							.ShadowOffset(FVector::ZeroVector)
							.Text(FEditorFontGlyphs::Download)
							.Justification(ETextJustify::Center)
							.Margin(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
						]
					]
				]

				+SHorizontalBox::Slot()
				.Padding( FMargin(0.0f, 0.0f, 1.0f, 0.0) )
				.AutoWidth()
				.VAlign( VAlign_Center )
				[
					SNew(SBox)
					.HeightOverride(21)
					.WidthOverride(21)
					[
						SAssignNew(ResetButton, SButton)
						.IsFocusable(false)
						.ToolTipText(LOCTEXT("ResetTooltip", "Reset to the property's default value"))
						.ButtonStyle( FEditorStyle::Get(), "HoverHintOnly" )
						.ContentPadding(6.0f)
						.OnClicked(this, &FVariantManagerPropertyNode::ResetMultipleValuesToDefault)
						.Visibility(FVariantManagerPropertyNode::GetResetButtonVisibility())
						.Content()
						[
							SNew(SImage)
							.Image( FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault") )
						]
					]
				]
			]
		]
	];
}

void FVariantManagerPropertyNode::UpdateRecordedDataFromSinglePropView(TSharedPtr<ISinglePropertyView> SinglePropView)
{
	// Warning: This also fires after UpdateSinglePropViewFromRecordedData when that fires

	// Get the address of the data the user just input
	TSharedPtr<IPropertyHandle> PropHandle = SinglePropView->GetPropertyHandle();
	TArray<void*> RawDataForEachObject;
	PropHandle->AccessRawData(RawDataForEachObject);
	void* SinglePropWidgetDataPtr = RawDataForEachObject[0]; // We'll always pass just a single object

	for (TWeakObjectPtr<UPropertyValue> PropertyValue : PropertyValues)
	{
		if (PropertyValue.IsValid())
		{
			PropertyValue.Get()->SetRecordedData((uint8*)SinglePropWidgetDataPtr, PropHandle->GetProperty()->ElementSize);
		}
	}

	RecordButton->SetVisibility(GetRecordButtonVisibility());
	ResetButton->SetVisibility(GetResetButtonVisibility());
}

FReply FVariantManagerPropertyNode::ResetMultipleValuesToDefault()
{
	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ResetPropertyValue", "Reset {0} property {0}|plural(one=capture,other=captures) to default"),
		PropertyValues.Num()
	));

	TArray<uint8> DefaultValue;
	for (TWeakObjectPtr<UPropertyValue> PropertyValue : PropertyValues)
	{
		if (PropertyValue.IsValid())
		{
			DefaultValue = PropertyValue->GetDefaultValue();
			break;
		}
	}

	if (DefaultValue.Num() == 0)
	{
		UE_LOG(LogVariantManager, Error, TEXT("Failed to find a valid default value for property '%s'"), *GetDisplayName().ToString());
		return FReply::Handled();
	}

	for (TWeakObjectPtr<UPropertyValue> PropertyValue : PropertyValues)
	{
		if (PropertyValue.IsValid())
		{
			PropertyValue->SetRecordedData(DefaultValue.GetData(), DefaultValue.Num());
		}
	}

	GetVariantManager().Pin()->GetVariantManagerWidget()->RefreshPropertyList();
	return FReply::Handled();
}

FReply FVariantManagerPropertyNode::RecordMultipleValues()
{
	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("RecordedNewDataForProperty", "Recorded new data for {0} property {0}|plural(one=capture,other=captures)"),
		PropertyValues.Num()
	));

	for (TWeakObjectPtr<UPropertyValue> PropertyValue : PropertyValues)
	{
		if (PropertyValue.IsValid() && PropertyValue->HasValidResolve())
		{
			PropertyValue->RecordDataFromResolvedObject();
		}
	}

	GetVariantManager().Pin()->GetVariantManagerWidget()->RefreshPropertyList();
	return FReply::Handled();
}

bool FVariantManagerPropertyNode::PropertiesHaveSameValue() const
{
	if (PropertyValues.Num() > 1 && PropertyValues[0].IsValid())
	{
		if (UPropertyValue* FirstProp = PropertyValues[0].Get())
		{
			const TArray<uint8>& FirstPropVal = FirstProp->GetRecordedData();
			for (int32 Index = 1; Index < PropertyValues.Num(); ++Index)
			{
				TWeakObjectPtr<UPropertyValue> WeakPropertyValue = PropertyValues[Index];
				if (!WeakPropertyValue.IsValid() ||
					!WeakPropertyValue->HasValidResolve() ||
					WeakPropertyValue->GetRecordedData() != FirstPropVal)
				{
					return false;
				}
			}
		}
	}

	return true;
}

bool FVariantManagerPropertyNode::PropertiesHaveDefaultValue() const
{
	if (!PropertiesHaveSameValue())
	{
		return false;
	}

	if (PropertyValues.Num() > 0 && PropertyValues[0].IsValid())
	{
		if (UPropertyValue* FirstProp = PropertyValues[0].Get())
		{
			return FirstProp->GetDefaultValue() == FirstProp->GetRecordedData();
		}
	}

	return false;
}

bool FVariantManagerPropertyNode::PropertiesHaveCurrentValue() const
{
	for (TWeakObjectPtr<UPropertyValue> Prop : PropertyValues)
	{
		if (Prop.IsValid())
		{
			if (!Prop->IsRecordedDataCurrent())
			{
				return false;
			}
		}
	}

	return true;
}

EVisibility FVariantManagerPropertyNode::GetResetButtonVisibility() const
{
	bool bNoneResolved = true;
	for (TWeakObjectPtr<UPropertyValue> WeakPropertyValue : PropertyValues)
	{
		if (WeakPropertyValue.IsValid() && WeakPropertyValue->HasValidResolve())
		{
			bNoneResolved = false;
			break;
		}
	}

	return bNoneResolved || PropertiesHaveDefaultValue() ? EVisibility::Hidden : EVisibility::Visible;
}

EVisibility FVariantManagerPropertyNode::GetRecordButtonVisibility() const
{
	bool bNoneResolved = true;
	for (TWeakObjectPtr<UPropertyValue> WeakPropertyValue : PropertyValues)
	{
		if (WeakPropertyValue.IsValid() && WeakPropertyValue->HasValidResolve())
		{
			bNoneResolved = false;
			break;
		}
	}

	return bNoneResolved || PropertiesHaveCurrentValue() ? EVisibility::Hidden : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
