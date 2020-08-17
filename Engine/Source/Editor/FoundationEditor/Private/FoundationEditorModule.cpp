// Copyright Epic Games, Inc. All Rights Reserved.
#include "FoundationEditorModule.h"
#include "FoundationActorDetails.h"
#include "Foundation/FoundationSubsystem.h"
#include "Foundation/FoundationActor.h"
#include "FoundationEditorSettings.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "LevelEditorMenuContext.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "LevelEditor.h"
#include "Engine/Selection.h"
#include "PropertyEditorModule.h"
#include "EditorLevelUtils.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "NewLevelDialogModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "Editor/EditorEngine.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "Widgets/Input/SNumericEntryBox.h"

IMPLEMENT_MODULE( FFoundationEditorModule, FoundationEditor );

#define LOCTEXT_NAMESPACE "FoundationEditor"

namespace FoundationMenuUtils
{
	FToolMenuSection& CreateFoundationSection(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Foundation"));
		Section.Label = LOCTEXT("Foundation", "Foundation");
		return Section;
	}

	void CreateEditSubMenu(UToolMenu* Menu, TArray<AFoundationActor*> FoundationHierarchy, AActor* ContextActor)
	{
		FToolMenuSection& Section = Menu->AddSection(NAME_None, LOCTEXT("FoundationContextEditSection", "Context"));
		for (AFoundationActor* FoundationActor : FoundationHierarchy)
		{
			FToolUIAction FoundationEditAction;
			FText EntryDesc = LOCTEXT("FoundationEditSubMenuEntry","");
			const bool bCanEdit = FoundationActor->CanEdit(&EntryDesc);

			FoundationEditAction.ExecuteAction.BindLambda([FoundationActor, ContextActor](const FToolMenuContext&)
			{
				FoundationActor->Edit(ContextActor);
			});
			FoundationEditAction.CanExecuteAction.BindLambda([bCanEdit](const FToolMenuContext&)
			{ 
				return bCanEdit;
			});

			const FText EntryLabel = FText::Format(LOCTEXT("FoundationName", "{0}:{1}"), FText::FromString(FoundationActor->GetName()), FText::FromString(FoundationActor->GetFoundationPackage()));
			Section.AddMenuEntry(NAME_None, EntryLabel, EntryDesc, FSlateIcon(), FoundationEditAction);
		}
	}

	void CreateCommitSubMenu(UToolMenu* Menu, TArray<AFoundationActor*> FoundationEdits, AFoundationActor* ContextFoundation)
	{
		FText OtherSectionLabel = LOCTEXT("FoundationOtherCommitSection", "Other(s)");
		FToolMenuSection* Section = &Menu->AddSection("FoundationContextCommitSection", ContextFoundation != nullptr? LOCTEXT("FoundationContextCommitSection", "Context") : OtherSectionLabel);
		for (AFoundationActor* FoundationActor : FoundationEdits)
		{
			FText EntryDesc = LOCTEXT("FoundationEditSubMenuEntry", "");
			const bool bCanCommit = FoundationActor->CanCommit(&EntryDesc);

			FToolUIAction FoundationEditAction;
			FoundationEditAction.ExecuteAction.BindLambda([FoundationActor](const FToolMenuContext&)
			{
				FoundationActor->Commit();
			});
			FoundationEditAction.CanExecuteAction.BindLambda([bCanCommit](const FToolMenuContext&)
			{
				return bCanCommit;
			});

			const FText EntryLabel = FText::Format(LOCTEXT("FoundationName", "{0}:{1}"), FText::FromString(FoundationActor->GetName()), FText::FromString(FoundationActor->GetFoundationPackage()));
			Section->AddMenuEntry(NAME_None, EntryLabel, EntryDesc, FSlateIcon(), FoundationEditAction);

			if (ContextFoundation == FoundationActor && FoundationEdits.Num() > 1)
			{
				Section = &Menu->AddSection(FName("FoundationOtherCommitSection"), OtherSectionLabel);
			}
		}
	}

	void CreateSetCurrentSubMenu(UToolMenu* Menu, TArray<AFoundationActor*> FoundationEdits, AFoundationActor* ContextFoundation)
	{
		FText OtherSectionLabel = LOCTEXT("FoundationOtherSetCurrentSection", "Other(s)");
		FToolMenuSection* Section = &Menu->AddSection(FName("FoundationContextSetCurrentSection"), ContextFoundation != nullptr? LOCTEXT("FoundationContextSetCurrentSection", "Context") : OtherSectionLabel);
		for (AFoundationActor* FoundationActor : FoundationEdits)
		{
			FToolUIAction FoundationSetCurrentAction;
			FoundationSetCurrentAction.ExecuteAction.BindLambda([FoundationActor](const FToolMenuContext&)
			{
				FoundationActor->SetCurrent();
			});
		
			const FText EntryLabel = FText::Format(LOCTEXT("FoundationName", "{0}:{1}"), FText::FromString(FoundationActor->GetName()), FText::FromString(FoundationActor->GetFoundationPackage()));
			Section->AddMenuEntry(NAME_None, EntryLabel, TAttribute<FText>(), FSlateIcon(), FoundationSetCurrentAction);

			if (FoundationActor == ContextFoundation && FoundationEdits.Num() > 1)
			{
				Section = &Menu->AddSection(FName("FoundationOtherSetCurrentSection"), OtherSectionLabel);
			}
		}
	}

	void MoveSelectionToFoundation(AFoundationActor* DestinationFoundation)
	{
		if (UFoundationSubsystem* FoundationSubsystem = DestinationFoundation->GetFoundationSubsystem())
		{
			TArray<AActor*> ActorsToMove;
			ActorsToMove.Reserve(GEditor->GetSelectedActorCount());
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					ActorsToMove.Add(Actor);
				}
			}
			
			FoundationSubsystem->MoveActorsTo(DestinationFoundation, ActorsToMove);
		}
	}

	void CreateMoveSelectionToSubMenu(UToolMenu* Menu, TArray<AFoundationActor*> FoundationEdits)
	{
		FToolMenuSection* Section = &Menu->AddSection(NAME_None);
		for (AFoundationActor* FoundationActor : FoundationEdits)
		{
			FToolUIAction FoundationMoveSelectionAction;
			FoundationMoveSelectionAction.ExecuteAction.BindLambda([FoundationActor](const FToolMenuContext&)
			{
				MoveSelectionToFoundation(FoundationActor);
			});

			const FText EntryLabel = FText::Format(LOCTEXT("FoundationName", "{0}:{1}"), FText::FromString(FoundationActor->GetName()), FText::FromString(FoundationActor->GetFoundationPackage()));
			Section->AddMenuEntry(NAME_None, EntryLabel, TAttribute<FText>(), FSlateIcon(), FoundationMoveSelectionAction);
		}
	}

	void CreateEditMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		if (UFoundationSubsystem* FoundationSubsystem = ContextActor->GetWorld()->GetSubsystem<UFoundationSubsystem>())
		{
			TArray<AFoundationActor*> FoundationHierarchy;
			FoundationSubsystem->ForEachFoundationAncestorsAndSelf(ContextActor, [&FoundationHierarchy](AFoundationActor* AncestorFoundation)
			{
				FoundationHierarchy.Add(AncestorFoundation);
				return true;
			});

			if (FoundationHierarchy.Num() > 0)
			{
				FToolMenuSection& Section = CreateFoundationSection(Menu);
				Section.AddSubMenu(
					"EditFoundations",
					LOCTEXT("EditFoundations", "Edit"),
					TAttribute<FText>(),
					FNewToolMenuDelegate::CreateStatic(&CreateEditSubMenu, MoveTemp(FoundationHierarchy), ContextActor)
				);
			}
		}
	}
	
	void CreateCommitMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		AFoundationActor* ContextFoundation = nullptr;
		TArray<AFoundationActor*> FoundationEdits;
		if (ContextActor)
		{
			if (UFoundationSubsystem* FoundationSubsystem = ContextActor->GetWorld()->GetSubsystem<UFoundationSubsystem>())
			{
				FoundationSubsystem->ForEachFoundationAncestorsAndSelf(ContextActor, [&ContextFoundation,&FoundationEdits](AFoundationActor* FoundationActor)
				{
					if (FoundationActor->IsEditing())
					{
						ContextFoundation = FoundationActor;
						FoundationEdits.Add(ContextFoundation);
						return false;
					}
					return true;
				});
			}
		}

		if (UFoundationSubsystem* FoundationSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<UFoundationSubsystem>())
		{
			FoundationSubsystem->ForEachFoundationEdit([&FoundationEdits, ContextFoundation](AFoundationActor* FoundationActor)
			{
				if (ContextFoundation != FoundationActor)
				{
					FoundationEdits.Add(FoundationActor);
				}
				return true;
			});
		}

		if (FoundationEdits.Num() > 0)
		{
			FToolMenuSection& Section = CreateFoundationSection(Menu);
			Section.AddSubMenu(
				"CommitFoundations",
				LOCTEXT("CommitFoundations", "Commit"),
				TAttribute<FText>(),
				FNewToolMenuDelegate::CreateStatic(&CreateCommitSubMenu, MoveTemp(FoundationEdits), ContextFoundation)
			);
		}
	}

	void CreateSetCurrentMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		AFoundationActor* ContextFoundation = nullptr;
		TArray<AFoundationActor*> FoundationEdits;
		if (ContextActor)
		{
			if (UFoundationSubsystem* FoundationSubsystem = ContextActor->GetWorld()->GetSubsystem<UFoundationSubsystem>())
			{
				FoundationSubsystem->ForEachFoundationAncestorsAndSelf(ContextActor, [&ContextFoundation,&FoundationEdits](AFoundationActor* FoundationActor)
				{
					if (FoundationActor->IsEditing())
					{
						if (!FoundationActor->IsCurrent())
						{
							ContextFoundation = FoundationActor;
							FoundationEdits.Add(ContextFoundation);
						}
						return false;
					}
					return true;
				});
			}
		}

		if (UFoundationSubsystem* FoundationSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<UFoundationSubsystem>())
		{
			FoundationSubsystem->ForEachFoundationEdit([&FoundationEdits, ContextFoundation](AFoundationActor* FoundationActor)
			{
				if (ContextFoundation != FoundationActor && !FoundationActor->IsCurrent())
				{
					FoundationEdits.Add(FoundationActor);
				}
				return true;
			});
		}

		if (FoundationEdits.Num() > 0)
		{
			FToolMenuSection& Section = CreateFoundationSection(Menu);
			Section.AddSubMenu(
				"SetCurrentFoundations",
				LOCTEXT("SetCurrentFoundations", "Set Current"),
				TAttribute<FText>(),
				FNewToolMenuDelegate::CreateStatic(&CreateSetCurrentSubMenu, MoveTemp(FoundationEdits), ContextFoundation)
			);
		}
	}

	void CreateMoveSelectionToMenu(UToolMenu* Menu)
	{
		if (GEditor->GetSelectedActorCount() > 0)
		{

			TArray<AFoundationActor*> FoundationEdits;
			if (UFoundationSubsystem* FoundationSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<UFoundationSubsystem>())
			{
				FoundationSubsystem->ForEachFoundationEdit([&FoundationEdits](AFoundationActor* FoundationActor)
				{
					FoundationEdits.Add(FoundationActor);
					return true;
				});
			}

			if (FoundationEdits.Num() > 0)
			{
				FToolMenuSection& Section = CreateFoundationSection(Menu);
				Section.AddSubMenu(
					"MoveSelectionToFoundations",
					LOCTEXT("MoveSelectionToFoundations", "Move Selection to"),
					TAttribute<FText>(),
					FNewToolMenuDelegate::CreateStatic(&CreateMoveSelectionToSubMenu, MoveTemp(FoundationEdits))
				);
			}
		}
	}

	void CreateFoundationFromSelection(UFoundationSubsystem* FoundationSubsystem)
	{
		TArray<AActor*> ActorsToMove;
		ActorsToMove.Reserve(GEditor->GetSelectedActorCount());
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				ActorsToMove.Add(Actor);
			}
		}

		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>("MainFrame");
		FNewLevelDialogModule& NewLevelDialogModule = FModuleManager::LoadModuleChecked<FNewLevelDialogModule>("NewLevelDialog");
		FString TemplateMapPackage;
		if (!GetMutableDefault<UFoundationEditorSettings>()->TemplateMapInfos.Num() || NewLevelDialogModule.CreateAndShowTemplateDialog(MainFrameModule.GetParentWindow(), LOCTEXT("FoundationTemplateDialog", "Choose Foundation Template..."), GetMutableDefault<UFoundationEditorSettings>()->TemplateMapInfos, TemplateMapPackage))
		{
			UPackage* TemplatePackage = !TemplateMapPackage.IsEmpty() ? LoadPackage(nullptr, *TemplateMapPackage, LOAD_None) : nullptr;
			UWorld* TemplateWorld = TemplatePackage ? UWorld::FindWorldInPackage(TemplatePackage) : nullptr;

			if (!FoundationSubsystem->CreateFoundationFrom(ActorsToMove, TemplateWorld))
			{
				FText Title = LOCTEXT("CreateFromSelectionFailTitle", "Create from selection failed");
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CreateFromSelectionFailMsg", "Failed to create foundation from selection. Check log for details."), &Title);
			}
		}
	}

	void CreateCreateMenu(UToolMenu* Menu)
	{
		if (UFoundationSubsystem* FoundationSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<UFoundationSubsystem>())
		{
			FToolMenuSection& Section = CreateFoundationSection(Menu);
			FToolUIAction UIAction;
			UIAction.ExecuteAction.BindLambda([FoundationSubsystem](const FToolMenuContext& MenuContext)
			{
				CreateFoundationFromSelection(FoundationSubsystem);
			});
			UIAction.CanExecuteAction.BindLambda([](const FToolMenuContext& MenuContext)
			{
				return GEditor->GetSelectedActorCount() > 0;
			});

			Section.AddMenuEntry(
				"CreateFoundationFromSelection",
				LOCTEXT("CreateFoundationFromSelection", "Create from selection"),
				TAttribute<FText>(),
				TAttribute<FSlateIcon>(),
				UIAction);
		}
	}

	void CreateSaveAsMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		AFoundationActor* ContextFoundation = nullptr;
		if (ContextActor)
		{
			if (UFoundationSubsystem* FoundationSubsystem = ContextActor->GetWorld()->GetSubsystem<UFoundationSubsystem>())
			{
				FoundationSubsystem->ForEachFoundationAncestorsAndSelf(ContextActor, [&ContextFoundation](AFoundationActor* FoundationActor)
					{
						if (FoundationActor->IsEditing())
						{
							ContextFoundation = FoundationActor;
							return false;
						}
						return true;
					});
			}
		}

		if (ContextFoundation)
		{
			FToolMenuSection& Section = CreateFoundationSection(Menu);
			FText EntryDesc = LOCTEXT("FoundationEditSubMenuEntry", "");
			const bool bCanCommit = ContextFoundation->CanCommit(&EntryDesc);

			FToolUIAction SaveAction;
			SaveAction.ExecuteAction.BindLambda([ContextFoundation](const FToolMenuContext& MenuContext)
				{
					ContextFoundation->SaveAs();
				});
			SaveAction.CanExecuteAction.BindLambda([bCanCommit](const FToolMenuContext& MenuContext)
				{
					return bCanCommit;
				});

			Section.AddMenuEntry(
				"SaveFoundationAs",
				LOCTEXT("SaveFoundationAs", "Save foundation as..."),
				TAttribute<FText>(),
				TAttribute<FSlateIcon>(),
				SaveAction);
		}
	}

	void CreateBreakSubMenu(UToolMenu* Menu, AFoundationActor* ContextFoundation)
	{
		static int32 BreakLevels = 1;

		check(ContextFoundation);

		if (UFoundationSubsystem* FoundationSubsystem = ContextFoundation->GetWorld()->GetSubsystem<UFoundationSubsystem>())
		{
			FToolMenuSection& Section = Menu->AddSection(NAME_None, LOCTEXT("FoundationBreakSection", "Break Foundation"));
			TSharedRef<SWidget> MenuWidget =
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SNew(SNumericEntryBox<int32>)
						.MinValue(1)
						.Value_Lambda([]() { return BreakLevels; })
						.OnValueChanged_Lambda([](int32 InValue) { BreakLevels = InValue; })
						.LabelPadding(0)
						.Label()
						[
							SNumericEntryBox<int32>::BuildLabel(LOCTEXT("BreakLevelsLabel", "Levels"), FLinearColor::White, SNumericEntryBox<int32>::BlueLabelBackgroundColor)
						]
					]
				]

				+SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Padding(0, 5, 0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked_Lambda([ContextFoundation, FoundationSubsystem]() {FoundationSubsystem->BreakFoundation(ContextFoundation, BreakLevels); return FReply::Handled(); })
					.Text(LOCTEXT("BreakFoundations_BreakFoundationButton", "Break Foundation"))
				];

			Section.AddEntry(FToolMenuEntry::InitWidget("SetBreakLevels", MenuWidget, FText::GetEmpty(), false));
		}
	}

	void CreateBreakMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		check(ContextActor);

		if (UFoundationSubsystem* FoundationSubsystem = ContextActor->GetWorld()->GetSubsystem<UFoundationSubsystem>())
		{
			AFoundationActor* ContextFoundation = nullptr;

			// Find the top level foundation
			FoundationSubsystem->ForEachFoundationAncestorsAndSelf(ContextActor, [FoundationSubsystem, ContextActor, &ContextFoundation](AFoundationActor* Ancestor)
				{
					if (Ancestor->GetLevel() == ContextActor->GetWorld()->GetCurrentLevel())
					{
						ContextFoundation = Ancestor;
						return false;
					}
					return true;
				});

			if (ContextFoundation && !ContextFoundation->IsEditing())
			{
				FToolMenuSection& Section = CreateFoundationSection(Menu);

				Section.AddSubMenu(
					"BreakFoundations",
					LOCTEXT("BreakFoundations", "Break..."),
					TAttribute<FText>(),
					FNewToolMenuDelegate::CreateStatic(&CreateBreakSubMenu, ContextFoundation)
				);
			}
		}
	}

	class FFoundationClassFilter : public IClassViewerFilter
	{
	public:
		
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InClass && InClass->IsChildOf(AFoundationActor::StaticClass()) && !InClass->HasAnyClassFlags(CLASS_Deprecated);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(AFoundationActor::StaticClass()) && !InUnloadedClassData->HasAnyClassFlags(CLASS_Deprecated);
		}
	};

	void CreateBlueprintFromWorld(UWorld* WorldAsset)
	{
		TSoftObjectPtr<UWorld> FoundationPtr(WorldAsset);

		int32 LastSlashIndex = 0;
		FString LongPackageName = FoundationPtr.GetLongPackageName();
		LongPackageName.FindLastChar('/', LastSlashIndex);
		
		FString PackagePath = LongPackageName.Mid(0, LastSlashIndex == INDEX_NONE ? MAX_int32 : LastSlashIndex);
		FString AssetName = FoundationPtr.GetAssetName() + "_Foundation";
		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();

		UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
		BlueprintFactory->AddToRoot();
		BlueprintFactory->OnConfigurePropertiesDelegate.BindLambda([](FClassViewerInitializationOptions* Options)
		{
			Options->bShowDefaultClasses = false;
			Options->bIsBlueprintBaseOnly = false;
			Options->InitiallySelectedClass = AFoundationActor::StaticClass();
			Options->bIsActorsOnly = true;
			Options->ClassFilter = MakeShareable(new FFoundationClassFilter);
		});
		ON_SCOPE_EXIT
		{
			BlueprintFactory->OnConfigurePropertiesDelegate.Unbind();
			BlueprintFactory->RemoveFromRoot();
		};

		if (UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAssetWithDialog(AssetName, PackagePath, UBlueprint::StaticClass(), BlueprintFactory, FName("Create Foundation Blueprint"))))
		{
			AFoundationActor* CDO = CastChecked<AFoundationActor>(NewBlueprint->GeneratedClass->GetDefaultObject());
			CDO->SetFoundation(FoundationPtr);
			FBlueprintEditorUtils::MarkBlueprintAsModified(NewBlueprint);
			
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<UObject*> Assets;
			Assets.Add(NewBlueprint);
			ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
		}		
	}

	void CreateBlueprintFromMenu(UToolMenu* Menu, UWorld* WorldAsset)
	{
		FToolMenuSection& Section = CreateFoundationSection(Menu);
		FToolUIAction UIAction;
		UIAction.ExecuteAction.BindLambda([WorldAsset](const FToolMenuContext& MenuContext)
		{
			CreateBlueprintFromWorld(WorldAsset);
		});

		Section.AddMenuEntry(
			"CreateFoundationBlueprint",
			LOCTEXT("CreateFoundationBlueprint", "New Blueprint..."),
			TAttribute<FText>(),
			TAttribute<FSlateIcon>(),
			UIAction);
	}
};

void FFoundationEditorModule::StartupModule()
{
	ExtendContextMenu();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("FoundationActor", FOnGetDetailCustomizationInstance::CreateStatic(&FFoundationActorDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

	// GEditor needs to be set before this module is loaded
	check(GEditor);
	GEditor->OnLevelActorDeleted().AddRaw(this, &FFoundationEditorModule::OnLevelActorDeleted);
	
	EditorLevelUtils::CanMoveActorToLevelDelegate.AddRaw(this, &FFoundationEditorModule::CanMoveActorToLevel);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnMapChanged().AddRaw(this, &FFoundationEditorModule::OnMapChanged);
}

void FFoundationEditorModule::ShutdownModule()
{
	if (GEditor)
	{
		GEditor->OnLevelActorDeleted().RemoveAll(this);
	}

	EditorLevelUtils::CanMoveActorToLevelDelegate.RemoveAll(this);

	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditorModule->OnMapChanged().RemoveAll(this);
	}
}

void FFoundationEditorModule::OnLevelActorDeleted(AActor* Actor)
{
	if (UFoundationSubsystem* FoundationSubsystem = Actor->GetWorld()->GetSubsystem<UFoundationSubsystem>())
	{
		FoundationSubsystem->OnActorDeleted(Actor);
	}
}

void FFoundationEditorModule::OnMapChanged(UWorld* World, EMapChangeType MapChangeType)
{
	// On Map Changed, Users will be asked to save unchanged maps. Once we hit the teardown we need to force
	// Foundation Edits to be cancelled. If they are still dirty it means the user decided not to save changes.
	if (World && MapChangeType == EMapChangeType::TearDownWorld)
	{
		if (UFoundationSubsystem* FoundationSubsystem = World->GetSubsystem<UFoundationSubsystem>())
		{
			FoundationSubsystem->DiscardEdits();
		}
	}
}

void FFoundationEditorModule::CanMoveActorToLevel(const AActor* ActorToMove, const ULevel* DestLevel, bool& bOutCanMove)
{
	if (UWorld* World = ActorToMove->GetWorld())
	{
		if (UFoundationSubsystem* FoundationSubsystem = World->GetSubsystem<UFoundationSubsystem>())
		{
			if (!FoundationSubsystem->CanMoveActorToLevel(ActorToMove))
			{
				bOutCanMove = false;
				return;
			}
		}
	}
}

void FFoundationEditorModule::ExtendContextMenu()
{
	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu"))
	{
		FToolMenuSection& Section = Menu->AddDynamicSection("ActorFoundation", FNewToolMenuDelegate::CreateLambda([this](UToolMenu* ToolMenu)
		{
			if (ToolMenu)
			{
				if (ULevelEditorContextMenuContext* LevelEditorMenuContext = ToolMenu->Context.FindContext<ULevelEditorContextMenuContext>())
				{
					AActor* ContextActor = LevelEditorMenuContext->HitProxyActor;
					if (!ContextActor && GEditor->GetSelectedActorCount() == 1)
					{
						ContextActor = Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
					}

					if (ContextActor)
					{
						FoundationMenuUtils::CreateEditMenu(ToolMenu, ContextActor);
						FoundationMenuUtils::CreateCommitMenu(ToolMenu, ContextActor);
						FoundationMenuUtils::CreateSaveAsMenu(ToolMenu, ContextActor);
						FoundationMenuUtils::CreateBreakMenu(ToolMenu, ContextActor);
					}

					FoundationMenuUtils::CreateSetCurrentMenu(ToolMenu, ContextActor);
					FoundationMenuUtils::CreateMoveSelectionToMenu(ToolMenu);
					FoundationMenuUtils::CreateCreateMenu(ToolMenu);
				}
			}
		}), FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
	}

	if (UToolMenu* WorldAssetMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.World"))
	{
		FToolMenuSection& Section = WorldAssetMenu->AddDynamicSection("ActorFoundation", FNewToolMenuDelegate::CreateLambda([this](UToolMenu* ToolMenu)
		{
			if (ToolMenu)
			{
				if (UContentBrowserAssetContextMenuContext* AssetMenuContext = ToolMenu->Context.FindContext<UContentBrowserAssetContextMenuContext>())
				{
					if (AssetMenuContext->SelectedObjects.Num() != 1)
					{
						return;
					}
					// World is already loaded by the AssetContextMenu code
					if (UWorld* WorldAsset = Cast<UWorld>(AssetMenuContext->SelectedObjects[0].Get()))
					{
						FoundationMenuUtils::CreateBlueprintFromMenu(ToolMenu, WorldAsset);
					}
				}
			}
		}), FToolMenuInsert(NAME_None, EToolMenuInsertType::Default));
	}
	
}

#undef LOCTEXT_NAMESPACE

