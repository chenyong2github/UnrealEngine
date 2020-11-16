// Copyright Epic Games, Inc. All Rights Reserved.
#include "LevelInstanceEditorModule.h"
#include "LevelInstanceActorDetails.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"
#include "LevelInstance/Packed/PackedLevelInstanceActor.h"
#include "LevelInstanceEditorSettings.h"
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
#include "Widgets/Input/SButton.h"
#include "MessageLogModule.h"

IMPLEMENT_MODULE( FLevelInstanceEditorModule, LevelInstanceEditor );

#define LOCTEXT_NAMESPACE "LevelInstanceEditor"

namespace LevelInstanceMenuUtils
{
	FToolMenuSection& CreateLevelInstanceSection(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("LevelInstance"));
		Section.Label = LOCTEXT("LevelInstance", "Level Instance");
		return Section;
	}

	void CreateEditSubMenu(UToolMenu* Menu, TArray<ALevelInstance*> LevelInstanceHierarchy, AActor* ContextActor)
	{
		FToolMenuSection& Section = Menu->AddSection(NAME_None, LOCTEXT("LevelInstanceContextEditSection", "Context"));
		for (ALevelInstance* LevelInstanceActor : LevelInstanceHierarchy)
		{
			FToolUIAction LevelInstanceEditAction;
			FText EntryDesc = LOCTEXT("LevelInstanceEditSubMenuEntry","");
			const bool bCanEdit = LevelInstanceActor->CanEdit(&EntryDesc);

			LevelInstanceEditAction.ExecuteAction.BindLambda([LevelInstanceActor, ContextActor](const FToolMenuContext&)
			{
				LevelInstanceActor->Edit(ContextActor);
			});
			LevelInstanceEditAction.CanExecuteAction.BindLambda([bCanEdit](const FToolMenuContext&)
			{ 
				return bCanEdit;
			});

			const FText EntryLabel = FText::Format(LOCTEXT("LevelInstanceName", "{0}:{1}"), FText::FromString(LevelInstanceActor->GetName()), FText::FromString(LevelInstanceActor->GetWorldAssetPackage()));
			Section.AddMenuEntry(NAME_None, EntryLabel, EntryDesc, FSlateIcon(), LevelInstanceEditAction);
		}
	}

	void CreateCommitSubMenu(UToolMenu* Menu, TArray<ALevelInstance*> LevelInstanceEdits, ALevelInstance* ContextLevelInstance)
	{
		FText OtherSectionLabel = LOCTEXT("LevelInstanceOtherCommitSection", "Other(s)");
		FToolMenuSection* Section = &Menu->AddSection("LevelInstanceContextCommitSection", ContextLevelInstance != nullptr? LOCTEXT("LevelInstanceContextCommitSection", "Context") : OtherSectionLabel);
		for (ALevelInstance* LevelInstanceActor : LevelInstanceEdits)
		{
			FText EntryDesc = LOCTEXT("LevelInstanceEditSubMenuEntry", "");
			const bool bCanCommit = LevelInstanceActor->CanCommit(&EntryDesc);

			FToolUIAction LevelInstanceEditAction;
			LevelInstanceEditAction.ExecuteAction.BindLambda([LevelInstanceActor](const FToolMenuContext&)
			{
				LevelInstanceActor->Commit();
			});
			LevelInstanceEditAction.CanExecuteAction.BindLambda([bCanCommit](const FToolMenuContext&)
			{
				return bCanCommit;
			});

			const FText EntryLabel = FText::Format(LOCTEXT("LevelInstanceName", "{0}:{1}"), FText::FromString(LevelInstanceActor->GetName()), FText::FromString(LevelInstanceActor->GetWorldAssetPackage()));
			Section->AddMenuEntry(NAME_None, EntryLabel, EntryDesc, FSlateIcon(), LevelInstanceEditAction);

			if (ContextLevelInstance == LevelInstanceActor && LevelInstanceEdits.Num() > 1)
			{
				Section = &Menu->AddSection(FName("LevelInstanceOtherCommitSection"), OtherSectionLabel);
			}
		}
	}

	void CreateSetCurrentSubMenu(UToolMenu* Menu, TArray<ALevelInstance*> LevelInstanceEdits, ALevelInstance* ContextLevelInstance)
	{
		FText OtherSectionLabel = LOCTEXT("LevelInstanceOtherSetCurrentSection", "Other(s)");
		FToolMenuSection* Section = &Menu->AddSection(FName("LevelInstanceContextSetCurrentSection"), ContextLevelInstance != nullptr? LOCTEXT("LevelInstanceContextSetCurrentSection", "Context") : OtherSectionLabel);
		for (ALevelInstance* LevelInstanceActor : LevelInstanceEdits)
		{
			FToolUIAction LevelInstanceSetCurrentAction;
			LevelInstanceSetCurrentAction.ExecuteAction.BindLambda([LevelInstanceActor](const FToolMenuContext&)
			{
				LevelInstanceActor->SetCurrent();
			});
		
			const FText EntryLabel = FText::Format(LOCTEXT("LevelInstanceName", "{0}:{1}"), FText::FromString(LevelInstanceActor->GetName()), FText::FromString(LevelInstanceActor->GetWorldAssetPackage()));
			Section->AddMenuEntry(NAME_None, EntryLabel, TAttribute<FText>(), FSlateIcon(), LevelInstanceSetCurrentAction);

			if (LevelInstanceActor == ContextLevelInstance && LevelInstanceEdits.Num() > 1)
			{
				Section = &Menu->AddSection(FName("LevelInstanceOtherSetCurrentSection"), OtherSectionLabel);
			}
		}
	}

	void MoveSelectionToLevelInstance(ALevelInstance* DestinationLevelInstance)
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = DestinationLevelInstance->GetLevelInstanceSubsystem())
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
			
			LevelInstanceSubsystem->MoveActorsTo(DestinationLevelInstance, ActorsToMove);
		}
	}

	void CreateMoveSelectionToSubMenu(UToolMenu* Menu, TArray<ALevelInstance*> LevelInstanceEdits)
	{
		FToolMenuSection* Section = &Menu->AddSection(NAME_None);
		for (ALevelInstance* LevelInstanceActor : LevelInstanceEdits)
		{
			FToolUIAction LevelInstanceMoveSelectionAction;
			LevelInstanceMoveSelectionAction.ExecuteAction.BindLambda([LevelInstanceActor](const FToolMenuContext&)
			{
				MoveSelectionToLevelInstance(LevelInstanceActor);
			});

			const FText EntryLabel = FText::Format(LOCTEXT("LevelInstanceName", "{0}:{1}"), FText::FromString(LevelInstanceActor->GetName()), FText::FromString(LevelInstanceActor->GetWorldAssetPackage()));
			Section->AddMenuEntry(NAME_None, EntryLabel, TAttribute<FText>(), FSlateIcon(), LevelInstanceMoveSelectionAction);
		}
	}

	void CreateEditMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextActor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			TArray<ALevelInstance*> LevelInstanceHierarchy;
			LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(ContextActor, [&LevelInstanceHierarchy](ALevelInstance* AncestorLevelInstance)
			{
				LevelInstanceHierarchy.Add(AncestorLevelInstance);
				return true;
			});

			if (LevelInstanceHierarchy.Num() > 0)
			{
				FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
				Section.AddSubMenu(
					"EditLevelInstances",
					LOCTEXT("EditLevelInstances", "Edit"),
					TAttribute<FText>(),
					FNewToolMenuDelegate::CreateStatic(&CreateEditSubMenu, MoveTemp(LevelInstanceHierarchy), ContextActor)
				);
			}
		}
	}
	
	void CreateCommitMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		ALevelInstance* ContextLevelInstance = nullptr;
		TArray<ALevelInstance*> LevelInstanceEdits;
		if (ContextActor)
		{
			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextActor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
			{
				LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(ContextActor, [&ContextLevelInstance,&LevelInstanceEdits](ALevelInstance* LevelInstanceActor)
				{
					if (LevelInstanceActor->IsEditing())
					{
						ContextLevelInstance = LevelInstanceActor;
						LevelInstanceEdits.Add(ContextLevelInstance);
						return false;
					}
					return true;
				});
			}
		}

		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			LevelInstanceSubsystem->ForEachLevelInstanceEdit([&LevelInstanceEdits, ContextLevelInstance](ALevelInstance* LevelInstanceActor)
			{
				if (ContextLevelInstance != LevelInstanceActor)
				{
					LevelInstanceEdits.Add(LevelInstanceActor);
				}
				return true;
			});
		}

		if (LevelInstanceEdits.Num() > 0)
		{
			FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
			Section.AddSubMenu(
				"CommitLevelInstances",
				LOCTEXT("CommitLevelInstances", "Commit"),
				TAttribute<FText>(),
				FNewToolMenuDelegate::CreateStatic(&CreateCommitSubMenu, MoveTemp(LevelInstanceEdits), ContextLevelInstance)
			);
		}
	}

	void CreateSetCurrentMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		ALevelInstance* ContextLevelInstance = nullptr;
		TArray<ALevelInstance*> LevelInstanceEdits;
		if (ContextActor)
		{
			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextActor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
			{
				LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(ContextActor, [&ContextLevelInstance,&LevelInstanceEdits](ALevelInstance* LevelInstanceActor)
				{
					if (LevelInstanceActor->IsEditing())
					{
						if (!LevelInstanceActor->IsCurrent())
						{
							ContextLevelInstance = LevelInstanceActor;
							LevelInstanceEdits.Add(ContextLevelInstance);
						}
						return false;
					}
					return true;
				});
			}
		}

		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			LevelInstanceSubsystem->ForEachLevelInstanceEdit([&LevelInstanceEdits, ContextLevelInstance](ALevelInstance* LevelInstanceActor)
			{
				if (ContextLevelInstance != LevelInstanceActor && !LevelInstanceActor->IsCurrent())
				{
					LevelInstanceEdits.Add(LevelInstanceActor);
				}
				return true;
			});
		}

		if (LevelInstanceEdits.Num() > 0)
		{
			FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
			Section.AddSubMenu(
				"SetCurrentLevelInstances",
				LOCTEXT("SetCurrentLevelInstances", "Set Current"),
				TAttribute<FText>(),
				FNewToolMenuDelegate::CreateStatic(&CreateSetCurrentSubMenu, MoveTemp(LevelInstanceEdits), ContextLevelInstance)
			);
		}
	}

	void CreateMoveSelectionToMenu(UToolMenu* Menu)
	{
		if (GEditor->GetSelectedActorCount() > 0)
		{

			TArray<ALevelInstance*> LevelInstanceEdits;
			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<ULevelInstanceSubsystem>())
			{
				LevelInstanceSubsystem->ForEachLevelInstanceEdit([&LevelInstanceEdits](ALevelInstance* LevelInstanceActor)
				{
					LevelInstanceEdits.Add(LevelInstanceActor);
					return true;
				});
			}

			if (LevelInstanceEdits.Num() > 0)
			{
				FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
				Section.AddSubMenu(
					"MoveSelectionToLevelInstances",
					LOCTEXT("MoveSelectionToLevelInstances", "Move Selection to"),
					TAttribute<FText>(),
					FNewToolMenuDelegate::CreateStatic(&CreateMoveSelectionToSubMenu, MoveTemp(LevelInstanceEdits))
				);
			}
		}
	}

	void CreateLevelInstanceFromSelection(ULevelInstanceSubsystem* LevelInstanceSubsystem, ELevelInstanceCreationType CreationType)
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
		if (!GetMutableDefault<ULevelInstanceEditorSettings>()->TemplateMapInfos.Num() || NewLevelDialogModule.CreateAndShowTemplateDialog(MainFrameModule.GetParentWindow(), LOCTEXT("LevelInstanceTemplateDialog", "Choose Level Instance Template..."), GetMutableDefault<ULevelInstanceEditorSettings>()->TemplateMapInfos, TemplateMapPackage))
		{
			UPackage* TemplatePackage = !TemplateMapPackage.IsEmpty() ? LoadPackage(nullptr, *TemplateMapPackage, LOAD_None) : nullptr;
			UWorld* TemplateWorld = TemplatePackage ? UWorld::FindWorldInPackage(TemplatePackage) : nullptr;

			if (!LevelInstanceSubsystem->CreateLevelInstanceFrom(ActorsToMove, CreationType, TemplateWorld))
			{
				FText Title = LOCTEXT("CreateFromSelectionFailTitle", "Create from selection failed");
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CreateFromSelectionFailMsg", "Failed to create LevelInstance from selection. Check log for details."), &Title);
			}
		}
	}

	void CreateLevelInstanceFromSelectionSubMenu(UToolMenu* ToolMenu, ULevelInstanceSubsystem* LevelInstanceSubsystem)
	{
		FToolMenuSection* Section = &ToolMenu->AddSection(FName("LevelInstanceCreateFromSelectionSub"));
		UEnum* CreationTypeEnum = StaticEnum<ELevelInstanceCreationType>();
		check(CreationTypeEnum);
		for (int32 i = 0; i < CreationTypeEnum->NumEnums()-1; ++i)
		{
			if (!CreationTypeEnum->HasMetaData(TEXT("Hidden"), i))
			{
				ELevelInstanceCreationType CreationType = (ELevelInstanceCreationType)CreationTypeEnum->GetValueByIndex(i);
				FToolUIAction CreateFromSelectionAction;
				CreateFromSelectionAction.ExecuteAction.BindLambda([LevelInstanceSubsystem, CreationType](const FToolMenuContext&)
				{
					CreateLevelInstanceFromSelection(LevelInstanceSubsystem, CreationType);
				});

				Section->AddMenuEntry(NAME_None, CreationTypeEnum->GetDisplayNameTextByIndex(i), TAttribute<FText>(), FSlateIcon(), CreateFromSelectionAction);
			}
		}
	}

	void CreateCreateMenu(UToolMenu* Menu)
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GEditor->GetEditorWorldContext().World()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
			
			Section.AddSubMenu(
				"CreateLevelInstanceFromSelection",
				LOCTEXT("CreateLevelInstanceFromSelection", "Create from selection"),
				TAttribute<FText>(),
				FNewToolMenuDelegate::CreateStatic(&CreateLevelInstanceFromSelectionSubMenu, LevelInstanceSubsystem)
			);
		}
	}

	void CreateSaveAsMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		ALevelInstance* ContextLevelInstance = nullptr;
		if (ContextActor)
		{
			if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextActor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
			{
				LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(ContextActor, [&ContextLevelInstance](ALevelInstance* LevelInstanceActor)
					{
						if (LevelInstanceActor->IsEditing())
						{
							ContextLevelInstance = LevelInstanceActor;
							return false;
						}
						return true;
					});
			}
		}

		if (ContextLevelInstance)
		{
			FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
			FText EntryDesc = LOCTEXT("LevelInstanceEditSubMenuEntry", "");
			const bool bCanCommit = ContextLevelInstance->CanCommit(&EntryDesc);

			FToolUIAction SaveAction;
			SaveAction.ExecuteAction.BindLambda([ContextLevelInstance](const FToolMenuContext& MenuContext)
				{
					ContextLevelInstance->SaveAs();
				});
			SaveAction.CanExecuteAction.BindLambda([bCanCommit](const FToolMenuContext& MenuContext)
				{
					return bCanCommit;
				});

			Section.AddMenuEntry(
				"SaveLevelInstanceAs",
				LOCTEXT("SaveLevelInstanceAs", "Save Level as..."),
				TAttribute<FText>(),
				TAttribute<FSlateIcon>(),
				SaveAction);
		}
	}

	void CreateBreakSubMenu(UToolMenu* Menu, ALevelInstance* ContextLevelInstance)
	{
		static int32 BreakLevels = 1;

		check(ContextLevelInstance);

		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextLevelInstance->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			FToolMenuSection& Section = Menu->AddSection(NAME_None, LOCTEXT("LevelInstanceBreakSection", "Break Level Instance"));
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
					.OnClicked_Lambda([ContextLevelInstance, LevelInstanceSubsystem]() 
						{
							const FText LevelInstanceBreakWarning = LOCTEXT("BreakingLevelInstance", "You are about to break the level instance. This action cannot be undone. Are you sure ?");
							if (FMessageDialog::Open(EAppMsgType::YesNo, LevelInstanceBreakWarning) == EAppReturnType::Yes)
							{
								LevelInstanceSubsystem->BreakLevelInstance(ContextLevelInstance, BreakLevels);
							}
							return FReply::Handled();
						})
					.Text(LOCTEXT("BreakLevelInstances_BreakLevelInstanceButton", "Break Level Instance"))
				];

			Section.AddEntry(FToolMenuEntry::InitWidget("SetBreakLevels", MenuWidget, FText::GetEmpty(), false));
		}
	}

	void CreateBreakMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		check(ContextActor);

		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextActor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			ALevelInstance* ContextLevelInstance = nullptr;

			// Find the top level LevelInstance
			LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(ContextActor, [LevelInstanceSubsystem, ContextActor, &ContextLevelInstance](ALevelInstance* Ancestor)
				{
					if (Ancestor->GetLevel() == ContextActor->GetWorld()->GetCurrentLevel())
					{
						ContextLevelInstance = Ancestor;
						return false;
					}
					return true;
				});

			if (ContextLevelInstance && !ContextLevelInstance->IsEditing())
			{
				FToolMenuSection& Section = CreateLevelInstanceSection(Menu);

				Section.AddSubMenu(
					"BreakLevelInstances",
					LOCTEXT("BreakLevelInstances", "Break..."),
					TAttribute<FText>(),
					FNewToolMenuDelegate::CreateStatic(&CreateBreakSubMenu, ContextLevelInstance)
				);
			}
		}
	}

	void CreatePackedBlueprintMenu(UToolMenu* Menu, AActor* ContextActor)
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = ContextActor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			ALevelInstance* ContextLevelInstance = nullptr;

			// Find the top level LevelInstance
			LevelInstanceSubsystem->ForEachLevelInstanceAncestorsAndSelf(ContextActor, [LevelInstanceSubsystem, ContextActor, &ContextLevelInstance](ALevelInstance* Ancestor)
			{
				if (Ancestor->GetLevel() == ContextActor->GetWorld()->GetCurrentLevel())
				{
					ContextLevelInstance = Ancestor;
					return false;
				}
				return true;
			});
						
			if (ContextLevelInstance && !ContextLevelInstance->IsEditing())
			{
				FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
				TSoftObjectPtr<UBlueprint> BlueprintAsset;
				if (APackedLevelInstance* PackedLevelInstance = Cast<APackedLevelInstance>(ContextLevelInstance))
				{
					BlueprintAsset = PackedLevelInstance->BlueprintAsset;
				}

				if (BlueprintAsset.IsNull())
				{
					FToolUIAction UIAction;
					UIAction.ExecuteAction.BindLambda([ContextLevelInstance](const FToolMenuContext& MenuContext)
					{
						TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
						Builder->CreateOrUpdateBlueprint(ContextLevelInstance, nullptr);
					});
					UIAction.CanExecuteAction.BindLambda([](const FToolMenuContext& MenuContext)
					{
						return GEditor->GetSelectedActorCount() > 0;
					});

					Section.AddMenuEntry(
						"CreatePackedBlueprint",
						LOCTEXT("CreatePackedBlueprint", "Create Packed Blueprint"),
						TAttribute<FText>(),
						TAttribute<FSlateIcon>(),
						UIAction);
				}
				else
				{
					FToolUIAction UIAction;
					UIAction.ExecuteAction.BindLambda([ContextLevelInstance, BlueprintAsset](const FToolMenuContext& MenuContext)
					{
						TSharedPtr<FPackedLevelInstanceBuilder> Builder = FPackedLevelInstanceBuilder::CreateDefaultBuilder();
						Builder->CreateOrUpdateBlueprint(ContextLevelInstance->GetWorldAsset(), BlueprintAsset);
					});
					UIAction.CanExecuteAction.BindLambda([](const FToolMenuContext& MenuContext)
					{
						return GEditor->GetSelectedActorCount() > 0;
					});

					Section.AddMenuEntry(
						"UpdatePackedBlueprint",
						LOCTEXT("UpdatePackedBlueprint", "Update Packed Blueprint"),
						TAttribute<FText>(),
						TAttribute<FSlateIcon>(),
						UIAction);
				}
			}
		}
	}

	class FLevelInstanceClassFilter : public IClassViewerFilter
	{
	public:
		
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InClass && InClass->IsChildOf(ALevelInstance::StaticClass()) && !InClass->HasAnyClassFlags(CLASS_Deprecated);
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(ALevelInstance::StaticClass()) && !InUnloadedClassData->HasAnyClassFlags(CLASS_Deprecated);
		}
	};

	void CreateBlueprintFromWorld(UWorld* WorldAsset)
	{
		TSoftObjectPtr<UWorld> LevelInstancePtr(WorldAsset);

		int32 LastSlashIndex = 0;
		FString LongPackageName = LevelInstancePtr.GetLongPackageName();
		LongPackageName.FindLastChar('/', LastSlashIndex);
		
		FString PackagePath = LongPackageName.Mid(0, LastSlashIndex == INDEX_NONE ? MAX_int32 : LastSlashIndex);
		FString AssetName = LevelInstancePtr.GetAssetName() + "_LevelInstance";
		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();

		UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
		BlueprintFactory->AddToRoot();
		BlueprintFactory->OnConfigurePropertiesDelegate.BindLambda([](FClassViewerInitializationOptions* Options)
		{
			Options->bShowDefaultClasses = false;
			Options->bIsBlueprintBaseOnly = false;
			Options->InitiallySelectedClass = ALevelInstance::StaticClass();
			Options->bIsActorsOnly = true;
			Options->ClassFilter = MakeShareable(new FLevelInstanceClassFilter);
		});
		ON_SCOPE_EXIT
		{
			BlueprintFactory->OnConfigurePropertiesDelegate.Unbind();
			BlueprintFactory->RemoveFromRoot();
		};

		if (UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAssetWithDialog(AssetName, PackagePath, UBlueprint::StaticClass(), BlueprintFactory, FName("Create LevelInstance Blueprint"))))
		{
			ALevelInstance* CDO = CastChecked<ALevelInstance>(NewBlueprint->GeneratedClass->GetDefaultObject());
			CDO->SetWorldAsset(LevelInstancePtr);
			FBlueprintEditorUtils::MarkBlueprintAsModified(NewBlueprint);
			
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<UObject*> Assets;
			Assets.Add(NewBlueprint);
			ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
		}		
	}

	void CreateBlueprintFromMenu(UToolMenu* Menu, UWorld* WorldAsset)
	{
		FToolMenuSection& Section = CreateLevelInstanceSection(Menu);
		FToolUIAction UIAction;
		UIAction.ExecuteAction.BindLambda([WorldAsset](const FToolMenuContext& MenuContext)
		{
			CreateBlueprintFromWorld(WorldAsset);
		});

		Section.AddMenuEntry(
			"CreateLevelInstanceBlueprint",
			LOCTEXT("CreateLevelInstanceBlueprint", "New Blueprint..."),
			TAttribute<FText>(),
			TAttribute<FSlateIcon>(),
			UIAction);
	}
};

void FLevelInstanceEditorModule::StartupModule()
{
	ExtendContextMenu();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("LevelInstance", FOnGetDetailCustomizationInstance::CreateStatic(&FLevelInstanceActorDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

	// GEditor needs to be set before this module is loaded
	check(GEditor);
	GEditor->OnLevelActorDeleted().AddRaw(this, &FLevelInstanceEditorModule::OnLevelActorDeleted);
	
	EditorLevelUtils::CanMoveActorToLevelDelegate.AddRaw(this, &FLevelInstanceEditorModule::CanMoveActorToLevel);

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnMapChanged().AddRaw(this, &FLevelInstanceEditorModule::OnMapChanged);

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions InitOptions;
	InitOptions.bShowFilters = true;
	InitOptions.bShowPages = false;
	InitOptions.bAllowClear = true;
	MessageLogModule.RegisterLogListing("LevelInstance", LOCTEXT("LevelInstanceLog", "Level Instance Log"), InitOptions);
}

void FLevelInstanceEditorModule::ShutdownModule()
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

void FLevelInstanceEditorModule::OnLevelActorDeleted(AActor* Actor)
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = Actor->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		LevelInstanceSubsystem->OnActorDeleted(Actor);
	}
}

void FLevelInstanceEditorModule::OnMapChanged(UWorld* World, EMapChangeType MapChangeType)
{
	// On Map Changed, Users will be asked to save unchanged maps. Once we hit the teardown we need to force
	// LevelInstance Edits to be cancelled. If they are still dirty it means the user decided not to save changes.
	if (World && MapChangeType == EMapChangeType::TearDownWorld)
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = World->GetSubsystem<ULevelInstanceSubsystem>())
		{
			LevelInstanceSubsystem->DiscardEdits();
		}
	}
}

void FLevelInstanceEditorModule::CanMoveActorToLevel(const AActor* ActorToMove, const ULevel* DestLevel, bool& bOutCanMove)
{
	if (UWorld* World = ActorToMove->GetWorld())
	{
		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = World->GetSubsystem<ULevelInstanceSubsystem>())
		{
			if (!LevelInstanceSubsystem->CanMoveActorToLevel(ActorToMove))
			{
				bOutCanMove = false;
				return;
			}
		}
	}
}

void FLevelInstanceEditorModule::ExtendContextMenu()
{
	if (UToolMenu* BuildMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Build"))
	{
		FToolMenuSection& Section = BuildMenu->AddSection("LevelEditorLevelInstance", LOCTEXT("LevelInstanceHeading", "Level Instance"));
		FUIAction PackAction(
			FExecuteAction::CreateLambda([]() 
			{
				UWorld* World = GEditor->GetEditorWorldContext().World();
				if (ULevelInstanceSubsystem* LevelInstanceSubsystem = World->GetSubsystem<ULevelInstanceSubsystem>())
				{
					LevelInstanceSubsystem->PackLevelInstances();
				}
			}), 
			FCanExecuteAction::CreateLambda([]()
			{
				UWorld* World = GEditor->GetEditorWorldContext().World();
				if (ULevelInstanceSubsystem* LevelInstanceSubsystem = World->GetSubsystem<ULevelInstanceSubsystem>())
				{
					return LevelInstanceSubsystem->CanPackLevelInstances();
				}
				return false;
			}));

		FToolMenuEntry& Entry = Section.AddMenuEntry(NAME_None, LOCTEXT("PackLevelInstancesTitle", "Pack Level Instances"),
			LOCTEXT("PackLevelInstancesTooltip", "Update packed level instances and blueprints"), FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.LevelInstance"), PackAction, EUserInterfaceActionType::Button);
	}

	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu"))
	{
		FToolMenuSection& Section = Menu->AddDynamicSection("ActorLevelInstance", FNewToolMenuDelegate::CreateLambda([this](UToolMenu* ToolMenu)
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
						LevelInstanceMenuUtils::CreateEditMenu(ToolMenu, ContextActor);
						LevelInstanceMenuUtils::CreateCommitMenu(ToolMenu, ContextActor);
						LevelInstanceMenuUtils::CreateSaveAsMenu(ToolMenu, ContextActor);
						LevelInstanceMenuUtils::CreateBreakMenu(ToolMenu, ContextActor);
						LevelInstanceMenuUtils::CreatePackedBlueprintMenu(ToolMenu, ContextActor);
					}

					LevelInstanceMenuUtils::CreateSetCurrentMenu(ToolMenu, ContextActor);
					LevelInstanceMenuUtils::CreateMoveSelectionToMenu(ToolMenu);
					LevelInstanceMenuUtils::CreateCreateMenu(ToolMenu);
				}
			}
		}), FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
	}

	if (UToolMenu* WorldAssetMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.World"))
	{
		FToolMenuSection& Section = WorldAssetMenu->AddDynamicSection("ActorLevelInstance", FNewToolMenuDelegate::CreateLambda([this](UToolMenu* ToolMenu)
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
						LevelInstanceMenuUtils::CreateBlueprintFromMenu(ToolMenu, WorldAsset);
					}
				}
			}
		}), FToolMenuInsert(NAME_None, EToolMenuInsertType::Default));
	}
	
}

#undef LOCTEXT_NAMESPACE

