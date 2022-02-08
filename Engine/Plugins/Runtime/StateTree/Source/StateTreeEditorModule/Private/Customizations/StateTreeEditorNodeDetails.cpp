// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorNodeDetails.h"
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
			// Pass the node ID to binding extension. Since the properties are added using AddChildStructure(), we break the hierarchy and cannot access parent.
			ChildPropHandle->SetInstanceMetaData(UE::StateTree::PropertyBinding::StateTreeNodeIDName, LexToString(ID));

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
class FBindableNodeInstanceDetails : public FInstancedStructDataDetails
{
public:

	FBindableNodeInstanceDetails(TSharedPtr<IPropertyHandle> InStructProperty, FGuid InID, UStateTreeEditorData* InEditorData)
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

TSharedRef<IPropertyTypeCustomization> FStateTreeEditorNodeDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeEditorNodeDetails);
}

FStateTreeEditorNodeDetails::~FStateTreeEditorNodeDetails()
{
	UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.Remove(OnBindingChangedHandle);
}

void FStateTreeEditorNodeDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	NodeProperty = StructProperty->GetChildHandle(TEXT("Node"));
	InstanceProperty = StructProperty->GetChildHandle(TEXT("Instance"));
	InstanceObjectProperty = StructProperty->GetChildHandle(TEXT("InstanceObject"));
	IDProperty = StructProperty->GetChildHandle(TEXT("ID"));

	check(NodeProperty.IsValid());
	check(InstanceProperty.IsValid());
	check(IDProperty.IsValid());
	
	// Find base class and struct from meta data.
	static const FName BaseStructMetaName(TEXT("BaseStruct")); // TODO: move these names into one central place.
	static const FName BaseClassMetaName(TEXT("BaseClass")); // TODO: move these names into one central place.
	
	const FString BaseStructName = StructProperty->GetMetaData(BaseStructMetaName);
	BaseScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseStructName);

	const FString BaseClassName = StructProperty->GetMetaData(BaseClassMetaName);
	BaseClass = FindObject<UClass>(ANY_PACKAGE, *BaseClassName);
	
	const FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FStateTreeEditorNodeDetails::ShouldResetToDefault);
	const FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FStateTreeEditorNodeDetails::ResetToDefault);
	const FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeEditorNodeDetails::OnIdentifierChanged);
	OnBindingChangedHandle = UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.AddRaw(this, &FStateTreeEditorNodeDetails::OnBindingChanged);

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
				.Text(this, &FStateTreeEditorNodeDetails::GetDescription)
				.Visibility(this, &FStateTreeEditorNodeDetails::IsDescriptionVisible)
			]
			// Name (Eval/Task)
			+ SHorizontalBox::Slot()
			.FillWidth(1.5f)
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.IsEnabled(TAttribute<bool>(this, &FStateTreeEditorNodeDetails::IsNameEnabled))
				.Text(this, &FStateTreeEditorNodeDetails::GetName)
				.OnTextCommitted(this, &FStateTreeEditorNodeDetails::OnNameCommitted)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Visibility(this, &FStateTreeEditorNodeDetails::IsNameVisible)
			]
			// Class picker
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(FMargin(4.0f, 0.0f, 0.0f, 0.0f)))
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboButton, SComboButton)
				.OnGetMenuContent(this, &FStateTreeEditorNodeDetails::GeneratePicker)
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
						.Image(this, &FStateTreeEditorNodeDetails::GetDisplayValueIcon)
					]
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &FStateTreeEditorNodeDetails::GetDisplayValueString)
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

bool FStateTreeEditorNodeDetails::ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	check(StructProperty);
	
	bool bAnyValid = false;
	
	TArray<const void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	for (const void* Data : RawNodeData)
	{
		if (const FStateTreeEditorNode* Node = static_cast<const FStateTreeEditorNode*>(Data))
		{
			if (Node->Node.IsValid())
			{
				bAnyValid = true;
				break;
			}
		}
	}
	
	// Assume that the default value is empty. Any valid means that some can be reset to empty.
	return bAnyValid;
}


void FStateTreeEditorNodeDetails::ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	check(StructProperty);
	
	GEditor->BeginTransaction(LOCTEXT("OnResetToDefault", "Reset to default"));

	StructProperty->NotifyPreChange();
	
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	for (void* Data : RawNodeData)
	{
		if (FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
		{
			Node->Reset();
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

void FStateTreeEditorNodeDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FStateTreeEditorPropertyBindings* EditorPropBindings = EditorData ? EditorData->GetPropertyEditorBindings() : nullptr;

	FGuid ID;
	UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);

	TSharedRef<FBindableNodeInstanceDetails> NodeDetails = MakeShareable(new FBindableNodeInstanceDetails(NodeProperty, FGuid(), EditorData));
	StructBuilder.AddCustomBuilder(NodeDetails);

	// Instance
	TSharedRef<FBindableNodeInstanceDetails> InstanceDetails = MakeShareable(new FBindableNodeInstanceDetails(InstanceProperty, ID, EditorData));
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

TSharedPtr<IPropertyHandle> FStateTreeEditorNodeDetails::GetInstancedObjectValueHandle(TSharedPtr<IPropertyHandle> PropertyHandle)
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

void FStateTreeEditorNodeDetails::OnIdentifierChanged(const UStateTree& InStateTree)
{
	if (PropUtils && StateTree == &InStateTree)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeEditorNodeDetails::OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath)
{
	check(StructProperty);

	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);

	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);

	if (OuterObjects.Num() != RawNodeData.Num())
	{
		return;
	}

	for (int32 i = 0; i < OuterObjects.Num(); i++)
	{
		const FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[i]);
		UObject* OuterObject = OuterObjects[i]; // Immediate outer, i.e StateTreeState
		if (Node != nullptr && EditorData != nullptr && Node->Node.IsValid() && Node->Instance.IsValid())
		{
			if (Node->ID == TargetPath.StructID)
			{
				if (FStateTreeConditionBase* Condition = Node->Node.GetMutablePtr<FStateTreeConditionBase>())
				{
					const FStateTreeBindingLookup BindingLookup(EditorData);

					OuterObject->Modify();
					Condition->OnBindingChanged(Node->ID, FStateTreeDataView(Node->Instance), SourcePath, TargetPath, BindingLookup);
				}
			}
		}
	}
}

void FStateTreeEditorNodeDetails::FindOuterObjects()
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

FText FStateTreeEditorNodeDetails::GetDescription() const
{
	check(StructProperty);

	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);

	// Multiple descriptions do not make sense, just if only one node is selected.
	if (RawNodeData.Num() != 1)
	{
		return LOCTEXT("MultipleSelectedFormatted", "<Details.Subdued>Multiple Selected</>");
	}

	FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[0]);
	if (Node != nullptr && EditorData != nullptr && Node->Node.IsValid())
	{
		if (const FStateTreeConditionBase* Condition = Node->Node.GetPtr<const FStateTreeConditionBase>())
		{
			const FStateTreeBindingLookup BindingLookup(EditorData);
			if (Node->Instance.IsValid())
			{
				return Condition->GetDescription(Node->ID, FStateTreeDataView(Node->Instance), BindingLookup);
			}
			else if (Node->InstanceObject != nullptr)
			{
				return Condition->GetDescription(Node->ID, FStateTreeDataView(Node->InstanceObject), BindingLookup);
			}
			else
			{
				return Node->Node.GetScriptStruct()->GetDisplayNameText();
			}
		}
	}
	
	return LOCTEXT("ConditionNotSet", "<Details.Subdued>Condition Not Set</>");
}

EVisibility FStateTreeEditorNodeDetails::IsDescriptionVisible() const
{
	const UScriptStruct* ScriptStruct = nullptr;
	if (const FStateTreeEditorNode* Node = GetCommonNode())
	{
		ScriptStruct = Node->Node.GetScriptStruct();
	}

	return ScriptStruct != nullptr && ScriptStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FStateTreeEditorNodeDetails::GetName() const
{
	check(StructProperty);

	// Multiple names do not make sense, just if only one node is selected.
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	if (RawNodeData.Num() == 1)
	{
		// Dig out name from the struct without knowing the type.
		FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[0]);

		// Make sure the associated property has valid data (e.g. while moving an node in array)
		const UScriptStruct* ScriptStruct = Node != nullptr ? Node->Node.GetScriptStruct() : nullptr;
		if (ScriptStruct != nullptr)
		{
			if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
			{
				if (NameProperty->IsA(FNameProperty::StaticClass()))
				{
					void* Ptr = const_cast<void*>(static_cast<const void*>(Node->Node.GetMemory()));
					const FName NameValue = *NameProperty->ContainerPtrToValuePtr<FName>(Ptr);
					return FText::FromName(NameValue);
				}
			}
		}
		return LOCTEXT("Empty", "Empty");
	}

	return LOCTEXT("MultipleSelected", "Multiple Selected");
}

EVisibility FStateTreeEditorNodeDetails::IsNameVisible() const
{
	const UStateTreeSchema* Schema = StateTree ? StateTree->GetSchema() : nullptr;
	if (Schema && Schema->AllowMultipleTasks() == false)
	{
		// Single task states use the state name as task name.
		return EVisibility::Collapsed;
	}
	
	const UScriptStruct* ScriptStruct = nullptr;
	if (const FStateTreeEditorNode* Node = GetCommonNode())
	{
		ScriptStruct = Node->Node.GetScriptStruct();
	}
	
	return ScriptStruct != nullptr && (ScriptStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()) || ScriptStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct())) ? EVisibility::Visible : EVisibility::Collapsed;
}

void FStateTreeEditorNodeDetails::OnNameCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
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

		TArray<void*> RawNodeData;
		StructProperty->AccessRawData(RawNodeData);
		
		for (void* Data : RawNodeData)
		{
			if (FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
			{
				// Set Name
				if (const UScriptStruct* ScriptStruct = Node->Node.GetScriptStruct())
				{
					if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
					{
						if (NameProperty->IsA(FNameProperty::StaticClass()))
						{
							void* Ptr = const_cast<void*>(static_cast<const void*>(Node->Node.GetMemory()));
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

bool FStateTreeEditorNodeDetails::IsNameEnabled() const
{
	// Can only edit if we have valid instantiated type.
	const FStateTreeEditorNode* Node = GetCommonNode();
	return Node && Node->Node.IsValid();
}

const FStateTreeEditorNode* FStateTreeEditorNodeDetails::GetCommonNode() const
{
	check(StructProperty);

	const UScriptStruct* CommonScriptStruct = nullptr;
	bool bMultipleValues = false;
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);

	const FStateTreeEditorNode* CommonNode = nullptr;
	
	for (void* Data : RawNodeData)
	{
		if (const FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
		{
			if (!bMultipleValues && !CommonNode)
			{
				CommonNode = Node;
			}
			else if (CommonNode != Node)
			{
				CommonNode = nullptr;
				bMultipleValues = true;
			}
		}
	}

	return CommonNode;
}

FText FStateTreeEditorNodeDetails::GetDisplayValueString() const
{
	if (const FStateTreeEditorNode* Node = GetCommonNode())
	{
		if (const UScriptStruct* ScriptStruct = Node->Node.GetScriptStruct())
		{
			if (ScriptStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct()))
			{
				if (Node->InstanceObject != nullptr
					&& Node->InstanceObject->GetClass() != nullptr)
				{
					return Node->InstanceObject->GetClass()->GetDisplayNameText();
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

const FSlateBrush* FStateTreeEditorNodeDetails::GetDisplayValueIcon() const
{
	if (const FStateTreeEditorNode* Node = GetCommonNode())
	{
		if (const UScriptStruct* ScriptStruct = Node->Node.GetScriptStruct())
		{
			if (ScriptStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct()))
			{
				if (Node->InstanceObject != nullptr)
				{
					return FSlateIconFinder::FindIconBrushForClass(Node->InstanceObject->GetClass());
				}
			}
		}
	}
	
	return FSlateIconFinder::FindIconBrushForClass(UScriptStruct::StaticClass());
}

TSharedRef<SWidget> FStateTreeEditorNodeDetails::GeneratePicker()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	FStateTreeEditorModule& EditorModule = FModuleManager::GetModuleChecked<FStateTreeEditorModule>(TEXT("StateTreeEditorModule"));
	FStateTreeNodeClassCache* ClassCache = EditorModule.GetNodeClassCache().Get();
	check(ClassCache);

	FUIAction ClearAction(FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnStructPicked, (const UScriptStruct*)nullptr));
	MenuBuilder.AddMenuEntry(LOCTEXT("ClearNode", "Clear"), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "Cross"), ClearAction);

	MenuBuilder.AddMenuSeparator();

	TArray<TSharedPtr<FStateTreeNodeClassData>> StructNodes;
	TArray<TSharedPtr<FStateTreeNodeClassData>> ObjectNodes;
	
	ClassCache->GetScripStructs(BaseScriptStruct, StructNodes);
	ClassCache->GetClasses(BaseClass, ObjectNodes);

	const FSlateIcon ScripStructIcon = FSlateIconFinder::FindIconForClass(UScriptStruct::StaticClass());
	const UStateTreeSchema* Schema = StateTree ? StateTree->GetSchema() : nullptr;
	
	for (const TSharedPtr<FStateTreeNodeClassData>& Data : StructNodes)
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
			
			FUIAction ItemAction(FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnStructPicked, ScriptStruct));
			MenuBuilder.AddMenuEntry(ScriptStruct->GetDisplayNameText(), TAttribute<FText>(), ScripStructIcon, ItemAction);
		}
	}

	if (StructNodes.Num() > 0 && ObjectNodes.Num() > 0)
	{
		MenuBuilder.AddMenuSeparator();
	}
	
	for (const TSharedPtr<FStateTreeNodeClassData>& Data : ObjectNodes)
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

			FUIAction ItemAction(FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnClassPicked, Class));
			MenuBuilder.AddMenuEntry(Class->GetDisplayNameText(), TAttribute<FText>(), FSlateIconFinder::FindIconForClass(Data->GetClass()), ItemAction);
		}
	}

	return MenuBuilder.MakeWidget();
}

void FStateTreeEditorNodeDetails::OnStructPicked(const UScriptStruct* InStruct)
{
	check(StructProperty);
	check(StateTree);

	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);

	GEditor->BeginTransaction(LOCTEXT("SelectNode", "Select Node"));

	StructProperty->NotifyPreChange();

	for (void* Data : RawNodeData)
	{
		if (FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
		{
			Node->Reset();
			
			if (InStruct)
			{
				// Generate new ID.
				Node->ID = FGuid::NewGuid();

				// Initialize node
				Node->Node.InitializeAs(InStruct);
				
				// Generate new name and instantiate instance data.
				if (InStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
				{
					FStateTreeTaskBase& Task = Node->Node.GetMutable<FStateTreeTaskBase>();
					Task.Name = FName(InStruct->GetDisplayNameText().ToString());

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Task.GetInstanceDataType()))
					{
						Node->Instance.InitializeAs(InstanceType);
					}
					else if (const UClass* InstanceClass = Cast<const UClass>(Task.GetInstanceDataType()))
					{
						Node->InstanceObject = NewObject<UObject>(EditorData, InstanceClass);
					}
				}
				else if (InStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
				{
					FStateTreeEvaluatorBase& Eval = Node->Node.GetMutable<FStateTreeEvaluatorBase>();
					Eval.Name = FName(InStruct->GetDisplayNameText().ToString());

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Eval.GetInstanceDataType()))
					{
						Node->Instance.InitializeAs(InstanceType);
					}
					else if (const UClass* InstanceClass = Cast<const UClass>(Eval.GetInstanceDataType()))
					{
						Node->InstanceObject = NewObject<UObject>(EditorData, InstanceClass);
					}
				}
				else if (InStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
				{
					FStateTreeConditionBase& Cond = Node->Node.GetMutable<FStateTreeConditionBase>();

					if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond.GetInstanceDataType()))
					{
						Node->Instance.InitializeAs(InstanceType);
					}
					else if (const UClass* InstanceClass = Cast<const UClass>(Cond.GetInstanceDataType()))
					{
						Node->InstanceObject = NewObject<UObject>(EditorData, InstanceClass);
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

void FStateTreeEditorNodeDetails::OnClassPicked(UClass* InClass)
{
	check(StructProperty);
	check(StateTree);

	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);

	GEditor->BeginTransaction(LOCTEXT("SelectBlueprintNode", "Select Blueprint Node"));

	StructProperty->NotifyPreChange();

	for (void* Data : RawNodeData)
	{
		if (FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
		{
			Node->Reset();

			if (InClass && InClass->IsChildOf(UStateTreeTaskBlueprintBase::StaticClass()))
			{
				Node->Node.InitializeAs(FStateTreeBlueprintTaskWrapper::StaticStruct());
				FStateTreeBlueprintTaskWrapper& Task = Node->Node.GetMutable<FStateTreeBlueprintTaskWrapper>();
				Task.TaskClass = InClass;
				Task.Name = FName(InClass->GetDisplayNameText().ToString());
				
				Node->InstanceObject = NewObject<UObject>(EditorData, InClass);

				Node->ID = FGuid::NewGuid();
			}
			else if (InClass && InClass->IsChildOf(UStateTreeEvaluatorBlueprintBase::StaticClass()))
			{
				Node->Node.InitializeAs(FStateTreeBlueprintEvaluatorWrapper::StaticStruct());
				FStateTreeBlueprintEvaluatorWrapper& Eval = Node->Node.GetMutable<FStateTreeBlueprintEvaluatorWrapper>();
				Eval.EvaluatorClass = InClass;
				Eval.Name = FName(InClass->GetDisplayNameText().ToString());
				
				Node->InstanceObject = NewObject<UObject>(EditorData, InClass);

				Node->ID = FGuid::NewGuid();
			}
			else if (InClass && InClass->IsChildOf(UStateTreeConditionBlueprintBase::StaticClass()))
			{
				Node->Node.InitializeAs(FStateTreeBlueprintConditionWrapper::StaticStruct());
				FStateTreeBlueprintConditionWrapper& Cond = Node->Node.GetMutable<FStateTreeBlueprintConditionWrapper>();
				Cond.ConditionClass = InClass;
				
				Node->InstanceObject = NewObject<UObject>(EditorData, InClass);

				Node->ID = FGuid::NewGuid();
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
