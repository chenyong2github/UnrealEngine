// Copyright Epic Games, Inc. All Rights Reserved.

#include "MatineeToLevelSequenceModule.h"
#include "Camera/CameraShakeBase.h"
#include "CameraAnimToTemplateSequenceConverter.h"
#include "Dialogs/Dialogs.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "IAssetTools.h"
#include "LevelEditor.h"
#include "Matinee/MatineeActor.h"
#include "Matinee/MatineeActorCameraAnim.h"
#include "MatineeCameraShakeToNewCameraShakeConverter.h"
#include "MatineeConverter.h"
#include "MatineeToLevelSequenceConverter.h"
#include "MatineeToLevelSequenceLog.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "MatineeToLevelSequenceModule"

DEFINE_LOG_CATEGORY(LogMatineeToLevelSequence);

#if !IS_MONOLITHIC
	UE::MovieScene::FEntityManager*& GEntityManagerForDebugging = UE::MovieScene::GEntityManagerForDebuggingVisualizers;
#endif

/**
 * Implements the MatineeToLevelSequence module.
 */
class FMatineeToLevelSequenceModule
	: public IMatineeToLevelSequenceModule
{
public:
	// IModuleInterface interface

	virtual void StartupModule() override
	{
		if (GEditor)
		{
			GEditor->OnShouldOpenMatinee().BindRaw(this, &FMatineeToLevelSequenceModule::ShouldOpenMatinee);
		}

		RegisterMenuExtensions();
		RegisterAssetTools();
		RegisterEditorTab();
	}
	
	virtual void ShutdownModule() override
	{
		UnregisterEditorTab();
		UnregisterAssetTools();
		UnregisterMenuExtensions();
	}

 	FDelegateHandle RegisterTrackConverterForMatineeClass(TSubclassOf<UInterpTrack> InterpTrackClass, FOnConvertMatineeTrack OnConvertMatineeTrack) override
	{
		return MatineeConverter.RegisterTrackConverterForMatineeClass(InterpTrackClass, OnConvertMatineeTrack);
	}
 	
	void UnregisterTrackConverterForMatineeClass(FDelegateHandle RemoveDelegate) override
	{
		MatineeConverter.UnregisterTrackConverterForMatineeClass(RemoveDelegate);
	}

protected:

	/** Register menu extensions for the level editor toolbar. */
	void RegisterMenuExtensions()
	{
		// Register level editor menu extender
		LevelEditorMenuExtenderDelegate = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateRaw(this, &FMatineeToLevelSequenceModule::ExtendLevelViewportContextMenu);
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
		MenuExtenders.Add(LevelEditorMenuExtenderDelegate);
		LevelEditorExtenderDelegateHandle = MenuExtenders.Last().GetHandle();
	}

	/** Register new asset tools actions on the content browser. */
	void RegisterAssetTools()
	{
		FToolMenuOwnerScoped OnwerScoped(this);

		UToolMenu* CameraAnimMenu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.CameraAnim");
		FToolMenuSection& CameraAnimMenuSection = CameraAnimMenu->AddSection("ConversionActions", LOCTEXT("CameraAnim_ConversionSection", "Conversion"), FToolMenuInsert(FName("CommonAssetActions"), EToolMenuInsertType::Before));
		CameraAnimMenuSection.AddMenuEntry(
				"CameraAnim_ConvertToSequence",
				LOCTEXT("CameraAnim_ConvertToSequence", "Convert to Sequence"),
				LOCTEXT("CameraAnim_ConvertToSequence_Tooltip", "Converts the CameraAnim asset to a CameraAnimationSequence, to be used with the Sequencer."),
				FSlateIcon(),
				FToolMenuExecuteAction(FToolMenuExecuteAction::CreateRaw(this, &FMatineeToLevelSequenceModule::OnExecuteConvertCameraAnim))
				);
	}

	void RegisterEditorTab()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda([this]()
			{
				// Add a new entry in the level editor's "Window" menu, which lets the user open the camera shake preview tool.
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
				TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

				const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
				const FSlateIcon Icon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditMatinee", "LevelEditor.EditMatinee.Small"));

				LevelEditorTabManager->RegisterTabSpawner("MatineeToLevelSequence", FOnSpawnTab::CreateRaw(this, &FMatineeToLevelSequenceModule::CreateCameraShakeConverterTab))
				.SetDisplayName(LOCTEXT("MatineeCameraShakeConverter", "Matinee Camera Shake Converter"))
				.SetTooltipText(LOCTEXT("MatineeCameraShakeConverterTooltipText", "Open the legacy camera shake converter tool."))
				.SetIcon(Icon)
				.SetGroup(MenuStructure.GetDeveloperToolsMiscCategory());
			});
	}

	/** Unregisters menu extensions for the level editor toolbar. */
	void UnregisterMenuExtensions()
	{
		// Unregister level editor menu extender
		if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll([&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate) {
				return Delegate.GetHandle() == LevelEditorExtenderDelegateHandle;
			});
		}
	}

	/** Unregisters content browser extensions. */
	void UnregisterAssetTools()
	{
		UToolMenus::UnregisterOwner(this);
	}

	void UnregisterEditorTab()
	{
		if (LevelEditorTabManagerChangedHandle.IsValid())
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}
	}

	TSharedRef<FExtender> ExtendLevelViewportContextMenu(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors)
	{
		TSharedRef<FExtender> Extender(new FExtender());

		TArray<TWeakObjectPtr<AActor> > ActorsToConvert;
		for (AActor* Actor : SelectedActors)
		{
			if (Actor->IsA(AMatineeActor::StaticClass()))
			{
				ActorsToConvert.Add(Actor);
			}
		}

		if (ActorsToConvert.Num())
		{
			// Add the convert to level sequence asset sub-menu extender
			Extender->AddMenuExtension(
				"ActorSelectVisibilityLevels",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateRaw(this, &FMatineeToLevelSequenceModule::CreateLevelViewportContextMenuEntries, ActorsToConvert));
		}

		return Extender;
	}

	void CreateLevelViewportContextMenuEntries(FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<AActor> > ActorsToConvert)
	{
		MenuBuilder.BeginSection("LevelSequence", LOCTEXT("LevelSequenceLevelEditorHeading", "Level Sequence"));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("MenuExtensionConvertMatineeToLevelSequence", "Convert to Level Sequence"),
			LOCTEXT("MenuExtensionConvertMatineeToLevelSequence_Tooltip", "Convert to Level Sequence"),
			FSlateIcon(),
			FExecuteAction::CreateRaw(this, &FMatineeToLevelSequenceModule::OnExecuteConvertMatineeToLevelSequence, ActorsToConvert),
			NAME_None,
			EUserInterfaceActionType::Button);

		MenuBuilder.EndSection();
	}

	/** Callback when opening a matinee. Prompts the user whether to convert this matinee to a level sequence actor */
	bool ShouldOpenMatinee(AMatineeActor* MatineeActor)
	{
		//@todo Camera anims aren't supported as level sequence assets yet
		if (MatineeActor->IsA(AMatineeActorCameraAnim::StaticClass()))
		{
			return true;
		}

		// Pop open a dialog asking whether the user to convert and launcher sequencer or no
		FSuppressableWarningDialog::FSetupInfo Info( 
			LOCTEXT("MatineeToLevelSequencePrompt", "As of 4.23, Matinee is no longer supported by UE4 and will be removed from the engine in the near future. Once removed, you will no longer be able to run a Matinee or open Matinee.\n\nWould you like to continue opening Matinee or convert your Matinee to a Level Sequence Asset?"), 
			LOCTEXT("MatineeToLevelSequenceTitle", "Convert Matinee to Level Sequence Asset"), 
			TEXT("MatineeToLevelSequence") );
		Info.ConfirmText = LOCTEXT("MatineeToLevelSequence_ConfirmText", "Open Matinee");
		Info.CancelText = LOCTEXT("MatineeToLevelSequence_CancelText", "Convert");
		Info.CheckBoxText = LOCTEXT("MatineeToLevelSequence_CheckBoxText", "Don't Ask Again");

		FSuppressableWarningDialog ShouldOpenMatineeDialog( Info );

		if (ShouldOpenMatineeDialog.ShowModal() == FSuppressableWarningDialog::EResult::Cancel)
		{
			TArray<TWeakObjectPtr<AActor>> ActorsToConvert;
			ActorsToConvert.Add(MatineeActor);
			OnExecuteConvertMatineeToLevelSequence(ActorsToConvert);

			// Return false so that the editor doesn't open matinee
			return false;
		}
	
		return true;
	}

	void OnExecuteConvertMatineeToLevelSequence(TArray<TWeakObjectPtr<AActor> > ActorsToConvert)
	{
		FMatineeToLevelSequenceConverter Converter(&MatineeConverter);
		Converter.ConvertMatineeToLevelSequence(ActorsToConvert);
	}

	void OnExecuteConvertCameraAnim(const FToolMenuContext& MenuContext)
	{
		FCameraAnimToTemplateSequenceConverter Converter(&MatineeConverter);
		Converter.ConvertCameraAnim(MenuContext);
	}

	TSharedRef<SDockTab> CreateCameraShakeConverterTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::PanelTab)
			[
				FMatineeCameraShakeToNewCameraShakeConverter::CreateMatineeCameraShakeConverter(&MatineeConverter)
			];
	}

private:

	FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors LevelEditorMenuExtenderDelegate;

	FDelegateHandle LevelEditorTabManagerChangedHandle;
	FDelegateHandle LevelEditorExtenderDelegateHandle;

	FMatineeConverter MatineeConverter;
};

IMPLEMENT_MODULE(FMatineeToLevelSequenceModule, MatineeToLevelSequence);

#undef LOCTEXT_NAMESPACE
