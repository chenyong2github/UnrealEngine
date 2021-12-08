// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeStateDetails.h"
#include "Misc/MessageDialog.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ComponentVisualizer.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "Editor.h"
#include "StateTreeEditorData.h"
#include "StateTreeViewModel.h"
#include "StateTree.h"


#define LOCTEXT_NAMESPACE "StateTreeEditor"


TSharedRef<IDetailCustomization> FStateTreeStateDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeStateDetails);
}

void FStateTreeStateDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Find StateTree associated with this panel.
	UStateTree* StateTree = nullptr;
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	for (TWeakObjectPtr<UObject>& WeakObject : Objects)
	{
		if (UObject* Object = WeakObject.Get())
		{
			if (UStateTree* OuterStateTree = Object->GetTypedOuter<UStateTree>())
			{
				StateTree = OuterStateTree;
				break;
			}
		}
	}
	const UStateTreeSchema* Schema = StateTree ? StateTree->GetSchema() : nullptr;

	TSharedPtr<IPropertyHandle> TasksProperty = DetailBuilder.GetProperty(TEXT("Tasks"));
	TSharedPtr<IPropertyHandle> SingleTaskProperty = DetailBuilder.GetProperty(TEXT("SingleTask"));
	TSharedPtr<IPropertyHandle> EnterConditionsProperty = DetailBuilder.GetProperty(TEXT("EnterConditions"));
	TSharedPtr<IPropertyHandle> EvaluatorsProperty = DetailBuilder.GetProperty(TEXT("Evaluators"));
	TSharedPtr<IPropertyHandle> TransitionsProperty = DetailBuilder.GetProperty(TEXT("Transitions"));

	IDetailCategoryBuilder& StateCategory = DetailBuilder.EditCategory(TEXT("State"), LOCTEXT("StateDetailsState", "State"));
	StateCategory.SetSortOrder(0);

	const FName EvalCategoryName(TEXT("Evaluators"));
	if (Schema && Schema->AllowEvaluators())
	{
		MakeArrayCategory(DetailBuilder, EvalCategoryName, LOCTEXT("StateDetailsEvaluators", "Evaluators"), 1, EvaluatorsProperty);
	}
	else
	{
		DetailBuilder.EditCategory(EvalCategoryName).SetCategoryVisibility(false);
	}

	const FName EnterConditionsCategoryName(TEXT("Enter Conditions"));
	if (Schema && Schema->AllowEnterConditions())
	{
		MakeArrayCategory(DetailBuilder, EnterConditionsCategoryName, LOCTEXT("StateDetailsEnterConditions", "Enter Conditions"), 2, EnterConditionsProperty);
	}
	else
	{
		DetailBuilder.EditCategory(EnterConditionsCategoryName).SetCategoryVisibility(false);
	}

	if (Schema && Schema->AllowMultipleTasks())
	{
		const FName TasksCategoryName(TEXT("Tasks"));
		MakeArrayCategory(DetailBuilder, TasksCategoryName, LOCTEXT("StateDetailsTasks", "Tasks"), 3, TasksProperty);
		SingleTaskProperty->MarkHiddenByCustomization();
	}
	else
	{
		const FName TaskCategoryName(TEXT("Task"));
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TaskCategoryName);
		Category.SetSortOrder(3);
		
		IDetailPropertyRow& Row = Category.AddProperty(SingleTaskProperty);
		Row.ShouldAutoExpand(true);
		
		TasksProperty->MarkHiddenByCustomization();
	}

	MakeArrayCategory(DetailBuilder, "Transitions", LOCTEXT("StateDetailsTransitions", "Transitions"), 4, TransitionsProperty);
}

void FStateTreeStateDetails::MakeArrayCategory(IDetailLayoutBuilder& DetailBuilder, FName CategoryName, const FText& DisplayName, int32 SortOrder, TSharedPtr<IPropertyHandle> PropertyHandle)
{
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(CategoryName, DisplayName);
	Category.SetSortOrder(SortOrder);

	TSharedRef<SHorizontalBox> HeaderContentWidget = SNew(SHorizontalBox);
	HeaderContentWidget->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Center)
	[
		PropertyHandle->CreateDefaultPropertyButtonWidgets()
	];
	Category.HeaderContent(HeaderContentWidget);

	// Add items inline
	TSharedRef<FDetailArrayBuilder> Builder = MakeShareable(new FDetailArrayBuilder(PropertyHandle.ToSharedRef(), /*InGenerateHeader*/ false, /*InDisplayResetToDefault*/ true, /*InDisplayElementNum*/ false));
	Builder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateSP(this, &FStateTreeStateDetails::GenerateArrayElementWidget));
	Category.AddCustomBuilder(Builder, /*bForAdvanced*/ false);
}

void FStateTreeStateDetails::GenerateArrayElementWidget(TSharedRef<IPropertyHandle> PropertyHandle, int32 ArrayIndex, IDetailChildrenBuilder& ChildrenBuilder)
{
	ChildrenBuilder.AddProperty(PropertyHandle);
};

#undef LOCTEXT_NAMESPACE
