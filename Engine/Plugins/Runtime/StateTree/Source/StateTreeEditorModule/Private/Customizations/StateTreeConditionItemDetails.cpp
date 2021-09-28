// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeConditionItemDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "InstancedStruct.h"
#include "Editor.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeBindingExtension.h"
#include "StateTreeEditorStyle.h"
#include "Modules/ModuleManager.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "InstancedStructDetails.h"
#include "Engine/UserDefinedStruct.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

// TODO: This has a lot in common with StateTreeBindableItemDetails, see if we could combine the efforts.

// Customized version of FInstancedStructDataDetails used to hide bindable properties.
class FConditionItemDataDetails : public FInstancedStructDataDetails
{
public:
	FConditionItemDataDetails(TSharedPtr<IPropertyHandle> InStructProperty, UStateTreeEditorData* InEditorData)
		: FInstancedStructDataDetails(InStructProperty)
		, EditorData(InEditorData)
	{
		check(EditorData);
		EditorPropBindings = EditorData->GetPropertyEditorBindings();
	}

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow)
	{
		check(EditorPropBindings);

		static const FName BindableMetaName(TEXT("Bindable"));

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
				.CustomWidget()
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

class FConditionItemStructFilter : public IStructViewerFilter
{
public:
	/** The base struct for the property that classes must be a child-of. */
	const UScriptStruct* BaseStruct = nullptr;

	/** A flag controlling whether we allow to select the BaseStruct */
	bool bAllowBaseStruct = true;

	virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		if (InStruct == BaseStruct)
		{
			return bAllowBaseStruct;
		}

		// Query the native struct to see if it has the correct parent type (if any)
		return !BaseStruct || InStruct->IsChildOf(BaseStruct);
	}

	virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FName InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
	{
		// We currently assume that conditions are implemented natively.
		return false;
	}
};


////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FStateTreeConditionItemDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeConditionItemDetails);
}

FStateTreeConditionItemDetails::~FStateTreeConditionItemDetails()
{
	UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.Remove(OnBindingChangedHandle);
}

void FStateTreeConditionItemDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	TypeProperty = StructProperty->GetChildHandle(TEXT("Type"));
	check(TypeProperty);

	// Find base class from meta data.
	static const FName BaseClassMetaName(TEXT("BaseStruct")); // TODO: move these names into one central place.
	const FString BaseClassName = TypeProperty->GetMetaData(BaseClassMetaName);
	BaseScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *BaseClassName);

	OnBindingChangedHandle = UE::StateTree::PropertyBinding::OnStateTreeBindingChanged.AddRaw(this, &FStateTreeConditionItemDetails::OnBindingChanged);

	FindOuterObjects();

	HeaderRow
		.WholeRowContent()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Description
			+ SHorizontalBox::Slot()
			.FillWidth(3.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SRichTextBlock)
				.DecoratorStyleSet(FStateTreeEditorStyle::Get().Get())
				.TextStyle(FStateTreeEditorStyle::Get(), "Details.Normal")
				.Text(this, &FStateTreeConditionItemDetails::GetDescription)
			]
			// Class picker
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboButton, SComboButton)
				.OnGetMenuContent(this, &FStateTreeConditionItemDetails::GenerateStructPicker)
				.ContentPadding(0)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &FStateTreeConditionItemDetails::GetDisplayValueString)
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			]
			// Array controls
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				StructPropertyHandle->CreateDefaultPropertyButtonWidgets()
			]
		];
}

void FStateTreeConditionItemDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	check(TypeProperty);

	TSharedRef<FConditionItemDataDetails> DataDetails = MakeShareable(new FConditionItemDataDetails(TypeProperty, EditorData));
	StructBuilder.AddCustomBuilder(DataDetails);
}

void FStateTreeConditionItemDetails::FindOuterObjects()
{
	check(TypeProperty);

	EditorData = nullptr;
	StateTree = nullptr;

	TArray<UObject*> OuterObjects;
	TypeProperty->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1 && OuterObjects[0] != nullptr)
	{
		EditorData = OuterObjects[0]->GetTypedOuter<UStateTreeEditorData>();
		StateTree = EditorData ? EditorData->GetTypedOuter<UStateTree>() : nullptr;
	}
}

void FStateTreeConditionItemDetails::OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath)
{
	check(TypeProperty);

	// Note: Does not support editing multiple objects.
	FStateTreeConditionBase* Condition = GetSingleCondition();
	if (!Condition)
	{
		return;
	}

	if (Condition->ID != TargetPath.StructID)
	{
		return;
	}

	TArray<UObject*> OuterObjects;
	TypeProperty->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1 && OuterObjects[0] != nullptr)
	{
		UObject* OuterObject = OuterObjects[0]; // Immediate outer, i.e StateTreeState
		UObject* OwnerObject = UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObject); // Outer that contains IStateTreeEditorPropertyBindingsOwner interface.
		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(OwnerObject))
		{
			// Binding changes modify only the object that contains the bindings, we need to also set the property owner (i.e. StateTreeState) as modified.
			OuterObject->Modify();
			FStateTreeBindingLookup BindingLookup(BindingOwner);
			Condition->OnBindingChanged(SourcePath, TargetPath, BindingLookup);
		}
	}
}

FText FStateTreeConditionItemDetails::GetDescription() const
{
	check(TypeProperty);

	if (TypeProperty->GetNumOuterObjects() != 1)
	{
		return LOCTEXT("MultipleSelected", "<Details.Subdued>Multiple selected</>");
	}

	if (FStateTreeConditionBase* Condition = GetSingleCondition())
	{
		if (IStateTreeEditorPropertyBindingsOwner* BindingOwner = Cast<IStateTreeEditorPropertyBindingsOwner>(GetEditorBindingsOwner()))
		{
			FStateTreeBindingLookup BindingLookup(BindingOwner);
			return Condition->GetDescription(BindingLookup);
		}
	}

	return LOCTEXT("ConditionNotSet", "<Details.Subdued>Condition Not Set</>");
}

FInstancedStruct* FStateTreeConditionItemDetails::GetSingleStructPtr() const
{
	check(TypeProperty);
	TArray<void*> RawData;
	TypeProperty->AccessRawData(RawData);
	return RawData.Num() == 1 ? static_cast<FInstancedStruct*>(RawData[0]) : nullptr;
}

FStateTreeConditionBase* FStateTreeConditionItemDetails::GetSingleCondition() const
{
	if (FInstancedStruct* StructPtr = GetSingleStructPtr())
	{
		return StructPtr->GetMutablePtr<FStateTreeConditionBase>();
	}
	return nullptr;
}

UObject* FStateTreeConditionItemDetails::GetEditorBindingsOwner() const
{
	check(TypeProperty);

	TArray<UObject*> OuterObjects;
	TypeProperty->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		return UE::StateTree::PropertyBinding::FindEditorBindingsOwner(OuterObjects[0]);
	}

	return nullptr;
}

FText FStateTreeConditionItemDetails::GetDisplayValueString() const
{
	if (FInstancedStruct* StructPtr = GetSingleStructPtr())
	{
		if (const UScriptStruct* ScriptStruct = StructPtr->GetScriptStruct())
		{
			return ScriptStruct->GetDisplayNameText();
		}
	}
	return FText();
}

TSharedRef<SWidget> FStateTreeConditionItemDetails::GenerateStructPicker()
{
	static const FName ExcludeBaseStruct(TEXT("ExcludeBaseStruct")); // TODO: move these names into one central place.
	const bool bExcludeBaseStruct = TypeProperty->HasMetaData(ExcludeBaseStruct);

	TSharedRef<FConditionItemStructFilter> StructFilter = MakeShared<FConditionItemStructFilter>();
	StructFilter->BaseStruct = BaseScriptStruct;
	StructFilter->bAllowBaseStruct = !bExcludeBaseStruct;

	FStructViewerInitializationOptions Options;
	Options.bShowUnloadedStructs = true;
	Options.bShowNoneOption = true;
	Options.StructFilter = StructFilter;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EStructViewerDisplayMode::ListView;
	Options.bAllowViewOptions = true;

	FOnStructPicked OnPicked(FOnStructPicked::CreateRaw(this, &FStateTreeConditionItemDetails::OnStructPicked));

	return SNew(SBox)
		.WidthOverride(280)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(500)
			[
				FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer").CreateStructViewer(Options, OnPicked)
			]
		];
}

void FStateTreeConditionItemDetails::OnStructPicked(const UScriptStruct* InStruct)
{
	check(TypeProperty);
	check(PropUtils);

	GEditor->BeginTransaction(LOCTEXT("OnStructPicked", "Set Struct"));

	TypeProperty->NotifyPreChange();

	if (FInstancedStruct* StructPtr = GetSingleStructPtr())
	{
		if (InStruct)
		{
			StructPtr->InitializeAs(InStruct);
			
			// Generate new ID.
			if (InStruct->IsChildOf(FStateTreeConditionBase::StaticStruct()))
			{
				FStateTreeConditionBase* Condition = StructPtr->GetMutablePtr<FStateTreeConditionBase>();
				check(Condition);
				Condition->ID = FGuid::NewGuid();
			}
		}
		else
		{
			StructPtr->Reset();
		}
	}

	TypeProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	GEditor->EndTransaction();
	TypeProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	ComboButton->SetIsOpen(false);

	PropUtils->ForceRefresh();
}

#undef LOCTEXT_NAMESPACE
