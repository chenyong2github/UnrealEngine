// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntity.h"

#include "ActorTreeItem.h"
#include "GameFramework/Actor.h"
#include "RemoteControlEntity.h"
#include "RemoteControlField.h"
#include "SceneOutlinerFilters.h"
#include "SceneOutlinerModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

TSharedRef<SWidget> SRCPanelExposedEntity::CreateInvalidWidget()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	FSceneOutlinerInitializationOptions Options;
	Options.Filters = MakeShared<FSceneOutlinerFilters>();
	Options.Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateRaw(this, &SRCPanelExposedEntity::IsActorSelectable));

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
	    .AutoWidth()
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RebindLabel", "Rebind"))
			]
			.MenuContent()
			[
				SNew(SBox)
				.MaxDesiredHeight(400.0f)
				.WidthOverride(300.0f)
				[
					SceneOutlinerModule.CreateActorPicker(Options, FOnActorPicked::CreateRaw(this, &SRCPanelExposedEntity::OnActorSelected), nullptr)
				]
			]
		];
}

void SRCPanelExposedEntity::OnActorSelected(AActor* InActor) const
{
	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		Entity->BindObject(InActor);
	}
}

bool SRCPanelExposedEntity::IsActorSelectable(const AActor* Actor) const
{
	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		return Entity->CanBindObject(Actor);
	}
	return false;
}


#undef LOCTEXT_NAMESPACE