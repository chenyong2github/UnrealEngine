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
#include "StateTreeEditorModule.h"
#include "StateTreeNodeClassCache.h"

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
					Condition->OnBindingChanged(Item->ID, FStateTreeDataView(Item->Instance), SourcePath, TargetPath, BindingLookup);
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
	if (Item != nullptr && EditorData != nullptr && Item->Item.IsValid())
	{
		if (const FStateTreeConditionBase* Condition = Item->Item.GetPtr<const FStateTreeConditionBase>())
		{
			const FStateTreeBindingLookup BindingLookup(EditorData);
			if (Item->Instance.IsValid())
			{
				return Condition->GetDescription(Item->ID, FStateTreeDataView(Item->Instance), BindingLookup);
			}
			else if (Item->InstanceObject != nullptr)
			{
				return Condition->GetDescription(Item->ID, FStateTreeDataView(Item->InstanceObject), BindingLookup);
			}
			else
			{
				return Item->Item.GetScriptStruct()->GetDisplayNameText();
			}
		}
	}
	
	return LOCTEXT("ConditionNotSet", "<Details.Subdued>Condition Not Set</>");
}

EVisibility FStateTreeBindableItemDetails::IsDescriptionVisible() const
{
	const UScriptStruct* ScriptStruct = nullptr;
	if (const FStateTreeItem* Item = GetCommonItem())
	{
		ScriptStruct = Item->Item.GetScriptStruct();
	}

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
	const UStateTreeSchema* Schema = StateTree ? StateTree->GetSchema() : nullptr;
	if (Schema && Schema->AllowMultipleTasks() == false)
	{
		// Single task states use the state name as task name.
		return EVisibility::Collapsed;
	}
	
	const UScriptStruct* ScriptStruct = nullptr;
	if (const FStateTreeItem* Item = GetCommonItem())
	{
		ScriptStruct = Item->Item.GetScriptStruct();
	}
	
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
	const FStateTreeItem* Item = GetCommonItem();
	return Item && Item->Item.IsValid();
}

const FStateTreeItem* FStateTreeBindableItemDetails::GetCommonItem() const
{
	check(StructProperty);

	const UScriptStruct* CommonScriptStruct = nullptr;
	bool bMultipleValues = false;
	TArray<void*> RawItemData;
	StructProperty->AccessRawData(RawItemData);

	const FStateTreeItem* CommonItem = nullptr;
	
	for (void* Data : RawItemData)
	{
		if (const FStateTreeItem* Item = static_cast<FStateTreeItem*>(Data))
		{
			if (!bMultipleValues && !CommonItem)
			{
				CommonItem = Item;
			}
			else if (CommonItem != Item)
			{
				CommonItem = nullptr;
				bMultipleValues = true;
			}
		}
	}

	return CommonItem;
}

FText FStateTreeBindableItemDetails::GetDisplayValueString() const
{
	if (const FStateTreeItem* Item = GetCommonItem())
	{
		if (const UScriptStruct* ScriptStruct = Item->Item.GetScriptStruct())
		{
			if (ScriptStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct()))
			{
				if (Item->InstanceObject != nullptr
					&& Item->InstanceObject->GetClass() != nullptr)
				{
					return Item->InstanceObject->GetClass()->GetDisplayNameText();
				}
			}
			else
			{
				return ScriptStruct->GetDisplayNameText();
			}
		}
	}
	return FText();
}

const FSlateBrush* FStateTreeBindableItemDetails::GetDisplayValueIcon() const
{
	if (const FStateTreeItem* Item = GetCommonItem())
	{
		if (const UScriptStruct* ScriptStruct = Item->Item.GetScriptStruct())
		{
			if (ScriptStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct()))
			{
				if (Item->InstanceObject != nullptr)
				{
					return FSlateIconFinder::FindIconBrushForClass(Item->InstanceObject->GetClass());
				}
			}
		}
	}
	
	return FSlateIconFinder::FindIconBrushForClass(UScriptStruct::StaticClass());
}

TSharedRef<SWidget> FStateTreeBindableItemDetails::GeneratePicker()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	FStateTreeEditorModule& EditorModule = FModuleManager::GetModuleChecked<FStateTreeEditorModule>(TEXT("StateTreeEditorModule"));
	FStateTreeNodeClassCache* ClassCache = EditorModule.GetNodeClassCache().Get();
	check(ClassCache);

	FUIAction ClearAction(FExecuteAction::CreateSP(this, &FStateTreeBindableItemDetails::OnStructPicked, (const UScriptStruct*)nullptr));
	MenuBuilder.AddMenuEntry(LOCTEXT("ClearItem", "Clear"), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "Cross"), ClearAction);

	MenuBuilder.AddMenuSeparator();

	TArray<TSharedPtr<FStateTreeNodeClassData>> StructItems;
	TArray<TSharedPtr<FStateTreeNodeClassData>> ObjectItems;
	
	ClassCache->GetScripStructs(BaseScriptStruct, StructItems);
	ClassCache->GetClasses(BaseClass, ObjectItems);

	const FSlateIcon ScripStructIcon = FSlateIconFinder::FindIconForClass(UScriptStruct::StaticClass());
	const UStateTreeSchema* Schema = StateTree ? StateTree->GetSchema() : nullptr;
	
	for (const TSharedPtr<FStateTreeNodeClassData>& Data : StructItems)
	{
		if (Data->GetScriptStruct() != nullptr)
		{
			const UScriptStruct* ScriptStruct = Data->GetScriptStruct();
			if (ScriptStruct == BaseScriptStruct)
			{
				continue;
			}
			if (ScriptStruct->HasMetaData(TEXT("Hidden")))
			{
				continue;				
			}
			if (Schema && !Schema->IsStructAllowed(ScriptStruct))
			{
				continue;				
			}
			
			FUIAction ItemAction(FExecuteAction::CreateSP(this, &FStateTreeBindableItemDetails::OnStructPicked, ScriptStruct));
			MenuBuilder.AddMenuEntry(ScriptStruct->GetDisplayNameText(), TAttribute<FText>(), ScripStructIcon, ItemAction);
		}
	}

	if (StructItems.Num() > 0 && ObjectItems.Num() > 0)
	{
		MenuBuilder.AddMenuSeparator();
	}
	
	for (const TSharedPtr<FStateTreeNodeClassData>& Data : ObjectItems)
	{
		if (Data->GetClass() != nullptr)
		{
			UClass* Class = Data->GetClass();
			if (Class == BaseClass)
			{
				continue;
			}
			if (Class->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown))
			{
				continue;				
			}
			if (Class->HasMetaData(TEXT("Hidden")))
			{
				continue;				
			}
			if (Schema && !Schema->IsClassAllowed(Class))
			{
				continue;				
			}

			FUIAction ItemAction(FExecuteAction::CreateSP(this, &FStateTreeBindableItemDetails::OnClassPicked, Class));
			MenuBuilder.AddMenuEntry(Class->GetDisplayNameText(), TAttribute<FText>(), FSlateIconFinder::FindIconForClass(Data->GetClass()), ItemAction);
		}
	}

	return MenuBuilder.MakeWidget();
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
