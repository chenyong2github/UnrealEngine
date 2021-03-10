// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelInputBindings.h"

#include "IStructureDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RemoteControlActor.h"
#include "RemoteControlEntity.h"
#include "RemoteControlPreset.h"
#include "SRCPanelExposedActor.h"
#include "SRCPanelExposedEntitiesList.h"
#include "SRCPanelExposedField.h"
#include "SRCPanelTreeNode.h"
#include "Templates/UnrealTypeTraits.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"

namespace RCPanelInputBindings
{
	template <typename EntityType> 
	TSharedPtr<FStructOnScope> GetEntityOnScope(const TSharedPtr<EntityType>& Entity)
	{
		static_assert(TIsDerivedFrom<EntityType, FRemoteControlEntity>::Value, "EntityType must derive from FRemoteControlEntity.");
		if (Entity)
		{
			return MakeShared<FStructOnScope>(EntityType::StaticStruct(), reinterpret_cast<uint8*>(Entity.Get()));
		}

		return nullptr;
	}
}

void SRCPanelInputBindings::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	EntityList = SNew(SRCPanelExposedEntitiesList, InPreset)
		.DisplayValues(false);

	EntityList->OnSelectionChange().AddSP(this, &SRCPanelInputBindings::UpdateEntityDetailsView);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			// Status, Protocol picker
			SNew(SBorder)
			.Padding(0.f)
			[
				SNew(STextBlock)
				.Text(INVTEXT("Top bar"))
			]
		]
		+ SVerticalBox::Slot()
		.Padding(0.f)
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value(0.3f)
			[
				// Exposed entities List
				SNew(SBorder)
				[
					EntityList.ToSharedRef()
				]
			]
			+ SSplitter::Slot()
			.Value(0.7f)
			[
				SNew(SBorder)
				.Padding(0.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.6f)
					[
						CreateEntityDetailsView()
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

TSharedRef<SWidget> SRCPanelInputBindings::CreateEntityDetailsView()
{
	FDetailsViewArgs Args;
	Args.bShowOptions = false;
	Args.bAllowFavoriteSystem = false;
	Args.bAllowSearch = false;
	Args.bShowScrollBar = false;

	EntityDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateStructureDetailView(MoveTemp(Args), FStructureDetailsViewArgs(), nullptr);

	UpdateEntityDetailsView(EntityList->GetSelection());
	if (ensure(EntityDetailsView && EntityDetailsView->GetWidget()))
	{
		return EntityDetailsView->GetWidget().ToSharedRef();
	}
	return SNullWidget::NullWidget;
}

void SRCPanelInputBindings::UpdateEntityDetailsView(const TSharedPtr<SRCPanelTreeNode>& SelectedNode)
{
	TSharedPtr<FStructOnScope> SelectedEntityPtr;
	if (SelectedNode)
	{
		if (TSharedPtr<SRCPanelExposedField> FieldWidget = SelectedNode->AsField())
		{
			if (TSharedPtr<FRemoteControlField> Field = FieldWidget->GetRemoteControlField().Pin())
			{
				SelectedEntity = Field;
				SelectedEntityPtr = RCPanelInputBindings::GetEntityOnScope<FRemoteControlField>(Field);
			}
		}
		else if (TSharedPtr<SRCPanelExposedActor> ActorWidget = SelectedNode->AsActor())
		{
			if (TSharedPtr<FRemoteControlActor> Actor = ActorWidget->GetRemoteControlActor().Pin())
			{
				SelectedEntity = Actor;
				SelectedEntityPtr = RCPanelInputBindings::GetEntityOnScope<FRemoteControlActor>(Actor);
			}
		}
	}
	if (ensure(EntityDetailsView))
	{
		EntityDetailsView->SetStructureData(SelectedEntityPtr);
	}
}
