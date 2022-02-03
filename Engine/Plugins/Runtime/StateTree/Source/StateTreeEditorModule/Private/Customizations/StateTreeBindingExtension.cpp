// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeBindingExtension.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "EdGraphSchema_K2.h"
#include "IPropertyAccessEditor.h"
#include "Features/IModularFeatures.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTree.h"
#include "Algo/Accumulate.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeAnyEnum.h"
#include "StateTreePropertyBindingCompiler.h"
#include "PropertyEditor/Private/PropertyNode.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::PropertyBinding
{
	
const FName StateTreeNodeIDName(TEXT("StateTreeNodeID"));
	

UObject* FindEditorBindingsOwner(UObject* InObject)
{
	UObject* Result = nullptr;

	for (UObject* Outer = InObject; Outer; Outer = Outer->GetOuter())
	{
		IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(Outer);
		if (BindingOwner)
		{
			Result = Outer;
			break;
		}
	}
	return Result;
}

void GetStructPropertyPath(TSharedPtr<IPropertyHandle> InPropertyHandle, FStateTreeEditorPropertyPath& OutPath)
{
	FGuid StructID;
	TArray<FBindingChainElement> BindingChain;

	if (FProperty* CurrentProperty = InPropertyHandle->GetProperty())
	{
		// Keep track of the path.
		BindingChain.Insert(FBindingChainElement(CurrentProperty, InPropertyHandle->GetIndexInArray()), 0);
		
		if (const FString* IDString = InPropertyHandle->GetInstanceMetaData(StateTreeNodeIDName))
		{
			LexFromString(StructID, **IDString);
		}
	}
	
	if (StructID.IsValid())
	{
		OutPath.StructID = StructID;
		IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
		PropertyAccessEditor.MakeStringPath(BindingChain, OutPath.Path);
	}
	else
	{
		OutPath = FStateTreeEditorPropertyPath();
	}
}

	
EStateTreePropertyUsage ParsePropertyUsage(TSharedPtr<const IPropertyHandle> InPropertyHandle)
{
	static const FName CategoryName(TEXT("Category"));
	
	const FString Category = InPropertyHandle->GetMetaData(CategoryName);

	if (Category == TEXT("Input"))
	{
		return EStateTreePropertyUsage::Input;
	}
	if (Category == TEXT("Output"))
	{
		return EStateTreePropertyUsage::Output;
	}
	if (Category == TEXT("Parameter"))
	{
		return EStateTreePropertyUsage::Parameter;
	}
	
	return EStateTreePropertyUsage::Invalid;
}


FOnStateTreeBindingChanged STATETREEEDITORMODULE_API OnStateTreeBindingChanged;

} // UE::StateTree::PropertyBinding

bool FStateTreeBindingExtension::IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle) const
{
	// Bindable property must have node ID
	if (PropertyHandle.GetInstanceMetaData(UE::StateTree::PropertyBinding::StateTreeNodeIDName) == nullptr)
	{
		return false;
	}

	// Only inputs and parameters are bindable.
	const EStateTreePropertyUsage Usage = UE::StateTree::PropertyBinding::ParsePropertyUsage(PropertyHandle.AsShared());
	return Usage == EStateTreePropertyUsage::Input || Usage == EStateTreePropertyUsage::Parameter;
}


void FStateTreeBindingExtension::ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable("PropertyAccessEditor"))
	{
		return;
	}

	IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");

	UObject* OwnerObject = nullptr;
	FStateTreeEditorPropertyBindings* EditorBindings = nullptr;

	// Array of structs we can bind to.
	TArray<FBindingContextStruct> Context;
	TArray<FStateTreeBindableStructDesc> AccessibleStructs;

	// The struct and property where we're binding.
	FStateTreeEditorPropertyPath TargetPath;

	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		// Only allow to binding when one object is selected.
		OwnerObject = UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]);

		// Figure out the structs we're editing, and property path relative to current property.
		UE::StateTree::PropertyBinding::GetStructPropertyPath(InPropertyHandle, TargetPath);

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

	FProperty* Property = InPropertyHandle->GetProperty();

	bool bIsAnyEnum = false;
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		bIsAnyEnum = StructProperty->Struct == FStateTreeAnyEnum::StaticStruct();
	}
	
	FPropertyBindingWidgetArgs Args;
	Args.Property = InPropertyHandle->GetProperty();

	Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([EditorBindings, OwnerObject, InPropertyHandle, bIsAnyEnum](FProperty* InProperty)
		{
			if (!EditorBindings || !OwnerObject)
			{
				return false;
			}

			// Special case for binding widget calling OnCanBindProperty with Args.Property (i.e. self).
			if (InPropertyHandle->GetProperty() == InProperty)
			{
				return true;
			}

			bool bCanBind = false;

			// AnyEnums need special handling.
			// It is a struct property but we want to treat it as an enum. We need to do this here, instead of 
			// FStateTreePropertyBindingCompiler::GetPropertyCompatibility() because the treatment depends on the value too.
			// Note: AnyEnums will need special handling before they can be used for binding.
			if (bIsAnyEnum)
			{
				FStateTreeAnyEnum AnyEnum;
				UE::StateTree::PropertyHelpers::GetStructValue(InPropertyHandle, AnyEnum);

				// If the enum class is not specified, allow to bind to any enum, if the class is specified allow only that enum.
				if (FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
				{
					if (UEnum* Enum = ByteProperty->GetIntPropertyEnum())
					{
						bCanBind = !AnyEnum.Enum || AnyEnum.Enum == Enum;
					}
				}
				else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
				{
					bCanBind = !AnyEnum.Enum || AnyEnum.Enum == EnumProperty->GetEnum();
				}
			}
			else
			{
				// Note: We support type promotion here
				bCanBind = FStateTreePropertyBindingCompiler::GetPropertyCompatibility(InProperty, InPropertyHandle->GetProperty()) != EPropertyAccessCompatibility::Incompatible;
			}

			return bCanBind;
		});

	Args.OnCanAcceptPropertyOrChildren = FOnCanBindProperty::CreateLambda([](FProperty* InProperty)
		{
			// Make only editor visible properties visible for binding.
			return InProperty->HasAnyPropertyFlags(CPF_Edit);
		});

	Args.OnCanBindToClass = FOnCanBindToClass::CreateLambda([](UClass* InClass)
		{
			return true;
		});

	Args.OnAddBinding = FOnAddBinding::CreateLambda([EditorBindings, OwnerObject, TargetPath, AccessibleStructs](FName InPropertyName, const TArray<FBindingChainElement>& InBindingChain)
		{
			IPropertyAccessEditor& PropertyAccessEditor = IModularFeatures::Get().GetModularFeature<IPropertyAccessEditor>("PropertyAccessEditor");
			if (EditorBindings && OwnerObject)
			{
				if (TargetPath.IsValid() && InBindingChain.Num() > 1)	// Assume at least: [0] struct index, [1] a property.
				{
					FStateTreeEditorPropertyPath SourcePath;
					const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
					TArray<FBindingChainElement> SourceBindingChain(InBindingChain.GetData() + 1, InBindingChain.Num() - 1);
					check(SourceStructIndex >= 0 && SourceStructIndex < AccessibleStructs.Num());

					SourcePath.StructID = AccessibleStructs[SourceStructIndex].ID;
					PropertyAccessEditor.MakeStringPath(SourceBindingChain, SourcePath.Path);

					OwnerObject->Modify();
					EditorBindings->AddPropertyBinding(SourcePath, TargetPath);

					UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.Broadcast(SourcePath, TargetPath);
				}
			}
		});

	Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([EditorBindings, OwnerObject, TargetPath](FName InPropertyName)
		{
			if (EditorBindings && OwnerObject)
			{
				OwnerObject->Modify();
				EditorBindings->RemovePropertyBindings(TargetPath);

				FStateTreeEditorPropertyPath SourcePath; // Null path
				UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.Broadcast(SourcePath, TargetPath);
			}
		});

	Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([EditorBindings, TargetPath](FName InPropertyName)
		{
			return EditorBindings && EditorBindings->HasPropertyBinding(TargetPath);
		});

	Args.CurrentBindingText = MakeAttributeLambda([EditorBindings, TargetPath, AccessibleStructs]()
		{
			const FText MultipleValues = LOCTEXT("MultipleValues", "Multiple Values");
			const FText Bind = LOCTEXT("Bind", "Bind");
			FText CurrentValue = Bind;

			if (EditorBindings)
			{
				if (const FStateTreeEditorPropertyPath* SourcePath = EditorBindings->GetPropertyBindingSource(TargetPath))
				{
					FString PropertyName;
					for (int32 i = 0; i < AccessibleStructs.Num(); i++)
					{
						if (AccessibleStructs[i].ID == SourcePath->StructID)
						{
							PropertyName = AccessibleStructs[i].Name.ToString();
							break;
						}
					}
					for (const FString& Segment : SourcePath->Path)
					{
						PropertyName += TEXT(".") + Segment;
					}
					CurrentValue = FText::FromString(PropertyName);
				}
			}
			else
			{
				// StateTreeEditorData is not valid if there's multiple objects selected.
				CurrentValue = MultipleValues;
			}

			return CurrentValue;
		});

	Args.CurrentBindingImage = MakeAttributeLambda([]() -> const FSlateBrush*
		{
			static FName PropertyIcon(TEXT("Kismet.Tabs.Variables"));
			return FEditorStyle::GetBrush(PropertyIcon);
		});

	Args.CurrentBindingColor = MakeAttributeLambda([InPropertyHandle]() -> FLinearColor
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

			FEdGraphPinType PinType;
			Schema->ConvertPropertyToPinType(InPropertyHandle->GetProperty(), PinType);
			const FLinearColor BindingColor = Schema->GetPinTypeColor(PinType);

			// TODO: Handle coloring of type promotion

			return BindingColor;
		});

	Args.bAllowNewBindings = false;
	Args.bAllowArrayElementBindings = false;
	Args.bAllowUObjectFunctions = false;

	InWidgetRow.ExtensionContent()
	[
		PropertyAccessEditor.MakePropertyBindingWidget(Context, Args)
	];
}

#undef LOCTEXT_NAMESPACE

