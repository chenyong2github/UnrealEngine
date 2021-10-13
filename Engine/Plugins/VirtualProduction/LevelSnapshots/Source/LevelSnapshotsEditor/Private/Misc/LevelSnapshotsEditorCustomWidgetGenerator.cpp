// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/LevelSnapshotsEditorCustomWidgetGenerator.h"

#include "LevelSnapshotsLog.h"
#include "Views/Results/LevelSnapshotsEditorResultsRow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void LevelSnapshotsEditorCustomWidgetGenerator::CreateRowsForPropertiesNotHandledByPropertyRowGenerator(
	TFieldPath<FProperty> InFieldPath,
	UObject* InSnapshotObject,
	UObject* InWorldObject, 
	const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView,
	const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow)
{
	if (!ensure(InFieldPath->IsValidLowLevel() && IsValid(InSnapshotObject) && IsValid(InWorldObject)))
	{
		return;
	}

	TSharedPtr<SWidget> CustomSnapshotWidget;
	TSharedPtr<SWidget> CustomWorldWidget;

	if (InFieldPath->GetName() == "AttachParent")
	{
		auto WidgetTextEditLambda = [] (const FString& InWidgetText, const UObject* InPropertyObject)
		{
			if (const USceneComponent* AsSceneComponent = Cast<USceneComponent>(InPropertyObject))
			{
				if (const AActor* OwningActor = AsSceneComponent->GetOwner())
				{
					return FString::Printf(TEXT("%s.%s"), *OwningActor->GetActorLabel(), *InWidgetText);
				}
			}

			return InWidgetText;
		};
		
		CustomSnapshotWidget = GenerateObjectPropertyWidget(InFieldPath, InSnapshotObject, WidgetTextEditLambda);
		CustomWorldWidget = GenerateObjectPropertyWidget(InFieldPath, InWorldObject, WidgetTextEditLambda);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("%hs: Unsupported Property found named '%s' with FieldPath: %s"), __FUNCTION__, *InFieldPath->GetAuthoredName(), *InFieldPath.ToString());
		
		CustomSnapshotWidget = GenerateGenericPropertyWidget(InFieldPath, InSnapshotObject, nullptr);
		CustomWorldWidget = GenerateGenericPropertyWidget(InFieldPath, InWorldObject, nullptr);
	}

	if (!CustomSnapshotWidget.IsValid() && !CustomWorldWidget.IsValid())
	{
		return;
	}

	// Create property
	const FLevelSnapshotsEditorResultsRowPtr NewProperty = 
		MakeShared<FLevelSnapshotsEditorResultsRow>(InFieldPath->GetDisplayNameText(), FLevelSnapshotsEditorResultsRow::SingleProperty, ECheckBoxState::Checked, 
		InResultsView, InDirectParentRow);

	NewProperty->InitPropertyRowWithCustomWidget(InDirectParentRow, InFieldPath.Get(), CustomSnapshotWidget, CustomWorldWidget);

	InDirectParentRow.Pin()->AddToChildRows(NewProperty);
}

TSharedPtr<SWidget> LevelSnapshotsEditorCustomWidgetGenerator::GenerateGenericPropertyWidget(
	TFieldPath<FProperty> InFieldPath, UObject* InObject,
	TFunction<FString(const FString&, const UObject*)> InWidgetTextEditLambda)
{
	const FProperty* Property = InFieldPath.Get();

	if (CastField<FStructProperty>(Property))
	{
		// Don't generate widgets for FStructProperty
		return nullptr;
	}

	if (InObject->IsValidLowLevel())
	{
		void* PropertyValue = nullptr;

		UObject* ParentObject = Property->GetOwner<UObject>();
		check(ParentObject);

		const FName OwnerObjectName = ParentObject->GetFName();

		if (const FProperty* OwnerProperty = FindFProperty<FProperty>(InObject->GetClass(), OwnerObjectName))
		{
			void* OwnerPropertyValue = OwnerProperty->ContainerPtrToValuePtr<void>(InObject);
			PropertyValue = OwnerProperty->ContainerPtrToValuePtr<void>(OwnerPropertyValue);
		}
		else if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(ParentObject))
		{
			for (TFieldIterator<FStructProperty> It(InObject->GetClass()); It; ++It)
			{
				const FStructProperty* StructProp = *It;
				if (StructProp->Struct == ScriptStruct)
				{
					if (void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(InObject))
					{
						PropertyValue = Property->ContainerPtrToValuePtr<void>(StructPtr);
					}

					break;
				}
			}
			
		}
		else if (Property->GetOwner<UClass>())
		{
			PropertyValue = Property->ContainerPtrToValuePtr<void>(InObject);
		}

		if (ensure(PropertyValue != nullptr))
		{
			FString ValueText;
			Property->ExportTextItem(ValueText, PropertyValue, nullptr, ParentObject, PPF_None);

			if (InWidgetTextEditLambda)
			{
				ValueText = InWidgetTextEditLambda(ValueText, InObject);
			}

			return SNew(STextBlock)
				.Text(FText::FromString(ValueText))
				.Font(FEditorStyle::GetFontStyle("BoldFont"))
				.ToolTipText(FText::FromString(ValueText));
		}
	}

	return nullptr;
}

TSharedPtr<SWidget> LevelSnapshotsEditorCustomWidgetGenerator::GenerateObjectPropertyWidget(
	TFieldPath<FProperty> InFieldPath, const UObject* InObject, TFunction<FString(const FString&, const UObject*)> InWidgetTextEditLambda)
{
	if (InObject->IsValidLowLevel())
	{
		if (const FObjectProperty* AsAttachParentObjectProperty = CastField<FObjectProperty>(InFieldPath.Get()))
		{
			const UObject* PropertyObject = AsAttachParentObjectProperty->GetObjectPropertyValue_InContainer(InObject);

			if (PropertyObject->IsValidLowLevel())
			{
				FString WidgetText = PropertyObject->GetName();

				if (InWidgetTextEditLambda)
				{
					WidgetText = InWidgetTextEditLambda(WidgetText, PropertyObject);
				}

				return SNew(STextBlock)
				.Text(FText::FromString(WidgetText))
				.Font(FEditorStyle::GetFontStyle("BoldFont"))
				.ToolTipText(FText::FromString(WidgetText));

			}
			else
			{
				return SNew(STextBlock)
				.Text(LOCTEXT("LevelSnapshotsEditorResults_AttachParentInvalidText","No Attach Parent found."));
			}
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE

/*
 * // Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyWidgetGenerator.h"

#include "LevelSnapshotsLog.h"

#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"

bool FPropertyWidgetGenerator::IsPropertyContainer(const FProperty* Property)
{
	if (!ensure(Property))
	{
		return false;
	}

	return Property->IsA(FArrayProperty::StaticClass()) || Property->IsA(FMapProperty::StaticClass()) ||
		Property->IsA(FSetProperty::StaticClass()) || Property->IsA(FStructProperty::StaticClass());
}

TSharedPtr<SWidget> FPropertyWidgetGenerator::GenerateWidgetForUClassProperty(FProperty* Property, UObject* OwningObject)
{
	if (!ensure(Property) || !ensure(OwningObject))
	{
		return nullptr;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(OwningObject);

	return GenerateWidgetForProperty_Internal(Property, OwningObject, ValuePtr);
}

TSharedPtr<SWidget> FPropertyWidgetGenerator::GenerateWidgetForPropertyInContainer(
	FProperty* Property, UObject* OwningObject, void* OuterPtr, int32 ArrayIndex)
{
	if (!ensure(Property) || !ensure(OwningObject) || !ensure(OuterPtr))
	{
		return nullptr;
	}
	
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(OuterPtr, ArrayIndex);

	return GenerateWidgetForProperty_Internal(Property, OwningObject, ValuePtr);
}

bool FPropertyWidgetGenerator::AreUClassPropertyValuesEqual(
	FProperty* Property, UObject* OwningObjectA, UObject* OwningObjectB)
{
	ensure(Property); 
	ensure(OwningObjectA);
	ensure(OwningObjectB);

	// First let's get the value ptrs of the property
	void* ValuePtrA = Property->ContainerPtrToValuePtr<void>(OwningObjectA);
	void* ValuePtrB = Property->ContainerPtrToValuePtr<void>(OwningObjectB);

	return ArePropertyValuesEqual_Internal(Property, ValuePtrA, ValuePtrB);
}

bool FPropertyWidgetGenerator::ArePropertyValuesInContainerEqual(
	FProperty* Property, void* OuterPtrA, void* OuterPtrB, int32 ArrayIndex)
{
	ensure(Property);
	ensure(OuterPtrA);
	ensure(OuterPtrB);
	check(ArrayIndex > -1);

	// First let's get the value ptr of the property
	void* ValuePtrA = Property->ContainerPtrToValuePtr<void>(OuterPtrA, ArrayIndex);
	void* ValuePtrB = Property->ContainerPtrToValuePtr<void>(OuterPtrB, ArrayIndex);

	return ArePropertyValuesEqual_Internal(Property, ValuePtrA, ValuePtrB);
}

TSharedRef<SWidget> FPropertyWidgetGenerator::MakeComboBoxWithSelection(const FString& InString, const FText& ToolTipText)
{
	return SNew(SComboBox<TSharedPtr<FString>>)
	       [
		       SNew(STextBlock)
				.ToolTipText(ToolTipText)
				.Text(FText::FromString(InString))
				.Font(FSlateFontInfo(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"), 8))
	       ];
}

TSharedPtr<SWidget> FPropertyWidgetGenerator::GenerateWidgetForProperty_Internal(
	FProperty* Property, UObject* OwningObject, void* ValuePtr)
{
	if (!ensure(ValuePtr))
	{
		return nullptr;
	}
	
	// Then let's get the class name
	const FText ToolTipText = FText::FromString(Property->GetClass()->GetName());

	// Then we'll iterate over relevant property types to see what kind of widget we should create
	if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			const float Value = FloatProperty->GetFloatingPointPropertyValue(ValuePtr);
			return SNew(SNumericEntryBox<float>)
				.ToolTipText(ToolTipText)
				.Value(Value)
				.AllowSpin(true)
				.MinSliderValue(Value)
				.MaxSliderValue(Value);
		}
		else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			const double Value = DoubleProperty->GetFloatingPointPropertyValue(ValuePtr);
			return SNew(SNumericEntryBox<double>)
				.ToolTipText(ToolTipText)
				.Value(Value)
				.AllowSpin(true)
				.MinSliderValue(Value)
				.MaxSliderValue(Value);
		}
		else // Not a float or double? Then some kind of integer (byte, int8, int16, int32, int64, uint8, uint16 ...)
		{
			const int64 Value = NumericProperty->GetUnsignedIntPropertyValue(ValuePtr);
			return SNew(SNumericEntryBox<int64>)
				.ToolTipText(ToolTipText)
				.Value(Value)
				.AllowSpin(true)
				.MinSliderValue(Value)
				.MaxSliderValue(Value);
		}
	}
	else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		return SNew(SCheckBox).IsChecked(BoolProperty->GetPropertyValue(ValuePtr)).ToolTipText(ToolTipText);
	}
	else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return SNew(SEditableTextBox).Text(FText::FromName(NameProperty->GetPropertyValue(ValuePtr))).ToolTipText(ToolTipText);
	}
	else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return SNew(SEditableTextBox).Text(FText::FromString(StringProperty->GetPropertyValue(ValuePtr))).ToolTipText(ToolTipText);
	}
	else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return SNew(SEditableTextBox).Text(TextProperty->GetPropertyValue(ValuePtr)).ToolTipText(ToolTipText);
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		FString ValueAsString;
		Property->ExportTextItem(ValueAsString, ValuePtr, ValuePtr, OwningObject, PPF_None);
		return MakeComboBoxWithSelection(ValueAsString, ToolTipText);
	}
	else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		if (Object->IsValidLowLevel())
		{
			return MakeComboBoxWithSelection(Object->GetName(), ToolTipText);
		}
		else
		{
			return MakeComboBoxWithSelection("None", ToolTipText);
		}
	}
	else if (FDelegateProperty* DelegateProperty = CastField<FDelegateProperty>(Property))
	{
		// We'll skip FDelegateProperty for now
		return nullptr;
	}
	else if (FMulticastDelegateProperty* MulticastDelegateProperty = CastField<FMulticastDelegateProperty>(Property))
	{
		// We'll skip FMulticastDelegateProperty for now
		return nullptr;
	}
	else
	{
		// If the property is not supported, use this as a fallback
		FString ValueAsString;
		Property->ExportTextItem(ValueAsString, ValuePtr, ValuePtr, OwningObject, PPF_None);
		return SNew(STextBlock).Text(FText::Format(INVTEXT("Class {0} As Text: {1}"),
			FText::FromString(Property->GetClass()->GetName()),
			FText::FromString(ValueAsString)));
	}
}

bool FPropertyWidgetGenerator::ArePropertyValuesEqual_Internal(FProperty* Property, void* ValuePtrA, void* ValuePtrB)
{
	ensure(ValuePtrA);
	ensure(ValuePtrB);

	// Then we'll iterate over relevant property types to see what kind of widget we should create
	if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
	{
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			const float ValueA = FloatProperty->GetFloatingPointPropertyValue(ValuePtrA); 
			const float ValueB = FloatProperty->GetFloatingPointPropertyValue(ValuePtrB); 
			return FMath::IsNearlyEqual(ValueA, ValueB);
		}
		else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			const double ValueA = DoubleProperty->GetFloatingPointPropertyValue(ValuePtrA);
			const double ValueB = DoubleProperty->GetFloatingPointPropertyValue(ValuePtrB);
			return FMath::IsNearlyEqual(ValueA, ValueB);
		}
		else // Not a float or double? Then some kind of integer (byte, int8, int16, int32, int64, uint8, uint16 ...)
		{
			const int64 ValueA = NumericProperty->GetSignedIntPropertyValue(ValuePtrA);
			const int64 ValueB = NumericProperty->GetSignedIntPropertyValue(ValuePtrB);
			return ValueA == ValueB;
		}
	}
	else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		return BoolProperty->GetPropertyValue(ValuePtrA) == BoolProperty->GetPropertyValue(ValuePtrB);
	}
	else if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		return NameProperty->GetPropertyValue(ValuePtrA) == NameProperty->GetPropertyValue(ValuePtrB);
	}
	else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		return StringProperty->GetPropertyValue(ValuePtrA).Equals(StringProperty->GetPropertyValue(ValuePtrB));
	}
	else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		return TextProperty->GetPropertyValue(ValuePtrA).EqualTo(TextProperty->GetPropertyValue(ValuePtrB));
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		return EnumProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(ValuePtrA) == 
			EnumProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(ValuePtrB);
	}
	else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjectA = ObjectProperty->GetObjectPropertyValue(ValuePtrA);
		UObject* ObjectB = ObjectProperty->GetObjectPropertyValue(ValuePtrB);

		if (ObjectA != nullptr && ObjectA->IsValidLowLevel())
		{
			if (ObjectB != nullptr && ObjectB->IsValidLowLevel())
			{
				// Both objects valid, verify names
				return ObjectA->GetName() == ObjectB->GetName();
			}
			else 
			{
				// A valid, B invalid
				return false;
			}
		}
		else
		{
			if (ObjectB != nullptr && ObjectB->IsValidLowLevel())
			{
				// A invalid, B valid
				return false;
			}
			else 
			{
				// Both are invalid
				return true;
			}
		}
	}
	else if (FDelegateProperty* DelegateProperty = CastField<FDelegateProperty>(Property))
	{
		// We'll skip FDelegateProperty for now
		return false;
	}
	else if (FMulticastDelegateProperty* MulticastDelegateProperty = CastField<FMulticastDelegateProperty>(Property))
	{
		// We'll skip FMulticastDelegateProperty for now
		return false;
	}
	else
	{
		// If the property is not supported, use this as a fallback
		return false;
	}
}

 */