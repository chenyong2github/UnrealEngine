// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingGenerator_PostProcessVolume.h"

#include "Engine/PostProcessVolume.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

TSharedRef<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingGenerator_PostProcessVolume::MakeInstance()
{
	return MakeShareable(new FDisplayClusterColorGradingGenerator_PostProcessVolume());
}

void FDisplayClusterColorGradingGenerator_PostProcessVolume::GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel)
{
	const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = PropertyRowGenerator.GetRootTreeNodes();

	const TSharedRef<IDetailTreeNode>* ColorGradingNodePtr = RootNodes.FindByPredicate([](const TSharedRef<IDetailTreeNode>& Node)
	{
		return Node->GetNodeName() == TEXT("Color Grading");
	});

	if (ColorGradingNodePtr)
	{
		const TSharedRef<IDetailTreeNode> ColorGradingNode = *ColorGradingNodePtr;
		FDisplayClusterColorGradingDataModel::FColorGradingGroup ColorGradingGroup;

		TArray<TSharedRef<IDetailTreeNode>> PropertyGroupNodes;
		ColorGradingNode->GetChildren(PropertyGroupNodes);

		for (const TSharedRef<IDetailTreeNode>& PropertyGroupNode : PropertyGroupNodes)
		{
			FString CategoryName = TEXT(""); 
			FString GroupName = TEXT("");
			PropertyGroupNode->GetNodeName().ToString().Split(TEXT("|"), &CategoryName, &GroupName);

			if (GroupName == TEXT("Global") || GroupName == TEXT("Shadows") || GroupName == TEXT("Midtones") || GroupName == TEXT("Highlights"))
			{
				FDisplayClusterColorGradingDataModel::FColorGradingElement ColorGradingElement = CreateColorGradingElement(PropertyGroupNode, FText::FromString(GroupName));
				ColorGradingGroup.ColorGradingElements.Add(ColorGradingElement);
			}

			AddPropertiesToDetailsView(PropertyGroupNode, ColorGradingGroup);
		}

		OutColorGradingDataModel.ColorGradingGroups.Add(ColorGradingGroup);
	}
}

FDisplayClusterColorGradingDataModel::FColorGradingElement FDisplayClusterColorGradingGenerator_PostProcessVolume::CreateColorGradingElement(const TSharedRef<IDetailTreeNode>& GroupNode, FText ElementLabel)
{
	FDisplayClusterColorGradingDataModel::FColorGradingElement ColorGradingElement;
	ColorGradingElement.DisplayName = ElementLabel;

	TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
	GroupNode->GetChildren(ChildNodes);

	for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = ChildNode->CreatePropertyHandle();
		if (PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
		{
			const FString ColorGradingModeString = PropertyHandle->GetProperty()->GetMetaData(TEXT("ColorGradingMode")).ToLower();

			if (!ColorGradingModeString.IsEmpty())
			{
				if (ColorGradingModeString.Compare(TEXT("saturation")) == 0)
				{
					ColorGradingElement.SaturationPropertyHandle = PropertyHandle;
				}
				else if (ColorGradingModeString.Compare(TEXT("contrast")) == 0)
				{
					ColorGradingElement.ContrastPropertyHandle = PropertyHandle;
				}
				else if (ColorGradingModeString.Compare(TEXT("gamma")) == 0)
				{
					ColorGradingElement.GammaPropertyHandle = PropertyHandle;
				}
				else if (ColorGradingModeString.Compare(TEXT("gain")) == 0)
				{
					ColorGradingElement.GainPropertyHandle = PropertyHandle;
				}
				else if (ColorGradingModeString.Compare(TEXT("offset")) == 0)
				{
					ColorGradingElement.OffsetPropertyHandle = PropertyHandle;
				}
			}
		}
	}

	return ColorGradingElement;
}

void FDisplayClusterColorGradingGenerator_PostProcessVolume::AddPropertiesToDetailsView(const TSharedRef<IDetailTreeNode>& GroupNode, FDisplayClusterColorGradingDataModel::FColorGradingGroup& ColorGradingGroup)
{
	FString CategoryName = TEXT("");
	FString GroupName = TEXT("");
	GroupNode->GetNodeName().ToString().Split(TEXT("|"), &CategoryName, &GroupName);

	TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
	GroupNode->GetChildren(ChildNodes);

	for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = ChildNode->CreatePropertyHandle();
		if (PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
		{
			const bool bIsColorProperty = PropertyHandle->GetProperty()->HasMetaData(TEXT("ColorGradingMode"));

			if (!bIsColorProperty)
			{
				PropertyHandle->SetInstanceMetaData(TEXT("CategoryOverride"), GroupName);
				ColorGradingGroup.DetailsViewPropertyHandles.Add(PropertyHandle);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE