// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Application/SlateApplication.h"
#include "SSceneOutliner.h"

#include "SceneOutlinerActorInfoColumn.h"
#include "SceneOutlinerGutter.h"
#include "SceneOutlinerItemLabelColumn.h"
#include "SceneOutlinerActorSCCColumn.h"
#include "SceneOutlinerPublicTypes.h"

#include "ActorPickingMode.h"
#include "ActorBrowsingMode.h"
#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"

/* FSceneOutlinerModule interface
 *****************************************************************************/

void FSceneOutlinerModule::StartupModule()
{
	RegisterDefaultColumnType< FSceneOutlinerItemLabelColumn >(FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));

	// Register builtin column types which are not active by default
	RegisterColumnType<FSceneOutlinerGutter>();
	RegisterColumnType<FActorInfoColumn>();
	RegisterColumnType<FSceneOutlinerActorSCCColumn>();
}


void FSceneOutlinerModule::ShutdownModule()
{
	UnRegisterColumnType<FSceneOutlinerGutter>();
	UnRegisterColumnType<FSceneOutlinerItemLabelColumn>();
	UnRegisterColumnType<FActorInfoColumn>();
}

TSharedRef<ISceneOutliner> FSceneOutlinerModule::CreateSceneOutliner(const FSceneOutlinerInitializationOptions& InitOptions) const
{
	return SNew(SSceneOutliner, InitOptions)
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute());
}

TSharedRef<ISceneOutliner> FSceneOutlinerModule::CreateActorPicker(const FSceneOutlinerInitializationOptions& InInitOptions, const FOnActorPicked& OnActorPickedDelegate, TWeakObjectPtr<UWorld> SpecifiedWorld) const
{
	auto OnItemPicked = FOnSceneOutlinerItemPicked::CreateLambda([OnActorPickedDelegate](TSharedRef<ISceneOutlinerTreeItem> Item)
		{
			if (FActorTreeItem* ActorItem = Item->CastTo<FActorTreeItem>())
			{
				if (ActorItem->IsValid())
				{
					OnActorPickedDelegate.ExecuteIfBound(ActorItem->Actor.Get());

				}
			}
		});

	FCreateSceneOutlinerMode ModeFactory = FCreateSceneOutlinerMode::CreateLambda([&OnItemPicked, &SpecifiedWorld](SSceneOutliner* Outliner)
		{
			FActorModeParams Params;
			Params.SceneOutliner = Outliner;
			Params.SpecifiedWorldToDisplay = SpecifiedWorld;
			Params.bHideComponents = true;
			Params.bHideLevelInstanceHierarchy = true;
			return new FActorPickingMode(Params, OnItemPicked);
		});

	

	FSceneOutlinerInitializationOptions InitOptions(InInitOptions);
	InitOptions.ModeFactory = ModeFactory;
	if (InitOptions.ColumnMap.Num() == 0)
	{
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0));
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::ActorInfo(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));
	}
	return CreateSceneOutliner(InitOptions);
}

TSharedRef<ISceneOutliner> FSceneOutlinerModule::CreateComponentPicker(const FSceneOutlinerInitializationOptions& InInitOptions, const FOnComponentPicked& OnComponentPickedDelegate, TWeakObjectPtr<UWorld> SpecifiedWorld) const
{
	auto OnItemPicked = FOnSceneOutlinerItemPicked::CreateLambda([OnComponentPickedDelegate](TSharedRef<ISceneOutlinerTreeItem> Item)
		{
			if (FComponentTreeItem* ComponentItem = Item->CastTo<FComponentTreeItem>())
			{
				if (ComponentItem->IsValid())
				{
					OnComponentPickedDelegate.ExecuteIfBound(ComponentItem->Component.Get());
				}
			}
		});

	FCreateSceneOutlinerMode ModeFactory = FCreateSceneOutlinerMode::CreateLambda([&OnItemPicked, &SpecifiedWorld](SSceneOutliner* Outliner)
		{
			FActorModeParams Params;
			Params.SceneOutliner = Outliner;
			Params.SpecifiedWorldToDisplay = SpecifiedWorld;
			Params.bHideComponents = false;
			Params.bHideLevelInstanceHierarchy = true;
			return new FActorPickingMode(Params, OnItemPicked);
		});

	FSceneOutlinerInitializationOptions InitOptions(InInitOptions);
	InitOptions.ModeFactory = ModeFactory;
	if (InitOptions.ColumnMap.Num() == 0)
	{
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0));
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::ActorInfo(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));
	}
	return CreateSceneOutliner(InitOptions);
}

TSharedRef< ISceneOutliner > FSceneOutlinerModule::CreateActorBrowser(const FSceneOutlinerInitializationOptions& InInitOptions, TWeakObjectPtr<UWorld> SpecifiedWorld) const
{
	FCreateSceneOutlinerMode ModeFactory = FCreateSceneOutlinerMode::CreateLambda([&SpecifiedWorld](SSceneOutliner* Outliner)
		{
			return new FActorBrowsingMode(Outliner, SpecifiedWorld);
		});

	FSceneOutlinerInitializationOptions InitOptions(InInitOptions);
	InitOptions.ModeFactory = ModeFactory;
	if (InitOptions.ColumnMap.Num() == 0)
	{
		InitOptions.UseDefaultColumns();
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Gutter(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0));
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::ActorInfo(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 20));
	}
	return CreateSceneOutliner(InitOptions);
}

IMPLEMENT_MODULE(FSceneOutlinerModule, SceneOutliner);
