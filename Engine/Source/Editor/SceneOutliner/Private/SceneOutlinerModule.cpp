// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Application/SlateApplication.h"
#include "SSceneOutliner.h"

#include "SceneOutlinerActorInfoColumn.h"
#include "SceneOutlinerGutter.h"
#include "SceneOutlinerItemLabelColumn.h"
#include "SceneOutlinerPublicTypes.h"

#include "ActorPickingMode.h"
#include "ActorBrowsingMode.h"
#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"

/* FSceneOutlinerModule interface
 *****************************************************************************/

void FSceneOutlinerModule::StartupModule()
{
	RegisterDefaultColumnType< SceneOutliner::FItemLabelColumn >(SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 10));

	// Register builtin column types which are not active by default
	RegisterColumnType<SceneOutliner::FSceneOutlinerGutter>();
	RegisterColumnType<SceneOutliner::FActorInfoColumn>();
}


void FSceneOutlinerModule::ShutdownModule()
{
	UnRegisterColumnType<SceneOutliner::FSceneOutlinerGutter>();
	UnRegisterColumnType<SceneOutliner::FItemLabelColumn>();
	UnRegisterColumnType<SceneOutliner::FActorInfoColumn>();
}

TSharedRef<ISceneOutliner> FSceneOutlinerModule::CreateSceneOutliner(const SceneOutliner::FInitializationOptions& InitOptions) const
{
	return SNew(SceneOutliner::SSceneOutliner, InitOptions)
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute());
}

TSharedRef<ISceneOutliner> FSceneOutlinerModule::CreateActorPicker(const SceneOutliner::FInitializationOptions& InInitOptions, const FOnActorPicked& OnActorPickedDelegate, TWeakObjectPtr<UWorld> SpecifiedWorld) const
{
	auto OnItemPicked = FOnSceneOutlinerItemPicked::CreateLambda([OnActorPickedDelegate](TSharedRef<SceneOutliner::ITreeItem> Item)
		{
			if (SceneOutliner::FActorTreeItem* ActorItem = Item->CastTo<SceneOutliner::FActorTreeItem>())
			{
				if (ActorItem->IsValid())
				{
					OnActorPickedDelegate.ExecuteIfBound(ActorItem->Actor.Get());

				}
			}
		});

	SceneOutliner::FCreateOutlinerMode ModeFactory = SceneOutliner::FCreateOutlinerMode::CreateLambda([&OnItemPicked, &SpecifiedWorld](SceneOutliner::SSceneOutliner* Outliner)
		{
			return new SceneOutliner::FActorPickingMode(Outliner, true, OnItemPicked, SpecifiedWorld);
		});

	

	SceneOutliner::FInitializationOptions InitOptions(InInitOptions);
	InitOptions.ModeFactory = ModeFactory;
	if (InitOptions.ColumnMap.Num() == 0)
	{
		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::Label(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 0));
		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::ActorInfo(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 10));
	}
	return CreateSceneOutliner(InitOptions);
}

TSharedRef<ISceneOutliner> FSceneOutlinerModule::CreateComponentPicker(const SceneOutliner::FInitializationOptions& InInitOptions, const FOnComponentPicked& OnComponentPickedDelegate, TWeakObjectPtr<UWorld> SpecifiedWorld) const
{
	auto OnItemPicked = FOnSceneOutlinerItemPicked::CreateLambda([OnComponentPickedDelegate](TSharedRef<SceneOutliner::ITreeItem> Item)
		{
			if (SceneOutliner::FComponentTreeItem* ComponentItem = Item->CastTo<SceneOutliner::FComponentTreeItem>())
			{
				if (ComponentItem->IsValid())
				{
					OnComponentPickedDelegate.ExecuteIfBound(ComponentItem->Component.Get());
				}
			}
		});

	SceneOutliner::FCreateOutlinerMode ModeFactory = SceneOutliner::FCreateOutlinerMode::CreateLambda([&OnItemPicked, &SpecifiedWorld](SceneOutliner::SSceneOutliner* Outliner)
		{
			return new SceneOutliner::FActorPickingMode(Outliner, false, OnItemPicked, SpecifiedWorld);
		});

	SceneOutliner::FInitializationOptions InitOptions(InInitOptions);
	InitOptions.ModeFactory = ModeFactory;
	if (InitOptions.ColumnMap.Num() == 0)
	{
		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::Label(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 0));
		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::ActorInfo(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 10));
	}
	return CreateSceneOutliner(InitOptions);
}

TSharedRef< ISceneOutliner > FSceneOutlinerModule::CreateActorBrowser(const SceneOutliner::FInitializationOptions& InInitOptions, TWeakObjectPtr<UWorld> SpecifiedWorld) const
{
	SceneOutliner::FCreateOutlinerMode ModeFactory = SceneOutliner::FCreateOutlinerMode::CreateLambda([&SpecifiedWorld](SceneOutliner::SSceneOutliner* Outliner)
		{
			return new SceneOutliner::FActorBrowsingMode(Outliner, SpecifiedWorld);
		});

	SceneOutliner::FInitializationOptions InitOptions(InInitOptions);
	InitOptions.ModeFactory = ModeFactory;
	if (InitOptions.ColumnMap.Num() == 0)
	{
		InitOptions.UseDefaultColumns();
		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::Gutter(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 0));
		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::ActorInfo(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 20));
	}
	return CreateSceneOutliner(InitOptions);
}

IMPLEMENT_MODULE(FSceneOutlinerModule, SceneOutliner);
