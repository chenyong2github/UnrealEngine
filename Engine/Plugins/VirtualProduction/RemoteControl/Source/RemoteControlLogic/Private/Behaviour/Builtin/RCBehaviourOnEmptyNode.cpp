// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/RCBehaviourOnEmptyNode.h"

#include "PropertyBag.h"
#include "Behaviour/RCBehaviour.h"
#include "Controller/RCController.h"

URCBehaviourOnEmptyNode::URCBehaviourOnEmptyNode()
{
	DisplayName = NSLOCTEXT("Remote Control Behaviour", "Behavior Name - On Empty", "On Empty");
	BehaviorDescription = NSLOCTEXT("Remote Control Behaviour", "Behavior Desc - On Empty", "Triggers an event when the input changes from no value. Matching LinkID's will copy visiiblity value.");
}

bool URCBehaviourOnEmptyNode::Execute_Implementation(URCBehaviour* InBehaviour) const
{
	if (!ensure(InBehaviour))
	{
		return false;
	}

	URCController* RCController = InBehaviour->ControllerWeakPtr.Get();
	if (!ensure(RCController))
	{
		return false;
	}
	
	if (FString StringValue; RCController->GetValueString(StringValue))
	{
		return StringValue.IsEmpty();
	}

	if (FName NameValue; RCController->GetValueName(NameValue))
	{
		return NameValue.IsNone();
	}

	if (FText TextValue; RCController->GetValueText(TextValue))
	{
		return TextValue.IsEmpty();
	}
	
	return false;
}

bool URCBehaviourOnEmptyNode::IsSupported_Implementation(URCBehaviour* InBehaviour) const
{
	static TArray<TPair<EPropertyBagPropertyType, UObject*>> SupportedPropertyBagTypes =
	{
		{ EPropertyBagPropertyType::Name, nullptr },
		{ EPropertyBagPropertyType::String, nullptr },
		{ EPropertyBagPropertyType::Text, nullptr },
	};

	return SupportedPropertyBagTypes.ContainsByPredicate(GetIsSupportedCallback(InBehaviour));
}
