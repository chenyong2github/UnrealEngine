// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchy.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "DetailsViewWrapperObject.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Styling/SlateTypes.h"
#include "IPropertyUtilities.h"
#include "SSearchableComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "SAdvancedTransformInputBox.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Internationalization/FastDecimalFormat.h"
#include "ScopedTransaction.h"

class IPropertyHandle;

class FRigElementKeyDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FRigElementKeyDetails);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:

	ERigElementType GetElementType() const;
	FString GetElementName() const;
	void SetElementName(FString InName);
	void UpdateElementNameList();
	void OnElementNameChanged(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectionInfo);
	TSharedRef<SWidget> OnGetElementNameWidget(TSharedPtr<FString> InItem);
	FText GetElementNameAsText() const;

	/** Helper buttons. */
	TSharedPtr<SButton> UseSelectedButton;
	TSharedPtr<SButton> SelectElementButton;
	FSlateColor OnGetWidgetForeground(const TSharedPtr<SButton> Button) const;
	FSlateColor OnGetWidgetBackground(const TSharedPtr<SButton> Button) const;
	FReply OnGetSelectedClicked();
	FReply OnSelectInHierarchyClicked();
	
	TSharedPtr<IPropertyHandle> TypeHandle;
	TSharedPtr<IPropertyHandle> NameHandle;
	TArray<TSharedPtr<FString>> ElementNameList;
	UControlRigBlueprint* BlueprintBeingCustomized;
	TSharedPtr<SSearchableComboBox> SearchableComboBox;
};

class FRigUnitDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigUnitDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:

	TSharedRef<SWidget> MakeNameListItemWidget(TSharedPtr<FString> InItem);
	FText GetNameListText(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty) const;
	TSharedPtr<FString> GetCurrentlySelectedItem(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, const TArray<TSharedPtr<FString>>* InNameList) const;
	void SetNameListText(const FText& NewTypeInValue, ETextCommit::Type /*CommitInfo*/, TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, TSharedRef<IPropertyUtilities> PropertyUtilities);
	void OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, TSharedRef<IPropertyUtilities> PropertyUtilities);
	void OnNameListComboBox(TSharedPtr<FStructOnScope> InStructOnScope, FNameProperty* InProperty, const TArray<TSharedPtr<FString>>* InNameList);
	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);

	UControlRigBlueprint* BlueprintBeingCustomized;
	UControlRigGraph* GraphBeingCustomized;
	TMap<FName, TSharedPtr<SControlRigGraphPinNameListValueWidget>> NameListWidgets;
};

UENUM()
enum class ERigElementDetailsTransformComponent : uint8
{
	TranslationX,
	TranslationY,
	TranslationZ,
	RotationRoll,
	RotationPitch,
	RotationYaw,
	ScaleX,
	ScaleY,
	ScaleZ
};

class FRigComputedTransformDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FRigComputedTransformDetails);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:

	TSharedPtr<IPropertyHandle> TransformHandle;
	FEditPropertyChain PropertyChain;
	UControlRigBlueprint* BlueprintBeingCustomized;

	void OnTransformChanged(FEditPropertyChain* InPropertyChain);
};

class FRigBaseElementDetails : public IDetailCustomization
{
public:

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	URigHierarchy* GetHierarchy() const { return HierarchyBeingCustomized; }
	URigHierarchy* GetHierarchyBeingDebugged() const;
	FRigElementKey GetElementKey() const;
	FText GetName() const;
	void SetName(const FText& InNewText, ETextCommit::Type InCommitType);
	bool OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage);

	void OnStructContentsChanged(FProperty* InProperty, const TSharedRef<IPropertyUtilities> PropertyUtilities);
	bool IsSetupModeEnabled() const;

	TArray<FRigElementKey> GetElementKeys() const;

	template<typename T>
	TArray<T> GetElementsInDetailsView() const
	{
		TArray<T> Elements;
		for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
		{
			if(ObjectBeingCustomized.IsValid())
			{
				Elements.Add(ObjectBeingCustomized->GetContent<T>());
			}
		}
		return Elements;
	}

	template<typename T>
	TArray<T*> GetElementsInHierarchy() const
	{
		TArray<T*> Elements;
		if(HierarchyBeingCustomized)
		{
			for(TWeakObjectPtr<UDetailsViewWrapperObject> ObjectBeingCustomized : ObjectsBeingCustomized)
			{
				if(ObjectBeingCustomized.IsValid())
				{
					Elements.Add(HierarchyBeingCustomized->Find<T>(ObjectBeingCustomized->GetContent<FRigBaseElement>().GetKey()));
				}
			}
		}
		return Elements;
	}

	bool IsAnyElementOfType(ERigElementType InType) const;
	bool IsAnyElementNotOfType(ERigElementType InType) const;
	bool IsAnyControlOfType(ERigControlType InType) const;
	bool IsAnyControlNotOfType(ERigControlType InType) const;

	static void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule);
	virtual void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass);

protected:

	UControlRigBlueprint* BlueprintBeingCustomized;
	URigHierarchy* HierarchyBeingCustomized;
	TArray<TWeakObjectPtr<UDetailsViewWrapperObject>> ObjectsBeingCustomized;
};

namespace ERigTransformElementDetailsTransform
{
	enum Type
	{
		Initial,
		Current,
		Offset,
		Minimum,
		Maximum,
		Max
	};
}

class FRigTransformElementDetails : public FRigBaseElementDetails
{
public:

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/** FRigBaseElementDetails interface */
	virtual void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass) override;

	virtual void CustomizeTransform(IDetailLayoutBuilder& DetailBuilder);

protected:

	bool IsCurrentLocalEnabled() const;

	void AddChoiceWidgetRow(IDetailCategoryBuilder& InCategory, const FText& InSearchText, TSharedRef<SWidget> InWidget);

protected:

	void CreateEulerTransformValueWidgetRow(
		URigHierarchy* HierarchyBeingDebugged,
		const TArray<FRigElementKey>& Keys,
		SAdvancedTransformInputBox<FEulerTransform>::FArguments TransformWidgetArgs,
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip,
		ERigTransformElementDetailsTransform::Type CurrentTransformType,
		ERigControlValueType ValueType);

private:

	static TSharedPtr<TArray<ERigTransformElementDetailsTransform::Type>> PickedTransforms;

protected:

	TSharedPtr<FScopedTransaction> SliderTransaction;
};

class FRigBoneElementDetails : public FRigTransformElementDetails
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigBoneElementDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

class FRigControlElementDetails : public FRigTransformElementDetails
{
public:

	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigControlElementDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	void CustomizeValue(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeControl(IDetailLayoutBuilder& DetailBuilder);
	void CustomizeShape(IDetailLayoutBuilder& DetailBuilder);

	/** FRigBaseElementDetails interface */
	virtual void RegisterSectionMappings(FPropertyEditorModule& PropertyEditorModule, UClass* InClass) override;

	bool IsShapeEnabled() const;

	const TArray<TSharedPtr<FString>>& GetShapeNameList() const;
	const TArray<TSharedPtr<FString>>& GetControlTypeList() const;

	FText GetDisplayName() const;
	void SetDisplayName(const FText& InNewText, ETextCommit::Type InCommitType);

	void OnCopyShapeProperties();
	void OnPasteShapeProperties();

	void CreateBoolValueWidgetRow(
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip, 
		ERigControlValueType ValueType,
		TAttribute<EVisibility> Visibility = EVisibility::Visible);

	void CreateFloatValueWidgetRow(
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip, 
		ERigControlValueType ValueType,
		TAttribute<EVisibility> Visibility = EVisibility::Visible);

	void CreateIntegerValueWidgetRow(
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip, 
		ERigControlValueType ValueType,
		TAttribute<EVisibility> Visibility = EVisibility::Visible);

	void CreateEnumValueWidgetRow(
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip, 
		ERigControlValueType ValueType,
		TAttribute<EVisibility> Visibility = EVisibility::Visible);

	void CreateVector2DValueWidgetRow(
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip, 
		ERigControlValueType ValueType,
		TAttribute<EVisibility> Visibility = EVisibility::Visible);


	// this is a template since we specialize it further down for
	// a 'nearly equals' implementation for floats and math types.
	template<typename T>
	static bool Equals(const T& A, const T& B)
	{
		return A == B;
	}

private:

	template<typename T>
	void CreateNumericValueWidgetRow(
		IDetailCategoryBuilder& CategoryBuilder, 
		const FText& Label, 
		const FText& Tooltip, 
		ERigControlValueType ValueType,
		TAttribute<EVisibility> Visibility = EVisibility::Visible)
	{
		const bool bCurrent = ValueType == ERigControlValueType::Current;
		const bool bInitial = ValueType == ERigControlValueType::Initial;
		const bool bShowToggle = (ValueType == ERigControlValueType::Minimum) || (ValueType == ERigControlValueType::Maximum);
		
		TArray<FRigElementKey> Keys = GetElementKeys();
		URigHierarchy* HierarchyBeingDebugged = GetHierarchyBeingDebugged();
		URigHierarchy* HierarchyToChange = bCurrent ? HierarchyBeingDebugged : HierarchyBeingCustomized;
		Keys = HierarchyBeingDebugged->SortKeys(Keys);

		TSharedPtr<SNumericEntryBox<T>> NumericEntryBox;
		
		FDetailWidgetRow& WidgetRow = CategoryBuilder.AddCustomRow(Label);
		TAttribute<ECheckBoxState> ToggleChecked;
		FOnCheckStateChanged OnToggleChanged;

		if(bShowToggle)
		{
			ToggleChecked = TAttribute<ECheckBoxState>::CreateLambda(
				[ValueType, Keys, HierarchyBeingDebugged]() -> ECheckBoxState
				{
					TOptional<bool> FirstValue;

					for(const FRigElementKey& Key : Keys)
					{
						if(const FRigControlElement* ControlElement = HierarchyBeingDebugged->Find<FRigControlElement>(Key))
						{
							if(ControlElement->Settings.LimitEnabled.Num() == 1)
							{
								const bool Value = ControlElement->Settings.LimitEnabled[0].GetForValueType(ValueType);
								if(FirstValue.IsSet())
								{
									if(FirstValue.GetValue() != Value)
									{
										return ECheckBoxState::Undetermined;
									}
								}
								else
								{
									FirstValue = Value;
								}
							}
						}
					}

					if(!ensure(FirstValue.IsSet()))
					{
						return ECheckBoxState::Undetermined;
					}

					return FirstValue.GetValue() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				});

			OnToggleChanged = FOnCheckStateChanged::CreateLambda(
				[ValueType, Keys, HierarchyToChange](ECheckBoxState InValue)
				{
					if(InValue == ECheckBoxState::Undetermined)
					{
						return;
					}

					FScopedTransaction Transaction(NSLOCTEXT("ControlRigElementDetails", "ChangeLimitToggle", "Change Limit Toggle"));
					for(const FRigElementKey& Key : Keys)
					{
						if(FRigControlElement* ControlElement = HierarchyToChange->Find<FRigControlElement>(Key))
						{
							if(ControlElement->Settings.LimitEnabled.Num() == 1)
							{
								HierarchyToChange->Modify();
								ControlElement->Settings.LimitEnabled[0].SetForValueType(ValueType, InValue == ECheckBoxState::Checked);
								HierarchyToChange->SetControlSettings(ControlElement, ControlElement->Settings, true, true, true);
							}
						}
					}
				});
		}

		auto OnValueChanged = [ValueType, Keys, HierarchyToChange, this]
			(TOptional<T> InValue, ETextCommit::Type InCommitType, bool bSetupUndo)
			{
				if(!InValue.IsSet())
				{
					return;
				}

				const T Value = InValue.GetValue();
				
				for(const FRigElementKey& Key : Keys)
				{
					const T PreviousValue = HierarchyToChange->GetControlValue(Key, ValueType).Get<T>();
					if(!Equals(PreviousValue, Value))
					{
						if(!SliderTransaction.IsValid())
						{
							SliderTransaction = MakeShareable(new FScopedTransaction(NSLOCTEXT("ControlRigElementDetails", "ChangeValue", "Change Value")));
							HierarchyToChange->Modify();
						}
						HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<T>(Value), ValueType, bSetupUndo, bSetupUndo);
					}
				}

				if(bSetupUndo)
				{
					SliderTransaction.Reset();
				}
			};

		WidgetRow
		.Visibility(Visibility)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(Label)
			.ToolTipText(Tooltip)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SAssignNew(NumericEntryBox, SNumericEntryBox<T>)
	        .Font(FEditorStyle::GetFontStyle(TEXT("MenuItem.Font")))
	        .AllowSpin(ValueType == ERigControlValueType::Current || ValueType == ERigControlValueType::Initial)
	        .LinearDeltaSensitivity(1)
			.Delta(0.01f)
	        .Value_Lambda([ValueType, Keys, HierarchyBeingDebugged]() -> TOptional<T>
	        {
		        const T FirstValue = HierarchyBeingDebugged->GetControlValue<T>(Keys[0], ValueType);
				for(int32 Index = 1; Index < Keys.Num(); Index++)
				{
					const T SecondValue = HierarchyBeingDebugged->GetControlValue<T>(Keys[Index], ValueType);
					if(FirstValue != SecondValue)
					{
						return TOptional<T>();
					}
				}
				return FirstValue;
	        })
	        .OnValueChanged_Lambda([ValueType, Keys, HierarchyToChange, OnValueChanged](TOptional<T> InValue)
	        {
        		OnValueChanged(InValue, ETextCommit::Default, false);
	        })
	        .OnValueCommitted_Lambda([ValueType, Keys, HierarchyToChange, OnValueChanged](TOptional<T> InValue, ETextCommit::Type InCommitType)
	        {
        		OnValueChanged(InValue, InCommitType, true);
	        })
	        .MinSliderValue_Lambda([ValueType, Keys, HierarchyBeingDebugged]() -> TOptional<T>
			 {
				 if(ValueType == ERigControlValueType::Current || ValueType == ERigControlValueType::Initial)
				 {
			 		return HierarchyBeingDebugged->GetControlValue<T>(Keys[0], ERigControlValueType::Minimum);
				 }
				 return TOptional<T>();
			 })
			 .MaxSliderValue_Lambda([ValueType, Keys, HierarchyBeingDebugged]() -> TOptional<T>
			 {
		 		if(ValueType == ERigControlValueType::Current || ValueType == ERigControlValueType::Initial)
		 		{
					 return HierarchyBeingDebugged->GetControlValue<T>(Keys[0], ERigControlValueType::Maximum);
				 }
				 return TOptional<T>();
			 })
			 .DisplayToggle(bShowToggle)
			 .ToggleChecked(ToggleChecked)
			 .OnToggleChanged(OnToggleChanged)
			 .UndeterminedString(NSLOCTEXT("FRigControlElementDetails", "MultipleValues", "Multiple Values"))
		]
		.CopyAction(FUIAction(
		FExecuteAction::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]()
			{
				const T FirstValue = HierarchyBeingDebugged->GetControlValue<T>(Keys[0], ValueType);
				const FString Content = FastDecimalFormat::NumberToString(
					FirstValue,
					FastDecimalFormat::GetCultureAgnosticFormattingRules(),
					FNumberFormattingOptions());
				FPlatformApplicationMisc::ClipboardCopy(*Content);
			}),
			FCanExecuteAction())
		)
		.PasteAction(FUIAction(
			FExecuteAction::CreateLambda([ValueType, Keys, HierarchyToChange]()
			{
				FString Content;
				FPlatformApplicationMisc::ClipboardPaste(Content);
				if(Content.IsEmpty())
				{
					return;
				}

				T Value = T(0);
				if(FastDecimalFormat::StringToNumber(
					*Content,
					Content.Len(),
					FastDecimalFormat::GetCultureAgnosticFormattingRules(),
					FNumberParsingOptions(),
					Value))
				{
					FScopedTransaction Transaction(NSLOCTEXT("ControlRigElementDetails", "ChangeValue", "Change Value"));
					for(const FRigElementKey& Key : Keys)
					{
						HierarchyToChange->Modify();
						HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<T>(Value), ValueType, true, true); 
					}
				}
			}),
			FCanExecuteAction())
		);

		if(ValueType == ERigControlValueType::Current || ValueType == ERigControlValueType::Initial)
		{
			WidgetRow.OverrideResetToDefault(FResetToDefaultOverride::Create(
				TAttribute<bool>::CreateLambda([ValueType, Keys, HierarchyBeingDebugged]() -> bool
				{
					const T FirstValue = HierarchyBeingDebugged->GetControlValue<T>(Keys[0], ValueType);
					const T ReferenceValue = ValueType == ERigControlValueType::Initial ? T(0) :
						HierarchyBeingDebugged->GetControlValue<T>(Keys[0], ERigControlValueType::Initial);

					return !FRigControlElementDetails::Equals(FirstValue, ReferenceValue);
				}),
				FSimpleDelegate::CreateLambda([ValueType, Keys, HierarchyToChange]()
				{
					FScopedTransaction Transaction(NSLOCTEXT("ControlRigElementDetails", "ResetValueToDefault", "Reset Value To Default"));
					for(const FRigElementKey& Key : Keys)
					{
						const T ReferenceValue = ValueType == ERigControlValueType::Initial ? T(0) :
							HierarchyToChange->GetControlValue<T>(Keys[0], ERigControlValueType::Initial);
						HierarchyToChange->Modify();
						HierarchyToChange->SetControlValue(Key, FRigControlValue::Make<T>(ReferenceValue), ValueType, true, true); 
					}
				})
			));
		}
	}

	TArray<TSharedPtr<FString>> ShapeNameList;
	static TArray<TSharedPtr<FString>> ControlTypeList;
	TSharedPtr<FRigInfluenceEntryModifier> InfluenceModifier;
	TSharedPtr<FStructOnScope> InfluenceModifierStruct;

	TSharedPtr<IPropertyHandle> ShapeNameHandle;
	TSharedPtr<IPropertyHandle> ShapeColorHandle;
	TSharedPtr<IPropertyHandle> ShapeTransformHandle;

	TArray<FRigControlElement> ControlElements;
	TArray<UDetailsViewWrapperObject*> ObjectPerControl;

	static TSharedPtr<TArray<ERigControlValueType>> PickedValueTypes;
};

template<>
inline bool FRigControlElementDetails::Equals<float>(const float& A, const float& B)
{
	return FMath::IsNearlyEqual(A, B);
}

template<>
inline bool FRigControlElementDetails::Equals<double>(const double& A, const double& B)
{
	return FMath::IsNearlyEqual(A, B);
}

template<>
inline bool FRigControlElementDetails::Equals<FVector>(const FVector& A, const FVector& B)
{
	return (A - B).IsNearlyZero();
}

template<>
inline bool FRigControlElementDetails::Equals<FRotator>(const FRotator& A, const FRotator& B)
{
	return (A - B).IsNearlyZero();
}

template<>
inline bool FRigControlElementDetails::Equals<FEulerTransform>(const FEulerTransform& A, const FEulerTransform& B)
{
	return Equals(A.Location, B.Location) && Equals(A.Rotation, B.Rotation) && Equals(A.Scale, B.Scale);
}

class FRigNullElementDetails : public FRigTransformElementDetails
{
public:

	// Makes a new instance of this detail layout class for a specific detail view requesting it
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FRigNullElementDetails);
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
