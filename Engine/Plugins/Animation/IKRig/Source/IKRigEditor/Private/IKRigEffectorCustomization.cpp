// Copyright Epic Games, Inc. All Rights Reservekd.

#include "IKRigEffectorCustomization.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IDetailChildrenBuilder.h"

#include "IKRigController.h"
#include "IKRigSolverDefinition.h"
 
#include "ScopedTransaction.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "SSearchableComboBox.h"

#define LOCTEXT_NAMESPACE	"IKRigDefinitionDetails"

/////////////////////////////////////////////////////////////////////////////////////////////
//  FIKRigEffectorCustomization
/////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FIKRigEffectorCustomization::MakeInstance()
{
	return MakeShareable(new FIKRigEffectorCustomization());
}

void FIKRigEffectorCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// find actual IKRigEffector - this is to query the data itself
	void* Data;
	FPropertyAccess::Result Result = StructPropertyHandle->GetValueData(Data);
	// if not a single value, we return, then we return
	if (Result != FPropertyAccess::Success)
	{
		// failed
		return;
	}

	// set editable skeleton info from struct
	SetSolverDefinition(StructPropertyHandle);
	SetPropertyHandle(StructPropertyHandle);

	// list out available goals
	// set the one currently selected
	if (IKRigController.IsValid())
	{
		TSharedPtr<FString> InitialSelected;
		
		TArray<FName> Goals;
		IKRigController->QueryGoals(Goals);

		Effector= reinterpret_cast<FIKRigEffector*>(Data);
		// give me the name of goal by effecto
		FName CurrentGoalName = IKRigController->GetGoalName(IKRigSolverDefinition.Get(), *Effector);

		// go through profile and see if it has mine
		for (const FName& Goal : Goals)
		{
			EffectorGoalComboList.Add(MakeShareable(new FString(Goal.ToString())));

			if (CurrentGoalName == Goal)
			{
				InitialSelected = EffectorGoalComboList.Last();
			}
		}

		HeaderRow
		.NameContent()
			[
				StructPropertyHandle->CreatePropertyNameWidget()
			]
		.ValueContent()
			[
				SAssignNew(EffectorGoalComboBox, SSearchableComboBox)
				.OptionsSource(&EffectorGoalComboList)
				.OnGenerateWidget(this, &FIKRigEffectorCustomization::MakeEffectorGoalComboWidget)
				.OnSelectionChanged(this, &FIKRigEffectorCustomization::OnEffectorGoalSelectionChanged)
				.OnComboBoxOpening(this, &FIKRigEffectorCustomization::OnEffectorGoalComboOpening)
				.InitiallySelectedItem(InitialSelected)
				.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
				.ContentPadding(0)
				.Content()
				[
					SNew(SEditableTextBox)
					.Text(this, &FIKRigEffectorCustomization::GetEffectorGoalComboBoxContent)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.OnTextCommitted(this, &FIKRigEffectorCustomization::OnEffectorGoalChanged)
					.ToolTipText(this, &FIKRigEffectorCustomization::GetEffectorGoalComboBoxToolTip)
				]
			];

			// I need to see bone name, where is it?
	}
	else
	{
		// if this FIKRigEffector is used by some other Outers, this will fail	
		// should warn programmers instead of silent fail
		ensureAlways(false);
	}
}

void FIKRigEffectorCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (EffectorBoneProperty)
	{
		ChildBuilder.AddProperty(EffectorBoneProperty.ToSharedRef());
	}
}

TSharedRef<SWidget> FIKRigEffectorCustomization::MakeEffectorGoalComboWidget(TSharedPtr<FString> InItem)
{
	return 	SNew(STextBlock).Text(FText::FromString(*InItem)).Font(IDetailLayoutBuilder::GetDetailFont());
}

void FIKRigEffectorCustomization::SetSolverDefinition(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	TArray<UObject*> Objects;
	StructPropertyHandle->GetOuterObjects(Objects);

	IKRigSolverDefinition.Reset();
	IKRigController.Reset();

	UIKRigSolverDefinition* SelectedSolverDefinition = nullptr;
	
	// currently not allowing multi selection
	// or if you select different IKRigSolvers, this will break
	for (UObject* Outer : Objects)
	{
		if (UIKRigSolverDefinition* SolverDefinition = Cast<UIKRigSolverDefinition>(Outer))
		{
			SelectedSolverDefinition = SolverDefinition;
			break;
		}
	}

	IKRigSolverDefinition = SelectedSolverDefinition;

	// once we know solver definion, we know outer is IKRigDefinition
	if (SelectedSolverDefinition)
	{
		UIKRigDefinition* IKRigDefinition = Cast<UIKRigDefinition>(SelectedSolverDefinition->GetOuter());
		if (IKRigDefinition)
		{
			IKRigController = UIKRigController::GetControllerByRigDefinition(IKRigDefinition);

			IKRigController->OnGoalModified.AddSP(this, &FIKRigEffectorCustomization::RefreshEffectorGoals);
		}
	}
}

void FIKRigEffectorCustomization::RefreshEffectorGoals()
{
	TArray<FName> Goals;
	IKRigController->QueryGoals(Goals);

	EffectorGoalComboList.Reset();

	// go through profile and see if it has mine
	for (const FName& Goal : Goals)
	{
		EffectorGoalComboList.Add(MakeShareable(new FString(Goal.ToString())));
	}

	EffectorGoalComboBox->ClearSelection();
	EffectorGoalComboBox->RefreshOptions();
}

TSharedPtr<IPropertyHandle> FIKRigEffectorCustomization::FindStructMemberProperty(TSharedRef<IPropertyHandle> PropertyHandle, const FName& PropertyName)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ++ChildIdx)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(ChildIdx);
		if (ChildHandle->GetProperty()->GetFName() == PropertyName)
		{
			return ChildHandle;
		}
	}

	return TSharedPtr<IPropertyHandle>();
}

void FIKRigEffectorCustomization::SetPropertyHandle(TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	EffectorBoneProperty = FindStructMemberProperty(StructPropertyHandle, GET_MEMBER_NAME_CHECKED(FIKRigEffector, Bone));
	check(EffectorBoneProperty->IsValidHandle());
}

// this is only selection change
void FIKRigEffectorCustomization::SetNewGoalName(FName Name)
{
	// I have to change the mapping of SolverDefinition
	if (UIKRigSolverDefinition* SolverDef = IKRigSolverDefinition.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("ApplyAdditiveSetting_Transaciton", "Apply Additive Setting"));
		SolverDef->Modify();
		IKRigController->SetGoalName(SolverDef, *Effector, Name);
	}
}

FName FIKRigEffectorCustomization::GetSelectedEffectorGoal() const
{
	if (UIKRigSolverDefinition* SolverDef = IKRigSolverDefinition.Get())
	{
		return IKRigController->GetGoalName(SolverDef, *Effector);
	}

	return NAME_None;
}

void FIKRigEffectorCustomization::OnEffectorGoalSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		FString NewValue = *NewSelection.Get();

		SetNewGoalName(FName(*NewValue, FNAME_Find));
	}
}

void FIKRigEffectorCustomization::OnEffectorGoalChanged(const FText& InText, ETextCommit::Type CommitType)
{
	if (UIKRigSolverDefinition* SolverDef = IKRigSolverDefinition.Get())
	{
		FString NewGoalName = InText.ToString();
		NewGoalName.TrimStartAndEndInline();
		if (!NewGoalName.IsEmpty())
		{
			IKRigController->SetGoalName(SolverDef, *Effector, FName(*NewGoalName));
		}
	}
}

void FIKRigEffectorCustomization::OnEffectorGoalComboOpening()
{
	TSharedPtr<FString> ComboStringPtr = GetEffectorGoalString();
	if (ComboStringPtr.IsValid())
	{
		EffectorGoalComboBox->SetSelectedItem(ComboStringPtr);
	}
}

FText FIKRigEffectorCustomization::GetEffectorGoalComboBoxContent() const
{
	return FText::FromName(GetSelectedEffectorGoal());
}

FText FIKRigEffectorCustomization::GetEffectorGoalComboBoxToolTip() const
{
	return LOCTEXT("EffectorGoalComboToolTip", "This will be the Goal name used externally to access this effector.");
}

TSharedPtr<FString> FIKRigEffectorCustomization::GetEffectorGoalString() const
{
	FName EffectorGoalName = GetSelectedEffectorGoal();

	// go through profile and see if it has mine
	for (int32 Index = 0; Index < EffectorGoalComboList.Num(); ++Index)
	{
		if (EffectorGoalName.ToString() == *EffectorGoalComboList[Index])
		{
			return EffectorGoalComboList[Index];
		}
	}

	return TSharedPtr<FString>();
}

#undef LOCTEXT_NAMESPACE
