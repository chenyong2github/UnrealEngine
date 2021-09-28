// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeVariableDetails.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "Styling/SlateIconFinder.h"
#include "IPropertyUtilities.h"
#include "Editor.h"
#include "StateTreeVariableProvider.h"
#include "StateTreeVariable.h"
#include "StateTreePropertyHelpers.h"
#include "ScopedTransaction.h"
#include "StateTree.h"
#include "StateTreeDelegates.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeVariableDetails::MakeInstance()
{
	return MakeShareable( new FStateTreeVariableDetails);
}

void FStateTreeVariableDetails::CustomizeHeader( TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils )
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	IDProperty = StructProperty->GetChildHandle(TEXT("ID"));
	NameProperty = StructProperty->GetChildHandle(TEXT("Name"));
	ValueProperty = StructProperty->GetChildHandle(TEXT("Value"));

	TypeProperty = StructProperty->GetChildHandle(TEXT("Type"));
	BaseClassProperty = StructProperty->GetChildHandle(TEXT("BaseClass"));
	BindingModeProperty = StructProperty->GetChildHandle(TEXT("BindingMode"));

	const FSimpleDelegate& ForceRefresh = FSimpleDelegate::CreateSP(this, &FStateTreeVariableDetails::OnForceRefresh);
	TypeProperty->SetOnPropertyValueChanged(ForceRefresh);
	BaseClassProperty->SetOnPropertyValueChanged(ForceRefresh);
	BindingModeProperty->SetOnPropertyValueChanged(ForceRefresh);
	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeVariableDetails::OnIdentifierChanged);

	CacheExposedVariables();

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Value inputs
			// int
			+ SHorizontalBox::Slot()
			[
				SNew(SNumericEntryBox<int32>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Value(this, &FStateTreeVariableDetails::GetIntValue)
				.OnValueCommitted(this, &FStateTreeVariableDetails::SetIntValue)
				.Visibility(this, &FStateTreeVariableDetails::IsIntVisible)
			]
			// float
			+ SHorizontalBox::Slot()
			[
				SNew(SNumericEntryBox<float>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Value(this, &FStateTreeVariableDetails::GetFloatValue)
				.OnValueCommitted(this, &FStateTreeVariableDetails::SetFloatValue)
				.Visibility(this, &FStateTreeVariableDetails::IsFloatVisible)
			]
			// bool
			+ SHorizontalBox::Slot()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FStateTreeVariableDetails::GetBoolValue)
				.OnCheckStateChanged(this, &FStateTreeVariableDetails::SetBoolValue)
				.Visibility(this, &FStateTreeVariableDetails::IsBoolVisible)
			]
			// vector
			+ SHorizontalBox::Slot()
			[
				SNew(SVectorInputBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.X(this, &FStateTreeVariableDetails::GetVectorXValue)
				.Y(this, &FStateTreeVariableDetails::GetVectorYValue)
				.Z(this, &FStateTreeVariableDetails::GetVectorZValue)
				.OnXCommitted(this, &FStateTreeVariableDetails::SetVectorXValue)
				.OnYCommitted(this, &FStateTreeVariableDetails::SetVectorYValue)
				.OnZCommitted(this, &FStateTreeVariableDetails::SetVectorZValue)
				.AllowSpin(false)
				.Visibility(this, &FStateTreeVariableDetails::IsVectorVisible)
			]
			// Object
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Empty", "Empty"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Visibility(this, &FStateTreeVariableDetails::IsObjectOrVoidVisible)
			]
			// Variable
			+ SHorizontalBox::Slot()
			[
				SNew(SHorizontalBox)
				.Visibility(this, &FStateTreeVariableDetails::IsVariableVisible)
				// Icon slot
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(this, &FStateTreeVariableDetails::GetCurrentKeyIcon)
					.ColorAndOpacity(FLinearColor::White)
				]
				// Name slot
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(3, 0)
				[
					SNew(STextBlock)
					.Text(this, &FStateTreeVariableDetails::GetCurrentKeyDesc)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
			// Binding button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6, 0, 0, 0)
			[
				SNew(SComboButton)
				.OnGetMenuContent(this, &FStateTreeVariableDetails::OnGetKeyContent)
				.ContentPadding(FMargin(2.0f, 0.0f))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Bind", "Bind"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		];
}

void FStateTreeVariableDetails::CustomizeChildren( TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils )
{
}

void FStateTreeVariableDetails::OnIdentifierChanged(const UStateTree& StateTree)
{
	OnForceRefresh();
	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeVariableDetails::OnForceRefresh()
{
	CacheExposedVariables();
}

void FStateTreeVariableDetails::CacheExposedVariables()
{
	KeyValues.Reset();

	const TOptional<EStateTreeVariableType> TypeOpt = GetVariableType();
	const TOptional<EStateTreeVariableBindingMode> BindingModeOpt = GetBindingMode();

	if (TypeOpt.IsSet() && BindingModeOpt.IsSet())
	{
		const EStateTreeVariableType Type = TypeOpt.GetValue();
		const EStateTreeVariableBindingMode BindingMode = BindingModeOpt.GetValue();
		const TOptional<UClass*> BaseClassOpt = GetBaseClass();
		TSubclassOf<UObject> BaseClass = BaseClassOpt.IsSet() ? BaseClassOpt.GetValue() : nullptr;

		TArray<UObject*> OuterObjects;
		StructProperty->GetOuterObjects(OuterObjects);
		for (int32 ObjectIdx = 0; ObjectIdx < OuterObjects.Num(); ObjectIdx++)
		{
			// Find outer which can provide us the keys
			IStateTreeVariableProvider* Provider = nullptr;
			for (UObject* OuterObj = OuterObjects[ObjectIdx]; OuterObj; OuterObj = OuterObj->GetOuter())
			{
				Provider = Cast<IStateTreeVariableProvider>(OuterObj);
				if (Provider)
				{
					break;
				}
			}

			if (Provider)
			{
				FStateTreeVariableLayout Variables;
				Provider->GetVisibleVariables(Variables);

				for (const FStateTreeVariableDesc& Var : Variables.Variables)
				{
					if (BindingMode == EStateTreeVariableBindingMode::Any || (Var.Type == Type && Var.BaseClass == BaseClass))
					{
						FVariableKey Key;
						Key.Name = Var.Name;
						Key.ID = Var.ID;
						Key.Type = Var.Type;
						Key.BaseClass = Var.BaseClass;
						KeyValues.Add(Key);
					}
				}
				break;
			}
		}
	}
}

TSharedRef<SWidget> FStateTreeVariableDetails::OnGetKeyContent() const
{
	FMenuBuilder MenuBuilder(true, NULL);

	const TOptional<EStateTreeVariableBindingMode> BindingModeOpt = GetBindingMode();
	const bool bCanBeValue = BindingModeOpt.IsSet() && BindingModeOpt.GetValue() == EStateTreeVariableBindingMode::Typed && HasValueProperty();

	if (bCanBeValue)
	{
		FUIAction ValueItemAction(FExecuteAction::CreateSP(const_cast<FStateTreeVariableDetails*>(this), &FStateTreeVariableDetails::OnKeyComboChange, -1));
		MenuBuilder.AddMenuEntry(LOCTEXT("LocalValue", "Local Value"), TAttribute<FText>(), FSlateIcon(), ValueItemAction);
		MenuBuilder.AddMenuSeparator();
	}
	else
	{
		FUIAction ValueItemAction(FExecuteAction::CreateSP(const_cast<FStateTreeVariableDetails*>(this), &FStateTreeVariableDetails::OnKeyComboChange, -1));
		MenuBuilder.AddMenuEntry(LOCTEXT("ClearItem", "Clear"), TAttribute<FText>(), FSlateIcon(), ValueItemAction);
		MenuBuilder.AddMenuSeparator();
	}

	if (KeyValues.Num() > 0)
	{
		for (int32 Idx = 0; Idx < KeyValues.Num(); Idx++)
		{
			const FVariableKey& Key = KeyValues[Idx];

			FUIAction ItemAction(FExecuteAction::CreateSP(const_cast<FStateTreeVariableDetails*>(this), &FStateTreeVariableDetails::OnKeyComboChange, Idx));
			MenuBuilder.AddMenuEntry(FText::FromName(Key.Name), TAttribute<FText>(), FSlateIcon(), ItemAction);
		}
	}
	else
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoVariablesToBindTo", "No Variables to bind to."), TAttribute<FText>(), FSlateIcon(), FUIAction());
	}

	return MenuBuilder.MakeWidget();
}

bool FStateTreeVariableDetails::HasValueProperty() const
{
	return ValueProperty.IsValid();
}

bool FStateTreeVariableDetails::IsBound() const
{
	FGuid ID;
	if (IDProperty)
	{
		UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);
	}

	return ID.IsValid();
}

FText FStateTreeVariableDetails::GetCurrentKeyDesc() const
{
	const bool bCanBeValue = GetBindingMode().Get(EStateTreeVariableBindingMode::Any) == EStateTreeVariableBindingMode::Typed && HasValueProperty();

	FName Name;
	FGuid ID;
	if (NameProperty && IDProperty)
	{
		NameProperty->GetValue(Name);
		UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);
	}

	const bool bIsBound = ID.IsValid();

	// Potentially Bound
	if (bIsBound)
	{
		int KeyIdx = INDEX_NONE;
		for (int32 Idx = 0; Idx < KeyValues.Num(); Idx++)
		{
			const FVariableKey& Key = KeyValues[Idx];
			if (Key.ID == ID)
			{
				return FText::FromName(Key.Name);
				break;
			}
		}
		FFormatNamedArguments Args;
		Args.Add(TEXT("Identifier"), FText::FromName(Name));
		return FText::Format(LOCTEXT("InvalidReference", "Invalid Reference {Identifier}"), Args);
	}

	return FText(); // Empty name
}

const FSlateBrush* FStateTreeVariableDetails::GetCurrentKeyIcon() const
{
	const bool bCanBeValue = GetBindingMode().Get(EStateTreeVariableBindingMode::Any) == EStateTreeVariableBindingMode::Typed && HasValueProperty();

	FGuid ID;
	if (NameProperty && IDProperty)
	{
		UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);
	}

	const bool bIsBound = ID.IsValid();

	// Potentially Bound
	if (bIsBound)
	{
		int KeyIdx = INDEX_NONE;
		for (int32 Idx = 0; Idx < KeyValues.Num(); Idx++)
		{
			const FVariableKey& Key = KeyValues[Idx];
			if (Key.ID == ID)
			{
				return UE::StateTree::PropertyHelpers::GetTypeIcon(Key.Type);
			}
		}
	}

	return nullptr;
}

void FStateTreeVariableDetails::OnKeyComboChange(int32 Index)
{
	if (Index == -1)
	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("SetPropertyValue", "Set {0}"), StructProperty->GetPropertyDisplayName()));

		// Clear or Local Value
		if (NameProperty && IDProperty)
		{
			NameProperty->SetValue(FName(), EPropertyValueSetFlags::NotTransactable);
			UE::StateTree::PropertyHelpers::SetStructValue<FGuid>(IDProperty, FGuid(), EPropertyValueSetFlags::NotTransactable);
		}

		// Clear the associated type for Any binding, it will be updated when reference is chosen.
		const TOptional<EStateTreeVariableBindingMode> BindingModeOpt = GetBindingMode();
		if (BindingModeOpt.IsSet() && BindingModeOpt.GetValue() == EStateTreeVariableBindingMode::Any)
		{
			if (TypeProperty && BaseClassProperty)
			{
				// Note: Set BaseClass first. Condition details is listening to changed in property type, and base class should be set up before we hit hte callback.
				BaseClassProperty->SetValue((UObject*)nullptr, EPropertyValueSetFlags::NotTransactable);
				TypeProperty->SetValue((uint8)EStateTreeVariableType::Void, EPropertyValueSetFlags::NotTransactable);
			}
		}
	}
	else
	{
		// Set reference.
		if (KeyValues.IsValidIndex(Index))
		{
			const FVariableKey& Key = KeyValues[Index];
			if (NameProperty && IDProperty)
			{
				FScopedTransaction Transaction(FText::Format(LOCTEXT("SetPropertyValue", "Set {0}"), StructProperty->GetPropertyDisplayName()));

				NameProperty->SetValue(Key.Name, EPropertyValueSetFlags::NotTransactable);
				UE::StateTree::PropertyHelpers::SetStructValue<FGuid>(IDProperty, Key.ID, EPropertyValueSetFlags::NotTransactable);

				const TOptional<EStateTreeVariableBindingMode> BindingModeOpt = GetBindingMode();
				if (BindingModeOpt.IsSet() && BindingModeOpt.GetValue() == EStateTreeVariableBindingMode::Any)
				{
					// Note: Set BaseClass first. Condition details is listening to changed in property type, and base class should be set up before we hit hte callback.
					// Copy base class from the variable.
					if (BaseClassProperty)
					{
						BaseClassProperty->SetValue(Key.BaseClass, EPropertyValueSetFlags::NotTransactable);
					}
					// Copy type from the variable.
					if (TypeProperty)
					{
						TypeProperty->SetValue((uint8)Key.Type, EPropertyValueSetFlags::NotTransactable);
					}
				}
			}
		}
	}
}

EVisibility FStateTreeVariableDetails::IsIntVisible() const
{
	TOptional<EStateTreeVariableType> Type = GetVariableType();
	return (!IsBound() && Type.IsSet() && Type.GetValue() == EStateTreeVariableType::Int) ? EVisibility::Visible : EVisibility::Collapsed;
}

TOptional<int32> FStateTreeVariableDetails::GetIntValue() const
{
	return UE::StateTree::PropertyHelpers::GetVariableValue<int32>(ValueProperty);
}

void FStateTreeVariableDetails::SetIntValue(int32 Value, ETextCommit::Type CommitType)
{
	UE::StateTree::PropertyHelpers::SetVariableValue<int32>(ValueProperty, Value);
}


EVisibility FStateTreeVariableDetails::IsFloatVisible() const
{
	TOptional<EStateTreeVariableType> Type = GetVariableType();
	return (!IsBound() && Type.IsSet() && Type.GetValue() == EStateTreeVariableType::Float) ? EVisibility::Visible : EVisibility::Collapsed;
}

TOptional<float> FStateTreeVariableDetails::GetFloatValue() const
{
	return UE::StateTree::PropertyHelpers::GetVariableValue<float>(ValueProperty);
}

void FStateTreeVariableDetails::SetFloatValue(float Value, ETextCommit::Type CommitType)
{
	UE::StateTree::PropertyHelpers::SetVariableValue<float>(ValueProperty, Value);
}


EVisibility FStateTreeVariableDetails::IsBoolVisible() const
{
	TOptional<EStateTreeVariableType> Type = GetVariableType();
	return (!IsBound() && Type.IsSet() && Type.GetValue() == EStateTreeVariableType::Bool) ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState FStateTreeVariableDetails::GetBoolValue() const
{
	TOptional<bool> Result = UE::StateTree::PropertyHelpers::GetVariableValue<bool>(ValueProperty);
	if (!Result.IsSet())
		return ECheckBoxState::Undetermined;
	return Result.GetValue() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FStateTreeVariableDetails::SetBoolValue(ECheckBoxState NewCheckedState)
{
	UE::StateTree::PropertyHelpers::SetVariableValue<bool>(ValueProperty, NewCheckedState == ECheckBoxState::Checked ? true : false);
}


EVisibility FStateTreeVariableDetails::IsVectorVisible() const
{
	TOptional<EStateTreeVariableType> Type = GetVariableType();
	return (!IsBound() && Type.IsSet() && Type.GetValue() == EStateTreeVariableType::Vector) ? EVisibility::Visible : EVisibility::Collapsed;
}

TOptional<float> FStateTreeVariableDetails::GetVectorXValue() const
{
	TOptional<FVector> Result = UE::StateTree::PropertyHelpers::GetVariableValue<FVector>(ValueProperty);
	return Result.IsSet() ? Result.GetValue().X : TOptional<float>();
}

TOptional<float> FStateTreeVariableDetails::GetVectorYValue() const
{
	TOptional<FVector> Result = UE::StateTree::PropertyHelpers::GetVariableValue<FVector>(ValueProperty);
	return Result.IsSet() ? Result.GetValue().Y : TOptional<float>();
}

TOptional<float> FStateTreeVariableDetails::GetVectorZValue() const
{
	TOptional<FVector> Result = UE::StateTree::PropertyHelpers::GetVariableValue<FVector>(ValueProperty);
	return Result.IsSet() ? Result.GetValue().Z : TOptional<float>();
}

void FStateTreeVariableDetails::SetVectorXValue(float X, ETextCommit::Type CommitType)
{
	FVector CurrentValue = UE::StateTree::PropertyHelpers::GetVariableValue<FVector>(ValueProperty).Get(FVector::ZeroVector);
	CurrentValue.X = X;
	UE::StateTree::PropertyHelpers::SetVariableValue<FVector>(ValueProperty, CurrentValue);
}

void FStateTreeVariableDetails::SetVectorYValue(float Y, ETextCommit::Type CommitType)
{
	FVector CurrentValue = UE::StateTree::PropertyHelpers::GetVariableValue<FVector>(ValueProperty).Get(FVector::ZeroVector);
	CurrentValue.Y = Y;
	UE::StateTree::PropertyHelpers::SetVariableValue<FVector>(ValueProperty, CurrentValue);
}

void FStateTreeVariableDetails::SetVectorZValue(float Z, ETextCommit::Type CommitType)
{
	FVector CurrentValue = UE::StateTree::PropertyHelpers::GetVariableValue<FVector>(ValueProperty).Get(FVector::ZeroVector);
	CurrentValue.Z = Z;
	UE::StateTree::PropertyHelpers::SetVariableValue<FVector>(ValueProperty, CurrentValue);
}

EVisibility FStateTreeVariableDetails::IsObjectOrVoidVisible() const
{
	TOptional<EStateTreeVariableType> Type = GetVariableType();
	return (!IsBound() && Type.IsSet() && (Type.GetValue() == EStateTreeVariableType::Object || Type.GetValue() == EStateTreeVariableType::Void)) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FStateTreeVariableDetails::IsVariableVisible() const
{
	TOptional<EStateTreeVariableBindingMode> BindingModeOpt = GetBindingMode();
	return (IsBound() || (BindingModeOpt.IsSet() && BindingModeOpt.GetValue() == EStateTreeVariableBindingMode::Any)) ? EVisibility::Visible : EVisibility::Collapsed;
}

TOptional<EStateTreeVariableType> FStateTreeVariableDetails::GetVariableType() const
{
	if (TypeProperty)
	{
		uint8 Value;
		if (TypeProperty->GetValue(Value) == FPropertyAccess::Success)
		{
			return EStateTreeVariableType(Value);
		}
	}
	return TOptional<EStateTreeVariableType>();
}

TOptional<EStateTreeVariableBindingMode> FStateTreeVariableDetails::GetBindingMode() const
{
	if (BindingModeProperty)
	{
		uint8 Value;
		if (BindingModeProperty->GetValue(Value) == FPropertyAccess::Success)
		{
			return EStateTreeVariableBindingMode(Value);
		}
	}
	return TOptional<EStateTreeVariableBindingMode>();
}

TOptional<UClass*> FStateTreeVariableDetails::GetBaseClass() const
{
	if (BaseClassProperty)
	{
		UObject* Value;
		if (BaseClassProperty->GetValue(Value) == FPropertyAccess::Success)
		{
			return Cast<UClass>(Value);
		}
	}
	return nullptr;
}


#undef LOCTEXT_NAMESPACE
