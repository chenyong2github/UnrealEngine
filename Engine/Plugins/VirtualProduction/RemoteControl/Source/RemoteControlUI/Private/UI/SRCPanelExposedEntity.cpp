// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedEntity.h"

#include "RemoteControlEntity.h"
#include "RemoteControlField.h"
#include "SceneOutlinerFilters.h"
#include "SceneOutlinerModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

class FIsActorBindableFilter : public SceneOutliner::FOutlinerFilter, public TSharedFromThis<FIsActorBindableFilter>
{
public:
	FIsActorBindableFilter(const TSharedPtr<FRemoteControlEntity>& InEntity)
        : FOutlinerFilter(SceneOutliner::EDefaultFilterBehaviour::Fail)
        , WeakEntity(InEntity)
	{}

	//~ Begin SceneOutliner::FOutlinerFilter interface
	virtual bool PassesFilter(const AActor* Actor) const override
	{
		if (TSharedPtr<FRemoteControlEntity> Entity = WeakEntity.Pin())
		{
			return Entity->CanBindObject(Actor);
		}
		return false;
	}
	//~ End SceneOutliner::FOutlinerFilter interface

private:
	/** Holds the weak entity that is going to be rebound. */
	TWeakPtr<FRemoteControlEntity> WeakEntity;
};

TSharedRef<SWidget> SRCPanelExposedEntity::CreateInvalidWidget()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	SceneOutliner::FInitializationOptions Options;
	Options.Filters = MakeShared<SceneOutliner::FOutlinerFilters>();

	if (TSharedPtr<FRemoteControlEntity> Entity = GetEntity())
	{
		Options.Filters->Add(MakeShared<FIsActorBindableFilter>(Entity));
	}

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
				SceneOutlinerModule.CreateSceneOutliner(Options, FOnActorPicked::CreateRaw(this, &SRCPanelExposedEntity::OnActorSelected))
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

#undef LOCTEXT_NAMESPACE