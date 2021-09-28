// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeConditionDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "StateTreeCondition.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "StateTreePropertyHelpers.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeConditionDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeConditionDetails);
}

void FStateTreeConditionDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	LeftProperty = StructProperty->GetChildHandle(TEXT("Left"));
	OperatorProperty = StructProperty->GetChildHandle(TEXT("Operator"));
	RightProperty = StructProperty->GetChildHandle(TEXT("Right"));

	if (LeftProperty)
	{
		LeftTypeProperty = LeftProperty->GetChildHandle(TEXT("Type"));
		LeftBaseClassProperty = LeftProperty->GetChildHandle(TEXT("BaseClass"));
	}
	if (RightProperty)
	{
		RightTypeProperty = RightProperty->GetChildHandle(TEXT("Type"));
		RightBaseClassProperty = RightProperty->GetChildHandle(TEXT("BaseClass"));
		RightNameProperty = RightProperty->GetChildHandle(TEXT("Name"));
		RightIDProperty = RightProperty->GetChildHandle(TEXT("ID"));
		RightValueProperty = RightProperty->GetChildHandle(TEXT("Value"));
	}

	if (LeftTypeProperty)
	{
		LeftTypeProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FStateTreeConditionDetails::OnLeftChanged));
	}

	HeaderRow
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			// Description
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FStateTreeConditionDetails::GetDescription)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				StructPropertyHandle->CreateDefaultPropertyButtonWidgets()
			]
		];
}

void FStateTreeConditionDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (LeftProperty)
	{
		IDetailPropertyRow& Property = StructBuilder.AddProperty(LeftProperty.ToSharedRef());
	}

	if (OperatorProperty)
	{
		StructBuilder.AddCustomRow(FText::GetEmpty())
			.NameContent()
			[
				OperatorProperty->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(100.f)
			.MaxDesiredWidth(150.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &FStateTreeConditionDetails::OnGetOperatorContent)
				.ContentPadding(FMargin(6.0f, 2.0f))
				.HAlign(HAlign_Center)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &FStateTreeConditionDetails::GetCurrentOperatorDesc)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Justification(ETextJustify::Center)
					.MinDesiredWidth(50.0f)
				]
			];
	}

	if (RightProperty)
	{
		IDetailPropertyRow& Property = StructBuilder.AddProperty(RightProperty.ToSharedRef());
	}
}

void FStateTreeConditionDetails::OnLeftChanged()
{
	// Check that the operator can be used with the type, if not reset operator.
	const EStateTreeVariableType LeftType = GetVariableType(LeftTypeProperty).Get(EStateTreeVariableType::Void);
	if (OperatorProperty)
	{
		const EGenericAICheck Operator = GetOperator().Get(EGenericAICheck::Equal);
		const bool bUseAllOperators = LeftType == EStateTreeVariableType::Float || LeftType == EStateTreeVariableType::Int;
		const bool bIsSimpleOperators = Operator == EGenericAICheck::Equal || Operator == EGenericAICheck::NotEqual;
		if (!bUseAllOperators && !bIsSimpleOperators)
		{
			OperatorProperty->SetValue((uint8)EGenericAICheck::Equal);
		}
	}

	// Make sure that the Right is the same type as Left.
	const EStateTreeVariableType RightType = GetVariableType(RightTypeProperty).Get(EStateTreeVariableType::Void);
	UClass* LeftBaseClass = GetBaseClass(LeftBaseClassProperty).Get(nullptr);
	UClass* RightBaseClass = GetBaseClass(RightBaseClassProperty).Get(nullptr);

	if (LeftType != RightType || LeftBaseClass != RightBaseClass)
	{
		ResetRightVariableType(LeftType, LeftBaseClass);
	}
}

void FStateTreeConditionDetails::OnOperatorComboChange(EGenericAICheck Operator)
{
	if (OperatorProperty)
		OperatorProperty->SetValue((uint8)Operator);
}

TSharedRef<SWidget> FStateTreeConditionDetails::OnGetOperatorContent() const
{
	FMenuBuilder MenuBuilder(true, nullptr);

	EStateTreeVariableType VarType = GetVariableType(LeftTypeProperty).Get(EStateTreeVariableType::Void);

	FUIAction EqualAction(FExecuteAction::CreateSP(const_cast<FStateTreeConditionDetails*>(this), &FStateTreeConditionDetails::OnOperatorComboChange, EGenericAICheck::Equal));
	MenuBuilder.AddMenuEntry(LOCTEXT("OperatorEqual", "=="), TAttribute<FText>(), FSlateIcon(), EqualAction);

	FUIAction NotEqualAction(FExecuteAction::CreateSP(const_cast<FStateTreeConditionDetails*>(this), &FStateTreeConditionDetails::OnOperatorComboChange, EGenericAICheck::NotEqual));
	MenuBuilder.AddMenuEntry(LOCTEXT("OperatorNotEqual", "!="), TAttribute<FText>(), FSlateIcon(), NotEqualAction);

	if (VarType == EStateTreeVariableType::Float || VarType == EStateTreeVariableType::Int)
	{
		FUIAction LessAction(FExecuteAction::CreateSP(const_cast<FStateTreeConditionDetails*>(this), &FStateTreeConditionDetails::OnOperatorComboChange, EGenericAICheck::Less));
		MenuBuilder.AddMenuEntry(LOCTEXT("OperatorLess", "<"), TAttribute<FText>(), FSlateIcon(), LessAction);

		FUIAction LessOrEqualAction(FExecuteAction::CreateSP(const_cast<FStateTreeConditionDetails*>(this), &FStateTreeConditionDetails::OnOperatorComboChange, EGenericAICheck::LessOrEqual));
		MenuBuilder.AddMenuEntry(LOCTEXT("OperatorLessOrEqual", "<="), TAttribute<FText>(), FSlateIcon(), LessOrEqualAction);

		FUIAction GreaterAction(FExecuteAction::CreateSP(const_cast<FStateTreeConditionDetails*>(this), &FStateTreeConditionDetails::OnOperatorComboChange, EGenericAICheck::Greater));
		MenuBuilder.AddMenuEntry(LOCTEXT("OperatorGreater", ">"), TAttribute<FText>(), FSlateIcon(), GreaterAction);

		FUIAction GreaterOrEqualAction(FExecuteAction::CreateSP(const_cast<FStateTreeConditionDetails*>(this), &FStateTreeConditionDetails::OnOperatorComboChange, EGenericAICheck::GreaterOrEqual));
		MenuBuilder.AddMenuEntry(LOCTEXT("OperatorGreaterOrEqual", ">="), TAttribute<FText>(), FSlateIcon(), GreaterOrEqualAction);
	}

	return MenuBuilder.MakeWidget();
}

FText FStateTreeConditionDetails::GetCurrentOperatorDesc() const
{
	const EGenericAICheck Operator = GetOperator().Get(EGenericAICheck::Equal);

	switch (Operator)
	{
	case EGenericAICheck::Equal:
		return LOCTEXT("OperatorEqual", "==");
		break;
	case EGenericAICheck::NotEqual:
		return LOCTEXT("OperatorNotEqual", "!=");
		break;
	case EGenericAICheck::Greater:
		return LOCTEXT("OperatorGreater", ">");
		break;
	case EGenericAICheck::GreaterOrEqual:
		return LOCTEXT("OperatorGreaterOrEqual", ">=");
		break;
	case EGenericAICheck::Less:
		return LOCTEXT("OperatorLess", "<");
		break;
	case EGenericAICheck::LessOrEqual:
		return LOCTEXT("OperatorLessOrEqual", "<=");
		break;
	default:
		return LOCTEXT("OperatorUnknown", "??");
		break;
	}
}

TOptional<EStateTreeVariableType> FStateTreeConditionDetails::GetVariableType(const TSharedPtr<IPropertyHandle>& PropertyHandle) const
{
	if (PropertyHandle)
	{
		uint8 Value;
		if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
		{
			return EStateTreeVariableType(Value);
		}
	}
	return TOptional<EStateTreeVariableType>();

}

TOptional<UClass*> FStateTreeConditionDetails::GetBaseClass(const TSharedPtr<IPropertyHandle>& PropertyHandle) const
{
	if (PropertyHandle)
	{
		UObject* Value;
		if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
		{
			return Cast<UClass>(Value);
		}
	}
	return nullptr;
}

void FStateTreeConditionDetails::ResetRightVariableType(EStateTreeVariableType NewType, UClass* NewBaseClass)
{
	FScopedTransaction Transaction(FText::Format(LOCTEXT("SetPropertyValue", "Set {0}"), RightTypeProperty->GetPropertyDisplayName()));

	if (RightTypeProperty)
	{
		bool bValueTypeChanged = false;
		uint8 OldType;
		if (RightTypeProperty->GetValue(OldType) == FPropertyAccess::Success)
		{
			bValueTypeChanged = NewType != (EStateTreeVariableType)OldType;
		}

		RightTypeProperty->SetValue((uint8)NewType, EPropertyValueSetFlags::NotTransactable);

		if (bValueTypeChanged)
		{
			switch (NewType)
			{
			case EStateTreeVariableType::Float:
				UE::StateTree::PropertyHelpers::SetVariableValue<float>(RightValueProperty, 0.0f, EPropertyValueSetFlags::NotTransactable);
				break;
			case EStateTreeVariableType::Int:
				UE::StateTree::PropertyHelpers::SetVariableValue<int32>(RightValueProperty, 0, EPropertyValueSetFlags::NotTransactable);
				break;
			case EStateTreeVariableType::Bool:
				UE::StateTree::PropertyHelpers::SetVariableValue<bool>(RightValueProperty, false, EPropertyValueSetFlags::NotTransactable);
				break;
			case EStateTreeVariableType::Vector:
				UE::StateTree::PropertyHelpers::SetVariableValue<FVector>(RightValueProperty, FVector::ZeroVector, EPropertyValueSetFlags::NotTransactable);
				break;
			case EStateTreeVariableType::Object:
				// fallthrough
			case EStateTreeVariableType::Void:
				UE::StateTree::PropertyHelpers::SetVariableValue<int32>(RightValueProperty, 0, EPropertyValueSetFlags::NotTransactable);	// Not representation, just clear to int32.
				break;
			default:
				ensureMsgf(false, TEXT("Unhandled variable type."));
				break;
			}
		}
	}

	if (RightBaseClassProperty)
	{
		RightBaseClassProperty->SetValue(NewBaseClass, EPropertyValueSetFlags::NotTransactable);
	}

	// Reset binding
	if (RightNameProperty)
	{
		RightNameProperty->SetValue(FName(), EPropertyValueSetFlags::NotTransactable);
	}
	if (RightIDProperty)
	{
		UE::StateTree::PropertyHelpers::SetStructValue<FGuid>(RightIDProperty, FGuid(), EPropertyValueSetFlags::NotTransactable);
	}

}

FText FStateTreeConditionDetails::GetDescription() const
{
	if (StructProperty)
	{
		TArray<void*> RawData;
		StructProperty->AccessRawData(RawData);
		if (RawData.Num() == 1)
		{
			FStateTreeCondition* Condition = static_cast<FStateTreeCondition*>(RawData[0]);
			if (Condition != nullptr)
			{
				return Condition->GetDescription();
			}
		}
		else
		{
			return LOCTEXT("MultipleSelected", "Multiple selected");
		}

	}
	return FText::GetEmpty();
}

TOptional<EGenericAICheck> FStateTreeConditionDetails::GetOperator() const
{
	if (OperatorProperty)
	{
		uint8 Value;
		if (OperatorProperty->GetValue(Value) == FPropertyAccess::Success)
		{
			return EGenericAICheck(Value);
		}
	}
	return TOptional<EGenericAICheck>();
}

#undef LOCTEXT_NAMESPACE
