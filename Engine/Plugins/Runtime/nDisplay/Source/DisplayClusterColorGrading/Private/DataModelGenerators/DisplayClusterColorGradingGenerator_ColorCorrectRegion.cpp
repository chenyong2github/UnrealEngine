// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingGenerator_ColorCorrectRegion.h"

#include "ColorCorrectRegion.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

TSharedRef<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingGenerator_ColorCorrectRegion::MakeInstance()
{
	return MakeShareable(new FDisplayClusterColorGradingGenerator_ColorCorrectRegion());
}

void FDisplayClusterColorGradingGenerator_ColorCorrectRegion::GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel)
{
	const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = PropertyRowGenerator.GetRootTreeNodes();

	const TSharedRef<IDetailTreeNode>* ColorCorrectionNodePtr = RootNodes.FindByPredicate([](const TSharedRef<IDetailTreeNode>& Node)
	{
		return Node->GetNodeName() == TEXT("Color Correction");
	});

	if (ColorCorrectionNodePtr)
	{
		const TSharedRef<IDetailTreeNode> ColorCorrectionNode = *ColorCorrectionNodePtr;
		FDisplayClusterColorGradingDataModel::FColorGradingGroup ColorGradingGroup;

		AddPropertiesToDetailsView(ColorCorrectionNode, ColorGradingGroup);

		TArray<TSharedRef<IDetailTreeNode>> ColorCorrectionPropertyNodes;
		ColorCorrectionNode->GetChildren(ColorCorrectionPropertyNodes);

		const TSharedRef<IDetailTreeNode>* ColorGradingSettingsNodePtr = ColorCorrectionPropertyNodes.FindByPredicate([](const TSharedRef<IDetailTreeNode>& Node)
		{
			return Node->GetNodeName() == GET_MEMBER_NAME_CHECKED(AColorCorrectRegion, ColorGradingSettings);
		});

		if (ColorGradingSettingsNodePtr)
		{
			const TSharedRef<IDetailTreeNode> ColorGradingSettingsNode = *ColorGradingSettingsNodePtr;

			TArray<TSharedRef<IDetailTreeNode>> ColorGradingPropertyNodes;
			ColorGradingSettingsNode->GetChildren(ColorGradingPropertyNodes);

			for (const TSharedRef<IDetailTreeNode>& PropertyNode : ColorGradingPropertyNodes)
			{
				FName NodeName = PropertyNode->GetNodeName();
				if (NodeName == GET_MEMBER_NAME_CHECKED(FColorGradingSettings, Global) ||
					NodeName == GET_MEMBER_NAME_CHECKED(FColorGradingSettings, Shadows) ||
					NodeName == GET_MEMBER_NAME_CHECKED(FColorGradingSettings, Midtones) ||
					NodeName == GET_MEMBER_NAME_CHECKED(FColorGradingSettings, Highlights))
				{
					TSharedPtr<IPropertyHandle> PropertyHandle = PropertyNode->CreatePropertyHandle();
					PropertyHandle->MarkHiddenByCustomization();

					FDisplayClusterColorGradingDataModel::FColorGradingElement ColorGradingElement = CreateColorGradingElement(PropertyNode, FText::FromName(PropertyNode->GetNodeName()));
					ColorGradingGroup.ColorGradingElements.Add(ColorGradingElement);
				}
			}
		}

		OutColorGradingDataModel.ColorGradingGroups.Add(ColorGradingGroup);
	}
}

FDisplayClusterColorGradingDataModel::FColorGradingElement FDisplayClusterColorGradingGenerator_ColorCorrectRegion::CreateColorGradingElement(const TSharedRef<IDetailTreeNode>& GroupNode, FText ElementLabel)
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

void FDisplayClusterColorGradingGenerator_ColorCorrectRegion::AddPropertiesToDetailsView(const TSharedRef<IDetailTreeNode>& GroupNode, FDisplayClusterColorGradingDataModel::FColorGradingGroup& ColorGradingGroup)
{
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
				PropertyHandle->SetInstanceMetaData(TEXT("CategoryOverride"), GroupNode->GetNodeName().ToString());
				ColorGradingGroup.DetailsViewPropertyHandles.Add(PropertyHandle);
			}
		}
	}
}