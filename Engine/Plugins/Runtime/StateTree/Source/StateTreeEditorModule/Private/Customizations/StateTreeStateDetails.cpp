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
	TSharedPtr<IPropertyHandle> TasksProperty = DetailBuilder.GetProperty(TEXT("Tasks"));
	TSharedPtr<IPropertyHandle> Tasks2Property = DetailBuilder.GetProperty(TEXT("Tasks2"));
	TSharedPtr<IPropertyHandle> EnterConditionsProperty = DetailBuilder.GetProperty(TEXT("EnterConditions"));
	TSharedPtr<IPropertyHandle> EnterConditions2Property = DetailBuilder.GetProperty(TEXT("EnterConditions2"));
	TSharedPtr<IPropertyHandle> EvaluatorsProperty = DetailBuilder.GetProperty(TEXT("Evaluators"));
	TSharedPtr<IPropertyHandle> Evaluators2Property = DetailBuilder.GetProperty(TEXT("Evaluators2"));
	TSharedPtr<IPropertyHandle> TransitionsProperty = DetailBuilder.GetProperty(TEXT("Transitions"));
	TSharedPtr<IPropertyHandle> Transitions2Property = DetailBuilder.GetProperty(TEXT("Transitions2"));
	TSharedPtr<IPropertyHandle> StateDoneTransitionProperty = DetailBuilder.GetProperty(TEXT("StateDoneTransition"));
	TSharedPtr<IPropertyHandle> StateFailedTransitionProperty = DetailBuilder.GetProperty(TEXT("StateFailedTransition"));

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	UObject* Object = Objects.IsEmpty() ? nullptr : Objects[0].Get();
	
	if (IsV2(Object))
	{
		EnterConditionsProperty->MarkHiddenByCustomization();
		EnterConditionsProperty = EnterConditions2Property;
		EvaluatorsProperty->MarkHiddenByCustomization();
		EvaluatorsProperty = Evaluators2Property;
		TasksProperty->MarkHiddenByCustomization();
		TasksProperty = Tasks2Property;
		TransitionsProperty->MarkHiddenByCustomization();
		TransitionsProperty = Transitions2Property;
		StateDoneTransitionProperty->MarkHiddenByCustomization();
		StateFailedTransitionProperty->MarkHiddenByCustomization();
	}
	else
	{
		EnterConditions2Property->MarkHiddenByCustomization();
		Evaluators2Property->MarkHiddenByCustomization();
		Tasks2Property->MarkHiddenByCustomization();
		Transitions2Property->MarkHiddenByCustomization();
	}

	IDetailCategoryBuilder& StateCategory = DetailBuilder.EditCategory(TEXT("State"), LOCTEXT("StateDetailsState", "State"));
	StateCategory.SetSortOrder(0);

	MakeArrayCategory(DetailBuilder, "Evaluators", LOCTEXT("StateDetailsEvaluators", "Evaluators"), 1, EvaluatorsProperty);

	MakeArrayCategory(DetailBuilder, "Enter Conditions", LOCTEXT("StateDetailsEnterConditions", "Enter Conditions"), 2, EnterConditionsProperty);

	MakeArrayCategory(DetailBuilder, "Tasks", LOCTEXT("StateDetailsTasks", "Tasks"), 3, TasksProperty);

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

bool FStateTreeStateDetails::IsV2(const UObject* Object) const
{
	if (!Object)
	{
		return false;
	}
	if (const UStateTree* StateTree = Object->GetTypedOuter<UStateTree>())
	{
		return StateTree->IsV2();
	}
	return false;
}


#undef LOCTEXT_NAMESPACE
