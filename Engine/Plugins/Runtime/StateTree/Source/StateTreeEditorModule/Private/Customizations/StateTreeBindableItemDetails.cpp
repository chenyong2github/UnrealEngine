// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeBindableItemDetails.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "InstancedStruct.h"
#include "Styling/SlateIconFinder.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "InstancedStructDetails.h"
#include "Engine/UserDefinedStruct.h"
#include "StateTreeBindingExtension.h"
#include "StateTreeDelegates.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeEditorStyle.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTreeEditor::Internal
{
	void ModifyRow(IDetailPropertyRow& ChildRow, const FGuid& ID, FStateTreeEditorPropertyBindings* EditorPropBindings)
	{
		if (!EditorPropBindings)
		{
			return;
		}
		
		TSharedPtr<IPropertyHandle> ChildPropHandle = ChildRow.GetPropertyHandle();
		const EStateTreePropertyUsage Category = UE::StateTree::PropertyBinding::ParsePropertyUsage(ChildPropHandle);
		const FProperty* Property = ChildPropHandle->GetProperty();
		
		// Conditionally control visibility of the value field of bound properties.
		if (Category != EStateTreePropertyUsage::Invalid && ID.IsValid())
		{
			// Pass the item ID to binding extension. Since the properties are added using AddChildStructure(), we break the hierarchy and cannot access parent.
			ChildPropHandle->SetInstanceMetaData(FName(TEXT("StateTreeItemID")), LexToString(ID));

			FStateTreeEditorPropertyPath Path(ID, *Property->GetFName().ToString());
			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			FDetailWidgetRow Row;
			ChildRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

			auto IsValueVisible = TAttribute<EVisibility>::Create([Path, EditorPropBindings]() -> EVisibility
				{
					return EditorPropBindings->HasPropertyBinding(Path) ? EVisibility::Collapsed : EVisibility::Visible;
				});

			if (Category == EStateTreePropertyUsage::Input || Category == EStateTreePropertyUsage::Output)
			{
				FEdGraphPinType PinType;
				const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
				Schema->ConvertPropertyToPinType(Property, PinType);
				const FSlateBrush* Icon = FBlueprintEditorUtils::GetIconFromPin(PinType, true);
				FText Label = Category == EStateTreePropertyUsage::Input ? LOCTEXT("Input", "Input") : LOCTEXT("Output", "Output");
				FText Tooltip = FText::FromString(Property->GetCPPType());
				const FLinearColor Color = Schema->GetPinTypeColor(PinType);

				ChildRow
					.CustomWidget(/*bShowChildren*/false)
					.NameContent()
					[
						NameWidget.ToSharedRef()
					]
					.ValueContent()
					[
						SNew(SHorizontalBox)
						.Visibility(IsValueVisible)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
						[
							SNew(SImage)
							.Image(Icon)
							.ColorAndOpacity(Color)
						]
						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.Text(Label)
							.ToolTipText(Tooltip)
						]
					];
			}
			else
			{
				ChildRow
					.CustomWidget(/*bShowChildren*/true)
					.NameContent()
					[
						NameWidget.ToSharedRef()
					]
					.ValueContent()
					[
						SNew(SBox)
						.Visibility(IsValueVisible)
						[
							ValueWidget.ToSharedRef()
						]
					];
			}
		}
	}
} // UE::StateTreeEditor::Internal

// Customized version of FInstancedStructDataDetails used to hide bindable properties.
class FBindableItemInstanceDetails : public FInstancedStructDataDetails
{
public:

	FBindableItemInstanceDetails(TSharedPtr<IPropertyHandle> InStructProperty, FGuid InID, UStateTreeEditorData* InEditorData)
		: FInstancedStructDataDetails(InStructProperty)
		, EditorData(InEditorData)
	{
		EditorPropBindings = EditorData ? EditorData->GetPropertyEditorBindings() : nullptr;
		ID = InID;
	}

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow)
	{
		UE::StateTreeEditor::Internal::ModifyRow(ChildRow, ID, EditorPropBindings);
	}

	UStateTreeEditorData* EditorData;
	FStateTreeEditorPropertyBindings* EditorPropBindings;
	FGuid ID;
};

////////////////////////////////////

class FBindableItemStructFilter : public IStructViewerFilter
{
public:
	/** The base struct for the property that classes must be a child-of. */
	const UScriptStruct* BaseStruct = nullptr;

	// A flag controlling whether we allow UserDefinedStructs
	bool bAllowUserDefinedStructs = false;

	// A flag controlling whether we allow to select the BaseStruct
	bool bAllowBaseStruct = true;

	// Schema to filter which structs are allowed.
	const UStateTreeSchema* Schema = nullptr;

	virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		if (!Schema)
		{
			return false;
		}

		if (!Schema->IsStructAllowed(InStruct))
		{
			return false;
		}
		
		if (InStruct->IsA<UUserDefinedStruct>())
		{
			return bAllowUserDefinedStructs;
		}

		if (InStruct == BaseStruct)
		{
			return bAllowBaseStruct;
		}

		// Query the native struct to see if it has the correct parent type (if any)
		return BaseStruct != nullptr && InStruct->IsChildOf(BaseStruct);
	}

	virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FName InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		// User Defined Structs don't support inheritance, so only include them requested
		return bAllowUserDefinedStructs;
	}
};

////////////////////////////////////

class FBindableItemClassFilter : public IClassViewerFilter
{
public:
	/** The base struct for the property that classes must be a child-of. */
	const UClass* BaseClass = nullptr;

	// A flag controlling whether we allow to select the BaseStruct
	bool bAllowBaseClass = true;

	// Schema to filter which structs are allowed.
	const UStateTreeSchema* Schema = nullptr;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs ) override
	{
		if (!Schema || !InClass)
		{
			return false;
		}

		if (!Schema->IsClassAllowed(InClass))
		{
			return false;
		}
		
		if (InClass == BaseClass)
		{
			return bAllowBaseClass;
		}

		// Query the native struct to see if it has the correct parent type (if any)
		return BaseClass != nullptr && InClass->IsChildOf(BaseClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		if (!Schema)
		{
			return false;
		}

		if (InUnloadedClassData->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract))
		{
			return false;
		}

		const UClass* NativeParentClass = InUnloadedClassData->GetNativeParent();

		if (!Schema->IsClassAllowed(NativeParentClass))
		{
			return false;
		}
		
		return NativeParentClass->IsChildOf(BaseClass);
	}
};

////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FStateTreeBindableItemDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeBindableItemDetails);
}

FStateTreeBindableItemDetails::~FStateTreeBindableItemDetails()
{
	UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.Remove(OnBindingChangedHandle);
}

void FStateTreeBindableItemDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	ItemProperty = StructProperty->GetChildHandle(TEXT("Item"));
	InstanceProperty = StructProperty->GetChildHandle(TEXT("Instance"));
	InstanceObjectProperty = StructProperty->GetChildHandle(TEXT("InstanceObject"));
	IDProperty = StructProperty->GetChildHandle(TEXT("ID"));

	check(ItemProperty.IsValid());
	check(InstanceProperty.IsValid());
	check(IDProperty.IsValid());
	
	// Find base class and struct from meta data.
	static const FName BaseStructMetaName(TEXT("BaseStruct")); // TODO: move these names into one central place.
	static const FName BaseClassMetaName(TEXT("BaseClass")); // TODO: move these names into one central place.
	
	const FString BaseStructName = StructProperty->GetMetaData(BaseStructMetaName);
	BaseScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseStructName);

	const FString BaseClassName = StructProperty->GetMetaData(BaseClassMetaName);
	BaseClass = FindObject<UClass>(ANY_PACKAGE, *BaseClassName);
	
	const FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FStateTreeBindableItemDetails::ShouldResetToDefault);
	const FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FStateTreeBindableItemDetails::ResetToDefault);
	const FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeBindableItemDetails::OnIdentifierChanged);
	OnBindingChangedHandle = UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.AddRaw(this, &FStateTreeBindableItemDetails::OnBindingChanged);

	FindOuterObjects();

	HeaderRow
		.WholeRowContent()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Description (Condition)
			+ SHorizontalBox::Slot()
			.FillWidth(3.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SRichTextBlock)
				.DecoratorStyleSet(FStateTreeEditorStyle::Get().Get())
				.TextStyle(FStateTreeEditorStyle::Get(), "Details.Normal")
				.Text(this, &FStateTreeBindableItemDetails::GetDescription)
				.Visibility(this, &FStateTreeBindableItemDetails::IsDescriptionVisible)
			]
			// Name (Eval/Task)
			+ SHorizontalBox::Slot()
			.FillWidth(1.5f)
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.IsEnabled(TAttribute<bool>(this, &FStateTreeBindableItemDetails::IsNameEnabled))
				.Text(this, &FStateTreeBindableItemDetails::GetName)
				.OnTextCommitted(this, &FStateTreeBindableItemDetails::OnNameCommitted)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Visibility(this, &FStateTreeBindableItemDetails::IsNameVisible)
			]
			// Class picker
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(FMargin(4.0f, 0.0f, 0.0f, 0.0f)))
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboButton, SComboButton)
				.OnGetMenuContent(this, &FStateTreeBindableItemDetails::GeneratePicker)
				.ContentPadding(0.f)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SImage)
						.Image(this, &FStateTreeBindableItemDetails::GetDisplayValueIcon)
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &FStateTreeBindableItemDetails::GetDisplayValueString)
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				StructPropertyHandle->CreateDefaultPropertyButtonWidgets()
			]
		]
		.OverrideResetToDefault(ResetOverride);
}

bool FStateTreeBindableItemDetails::ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	check(StructProperty);
	
	bool bAnyValid = false;
	
	TArray<const void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);
	for (const void* Data : RawItemData)
	{
		if (const FStateTreeItem* Item = static_cast<const FStateTreeItem*>(Data))
		{
			if (Item->Item.IsValid())
			{
				bAnyValid = true;
				break;
			}
		}
	}
	
	// Assume that the default value is empty. Any valid means that some can be reset to empty.
	return bAnyValid;
}


void FStateTreeBindableItemDetails::ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	check(StructProperty);
	
	GEditor->BeginTransaction(LOCTEXT("OnResetToDefault", "Reset to default"));

	StructProperty->NotifyPreChange();
	
	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);
	for (void* Data : RawItemData)
	{
		if (FStateTreeItem* Item = static_cast<FStateTreeItem*>(Data))
		{
			Item->Reset();
		}
	}

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeBindableItemDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FStateTreeEditorPropertyBindings* EditorPropBindings = EditorData ? EditorData->GetPropertyEditorBindings() : nullptr;

	FGuid ID;
	UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);

	TSharedRef<FBindableItemInstanceDetails> ItemDetails = MakeShareable(new FBindableItemInstanceDetails(ItemProperty, FGuid(), EditorData));
	StructBuilder.AddCustomBuilder(ItemDetails);

	// Instance
	TSharedRef<FBindableItemInstanceDetails> InstanceDetails = MakeShareable(new FBindableItemInstanceDetails(InstanceProperty, ID, EditorData));
	StructBuilder.AddCustomBuilder(InstanceDetails);

	// InstanceObject
	// Get the actual UObject from the pointer.
	TSharedPtr<IPropertyHandle> InstanceObjectValueProperty = GetInstancedObjectValueHandle(InstanceObjectProperty);
	if (InstanceObjectValueProperty.IsValid())
	{
		static const FName CategoryName(TEXT("Category"));

		uint32 NumChildren = 0;
		InstanceObjectValueProperty->GetNumChildren(NumChildren);
		for (uint32 Index = 0; Index < NumChildren; Index++)
		{
			TSharedPtr<IPropertyHandle> Child = InstanceObjectValueProperty->GetChildHandle(Index);
			
			if (!Child.IsValid()
				|| Child->GetProperty()->HasAllPropertyFlags(CPF_DisableEditOnInstance)
				|| Child->GetMetaData(CategoryName) == TEXT("ExternalData"))
			{
				continue;
			}
			
			IDetailPropertyRow& ChildRow = StructBuilder.AddProperty(Child.ToSharedRef());
			UE::StateTreeEditor::Internal::ModifyRow(ChildRow, ID, EditorPropBindings);
		}
	}
}

TSharedPtr<IPropertyHandle> FStateTreeBindableItemDetails::GetInstancedObjectValueHandle(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	TSharedPtr<IPropertyHandle> ChildHandle;

	uint32 NumChildren;
	PropertyHandle->GetNumChildren(NumChildren);

	if (NumChildren > 0)
	{
		// when the property is a (inlined) object property, the first child will be
		// the object instance, and its properties are the children underneath that
		ensure(NumChildren == 1);
		ChildHandle = PropertyHandle->GetChildHandle(0);
	}

	return ChildHandle;
}

void FStateTreeBindableItemDetails::OnIdentifierChanged(const UStateTree& InStateTree)
{
	if (PropUtils && StateTree == &InStateTree)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeBindableItemDetails::OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath)
{
	check(StructProperty);

	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);

	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);

	if (OuterObjects.Num() != RawItemData.Num())
	{
		return;
	}

	for (int32 i = 0; i < OuterObjects.Num(); i++)
	{
		const FStateTreeItem* Item = static_cast<FStateTreeItem*>(RawItemData[i]);
		UObject* OuterObject = OuterObjects[i]; // Immediate outer, i.e StateTreeState
		if (Item != nullptr && EditorData != nullptr && Item->Item.IsValid() && Item->Instance.IsValid())
		{
			if (Item->ID == TargetPath.StructID)
			{
				if (FStateTreeConditionBase* Condition = Item->Item.GetMutablePtr<FStateTreeConditionBase>())
				{
					const FStateTreeBindingLookup BindingLookup(EditorData);

					OuterObject->Modify();
					Condition->OnBindingChanged(Item->ID, Item->Instance, SourcePath, TargetPath, BindingLookup);
				}
			}
		}
	}
}

void FStateTreeBindableItemDetails::FindOuterObjects()
{
	check(StructProperty);
	
	EditorData = nullptr;
	StateTree = nullptr;

	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);
	for (UObject* Outer : OuterObjects)
	{
		UStateTreeEditorData* OuterEditorData = Outer->GetTypedOuter<UStateTreeEditorData>();
		UStateTree* OuterStateTree = OuterEditorData ? OuterEditorData->GetTypedOuter<UStateTree>() : nullptr;
		if (OuterEditorData && OuterStateTree)
		{
			StateTree = OuterStateTree;
			EditorData = OuterEditorData;
			break;
		}
	}
}

FText FStateTreeBindableItemDetails::GetDescription() const
{
	check(StructProperty);

	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);

	// Multiple descriptions do not make sense, just if only one item is selected.
	if (RawItemData.Num() != 1)
	{
		return LOCTEXT("MultipleSelected", "<Details.Subdued>Multiple selected</>");
	}

	FStateTreeItem* Item = static_cast<FStateTreeItem*>(RawItemData[0]);
	if (Item != nullptr && EditorData != nullptr && Item->Item.IsValid() && Item->Instance.IsValid())
	{
		if (const FStateTreeConditionBase* Condition = Item->Item.GetPtr<const FStateTreeConditionBase>())
		{
			const FStateTreeBindingLookup BindingLookup(EditorData);
			return Condition->GetDescription(Item->ID, Item->Instance, BindingLookup);
		}
	}
	
	return LOCTEXT("ConditionNotSet", "<Details.Subdued>Condition Not Set</>");
}

EVisibility FStateTreeBindableItemDetails::IsDescriptionVisible() const
{
	const UScriptStruct* ScriptStruct = GetCommonItemScriptStruct();
	return ScriptStruct != nullptr && ScriptStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FStateTreeBindableItemDetails::GetName() const
{
	check(StructProperty);

	// Multiple names do not make sense, just if only one item is selected.
	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);
	if (RawItemData.Num() == 1)
	{
		// Dig out name from the struct without knowing the type.
		FStateTreeItem* Item = static_cast<FStateTreeItem*>(RawItemData[0]);

		// Make sure the associated property has valid data (e.g. while moving an item in array)
		const UScriptStruct* ScriptStruct = Item != nullptr ? Item->Item.GetScriptStruct() : nullptr;
		if (ScriptStruct != nullptr)
		{
			if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
			{
				if (NameProperty->IsA(FNameProperty::StaticClass()))
				{
					void* Ptr = const_cast<void*>(static_cast<const void*>(Item->Item.GetMemory()));
					const FName NameValue = *NameProperty->ContainerPtrToValuePtr<FName>(Ptr);
					return FText::FromName(NameValue);
				}
			}
		}
		return LOCTEXT("Empty", "Empty");
	}

	return LOCTEXT("MultipleSelected", "Multiple selected");
}

EVisibility FStateTreeBindableItemDetails::IsNameVisible() const
{
	const UScriptStruct* ScriptStruct = GetCommonItemScriptStruct();
	return ScriptStruct != nullptr && (ScriptStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()) || ScriptStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct())) ? EVisibility::Visible : EVisibility::Collapsed;
}

void FStateTreeBindableItemDetails::OnNameCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	check(StructProperty);

	if (InTextCommit == ETextCommit::OnEnter || InTextCommit == ETextCommit::OnUserMovedFocus)
	{
		// Remove excess whitespace and prevent categories with just spaces
		FString NewName = FText::TrimPrecedingAndTrailing(NewText).ToString();

		if (GEditor)
		{
			GEditor->BeginTransaction(LOCTEXT("SetName", "Set Name"));
		}
		StructProperty->NotifyPreChange();

		TArray<void*> RawItemData;
		StructProperty->AccessRawData(RawItemData);
		
		for (void* Data : RawItemData)
		{
			if (FStateTreeItem* Item = static_cast<FStateTreeItem*>(Data))
			{
				// Set Name
				if (const UScriptStruct* ScriptStruct = Item->Item.GetScriptStruct())
				{
					if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
					{
						if (NameProperty->IsA(FNameProperty::StaticClass()))
						{
							void* Ptr = const_cast<void*>(static_cast<const void*>(Item->Item.GetMemory()));
							FName& NameValue = *NameProperty->ContainerPtrToValuePtr<FName>(Ptr);
							NameValue = FName(NewName);
						}
					}
				}
			}
		}

		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);

		if (StateTree)
		{
			UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
		}

		if (GEditor)
		{
			GEditor->EndTransaction();
		}

		StructProperty->NotifyFinishedChangingProperties();
	}
}

bool FStateTreeBindableItemDetails::IsNameEnabled() const
{
	// Can only edit if we have valid instantiated type.
	return GetCommonItemScriptStruct() != nullptr;
}

const UScriptStruct* FStateTreeBindableItemDetails::GetCommonItemScriptStruct() const
{
	check(StructProperty);

	const UScriptStruct* CommonScriptStruct = nullptr;
	bool bMultipleValues = false;
	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);

	for (void* Data : RawItemData)
	{
		if (FStateTreeItem* Item = static_cast<FStateTreeItem*>(Data))
		{
			const UScriptStruct* ScriptStruct = Item->Item.GetScriptStruct();
			if (!bMultipleValues && !CommonScriptStruct)
			{
				CommonScriptStruct = ScriptStruct;
			}
			else if (ScriptStruct != CommonScriptStruct)
			{
				CommonScriptStruct = nullptr;
				bMultipleValues = true;
			}
		}
	}

	return CommonScriptStruct;
}

FText FStateTreeBindableItemDetails::GetDisplayValueString() const
{
	const UScriptStruct* ScriptStruct = GetCommonItemScriptStruct();
	if (ScriptStruct)
	{
		return ScriptStruct->GetDisplayNameText();
	}
	return FText();
}

const FSlateBrush* FStateTreeBindableItemDetails::GetDisplayValueIcon() const
{
	return FSlateIconFinder::FindIconBrushForClass(UScriptStruct::StaticClass());
}

TSharedRef<SWidget> FStateTreeBindableItemDetails::GeneratePicker()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// @todo: Find a way to combine the menus. Now both are displayed even if one may be empty.
	
	MenuBuilder.AddWrapperSubMenu(
		LOCTEXT("Items", "Items"),
		LOCTEXT("Items_ToolTip", "Items"),
		FOnGetContent::CreateSP(this, &FStateTreeBindableItemDetails::GenerateStructPicker),
		FSlateIcon()
		);

	MenuBuilder.AddWrapperSubMenu(
		LOCTEXT("BlueprintItems", "Blueprint Items"),
		LOCTEXT("BlueprintItems_ToolTip", "Blueprint Items"),
		FOnGetContent::CreateSP(this, &FStateTreeBindableItemDetails::GenerateClassPicker),
		FSlateIcon()
	);

	MenuBuilder.AddMenuSeparator();

	FUIAction ClearAction(FExecuteAction::CreateSP(this, &FStateTreeBindableItemDetails::OnStructPicked, (const UScriptStruct*)nullptr));
	MenuBuilder.AddMenuEntry(LOCTEXT("ClearItem", "Clear Item"), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "Cross"), ClearAction);
	
	return MenuBuilder.MakeWidget();
}



TSharedRef<SWidget> FStateTreeBindableItemDetails::GenerateStructPicker()
{
	TSharedRef<FBindableItemStructFilter> StructFilter = MakeShared<FBindableItemStructFilter>();
	StructFilter->BaseStruct = BaseScriptStruct;
	StructFilter->bAllowUserDefinedStructs = false;
	StructFilter->bAllowBaseStruct = false;
	StructFilter->Schema = StateTree ? StateTree->GetSchema() : nullptr;

	FStructViewerInitializationOptions Options;
	Options.bShowUnloadedStructs = true;
	Options.bShowNoneOption = true;
	Options.StructFilter = StructFilter;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EStructViewerDisplayMode::ListView;
	Options.bAllowViewOptions = true;

	FOnStructPicked OnPicked(FOnStructPicked::CreateRaw(this, &FStateTreeBindableItemDetails::OnStructPicked));
	
	return SNew(SBox)
		.WidthOverride(280.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(500.f)
			[
				FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer").CreateStructViewer(Options, OnPicked)
			]
		];
}

void FStateTreeBindableItemDetails::OnStructPicked(const UScriptStruct* InStruct)
{
	check(StructProperty);
	check(StateTree);

	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);

	GEditor->BeginTransaction(LOCTEXT("OnStructPicked", "Set item"));

	StructProperty->NotifyPreChange();

	for (void* Data : RawItemData)
	{
		if (FStateTreeItem* Item = static_cast<FStateTreeItem*>(Data))
		{
		
			Item->Reset();
			
			if (InStruct)
			{
				// Generate new ID.
				Item->ID = FGuid::NewGuid();

				// Initialize item
				Item->Item.InitializeAs(InStruct);
				
				// Generate new name and instantiate instance data.
				if (InStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
				{
					FStateTreeTaskBase& Task = Item->Item.GetMutable<FStateTreeTaskBase>();
					Task.Name = FName(InStruct->GetDisplayNameText().ToString());

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Task.GetInstanceDataType()))
					{
						Item->Instance.InitializeAs(InstanceType);
					}
					else if (const UClass* InstanceClass = Cast<const UClass>(Task.GetInstanceDataType()))
					{
						Item->InstanceObject = NewObject<UObject>(EditorData, InstanceClass);
					}
				}
				else if (InStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
				{
					FStateTreeEvaluatorBase& Eval = Item->Item.GetMutable<FStateTreeEvaluatorBase>();
					Eval.Name = FName(InStruct->GetDisplayNameText().ToString());

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Eval.GetInstanceDataType()))
					{
						Item->Instance.InitializeAs(InstanceType);
					}
					else if (const UClass* InstanceClass = Cast<const UClass>(Eval.GetInstanceDataType()))
					{
						Item->InstanceObject = NewObject<UObject>(EditorData, InstanceClass);
					}
				}
				else if (InStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
				{
					FStateTreeConditionBase& Cond = Item->Item.GetMutable<FStateTreeConditionBase>();

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond.GetInstanceDataType()))
					{
						Item->Instance.InitializeAs(InstanceType);
					}
					else if (const UClass* InstanceClass = Cast<const UClass>(Cond.GetInstanceDataType()))
					{
						Item->InstanceObject = NewObject<UObject>(EditorData, InstanceClass);
					}
				}
			}
		}
	}

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	ComboButton->SetIsOpen(false);

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}


TSharedRef<SWidget> FStateTreeBindableItemDetails::GenerateClassPicker()
{
	TSharedRef<FBindableItemClassFilter> ClassFilter = MakeShared<FBindableItemClassFilter>();
	ClassFilter->BaseClass = BaseClass;
	ClassFilter->bAllowBaseClass = false;
	ClassFilter->Schema = StateTree ? StateTree->GetSchema() : nullptr;

	FClassViewerInitializationOptions Options;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = true;
	Options.ClassFilters.Add(ClassFilter);
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EClassViewerDisplayMode::ListView;
	Options.bAllowViewOptions = true;

	FOnClassPicked OnPicked(FOnClassPicked::CreateRaw(this, &FStateTreeBindableItemDetails::OnClassPicked));

	return SNew(SBox)
		.WidthOverride(280.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(500.f)
			[
				FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options, OnPicked)
			]
		];
}

void FStateTreeBindableItemDetails::OnClassPicked(UClass* InClass)
{
	check(StructProperty);
	check(StateTree);

	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);

	GEditor->BeginTransaction(LOCTEXT("OnClassPicked", "Set Blueprint item"));

	StructProperty->NotifyPreChange();

	for (void* Data : RawItemData)
	{
		if (FStateTreeItem* Item = static_cast<FStateTreeItem*>(Data))
		{
			Item->Reset();

			if (InClass && InClass->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()))
			{
				Item->Item.InitializeAs(FStateTreeBlueprintTaskWrapper::StaticStruct());
				FStateTreeBlueprintTaskWrapper& Task = Item->Item.GetMutable<FStateTreeBlueprintTaskWrapper>();
				Task.TaskClass = InClass;
				Task.Name = FName(InClass->GetDisplayNameText().ToString());
				
				Item->InstanceObject = NewObject<UObject>(EditorData, InClass);

				Item->ID = FGuid::NewGuid();
			}
			else if (InClass && InClass->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()))
			{
				Item->Item.InitializeAs(FStateTreeBlueprintEvaluatorWrapper::StaticStruct());
				FStateTreeBlueprintEvaluatorWrapper& Eval = Item->Item.GetMutable<FStateTreeBlueprintEvaluatorWrapper>();
				Eval.EvaluatorClass = InClass;
				Eval.Name = FName(InClass->GetDisplayNameText().ToString());
				
				Item->InstanceObject = NewObject<UObject>(EditorData, InClass);

				Item->ID = FGuid::NewGuid();
			}
			else if (InClass && InClass->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()))
			{
				Item->Item.InitializeAs(FStateTreeBlueprintConditionWrapper::StaticStruct());
				FStateTreeBlueprintConditionWrapper& Cond = Item->Item.GetMutable<FStateTreeBlueprintConditionWrapper>();
				Cond.ConditionClass = InClass;
				
				Item->InstanceObject = NewObject<UObject>(EditorData, InClass);

				Item->ID = FGuid::NewGuid();
			}

			
		}
	}

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	ComboButton->SetIsOpen(false);

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}


#undef LOCTEXT_NAMESPACE
