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

#define LOCTEXT_NAMESPACE "StateTreeEditor"

// Customized version of FInstancedStructDataDetails used to hide bindable properties.
class FBindableItemDataDetails : public FInstancedStructDataDetails
{
public:

	FBindableItemDataDetails(TSharedPtr<IPropertyHandle> InStructProperty, FGuid InID, UStateTreeEditorData* InEditorData)
		: FInstancedStructDataDetails(InStructProperty)
		, EditorData(InEditorData)
	{
		EditorPropBindings = EditorData ? EditorData->GetPropertyEditorBindings() : nullptr;
		ID = InID;
	}

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow)
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

			auto IsValueVisible = TAttribute<EVisibility>::Create([this, Path]() -> EVisibility
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
		return !BaseStruct || InStruct->IsChildOf(BaseStruct);
	}

	virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FName InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		// User Defined Structs don't support inheritance, so only include them requested
		return bAllowUserDefinedStructs;
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
	IDProperty = StructProperty->GetChildHandle(TEXT("ID"));

	check(ItemProperty.IsValid());
	check(InstanceProperty.IsValid());
	check(IDProperty.IsValid());
	
	// Find base class from meta data.
	static const FName BaseClassMetaName(TEXT("BaseStruct")); // TODO: move these names into one central place.
	const FString BaseClassName = ItemProperty->GetMetaData(BaseClassMetaName);
	BaseScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseClassName);

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
				.OnGetMenuContent(this, &FStateTreeBindableItemDetails::GenerateStructPicker)
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
	check(ItemProperty);
	
	bool bAnyValid = false;
	
	TArray<void*> RawData;
	ItemProperty->AccessRawData(RawData);
	for (void* Data : RawData)
	{
		if (const FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			if (Struct->IsValid())
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
	check(ItemProperty);
	check(InstanceProperty);
	
	GEditor->BeginTransaction(LOCTEXT("OnResetToDefault", "Reset to default"));

	ItemProperty->NotifyPreChange();
	TArray<void*> RawItemData;
	ItemProperty->AccessRawData(RawItemData);
	for (void* Data : RawItemData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			// Assume that the default value is empty.
			Struct->Reset();
		}
	}

	InstanceProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	InstanceProperty->NotifyFinishedChangingProperties();

	InstanceProperty->NotifyPreChange();
	TArray<void*> RawInstanceData;
	InstanceProperty->AccessRawData(RawInstanceData);
	for (void* Data : RawInstanceData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			// Assume that the default value is empty.
			Struct->Reset();
		}
	}

	InstanceProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	InstanceProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeBindableItemDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FGuid ID;
	UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);

	TSharedRef<FBindableItemDataDetails> ItemDetails = MakeShareable(new FBindableItemDataDetails(ItemProperty, FGuid(), EditorData));
	StructBuilder.AddCustomBuilder(ItemDetails);

	TSharedRef<FBindableItemDataDetails> InstanceDetails = MakeShareable(new FBindableItemDataDetails(InstanceProperty, ID, EditorData));
	StructBuilder.AddCustomBuilder(InstanceDetails);
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
	check(ItemProperty);

	FGuid ID;
	if (UE::StateTree::PropertyHelpers::GetStructValue(IDProperty, ID) != FPropertyAccess::Success)
	{
		return;
	}

	if (ID != TargetPath.StructID)
	{
		return;
	}

	TArray<UObject*> OuterObjects;
	ItemProperty->GetOuterObjects(OuterObjects);

	TArray<void*> RawItemData;
	ItemProperty->AccessRawData(RawItemData);

	TArray<void*> RawInstanceData;
	InstanceProperty->AccessRawData(RawInstanceData);

	if (OuterObjects.Num() != RawItemData.Num() || OuterObjects.Num() != RawInstanceData.Num())
	{
		return;
	}

	for (int32 i = 0; i < OuterObjects.Num(); i++)
	{
		// Dig out name from the struct without knowing the type.
		const FInstancedStruct* ItemPtr = static_cast<FInstancedStruct*>(RawItemData[i]);
		const FInstancedStruct* InstancePtr = static_cast<FInstancedStruct*>(RawInstanceData[i]);
		UObject* OuterObject = OuterObjects[i]; // Immediate outer, i.e StateTreeState

		if (InstancePtr->IsValid() && EditorData != nullptr)
		{
			if (FStateTreeConditionBase* Condition = ItemPtr->GetMutablePtr<FStateTreeConditionBase>())
			{
				const FStateTreeBindingLookup BindingLookup(EditorData);

				OuterObject->Modify();
				Condition->OnBindingChanged(ID, *InstancePtr, SourcePath, TargetPath, BindingLookup);
			}
		}
	}
}

void FStateTreeBindableItemDetails::FindOuterObjects()
{
	check(ItemProperty);
	
	EditorData = nullptr;
	StateTree = nullptr;

	TArray<UObject*> OuterObjects;
	ItemProperty->GetOuterObjects(OuterObjects);
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
	check(ItemProperty);
	check(InstanceProperty);

	// Multiple descriptions do not make sense, just if only one item is selected.
	if (ItemProperty->GetNumOuterObjects() != 1)
	{
		return LOCTEXT("MultipleSelected", "<Details.Subdued>Multiple selected</>");
	}

	FGuid ID;
	if (UE::StateTree::PropertyHelpers::GetStructValue(IDProperty, ID) != FPropertyAccess::Success)
	{
		return LOCTEXT("MultipleSelected", "<Details.Subdued>Multiple selected</>");
	}

	TArray<void*> RawItemData;
	ItemProperty->AccessRawData(RawItemData);
	check(RawItemData.Num() == 1);

	TArray<void*> RawInstanceData;
	InstanceProperty->AccessRawData(RawInstanceData);
	check(RawInstanceData.Num() == 1);

	// Dig out name from the struct without knowing the type.
	const FInstancedStruct* ItemPtr = static_cast<FInstancedStruct*>(RawItemData[0]);
	const FInstancedStruct* InstancePtr = static_cast<FInstancedStruct*>(RawInstanceData[0]);

	if (InstancePtr->IsValid() && EditorData != nullptr)
	{
		if (const FStateTreeConditionBase* Condition = ItemPtr->GetPtr<const FStateTreeConditionBase>())
		{
			const FStateTreeBindingLookup BindingLookup(EditorData);
			return Condition->GetDescription(ID, *InstancePtr, BindingLookup);
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
	check(ItemProperty);

	// Multiple names do not make sense, just if only one item is selected.
	TArray<void*> RawItemData;
	ItemProperty->AccessRawData(RawItemData);
	if (RawItemData.Num() == 1)
	{
		// Dig out name from the struct without knowing the type.
		FInstancedStruct* StructPtr = static_cast<FInstancedStruct*>(RawItemData[0]);

		// Make sure the associated property has valid data (e.g. while moving an item in array)
		const UScriptStruct* ScriptStruct = StructPtr != nullptr ? StructPtr->GetScriptStruct() : nullptr;
		if (ScriptStruct != nullptr)
		{
			if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
			{
				if (NameProperty->IsA(FNameProperty::StaticClass()))
				{
					void* Ptr = const_cast<void*>(static_cast<const void*>(StructPtr->GetMemory()));
					FName NameValue = *NameProperty->ContainerPtrToValuePtr<FName>(Ptr);
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
	check(ItemProperty);

	if (InTextCommit == ETextCommit::OnEnter || InTextCommit == ETextCommit::OnUserMovedFocus)
	{
		// Remove excess whitespace and prevent categories with just spaces
		FString NewName = FText::TrimPrecedingAndTrailing(NewText).ToString();

		if (GEditor)
		{
			GEditor->BeginTransaction(LOCTEXT("SetName", "Set Name"));
		}
		ItemProperty->NotifyPreChange();

		TArray<void*> RawItemData;
		ItemProperty->AccessRawData(RawItemData);
		for (void* Data : RawItemData)
		{
			// Set Name
			FInstancedStruct* ItemPtr = static_cast<FInstancedStruct*>(Data);
			if (const UScriptStruct* ScriptStruct = ItemPtr->GetScriptStruct())
			{
				if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
				{
					if (NameProperty->IsA(FNameProperty::StaticClass()))
					{
						void* Ptr = const_cast<void*>(static_cast<const void*>(ItemPtr->GetMemory()));
						FName& NameValue = *NameProperty->ContainerPtrToValuePtr<FName>(Ptr);
						NameValue = FName(NewName);
					}
				}
			}
		}

		ItemProperty->NotifyPostChange(EPropertyChangeType::ValueSet);

		if (StateTree)
		{
			UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
		}

		if (GEditor)
		{
			GEditor->EndTransaction();
		}

		ItemProperty->NotifyFinishedChangingProperties();
	}
}

bool FStateTreeBindableItemDetails::IsNameEnabled() const
{
	// Can only edit if we have valid instantiated type.
	return GetCommonItemScriptStruct() != nullptr;
}

const UScriptStruct* FStateTreeBindableItemDetails::GetCommonItemScriptStruct() const
{
	check(ItemProperty);

	const UScriptStruct* CommonScriptStruct = nullptr;
	bool bMultipleValues = false;
	TArray<void*> RawItemData;
	ItemProperty->AccessRawData(RawItemData);
	for (void* Data : RawItemData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			const UScriptStruct* ScriptStruct = Struct->GetScriptStruct();
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

TSharedRef<SWidget> FStateTreeBindableItemDetails::GenerateStructPicker()
{
	static const FName ExcludeBaseStruct(TEXT("ExcludeBaseStruct")); // TODO: move these names into one central place.
	const bool bExcludeBaseStruct = ItemProperty->HasMetaData(ExcludeBaseStruct);

	TSharedRef<FBindableItemStructFilter> StructFilter = MakeShared<FBindableItemStructFilter>();
	StructFilter->BaseStruct = BaseScriptStruct;
	StructFilter->bAllowUserDefinedStructs = false;
	StructFilter->bAllowBaseStruct = !bExcludeBaseStruct;
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
	check(ItemProperty);
	check(InstanceProperty);
	check(IDProperty);

	TArray<void*> RawItemData;
	ItemProperty->AccessRawData(RawItemData);

	TArray<void*> RawInstanceData;
	InstanceProperty->AccessRawData(RawInstanceData);

	TArray<void*> RawIDData;
	IDProperty->AccessRawData(RawIDData);

	if (RawItemData.Num() != RawInstanceData.Num() || RawItemData.Num() != RawIDData.Num())
	{
		return;
	}

	GEditor->BeginTransaction(LOCTEXT("OnStructPicked", "Set Struct"));

	ItemProperty->NotifyPreChange();
	InstanceProperty->NotifyPreChange();
	IDProperty->NotifyPreChange();

	for (int32 Index = 0; Index < RawItemData.Num(); Index++)
	{
		FInstancedStruct* ItemPtr = static_cast<FInstancedStruct*>(RawItemData[Index]);
		FInstancedStruct* InstancePtr = static_cast<FInstancedStruct*>(RawInstanceData[Index]);
		FGuid* ID = static_cast<FGuid*>(RawIDData[Index]);
		
		if (ItemPtr != nullptr && InstancePtr != nullptr && ID != nullptr)
		{
			if (InStruct)
			{
				// Generate new ID and name.
				*ID = FGuid::NewGuid();

				// Intialize item
				ItemPtr->InitializeAs(InStruct);
				
				// Generate new name and instantiate instance data.
				if (InStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
				{
					FStateTreeTaskBase* Task = ItemPtr->GetMutablePtr<FStateTreeTaskBase>();
					check(Task);
					Task->Name = FName(InStruct->GetDisplayNameText().ToString());

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Task->GetInstanceDataType()))
					{
						InstancePtr->InitializeAs(InstanceType);
					}
					else
					{
						InstancePtr->Reset();
					}
				}
				else if (InStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
				{
					FStateTreeEvaluatorBase* Eval = ItemPtr->GetMutablePtr<FStateTreeEvaluatorBase>();
					check(Eval);
					Eval->Name = FName(InStruct->GetDisplayNameText().ToString());

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Eval->GetInstanceDataType()))
					{
						InstancePtr->InitializeAs(InstanceType);
					}
					else
					{
						InstancePtr->Reset();
					}
				}
				else if (InStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
				{
					FStateTreeConditionBase* Cond = ItemPtr->GetMutablePtr<FStateTreeConditionBase>();
					check(Cond);

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond->GetInstanceDataType()))
					{
						InstancePtr->InitializeAs(InstanceType);
					}
					else
					{
						InstancePtr->Reset();
					}
				}
			}
			else
			{
				ItemPtr->Reset();
				InstancePtr->Reset();
				ID->Invalidate();
			}
		}
	}

	ItemProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	ItemProperty->NotifyFinishedChangingProperties();

	InstanceProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	InstanceProperty->NotifyFinishedChangingProperties();

	IDProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	IDProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	ComboButton->SetIsOpen(false);

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
