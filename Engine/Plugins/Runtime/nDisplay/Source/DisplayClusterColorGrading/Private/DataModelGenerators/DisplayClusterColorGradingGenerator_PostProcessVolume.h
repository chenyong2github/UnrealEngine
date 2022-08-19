// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterColorGradingDataModel.h"

class IDetailTreeNode;

/** Color Grading Data Model Generator for the APostProcessVolume actor class */
class FDisplayClusterColorGradingGenerator_PostProcessVolume: public IDisplayClusterColorGradingDataModelGenerator
{
public:
	static TSharedRef<IDisplayClusterColorGradingDataModelGenerator> MakeInstance();

	//~ IDisplayClusterColorGradingDataModelGenerator interface
	virtual void GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel) override;
	//~ End IDisplayClusterColorGradingDataModelGenerator interface

private:
	/** Creates a new color grading element structure for the specified detail tree node, which is expected to have child color properties with the ColorGradingMode metadata set */
	FDisplayClusterColorGradingDataModel::FColorGradingElement CreateColorGradingElement(const TSharedRef<IDetailTreeNode>& GroupNode, FText ElementLabel);

	/** Adds all child properties of the specified detail tree node to the color grading group's list of properties to display in the details view*/
	void AddPropertiesToDetailsView(const TSharedRef<IDetailTreeNode>& GroupNode, FDisplayClusterColorGradingDataModel::FColorGradingGroup& ColorGradingGroup);
};