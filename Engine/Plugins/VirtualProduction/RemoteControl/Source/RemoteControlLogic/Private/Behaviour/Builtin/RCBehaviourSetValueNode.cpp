// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/RCBehaviourSetValueNode.h"

#include "PropertyBag.h"

URCBehaviourSetValueNode::URCBehaviourSetValueNode()
{
	DisplayName = NSLOCTEXT("Remote Control Behaviour", "Behavior Name - Set Value", "Set Value");
	BehaviorDescription = NSLOCTEXT("Remote Control Behaviour", "Behavior Desc - Set Value", "Triggers an event when the associated property is modified");
}


bool URCBehaviourSetValueNode::Execute_Implementation(URCBehaviour* InBehaviour) const
{
	return true;
}

bool URCBehaviourSetValueNode::IsSupported_Implementation(URCBehaviour* InBehaviour) const
{
	static TArray<TPair<EPropertyBagPropertyType, UObject*>> SupportedPropertyBagTypes =
	{
		{ EPropertyBagPropertyType::Bool, nullptr },
		{ EPropertyBagPropertyType::Byte, nullptr },
		{ EPropertyBagPropertyType::Int32, nullptr },
		{ EPropertyBagPropertyType::Float, nullptr },
		{ EPropertyBagPropertyType::Double, nullptr },
		{ EPropertyBagPropertyType::Name, nullptr },
		{ EPropertyBagPropertyType::String, nullptr },
		{ EPropertyBagPropertyType::Text, nullptr },
		{ EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get() },
		{ EPropertyBagPropertyType::Struct, TBaseStructure<FColor>::Get() },
		{ EPropertyBagPropertyType::Struct, TBaseStructure<FRotator>::Get() }
	};
	
	return SupportedPropertyBagTypes.ContainsByPredicate(GetIsSupportedCallback(InBehaviour));
}
