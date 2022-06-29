// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeBindingExtension.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "EdGraphSchema_K2.h"
#include "IPropertyAccessEditor.h"
#include "Features/IModularFeatures.h"
#include "StateTreeEvaluatorBase.h"
#include "Algo/Accumulate.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeAnyEnum.h"
#include "StateTreePropertyBindingCompiler.h"
#include "StateTreeCompiler.h"
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
	
	FString Category = InPropertyHandle->GetMetaData(CategoryName);
	if (Category.IsEmpty())
	{
		if (const FString* InstanceMetaData = InPropertyHandle->GetInstanceMetaData(CategoryName))
		{
			Category = *InstanceMetaData;
		}
	}
	
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

FText GetSectionNameFromDataSource(const EStateTreeBindableStructSource Source)
{
	switch (Source)
	{
		case EStateTreeBindableStructSource::Task:
			return LOCTEXT("Tasks", "Tasks");
		case EStateTreeBindableStructSource::Evaluator:
			return LOCTEXT("Evaluators", "Evaluators");
		case EStateTreeBindableStructSource::TreeData:
			return LOCTEXT("Tree Data", "Tree Data");
		case EStateTreeBindableStructSource::StateParameter:
			return LOCTEXT("State Parameter", "State Parameter");
		case EStateTreeBindableStructSource::TreeParameter:
			return LOCTEXT("Tree Parameter", "Tree Parameter");
	}

	return FText::GetEmpty();
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

				FBindingContextStruct& ContextStruct = Context.AddDefaulted_GetRef();
				ContextStruct.DisplayText = FText::FromString(StructDesc.Name.ToString());
				ContextStruct.Struct = const_cast<UStruct*>(Struct);
				ContextStruct.Section = UE::StateTree::PropertyBinding::GetSectionNameFromDataSource(StructDesc.DataSource);
			}
		}
	}

	FProperty* Property = InPropertyHandle->GetProperty();

	bool bIsDataRef = false;
	bool bIsAnyEnum = false;
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		bIsAnyEnum = StructProperty->Struct == FStateTreeAnyEnum::StaticStruct();
		bIsDataRef = StructProperty->Struct == FStateTreeStructRef::StaticStruct();
	}

	const UScriptStruct* DataRefBaseStruct = nullptr;
	if (bIsDataRef)
	{
		FString BaseStructName;
		DataRefBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(Property, BaseStructName);
	}
	
	FPropertyBindingWidgetArgs Args;
	Args.Property = InPropertyHandle->GetProperty();

	Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([EditorBindings, OwnerObject, InPropertyHandle, bIsAnyEnum, bIsDataRef, DataRefBaseStruct](FProperty* InProperty)
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
			else if (bIsDataRef && DataRefBaseStruct != nullptr)
			{
				if (const FStructProperty* SourceStructProperty = CastField<FStructProperty>(InProperty))
				{
					if (SourceStructProperty->Struct == TBaseStructure<FStateTreeStructRef>::Get())
					{
						FString SourceBaseStructName;
						const UScriptStruct* SourceDataRefBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(SourceStructProperty, SourceBaseStructName);
						bCanBind = SourceDataRefBaseStruct && SourceDataRefBaseStruct->IsChildOf(DataRefBaseStruct);
					}
					else
					{
						bCanBind = SourceStructProperty->Struct && SourceStructProperty->Struct->IsChildOf(DataRefBaseStruct);
					}
				}
			}
			else
			{
				// Note: We support type promotion here
				bCanBind = FStateTreePropertyBindingCompiler::GetPropertyCompatibility(InProperty, InPropertyHandle->GetProperty()) != EPropertyAccessCompatibility::Incompatible;
			}

			return bCanBind;
		});

	Args.OnCanBindToContextStruct = FOnCanBindToContextStruct::CreateLambda([InPropertyHandle](const UStruct* InStruct)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(InPropertyHandle->GetProperty()))
			{
				return StructProperty->Struct == InStruct;
			}
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InPropertyHandle->GetProperty()))
			{
				return InStruct != nullptr && InStruct->IsChildOf(ObjectProperty->PropertyClass);
			}
			return false;
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
				if (TargetPath.IsValid() && InBindingChain.Num() > 0)
				{
					// First item in the binding chain is the index in AccessibleStructs.
					FStateTreeEditorPropertyPath SourcePath;
					const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
					
					TArray<FBindingChainElement> SourceBindingChain = InBindingChain;
					SourceBindingChain.RemoveAt(0); // remove struct index.

					check(SourceStructIndex >= 0 && SourceStructIndex < AccessibleStructs.Num());

					// If SourceBindingChain is empty at this stage, it means that the binding points to the source struct itself.
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
			return FAppStyle::GetBrush(PropertyIcon);
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
