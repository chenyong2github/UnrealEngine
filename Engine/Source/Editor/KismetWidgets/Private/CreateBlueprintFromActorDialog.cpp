// Copyright Epic Games, Inc. All Rights Reserved.

#include "CreateBlueprintFromActorDialog.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Editor.h"
#include "SCreateAssetFromObject.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "CreateBlueprintFromActorDialog"

TWeakObjectPtr<AActor> FCreateBlueprintFromActorDialog::ActorOverride = nullptr;

void FCreateBlueprintFromActorDialog::OpenDialog(ECreateBlueprintFromActorMode CreateMode, AActor* InActorOverride )
{
	ActorOverride = InActorOverride;

	TSharedPtr<SWindow> PickBlueprintPathWidget;
	SAssignNew(PickBlueprintPathWidget, SWindow)
		.Title(LOCTEXT("SelectPath", "Select Path"))
		.ToolTipText(LOCTEXT("SelectPathTooltip", "Select the path where the Blueprint will be created at"))
		.ClientSize(FVector2D(400, 400));

	TSharedPtr<SCreateAssetFromObject> CreateBlueprintFromActorDialog;
	PickBlueprintPathWidget->SetContent
	(
		SAssignNew(CreateBlueprintFromActorDialog, SCreateAssetFromObject, PickBlueprintPathWidget)
		.AssetFilenameSuffix(TEXT("Blueprint"))
		.HeadingText(LOCTEXT("CreateBlueprintFromActor_Heading", "Blueprint Name"))
		.CreateButtonText(LOCTEXT("CreateBlueprintFromActor_ButtonLabel", "Create Blueprint"))
		.OnCreateAssetAction(FOnPathChosen::CreateStatic(FCreateBlueprintFromActorDialog::OnCreateBlueprint, CreateMode))
	);

	TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
	if (RootWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(PickBlueprintPathWidget.ToSharedRef(), RootWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(PickBlueprintPathWidget.ToSharedRef());
	}
}

void FCreateBlueprintFromActorDialog::OnCreateBlueprint(const FString& InAssetPath, ECreateBlueprintFromActorMode CreateMode)
{
	UBlueprint* Blueprint = NULL;

	switch (CreateMode) 
	{
		case ECreateBlueprintFromActorMode::Harvest:
		{
			TArray<AActor*> Actors;

			USelection* SelectedActors = GEditor->GetSelectedActors();
			for(FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
			{
				// We only care about actors that are referenced in the world for literals, and also in the same level as this blueprint
				if (AActor* Actor = Cast<AActor>(*Iter))
				{
					Actors.Add(Actor);
				}
			}

			const bool bReplaceActor = true;
			Blueprint = FKismetEditorUtilities::HarvestBlueprintFromActors(InAssetPath, Actors, bReplaceActor);
		}
		break;

		case ECreateBlueprintFromActorMode::Subclass:
		{
			AActor* ActorToUse = ActorOverride.Get();

			if (!ActorToUse)
			{
				TArray< UObject* > SelectedActors;
				GEditor->GetSelectedActors()->GetSelectedObjects(AActor::StaticClass(), SelectedActors);
				check(SelectedActors.Num() == 1);
				ActorToUse = Cast<AActor>(SelectedActors[0]);
			}

			const bool bReplaceActor = true;
			Blueprint = FKismetEditorUtilities::CreateBlueprintFromActor(InAssetPath, ActorToUse, bReplaceActor);
		}
		break;
	}

	if(Blueprint)
	{
		// Select the newly created blueprint in the content browser, but don't activate the browser
		TArray<UObject*> Objects;
		Objects.Add(Blueprint);
		GEditor->SyncBrowserToObjects( Objects, false );
	}
	else
	{
		FNotificationInfo Info( LOCTEXT("CreateBlueprintFromActorFailed", "Unable to create a blueprint from actor.") );
		Info.ExpireDuration = 3.0f;
		Info.bUseLargeFont = false;
		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if ( Notification.IsValid() )
		{
			Notification->SetCompletionState( SNotificationItem::CS_Fail );
		}
	}
}


#undef LOCTEXT_NAMESPACE
