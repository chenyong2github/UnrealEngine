// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UObject/WeakFieldPtr.h"

class FRCBehaviourModel;
class IDetailTreeNode;
class SRemoteControlPanel;
class URCBehaviour;
class URCControllerContainer;
class URCUserDefinedStruct;
class URCVirtualPropertyContainerBase;
class URCVirtualPropertyBase;

/*
* ~ FRCControllerModel ~
*
* UI model for representing a Controller
* Contains a row widget with Controller Name and a Value Widget
*/
class FRCControllerModel : public FRCLogicModeBase
{
public:
	FRCControllerModel(const FName& PropertyName, const TSharedRef<IDetailTreeNode>& InTreeNode, const TSharedPtr<SRemoteControlPanel> InRemoteControlPanel);
	
	/** The widget to be rendered for this Controller
	* Used to represent a single row when added to the Controllers Panel List
	*/
	virtual TSharedRef<SWidget> GetWidget() const override;

	/** Returns the Controller (Data Model) associated with us*/
	virtual URCVirtualPropertyBase* GetVirtualProperty() const;
	
	/** Returns the Controller Name (from Virtual Property) represented by us*/
	virtual const FName& GetPropertyName() const;

	/** Returns the currently selected Behaviour (UI model) */
	TSharedPtr<FRCBehaviourModel> GetSelectedBehaviourModel() const;

	/** Updates our internal record of the currently selected Behaviour (UI Model) */
	void UpdateSelectedBehaviourModel(TSharedPtr<FRCBehaviourModel> InModel);

private:
	/** The Parent Container holding our Controller (Virtual Property) Data Model */
	TWeakObjectPtr<URCVirtualPropertyContainerBase> PropertyContainerWeakPtr;
	
	/** The row generator used to build a generic Value Widget*/
	TWeakPtr<IDetailTreeNode> DetailTreeNodeWeakPtr;

	/** The currently selected Behaviour (UI model) */
	TWeakPtr<FRCBehaviourModel>  SelectedBehaviourModelWeakPtr;

	/** The Controller (Virtual Property Name) represented by us*/
	FName PropertyName;
};
