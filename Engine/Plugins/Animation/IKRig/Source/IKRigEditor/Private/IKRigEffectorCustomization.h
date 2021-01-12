// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "IKRigDataTypes.h"

class UIKRigSolver;
class UIKRigController;
class SSearchableComboBox;

//////////////////////////////////////////////////////////////////////////
// FIKRigEffectorCustomization

class FIKRigEffectorCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	void SetSolverDefinition(TSharedRef<IPropertyHandle> StructPropertyHandle);
	virtual void SetPropertyHandle(TSharedRef<IPropertyHandle> StructPropertyHandle);
	TSharedPtr<IPropertyHandle> FindStructMemberProperty(TSharedRef<IPropertyHandle> PropertyHandle, const FName& PropertyName);

	// Property to change after bone has been picked
	TSharedPtr<IPropertyHandle> EffectorBoneProperty;

	// Target Skeleton this widget is referencing
	TWeakObjectPtr<UIKRigSolver>		IKRigSolver;
	TWeakObjectPtr<UIKRigController>	IKRigController;

	// Goal update option
	TSharedPtr<SSearchableComboBox>	EffectorGoalComboBox;
	TArray<TSharedPtr<FString>>	EffectorGoalComboList;
	FIKRigEffector*	Effector;								

	TSharedRef<SWidget> MakeEffectorGoalComboWidget(TSharedPtr<FString> InItem);
	// combo box
	void OnEffectorGoalSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	// edit box
	void OnEffectorGoalChanged(const FText& InText, ETextCommit::Type CommitType);

	FText GetEffectorGoalComboBoxContent() const;
	// tool tip test
	FText GetEffectorGoalComboBoxToolTip() const;
	// when combo box is opened
	void OnEffectorGoalComboOpening();
	TSharedPtr<FString> GetEffectorGoalString() const;
	// this is possibly renamed OR reusing same name
	FName GetSelectedEffectorGoal() const;

	void SetNewGoalName(FName Name);

	const TIKRigEffectorMap<FName>& GetEffectorMap() const;
	const TArray<FName>& GetAvailableGoals() const;

	void RefreshEffectorGoals();
};