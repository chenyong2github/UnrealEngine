// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/RCBehaviourIsEqualNode.h"

#include "RCVirtualProperty.h"
#include "Behaviour/RCIsEqualBehaviour.h"
#include "Controller/RCController.h"

URCBehaviourIsEqualNode::URCBehaviourIsEqualNode()
{
	DisplayName = NSLOCTEXT("Remote Control Behaviour", "Behavior Name - Is Equal", "Is Equal");
	BehaviorDescription = NSLOCTEXT("Remote Control Behaviour", "Behavior Desc - Is Equal", "Triggers an event when the input equals the specified value.");
}

bool URCBehaviourIsEqualNode::Execute_Implementation(URCBehaviour* InBehaviour) const
{
	if (!ensure(InBehaviour))
	{
		return false;
	}
	
	URCController* RCController = InBehaviour->ControllerWeakPtr.Get();
	if (!RCController)
	{
		return false;
	}

	URCIsEqualBehaviour* IsEqualBehaviour = Cast<URCIsEqualBehaviour>(InBehaviour);
	if (!IsEqualBehaviour)
	{
		return false;
	}

	return RCController->IsValueEqual(IsEqualBehaviour->PropertySelfContainer);
}

bool URCBehaviourIsEqualNode::IsSupported_Implementation(URCBehaviour* InBehaviour) const
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

UClass* URCBehaviourIsEqualNode::GetBehaviourClass() const
{
	return URCIsEqualBehaviour::StaticClass();
}

void URCBehaviourIsEqualNode::OnPassed_Implementation(URCBehaviour* InBehaviour) const
{
}



