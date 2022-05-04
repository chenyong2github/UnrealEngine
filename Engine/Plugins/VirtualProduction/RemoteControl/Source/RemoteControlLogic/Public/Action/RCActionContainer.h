// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "RCActionContainer.generated.h"

class URemoteControlPreset;
class URCAction;
class URCFunctionAction;
class URCPropertyAction;

struct FRemoteControlField;
struct FRemoteControlFunction;
struct FRemoteControlProperty;

/**
 * Container for created actions
 */
UCLASS()
class REMOTECONTROLLOGIC_API URCActionContainer : public UObject
{
	GENERATED_BODY()

public:
	/** Execute container actions */
	void ExecuteActions();

	/** Add remote control property action  */
	URCPropertyAction* AddAction(const TSharedPtr<FRemoteControlProperty> InRemoteControlProperty);

	/** Add remote control property function */
	URCFunctionAction* AddAction(const TSharedPtr<FRemoteControlFunction> InRemoteControlFunction);

	/** Find Action by given exposed filed id */
	URCAction* FindActionByFieldId(const FGuid InFieldId);

	/** Find Action by given remote control field */
	URCAction* FindActionByField(const TSharedPtr<FRemoteControlField> InRemoteControlField);
	
	/** Remove action by exposed field Id */
	virtual int32 RemoveAction(const FGuid InExposedFieldId);

	/** Remove Action by given action UObject */
	virtual int32 RemoveAction(URCAction* InAction);

	/** Empty action set */
	void EmptyActions();

public:
	/** Set of actions */
	UPROPERTY()
	TSet<TObjectPtr<URCAction>> Actions;
	
	/** Set of child action container */
	UPROPERTY()
	TSet<TObjectPtr<URCActionContainer>> ActionContainers;

	/** Reference to Preset */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> PresetWeakPtr;
};
