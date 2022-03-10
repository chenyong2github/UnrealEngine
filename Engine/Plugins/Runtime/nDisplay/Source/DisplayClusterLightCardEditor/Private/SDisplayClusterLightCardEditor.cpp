// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDisplayClusterLightCardEditor.h"

#include "SDisplayClusterLightCardList.h"

#include "IDisplayClusterOperator.h"
#include "Viewport/DisplayClusterLightcardEditorViewport.h"

#include "DisplayClusterRootActor.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Viewport/DisplayClusterLightCardEditorViewportClient.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterLightCardEditor"

const FName SDisplayClusterLightCardEditor::TabName = TEXT("DisplayClusterLightCardEditorTab");

void SDisplayClusterLightCardEditor::RegisterTabSpawner()
{
	IDisplayClusterOperator::Get().OnRegisterLayoutExtensions().AddStatic(&SDisplayClusterLightCardEditor::RegisterLayoutExtension);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&SDisplayClusterLightCardEditor::SpawnInTab))
		.SetDisplayName(LOCTEXT("TabDisplayName", "Light Cards Editor"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Editing tools for nDisplay light cards."));

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("General", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateStatic(&SDisplayClusterLightCardEditor::ExtendToolbar));
	IDisplayClusterOperator::Get().GetOperatorToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void SDisplayClusterLightCardEditor::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
}

void SDisplayClusterLightCardEditor::RegisterLayoutExtension(FLayoutExtender& InExtender)
{
	FTabManager::FTab NewTab(FTabId(TabName, ETabIdFlags::SaveLayout), ETabState::OpenedTab);
	InExtender.ExtendStack(IDisplayClusterOperator::Get().GetOperatorExtensionId(), ELayoutExtensionPosition::After, NewTab);
}

TSharedRef<SDockTab> SDisplayClusterLightCardEditor::SpawnInTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	MajorTab->SetContent(SNew(SDisplayClusterLightCardEditor, MajorTab, SpawnTabArgs.GetOwnerWindow()));

	return MajorTab;
}

void SDisplayClusterLightCardEditor::ExtendToolbar(FToolBarBuilder& ToolbarBuilder)
{
	// TODO: Any toolbar buttons needed for the lightcards editor can be added to the operator panel's toolbar using this toolbar extender
}

SDisplayClusterLightCardEditor::~SDisplayClusterLightCardEditor()
{
	IDisplayClusterOperator::Get().OnActiveRootActorChanged().Remove(ActiveRootActorChangedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);

	if (GEngine != nullptr)
	{
		GEngine->OnLevelActorDeleted().RemoveAll(this);
	}

	RemoveCompileDelegates();
}

void SDisplayClusterLightCardEditor::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& MajorTabOwner, const TSharedPtr<SWindow>& WindowOwner)
{
	ActiveRootActorChangedHandle = IDisplayClusterOperator::Get().OnActiveRootActorChanged().AddSP(this, &SDisplayClusterLightCardEditor::OnActiveRootActorChanged);
	if (GEngine != nullptr)
	{
		GEngine->OnLevelActorDeleted().AddSP(this, &SDisplayClusterLightCardEditor::OnLevelActorDeleted);
	}

	ChildSlot                     
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		
		+SSplitter::Slot()
		.Value(0.25f)
		[
			// Vertical box for the left hand panel of the editor. Add new slots here as needed for any editor UI controls
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreateLightCardListWidget()
			]
		]
		+SSplitter::Slot()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(0.75f)
			[
				CreateViewportWidget()
			]
		]
	];

	BindCompileDelegates();
}

void SDisplayClusterLightCardEditor::OnActiveRootActorChanged(ADisplayClusterRootActor* NewRootActor)
{
	RemoveCompileDelegates();
	
	// The new root actor pointer could be null, indicating that it was deleted or the user didn't select a valid root actor
	ActiveRootActor = NewRootActor;
	LightCardList->SetRootActor(NewRootActor);
	ViewportView->SetRootActor(NewRootActor);
	
	BindCompileDelegates();

	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SDisplayClusterLightCardEditor::OnActorPropertyChanged);
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::CreateLightCardListWidget()
{
	return SAssignNew(LightCardList, SDisplayClusterLightCardList);
}

TSharedRef<SWidget> SDisplayClusterLightCardEditor::CreateViewportWidget()
{
	return SAssignNew(ViewportView, SDisplayClusterLightCardEditorViewport, SharedThis(this));
}

void SDisplayClusterLightCardEditor::RefreshPreviewActors()
{
	RemoveCompileDelegates();
	
	if (ADisplayClusterRootActor* RootActor = GetActiveRootActor().Get())
	{
		if (LightCardList.IsValid())
		{
			LightCardList->SetRootActor(RootActor);
		}
			
		if (ViewportView.IsValid())
		{
			const bool bForce = true;
			ViewportView->GetLightCardEditorViewportClient()->UpdatePreviewActor(RootActor, bForce);
		}
	}
	
	BindCompileDelegates();
}

void SDisplayClusterLightCardEditor::BindCompileDelegates()
{
	if (LightCardList.IsValid())
	{
		for (const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCardActor : LightCardList->GetLightCardActors())
		{
			if (LightCardActor.IsValid() && LightCardActor->LightCardActor.IsValid())
			{
				if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(LightCardActor->LightCardActor->GetClass()))
				{
					Blueprint->OnCompiled().AddSP(this, &SDisplayClusterLightCardEditor::OnBlueprintCompiled);
				}
			}
		}
	}
}

void SDisplayClusterLightCardEditor::RemoveCompileDelegates()
{
	if (LightCardList.IsValid())
	{
		for (const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCardActor : LightCardList->GetLightCardActors())
		{
			if (LightCardActor.IsValid() && LightCardActor->LightCardActor.IsValid())
			{
				if (UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(LightCardActor->LightCardActor->GetClass()))
				{
					Blueprint->OnCompiled().RemoveAll(this);
				}
			}
		}
	}
}

void SDisplayClusterLightCardEditor::OnActorPropertyChanged(UObject* ObjectBeingModified,
                                                            FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	auto IsOurActor = [ObjectBeingModified] (UObject* ObjectToCompare) -> bool
	{
		if (ObjectToCompare)
		{
			if (ObjectBeingModified == ObjectToCompare)
			{
				return true;
			}

			if (const UObject* RootActorOuter = ObjectBeingModified->GetTypedOuter(ObjectToCompare->GetClass()))
			{
				return RootActorOuter == ObjectToCompare;
			}
		}

		return false;
	};
	
	bool bIsOurActor = IsOurActor(GetActiveRootActor().Get());
	if (!bIsOurActor && LightCardList.IsValid())
	{
		for (const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCard : LightCardList->GetLightCardActors())
		{
			bIsOurActor = IsOurActor(LightCard->LightCardActor.Get());
			if (bIsOurActor)
			{
				break;
			}
		}
	}
	
	if (bIsOurActor)
	{
		RefreshPreviewActors();
	}
}

void SDisplayClusterLightCardEditor::OnLevelActorDeleted(AActor* Actor)
{
	if (LightCardList.IsValid() &&
		LightCardList->GetLightCardActors().ContainsByPredicate([Actor](const TSharedPtr<SDisplayClusterLightCardList::FLightCardTreeItem>& LightCardTreeItem)
	{
		return LightCardTreeItem.IsValid() && Actor == LightCardTreeItem->LightCardActor.Get();
	}))
	{
		if (Actor && Actor->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			// When a blueprint class is regenerated instances are deleted and replaced.
			// In this case the OnCompiled() delegate will fire and refresh the actor.
			return;
		}
		
		if (ViewportView.IsValid())
		{
			ViewportView->GetLightCardEditorViewportClient()->GetWorld()->GetTimerManager().SetTimerForNextTick([=]()
			{
				// Schedule for next tick so available selections are properly updated once the
				// actor is fully deleted.
				RefreshPreviewActors();
			});
		}
	}
}

void SDisplayClusterLightCardEditor::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	RefreshPreviewActors();
}

#undef LOCTEXT_NAMESPACE
