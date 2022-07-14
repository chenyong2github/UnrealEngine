// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/Conditional/RCBehaviourConditional.h"

#include "Action/RCAction.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "Behaviour/Builtin/Conditional/RCBehaviourConditionalNode.h"
#include "RCVirtualProperty.h"
#include "RemoteControlField.h"
#include "Controller/RCController.h"

URCBehaviourConditional::URCBehaviourConditional()
{
}

void URCBehaviourConditional::Initialize()
{
	Super::Initialize();
}

void URCBehaviourConditional::Execute()
{
	const URCBehaviourNode* BehaviourNode = GetBehaviourNode();
	check(BehaviourNode);

	if (BehaviourNode->GetClass() != URCBehaviourConditionalNode::StaticClass())
	{
		return; // Allow custom Blueprints to drive their own behaviour entirely
	}

	// Execute before the logic
	BehaviourNode->PreExecute(this);

	bool bConditionPass = false;

	/* Consider the following sequence of conditions for a hypothetical Controller "Tricode" with value "Tri2"
	*   =Tri1    <FALSE> (Action 1)       ...skip...
	*   =Tri2   <TRUE>    (Action 2)   ...execute...
	*   =Tri2   <TRUE>    (Action 3)   ...execute...
	*   =Tri3   <FALSE>  (Action 4)       ...skip...
	*   Else                                          ...skip...
	* 
	* For such multiple equality rows we want to know if at least one of them succeeded. 
	* The flag bHasEqualitySuccess is used for determining whether Else should be executed 
	*/
	bool bHasEqualitySuccess = false;

	for (TObjectPtr<URCAction> Action : ActionContainer->Actions)
	{
		FRCBehaviourCondition* Condition = Conditions.Find(Action);
		if (!Condition)
		{
			ensureMsgf(false, TEXT("Unable to find condition for Action"));
			continue;
		}

		const ERCBehaviourConditionType ConditionType = Condition->ConditionType;		

		URCController* RCController = ControllerWeakPtr.Get();
		if (RCController)
		{
			switch (ConditionType)
			{
			case ERCBehaviourConditionType::IsEqual:
				bConditionPass = RCController->IsValueEqual(Condition->Comparand);
				bHasEqualitySuccess |= bConditionPass;
				break;			

			case ERCBehaviourConditionType::IsGreaterThan:
				bConditionPass = RCController->IsValueGreaterThan(Condition->Comparand);
				break;

			case ERCBehaviourConditionType::IsGreaterThanOrEqualTo:
				bConditionPass = RCController->IsValueGreaterThanOrEqualTo(Condition->Comparand);
				break;

			case ERCBehaviourConditionType::IsLesserThan:
				bConditionPass = RCController->IsValueLesserThan(Condition->Comparand);
				break;

			case ERCBehaviourConditionType::IsLesserThanOrEqualTo:
				bConditionPass = RCController->IsValueLesserThanOrEqualTo(Condition->Comparand);
				break;

			case ERCBehaviourConditionType::Else:
				// If the previous condition failed and no prior equality condition succeeded (among multiple =rows above) then execute Else!
				bConditionPass = !bConditionPass && !bHasEqualitySuccess;
				break;

			default:				
				ensureAlwaysMsgf(false, TEXT("Unimplemented comparator!"));
			}

			if (ConditionType != ERCBehaviourConditionType::IsEqual)
			{
				bHasEqualitySuccess = false; // Reset flag; either we reached an else clause (resolved above) or we moved from equality rows to a different comparison type
			}
		}

		if (bConditionPass)
		{
			Action->Execute();
			BehaviourNode->OnPassed(this);
		}
	}
}

void URCBehaviourConditional::OnActionAdded(URCAction* Action, const ERCBehaviourConditionType InConditionType, const TObjectPtr<URCVirtualPropertySelfContainer> InComparand)
{
	FRCBehaviourCondition Condition(InConditionType, InComparand);

	Conditions.Add(Action, MoveTemp(Condition));	
}

URCAction* URCBehaviourConditional::AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField, const ERCBehaviourConditionType InConditionType, const TObjectPtr<URCVirtualPropertySelfContainer> InComparand)
{
	TRCActionUniquenessTest UniquenessTest = [this, InRemoteControlField, InConditionType, InComparand](const TSet<TObjectPtr<URCAction>>& Actions)
	{
		const FGuid FieldId = InRemoteControlField->GetId();

		for (const URCAction* Action : Actions)
		{
			if (const FRCBehaviourCondition* Condition = this->Conditions.Find(Action))
			{
				const ERCBehaviourConditionType ActionCondition = Condition->ConditionType;

				TObjectPtr<URCVirtualPropertySelfContainer> ActionComparand = Condition->Comparand;

				if (Action->ExposedFieldId == FieldId && ActionCondition == InConditionType && ActionComparand->IsValueEqual(InComparand))
				{
					return false; // Not Unique!
				}
			}
		}

		return true;
	};

	return ActionContainer->AddAction(UniquenessTest, InRemoteControlField);
}

FText URCBehaviourConditional::GetConditionTypeAsText(ERCBehaviourConditionType ConditionType) const
{
	FText ConditionDisplayText;

	switch (ConditionType)
	{
		case ERCBehaviourConditionType::IsEqual:
		{
			ConditionDisplayText = FText::FromName("=");
			break;
		}
		case ERCBehaviourConditionType::IsGreaterThan:
		{
			ConditionDisplayText = FText::FromName(">");
			break;
		}
		case ERCBehaviourConditionType::IsLesserThan:
		{
			ConditionDisplayText = FText::FromName("<");
			break;
		}
		case ERCBehaviourConditionType::IsGreaterThanOrEqualTo:
		{
			ConditionDisplayText = FText::FromName(">=");
			break;
		}
		case ERCBehaviourConditionType::IsLesserThanOrEqualTo:
		{
			ConditionDisplayText = FText::FromName("<=");
			break;
		}
		case ERCBehaviourConditionType::Else:
		{
			ConditionDisplayText = FText::FromName("Else");
			break;
		}
		default:
		{
			ensureMsgf(false, TEXT("Unknown condition type"));
		}
	}

	return ConditionDisplayText;
}