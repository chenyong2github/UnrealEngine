// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UI/Behaviour/RCBehaviourModel.h"

class IDetailTreeNode;
class IPropertyRowGenerator;
class SWidget;

/*
* ~ FRCIsEqualBehaviourModel ~
*
* Child Behaviour class representing the "Is Equal" Behaviour's UI model
* 
* Generates a Property Widget where users can enter the comparison value.
* The value of this is compared with the associated Controller for evaluating Is Equal behaviour.
*/
class FRCIsEqualBehaviourModel : public FRCBehaviourModel
{
public:
	FRCIsEqualBehaviourModel(URCIsEqualBehaviour* IsEqualBehaviour);

	/** Builds a Behaviour specific widget as required for Is Equal behaviour*/
	virtual TSharedRef<SWidget> GetBehaviourDetailsWidget() override;

	/** Builds a generic Value Widget of the Controller's type
	* Use to store user input for performing the Is Equal Behaviour comparison*/
	TSharedRef<SWidget> GetPropertyWidget() const;

private:

	/** The Is Equal Behaviour (Data model) associated with us*/
	TWeakObjectPtr<URCIsEqualBehaviour> EqualBehaviourWeakPtr;

	/** The row generator used to build a generic Value Widget*/
	TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator;

	/** Used to create a generic Value Widget for comparison based on the active Controller's type*/
	TWeakPtr<IDetailTreeNode> DetailTreeNodeWeakPtr;
};