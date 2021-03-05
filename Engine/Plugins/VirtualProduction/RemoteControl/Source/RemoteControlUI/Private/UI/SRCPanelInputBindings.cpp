// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelInputBindings.h"

#include "SRCPanelExposedEntitiesList.h"
#include "SRCPanelExposedField.h"
#include "SRCPanelTreeNode.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"

void SRCPanelInputBindings::Construct(const FArguments& InArgs, URemoteControlPreset* Preset)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			// Status, Protocol picker
			SNew(SBorder)
			[
				SNew(STextBlock)
				.Text(INVTEXT("Top bar"))
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.3f)
			[
				// Exposed Fields List
				SNew(SBorder)
				[
					SAssignNew(EntityList, SRCPanelExposedEntitiesList, Preset)
					.DisplayValues(false)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.7f)
			[
				SNew(SBorder)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.6f)
					[
						// Exposed Field details view
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							if (TSharedPtr<SRCPanelTreeNode> SelectedNode = EntityList->GetSelection())
							{
								if (TSharedPtr<SRCPanelExposedField> Field = SelectedNode->AsField())
								{
									return FText::FromName(Field->GetFieldLabel());
								}
							}
						
							return FText::GetEmpty();
						})
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.4f)
					[
						// Input Mapping details view.
						SNew(STextBlock)
						.Text(INVTEXT("Bottom Right bar"))
					]
				]
			]
		]
	];
}
