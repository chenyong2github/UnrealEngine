// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MatineeToLevelSequenceModule.h"
#include "MatineeToLevelSequenceLog.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "IMovieScenePlayer.h"
#include "MovieSceneSequence.h"
#include "LevelSequence.h"
#include "Factories/Factory.h"
#include "AssetData.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_Base.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Misc/Paths.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/TabManager.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Character.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraAnim.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/LightComponentBase.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "Matinee/InterpData.h"
#include "Matinee/InterpTrackBoolProp.h"
#include "Matinee/InterpTrackLinearColorProp.h"
#include "Matinee/InterpTrackColorScale.h"
#include "Matinee/InterpTrackColorProp.h"
#include "Matinee/InterpTrackFloatProp.h"
#include "Matinee/InterpTrackFloatMaterialParam.h"
#include "Matinee/InterpTrackVectorMaterialParam.h"
#include "Matinee/InterpTrackMove.h"
#include "Matinee/InterpTrackAnimControl.h"
#include "Matinee/InterpTrackSound.h"
#include "Matinee/InterpTrackFade.h"
#include "Matinee/InterpTrackSlomo.h"
#include "Matinee/InterpTrackDirector.h"
#include "Matinee/InterpTrackEvent.h"
#include "Matinee/InterpTrackAudioMaster.h"
#include "Matinee/InterpTrackVectorProp.h"
#include "Matinee/InterpTrackVisibility.h"
#include "Matinee/InterpGroupInst.h"
#include "Matinee/InterpGroupDirector.h"
#include "Matinee/MatineeActor.h"
#include "Matinee/MatineeActorCameraAnim.h"
#include "Matinee/InterpTrackMoveAxis.h"
#include "MatineeUtils.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "MovieScene.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "LevelSequenceActor.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Matinee/InterpTrackToggle.h"
#include "MatineeImportTools.h"
#include "MovieSceneFolder.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneMaterialTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneSlomoTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Animation/SkeletalMeshActor.h"
#include "Dialogs/Dialogs.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "MovieSceneTimeHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EngineAnalytics.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "CameraAnimationSequence.h"

#define LOCTEXT_NAMESPACE "MatineeToLevelSequence"

DEFINE_LOG_CATEGORY(LogMatineeToLevelSequence);

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
	}
	
	virtual void ShutdownModule() override
	{
		UnregisterAssetTools();
		UnregisterMenuExtensions();
	}

 	FDelegateHandle RegisterTrackConverterForMatineeClass(TSubclassOf<UInterpTrack> InterpTrackClass, FOnConvertMatineeTrack OnConvertMatineeTrack) override
	{
		if (ExtendedInterpConverters.Contains(InterpTrackClass))
		{
			UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Track converter already registered for: %s"), InterpTrackClass->GetClass());
			return FDelegateHandle();
		}

		return ExtendedInterpConverters.Add(InterpTrackClass, OnConvertMatineeTrack).GetHandle();
	}
 	
	void UnregisterTrackConverterForMatineeClass(FDelegateHandle RemoveDelegate) override
	{
		for (auto InterpConverter : ExtendedInterpConverters)
		{
			if (InterpConverter.Value.GetHandle() == RemoveDelegate)
			{
				ExtendedInterpConverters.Remove(*InterpConverter.Key);
				return;
			}
		}

		UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Attempted to remove track convert that could not be found"));
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

		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.CameraAnim");
		FToolMenuSection& MenuSection = Menu->AddSection("ConversionActions", LOCTEXT("CameraAnim_ConversionSection", "Conversion"), FToolMenuInsert(FName("CommonAssetActions"), EToolMenuInsertType::Before));
		MenuSection.AddMenuEntry(
				"CameraAnim_ConvertToSequence",
				LOCTEXT("CameraAnim_ConvertToSequence", "Convert to Sequence"),
				LOCTEXT("CameraAnim_ConvertToSequence_Tooltip", "Converts the CameraAnim asset to a CameraAnimationSequence, to be used with the Sequencer."),
				FSlateIcon(),
				FToolMenuExecuteAction(FToolMenuExecuteAction::CreateRaw(this, &FMatineeToLevelSequenceModule::OnExecuteConvertCameraAnim))
				);
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
			FExecuteAction::CreateRaw(this, &FMatineeToLevelSequenceModule::OnConvertMatineeToLevelSequence, ActorsToConvert),
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
			OnConvertMatineeToLevelSequence(ActorsToConvert);

			// Return false so that the editor doesn't open matinee
			return false;
		}
	
		return true;
	}

	void OnExecuteConvertCameraAnim(const FToolMenuContext& MenuContext)
	{
		UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
		if (Context == nullptr)
		{
			return;
		}

		// Get the assets to convert.
		TArray<UCameraAnim*> CameraAnimsToConvert;
		for (TWeakObjectPtr<UObject> SelectedObject : Context->SelectedObjects)
		{
			if (UCameraAnim* CameraAnimToConvert = CastChecked<UCameraAnim>(SelectedObject.Get(), ECastCheckedType::NullAllowed))
			{
				CameraAnimsToConvert.Add(CameraAnimToConvert);
			}
		}
		if (CameraAnimsToConvert.Num() == 0)
		{
			return;
		}

		// Find the factory class.
		UFactory* CameraAnimationSequenceFactoryNew = FindFactoryForClass(UCameraAnimationSequence::StaticClass());
		ensure(CameraAnimationSequenceFactoryNew != nullptr);

		// Convert all selected camera anims.
		int32 NumWarnings = 0;
		bool bConvertSuccess = false;
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (UCameraAnim* CameraAnimToConvert : CameraAnimsToConvert)
		{
			bConvertSuccess = ConvertSingleCameraAnimToTemplateSequence(
					CameraAnimToConvert, AssetTools, CameraAnimationSequenceFactoryNew, NumWarnings) 
				|| bConvertSuccess;
		}

		if (bConvertSuccess)
		{
			FText NotificationText = FText::Format(
					LOCTEXT("CameraAnim_ConvertToSequence_Notification", "Converted {0} assets with {1} warnings"),
					FText::AsNumber(CameraAnimsToConvert.Num()), FText::AsNumber(NumWarnings));
			FNotificationInfo NotificationInfo(NotificationText);
			NotificationInfo.ExpireDuration = 5.f;
			NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->InvokeTab(FName("OutputLog")); });
			NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		}
	}

	bool ConvertSingleCameraAnimToTemplateSequence(UCameraAnim* CameraAnimToConvert, IAssetTools& AssetTools, UFactory* CameraAnimationSequenceFactoryNew, int32& NumWarnings)
	{
		// Ask user for the new asset's name and folder.
		UPackage* AssetPackage = CameraAnimToConvert->GetOutermost();
		FString NewCameraAnimSequenceName = CameraAnimToConvert->GetName() + FString("Sequence");
		FString NewCameraAnimSequencePath = FPaths::GetPath(AssetPackage->GetName());

		UObject* NewAsset = AssetTools.CreateAssetWithDialog(NewCameraAnimSequenceName, NewCameraAnimSequencePath, UCameraAnimationSequence::StaticClass(), CameraAnimationSequenceFactoryNew);
		if (NewAsset == nullptr)
		{
			return false;
		}

		// Create the new sequence.
		UCameraAnimationSequence* NewSequence = Cast<UCameraAnimationSequence>(NewAsset);
		NewSequence->BoundActorClass = ACameraActor::StaticClass();

		UMovieScene* NewMovieScene = NewSequence->GetMovieScene();

		// Add the spawnable for the camera.
		ACameraActor* CameraTemplate = NewObject<ACameraActor>(NewMovieScene, ACameraActor::StaticClass());
		FGuid SpawnableGuid = NewMovieScene->AddSpawnable("CameraActor", *CameraTemplate);
		
		// Set sequence length.
		const int32 LengthInFrames = (CameraAnimToConvert->AnimLength * NewMovieScene->GetTickResolution()).FrameNumber.Value;
		NewMovieScene->SetPlaybackRange(FFrameNumber(0), LengthInFrames + 1);

		// Add spawning track for the camera.
		UMovieSceneSpawnTrack* NewSpawnTrack = NewMovieScene->AddTrack<UMovieSceneSpawnTrack>(SpawnableGuid);
		UMovieSceneSpawnSection* NewSpawnSection = CastChecked<UMovieSceneSpawnSection>(NewSpawnTrack->CreateNewSection());
		NewSpawnSection->GetChannel().SetDefault(true);
		NewSpawnSection->SetStartFrame(TRangeBound<FFrameNumber>());
		NewSpawnSection->SetEndFrame(TRangeBound<FFrameNumber>());
		NewSpawnTrack->AddSection(*NewSpawnSection);

		// Add camera animation data.
		if (CameraAnimToConvert->CameraInterpGroup != nullptr)
		{
			ConvertInterpGroup(
					CameraAnimToConvert->CameraInterpGroup, SpawnableGuid, nullptr, 
					NewSequence, NewMovieScene, NumWarnings);
		}

		return true;
	}

	/** Callback for converting a matinee to a level sequence asset. */
	void OnConvertMatineeToLevelSequence(TArray<TWeakObjectPtr<AActor> > ActorsToConvert)
	{
		// Keep track of how many people actually used the tool to convert assets over.
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Matinee.ConversionTool.MatineeActorConverted"));
		}

		TArray<TWeakObjectPtr<ALevelSequenceActor> > NewActors;

		int32 NumWarnings = 0;
		for (TWeakObjectPtr<AActor> Actor : ActorsToConvert)
		{
			TWeakObjectPtr<ALevelSequenceActor> NewActor = ConvertSingleMatineeToLevelSequence(Actor, NumWarnings);
			if (NewActor.IsValid())
			{
				NewActors.Add(NewActor);
			}
		}

		// Select the newly created level sequence actors
		const bool bNotifySelectionChanged = true;
		const bool bDeselectBSP = true;
		const bool bWarnAboutTooManyActors = false;
		const bool bSelectEvenIfHidden = false;

		GEditor->GetSelectedActors()->Modify();
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();

		GEditor->SelectNone( bNotifySelectionChanged, bDeselectBSP, bWarnAboutTooManyActors );

		for (TWeakObjectPtr<AActor> NewActor : NewActors )
		{
			GEditor->SelectActor(NewActor.Get(), true, bNotifySelectionChanged, bSelectEvenIfHidden );
		}

		GEditor->GetSelectedActors()->EndBatchSelectOperation();
		GEditor->NoteSelectionChange();

		// Edit the first asset
		if (NewActors.Num())
		{
			UObject* NewAsset = NewActors[0]->LevelSequence.TryLoad();
			if (NewAsset)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
			}

			FText NotificationText;
			if (NewActors.Num() == 1)
			{
				NotificationText = FText::Format(LOCTEXT("MatineeToLevelSequence_SingularResult", "Conversion to {0} complete with {1} warnings"), FText::FromString(NewActors[0]->GetActorLabel()), FText::AsNumber(NumWarnings));
			}
			else
			{
				NotificationText = FText::Format(LOCTEXT("MatineeToLevelSequence_Result", "Converted {0} with {1} warnings"), FText::AsNumber(NewActors.Num()), FText::AsNumber(NumWarnings));
			}

			FNotificationInfo NotificationInfo(NotificationText);
			NotificationInfo.ExpireDuration = 5.f;
			NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->InvokeTab(FName("OutputLog")); });
			NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		}
	}

	/** Find the factory that can create instances of the given class */
	static UFactory* FindFactoryForClass(UClass* InClass)
	{
		for (TObjectIterator<UClass> It ; It ; ++It)
		{
			UClass* CurrentClass = *It;
			if (CurrentClass->IsChildOf(UFactory::StaticClass()) && !(CurrentClass->HasAnyClassFlags(CLASS_Abstract)))
			{
				UFactory* Factory = Cast<UFactory>(CurrentClass->GetDefaultObject());
				if (Factory->CanCreateNew() && Factory->ImportPriority >= 0 && Factory->SupportedClass == InClass)
				{
					return Factory;
				}
			}
		}
		return nullptr;
	}

	/** Find or add a folder for the given actor **/
	static UMovieSceneFolder* FindOrAddFolder(UMovieScene* MovieScene, FName FolderName)
	{
		// look for a folder to put us in
		UMovieSceneFolder* FolderToUse = nullptr;
		for (UMovieSceneFolder* Folder : MovieScene->GetRootFolders())
		{
			if (Folder->GetFolderName() == FolderName)
			{
				FolderToUse = Folder;
				break;
			}
		}

		if (FolderToUse == nullptr)
		{
			FolderToUse = NewObject<UMovieSceneFolder>(MovieScene, NAME_None, RF_Transactional);
			FolderToUse->SetFolderName(FolderName);
			MovieScene->GetRootFolders().Add(FolderToUse);
		}

		return FolderToUse;
	}

	/** Find or add a folder for the given actor **/
	static void FindOrAddFolder(UMovieScene* MovieScene, TWeakObjectPtr<AActor> Actor, FGuid Guid)
	{
		FName FolderName(NAME_None);
		if(Actor.Get()->IsA<ACharacter>() || Actor.Get()->IsA<ASkeletalMeshActor>())
		{
			FolderName = TEXT("Characters");
		}
		else if(Actor.Get()->GetClass()->IsChildOf(ACameraActor::StaticClass()))
		{
			FolderName = TEXT("Cameras");
		}
		else if(Actor.Get()->GetClass()->IsChildOf(ALight::StaticClass()))
		{
			FolderName = TEXT("Lights");
		}
		else if (Actor.Get()->FindComponentByClass<UParticleSystemComponent>())
		{
			FolderName = TEXT("Particles");
		}
		else
		{
			FolderName = TEXT("Misc");
		}

		UMovieSceneFolder* FolderToUse = FindOrAddFolder(MovieScene, FolderName);
		FolderToUse->AddChildObjectBinding(Guid);
	}

	/** Add master track to a folder **/
	static void AddMasterTrackToFolder(UMovieScene* MovieScene, UMovieSceneTrack* MovieSceneTrack, FName FolderName)
	{
		UMovieSceneFolder* FolderToUse = FindOrAddFolder(MovieScene, FolderName);
		FolderToUse->AddChildMasterTrack(MovieSceneTrack);
	}

	/** Add property to possessable node **/
	template <typename T>
	static T* AddPropertyTrack(FName InPropertyName, AActor* InActor, const FGuid& PossessableGuid, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings, TMap<UObject*, FGuid>& BoundObjectsToGuids)
	{
		T* PropertyTrack = nullptr;

		// Find the property that matinee uses
		void* PropContainer = NULL;
		FProperty* Property = NULL;
		UObject* PropObject = FMatineeUtils::FindObjectAndPropOffset(PropContainer, Property, InActor, InPropertyName );

		FGuid ObjectGuid = PossessableGuid;
		if (PropObject && Property)
		{
			// If the property object that owns this property isn't already bound, add a binding to the property object
			if (BoundObjectsToGuids.Contains(PropObject))
			{
				ObjectGuid = BoundObjectsToGuids[PropObject];
			}
			else
			{
				ObjectGuid = NewMovieScene->AddPossessable(PropObject->GetName(), PropObject->GetClass());
				NewSequence->BindPossessableObject(ObjectGuid, *PropObject, InActor);

				BoundObjectsToGuids.Add(PropObject, ObjectGuid);
			}

			FMovieScenePossessable* ChildPossessable = NewMovieScene->FindPossessable(ObjectGuid);

			if (ChildPossessable)
			{
				ChildPossessable->SetParent(PossessableGuid);
			}

			// cbb: String manipulations to get the property path in the right form for sequencer
			FString PropertyName = Property->GetFName().ToString();

			// Special case for Light components which have some deprecated names
			if (PropObject->GetClass()->IsChildOf(ULightComponentBase::StaticClass()))
			{
				TMap<FName, FName> PropertyNameRemappings;
				PropertyNameRemappings.Add(TEXT("Brightness"), TEXT("Intensity"));
				PropertyNameRemappings.Add(TEXT("Radius"), TEXT("AttenuationRadius"));

				FName* RemappedName = PropertyNameRemappings.Find(*PropertyName);
				if (RemappedName != nullptr)
				{
					PropertyName = RemappedName->ToString();
				}
			}

			TArray<FFieldVariant> PropertyArray;
			FFieldVariant Outer = Property->GetOwnerVariant();
			while (Outer.IsA(FProperty::StaticClass()) || Outer.IsA(UScriptStruct::StaticClass()))
			{
				PropertyArray.Insert(Outer, 0);
				Outer = Outer.GetOwnerVariant();
			}

			FString PropertyPath;
			for (auto PropertyIt : PropertyArray)
			{
				if (PropertyPath.Len())
				{
					PropertyPath = PropertyPath + TEXT(".");
				}
				PropertyPath = PropertyPath + PropertyIt.GetName();
			}
			if (PropertyPath.Len())
			{
				PropertyPath = PropertyPath + TEXT(".");
			}
			PropertyPath = PropertyPath + PropertyName;

			PropertyTrack = NewMovieScene->AddTrack<T>(ObjectGuid);	
			PropertyTrack->SetPropertyNameAndPath(*PropertyName, PropertyPath);
		}
		else
		{
			UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Can't find property '%s' for '%s'."), *InPropertyName.ToString(), *InActor->GetActorLabel());
			++NumWarnings;
		}

		return PropertyTrack;
	}

	FGuid FindComponentGuid(AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, FGuid PossessableGuid)
	{
		FGuid ComponentGUID;
		// Skeletal and static mesh actors can both have material component tracks, and need to have their mesh component added to sequencer.
		if (GroupActor->GetClass() == ASkeletalMeshActor::StaticClass())
		{

			ASkeletalMeshActor* SkelMeshActor = CastChecked<ASkeletalMeshActor>(GroupActor);
			USkeletalMeshComponent* SkelMeshComponent = SkelMeshActor->GetSkeletalMeshComponent();
			// In matinee a component may be referenced in multiple material tracks, so check to see if this one is already bound.
			FGuid FoundGUID = NewSequence->FindPossessableObjectId(*SkelMeshComponent, SkelMeshActor);
			if (FoundGUID != FGuid())
			{
				ComponentGUID = FoundGUID;
			}
			else
			{
				ComponentGUID = NewMovieScene->AddPossessable(SkelMeshComponent->GetName(), SkelMeshComponent->GetClass());
				FMovieScenePossessable* ChildPossesable = NewMovieScene->FindPossessable(ComponentGUID);
				ChildPossesable->SetParent(PossessableGuid);
				NewSequence->BindPossessableObject(ComponentGUID, *SkelMeshComponent, GroupActor->GetWorld());
			}
		}
		else if (GroupActor->GetClass() == AStaticMeshActor::StaticClass())
		{
			AStaticMeshActor* StaticMeshActor = CastChecked<AStaticMeshActor>(GroupActor);
			UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent();
			FGuid FoundGUID = NewSequence->FindPossessableObjectId(*StaticMeshComponent, StaticMeshActor);
			if (FoundGUID != FGuid())
			{
				ComponentGUID = FoundGUID;
			}
			else
			{
				ComponentGUID = NewMovieScene->AddPossessable(StaticMeshComponent->GetName(), StaticMeshComponent->GetClass());
				FMovieScenePossessable* ChildPossesable = NewMovieScene->FindPossessable(ComponentGUID);
				ChildPossesable->SetParent(PossessableGuid);
				NewSequence->BindPossessableObject(ComponentGUID, *StaticMeshComponent, GroupActor->GetWorld());
			}
		}
		else
		{
			return FGuid();
		}
		return ComponentGUID;
	}
	
	template<typename T>
	void CopyMaterialsToComponents(int32 NumMaterials, FGuid ComponentGuid, UMovieScene* NewMovieScene, T* MatineeMaterialParamTrack)
	{
		// One matinee material track can change the same parameter for multiple materials, but sequencer binds them to individual tracks.
		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			UMovieSceneComponentMaterialTrack* MaterialTrack = nullptr;

			TArray<UMovieSceneComponentMaterialTrack *> BoundTracks;

			// Find all tracks bound to the component we added.
			for (const FMovieSceneBinding& Binding : NewMovieScene->GetBindings())
			{
				if (Binding.GetObjectGuid() == ComponentGuid)
				{
					for (auto Track : Binding.GetTracks())
					{
						MaterialTrack = Cast<UMovieSceneComponentMaterialTrack>(Track);
						if (MaterialTrack)
						{
							BoundTracks.Add(MaterialTrack);
						}
					}
					break;
				}
			}

			// The material may have already been added to the component, so look first to see if there is a track with the current material index.
			UMovieSceneComponentMaterialTrack** FoundTrack = BoundTracks.FindByPredicate([MaterialIndex](UMovieSceneComponentMaterialTrack* Track) -> bool { return Track && Track->GetMaterialIndex() == MaterialIndex; });
			if (FoundTrack)
			{
				MaterialTrack = *FoundTrack;
			}

			if (MaterialTrack)
			{
				FMatineeImportTools::CopyInterpMaterialParamTrack(MatineeMaterialParamTrack, MaterialTrack);
			}
			else
			{
				MaterialTrack = NewMovieScene->AddTrack<UMovieSceneComponentMaterialTrack>(ComponentGuid);
				if (MaterialTrack)
				{
					MaterialTrack->SetMaterialIndex(MaterialIndex);
					FMatineeImportTools::CopyInterpMaterialParamTrack(MatineeMaterialParamTrack, MaterialTrack);
				}
			}
			
		}
	}

	/** Convert an interp group */
	void ConvertInterpGroup(UInterpGroup* Group, AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings)
	{
		FGuid PossessableGuid;

		// Bind the group actor as a possessable						
		if (GroupActor)
		{
			UObject* BindingContext = GroupActor->GetWorld();
			PossessableGuid = NewMovieScene->AddPossessable(GroupActor->GetActorLabel(), GroupActor->GetClass());
			NewSequence->BindPossessableObject(PossessableGuid, *GroupActor, BindingContext);
	
			FindOrAddFolder(NewMovieScene, GroupActor, PossessableGuid);
		}

		ConvertInterpGroup(Group, PossessableGuid, GroupActor, NewSequence, NewMovieScene, NumWarnings);
	}

	void ConvertInterpGroup(UInterpGroup* Group, FGuid ObjectBindingGuid, AActor* GroupActor, UMovieSceneSequence* NewSequence, UMovieScene* NewMovieScene, int32& NumWarnings)
	{
		TMap<UObject*, FGuid> BoundObjectsToGuids;

		for (int32 j=0; j<Group->InterpTracks.Num(); ++j)
		{
			UInterpTrack* Track = Group->InterpTracks[j];
			if (Track->IsDisabled())
			{
				continue;
			}

			// Handle each track class
			if (ExtendedInterpConverters.Find(Track->GetClass()))
			{
				ExtendedInterpConverters.Find(Track->GetClass())->Execute(Track, ObjectBindingGuid, NewMovieScene);
			}
			else if (Track->IsA(UInterpTrackMove::StaticClass()))
			{
				UInterpTrackMove* MatineeMoveTrack = StaticCast<UInterpTrackMove*>(Track);

				bool bHasKeyframes = MatineeMoveTrack->GetNumKeyframes() != 0;

				for (auto SubTrack : MatineeMoveTrack->SubTracks)
				{
					if (SubTrack->IsA(UInterpTrackMoveAxis::StaticClass()))
					{
						UInterpTrackMoveAxis* MoveSubTrack = Cast<UInterpTrackMoveAxis>(SubTrack);
						if (MoveSubTrack)
						{
							if (MoveSubTrack->FloatTrack.Points.Num() > 0)
							{
								bHasKeyframes = true;
								break;
							}
						}
					}
				}

				if ( bHasKeyframes && ObjectBindingGuid.IsValid())
				{
					FVector DefaultScale = GroupActor != nullptr ? GroupActor->GetActorScale() : FVector(1.f);
					UMovieScene3DTransformTrack* TransformTrack = NewMovieScene->AddTrack<UMovieScene3DTransformTrack>(ObjectBindingGuid);								
					FMatineeImportTools::CopyInterpMoveTrack(MatineeMoveTrack, TransformTrack, DefaultScale);
				}
			}
			else if (Track->IsA(UInterpTrackAnimControl::StaticClass()))
			{
				UInterpTrackAnimControl* MatineeAnimControlTrack = StaticCast<UInterpTrackAnimControl*>(Track);
				if (MatineeAnimControlTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneSkeletalAnimationTrack* SkeletalAnimationTrack = NewMovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(ObjectBindingGuid);	
					FFrameNumber EndPlaybackRange = MovieScene::DiscreteExclusiveUpper(NewMovieScene->GetPlaybackRange());
					FMatineeImportTools::CopyInterpAnimControlTrack(MatineeAnimControlTrack, SkeletalAnimationTrack, EndPlaybackRange);
				}
			}
			else if (Track->IsA(UInterpTrackToggle::StaticClass()))
			{
				UInterpTrackToggle* MatineeParticleTrack = StaticCast<UInterpTrackToggle*>(Track);
				if (MatineeParticleTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneParticleTrack* ParticleTrack = NewMovieScene->AddTrack<UMovieSceneParticleTrack>(ObjectBindingGuid);	
					FMatineeImportTools::CopyInterpParticleTrack(MatineeParticleTrack, ParticleTrack);
				}
			}
			else if (Track->IsA(UInterpTrackEvent::StaticClass()))
			{
				UInterpTrackEvent* MatineeEventTrack = StaticCast<UInterpTrackEvent*>(Track);
				if (MatineeEventTrack->GetNumKeyframes() != 0)
				{
					UMovieSceneEventTrack* EventTrack = NewMovieScene->AddMasterTrack<UMovieSceneEventTrack>();
					FString EventTrackName = Group->GroupName.ToString() + TEXT("Events");
					EventTrack->SetDisplayName(FText::FromString(EventTrackName));
					FMatineeImportTools::CopyInterpEventTrack(MatineeEventTrack, EventTrack);

					static FName EventsFolder("Events");
					AddMasterTrackToFolder(NewMovieScene, EventTrack, EventsFolder);
				}
			}
			else if (Track->IsA(UInterpTrackSound::StaticClass()))
			{
				UInterpTrackSound* MatineeSoundTrack = StaticCast<UInterpTrackSound*>(Track);
				if (MatineeSoundTrack->GetNumKeyframes() != 0)
				{
					UMovieSceneAudioTrack* AudioTrack = NewMovieScene->AddMasterTrack<UMovieSceneAudioTrack>();
					FString AudioTrackName = Group->GroupName.ToString() + TEXT("Audio");
					AudioTrack->SetDisplayName(FText::FromString(AudioTrackName));					
					FMatineeImportTools::CopyInterpSoundTrack(MatineeSoundTrack, AudioTrack);

					static FName AudioFolder("Audio");
					AddMasterTrackToFolder(NewMovieScene, AudioTrack, AudioFolder);
				}
			}
			else if (Track->IsA(UInterpTrackBoolProp::StaticClass()))
			{
				UInterpTrackBoolProp* MatineeBoolTrack = StaticCast<UInterpTrackBoolProp*>(Track);
				if (MatineeBoolTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneBoolTrack* BoolTrack = AddPropertyTrack<UMovieSceneBoolTrack>(MatineeBoolTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
					if (BoolTrack)
					{
						FMatineeImportTools::CopyInterpBoolTrack(MatineeBoolTrack, BoolTrack);
					}
				}
			}
			else if (Track->IsA(UInterpTrackFloatProp::StaticClass()))
			{
				UInterpTrackFloatProp* MatineeFloatTrack = StaticCast<UInterpTrackFloatProp*>(Track);
				if (MatineeFloatTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneFloatTrack* FloatTrack = AddPropertyTrack<UMovieSceneFloatTrack>(MatineeFloatTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
					if (FloatTrack)
					{
						FMatineeImportTools::CopyInterpFloatTrack(MatineeFloatTrack, FloatTrack);
					}
				}
			}
			else if (Track->IsA(UInterpTrackFloatMaterialParam::StaticClass()))
			{
				UInterpTrackFloatMaterialParam* MatineeMaterialParamTrack = StaticCast<UInterpTrackFloatMaterialParam*>(Track);
				if (MatineeMaterialParamTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					FGuid ComponentGuid = FindComponentGuid(GroupActor, NewSequence, NewMovieScene, ObjectBindingGuid);

					if (ComponentGuid == FGuid())
					{
						continue;
					}

					CopyMaterialsToComponents(MatineeMaterialParamTrack->TargetMaterials.Num(), ComponentGuid, NewMovieScene, MatineeMaterialParamTrack);
					
				}
			}
			else if (Track->IsA(UInterpTrackVectorMaterialParam::StaticClass()))
			{
				UInterpTrackVectorMaterialParam* MatineeMaterialParamTrack = StaticCast<UInterpTrackVectorMaterialParam*>(Track);
				if (MatineeMaterialParamTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					FGuid ComponentGuid = FindComponentGuid(GroupActor, NewSequence, NewMovieScene, ObjectBindingGuid);

					if (ComponentGuid == FGuid())
					{
						continue;
					}

					CopyMaterialsToComponents(MatineeMaterialParamTrack->TargetMaterials.Num(), ComponentGuid, NewMovieScene, MatineeMaterialParamTrack);

				}
			}
			else if (Track->IsA(UInterpTrackVectorProp::StaticClass()))
			{
				UInterpTrackVectorProp* MatineeVectorTrack = StaticCast<UInterpTrackVectorProp*>(Track);
				if (MatineeVectorTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneVectorTrack* VectorTrack = AddPropertyTrack<UMovieSceneVectorTrack>(MatineeVectorTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
					if (VectorTrack)
					{
						VectorTrack->SetNumChannelsUsed(3);
						FMatineeImportTools::CopyInterpVectorTrack(MatineeVectorTrack, VectorTrack);
					}
				}
			}
			else if (Track->IsA(UInterpTrackColorProp::StaticClass()))
			{
				UInterpTrackColorProp* MatineeColorTrack = StaticCast<UInterpTrackColorProp*>(Track);
				if (MatineeColorTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneColorTrack* ColorTrack = AddPropertyTrack<UMovieSceneColorTrack>(MatineeColorTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
					if (ColorTrack)
					{
						FMatineeImportTools::CopyInterpColorTrack(MatineeColorTrack, ColorTrack);
					}
				}
			}
			else if (Track->IsA(UInterpTrackLinearColorProp::StaticClass()))
			{
				UInterpTrackLinearColorProp* MatineeLinearColorTrack = StaticCast<UInterpTrackLinearColorProp*>(Track);
				if (MatineeLinearColorTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneColorTrack* ColorTrack = AddPropertyTrack<UMovieSceneColorTrack>(MatineeLinearColorTrack->PropertyName, GroupActor, ObjectBindingGuid, NewSequence, NewMovieScene, NumWarnings, BoundObjectsToGuids);
					if (ColorTrack)
					{
						FMatineeImportTools::CopyInterpLinearColorTrack(MatineeLinearColorTrack, ColorTrack);
					}
				}
			}
			else if (Track->IsA(UInterpTrackVisibility::StaticClass()))
			{
				UInterpTrackVisibility* MatineeVisibilityTrack = StaticCast<UInterpTrackVisibility*>(Track);
				if (MatineeVisibilityTrack->GetNumKeyframes() != 0 && ObjectBindingGuid.IsValid())
				{
					UMovieSceneVisibilityTrack* VisibilityTrack = NewMovieScene->AddTrack<UMovieSceneVisibilityTrack>(ObjectBindingGuid);	
					if (VisibilityTrack)
					{
						VisibilityTrack->SetPropertyNameAndPath(TEXT("bHidden"), GroupActor->GetPathName() + TEXT(".bHidden"));

						FMatineeImportTools::CopyInterpVisibilityTrack(MatineeVisibilityTrack, VisibilityTrack);
					}
				}
			}
			else if (Track->IsA(UInterpTrackDirector::StaticClass()))
			{
				// Intentionally left blank - The director track is converted in a separate pass below.
			}
			else
			{
				if (GroupActor)
				{
					UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s' for '%s'."), *Track->TrackTitle, *GroupActor->GetActorLabel());
				}
				else
				{
					UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s'."), *Track->TrackTitle);
				}

				++NumWarnings;
			}
		}
	}

	/** Convert a single matinee to a level sequence asset */
	TWeakObjectPtr<ALevelSequenceActor> ConvertSingleMatineeToLevelSequence(TWeakObjectPtr<AActor> ActorToConvert, int32& NumWarnings)
	{
		UObject* AssetOuter = ActorToConvert->GetOuter();
		UPackage* AssetPackage = AssetOuter->GetOutermost();

		FString NewLevelSequenceAssetName = ActorToConvert->GetActorLabel() + FString("LevelSequence");
		FString NewLevelSequenceAssetPath = AssetPackage->GetName();
		int LastSlashPos = NewLevelSequenceAssetPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		NewLevelSequenceAssetPath = NewLevelSequenceAssetPath.Left(LastSlashPos);

		// Create a new level sequence asset with the appropriate name
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UObject* NewAsset = nullptr;
		UFactory* Factory = FindFactoryForClass(ULevelSequence::StaticClass());
		if (Factory != nullptr)
		{
			NewAsset = AssetTools.CreateAssetWithDialog(NewLevelSequenceAssetName, NewLevelSequenceAssetPath, ULevelSequence::StaticClass(), Factory);
		}
		if (!NewAsset)
		{
			return nullptr;
		}

		UMovieSceneSequence* NewSequence = Cast<UMovieSceneSequence>(NewAsset);
		UMovieScene* NewMovieScene = NewSequence->GetMovieScene();

		// Add a level sequence actor for this new sequence
		UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(ALevelSequenceActor::StaticClass());
		if (!ensure(ActorFactory))
		{
			return nullptr;
		}

		ALevelSequenceActor* NewActor = CastChecked<ALevelSequenceActor>(GEditor->UseActorFactory(ActorFactory, FAssetData(NewAsset), &FTransform::Identity));

		struct FTemporaryPlayer : IMovieScenePlayer
		{
			FTemporaryPlayer(UMovieSceneSequence& InSequence, UObject* InContext)
				: Context(InContext)
			{
				RootInstance.Initialize(InSequence, *this);
			}

			virtual FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() { return RootInstance; }
			virtual void UpdateCameraCut(UObject* CameraObject, UObject* UnlockIfCameraObject = nullptr, bool bJumpCut = false) {}
			virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) {}
			virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const {}
			virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const { return EMovieScenePlayerStatus::Stopped; }
			virtual void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) {}
			virtual UObject* GetPlaybackContext() const { return Context; }

			FMovieSceneRootEvaluationTemplateInstance RootInstance;
			UObject* Context;

		} TemporaryPlayer(*NewSequence, NewActor->GetWorld());

		// Walk through all the interp group data and create corresponding tracks on the new level sequence asset
		if (ActorToConvert->IsA(AMatineeActor::StaticClass()))
		{
			AMatineeActor* MatineeActor = StaticCast<AMatineeActor*>(ActorToConvert.Get());
			MatineeActor->InitInterp();

			// Set the length
			const int32 LengthInFrames = (MatineeActor->MatineeData->InterpLength * NewMovieScene->GetTickResolution()).FrameNumber.Value;
			NewMovieScene->SetPlaybackRange(FFrameNumber(0), LengthInFrames + 1);

			// Convert the groups
			for (int32 i=0; i<MatineeActor->GroupInst.Num(); ++i)
			{
				UInterpGroupInst* GrInst = MatineeActor->GroupInst[i];
				UInterpGroup* Group = GrInst->Group;
				AActor* GroupActor = GrInst->GetGroupActor();
				ConvertInterpGroup(Group, GroupActor, NewSequence, NewMovieScene, NumWarnings);
			}

			// Force an evaluation so that bound objects will 
			FMovieSceneContext Context = FMovieSceneContext(FMovieSceneEvaluationRange(FFrameTime(), FFrameRate()), EMovieScenePlayerStatus::Jumping);
			TemporaryPlayer.GetEvaluationTemplate().Evaluate(Context, TemporaryPlayer);

			// Director group - convert this after the regular groups to ensure that the camera cut bindings are there
			UInterpGroupDirector* DirGroup = MatineeActor->MatineeData->FindDirectorGroup();
			if (DirGroup)
			{
				UInterpTrackDirector* MatineeDirectorTrack = DirGroup->GetDirectorTrack();
				if (MatineeDirectorTrack && MatineeDirectorTrack->GetNumKeyframes() != 0)
				{
					UMovieSceneCameraCutTrack* CameraCutTrack = NewMovieScene->AddMasterTrack<UMovieSceneCameraCutTrack>();
					FMatineeImportTools::CopyInterpDirectorTrack(MatineeDirectorTrack, CameraCutTrack, MatineeActor, TemporaryPlayer);
				}

				UInterpTrackFade* MatineeFadeTrack = DirGroup->GetFadeTrack();
				if (MatineeFadeTrack && MatineeFadeTrack->GetNumKeyframes() != 0)
				{						
					UMovieSceneFadeTrack* FadeTrack = NewMovieScene->AddMasterTrack<UMovieSceneFadeTrack>();
					FMatineeImportTools::CopyInterpFadeTrack(MatineeFadeTrack, FadeTrack);
				}

				UInterpTrackSlomo* MatineeSlomoTrack = DirGroup->GetSlomoTrack();
				if (MatineeSlomoTrack && MatineeSlomoTrack->GetNumKeyframes() != 0)
				{
					UMovieSceneSlomoTrack* SlomoTrack = NewMovieScene->AddMasterTrack<UMovieSceneSlomoTrack>();
					FMatineeImportTools::CopyInterpFloatTrack(MatineeSlomoTrack, SlomoTrack);
				}
				
				UInterpTrackColorScale* MatineeColorScaleTrack = DirGroup->GetColorScaleTrack();
				if (MatineeColorScaleTrack && MatineeColorScaleTrack->GetNumKeyframes() != 0)
				{
					UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s'."), *MatineeColorScaleTrack->TrackTitle);
					++NumWarnings;
				}

				UInterpTrackAudioMaster* MatineeAudioMasterTrack = DirGroup->GetAudioMasterTrack();
				if (MatineeAudioMasterTrack && MatineeAudioMasterTrack->GetNumKeyframes() != 0)
				{
					UE_LOG(LogMatineeToLevelSequence, Warning, TEXT("Unsupported track '%s'."), *MatineeAudioMasterTrack->TrackTitle);
					++NumWarnings;
				}
			}

			MatineeActor->TermInterp();
		}
		return NewActor;
	}

private:

	FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors LevelEditorMenuExtenderDelegate;

	FDelegateHandle LevelEditorExtenderDelegateHandle;

	// IMatineeToLevelSequenceModule interface
	TMap<TSubclassOf<UInterpTrack>, FOnConvertMatineeTrack > ExtendedInterpConverters;
};

IMPLEMENT_MODULE(FMatineeToLevelSequenceModule, MatineeToLevelSequence);

#undef LOCTEXT_NAMESPACE
