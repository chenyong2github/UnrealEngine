// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UObject/WeakFieldPtr.h"

class IDetailTreeNode;
class IPropertyRowGenerator;
class SWidget;
class URCAction;
class URCPropertyAction;
class URCVirtualProperty;
class URCFunctionAction;

/* 
* ~ FRCActionModel ~
*
* UI model for representing an Action.
* Contains a row widget with Action related metadata and a generic value widget
*/
class FRCActionModel : public FRCLogicModeBase
{
public:
	FRCActionModel(URCAction* InAction);
	
	/** Fetch the Action (data model) associated with us*/
	URCAction* GetAction() const;

private:
	/** The Action (data model) associated with us*/
	TWeakObjectPtr<URCAction> ActionWeakPtr;
};

/* 
* ~ FRCPropertyActionModel ~
*
* UI model for Property based Actions
*/
class FRCPropertyActionModel : public FRCActionModel
{
public:
	FRCPropertyActionModel(URCPropertyAction* InPropertyAction);

	/** Property Name associated with this Action */
	const FName& GetPropertyName() const;

	/** The widget to be rendered for this Property 
	* Used to represent a single row when added to the Actions Panel List
	*/
	virtual TSharedRef<SWidget> GetWidget() const override;

private:
	/** The Property Action (data model) associated with us*/
	TWeakObjectPtr<URCPropertyAction> PropertyActionWeakPtr;

	/** The row generator used to represent this widget as a row, when used with SListView */
	TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator;

	/** Used to create a generic Value Widget for the property row widget*/
	TWeakPtr<IDetailTreeNode> DetailTreeNodeWeakPtr;
};

/*
* ~ FRCFunctionActionModel ~
*
* UI model for Function based Actions
*/
class FRCFunctionActionModel : public FRCActionModel
{
public:
	FRCFunctionActionModel(URCFunctionAction* InAction);

	/** The widget to be rendered for this Function
	* Used to represent a single row when added to the Actions Panel List
	*/
	virtual TSharedRef<SWidget> GetWidget() const override;

private:
	/** The Function Action (data model) associated with us*/
	TWeakObjectPtr<URCFunctionAction> FunctionActionWeakPtr;
};
