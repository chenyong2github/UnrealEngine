// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorPropertyPathDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeBindingExtension.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"

#include "EdGraphSchema_K2.h"
#include "IPropertyAccessEditor.h"
#include "Features/IModularFeatures.h"
#include "StateTreeEditorData.h"
#include "Algo/Accumulate.h"
#include "InstancedStruct.h"
#include "StateTreePropertyHelpers.h"


#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeEditorPropertyPathDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeEditorPropertyPathDetails);
}

void FStateTreeEditorPropertyPathDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.VAlign(VAlign_Center)
		[
			CreateBindingWidget(StructProperty)
		];
}

void FStateTreeEditorPropertyPathDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

TSharedRef<SWidget> FStateTreeEditorPropertyPathDetails::CreateBindingWidget(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		return SNullWidget::NullWidget;
	}

	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

	UObject* OwnerObject = nullptr;
	FStateTreeEditorPropertyBindings* EditorBindings = nullptr;

	// Array of structs we can bind to.
	TArray<FBindingContextStruct> Context;
	TArray<FStateTreeBindableStructDesc> AccessibleStructs;

	// The struct and property where we're binding.
	FStateTreeEditorPropertyPath TargetPath;
	UStateTreeState* OuterState = nullptr;

	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		// Only allow to binding when one object is selected.
		OwnerObject = UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]);

		// Figure out the structs we're editing, and property path relative to current property.
		UE::StateTree::PropertyBinding::GetOuterStructPropertyPath(InPropertyHandle, TargetPath);

		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject))
		{
			EditorBindings = BindingOwner->GetPropertyEditorBindings();
			BindingOwner->GetAccessibleStructs(TargetPath.StructID, AccessibleStructs);
			for (FStateTreeBindableStructDesc& StructDesc : AccessibleStructs)
			{
				const UStruct* Struct = StructDesc.Struct;
				Context.Emplace(const_cast<UStruct*>(Struct), nullptr, FText::FromString(StructDesc.Name.ToString()));
			}
		}
	}

	FPropertyBindingWidgetArgs Args;
	Args.Property = nullptr; // Bind to any property type.

	Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([OwnerObject](FProperty* InProperty)
		{
			// Special case for null, binding widget calls OnCanBindProperty with Args.Property as well, this is to catch the case that we've set Args.Property to null.
			if (InProperty == nullptr)
			{
				return true;
			}

			// TODO: Make this configurable with meta tags.
			const bool bIsNumeric = InProperty->IsA<FNumericProperty>();
			const bool bIsEnum = InProperty->IsA<FEnumProperty>();

			return OwnerObject != nullptr && (bIsNumeric || bIsEnum);
		});

	Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
		{
			return true;
		});

	Args.OnAddBinding = FOnAddBinding::CreateLambda([InPropertyHandle, OwnerObject, AccessibleStructs](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
			if (OwnerObject && InPropertyHandle)
			{
				if (InBindingChain.Num() > 1)	// Assume at least: [0] struct index, [1] a property.
				{
					const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
					TArray<FBindingChainElement> SourceBindingChain(InBindingChain.GetData() + 1, InBindingChain.Num() - 1);
					check(SourceStructIndex >= 0 && SourceStructIndex < AccessibleStructs.Num());

					FStateTreeEditorPropertyPath NewPath;
					NewPath.StructID = AccessibleStructs[SourceStructIndex].ID;
					PropertyAccessEditor.MakeStringPath(SourceBindingChain, NewPath.Path);

					UE::StateTree::PropertyHelpers::SetStructValue<FStateTreeEditorPropertyPath>(InPropertyHandle, NewPath, EPropertyValueSetFlags::NotTransactable);
				}
			}
		});

	Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([InPropertyHandle, OwnerObject](FName InPropertyName)
		{
			if (OwnerObject && InPropertyHandle)
			{
				FStateTreeEditorPropertyPath EmptyPath;
				UE::StateTree::PropertyHelpers::SetStructValue<FStateTreeEditorPropertyPath>(InPropertyHandle, EmptyPath, EPropertyValueSetFlags::NotTransactable);
			}
		});

	Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([](FName InPropertyName)
		{
			return true;
		});

	Args.CurrentBindingText = MakeAttributeLambda([InPropertyHandle, OwnerObject, AccessibleStructs]()
		{
			const FText MultipleValues = LOCTEXT("MultipleValues", "Multiple Values");
			const FText Bind = LOCTEXT("Bind", "Bind");
			FText CurrentValue = Bind;

			if (OwnerObject && InPropertyHandle)
			{
				OwnerObject->Modify();

				TArray<void*> RawData;
				InPropertyHandle->AccessRawData(RawData);
				if (RawData.Num() == 1 && RawData[0])
				{
					FStateTreeEditorPropertyPath& Path = *static_cast<FStateTreeEditorPropertyPath*>(RawData[0]);
					if (Path.IsValid())
					{
						FString PropertyName;
						for (int32 i = 0; i < AccessibleStructs.Num(); i++)
						{
							if (AccessibleStructs[i].ID == Path.StructID)
							{
								PropertyName = AccessibleStructs[i].Name.ToString();
								break;
							}
						}
						for (const FString& Segment : Path.Path)
						{
							PropertyName += TEXT(".") + Segment;
						}
						CurrentValue = FText::FromString(PropertyName);
					}
				}
			}
			else
			{
				// OwnerObject is not valid if there's multiple objects selected.
				CurrentValue = MultipleValues;
			}

			return CurrentValue;
		});

	Args.CurrentBindingImage = MakeAttributeLambda([]() -> const FSlateBrush*
		{
			static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
			return FEditorStyle::GetBrush(PropertyIcon);
		});

	Args.CurrentBindingColor = MakeAttributeLambda([InPropertyHandle, AccessibleStructs]() -> FLinearColor
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

			// Get the path we're editing
			FStateTreeEditorPropertyPath* SourcePath = nullptr;
			TArray<void*> RawData;
			InPropertyHandle->AccessRawData(RawData);
			if (RawData.Num() == 1 && RawData[0])
			{
				SourcePath = static_cast<FStateTreeEditorPropertyPath*>(RawData[0]);
			}

			// Figure out source struct type.
			const UStruct* SourceStruct = nullptr;
			if (SourcePath && SourcePath->IsValid())
			{
				const FStateTreeBindableStructDesc* Desc = AccessibleStructs.FindByPredicate([SourcePath](const FStateTreeBindableStructDesc& Desc) { return Desc.ID == SourcePath->StructID; });
				if (Desc)
				{
					SourceStruct = Desc->Struct;
				}
			}

			// And resolve property from the struct and path.
			FProperty* SourceProperty = nullptr;
			if (SourceStruct && SourcePath)
			{
				int32 ArrayIndex = 0;
				PropertyAccessEditor.ResolvePropertyAccess(SourceStruct, SourcePath->Path, SourceProperty, ArrayIndex);
			}

			FLinearColor BindingColor = FLinearColor::Gray;
			if (SourceProperty)
			{
				const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
				FEdGraphPinType PinType;
				Schema->ConvertPropertyToPinType(SourceProperty, PinType);
				BindingColor = Schema->GetPinTypeColor(PinType);
			}

			// TODO: Handle coloring of type promotion

			return BindingColor;
		});

	Args.bAllowNewBindings = false;
	Args.bAllowArrayElementBindings = false;
	Args.bAllowUObjectFunctions = false;

	return PropertyAccessEditor.MakePropertyBindingWidget(Context, Args);
}

#undef LOCTEXT_NAMESPACE
