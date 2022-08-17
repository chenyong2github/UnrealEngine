// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/RangeMap/RCRangeMapBehaviour.h"

#include "Action/RCAction.h"
#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "Behaviour/Builtin/RangeMap/RCBehaviourRangeMapNode.h"
#include "Behaviour/RCBehaviourNode.h"
#include "Containers/Set.h"
#include "Controller/RCController.h"
#include "IRemoteControlPropertyHandle.h"
#include "Kismet/KismetMathLibrary.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"

namespace UE::RCRangeMapBehaviour
{
	const FName MinValue = TEXT("MinValue");
	const FName MaxValue = TEXT("MaxValue");
	const FName Threshold = TEXT("Threshold");
	const FName Step = TEXT("Step");
}

URCRangeMapBehaviour::URCRangeMapBehaviour()
{
	PropertyContainer = CreateDefaultSubobject<URCVirtualPropertyContainerBase>(FName("VirtualPropertyContainer"));
}

void URCRangeMapBehaviour::Initialize()
{
	URCController* RCController = ControllerWeakPtr.Get();
	if (!RCController)
	{
		return;
	}

	TWeakObjectPtr<URCVirtualPropertyContainerBase> ContainerPtr = PropertyContainer;

	ContainerPtr->AddProperty(UE::RCRangeMapBehaviour::MinValue, URCController::StaticClass(), EPropertyBagPropertyType::Double);
	ContainerPtr->AddProperty(UE::RCRangeMapBehaviour::MaxValue, URCController::StaticClass(), EPropertyBagPropertyType::Double);
	ContainerPtr->AddProperty(UE::RCRangeMapBehaviour::Threshold, URCController::StaticClass(), EPropertyBagPropertyType::Double);
	ContainerPtr->AddProperty(UE::RCRangeMapBehaviour::Step, URCController::StaticClass(), EPropertyBagPropertyType::Double);

	Super::Initialize();
}

bool URCRangeMapBehaviour::IsSupportedActionLerpType(TObjectPtr<URCAction> InAction) const
{
	if (TObjectPtr<URCFunctionAction> InFunctionAction = Cast<URCFunctionAction>(InAction))
	{
		// Early false if function. Functions are always non-lerp.
		return false;
	}
	
	TObjectPtr<URCPropertyAction> InPropertyAction = Cast<URCPropertyAction>(InAction);
	if (!InPropertyAction)
	{
		return false;
	}
	
	const bool bIsNumeric = InPropertyAction->PropertySelfContainer->IsNumericType();
	const bool bIsVector  = InPropertyAction->PropertySelfContainer->IsVectorType();
	const bool bIsRotator = InPropertyAction->PropertySelfContainer->IsRotatorType();

	return bIsNumeric || bIsVector || bIsRotator;
}

void URCRangeMapBehaviour::Refresh()
{
	URCController* Controller = ControllerWeakPtr.Get();
	if (!Controller)
	{
		return;
	}

	// Step 0: Get All required Values, getting the current values set within the DetailPanel setting them up for our header.
	PropertyContainer->GetVirtualProperty(UE::RCRangeMapBehaviour::MinValue)->GetValueDouble(MinValue);
	PropertyContainer->GetVirtualProperty(UE::RCRangeMapBehaviour::MaxValue)->GetValueDouble(MaxValue);
	PropertyContainer->GetVirtualProperty(UE::RCRangeMapBehaviour::Threshold)->GetValueDouble(Threshold);

	Controller->GetValueFloat(ControllerFloatValue);
	
	// Step 1: Clamp the Value between Max/Min if necessary
	if (ControllerFloatValue < MinValue || MaxValue < ControllerFloatValue)
	{
		ControllerFloatValue = FMath::Clamp(ControllerFloatValue, MinValue, MaxValue);
		Controller->SetValueFloat(ControllerFloatValue);
	}
}

bool URCRangeMapBehaviour::GetNearestActionByThreshold(TTuple<URCAction*, bool>& OutTuple)
{
	// Variables
	TMap<double, URCAction*> NonLerpActions = GetNonLerpActions();
	
	TArray<double> StepActionArray;
	NonLerpActions.GenerateKeyArray(StepActionArray);

	// Step 1: Convert to double for Kismet Operation and Normalize.
	double NormalizedControllerValue = UKismetMathLibrary::NormalizeToRange(ControllerFloatValue, MinValue, MaxValue);

	// Step 2: Go through each of the Action Steps and look for the shortest distance
	for (int StepIndex = 0; StepIndex < StepActionArray.Num(); StepIndex++)
	{
		double StepValue = StepActionArray[StepIndex];
		double ValueDifference = FMath::Abs(StepValue - NormalizedControllerValue);
		
		if (StepActionArray.Num() > 1 && StepIndex > 0)
		{
			double PrevValueDifference = FMath::Abs(StepActionArray[StepIndex-1] - NormalizedControllerValue);
			
			// Break out early in case previous Step had a lesser difference
			if (PrevValueDifference <= ValueDifference)
			{
				break;
			}
		}
		OutTuple = TTuple<URCAction*, bool>(NonLerpActions[StepValue], ValueDifference <= Threshold);
	}
	return true;
}

void URCRangeMapBehaviour::GetLerpActions(TMap<FGuid, TArray<URCAction*>>& OutNumericActionsByField)
{
	for (URCAction* Action : ActionContainer->Actions)
	{
		TArray<URCAction*>& LerpActionArray = OutNumericActionsByField.FindOrAdd(Action->ExposedFieldId);
		
		// Step 01: Find Action
		if (IsSupportedActionLerpType(Action))
		{
			// Step 02: Add Action if it's numerical
			LerpActionArray.Add(Action);
		}
	}

	// Step 03: Sort actions using their StepValue
	for (TTuple<FGuid, TArray<URCAction*>>& NumericActionTuple : OutNumericActionsByField)
	{
		TArray<URCAction*>& ArrayToSort = NumericActionTuple.Value;

		Algo::Sort(ArrayToSort, [this](const URCAction* ActionA, const URCAction* ActionB)
		{
			double A = RangeMapActionContainer.Find(ActionA)->StepValue;
			double B = RangeMapActionContainer.Find(ActionB)->StepValue;

			return A > B;
		});
	}
}

void URCRangeMapBehaviour::Execute()
{
	Refresh();
	const URCBehaviourNode* BehaviourNode = GetBehaviourNode();
	check(BehaviourNode);

	if (BehaviourNode->GetClass() != URCBehaviourRangeMapNode::StaticClass())
	{
		return; // Allow custom Blueprints to drive their own behaviour entirely
	}

	// Do this beforehand.
	BehaviourNode->PreExecute(this);
	
	URCController* RCController = ControllerWeakPtr.Get();
	if (!RCController)
	{
		ensureMsgf(false, TEXT("Remote Control Controller is not available/nullptr."));
		return;
	}

	RCController->GetValueFloat(ControllerFloatValue);
	ControllerFloatValue = FMath::Clamp(ControllerFloatValue, MinValue, MaxValue);

	TTuple<URCAction*, bool> NearestAction;
	if (GetNearestActionByThreshold(NearestAction) && NearestAction.Value)
	{
		// Execute nearest Action in case its under/passes the Threshold in distance.
		NearestAction.Key->Execute();
	}

	// Apply Lerp if possible
	TMap<FGuid, TTuple<URCAction*, URCAction*>> LerpActions;
	if (!GetRangeValuePairsForLerp(LerpActions))
	{
		return;
	}

	for (TTuple<FGuid, TTuple<URCAction*, URCAction*>>& LerpAction : LerpActions)
	{
		URemoteControlPreset* Preset = RCController->PresetWeakPtr.Get();
		if (!Preset)
		{
			ensureMsgf(false, TEXT("Preset is invalid or unavailable."));
			return;
		}
		FGuid CurrentFieldId = LerpAction.Key;

		// Get the StepValues given for the FGuid
		URCAction* MinActionOfPair = LerpAction.Value.Key;
		URCAction* MaxActionOfPair = LerpAction.Value.Value;

		if (!MinActionOfPair || !MaxActionOfPair)
		{
			return;
		}

		FRCRangeMapStep* MinRangeMapStep = RangeMapActionContainer.Find(MinActionOfPair);
		FRCRangeMapStep* MaxRangeMapStep = RangeMapActionContainer.Find(MaxActionOfPair);

		if (!(MinRangeMapStep && MaxRangeMapStep))
		{
			ensureMsgf(false, TEXT("%s is an invalid pointer."), MinRangeMapStep ? TEXT("MaxRangeMapStep") : TEXT("MinRangeMapStep"));
			return;
		}

		// Denormalize and Map them based on our Min and Max Value
		double MappedMinStep = UKismetMathLibrary::Lerp(MinValue, MaxValue, MinRangeMapStep->StepValue);
		double MappedMaxStep = UKismetMathLibrary::Lerp(MinValue, MaxValue, MaxRangeMapStep->StepValue);

		// Normalize our Controller based on the new MappedMinStep and MappedMaxStep Range.
		double CustomNormalizedStepValue = UKismetMathLibrary::NormalizeToRange(ControllerFloatValue, MappedMinStep, MappedMaxStep);

		// Apply value based on type
		switch(MinRangeMapStep->PropertyValue->GetValueType())
		{
		case EPropertyBagPropertyType::Double:
			{
				double MinRangeDouble;
				double MaxRangeDouble;

				MinRangeMapStep->PropertyValue->GetValueDouble(MinRangeDouble);
				MaxRangeMapStep->PropertyValue->GetValueDouble(MaxRangeDouble);

				double ResultDouble = UKismetMathLibrary::Lerp(MinRangeDouble, MaxRangeDouble, CustomNormalizedStepValue);
				if (TSharedPtr<FRemoteControlProperty> RCProperty = Preset->GetExposedEntity<FRemoteControlProperty>(CurrentFieldId).Pin())
				{
					RCProperty->GetPropertyHandle()->SetValue(ResultDouble);
				}
				
				break;
			}
		case EPropertyBagPropertyType::Float:
			{
				float MinRangeFloat;
				float MaxRangeFloat;

				MinRangeMapStep->PropertyValue->GetValueFloat(MinRangeFloat);
				MaxRangeMapStep->PropertyValue->GetValueFloat(MaxRangeFloat);

				float ResultFloat = UKismetMathLibrary::Lerp(MinRangeFloat, MaxRangeFloat, CustomNormalizedStepValue);
				if (TSharedPtr<FRemoteControlProperty> RCProperty = Preset->GetExposedEntity<FRemoteControlProperty>(CurrentFieldId).Pin())
				{
					RCProperty->GetPropertyHandle()->SetValue(ResultFloat);
				}
				
				break;
			}
		case EPropertyBagPropertyType::Struct:
			{
				ApplyLerpOnStruct(MinRangeMapStep, MaxRangeMapStep, CustomNormalizedStepValue, CurrentFieldId);
				
				break;
			}
		default:
			// Shouldn't happen.
			ensureMsgf(false, TEXT("Unsupported Lerp Type."));
		}
	}
}

URCAction* URCRangeMapBehaviour::AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	double InStepValue = 0.0;
	PropertyContainer->GetVirtualProperty(UE::RCRangeMapBehaviour::Step);
	
	const TRCActionUniquenessTest ActionUniquenessTest = [this, InRemoteControlField, InStepValue](const TSet<TObjectPtr<URCAction>>& Actions)
	{
		return IsActionUnique(InRemoteControlField, InStepValue, Actions);
	};
	
	return ActionContainer->AddAction(ActionUniquenessTest, InRemoteControlField);
}

void URCRangeMapBehaviour::OnActionAdded(URCAction* Action, const TObjectPtr<URCVirtualPropertySelfContainer> InPropertyValue)
{
	double InStepValue;
	URCVirtualPropertyBase* StepVirtualProperty = PropertyContainer->GetVirtualProperty(UE::RCRangeMapBehaviour::Step);
	StepVirtualProperty->GetValueDouble(InStepValue);
	StepVirtualProperty->SetValueDouble(0.0);
	
	FRCRangeMapStep RangeMapStep = FRCRangeMapStep(InStepValue, InPropertyValue);

	RangeMapActionContainer.Add(Action, MoveTemp(RangeMapStep));
}

bool URCRangeMapBehaviour::GetRangeValuePairsForLerp(TMap<FGuid, TTuple<URCAction*, URCAction*>>& OutPairs)
{
	double NormalizedControllerValue = UKismetMathLibrary::NormalizeToRange(ControllerFloatValue, MinValue, MaxValue);

	TMap<FGuid, TArray<URCAction*>> LerpActionMap;
	GetLerpActions(LerpActionMap);
	
	for (TTuple<FGuid, TArray<URCAction*>>& NumericActionTuple : LerpActionMap)
	{
		if (NumericActionTuple.Value.Num() < 2)
		{
			// Skip and continue early if the Array has less Numeric Values needed to actually Lerp
			continue;
		}

		// Intermediary calculations of Lerp for in-between Lerp.
		URCAction* MinAction;
		FRCRangeMapStep* MinRangeMap = nullptr;
		URCAction* MaxAction;
		FRCRangeMapStep* MaxRangeMap = nullptr;
		
		for (URCAction* Action : NumericActionTuple.Value)
		{
			FRCRangeMapStep* RangeMap = RangeMapActionContainer.Find(Action);
			if (!RangeMap)
			{
				// Shouldn't happen, but skip if it does
				continue;
			}

			// Deal with the nullptr
			if (NormalizedControllerValue >= RangeMap->StepValue && (!MinRangeMap || NumericActionTuple.Value.Last() != Action))
			{
				MinAction = Action;
				MinRangeMap = RangeMap;
				
				continue;
			}

			if (NormalizedControllerValue <= RangeMap->StepValue)
			{
				MaxAction = Action;
				MaxRangeMap = RangeMap;

				continue;
			}

			// Ensure there are no nullptr
			if (!MinRangeMap || !MaxRangeMap)
			{
				return false;
			}
		}

		OutPairs.Add(NumericActionTuple.Key, TTuple<URCAction*, URCAction*>(MinAction, MaxAction));
	}
	
	return true;
}

TMap<double, URCAction*> URCRangeMapBehaviour::GetNonLerpActions()
{
	TMap<double, URCAction*> ActionMap;
	
	for (URCAction* Action : ActionContainer->Actions)
	{
		
		if (!IsSupportedActionLerpType(Action))
		{
			FRCRangeMapStep* RangeMapStep = RangeMapActionContainer.Find(Action);

			if (RangeMapStep)
			{
				ActionMap.Add(RangeMapStep->StepValue, Action);
			}
		}
	}

	return ActionMap;
}

bool URCRangeMapBehaviour::CanHaveActionForField(const TSharedPtr<FRemoteControlField> InRemoteControlField) const
{
	// We can select all of the actions, and the check whether its actually added is done elsewhere.
	return true;
}

void URCRangeMapBehaviour::ApplyLerpOnStruct(const FRCRangeMapStep* MinRangeStep, const FRCRangeMapStep* MaxRangeStep, const double& StepAlpha, const FGuid& FieldId)
{
	URCController* RCController = ControllerWeakPtr.Get();
	if (!RCController)
	{
		ensureMsgf(false, TEXT("Remote Controller is invalid or unavailable."));
		return;
	}
	
	URemoteControlPreset* Preset = RCController->PresetWeakPtr.Get();
	if (!Preset)
	{
		ensureMsgf(false, TEXT("Preset is invalid or unavailable."));
		return;
	}
	
	// Check whether its a Rotator or FVector based on the MinRangeStep
	if (MinRangeStep->PropertyValue->IsVectorType())
	{
		FVector MinRangeVector;
		FVector MaxRangeVector;

		MinRangeStep->PropertyValue->GetValueVector(MinRangeVector);
		MaxRangeStep->PropertyValue->GetValueVector(MaxRangeVector);

		FVector ResultVector = UKismetMathLibrary::VLerp(MinRangeVector, MaxRangeVector, StepAlpha);
		if (TSharedPtr<FRemoteControlProperty> RCProperty = Preset->GetExposedEntity<FRemoteControlProperty>(FieldId).Pin())
		{
			RCProperty->GetPropertyHandle()->SetValue(ResultVector);
		}
	}

	if (MinRangeStep->PropertyValue->IsRotatorType())
	{
		FRotator MinRangeRotator;
		FRotator MaxRangeRotator;

		MinRangeStep->PropertyValue->GetValueRotator(MinRangeRotator);
		MaxRangeStep->PropertyValue->GetValueRotator(MaxRangeRotator);

		FRotator ResultVector = UKismetMathLibrary::RLerp(MinRangeRotator, MaxRangeRotator, StepAlpha, true);
		if (TSharedPtr<FRemoteControlProperty> RCProperty = Preset->GetExposedEntity<FRemoteControlProperty>(FieldId).Pin())
		{
			 RCProperty->GetPropertyHandle()->SetValue(ResultVector);
		}
	}
}

bool URCRangeMapBehaviour::IsActionUnique(const TSharedRef<const FRemoteControlField> InRemoteControlField, const double& InStepValue, const TSet<TObjectPtr<URCAction>>& InActions)
{
	const FGuid FieldId = InRemoteControlField->GetId();

	for (const URCAction* Action : InActions)
	{
		// Check whether or not we have a RangeStep.
		if (const FRCRangeMapStep* RangeMapStep = this->RangeMapActionContainer.Find(Action))
		{
			// Check if the Step itself is already used
			const double ActionStepValue = RangeMapStep->StepValue;

			// Only Important bit is StepValue and FieldId
			if (Action->ExposedFieldId == FieldId && FMath::IsNearlyEqual(ActionStepValue, InStepValue))
			{
				return false; // Not Unique.
			}
		}
	}
	
	return true;
}
