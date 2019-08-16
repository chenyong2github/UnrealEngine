// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_ControlRig.h"
#include "Kismet2/CompilerResultsLog.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "DetailWidgetRow.h"
#include "ControlRigVariables.h"
#include "SVariableMappingWidget.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_ControlRig"

UAnimGraphNode_ControlRig::UAnimGraphNode_ControlRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_ControlRig::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// display control rig here
	return LOCTEXT("AnimGraphNode_ControlRig_Title", "Control Rig");
}

FText UAnimGraphNode_ControlRig::GetTooltipText() const
{
	// display control rig here
	return LOCTEXT("AnimGraphNode_ControlRig_Tooltip", "Evaluates a control rig");
}

void UAnimGraphNode_ControlRig::GetExposableProperties(TArray<UProperty*>& OutExposableProperties) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// we only need inputs
	TMap<FName, FControlRigIOVariable> LocalInputVariables;
	GetIOProperties(true, LocalInputVariables);

	UClass* TargetClass = GetTargetClass();
	// if we have target class, see if we can get 
	if (TargetClass)
	{
		UControlRig* ControlRig = TargetClass->GetDefaultObject<UControlRig>();
		if (ControlRig)
		{
			for (auto Iter = LocalInputVariables.CreateIterator(); Iter; ++Iter)
			{
				FControlRigIOVariable& Var = Iter.Value();
				FCachedPropertyPath CachePath;
				ControlRig->GetInOutPropertyPath(true, Iter.Key(), CachePath);
				if (CachePath.IsResolved())
				{
					OutExposableProperties.Add(CachePath.GetUProperty());
				}
			}
		}
	}
}

void UAnimGraphNode_ControlRig::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// we do this to refresh input variables
	RebuildExposedProperties();
	// we avoid CustomProperty, it only allows "the direct child"
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UAnimGraphNode_ControlRig::RebuildExposedProperties()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	KnownExposableProperties.Empty();

	// go through exposed properties, and clean up
	GetIOProperties(true, InputVariables);
	// we still update OUtputvariables
	// we don't want output to be exposed
	GetIOProperties(false, OutputVariables);

	// clear IO variables that don't exist anymore
	auto ClearInvalidMapping = [](const TMap<FName, FControlRigIOVariable>& InVariables, TMap<FName, FName>& InOutMapping)
	{
		TArray<FName> KeyArray;
		InOutMapping.GenerateKeyArray(KeyArray);

		for (int32 Index=0; Index<KeyArray.Num(); ++Index)
		{
			// if this input doesn't exist anymore
			if (!InVariables.Contains(KeyArray[Index]))
			{
				InOutMapping.Remove(KeyArray[Index]);
			}
		}
	};

	ClearInvalidMapping(InputVariables, Node.InputMapping);
	ClearInvalidMapping(OutputVariables, Node.OutputMapping);

	for (auto Iter = InputVariables.CreateConstIterator(); Iter; ++Iter)
	{
		KnownExposableProperties.Add(Iter.Key());
	}

	for (int32 Index = 0; Index < ExposedPropertyNames.Num(); ++Index)
	{
		// remove if it's not there anymore
		if (!KnownExposableProperties.Contains(ExposedPropertyNames[Index]))
		{
			ExposedPropertyNames.RemoveAt(Index);
			--Index;
		}
	}
}

bool UAnimGraphNode_ControlRig::IsInputProperty(const FName& PropertyName) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	return InputVariables.Contains(PropertyName);
}

bool UAnimGraphNode_ControlRig::IsAvailableToMapToCurve(const FName& PropertyName, bool bInput) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// find if input or output
	// ensure it could convert to float
	const FControlRigIOVariable* Variable = (bInput) ? InputVariables.Find(PropertyName) : OutputVariables.Find(PropertyName);
	if (ensure(Variable))
	{
		return FControlRigIOHelper::CanConvert(FName(*Variable->PropertyType), FControlRigIOTypes::GetTypeString<float>());
	}

	return false;
}

bool UAnimGraphNode_ControlRig::IsPropertyExposeEnabled(FName PropertyName) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// if known exposable, and and if it hasn't been exposed yet
	if (KnownExposableProperties.Contains(PropertyName))
	{
		return IsInputProperty(PropertyName);
	}

	return false;
}

ECheckBoxState UAnimGraphNode_ControlRig::IsPropertyExposed(FName PropertyName) const
{
	return Super::IsPropertyExposed(PropertyName);
}

void UAnimGraphNode_ControlRig::OnPropertyExposeCheckboxChanged(ECheckBoxState NewState, FName PropertyName)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::OnPropertyExposeCheckboxChanged(NewState, PropertyName);

	// see if any of my child has the mapping, and clear them
	if (NewState == ECheckBoxState::Checked)
	{
		FScopedTransaction Transaction(LOCTEXT("PropertyExposedChanged", "Expose Property to Pin"));
		Modify();

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());

		bool bInput = IsInputProperty(PropertyName);
		// if checked, we clear mapping
		// and unclear all children
		Node.SetIOMapping(bInput, PropertyName, NAME_None);
	}
}

void UAnimGraphNode_ControlRig::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::CustomizeDetails(DetailBuilder);

	// We dont allow multi-select here
	if (DetailBuilder.GetSelectedObjects().Num() > 1)
	{
		return;
	}

	RebuildExposedProperties();

	IDetailCategoryBuilder& InputCategoryBuilder = DetailBuilder.EditCategory(FName(TEXT("Input")));

	FDetailWidgetRow& InputWidgetRow = InputCategoryBuilder.AddCustomRow(FText::FromString(TEXT("Input")));
	InputWidgetRow.WholeRowContent()
	[
		SNew(SVariableMappingWidget)
		.OnVariableMappingChanged(FOnVariableMappingChanged::CreateUObject(this, &UAnimGraphNode_ControlRig::OnVariableMappingChanged, true))
		.OnGetVariableMapping(FOnGetVariableMapping::CreateUObject(this, &UAnimGraphNode_ControlRig::GetVariableMapping, true))
		.OnGetAvailableMapping(FOnGetAvailableMapping::CreateUObject(this, &UAnimGraphNode_ControlRig::GetAvailableMapping, true))
		.OnCreateVariableMapping(FOnCreateVariableMapping::CreateUObject(this, &UAnimGraphNode_ControlRig::CreateVariableMapping, true))
		.OnVariableOptionAvailable(FOnVarOptionAvailable::CreateUObject(this, &UAnimGraphNode_ControlRig::IsAvailableToMapToCurve, true))
		.OnPinGetCheckState(FOnPinGetCheckState::CreateUObject(this, &UAnimGraphNode_ControlRig::IsPropertyExposed))
		.OnPinCheckStateChanged(FOnPinCheckStateChanged::CreateUObject(this, &UAnimGraphNode_ControlRig::OnPropertyExposeCheckboxChanged))
		.OnPinIsEnabledCheckState(FOnPinIsCheckEnabled::CreateUObject(this, &UAnimGraphNode_ControlRig::IsPropertyExposeEnabled))
	];

	IDetailCategoryBuilder& OutputCategoryBuilder = DetailBuilder.EditCategory(FName(TEXT("Output")));

	FDetailWidgetRow& OutputWidgetRow = OutputCategoryBuilder.AddCustomRow(FText::FromString(TEXT("Output")));
	OutputWidgetRow.WholeRowContent()
	[
		SNew(SVariableMappingWidget)
		.OnVariableMappingChanged(FOnVariableMappingChanged::CreateUObject(this, &UAnimGraphNode_ControlRig::OnVariableMappingChanged, false))
		.OnGetVariableMapping(FOnGetVariableMapping::CreateUObject(this, &UAnimGraphNode_ControlRig::GetVariableMapping, false))
		.OnGetAvailableMapping(FOnGetAvailableMapping::CreateUObject(this, &UAnimGraphNode_ControlRig::GetAvailableMapping, false))
		.OnCreateVariableMapping(FOnCreateVariableMapping::CreateUObject(this, &UAnimGraphNode_ControlRig::CreateVariableMapping, false))
		.OnVariableOptionAvailable(FOnVarOptionAvailable::CreateUObject(this, &UAnimGraphNode_ControlRig::IsAvailableToMapToCurve, false))
		.OnPinGetCheckState(FOnPinGetCheckState::CreateUObject(this, &UAnimGraphNode_ControlRig::IsPropertyExposed))
		.OnPinCheckStateChanged(FOnPinCheckStateChanged::CreateUObject(this, &UAnimGraphNode_ControlRig::OnPropertyExposeCheckboxChanged))
		.OnPinIsEnabledCheckState(FOnPinIsCheckEnabled::CreateUObject(this, &UAnimGraphNode_ControlRig::IsPropertyExposeEnabled))
	];

	TSharedRef<IPropertyHandle> ClassHandle = DetailBuilder.GetProperty(TEXT("Node.ControlRigClass"), GetClass());
	if (ClassHandle->IsValidHandle())
	{
		ClassHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateUObject(this, &UAnimGraphNode_ControlRig::OnInstanceClassChanged, &DetailBuilder));
	}
}

void UAnimGraphNode_ControlRig::GetIOProperties(bool bInput, TMap<FName, FControlRigIOVariable>& OutVars) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	OutVars.Reset();

	UClass* TargetClass = GetTargetClass();
	// if we have target class, see if we can get 
	if (TargetClass)
	{
		UControlRig* ControlRig = TargetClass->GetDefaultObject<UControlRig>();
		if (ControlRig)
		{
			TArray<FControlRigIOVariable> RigIOVars;
			// we use CDO to do this because we don't have instance
			ControlRig->QueryIOVariables(bInput, RigIOVars);

			// now we have IO variables, but need to fill up property array
			for (int32 Index = 0; Index < RigIOVars.Num(); ++Index)
			{
				// this sub property name doesn't exist - ARGGG
				FName PropertyName = FName(*RigIOVars[Index].PropertyPath);

				OutVars.Add(PropertyName, RigIOVars[Index]);
			}
		}
	}
}

void UAnimGraphNode_ControlRig::OnVariableMappingChanged(const FName& PathName, const FName& Curve, bool bInput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FScopedTransaction Transaction(LOCTEXT("VariableMappingChanged", "Change Variable Mapping"));
	Modify();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());

	// @todo: this is not enough when we start breaking down struct
	Node.SetIOMapping(bInput, PathName, Curve);
}

FName UAnimGraphNode_ControlRig::GetVariableMapping(const FName& PathName, bool bInput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// @todo: this is not enough when we start breaking down struct
	return Node.GetIOMapping(bInput, PathName);
}

void UAnimGraphNode_ControlRig::GetAvailableMapping(const FName& PathName, TArray<FName>& OutArray, bool bInput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	UAnimBlueprint* AnimBP = CastChecked<UAnimBlueprint>(GetBlueprint());
	USkeleton* TargetSkeleton = AnimBP->TargetSkeleton;
	OutArray.Reset();
	if (TargetSkeleton)
	{
		const FSmartNameMapping* CurveMapping = TargetSkeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
		CurveMapping->FillNameArray(OutArray);

		TArray<FName> InputCurves, OutputCurves;
		// we want to exclude anything that has been mapped already
		Node.InputMapping.GenerateValueArray(InputCurves);
		Node.InputMapping.GenerateValueArray(OutputCurves);

		// I have to remove Input/Output Curves from OutArray
		for (int32 Index = 0; Index < OutArray.Num(); ++Index)
		{
			const FName& Item = OutArray[Index];

			if (InputCurves.Contains(Item))
			{
				OutArray.RemoveAt(Index);
				--Index;
			}
			else if (OutputCurves.Contains(Item))
			{
				OutArray.RemoveAt(Index);
				--Index;
			}
		}
	}
}

void UAnimGraphNode_ControlRig::CreateVariableMapping(const FString& FilteredText, TArray< TSharedPtr<FVariableMappingInfo> >& OutArray, bool bInput)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	// should have latest
	OutArray.Reset();

	bool bDoFiltering = !FilteredText.IsEmpty();
	const TMap<FName, FControlRigIOVariable>& Variables = (bInput)? InputVariables : OutputVariables;
	for (auto Iter = Variables.CreateConstIterator(); Iter; ++Iter)
	{
		const FName& Name = Iter.Key();
		const FString& DisplayName = Name.ToString();

		const FString MappedName = GetVariableMapping(Name, bInput).ToString();
		// make sure it doesn't fit any of them
		if (!bDoFiltering || 
			(DisplayName.Contains(FilteredText) || MappedName.Contains(FilteredText)))
		{
			TSharedRef<FVariableMappingInfo> Item = FVariableMappingInfo::Make(Iter.Key());

			FVariableMappingInfo& ItemRaw = Item.Get();
			FString PathString = ItemRaw.GetPathName().ToString();
			FString DisplayString = ItemRaw.GetDisplayName();

			OutArray.Add(Item);
		}
	}
}

void UAnimGraphNode_ControlRig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bRequiresNodeReconstruct = false;
	UProperty* ChangedProperty = PropertyChangedEvent.Property;

	if (ChangedProperty)
	{
		if (ChangedProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_ControlRig, ControlRigClass))
		{
			bRequiresNodeReconstruct = true;
			RebuildExposedProperties();
		}
	}

	if (bRequiresNodeReconstruct)
	{
		ReconstructNode();
	}
}

#undef LOCTEXT_NAMESPACE
