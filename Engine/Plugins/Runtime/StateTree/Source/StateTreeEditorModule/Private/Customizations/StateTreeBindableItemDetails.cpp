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

#define LOCTEXT_NAMESPACE "StateTreeEditor"

// Customized version of FInstancedStructDataDetails used to hide bindable properties.
class FBindableItemDataDetails : public FInstancedStructDataDetails
{
public:
	FBindableItemDataDetails(TSharedPtr<IPropertyHandle> InStructProperty, UStateTreeEditorData* InEditorData)
		: FInstancedStructDataDetails(InStructProperty)
		, EditorData(InEditorData)
	{
		EditorPropBindings = EditorData ? EditorData->GetPropertyEditorBindings() : nullptr;
	}

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow)
	{
		static const FName BindableMetaName(TEXT("Bindable"));

		if (!EditorPropBindings)
		{
			return;
		}
		
		TSharedPtr<IPropertyHandle> ChildPropHandle = ChildRow.GetPropertyHandle();

		// Conditionally control visibility of the value field of bound properties.
		if (ChildPropHandle->HasMetaData(BindableMetaName))
		{
			// Find property path relative to closest outer bindable struct.
			FStateTreeEditorPropertyPath Path;
			UE::StateTree::PropertyBinding::GetOuterStructPropertyPath(ChildPropHandle, Path);

			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			FDetailWidgetRow Row;
			ChildRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

			auto IsValueVisible = TAttribute<EVisibility>::Create([this, Path]() -> EVisibility
				{
					return EditorPropBindings->HasPropertyBinding(Path) ? EVisibility::Collapsed : EVisibility::Visible;
				});

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

	UStateTreeEditorData* EditorData;
	FStateTreeEditorPropertyBindings* EditorPropBindings;
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
		if (Schema && !Schema->IsStructAllowed(InStruct))
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

void FStateTreeBindableItemDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	TypeProperty = StructProperty->GetChildHandle(TEXT("Type"));

	// Find base class from meta data.
	static const FName BaseClassMetaName(TEXT("BaseStruct")); // TODO: move these names into one central place.
	const FString BaseClassName = TypeProperty->GetMetaData(BaseClassMetaName);
	BaseScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseClassName);

	const FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FStateTreeBindableItemDetails::ShouldResetToDefault);
	const FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FStateTreeBindableItemDetails::ResetToDefault);
	const FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeBindableItemDetails::OnIdentifierChanged);

	FindOuterObjects();

	HeaderRow
		.WholeRowContent()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Name
			+ SHorizontalBox::Slot()
			.FillWidth(1.5f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(FMargin(0.0f, 0.0f, 4.0f, 0.0f)))
			[
				SNew(SEditableTextBox)
				.IsEnabled(TAttribute<bool>(this, &FStateTreeBindableItemDetails::IsNameEnabled))
				.Text(this, &FStateTreeBindableItemDetails::GetName)
				.OnTextCommitted(this, &FStateTreeBindableItemDetails::OnNameCommitted)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			// Class picker
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
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
	check(TypeProperty);
	
	bool bAnyValid = false;
	
	TArray<void*> RawData;
	TypeProperty->AccessRawData(RawData);
	for (void* Data : RawData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
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
	check(TypeProperty);
	
	GEditor->BeginTransaction(LOCTEXT("OnResetToDefault", "Reset to default"));

	TypeProperty->NotifyPreChange();

	TArray<void*> RawData;
	TypeProperty->AccessRawData(RawData);
	for (void* Data : RawData)
	{
		if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
		{
			// Assume that the default value is empty.
			Struct->Reset();
		}
	}

	TypeProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	GEditor->EndTransaction();
	TypeProperty->NotifyFinishedChangingProperties();

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeBindableItemDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedRef<FBindableItemDataDetails> DataDetails = MakeShareable(new FBindableItemDataDetails(TypeProperty, EditorData));
	StructBuilder.AddCustomBuilder(DataDetails);
}

void FStateTreeBindableItemDetails::OnIdentifierChanged(const UStateTree& InStateTree)
{
	if (PropUtils && StateTree == &InStateTree)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeBindableItemDetails::FindOuterObjects()
{
	EditorData = nullptr;
	StateTree = nullptr;

	if (TypeProperty)
	{
		TArray<UObject*> OuterObjects;
		TypeProperty->GetOuterObjects(OuterObjects);
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
}

FText FStateTreeBindableItemDetails::GetName() const
{
	if (TypeProperty)
	{
		TArray<void*> RawData;
		TypeProperty->AccessRawData(RawData);
		if (RawData.Num() == 1)
		{
			// Dig out name from the struct without knowing the type.
			FInstancedStruct* StructPtr = static_cast<FInstancedStruct*>(RawData[0]);

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
		else
		{
			return LOCTEXT("MultipleSelected", "Multiple selected");
		}
	}
	return FText::GetEmpty();
}

void FStateTreeBindableItemDetails::OnNameCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	if (InTextCommit == ETextCommit::OnEnter || InTextCommit == ETextCommit::OnUserMovedFocus)
	{
		// Remove excess whitespace and prevent categories with just spaces
		FString NewName = FText::TrimPrecedingAndTrailing(NewText).ToString();

		if (TypeProperty)
		{
			TArray<void*> RawData;
			TypeProperty->AccessRawData(RawData);
			if (RawData.Num() == 1)
			{
				if (GEditor)
				{
					GEditor->BeginTransaction(LOCTEXT("SetName", "Set Name"));
				}
				TypeProperty->NotifyPreChange();
					
				// Set Name
				FInstancedStruct* StructPtr = static_cast<FInstancedStruct*>(RawData[0]);
				if (const UScriptStruct* ScriptStruct = StructPtr->GetScriptStruct())
				{
					if (FProperty* NameProperty = ScriptStruct->FindPropertyByName(TEXT("Name")))
					{
						if (NameProperty->IsA(FNameProperty::StaticClass()))
						{
							void* Ptr = const_cast<void*>(static_cast<const void*>(StructPtr->GetMemory()));
							FName& NameValue = *NameProperty->ContainerPtrToValuePtr<FName>(Ptr);
							NameValue = FName(NewName);
						}
					}
				}

				TypeProperty->NotifyPostChange(EPropertyChangeType::ValueSet);

				if (StateTree)
				{
					UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
				}

				if (GEditor)
				{
					GEditor->EndTransaction();
				}

				TypeProperty->NotifyFinishedChangingProperties();
			}
		}
	}
}

bool FStateTreeBindableItemDetails::IsNameEnabled() const
{
	// Can only edit if we have valid instantiated type.
	return GetCommonScriptStruct() != nullptr;
}

const UScriptStruct* FStateTreeBindableItemDetails::GetCommonScriptStruct() const
{
	if (!TypeProperty)
	{
		return nullptr;
	}
	const UScriptStruct* CommonScriptStruct = nullptr;
	bool bMultipleValues = false;
	TArray<void*> RawData;
	TypeProperty->AccessRawData(RawData);
	for (void* Data : RawData)
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
	const UScriptStruct* ScriptStruct = GetCommonScriptStruct();
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
	const bool bExcludeBaseStruct = TypeProperty->HasMetaData(ExcludeBaseStruct);

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
	if (TypeProperty)
	{
		GEditor->BeginTransaction(LOCTEXT("OnStructPicked", "Set Struct"));

		TypeProperty->NotifyPreChange();

		TArray<void*> RawData;
		TypeProperty->AccessRawData(RawData);
		for (void* Data : RawData)
		{
			if (FInstancedStruct* Struct = static_cast<FInstancedStruct*>(Data))
			{
				if (InStruct)
				{
					Struct->InitializeAs(InStruct);

					// Generate new ID and name.
					if (InStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
					{
						FStateTreeTaskBase* Task = Struct->GetMutablePtr<FStateTreeTaskBase>();
						check(Task);
						Task->ID = FGuid::NewGuid();
						Task->Name = FName(InStruct->GetDisplayNameText().ToString());
					}
					else if (InStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
					{
						FStateTreeEvaluatorBase* Eval = Struct->GetMutablePtr<FStateTreeEvaluatorBase>();
						check(Eval);
						Eval->ID = FGuid::NewGuid();
						Eval->Name = FName(InStruct->GetDisplayNameText().ToString());
					}
				}
				else
				{
					Struct->Reset();
				}
			}
		}

		TypeProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
		GEditor->EndTransaction();
		TypeProperty->NotifyFinishedChangingProperties();
	}

	ComboButton->SetIsOpen(false);

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
