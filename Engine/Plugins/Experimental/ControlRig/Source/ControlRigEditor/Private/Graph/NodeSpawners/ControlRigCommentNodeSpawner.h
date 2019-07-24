// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "UObject/UnrealType.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "BlueprintActionFilter.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintFieldNodeSpawner.h"
#include "ControlRigCommentNodeSpawner.generated.h"

class UControlRigGraphNode;

UCLASS(Transient)
class CONTROLRIGEDITOR_API UControlRigCommentNodeSpawner : public UBlueprintNodeSpawner
{
	GENERATED_BODY()

public:
	/**
	 * Creates a new UControlRigVariableNodeSpawner, charged with spawning 
	 * a new member-variable node
	 * 
	 * @return A newly allocated instance of this class.
	 */
	static UControlRigCommentNodeSpawner* Create();

	// UBlueprintNodeSpawner interface
	virtual void Prime() override;
	virtual FBlueprintNodeSignature GetSpawnerSignature() const override;
	virtual FBlueprintActionUiSpec GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const override;
	virtual UEdGraphNode* Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const override;
	virtual bool IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const override;
	// End UBlueprintNodeSpawner interface

private:

	friend class UEngineTestControlRig;
	friend class FControlRigEditor;
};
