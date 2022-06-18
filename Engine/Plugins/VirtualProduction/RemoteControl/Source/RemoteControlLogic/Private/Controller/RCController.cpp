// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controller/RCController.h"

#include "RCVirtualPropertyContainer.h"
#include "RemoteControlPreset.h"
#include "Action/RCActionContainer.h"
#include "Behaviour/RCBehaviour.h"
#include "Behaviour/RCBehaviourNode.h"

URCBehaviour* URCController::AddBehaviour(TSubclassOf<URCBehaviourNode> InBehaviourNodeClass)
{
	URCBehaviour* NewBehaviour = CreateBehaviour(InBehaviourNodeClass);
	if (!ensure(NewBehaviour))
	{
		return nullptr;
	}

	NewBehaviour->Initialize();
	
	Behaviours.Add(NewBehaviour);

	return NewBehaviour;
}

URCBehaviour* URCController::CreateBehaviour(TSubclassOf<URCBehaviourNode> InBehaviourNodeClass)
{
	const URCBehaviourNode* DefaultBehaviourNode = Cast<URCBehaviourNode>(InBehaviourNodeClass->GetDefaultObject());
	
	URCBehaviour* NewBehaviour = NewObject<URCBehaviour>(this, DefaultBehaviourNode->GetBehaviourClass());
	NewBehaviour->BehaviourNodeClass = InBehaviourNodeClass;
	NewBehaviour->Id = FGuid::NewGuid();
	NewBehaviour->ActionContainer->PresetWeakPtr = PresetWeakPtr;
	NewBehaviour->ControllerWeakPtr = this;
	
	if (!DefaultBehaviourNode->IsSupported(NewBehaviour))
	{
		return nullptr;
	}
	
	return NewBehaviour;
}

int32 URCController::RemoveBehaviour(URCBehaviour* InBehaviour)
{
	return Behaviours.Remove(InBehaviour);
}

int32 URCController::RemoveBehaviour(const FGuid InBehaviourId)
{
	int32 RemovedCount = 0;
	
	for (auto BehaviourIt = Behaviours.CreateIterator(); BehaviourIt; ++BehaviourIt)
	{
		if (const URCBehaviour* Behaviour = *BehaviourIt; Behaviour->Id == InBehaviourId)
		{
			BehaviourIt.RemoveCurrent();
			RemovedCount++;
		}
	}

	return RemovedCount;
}

void URCController::EmptyBehaviours()
{
	Behaviours.Empty();
}

void URCController::ExecuteBehaviours()
{
	for (URCBehaviour* Behaviour : Behaviours)
	{
		if (Behaviour->bIsEnabled)
		{
			Behaviour->Execute();
		}
	}
}
