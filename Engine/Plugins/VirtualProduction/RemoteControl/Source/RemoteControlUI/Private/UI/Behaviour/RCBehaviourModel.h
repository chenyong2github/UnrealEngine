// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Behaviour/RCIsEqualBehaviour.h"
#include "UI/BaseLogicUI/RCLogicModeBase.h"

class IPropertyRowGenerator;
class SWidget;
class URCBehaviour;

/*
* ~ FRCBehaviourModel ~
*
* UI model for representing a Behaviour
* Generates a row widget with Behaviour related metadata
*/
class FRCBehaviourModel : public FRCLogicModeBase
{
public:
	FRCBehaviourModel(URCBehaviour* InBehaviour);

	/** The widget to be rendered for this Property in the Behaviour panel
	* Currently displays Behaviour Name metadata in the Behaviours Panel List
	*/
	virtual TSharedRef<SWidget> GetWidget() const override;

	/** Builds a Behaviour specific widget that child Behaviour classes can implement as required*/
	virtual TSharedRef<SWidget> GetBehaviourDetailsWidget() const;

	/** Returns the Behaviour (Data model) associated with us*/
	URCBehaviour* GetBehaviour() const;

	/** Handling for user action to override this behaviour via new Blueprint class */
	void OnOverrideBlueprint() const;	

private:
	/** The Behaviour (Data model) associated with us*/
	TWeakObjectPtr<URCBehaviour> BehaviourWeakPtr;
};