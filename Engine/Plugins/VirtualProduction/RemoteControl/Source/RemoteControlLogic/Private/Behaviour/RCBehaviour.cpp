// Copyright Epic Games, Inc. All Rights Reserved.

#include "Behaviour/RCBehaviour.h"

#include "Engine/Blueprint.h"

#include "Action/RCActionContainer.h"
#include "Behaviour/RCBehaviourNode.h"

URCBehaviour::URCBehaviour()
{
	ActionContainer = CreateDefaultSubobject<URCActionContainer>(FName("ActionContainer"));
}

void URCBehaviour::Execute()
{
	if (!ActionContainer->Actions.Num())
	{
		return;
	}
	const URCBehaviourNode* BehaviourNode = GetBehaviourNode();

	// Execute before the logic
	BehaviourNode->PreExecute(this);

	if (BehaviourNode->Execute(this))
	{
		ActionContainer->ExecuteActions();
		BehaviourNode->OnPassed(this);
	}
}

int32 URCBehaviour::GetNumActions() const
{
	return ActionContainer->Actions.Num();
}

UClass* URCBehaviour::GetOverrideBehaviourBlueprintClass() const
{
	// Blueprint extensions stores as a separate blueprints files that is why it should check can be path resolved as asset 
	if (!OverrideBehaviourBlueprintClassPath.IsValid() || !ensure(OverrideBehaviourBlueprintClassPath.IsAsset()))
	{
		return nullptr;
	}

	// If class was loaded before then just resolve class, that is faster
	if (UClass* ResolveClass = OverrideBehaviourBlueprintClassPath.ResolveClass())
	{
		return ResolveClass;
	}

	// Load class from the UObject path
	return OverrideBehaviourBlueprintClassPath.TryLoadClass<URCBehaviourNode>();
}

#if WITH_EDITORONLY_DATA
UBlueprint* URCBehaviour::GetBlueprint() const
{
	UClass* OverrideBehaviourBlueprintClass = GetOverrideBehaviourBlueprintClass();
	return OverrideBehaviourBlueprintClass ? Cast<UBlueprint>(OverrideBehaviourBlueprintClass->ClassGeneratedBy) : nullptr;
}
#endif

void URCBehaviour::SetOverrideBehaviourBlueprintClass(UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		OverrideBehaviourBlueprintClassPath = InBlueprint->GeneratedClass.Get();
	}
}

URCBehaviourNode* URCBehaviour::GetBehaviourNode()
{
	UClass* OverrideBehaviourBlueprintClass = GetOverrideBehaviourBlueprintClass();
	UClass* FinalBehaviourNodeClass = OverrideBehaviourBlueprintClass ? OverrideBehaviourBlueprintClass : BehaviourNodeClass.Get();

	if (!CachedBehaviourNode || CachedBehaviourNodeClass != FinalBehaviourNodeClass)
	{
		CachedBehaviourNode = NewObject<URCBehaviourNode>(this, FinalBehaviourNodeClass);
	}

	CachedBehaviourNodeClass = FinalBehaviourNodeClass;

	return CachedBehaviourNode;
}

#if WITH_EDITOR
const FText& URCBehaviour::GetDisplayName()
{
	if (!ensure(BehaviourNodeClass))
	{
		return FText::GetEmpty();
	}

	return GetDefault<URCBehaviourNode>(BehaviourNodeClass)->DisplayName;
}

const FText& URCBehaviour::GetBehaviorDescription()
{
	if (!ensure(BehaviourNodeClass))
	{
		return FText::GetEmpty();
	}

	return GetDefault<URCBehaviourNode>(BehaviourNodeClass)->BehaviorDescription;
}
#endif