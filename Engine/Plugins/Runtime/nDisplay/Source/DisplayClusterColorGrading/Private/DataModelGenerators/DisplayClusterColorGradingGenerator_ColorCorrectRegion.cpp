// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingGenerator_ColorCorrectRegion.h"

#include "ClassIconFinder.h"
#include "ColorCorrectRegion.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyHandle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

TSharedRef<IDisplayClusterColorGradingDataModelGenerator> FDisplayClusterColorGradingGenerator_ColorCorrectRegion::MakeInstance()
{
	return MakeShareable(new FDisplayClusterColorGradingGenerator_ColorCorrectRegion());
}

void FDisplayClusterColorGradingGenerator_ColorCorrectRegion::GenerateDataModel(IPropertyRowGenerator& PropertyRowGenerator, FDisplayClusterColorGradingDataModel& OutColorGradingDataModel)
{
	TArray<TWeakObjectPtr<AColorCorrectRegion>> SelectedCCRs;
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyRowGenerator.GetSelectedObjects();
	for (const TWeakObjectPtr<UObject>& SelectedObject : SelectedObjects)
	{
		if (SelectedObject.IsValid() && SelectedObject->IsA<AColorCorrectRegion>())
		{
			TWeakObjectPtr<AColorCorrectRegion> SelectedRootActor = CastChecked<AColorCorrectRegion>(SelectedObject.Get());
			SelectedCCRs.Add(SelectedRootActor);
		}
	}

	if (!SelectedCCRs.Num())
	{
		return;
	}

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
					.Image(FClassIconFinder::FindIconForActor(SelectedCCRs[0]))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(FText::FromString(SelectedCCRs[0]->GetActorLabel()))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
			];

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

#undef LOCTEXT_NAMESPACE