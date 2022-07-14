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

typedef TFunction<bool(const TSet<TObjectPtr<URCAction>>& Actions)> TRCActionUniquenessTest;

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

	TRCActionUniquenessTest GetDefaultActionUniquenessTest(const TSharedRef<const FRemoteControlField> InRemoteControlField);

	/** Add remote control property action  */
	URCAction* AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField);

	/** Add remote control property action  */
	URCAction* AddAction(TRCActionUniquenessTest IsUnique, const TSharedRef<const FRemoteControlField> InRemoteControlField);

	/** Find Action by given exposed filed id */
	URCAction* FindActionByFieldId(const FGuid InFieldId) const;

	/** Find Action by given remote control field */
	URCAction* FindActionByField(const TSharedRef<const FRemoteControlField> InRemoteControlField) const;
	
	/** Remove action by exposed field Id */
	virtual int32 RemoveAction(const FGuid InExposedFieldId);

	/** Remove Action by given action UObject */
	virtual int32 RemoveAction(URCAction* InAction);

	/** Empty action set */
	void EmptyActions();

	/** Set of actions */
	UPROPERTY()
	TSet<TObjectPtr<URCAction>> Actions;
	
	/** Set of child action container */
	UPROPERTY()
	TSet<TObjectPtr<URCActionContainer>> ActionContainers;

	/** Reference to Preset */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> PresetWeakPtr;

private:
	/** Add remote control property action  */
	URCPropertyAction* AddPropertyAction(const TSharedRef<const FRemoteControlProperty> InRemoteControlProperty);

	/** Add remote control property function */
	URCFunctionAction* AddFunctionAction(const TSharedRef<const FRemoteControlFunction> InRemoteControlFunction);
};
