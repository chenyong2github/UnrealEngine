// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/Builtin/RCBehaviourSetAssetByPathNode.h"

#include "Behaviour/RCBehaviour.h"
#include "Behaviour/RCSetAssetByPathBehaviour.h"
#include "Controller/RCController.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"

URCBehaviourSetAssetByPathNode::URCBehaviourSetAssetByPathNode()
{
	DisplayName = NSLOCTEXT("Remote Control Behaviour", "Behaviour Name - Set Asset By Path", "Set Asset By Path");
	BehaviorDescription = NSLOCTEXT("Remote Control Behaviour", "Behavior Desc - Set Asset By Path", "Triggers an event which sets an object based on the TargetProperty Name.");
}

bool URCBehaviourSetAssetByPathNode::Execute_Implementation(URCBehaviour* InBehaviour) const
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

	URCSetAssetByPathBehaviour* SetAssetByPathBehaviour = Cast<URCSetAssetByPathBehaviour>(InBehaviour);
	if (!SetAssetByPathBehaviour)
	{
		return false;
	}

	FString ControllerString;
	FString TargetPropertyString;
	FString DefaultString;
	
	RCController->GetValueString(ControllerString);
	SetAssetByPathBehaviour->PropertyInContainer->GetVirtualProperty(SetAssetByPathBehaviourHelpers::TargetProperty)->GetValueString(TargetPropertyString);
	SetAssetByPathBehaviour->PropertyInContainer->GetVirtualProperty(SetAssetByPathBehaviourHelpers::DefaultProperty)->GetValueString(DefaultString);

	// Add Path String Concat
	FString ConcatPath = SetAssetByPathBehaviourHelpers::ContentFolder;
	for (FString PathPart : SetAssetByPathBehaviour->PathStruct.PathArray)
	{
		if (PathPart.IsEmpty())
		{
			continue;
		}
		// Add '/' if needed
		int32 IndexFound;

		if (PathPart.FindLastChar('_', IndexFound) && IndexFound == PathPart.Len() - 1)
		{
			// In the case there's underscore char in the at the end of one of the Path Strings, do nothing to facilitate more complex pathing behaviours.
		}
		else if (!PathPart.FindLastChar('/', IndexFound) || IndexFound < PathPart.Len() - 1)
		{
			PathPart = PathPart.AppendChar('/');
		}
		
		ConcatPath += PathPart;
	}
	
	return SetAssetByPathBehaviour->SetAssetByPath(ConcatPath, TargetPropertyString, DefaultString);
}

bool URCBehaviourSetAssetByPathNode::IsSupported_Implementation(URCBehaviour* InBehaviour) const
{
	static TArray<TPair<EPropertyBagPropertyType, UObject*>> SupportedPropertyBagTypes =
	{
		{ EPropertyBagPropertyType::String, nullptr }
	};
	
	return SupportedPropertyBagTypes.ContainsByPredicate(GetIsSupportedCallback(InBehaviour));
}

UClass* URCBehaviourSetAssetByPathNode::GetBehaviourClass() const
{
	return URCSetAssetByPathBehaviour::StaticClass();
}
