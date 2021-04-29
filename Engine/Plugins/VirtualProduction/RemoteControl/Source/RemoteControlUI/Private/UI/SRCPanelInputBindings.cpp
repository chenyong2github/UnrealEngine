// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelInputBindings.h"

#include "IStructureDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RemoteControlActor.h"
#include "RemoteControlEntity.h"
#include "RemoteControlPreset.h"
#include "IRemoteControlProtocolWidgetsModule.h"
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
	Preset = InPreset;
	
	EntityList = SNew(SRCPanelExposedEntitiesList, InPreset)
		.DisplayValues(false);

	EntityList->OnSelectionChange().AddSP(this, &SRCPanelInputBindings::UpdateEntityDetailsView);

	EntityProtocolDetails = SNew(SBox);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			// Status, Protocol picker
			SNew(SBorder)
			.Padding(10.f)
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
					SNew(SSplitter)
					.Orientation(Orient_Vertical)
					+ SSplitter::Slot()
					.Value(0.2f)
					[
						CreateEntityDetailsView()
					]
					+ SSplitter::Slot()
					.Value(0.8f)
					[
						EntityProtocolDetails.ToSharedRef()
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
	EExposedFieldType FieldType = EExposedFieldType::Invalid;
	if (SelectedNode)
	{
		if (const TSharedPtr<SRCPanelExposedField> FieldWidget = SelectedNode->AsField())
		{
			if (const TSharedPtr<FRemoteControlField> Field = FieldWidget->GetRemoteControlField().Pin())
			{
				if(Field->FieldType == (FieldType = EExposedFieldType::Property))
				{
					SelectedEntityPtr = RCPanelInputBindings::GetEntityOnScope(StaticCastSharedPtr<FRemoteControlProperty>(Field));
				}
				else if(Field->FieldType == (FieldType = EExposedFieldType::Function))
				{
					SelectedEntityPtr = RCPanelInputBindings::GetEntityOnScope(StaticCastSharedPtr<FRemoteControlField>(Field));
				}
				else
				{
					checkNoEntry();
				}
				
				SelectedEntity = Field;			
			}
		}
		else if (const TSharedPtr<SRCPanelExposedActor> ActorWidget = SelectedNode->AsActor())
		{
			if (const TSharedPtr<FRemoteControlActor> Actor = ActorWidget->GetRemoteControlActor().Pin())
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

	static const FName ProtocolWidgetsModuleName = "RemoteControlProtocolWidgets";	
	if(SelectedEntity && SelectedNode.IsValid() && FModuleManager::Get().IsModuleLoaded(ProtocolWidgetsModuleName))
	{
		// If the SelectedNode is valid, the Preset should be too.
		if(ensure(Preset.IsValid()))
		{
			IRemoteControlProtocolWidgetsModule& ProtocolWidgetsModule = FModuleManager::LoadModuleChecked<IRemoteControlProtocolWidgetsModule>(ProtocolWidgetsModuleName);
			EntityProtocolDetails->SetContent(ProtocolWidgetsModule.GenerateDetailsForEntity(Preset.Get(), SelectedEntity->GetId(), FieldType));	
		}
	}
}
