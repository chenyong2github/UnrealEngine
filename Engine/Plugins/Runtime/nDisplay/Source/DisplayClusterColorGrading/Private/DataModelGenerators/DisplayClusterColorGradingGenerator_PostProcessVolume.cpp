// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingGenerator_PostProcessVolume.h"

#include "ClassIconFinder.h"
#include "Engine/PostProcessVolume.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyHandle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

TSharedRef<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingGenerator_PostProcessVolume::MakeInstance()
{
	return MakeShareable(new FDisplayClusterColorGradingGenerator_PostProcessVolume());
}

void FDisplayClusterColorGradingGenerator_PostProcessVolume::GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel)
{
	TArray<TWeakObjectPtr<APostProcessVolume>> SelectedPPVs;
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyRowGenerator.GetSelectedObjects();
	for (const TWeakObjectPtr<UObject>& SelectedObject : SelectedObjects)
	{
		if (SelectedObject.IsValid() && SelectedObject->IsA<APostProcessVolume>())
		{
			TWeakObjectPtr<APostProcessVolume> SelectedRootActor = CastChecked<APostProcessVolume>(SelectedObject.Get());
			SelectedPPVs.Add(SelectedRootActor);
		}
	}

	if (!SelectedPPVs.Num())
	{
		return;
	}

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

		ColorGradingGroup.GroupHeaderWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0, 1, 6, 1))
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(16)
				.HeightOverride(16)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FClassIconFinder::FindIconForActor(SelectedPPVs[0]))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(FText::FromString(SelectedPPVs[0]->GetActorLabel()))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			];

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