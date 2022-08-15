// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Action/RCActionContainer.h"
#include "Behaviour/RCBehaviour.h"

#include "RCBehaviourBind.generated.h"

class URCVirtualPropertySelfContainer;
class URCAction;

/**
 * [Bind Behaviour]
 * 
 * Binds a given Controller with multiple exposed properties.
 * Any changes to the value of the controller are directly propagated to the linked properties by Bind Behaviour. 
 */
UCLASS()
class REMOTECONTROLLOGIC_API URCBehaviourBind : public URCBehaviour
{
	GENERATED_BODY()

public:
	URCBehaviourBind();
	
	//~ Begin URCBehaviour interface
	virtual void Initialize() override;

	/** Add a Logic action using a remote control field as input */
	virtual URCAction* AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField) override;

	/** Whether we can create an action pertaining to a given remote control field for the current behaviour */
	virtual bool CanHaveActionForField(const TSharedPtr<FRemoteControlField> InRemoteControlField) const override;

	//~ End URCBehaviour interface

	/** Add an action specifically for Bind Behaviour */
	URCPropertyBindAction* AddPropertyBindAction(const TSharedRef<const FRemoteControlProperty> InRemoteControlProperty);

	/** Indicates whether we support binding of strings to numeric fields and vice versa.
	* This flag determines the list of bindable properties the user sees in the "Add Action" menu*/
	bool AreNumericInputsAllowedAsStrings() const { return bAllowNumericInputAsStrings; }
	void SetAllowNumericInputAsStrings(bool Value) { bAllowNumericInputAsStrings = Value; }
private:
	bool bAllowNumericInputAsStrings = false;
};
