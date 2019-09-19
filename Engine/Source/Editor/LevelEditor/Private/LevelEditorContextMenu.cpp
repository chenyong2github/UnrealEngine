// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelEditorContextMenu.h"
#include "Misc/Attribute.h"
#include "Styling/SlateColor.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "Misc/Paths.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Engine/Selection.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "SLevelEditor.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Editor/GroupActor.h"
#include "LevelEditorViewport.h"
#include "EditorModes.h"
#include "EditorModeInterpolation.h"
#include "LevelEditor.h"
#include "Matinee/MatineeActor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetSelection.h"
#include "LevelEditorActions.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerModule.h"
#include "Kismet2/DebuggerCommands.h"
#include "Styling/SlateIconFinder.h"
#include "EditorViewportCommands.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "LevelEditorCreateActorMenu.h"
#include "SourceCodeNavigation.h"
#include "EditorClassUtils.h"
#include "Framework/Commands/GenericCommands.h"
#include "LevelViewportActions.h"
#include "ActorGroupingUtils.h"

#define LOCTEXT_NAMESPACE "LevelViewportContextMenu"

DEFINE_LOG_CATEGORY_STATIC(LogViewportMenu, Log, All);

class FLevelEditorContextMenuImpl
{
public:
	static FSelectedActorInfo SelectionInfo;
public:
	/**
	 * Fills in menu options for the select actor menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillSelectActorMenu(UToolMenu* Menu);

	/**
	 * Fills in menu options for the actor visibility menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillActorVisibilityMenu(UToolMenu* Menu);

	/**
	 * Fills in menu options for the actor level menu
	 *
	 * @param SharedLevel			The level shared between all selected actors.  If any actors are in a different level, this is NULL
	 * @param bAllInCurrentLevel	true if all selected actors are in the current level
	 * @param MenuBuilder			The menu to add items to
	 */
	static void FillActorLevelMenu(UToolMenu* Menu);

	/**
	 * Fills in menu options for the transform menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillTransformMenu(UToolMenu* Menu);

	/**
	 * Fills in menu options for the Fill Actor menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillActorMenu(UToolMenu* Menu);

	/**
	 * Fills in menu options for the snap menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillSnapAlignMenu(UToolMenu* Menu);

	/**
	 * Fills in menu options for the pivot menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillPivotMenu(UToolMenu* Menu);
	
	/**
	 * Fills in menu options for the group menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillGroupMenu( UToolMenu* Menu );

	/**
	 * Fills in menu options for the edit menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 * @param ContextType	The context for this editor menu
	 */
	static void FillEditMenu(UToolMenu* Menu);

private:
	/**
	 * Fills in menu options for the matinee selection menu
	 *
	 * @param MenuBuilder	The menu to add items to
	 */
	static void FillMatineeSelectActorMenu(UToolMenu* Menu);
};

FSelectedActorInfo FLevelEditorContextMenuImpl::SelectionInfo;

struct FLevelScriptEventMenuHelper
{
	/**
	* Fills in menu options for events that can be associated with that actors's blueprint in the level script blueprint
	*
	* @param MenuBuilder	The menu to add items to
	*/
	static void FillLevelBlueprintEventsMenu(UToolMenu* Menu, const TArray<AActor*>& SelectedActors);
};

void FLevelEditorContextMenu::RegisterComponentContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered("LevelEditor.ComponentContextMenu"))
	{
		return;
	}

	UToolMenu* Menu = ToolMenus->RegisterMenu("LevelEditor.ComponentContextMenu");
	Menu->AddDynamicSection("ComponentControlDynamic", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
	{
		ULevelEditorContextMenuContext* LevelEditorContext = InMenu->FindContext<ULevelEditorContextMenuContext>();
		if (!LevelEditorContext)
		{
			return;
		}

		TArray<UActorComponent*> SelectedComponents;
		for (FSelectedEditableComponentIterator It(GEditor->GetSelectedEditableComponentIterator()); It; ++It)
		{
			SelectedComponents.Add(CastChecked<UActorComponent>(*It));
		}

		{
			FToolMenuSection& Section = InMenu->AddSection("ComponentControl", LOCTEXT("ComponentControlHeading", "Component"));

			AActor* OwnerActor = GEditor->GetSelectedActors()->GetTop<AActor>();
			if(OwnerActor)
			{
				Section.AddMenuEntry(
					FLevelEditorCommands::Get().SelectComponentOwnerActor,
					FText::Format(LOCTEXT("SelectComponentOwner", "Select Owner [{0}]"), FText::FromString(OwnerActor->GetHumanReadableName())),
					TAttribute<FText>(),
					FSlateIconFinder::FindIconForClass(OwnerActor->GetClass())
				);
			}

			Section.AddMenuEntry(FEditorViewportCommands::Get().FocusViewportToSelection);

			const FVector* ClickLocation = &GEditor->ClickLocation;
			FUIAction GoHereAction;
			GoHereAction.ExecuteAction = FExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::GoHere_Clicked, ClickLocation);

			Section.AddMenuEntry(FLevelEditorCommands::Get().GoHere);
			Section.AddMenuEntry(FLevelEditorCommands::Get().SnapCameraToObject);
			Section.AddMenuEntry(FLevelEditorCommands::Get().SnapObjectToCamera);
		}

		FComponentEditorUtils::FillComponentContextMenuOptions(InMenu, SelectedComponents);
	}));
}

void FLevelEditorContextMenu::RegisterActorContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered("LevelEditor.ActorContextMenu"))
	{
		return;
	}

	UToolMenu* Menu = ToolMenus->RegisterMenu("LevelEditor.ActorContextMenu");
	Menu->AddDynamicSection("ActorContextMenuDynamic", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
	{
		ULevelEditorContextMenuContext* LevelEditorContext = InMenu->FindContext<ULevelEditorContextMenuContext>();
		if (!LevelEditorContext || !LevelEditorContext->LevelEditor.IsValid())
		{
			return;
		}

		TWeakPtr<SLevelEditor> LevelEditor = LevelEditorContext->LevelEditor;

		// Generate information about our selection
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

		FSelectedActorInfo& SelectionInfo = FLevelEditorContextMenuImpl::SelectionInfo;
		SelectionInfo = AssetSelectionUtils::BuildSelectedActorInfo(SelectedActors);

		// Check if current selection has any assets that can be browsed to
		TArray< UObject* > ReferencedAssets;
		GEditor->GetReferencedAssetsForEditorSelection(ReferencedAssets);

		const bool bCanSyncToContentBrowser = GEditor->CanSyncToContentBrowser();

		if (bCanSyncToContentBrowser || ReferencedAssets.Num() > 0)
		{
			{
				FToolMenuSection& Section = InMenu->AddSection("ActorAsset", LOCTEXT("AssetHeading", "Asset"));
				if (bCanSyncToContentBrowser)
				{
					Section.AddMenuEntry(FGlobalEditorCommonCommands::Get().FindInContentBrowser);
				}

				if (ReferencedAssets.Num() == 1)
				{
					auto Asset = ReferencedAssets[0];

					Section.AddMenuEntry(
						FLevelEditorCommands::Get().EditAsset,
						FText::Format(LOCTEXT("EditAssociatedAsset", "Edit {0}"), FText::FromString(Asset->GetName())),
						TAttribute<FText>(),
						FSlateIconFinder::FindIconForClass(Asset->GetClass())
						);
				}
				else if (ReferencedAssets.Num() > 1)
				{
					Section.AddMenuEntry(
						FLevelEditorCommands::Get().EditAssetNoConfirmMultiple,
						LOCTEXT("EditAssociatedAssetsMultiple", "Edit Multiple Assets"),
						TAttribute<FText>(),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Default")
						);

				}
			}
		}


		{
			FToolMenuSection& Section = InMenu->AddSection("ActorControl", LOCTEXT("ActorHeading", "Actor"));
			Section.AddMenuEntry(FEditorViewportCommands::Get().FocusViewportToSelection);


			const FVector* ClickLocation = &GEditor->ClickLocation;

			FUIAction GoHereAction;
			GoHereAction.ExecuteAction = FExecuteAction::CreateStatic(&FLevelEditorActionCallbacks::GoHere_Clicked, ClickLocation);

			Section.AddMenuEntry(FLevelEditorCommands::Get().GoHere);
			Section.AddMenuEntry(FLevelEditorCommands::Get().SnapCameraToObject);
			Section.AddMenuEntry(FLevelEditorCommands::Get().SnapObjectToCamera);

			if (SelectedActors.Num() == 1)
			{
				const FLevelViewportCommands& Actions = FLevelViewportCommands::Get();

				auto Viewport = LevelEditor.Pin()->GetActiveViewport();
				if (Viewport.IsValid())
				{
					auto& ViewportClient = Viewport->GetLevelViewportClient();

					if (ViewportClient.IsPerspective() && !ViewportClient.IsLockedToMatinee())
					{
						if (Viewport->IsSelectedActorLocked())
						{
							Section.AddMenuEntry(
								Actions.EjectActorPilot,
								FText::Format(LOCTEXT("PilotActor_Stop", "Stop piloting '{0}'"), FText::FromString(SelectedActors[0]->GetActorLabel()))
								);
						}
						else
						{
							Section.AddMenuEntry(
								Actions.PilotSelectedActor,
								FText::Format(LOCTEXT("PilotActor", "Pilot '{0}'"), FText::FromString(SelectedActors[0]->GetActorLabel()))
								);
						}
					}
				}
			}
		}

		// Go to C++ Code
		if (SelectionInfo.SelectionClass != NULL)
		{
			if (FSourceCodeNavigation::IsCompilerAvailable())
			{
				FString ClassHeaderPath;
				if (FSourceCodeNavigation::FindClassHeaderPath(SelectionInfo.SelectionClass, ClassHeaderPath) && IFileManager::Get().FileSize(*ClassHeaderPath) != INDEX_NONE)
				{
					const FString CodeFileName = FPaths::GetCleanFilename(*ClassHeaderPath);

					FToolMenuSection& Section = InMenu->AddSection("ActorCode", LOCTEXT("ActorCodeHeading", "C++"));
					{
						Section.AddMenuEntry(FLevelEditorCommands::Get().GoToCodeForActor,
							FText::Format(LOCTEXT("GoToCodeForActor", "Open {0}"), FText::FromString(CodeFileName)),
							FText::Format(LOCTEXT("GoToCodeForActor_ToolTip", "Opens the header file for this actor ({0}) in a code editing program"), FText::FromString(CodeFileName)));
					}
				}
			}

			const FString DocumentationLink = FEditorClassUtils::GetDocumentationLink(SelectionInfo.SelectionClass);
			if (!DocumentationLink.IsEmpty())
			{
				FToolMenuSection& Section = InMenu->AddSection("ActorDocumentation", LOCTEXT("ActorDocsHeading", "Documentation"));
				{
					Section.AddMenuEntry(FLevelEditorCommands::Get().GoToDocsForActor,
						LOCTEXT("GoToDocsForActor", "View Documentation"),
						LOCTEXT("GoToDocsForActor_ToolTip", "Click to open documentation for this actor"),
						FSlateIcon(FEditorStyle::GetStyleSetName(), "HelpIcon.Hovered"));
				}
			}
		}

		{
			FToolMenuSection& Section = InMenu->AddSection("ActorSelectVisibilityLevels");

			// Add a sub-menu for "Select"
			Section.AddSubMenu(
				"SelectSubMenu",
				LOCTEXT("SelectSubMenu", "Select"),
				LOCTEXT("SelectSubMenu_ToolTip", "Opens the actor selection menu"),
				FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillSelectActorMenu));

			Section.AddSubMenu(
				"EditSubMenu",
				LOCTEXT("EditSubMenu", "Edit"),
				FText::GetEmpty(),
				FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillEditMenu));

			Section.AddSubMenu(
				"VisibilitySubMenu",
				LOCTEXT("VisibilitySubMenu", "Visibility"),
				LOCTEXT("VisibilitySubMenu_ToolTip", "Selected actor visibility options"),
				FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillActorVisibilityMenu));

			// Build the menu for grouping actors
			BuildGroupMenu(InMenu, SelectionInfo);

			Section.AddSubMenu(
				"LevelSubMenu",
				LOCTEXT("LevelSubMenu", "Level"),
				LOCTEXT("LevelSubMenu_ToolTip", "Options for interacting with this actor's level"),
				FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillActorLevelMenu));
		}

		if (LevelEditorContext->ContextType == ELevelEditorMenuContext::Viewport)
		{
			LevelEditorCreateActorMenu::FillAddReplaceViewportContextMenuSections(InMenu);

			FToolMenuSection& Section = InMenu->AddSection("OpenMergeActor");
			Section.AddMenuEntry(FLevelEditorCommands::Get().OpenMergeActor,
					LOCTEXT("OpenMergeActor", "Merge Actors"),
					LOCTEXT("OpenMergeActor_ToolTip", "Click to open the Merge Actor panel"));
		}

		if (GEditor->PlayWorld != NULL)
		{
			if (SelectionInfo.NumSelected > 0)
			{
				FToolMenuSection& Section = InMenu->AddSection("Simulation", NSLOCTEXT("LevelViewportContextMenu", "SimulationHeading", "Simulation"));
				{
					Section.AddMenuEntry(FLevelEditorCommands::Get().KeepSimulationChanges);
				}
			}
		}

		{
			FToolMenuSection& Section = InMenu->AddSection("LevelViewportAttach");

			// Only display the attach menu if we have actors selected
			if (GEditor->GetSelectedActorCount())
			{
				if (SelectionInfo.bHaveAttachedActor)
				{
					Section.AddMenuEntry(FLevelEditorCommands::Get().DetachFromParent);
				}

				Section.AddSubMenu(
					"ActorAttachToSubMenu",
					LOCTEXT("ActorAttachToSubMenu", "Attach To"),
					LOCTEXT("ActorAttachToSubMenu_ToolTip", "Attach Actor as child"),
					FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillActorMenu));
			}

			// Add a heading for "Movement" if an actor is selected
			if (GEditor->GetSelectedActorIterator())
			{
				// Add a sub-menu for "Transform"
				Section.AddSubMenu(
					"TransformSubMenu",
					LOCTEXT("TransformSubMenu", "Transform"),
					LOCTEXT("TransformSubMenu_ToolTip", "Actor transform utils"),
					FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillTransformMenu));
			}

			// Add a sub-menu for "Pivot"
			Section.AddSubMenu(
				"PivotSubMenu",
				LOCTEXT("PivotSubMenu", "Pivot"),
				LOCTEXT("PivotSubMenu_ToolTip", "Actor pivoting utils"),
				FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillPivotMenu));
		}

		FLevelScriptEventMenuHelper::FillLevelBlueprintEventsMenu(InMenu, SelectedActors);
	}));
}

void FLevelEditorContextMenu::RegisterSceneOutlinerContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered("LevelEditor.SceneOutlinerContextMenu"))
	{
		return;
	}

	UToolMenu* Menu = ToolMenus->RegisterMenu("LevelEditor.SceneOutlinerContextMenu");
	Menu->AddDynamicSection("SelectVisibilityLevels", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
	{
		if (ULevelEditorContextMenuContext* LevelEditorContext = InMenu->FindContext<ULevelEditorContextMenuContext>())
		{
			TWeakPtr<ISceneOutliner> SceneOutlinerPtr = LevelEditorContext->LevelEditor.Pin()->GetSceneOutliner();
			if (SceneOutlinerPtr.IsValid())
			{
				FToolMenuSection& Section = InMenu->AddSection("SelectVisibilityLevels");
				Section.AddSubMenu(
					"EditSubMenu",
					LOCTEXT("EditSubMenu", "Edit"),
					FText::GetEmpty(),
					FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillEditMenu)
				);
			}
		}
	}));
}

FName FLevelEditorContextMenu::GetContextMenuName(ELevelEditorMenuContext ContextType)
{
	if (GEditor->GetSelectedComponentCount() > 0)
	{
		return "LevelEditor.ComponentContextMenu";
	}
	else if (GEditor->GetSelectedActorCount() > 0)
	{
		return "LevelEditor.ActorContextMenu";
	}
	else if (ContextType == ELevelEditorMenuContext::SceneOutliner)
	{
		return "LevelEditor.SceneOutlinerContextMenu";
	}

	return NAME_None;
}

FName FLevelEditorContextMenu::InitMenuContext(FToolMenuContext& Context, TWeakPtr<SLevelEditor> LevelEditor, ELevelEditorMenuContext ContextType)
{
	RegisterComponentContextMenu();
	RegisterActorContextMenu();
	RegisterSceneOutlinerContextMenu();

	TSharedPtr<FUICommandList> LevelEditorActionsList = LevelEditor.Pin()->GetLevelEditorActions();
	Context.AppendCommandList(LevelEditorActionsList);

	ULevelEditorContextMenuContext* ContextObject = NewObject<ULevelEditorContextMenuContext>();
	ContextObject->LevelEditor = LevelEditor;
	ContextObject->ContextType = ContextType;
	for (FSelectedEditableComponentIterator It(GEditor->GetSelectedEditableComponentIterator()); It; ++It)
	{
		ContextObject->SelectedComponents.Add(CastChecked<UActorComponent>(*It));
	}
	Context.AddObject(ContextObject);

	if (GEditor->GetSelectedComponentCount() == 0 && GEditor->GetSelectedActorCount() > 0)
	{
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

		// Get all menu extenders for this context menu from the level editor module
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors> MenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
		TArray<TSharedPtr<FExtender>> Extenders;
		for (int32 i = 0; i < MenuExtenderDelegates.Num(); ++i)
		{
			if (MenuExtenderDelegates[i].IsBound())
			{
				Extenders.Add(MenuExtenderDelegates[i].Execute(LevelEditorActionsList.ToSharedRef(), SelectedActors));
			}
		}

		if (Extenders.Num() > 0)
		{
			Context.AddExtender(FExtender::Combine(Extenders));
		}
	}

	return GetContextMenuName(ContextType);
}

UToolMenu* FLevelEditorContextMenu::GenerateMenu(TWeakPtr<SLevelEditor> LevelEditor, ELevelEditorMenuContext ContextType, TSharedPtr<FExtender> Extender)
{
	FToolMenuContext Context;
	if (Extender.IsValid())
	{
		Context.AddExtender(Extender);
	}

	FName ContextMenuName = InitMenuContext(Context, LevelEditor, ContextType);
	return UToolMenus::Get()->GenerateMenu(ContextMenuName, Context);
}

// NOTE: We intentionally receive a WEAK pointer here because we want to be callable by a delegate whose
//       payload contains a weak reference to a level editor instance
TSharedPtr< SWidget > FLevelEditorContextMenu::BuildMenuWidget(TWeakPtr< SLevelEditor > LevelEditor, ELevelEditorMenuContext ContextType, TSharedPtr<FExtender> Extender)
{
	UToolMenu* Menu = GenerateMenu(LevelEditor, ContextType, Extender);
	return UToolMenus::Get()->GenerateWidget(Menu);
}

namespace EViewOptionType
{
	enum Type
	{
		Top,
		Bottom,
		Left,
		Right,
		Front,
		Back,
		Perspective
	};
}

TSharedPtr<SWidget> MakeViewOptionWidget(const TSharedRef< SLevelEditor >& LevelEditor, bool bShouldCloseWindowAfterMenuSelection, EViewOptionType::Type ViewOptionType)
{
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, LevelEditor->GetActiveViewport()->GetCommandList());

	if (ViewOptionType == EViewOptionType::Top)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Top);
	}
	else if (ViewOptionType == EViewOptionType::Bottom)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Bottom);
	}
	else if (ViewOptionType == EViewOptionType::Left)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Left);
	}
	else if (ViewOptionType == EViewOptionType::Right)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Right);
	}
	else if (ViewOptionType == EViewOptionType::Front)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Front);
	}
	else if (ViewOptionType == EViewOptionType::Back)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Back);
	}
	else if (ViewOptionType == EViewOptionType::Perspective)
	{
		MenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Perspective);
	}
	else
	{
		return nullptr;
	}
	return MenuBuilder.MakeWidget();
}

void BuildViewOptionMenu(const TSharedRef< SLevelEditor >& LevelEditor, TSharedPtr<SWidget> InWidget, const FVector2D WidgetPosition)
{
	if (InWidget.IsValid())
	{
		FSlateApplication::Get().PushMenu(
			LevelEditor->GetActiveViewport().ToSharedRef(),
			FWidgetPath(),
			InWidget.ToSharedRef(),
			WidgetPosition,
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}
}

void FLevelEditorContextMenu::SummonViewOptionMenu( const TSharedRef< SLevelEditor >& LevelEditor, const ELevelViewportType ViewOption )
{
	const FVector2D MouseCursorLocation = FSlateApplication::Get().GetCursorPos();

	bool bShouldCloseWindowAfterMenuSelection = true;
	EViewOptionType::Type ViewOptionType = EViewOptionType::Perspective;

	switch (ViewOption)
	{
		case LVT_OrthoNegativeXY:
			ViewOptionType = EViewOptionType::Bottom;
			break;
		case LVT_OrthoNegativeXZ:
			ViewOptionType = EViewOptionType::Back;
			break;
		case LVT_OrthoNegativeYZ:
			ViewOptionType = EViewOptionType::Right;
			break;
		case LVT_OrthoXY:
			ViewOptionType = EViewOptionType::Top;
			break;
		case LVT_OrthoXZ:
			ViewOptionType = EViewOptionType::Front;
			break;
		case LVT_OrthoYZ:
			ViewOptionType = EViewOptionType::Left;
			break;
		case LVT_Perspective:
			ViewOptionType = EViewOptionType::Perspective;
			break;
	};
	// Build up menu
	BuildViewOptionMenu(LevelEditor, MakeViewOptionWidget(LevelEditor, bShouldCloseWindowAfterMenuSelection, ViewOptionType), MouseCursorLocation);
}

void FLevelEditorContextMenu::SummonMenu(const TSharedRef< SLevelEditor >& LevelEditor, ELevelEditorMenuContext ContextType)
{
	struct Local
	{
		static void ExtendMenu( FMenuBuilder& MenuBuilder )
		{
			// one extra entry when summoning the menu this way
			MenuBuilder.BeginSection("ActorPreview", LOCTEXT("PreviewHeading", "Preview") );
			{
				// Note: not using a command for play from here since it requires a mouse click
				FUIAction PlayFromHereAction( 
					FExecuteAction::CreateStatic( &FPlayWorldCommandCallbacks::StartPlayFromHere ) );

				const FText PlayFromHereLabel = GEditor->OnlyLoadEditorVisibleLevelsInPIE() ? LOCTEXT("PlayFromHereVisible", "Play From Here (visible levels)") : LOCTEXT("PlayFromHere", "Play From Here");
				MenuBuilder.AddMenuEntry( PlayFromHereLabel, LOCTEXT("PlayFromHere_ToolTip", "Starts a game preview from the clicked location"),FSlateIcon(), PlayFromHereAction );
			}
			MenuBuilder.EndSection();
		}
	};
	
	TSharedRef<FExtender> Extender = MakeShareable(new FExtender);
	Extender->AddMenuExtension("LevelViewportAttach", EExtensionHook::After, TSharedPtr< FUICommandList >(), FMenuExtensionDelegate::CreateStatic(&Local::ExtendMenu));

	// Create the context menu!
	TSharedPtr<SWidget> MenuWidget = BuildMenuWidget( LevelEditor, ContextType, Extender );
	if ( MenuWidget.IsValid() )
	{
		// @todo: Should actually use the location from a click event instead!
		const FVector2D MouseCursorLocation = FSlateApplication::Get().GetCursorPos();
	
		FSlateApplication::Get().PushMenu(
			LevelEditor->GetActiveViewport().ToSharedRef(),
			FWidgetPath(),
			MenuWidget.ToSharedRef(),
			MouseCursorLocation,
			FPopupTransitionEffect( FPopupTransitionEffect::ContextMenu ) );
	}
}

FSlateColor InvertOnHover( const TWeakPtr< SWidget > WidgetPtr )
{
	TSharedPtr< SWidget > Widget = WidgetPtr.Pin();
	if ( Widget.IsValid() && Widget->IsHovered() )
	{
		static const FName InvertedForegroundName("InvertedForeground");
		return FEditorStyle::GetSlateColor(InvertedForegroundName);
	}

	return FSlateColor::UseForeground();
}

void FLevelEditorContextMenu::BuildGroupMenu(UToolMenu* Menu, const FSelectedActorInfo& SelectedActorInfo)
{
	if( UActorGroupingUtils::IsGroupingActive() )
	{
		FToolMenuSection& Section = Menu->AddSection("GroupMenu");

		// Whether or not we added a grouping sub-menu
		bool bNeedGroupSubMenu = SelectedActorInfo.bHaveSelectedLockedGroup || SelectedActorInfo.bHaveSelectedUnlockedGroup;

		// Grouping based on selection (must have selected at least two actors)
		if( SelectedActorInfo.NumSelected > 1 )
		{
			if( !SelectedActorInfo.bHaveSelectedLockedGroup && !SelectedActorInfo.bHaveSelectedUnlockedGroup )
			{
				// Only one menu entry needed so dont use a sub-menu
				Section.AddMenuEntry(FLevelEditorCommands::Get().RegroupActors, FLevelEditorCommands::Get().GroupActors->GetLabel(), FLevelEditorCommands::Get().GroupActors->GetDescription());
			}
			else
			{
				// Put everything into a sub-menu
				bNeedGroupSubMenu = true;
			}
		}
		
		if( bNeedGroupSubMenu )
		{
			Section.AddSubMenu(
				"GroupMenu",
				LOCTEXT("GroupMenu", "Groups"),
				LOCTEXT("GroupMenu_ToolTip", "Opens the actor grouping menu"),
				FNewToolMenuDelegate::CreateStatic( &FLevelEditorContextMenuImpl::FillGroupMenu));
		}
	}
}

void FLevelEditorContextMenuImpl::FillSelectActorMenu(UToolMenu* Menu)
{
	FText SelectAllActorStr = FText::Format( LOCTEXT("SelectActorsOfSameClass", "Select All {0}(s)"), FText::FromString( SelectionInfo.SelectionStr ) );
	int32 NumSelectedSurfaces = AssetSelectionUtils::GetNumSelectedSurfaces( SelectionInfo.SharedWorld );

	{
		FToolMenuSection& Section = Menu->AddSection("SelectActorGeneral", LOCTEXT("SelectAnyHeading", "General"));
		Section.AddMenuEntry( FGenericCommands::Get().SelectAll, TAttribute<FText>(), LOCTEXT("SelectAll_ToolTip", "Selects all actors") );
		Section.AddMenuEntry( FLevelEditorCommands::Get().SelectNone );
		Section.AddMenuEntry( FLevelEditorCommands::Get().InvertSelection );
	}

	if( !SelectionInfo.bHaveBrush && SelectionInfo.bAllSelectedActorsOfSameType && SelectionInfo.SelectionStr.Len() != 0 )
	{
		// These menu options appear if only if all the actors are the same type and we aren't selecting brush
		FToolMenuSection& Section = Menu->AddSection("SelectAllActorsOfSameClass");
		Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllActorsOfSameClass, SelectAllActorStr);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("SelectActorHierarchy", LOCTEXT("SelectHierarchyHeading", "Hierarchy"));
		Section.AddMenuEntry( FLevelEditorCommands::Get().SelectImmediateChildren );
		Section.AddMenuEntry( FLevelEditorCommands::Get().SelectAllDescendants );
	}

	// Add brush commands when we have a brush or any surfaces selected
	{
		FToolMenuSection& Section = Menu->AddSection("SelectBSP", LOCTEXT("SelectBSPHeading", "BSP"));
		if( SelectionInfo.bHaveBrush || NumSelectedSurfaces > 0 )
		{
			if( SelectionInfo.bAllSelectedAreBrushes )
			{
				Section.AddMenuEntry( FLevelEditorCommands::Get().SelectAllActorsOfSameClass, SelectAllActorStr);
			}
		}

		Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllAddditiveBrushes);
		Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllSubtractiveBrushes);
		Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllSurfaces);
	}

	if( SelectionInfo.NumSelected > 0 || NumSelectedSurfaces > 0 )
	{
		// If any actors are selected add lights selection options
		{
			FToolMenuSection& Section = Menu->AddSection("SelectLights", LOCTEXT("SelectLightHeading", "Lights"));
			Section.AddMenuEntry(FLevelEditorCommands::Get().SelectRelevantLights);

			if ( SelectionInfo.bHaveLight )
			{
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllLights);
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectStationaryLightsExceedingOverlap);
			}
		}

		if( SelectionInfo.bHaveStaticMesh )
		{
			// if any static meshes are selected allow selecting actors using the same mesh
			{
				FToolMenuSection& Section = Menu->AddSection("SelectMeshes", LOCTEXT("SelectStaticMeshHeading", "Static Meshes"));
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectStaticMeshesOfSameClass, LOCTEXT("SelectStaticMeshesOfSameClass_Menu", "Select Matching (Selected Classes)"));
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectStaticMeshesAllClasses, LOCTEXT("SelectStaticMeshesAllClasses_Menu", "Select Matching (All Classes)"));
			}

			if (SelectionInfo.NumSelected == 1)
			{
				{
					FToolMenuSection& Section = Menu->AddSection("SelectHLODCluster", LOCTEXT("SelectHLODClusterHeading", "Hierachical LODs"));
					Section.AddMenuEntry(FLevelEditorCommands::Get().SelectOwningHierarchicalLODCluster, LOCTEXT("SelectOwningHierarchicalLODCluster_Menu", "Select Owning HierarchicalLODCluster"));
				}
			}			
		}

		if( SelectionInfo.bHavePawn || SelectionInfo.bHaveSkeletalMesh )
		{
			// if any skeletal meshes are selected allow selecting actors using the same mesh
			{
				FToolMenuSection& Section = Menu->AddSection("SelectSkeletalMeshes", LOCTEXT("SelectSkeletalMeshHeading", "Skeletal Meshes"));
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectSkeletalMeshesOfSameClass);
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectSkeletalMeshesAllClasses);
			}
		}

		if( SelectionInfo.bHaveEmitter )
		{
			{
				FToolMenuSection& Section = Menu->AddSection("SelectEmitters", LOCTEXT("SelectEmitterHeading", "Emitters"));
				Section.AddMenuEntry(FLevelEditorCommands::Get().SelectMatchingEmitter);
			}
		}
	}

	if( SelectionInfo.bHaveBrush || SelectionInfo.NumSelected > 0 )
	{
		FToolMenuSection& Section = Menu->AddSection("SelectMaterial", LOCTEXT("SelectMaterialHeading", "Materials"));
		{
			Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllWithSameMaterial);
		}
	}

	// Add geometry collection commands
	if (FModuleManager::Get().IsModuleLoaded("GeometryCollectionEditor"))
	{
		FToolMenuSection& Section = Menu->AddSection("SelectBones", LOCTEXT("GeometryCollectionHeading", "Geometry Collection"));
		{
			Section.AddMenuEntry(FLevelEditorCommands::Get().GeometryCollectionSelectAllGeometry);
			Section.AddMenuEntry(FLevelEditorCommands::Get().GeometryCollectionSelectNone);
			Section.AddMenuEntry(FLevelEditorCommands::Get().GeometryCollectionSelectInverseGeometry);
		}
	}

	// build matinee related selection menu
	FillMatineeSelectActorMenu( Menu );
}

void FLevelEditorContextMenuImpl::FillMatineeSelectActorMenu(UToolMenu* Menu)
{
	{
		FToolMenuSection& Section = Menu->AddSection("SelectMatinee", LOCTEXT("SelectMatineeHeading", "Matinee"));
		// show list of Matinee Actors that controls this actor
		// this is ugly but we don't have good way of knowing which Matinee actor controls me
		// in the future this can be cached to TMap somewhere and use that list
		// for now we show only when 1 actor is selected
		if ( SelectionInfo.SharedLevel && SelectionInfo.NumSelected == 1 )
		{
			TArray<AMatineeActor*> MatineeActors;	
			// first collect all matinee actors
			for ( AActor* Actor : SelectionInfo.SharedLevel->Actors )
			{
				AMatineeActor * CurActor = Cast<AMatineeActor>(Actor);
				if ( CurActor )
				{
					MatineeActors.Add(CurActor);
				}
			}

			if ( MatineeActors.Num() > 0 )
			{
				FSelectionIterator ActorIter( GEditor->GetSelectedActorIterator() );
				AActor* SelectedActor = Cast<AActor>(*ActorIter);

				// now delete the matinee actors that don't control currently selected actor
				for (int32 MatineeActorIter=0; MatineeActorIter<MatineeActors.Num(); ++MatineeActorIter)
				{
					AMatineeActor * CurMatineeActor = MatineeActors[MatineeActorIter];
					TArray<AActor *> CutMatineeControlledActors;
					CurMatineeActor->GetControlledActors(CutMatineeControlledActors);
					bool bIsMatineeControlled=false;
					for ( AActor* ControlledActor : CutMatineeControlledActors )
					{
						if (ControlledActor == SelectedActor)
						{
							bIsMatineeControlled = true;
						}
					}

					// if not, remove it
					if (!bIsMatineeControlled)
					{
						MatineeActors.RemoveAt(MatineeActorIter);
						--MatineeActorIter;
					}
				}

				// if some matinee controls this, add to menu for direct selection
				if ( MatineeActors.Num() > 0 )
				{
					for (int32 MatineeActorIter=0; MatineeActorIter<MatineeActors.Num(); ++MatineeActorIter)
					{
						AMatineeActor * CurMatineeActor = MatineeActors[MatineeActorIter];
						const FText Text = FText::Format( LOCTEXT("SelectMatineeActor", "Select {0}"), FText::FromString( CurMatineeActor->GetName() ) );

						FUIAction CurMatineeActorAction( FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::OnSelectMatineeActor, CurMatineeActor ) );
						Section.AddMenuEntry(NAME_None, Text, Text, FSlateIcon(), CurMatineeActorAction);

						// if matinee is opened, and if that is CurMatineeActor, show option to go to group
						if( GLevelEditorModeTools().IsModeActive( FBuiltinEditorModes::EM_InterpEdit ) )
						{
							const FEdModeInterpEdit* InterpEditMode = (const FEdModeInterpEdit*)GLevelEditorModeTools().GetActiveMode( FBuiltinEditorModes::EM_InterpEdit );

							if ( InterpEditMode && InterpEditMode->MatineeActor == CurMatineeActor )
							{
								FUIAction SelectedActorAction( FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::OnSelectMatineeGroup, SelectedActor ) );
								Section.AddMenuEntry(NAME_None, LOCTEXT("SelectMatineeGroupForActorMenuTitle", "Select Matinee Group For This Actor"), LOCTEXT("SelectMatineeGroupForActorMenuTooltip", "Selects matinee group controlling this actor"), FSlateIcon(), SelectedActorAction);
							}
						}
					}
				}
			}
		}

		// if this class is Matinee Actor, add option to allow select all controlled actors
		if ( SelectionInfo.bHaveMatinee )
		{
			Section.AddMenuEntry(FLevelEditorCommands::Get().SelectAllActorsControlledByMatinee);	
		}
	}
}

void FLevelEditorContextMenuImpl::FillActorVisibilityMenu(UToolMenu* Menu)
{
	{
		FToolMenuSection& Section = Menu->AddSection("VisibilitySelected");
		// Show 'Show Selected' only if the selection has any hidden actors
		if ( SelectionInfo.bHaveHidden )
		{
			Section.AddMenuEntry(FLevelEditorCommands::Get().ShowSelected);
		}
		Section.AddMenuEntry(FLevelEditorCommands::Get().HideSelected);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("VisibilityAll");
		Section.AddMenuEntry(FLevelEditorCommands::Get().ShowSelectedOnly);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ShowAll);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("VisibilityStartup");
		Section.AddMenuEntry(FLevelEditorCommands::Get().ShowAllStartup);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ShowSelectedStartup);
		Section.AddMenuEntry(FLevelEditorCommands::Get().HideSelectedStartup);
	}
}

void FLevelEditorContextMenuImpl::FillActorLevelMenu(UToolMenu* Menu)
{
	{
		FToolMenuSection& Section = Menu->AddSection("ActorLevel", LOCTEXT("ActorLevel", "Actor Level"));
		if( SelectionInfo.SharedLevel && SelectionInfo.SharedWorld && SelectionInfo.SharedWorld->GetCurrentLevel() != SelectionInfo.SharedLevel )
		{
			// All actors are in the same level and that level is not the current level 
			// so add a menu entry to make the shared level current

			FText MakeCurrentLevelText = FText::Format( LOCTEXT("MakeCurrentLevelMenu", "Make Current Level: {0}"), FText::FromString( SelectionInfo.SharedLevel->GetOutermost()->GetName() ) );
			Section.AddMenuEntry(FLevelEditorCommands::Get().MakeActorLevelCurrent, MakeCurrentLevelText);
		}

		if( !SelectionInfo.bAllSelectedActorsBelongToCurrentLevel )
		{
			// Only show this menu entry if any actors are not in the current level
			Section.AddMenuEntry(FLevelEditorCommands::Get().MoveSelectedToCurrentLevel);
		}

		Section.AddMenuEntry(FLevelEditorCommands::Get().FindActorLevelInContentBrowser);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelBlueprint", LOCTEXT("LevelBlueprint", "Level Blueprint"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().FindActorInLevelScript);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelBrowser", LOCTEXT("LevelBrowser", "Level Browser"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().FindLevelsInLevelBrowser);
		Section.AddMenuEntry(FLevelEditorCommands::Get().AddLevelsToSelection);
		Section.AddMenuEntry(FLevelEditorCommands::Get().RemoveLevelsFromSelection);
	}
}


void FLevelEditorContextMenuImpl::FillTransformMenu(UToolMenu* Menu)
{
	if ( FLevelEditorActionCallbacks::ActorSelected_CanExecute() )
	{
		{
			FToolMenuSection& Section = Menu->AddSection("TransformSnapAlign");
			Section.AddSubMenu(
				"SnapAlignSubMenu",
				LOCTEXT("SnapAlignSubMenu", "Snap/Align"), 
				LOCTEXT("SnapAlignSubMenu_ToolTip", "Actor snap/align utils"),
				FNewToolMenuDelegate::CreateStatic(&FLevelEditorContextMenuImpl::FillSnapAlignMenu));
		}

		{
			FToolMenuSection& Section = Menu->AddSection("DeltaTransformToActors");
			Section.AddMenuEntry(FLevelEditorCommands::Get().DeltaTransformToActors);
		}
	}

	{
		FToolMenuSection& Section = Menu->AddSection("MirrorLock");
		Section.AddMenuEntry(FLevelEditorCommands::Get().MirrorActorX);
		Section.AddMenuEntry(FLevelEditorCommands::Get().MirrorActorY);
		Section.AddMenuEntry(FLevelEditorCommands::Get().MirrorActorZ);
		Section.AddMenuEntry(FLevelEditorCommands::Get().LockActorMovement);
	}
}

void FLevelEditorContextMenuImpl::FillActorMenu(UToolMenu* Menu)
{
	struct Local
	{
		static FReply OnInteractiveActorPickerClicked()
		{
			FSlateApplication::Get().DismissAllMenus();
			FLevelEditorActionCallbacks::AttachActorIteractive();
			return FReply::Handled();
		}
	};

	SceneOutliner::FInitializationOptions InitOptions;
	{
		InitOptions.Mode = ESceneOutlinerMode::ActorPicker;			
		InitOptions.bShowHeaderRow = false;
		InitOptions.bFocusSearchBoxWhenOpened = true;

		// Only display Actors that we can attach too
		InitOptions.Filters->AddFilterPredicate( SceneOutliner::FActorFilterPredicate::CreateStatic( &FLevelEditorActionCallbacks::IsAttachableActor) );
	}		

	FToolMenuSection& Section = Menu->AddSection("Actor");
	if(SelectionInfo.bHaveAttachedActor)
	{
		Section.AddMenuEntry(FLevelEditorCommands::Get().DetachFromParent, LOCTEXT("None", "None"));
	}

	// Actor selector to allow the user to choose a parent actor
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>( "SceneOutliner" );

	TSharedRef< SWidget > MenuWidget = 
		SNew(SHorizontalBox)

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.MaxHeight(400.0f)
			[
				SceneOutlinerModule.CreateSceneOutliner(
					InitOptions,
					FOnActorPicked::CreateStatic( &FLevelEditorActionCallbacks::AttachToActor )
					)
			]
		]
	
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Top)
		.AutoWidth()
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText( LOCTEXT( "PickButtonLabel", "Pick a parent actor to attach to") )
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(FOnClicked::CreateStatic(&Local::OnInteractiveActorPickerClicked))
				.ContentPadding(4.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_PickActorInteractive"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];

	Section.AddEntry(FToolMenuEntry::InitWidget("PickParentActor", MenuWidget, FText::GetEmpty(), false));
}

void FLevelEditorContextMenuImpl::FillSnapAlignMenu(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->AddSection("SnapAlign");
	Section.AddMenuEntry( FLevelEditorCommands::Get().SnapOriginToGrid );
	Section.AddMenuEntry( FLevelEditorCommands::Get().SnapOriginToGridPerActor );
	Section.AddMenuEntry( FLevelEditorCommands::Get().AlignOriginToGrid );
	Section.AddMenuEntry( FLevelEditorCommands::Get().SnapTo2DLayer );
	Section.AddMenuEntry( FLevelEditorCommands::Get().SnapToFloor );
	Section.AddMenuEntry( FLevelEditorCommands::Get().AlignToFloor );
	Section.AddMenuEntry( FLevelEditorCommands::Get().SnapPivotToFloor );
	Section.AddMenuEntry( FLevelEditorCommands::Get().AlignPivotToFloor );
	Section.AddMenuEntry( FLevelEditorCommands::Get().SnapBottomCenterBoundsToFloor );
	Section.AddMenuEntry( FLevelEditorCommands::Get().AlignBottomCenterBoundsToFloor );
/*
	Section.AddSeparator();
	AActor* Actor = GEditor->GetSelectedActors()->GetBottom<AActor>();
	if( Actor && FLevelEditorActionCallbacks::ActorsSelected_CanExecute())
	{
		const FString Label = Actor->GetActorLabel();	// Update the options to show the actors label
		
		TSharedPtr< FUICommandInfo > SnapOriginToActor = FLevelEditorCommands::Get().SnapOriginToActor;
		TSharedPtr< FUICommandInfo > AlignOriginToActor = FLevelEditorCommands::Get().AlignOriginToActor;
		TSharedPtr< FUICommandInfo > SnapToActor = FLevelEditorCommands::Get().SnapToActor;
		TSharedPtr< FUICommandInfo > AlignToActor = FLevelEditorCommands::Get().AlignToActor;
		TSharedPtr< FUICommandInfo > SnapPivotToActor = FLevelEditorCommands::Get().SnapPivotToActor;
		TSharedPtr< FUICommandInfo > AlignPivotToActor = FLevelEditorCommands::Get().AlignPivotToActor;
		TSharedPtr< FUICommandInfo > SnapBottomCenterBoundsToActor = FLevelEditorCommands::Get().SnapBottomCenterBoundsToActor;
		TSharedPtr< FUICommandInfo > AlignBottomCenterBoundsToActor = FLevelEditorCommands::Get().AlignBottomCenterBoundsToActor;

		SnapOriginToActor->Label = FString::Printf( *LOCTEXT("Snap Origin To", "Snap Origin to %s"), *Label);
		AlignOriginToActor->Label = FString::Printf( *LOCTEXT("Align Origin To", "Align Origin to %s"), *Label);
		SnapToActor->Label = FString::Printf( *LOCTEXT("Snap To", "Snap to %s"), *Label);
		AlignToActor->Label = FString::Printf( *LOCTEXT("Align To", "Align to %s"), *Label);
		SnapPivotToActor->Label = FString::Printf( *LOCTEXT("Snap Pivot To", "Snap Pivot to %s"), *Label);
		AlignPivotToActor->Label = FString::Printf( *LOCTEXT("Align Pivot To", "Align Pivot to %s"), *Label);
		SnapBottomCenterBoundsToActor->Label = FString::Printf( *LOCTEXT("Snap Bottom Center Bounds To", "Snap Bottom Center Bounds to %s"), *Label);
		AlignBottomCenterBoundsToActor->Label = FString::Printf( *LOCTEXT("Align Bottom Center Bounds To", "Align Bottom Center Bounds to %s"), *Label);

		Section.AddMenuEntry( SnapOriginToActor );
		Section.AddMenuEntry( AlignOriginToActor );
		Section.AddMenuEntry( SnapToActor );
		Section.AddMenuEntry( AlignToActor );
		Section.AddMenuEntry( SnapPivotToActor );
		Section.AddMenuEntry( AlignPivotToActor );
		Section.AddMenuEntry( SnapBottomCenterBoundsToActor );
		Section.AddMenuEntry( AlignBottomCenterBoundsToActor );
	}
	else
	{
		Section.AddMenuEntry( FLevelEditorCommands::Get().SnapOriginToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().AlignOriginToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().SnapToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().AlignToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().SnapPivotToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().AlignPivotToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().SnapBottomCenterBoundsToActor );
		Section.AddMenuEntry( FLevelEditorCommands::Get().AlignBottomCenterBoundsToActor );
	}
*/
}

void FLevelEditorContextMenuImpl::FillPivotMenu( UToolMenu* Menu )
{
	{
		FToolMenuSection& Section = Menu->AddSection("SaveResetPivot");
		Section.AddMenuEntry(FLevelEditorCommands::Get().SavePivotToPrePivot);
		Section.AddMenuEntry(FLevelEditorCommands::Get().ResetPrePivot);
		Section.AddMenuEntry(FLevelEditorCommands::Get().MovePivotHere);
		Section.AddMenuEntry(FLevelEditorCommands::Get().MovePivotHereSnapped);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("MovePivot");
		Section.AddMenuEntry(FLevelEditorCommands::Get().MovePivotToCenter);
	}
}

void FLevelEditorContextMenuImpl::FillGroupMenu( UToolMenu* Menu )
{
	FToolMenuSection& Section = Menu->AddSection("Group");

	if( SelectionInfo.NumSelectedUngroupedActors > 1 )
	{
		// Only show this menu item if we have more than one actor.
		Section.AddMenuEntry( FLevelEditorCommands::Get().GroupActors );
	}

	if( SelectionInfo.bHaveSelectedLockedGroup || SelectionInfo.bHaveSelectedUnlockedGroup )
	{
		const int32 NumActiveGroups = AGroupActor::NumActiveGroups(true);

		// Regroup will clear any existing groups and create a new one from the selection
		// Only allow regrouping if multiple groups are selected, or a group and ungrouped actors are selected
		if( NumActiveGroups > 1 || (NumActiveGroups && SelectionInfo.NumSelectedUngroupedActors) )
		{
			Section.AddMenuEntry( FLevelEditorCommands::Get().RegroupActors );
		}

		Section.AddMenuEntry( FLevelEditorCommands::Get().UngroupActors );

		if( SelectionInfo.bHaveSelectedUnlockedGroup )
		{
			// Only allow removal of loose actors or locked subgroups
			if( !SelectionInfo.bHaveSelectedLockedGroup || ( SelectionInfo.bHaveSelectedLockedGroup && SelectionInfo.bHaveSelectedSubGroup ) )
			{
				Section.AddMenuEntry( FLevelEditorCommands::Get().RemoveActorsFromGroup );
			}
			Section.AddMenuEntry( FLevelEditorCommands::Get().LockGroup );
		}

		if( SelectionInfo.bHaveSelectedLockedGroup )
		{
			Section.AddMenuEntry( FLevelEditorCommands::Get().UnlockGroup );
		}

		// Only allow group adds if a single group is selected in addition to ungrouped actors
		if( AGroupActor::NumActiveGroups(true, false) == 1 && SelectionInfo.NumSelectedUngroupedActors )
		{ 
			Section.AddMenuEntry( FLevelEditorCommands::Get().AddActorsToGroup );
		}
	}
}

void FLevelEditorContextMenuImpl::FillEditMenu( UToolMenu* Menu )
{
	FToolMenuSection& Section = Menu->AddSection("Section");

	Section.AddMenuEntry( FGenericCommands::Get().Cut );
	Section.AddMenuEntry( FGenericCommands::Get().Copy );
	Section.AddMenuEntry( FGenericCommands::Get().Paste );
	if (ULevelEditorContextMenuContext* LevelEditorContext = Menu->FindContext<ULevelEditorContextMenuContext>())
	{
		if (LevelEditorContext->ContextType == ELevelEditorMenuContext::Viewport)
		{
			Section.AddMenuEntry(FLevelEditorCommands::Get().PasteHere);
		}
	}

	Section.AddMenuEntry( FGenericCommands::Get().Duplicate );
	Section.AddMenuEntry( FGenericCommands::Get().Delete );
	Section.AddMenuEntry( FGenericCommands::Get().Rename );
}

void FLevelScriptEventMenuHelper::FillLevelBlueprintEventsMenu(UToolMenu* Menu, const TArray<AActor*>& SelectedActors)
{
	AActor* SelectedActor = (1 == SelectedActors.Num()) ? SelectedActors[0] : NULL;

	if (FKismetEditorUtilities::IsActorValidForLevelScript(SelectedActor))
	{
		const bool bAnyEventExists = FKismetEditorUtilities::AnyBoundLevelScriptEventForActor(SelectedActor, false);
		const bool bAnyEventCanBeAdded = FKismetEditorUtilities::AnyBoundLevelScriptEventForActor(SelectedActor, true);

		if (bAnyEventExists || bAnyEventCanBeAdded)
		{
			TWeakObjectPtr<AActor> ActorPtr(SelectedActor);

			FToolMenuSection& Section = Menu->AddSection("LevelBlueprintEvents", LOCTEXT("LevelBlueprintEvents", "Level Blueprint Events"));

			if (bAnyEventExists)
			{
				Section.AddSubMenu(
					"JumpEventSubMenu",
					LOCTEXT("JumpEventSubMenu", "Jump to Event"),
					FText::GetEmpty(),
					FNewToolMenuDelegate::CreateStatic(&FKismetEditorUtilities::AddLevelScriptEventOptionsForActor
					, ActorPtr
					, true
					, false
					, true));
			}

			if (bAnyEventCanBeAdded)
			{
				Section.AddSubMenu(
					"AddEventSubMenu",
					LOCTEXT("AddEventSubMenu", "Add Event"),
					FText::GetEmpty(),
					FNewToolMenuDelegate::CreateStatic(&FKismetEditorUtilities::AddLevelScriptEventOptionsForActor
					, ActorPtr
					, false
					, true
					, true));
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
