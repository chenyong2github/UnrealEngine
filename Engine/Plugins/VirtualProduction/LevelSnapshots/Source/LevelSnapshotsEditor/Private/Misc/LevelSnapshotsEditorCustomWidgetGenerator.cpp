// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/LevelSnapshotsEditorCustomWidgetGenerator.h"

#include "Data/PropertySelection.h"
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
		MakeShared<FLevelSnapshotsEditorResultsRow>(InFieldPath->GetDisplayNameText(), FLevelSnapshotsEditorResultsRow::SingleProperty, 
		InDirectParentRow.IsValid() ? InDirectParentRow.Pin()->GenerateChildWidgetCheckedStateBasedOnParent() : ECheckBoxState::Checked, 
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

		// Owning object is an FProperty (usually for members of a collection
		// I.E., Property is an item in array/set/map, so OwnerProperty is the collection property)
		if (const FProperty* OwnerProperty = FindFProperty<FProperty>(InObject->GetClass(), OwnerObjectName))
		{
			void* OwnerPropertyValue = OwnerProperty->ContainerPtrToValuePtr<void>(InObject);
			PropertyValue = OwnerProperty->ContainerPtrToValuePtr<void>(OwnerPropertyValue);
		}
		else if (const UScriptStruct* ParentScriptStruct = Cast<UScriptStruct>(ParentObject)) 
		{
			// For members of a struct, the struct is a UScriptStruct object rather than the FStructProperty.
			// In this case, we need to go through the Class tree an generate a PropertyChain, e.g. FStructProperty->FStructProperty->LeafProperty

			UStruct* IterableStruct;

			if (UScriptStruct* AsScriptStruct = Cast<UScriptStruct>(InObject))
			{
				IterableStruct = AsScriptStruct;
			}
			else
			{
				IterableStruct = InObject->GetClass();
			}
			check(IterableStruct);

			TOptional<FLevelSnapshotPropertyChain> OutChain = FLevelSnapshotPropertyChain::FindPathToProperty(Property, IterableStruct);

			if (OutChain.IsSet())
			{
				void* ChainIterationPropertyPtr = InObject;

				const int32 ChainLength = OutChain.GetValue().GetNumProperties(); 

				for (int32 ChainIndex = 0; ChainIndex < ChainLength; ChainIndex++)
				{
					const FProperty* IteratedProperty = OutChain.GetValue().GetPropertyFromRoot(ChainIndex);

					ChainIterationPropertyPtr = IteratedProperty->ContainerPtrToValuePtr<void>(ChainIterationPropertyPtr);
				}

				PropertyValue = ChainIterationPropertyPtr;
			}
			
		}
		else if (Property->GetOwner<UClass>()) // This means the property is a surface-level property in a class, so it's not in a struct or collection
		{
			PropertyValue = Property->ContainerPtrToValuePtr<void>(InObject);
		}

		// We should have found a value ptr in the methods above, but we ensure in case of a missed scenario
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
