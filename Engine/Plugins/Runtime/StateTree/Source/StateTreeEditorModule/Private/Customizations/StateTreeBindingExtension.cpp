// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeBindingExtension.h"
#include "Blueprint/StateTreeNodeBlueprintBase.h"
#include "EdGraphSchema_K2.h"
#include "Features/IModularFeatures.h"
#include "IPropertyAccessEditor.h"
#include "StateTreeAnyEnum.h"
#include "StateTreeCompiler.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeNodeBase.h"
#include "Styling/AppStyle.h"
#include "UObject/EnumProperty.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTree::PropertyBinding
{
	
const FName StateTreeNodeIDName(TEXT("StateTreeNodeID"));
const FName AllowAnyBindingName(TEXT("AllowAnyBinding"));

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

void MakeStructPropertyPathFromBindingChain(const FGuid StructID, const TArray<FBindingChainElement>& InBindingChain, FStateTreePropertyPath& OutPath)
{
	OutPath.Reset();
	OutPath.SetStructID(StructID);
	
	for (const FBindingChainElement& Element : InBindingChain)
	{
		if (const FProperty* Property = Element.Field.Get<FProperty>())
		{
			OutPath.AddPathSegment(Property->GetFName(), Element.ArrayIndex);
		}
		else if (const UFunction* Function = Element.Field.Get<UFunction>())
		{
			OutPath.AddPathSegment(Function->GetFName());
		}
	}
}

EStateTreePropertyUsage MakeStructPropertyPathFromPropertyHandle(TSharedPtr<const IPropertyHandle> InPropertyHandle, FStateTreePropertyPath& OutPath)
{
	OutPath.Reset();

	FGuid StructID;
	TArray<FStateTreePropertyPathSegment> PathSegments;
	EStateTreePropertyUsage ResultUsage = EStateTreePropertyUsage::Invalid; 

	TSharedPtr<const IPropertyHandle> CurrentPropertyHandle = InPropertyHandle;
	while (CurrentPropertyHandle.IsValid())
	{
		const FProperty* Property = CurrentPropertyHandle->GetProperty();
		if (Property)
		{
			FStateTreePropertyPathSegment& Segment = PathSegments.InsertDefaulted_GetRef(0); // Traversing from leaf to root, insert in reverse.

			// Store path up to the property which has ID.
			Segment.SetName(Property->GetFName());
			Segment.SetArrayIndex(CurrentPropertyHandle->GetIndexInArray());

			// Store type of the object (e.g. for instanced objects or instanced structs).
			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				if (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_InstancedReference))
				{
					const UObject* Object = nullptr;
					if (CurrentPropertyHandle->GetValue(Object) == FPropertyAccess::Success)
					{
						if (Object)
						{
							Segment.SetInstanceStruct(Object->GetClass());
						}
					}
				}
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
				{
					void* Address = nullptr;
					if (CurrentPropertyHandle->GetValueData(Address) == FPropertyAccess::Success)
					{
						if (Address)
						{
							FInstancedStruct& Struct = *static_cast<FInstancedStruct*>(Address);
							Segment.SetInstanceStruct(Struct.GetScriptStruct());
						}
					}
				}
			}

			// Array access is represented as: "Array, PropertyInArray[Index]", we're traversing from leaf to root, skip the node without index.
			// Advancing the node before ID test, since the array is on the instance data, the ID will be on the Array node.
			if (Segment.GetArrayIndex() != INDEX_NONE)
			{
				TSharedPtr<const IPropertyHandle> ParentPropertyHandle = CurrentPropertyHandle->GetParentHandle();
				if (ParentPropertyHandle.IsValid())
				{
					const FProperty* ParentProperty = ParentPropertyHandle->GetProperty();
					if (ParentProperty
						&& ParentProperty->IsA<FArrayProperty>()
						&& Property->GetFName() == ParentProperty->GetFName())
					{
						CurrentPropertyHandle = ParentPropertyHandle;
					}
				}
			}

			// Bindable property must have node ID
			if (const FString* IDString = CurrentPropertyHandle->GetInstanceMetaData(UE::StateTree::PropertyBinding::StateTreeNodeIDName))
			{
				LexFromString(StructID, **IDString);
				ResultUsage = UE::StateTree::Compiler::GetUsageFromMetaData(Property);
				break;
			}
		}
		
		CurrentPropertyHandle = CurrentPropertyHandle->GetParentHandle();
	}

	if (!StructID.IsValid())
	{
		ResultUsage = EStateTreePropertyUsage::Invalid;
	}
	else
	{
		OutPath = FStateTreePropertyPath(StructID, PathSegments);
	}

	return ResultUsage;
}

FText GetSectionNameFromDataSource(const EStateTreeBindableStructSource Source)
{
	switch (Source)
	{
	case EStateTreeBindableStructSource::Context:
		return LOCTEXT("Context", "Context");
	case EStateTreeBindableStructSource::Parameter:
		return LOCTEXT("Parameters", "Parameters");
	case EStateTreeBindableStructSource::Evaluator:
		return LOCTEXT("Evaluators", "Evaluators");
	case EStateTreeBindableStructSource::GlobalTask:
		return LOCTEXT("StateGlobalTasks", "Global Tasks");
	case EStateTreeBindableStructSource::State:
		return LOCTEXT("StateParameters", "State");
	case EStateTreeBindableStructSource::Task:
		return LOCTEXT("Tasks", "Tasks");
	default:
		return FText::GetEmpty();
	}
}

// @todo: there's a similar function in StateTreeNodeDetails.cpp, merge.
FText GetPropertyTypeText(const FProperty* Property)
{
	FEdGraphPinType PinType;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->ConvertPropertyToPinType(Property, PinType);
				
	const FName PinSubCategory = PinType.PinSubCategory;
	const UObject* PinSubCategoryObject = PinType.PinSubCategoryObject.Get();
	if (PinSubCategory != UEdGraphSchema_K2::PSC_Bitmask && PinSubCategoryObject)
	{
		if (const UField* Field = Cast<const UField>(PinSubCategoryObject))
		{
			return Field->GetDisplayNameText();
		}
		return FText::FromString(PinSubCategoryObject->GetName());
	}

	return UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, NAME_None, true);
}

FOnStateTreePropertyBindingChanged STATETREEEDITORMODULE_API OnStateTreePropertyBindingChanged;

} // UE::StateTree::PropertyBinding

bool FStateTreeBindingExtension::IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& PropertyHandle) const
{
	const FProperty* Property = PropertyHandle.GetProperty();
	if (Property->HasAnyPropertyFlags(CPF_PersistentInstance | CPF_EditorOnly | CPF_Config | CPF_InstancedReference))
	{
		return false;
	}
	
	FStateTreePropertyPath TargetPath;
	// Figure out the structs we're editing, and property path relative to current property.
	const EStateTreePropertyUsage Usage = UE::StateTree::PropertyBinding::MakeStructPropertyPathFromPropertyHandle(PropertyHandle.AsShared(), TargetPath);

	if (Usage == EStateTreePropertyUsage::Input || Usage == EStateTreePropertyUsage::Context)
	{
		// Allow to bind only to the main level on input and context properties.
		return TargetPath.GetSegments().Num() == 1;
	}
	if (Usage == EStateTreePropertyUsage::Parameter)
	{
		return true;
	}
	
	return false;  
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
	FStateTreePropertyPath TargetPath;

	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		// Only allow to binding when one object is selected.
		OwnerObject = UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]);

		// Figure out the structs we're editing, and property path relative to current property.
		UE::StateTree::PropertyBinding::MakeStructPropertyPathFromPropertyHandle(InPropertyHandle, TargetPath);

		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject))
		{
			EditorBindings = BindingOwner->GetPropertyEditorBindings();
			BindingOwner->GetAccessibleStructs(TargetPath.GetStructID(), AccessibleStructs);
			for (FStateTreeBindableStructDesc& StructDesc : AccessibleStructs)
			{
				const UStruct* Struct = StructDesc.Struct;

				FBindingContextStruct& ContextStruct = Context.AddDefaulted_GetRef();
				ContextStruct.DisplayText = FText::FromString(StructDesc.Name.ToString());
				ContextStruct.Struct = const_cast<UStruct*>(Struct);
				ContextStruct.Section = UE::StateTree::PropertyBinding::GetSectionNameFromDataSource(StructDesc.DataSource);
			}
		}

		// Wrap value widget 
		if (EditorBindings)
		{
			auto IsValueVisible = TAttribute<EVisibility>::Create([TargetPath, EditorBindings]() -> EVisibility
			{
				return EditorBindings->HasPropertyBinding(TargetPath) ? EVisibility::Collapsed : EVisibility::Visible;
			});

			TSharedPtr<SWidget> ValueWidget = InWidgetRow.ValueContent().Widget;
			InWidgetRow.ValueContent()
			[
				SNew(SBox)
				.Visibility(IsValueVisible)
				[
					ValueWidget.ToSharedRef()
				]
			];
		}
		
	}

	FProperty* Property = InPropertyHandle->GetProperty();

	bool bIsStructRef = false;
	bool bIsAnyEnum = false;
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		bIsAnyEnum = StructProperty->Struct == FStateTreeAnyEnum::StaticStruct();
		bIsStructRef = StructProperty->Struct == FStateTreeStructRef::StaticStruct();
	}

	const UScriptStruct* StructRefBaseStruct = nullptr;
	if (bIsStructRef)
	{
		FString BaseStructName;
		StructRefBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(Property, BaseStructName);
	}
	
	FPropertyBindingWidgetArgs Args;
	Args.Property = InPropertyHandle->GetProperty();

	Args.OnCanBindProperty = FOnCanBindProperty::CreateLambda([EditorBindings, OwnerObject, InPropertyHandle, bIsAnyEnum, bIsStructRef, StructRefBaseStruct](FProperty* InProperty)
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
				// If the AnyEnum has AllowAnyBinding, allow to bind to any enum.
				const bool bAllowAnyBinding = InPropertyHandle->HasMetaData(UE::StateTree::PropertyBinding::AllowAnyBindingName);

				FStateTreeAnyEnum AnyEnum;
				UE::StateTree::PropertyHelpers::GetStructValue(InPropertyHandle, AnyEnum);

				// If the enum class is not specified, allow to bind to any enum, if the class is specified allow only that enum.
				if (const FByteProperty* ByteProperty = CastField<FByteProperty>(InProperty))
				{
					if (UEnum* Enum = ByteProperty->GetIntPropertyEnum())
					{
						bCanBind = bAllowAnyBinding || AnyEnum.Enum == Enum;
					}
				}
				else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(InProperty))
				{
					bCanBind = bAllowAnyBinding || AnyEnum.Enum == EnumProperty->GetEnum();
				}
			}
			else if (bIsStructRef && StructRefBaseStruct != nullptr)
			{
				if (const FStructProperty* SourceStructProperty = CastField<FStructProperty>(InProperty))
				{
					if (SourceStructProperty->Struct == TBaseStructure<FStateTreeStructRef>::Get())
					{
						FString SourceBaseStructName;
						const UScriptStruct* SourceDataRefBaseStruct = UE::StateTree::Compiler::GetBaseStructFromMetaData(SourceStructProperty, SourceBaseStructName);
						bCanBind = SourceDataRefBaseStruct && SourceDataRefBaseStruct->IsChildOf(StructRefBaseStruct);
					}
					else
					{
						bCanBind = SourceStructProperty->Struct && SourceStructProperty->Struct->IsChildOf(StructRefBaseStruct);
					}
				}
			}
			else
			{
				// Note: We support type promotion here
				bCanBind = FStateTreePropertyBindings::GetPropertyCompatibility(InProperty, InPropertyHandle->GetProperty()) != EStateTreePropertyAccessCompatibility::Incompatible;
			}

			return bCanBind;
		});

	Args.OnCanBindToContextStruct = FOnCanBindToContextStruct::CreateLambda([InPropertyHandle](const UStruct* InStruct)
		{
			// Do not allow to bind directly StateTree nodes
			if (InStruct != nullptr)
			{
				if (InStruct->IsChildOf(UStateTreeNodeBlueprintBase::StaticClass())
					|| InStruct->IsChildOf(FStateTreeNodeBase::StaticStruct()))
				{
					return false;
				}
			}
		
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
				if (TargetPath.GetStructID().IsValid() && InBindingChain.Num() > 0)
				{
					// First item in the binding chain is the index in AccessibleStructs.
					const int32 SourceStructIndex = InBindingChain[0].ArrayIndex;
						
					TArray<FBindingChainElement> SourceBindingChain = InBindingChain;
					SourceBindingChain.RemoveAt(0); // remove struct index.

					check(SourceStructIndex >= 0 && SourceStructIndex < AccessibleStructs.Num());

					// If SourceBindingChain is empty at this stage, it means that the binding points to the source struct itself.
					FStateTreePropertyPath SourcePath;
					UE::StateTree::PropertyBinding::MakeStructPropertyPathFromBindingChain(AccessibleStructs[SourceStructIndex].ID, SourceBindingChain, SourcePath);
						
					OwnerObject->Modify();
					EditorBindings->AddPropertyBinding(SourcePath, TargetPath);

					UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.Broadcast(SourcePath, TargetPath);
				}
			}
		});

	Args.OnRemoveBinding = FOnRemoveBinding::CreateLambda([EditorBindings, OwnerObject, TargetPath](FName InPropertyName)
		{
			if (EditorBindings && OwnerObject)
			{
				OwnerObject->Modify();
				EditorBindings->RemovePropertyBindings(TargetPath);

				const FStateTreePropertyPath SourcePath; // Null path
				UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.Broadcast(SourcePath, TargetPath);
			}
		});

	Args.OnCanRemoveBinding = FOnCanRemoveBinding::CreateLambda([EditorBindings, TargetPath](FName InPropertyName)
		{
			return EditorBindings && EditorBindings->HasPropertyBinding(TargetPath);
		});

	Args.CurrentBindingText = MakeAttributeLambda([EditorBindings, TargetPath, AccessibleStructs]()
		{
			FText CurrentValue = FText::GetEmpty();

			if (EditorBindings)
			{
				if (const FStateTreePropertyPath* SourcePath = EditorBindings->GetPropertyBindingSource(TargetPath))
				{
					FString PropertyName;
					for (int32 i = 0; i < AccessibleStructs.Num(); i++)
					{
						if (AccessibleStructs[i].ID == SourcePath->GetStructID())
						{
							PropertyName = AccessibleStructs[i].Name.ToString();
							break;
						}
					}
					if (!SourcePath->IsPathEmpty())
					{
						PropertyName += TEXT(".") + SourcePath->ToString();
					}
					CurrentValue = FText::FromString(PropertyName);
				}
			}

			return CurrentValue;
		});

	Args.CurrentBindingToolTipText = MakeAttributeLambda([EditorBindings, TargetPath, AccessibleStructs, Property]()
		{
			FText CurrentValue = FText::GetEmpty();

			if (EditorBindings)
			{
				if (const FStateTreePropertyPath* SourcePath = EditorBindings->GetPropertyBindingSource(TargetPath))
				{
					FString PropertyName;
					for (int32 i = 0; i < AccessibleStructs.Num(); i++)
					{
						if (AccessibleStructs[i].ID == SourcePath->GetStructID())
						{
							PropertyName = AccessibleStructs[i].Name.ToString();
							break;
						}
					}
					if (!SourcePath->IsPathEmpty())
					{
						PropertyName += TEXT(".") + SourcePath->ToString();
					}
					CurrentValue = FText::Format(LOCTEXT("ExistingBindingTooltip", "Property is bound to {0}."), FText::FromString(PropertyName));
				}
			}
			else if (Property != nullptr)
			{
				CurrentValue = FText::Format(LOCTEXT("BindTooltip", "Bind value to {0} property."), UE::StateTree::PropertyBinding::GetPropertyTypeText(Property));
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

	Args.BindButtonStyle = &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("HoverHintOnly");
	Args.bAllowNewBindings = false;
	Args.bAllowArrayElementBindings = false;
	Args.bAllowUObjectFunctions = false;

	InWidgetRow.ExtensionContent()
	[
		PropertyAccessEditor.MakePropertyBindingWidget(Context, Args)
	];
}

#undef LOCTEXT_NAMESPACE
