// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Behaviour/RCSetAssetByPathBehaviour.h"
#include "UI/Behaviour/RCBehaviourModel.h"

class IDetailTreeNode;
class SBox;


/*
 * ~ FRCSetAssetByPathModel ~
 *
 * Child Behaviour class representing the "Set Asset By Path" Behaviour's UI model.
 *
 * Generates several Widgets where users can enter the RootPath, Target Property, and Default Property as FStrings.
 * The values are then put together, getting a path towards a possible object.
 */
class FRCSetAssetByPathBehaviourModel : public FRCBehaviourModel
{
public:
	FRCSetAssetByPathBehaviourModel(URCSetAssetByPathBehaviour* SetAssetByPathBehaviour);

	/** Builds a Behaviour specific widget as required for the Set Asset By Path Behaviour */
	virtual TSharedRef<SWidget> GetBehaviourDetailsWidget() override;

	/*
	 * Builds the Property Details Widget, including a generic expendable Array Widget and
	 * further two Text Widgets representing elements needed for the Path Behaviour.
	 * All of them store the user input and use them to perform the SetAssetByPath Behaviour.
	 */
	TSharedRef<SWidget> GetPropertyWidget() const;
	
private:
	/** The SetAssetByPath Behaviour associated with this Model */
	TWeakObjectPtr<URCSetAssetByPathBehaviour> SetAssetByPathBehaviourWeakPtr;

	/** Pointer to the Widget holding the PathArray created for this behaviour */
	TSharedPtr<SBox> PathArrayWidget;

	/** The row generator used to build the generic value widgets */
	TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator;
	
	/** The row generator used to build the generic array value widget for the Path Array */
	TSharedPtr<IPropertyRowGenerator> PropertyRowGeneratorArray;

	/** Used to create a generic Value Widget based on the active Controller's type*/
	TArray<TSharedPtr<IDetailTreeNode>> DetailTreeNodeWeakPtr;
	
	/** Used to create a generic Value Widget based on the Paths Available*/
	TArray<TSharedPtr<IDetailTreeNode>> DetailTreeNodeWeakPtrArray;

private:
	void RegenerateWeakPtrInternal();
	
	/** Regenerates and creates a new PathArray Widget if changed */
	void RegeneratePathArrayWidget();
};
