// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "ControlRigParameterNodeSpawner.generated.h"

class UControlRigGraphNode;

UCLASS(Transient)
class CONTROLRIGEDITOR_API UControlRigParameterNodeSpawner : public UBlueprintNodeSpawner
{
	GENERATED_BODY()

public:
	/**
	 * Creates a new UControlRigParameterNodeSpawner, charged with spawning 
	 * a new member-parameter node
	 * 
	 * @return A newly allocated instance of this class.
	 */
	static UControlRigParameterNodeSpawner* CreateFromPinType(const FEdGraphPinType& InPinType, bool bInIsGetter, const FText& InMenuDesc, const FText& InCategory, const FText& InTooltip);

	// UBlueprintNodeSpawner interface
	virtual void Prime() override;
	virtual FBlueprintNodeSignature GetSpawnerSignature() const override;
	virtual FBlueprintActionUiSpec GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const override;
	virtual UEdGraphNode* Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const override;
	// End UBlueprintNodeSpawner interface

private:
	/** The pin type we will spawn */
	FEdGraphPinType EdGraphPinType;
	bool bIsGetter;

	friend class UEngineTestControlRig;
};
