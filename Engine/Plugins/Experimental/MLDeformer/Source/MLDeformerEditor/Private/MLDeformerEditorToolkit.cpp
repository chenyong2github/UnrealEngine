// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerEditorToolkit.h"

#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "MLDeformerApplicationMode.h"
#include "MLDeformerEditorMode.h"
#include "MLDeformerEditorData.h"
#include "MLDeformerComponent.h"
#include "MLDeformerInputInfo.h"
#include "MLDeformerVizSettingsDetails.h"
#include "MLDeformerEditorStyle.h"

#include "NeuralNetwork.h"

#include "GeometryCacheComponent.h"
#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"

#include "Rendering/SkeletalMeshModel.h"

#include "AnimationEditorPreviewActor.h"
#include "EditorModeManager.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "PersonaModule.h"
#include "IPersonaToolkit.h"
#include "IAssetFamily.h"
#include "ISkeletonEditorModule.h"
#include "IPersonaViewport.h"
#include "Preferences/PersonaOptions.h"
#include "AnimCustomInstanceHelper.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AnimPreviewInstance.h"
#include "MLDeformerPythonTrainingModel.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/Object.h"
#include "Components/TextRenderComponent.h"

#include "Widgets/SBoxPanel.h"
#include "Framework/Notifications/NotificationManager.h"

#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "MLDeformerEditorToolkit"

const FName MLDeformerEditorModes::Editor("MLDeformerEditorMode");
const FName MLDeformerEditorAppName = FName(TEXT("MLDeformerEditorApp"));

FMLDeformerEditorToolkit::FMLDeformerEditorToolkit()
{
	EditorData = MakeShared<FMLDeformerEditorData>();
}

FMLDeformerEditorToolkit::~FMLDeformerEditorToolkit()
{
	EditorData.Reset();
}

void FMLDeformerEditorToolkit::InitAssetEditor(
	const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UMLDeformerAsset* DeformerAsset)
{
	EditorData->SetDeformerAsset(DeformerAsset);

	FPersonaToolkitArgs PersonaToolkitArgs;
	PersonaToolkitArgs.OnPreviewSceneCreated = FOnPreviewSceneCreated::FDelegate::CreateSP(this, &FMLDeformerEditorToolkit::HandlePreviewSceneCreated);
	
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	auto PersonaToolkit = PersonaModule.CreatePersonaToolkit(DeformerAsset, PersonaToolkitArgs);
	EditorData->SetPersonaToolkit(PersonaToolkit);
	EditorData->SetEditorToolkit(this);

	TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(DeformerAsset);
	AssetFamily->RecordAssetOpened(FAssetData(DeformerAsset));

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		MLDeformerEditorAppName,
		FTabManager::FLayout::NullLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		DeformerAsset);

	// Create and set the application mode.
	FMLDeformerApplicationMode* ApplicationMode = new FMLDeformerApplicationMode(SharedThis(this), PersonaToolkit->GetPreviewScene());
	AddApplicationMode(MLDeformerEditorModes::Editor, MakeShareable(ApplicationMode));
	SetCurrentMode(MLDeformerEditorModes::Editor);

	// Activate the editor mode.
	GetEditorModeManager().SetDefaultMode(FMLDeformerEditorMode::ModeName);
	GetEditorModeManager().ActivateMode(FMLDeformerEditorMode::ModeName);

	FMLDeformerEditorMode* EditorMode = static_cast<FMLDeformerEditorMode*>(GetEditorModeManager().GetActiveMode(FMLDeformerEditorMode::ModeName));
	EditorMode->SetEditorData(EditorData);

	ExtendToolbar();
	RegenerateMenusAndToolbars();

	OnSwitchedVisualizationMode();
	EditorData->UpdateTimeSlider();
	EditorData->UpdateIsReadyForTrainingState();
}

TSharedRef<IPersonaToolkit> FMLDeformerEditorToolkit::GetPersonaToolkit() const 
{ 
	return EditorData->GetPersonaToolkitPointer().ToSharedRef();
}

void FMLDeformerEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MLDeformerEditor", "ML Deformer Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FMLDeformerEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FMLDeformerEditorToolkit::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FMLDeformerEditorToolkit::FillToolbar)
	);
}

void FMLDeformerEditorToolkit::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("Training");
	{
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UMLDeformerAsset* DeformerAsset = EditorData->GetDeformerAsset();

					// Ask if we want to retrain the network if we already have something trained.
					if (DeformerAsset->GetInferenceNeuralNetwork() != nullptr)
					{
						const FText ConfirmTitle(LOCTEXT("RetrainConfirmationTitle", "Re-train the network?"));
						const EAppReturnType::Type ConfirmReturnType = FMessageDialog::Open(
							EAppMsgType::YesNo, 
							FText(LOCTEXT("RetrainConfirmationMessage", "This asset already has been trained.\n\nAre you sure you would like to re-train the network with your current settings?")),
							&ConfirmTitle);

						if (ConfirmReturnType == EAppReturnType::No || ConfirmReturnType == EAppReturnType::Cancel)
						{
							return;
						}
					}

					UMLDeformerPythonTrainingModel* MLDeformerModel = UMLDeformerPythonTrainingModel::Get();
            		FGCObjectScopeGuard ModelGuard(MLDeformerModel);
					if (MLDeformerModel)
					{
						ShowNotification(LOCTEXT("StartTraining", "Starting training process"), SNotificationItem::ECompletionState::CS_Pending, true);

						// Backup the mean and scale.
						EditorData->VertexDeltaMeanBackup = DeformerAsset->GetVertexDeltaMean();
						EditorData->VertexDeltaScaleBackup = DeformerAsset->GetVertexDeltaScale();

						// Change the interpolation type for the training sequence to step.
						DeformerAsset->GetAnimSequence()->Interpolation = EAnimInterpolationType::Step;

						// Initialize the training inputs.
						DeformerAsset->SetInputInfo(DeformerAsset->CreateInputInfo());

						// Make sure we have something to train on.
						// If this triggers, the train button most likely was enabled while it shouldn't be.
						check(!DeformerAsset->GetInputInfo().IsEmpty());

						// Init the frame cache.
						FMLDeformerFrameCache::FInitSettings FrameCacheInitSettings;
						FrameCacheInitSettings.DeformerAsset = DeformerAsset;
						
						// Disable cache as it is now implemented in pytorch dataloader
						FrameCacheInitSettings.CacheSizeInBytes = 0; 
						FrameCacheInitSettings.World = EditorData->GetWorld();
						FrameCacheInitSettings.DeltaMode = EDeltaMode::PreSkinning;
						TSharedPtr<FMLDeformerFrameCache> FrameCache = MakeShared<FMLDeformerFrameCache>();
						FrameCache->Init(FrameCacheInitSettings);

						// Perform training and load the resulting model.
						MLDeformerModel->SetEditorData(EditorData);
						MLDeformerModel->SetFrameCache(FrameCache);
						MLDeformerModel->CreateDataSetInterface();

						// Train ML deformer model using user-defined parameters.
						const double StartTime = FPlatformTime::Seconds();
						const int ReturnCode = MLDeformerModel->Train();
						const double TrainingDuration = FPlatformTime::Seconds() - StartTime;
						const ETrainingResult TrainingResult = static_cast<ETrainingResult>(ReturnCode);
						const bool bMarkDirty = HandleTrainingResult(TrainingResult, TrainingDuration);

						UDebugSkelMeshComponent* SkelMeshComponent = EditorData->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
						EditorData->InitAssets();
						if (TrainingResult == ETrainingResult::Success || TrainingResult == ETrainingResult::Aborted)
						{
							// The InitAssets call above reset the normalized flag, so set it back to true.
							// This is safe as we finished training, which means we already normalized data.
							// If we aborted we still have normalized the data. Only when we have AbortedCantUse then we canceled the normalization process.
							EditorData->bIsVertexDeltaNormalized = true;
						}

						EditorData->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).MLDeformerComponent->SetupComponent(DeformerAsset, SkelMeshComponent);
						if (bMarkDirty)
						{
							EditorData->GetDeformerAsset()->Modify();
						}
						
						EditorData->GetDetailsView()->ForceRefresh();
						EditorData->GetVizSettingsDetailsView()->ForceRefresh();

						// Log memory usage.
						const SIZE_T NumBytes = FrameCache->CalcMemUsageInBytes();;
						UE_LOG(LogMLDeformer, Display, TEXT("Cache size used = %jd Bytes (%jd Kb or %.2f Mb)"), 
							NumBytes, 
							NumBytes / 1024, 
							NumBytes / (float)(1024*1024));

						// Clear the model internally, so it deletes the frame cache.
						MLDeformerModel->Clear();
					}
					else
					{
						ShowNotification(LOCTEXT("ModelError", "Python Training module not defined by init_unreal.py"), SNotificationItem::ECompletionState::CS_Fail, true);
						UE_LOG(LogMLDeformer, Error, TEXT("FMLDeformerEditorToolkit: Python Training module not defined by init_unreal.py"));
					}
				}),
				FCanExecuteAction::CreateLambda(
					[this]() 
					{
						return EditorData->IsReadyForTraining();
					})
			),
			NAME_None,
			LOCTEXT("TrainModel", "Train Model"),
			LOCTEXT("TrainModelTooltip", "Train Model using Pytorch"),
			FSlateIcon(),
			EUserInterfaceActionType::ToggleButton
		);
	}
	ToolbarBuilder.EndSection();
}

bool FMLDeformerEditorToolkit::HandleTrainingResult(ETrainingResult TrainingResult, double TrainingDuration)
{
	FText WindowTitle(LOCTEXT("TrainingResultsWindowTitle", "Training Results"));
	FText WindowMessage;

	// Calculate hours, minutes and seconds.
    const int32 Hours = static_cast<int32>(TrainingDuration / 3600);
    TrainingDuration -= Hours * 3600.0;
    const int32 Minutes = static_cast<int32>(TrainingDuration / 60);
    TrainingDuration -= Minutes * 60.0;
    const int32 Seconds = static_cast<int32>(TrainingDuration);

	// Format the results in some HH:MM::SS format.
	FFormatNamedArguments Args;
	FNumberFormattingOptions NumberOptions;
	NumberOptions.SetMinimumIntegralDigits(2);
	NumberOptions.SetUseGrouping(false);
	Args.Add(TEXT("Hours"), FText::AsNumber(Hours, &NumberOptions));
	Args.Add(TEXT("Minutes"), FText::AsNumber(Minutes, &NumberOptions));
	Args.Add(TEXT("Seconds"), FText::AsNumber(Seconds, &NumberOptions));
	const FText FormatString = LOCTEXT("TrainingDurationFormat", "{Hours}:{Minutes}:{Seconds} (HH:MM:SS)");
	const FText TrainingDurationText = FText::Format(FormatString, Args);
	UE_LOG(LogMLDeformer, Display, TEXT("Training duration: %s"), *TrainingDurationText.ToString());

	bool bMarkDirty = false;
	switch (TrainingResult)
	{
		// Training fully finished.
		case ETrainingResult::Success:
		{
			if (!TryLoadOnnxFile())
			{
				GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
				WindowMessage = LOCTEXT("TrainingOnnxLoadFailed", "Training completed but resulting onnx file couldn't be loaded!");
			}
			else
			{
				GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue"));
				FFormatNamedArguments SuccessArgs;
				SuccessArgs.Add(TEXT("Duration"), TrainingDurationText);
				WindowMessage = FText::Format(LOCTEXT("TrainingSuccess", "Training completed successfully!\n\nTraining time: {Duration}"), SuccessArgs);
				bMarkDirty = true;
			}
		}
		break;

		// User aborted the training process. Ask whether they want to use the partially trained results or not.
		case ETrainingResult::Aborted:
		{
			const FText Title(LOCTEXT("TrainingAbortedMessageTitle", "Use partially trained network?"));
			const EAppReturnType::Type ReturnType = FMessageDialog::Open(
				EAppMsgType::YesNo, 
				FText(LOCTEXT("TrainingAbortedMessage", "Training has been aborted.\nThe neural network has only been partially trained.\nWould you like to use this partially trained network?")),
				&Title);

			if (ReturnType == EAppReturnType::Yes)
			{
				if (!TryLoadOnnxFile())
				{
					ShowNotification(LOCTEXT("TrainingOnnxLoadFailedPartial", "Training partially completed, but resulting onnx file couldn't be loaded!"), SNotificationItem::ECompletionState::CS_Fail, true);
				}
				else
				{
					ShowNotification(LOCTEXT("PartialTrainingSuccess", "Training partially completed!"), SNotificationItem::ECompletionState::CS_Success, true);
					bMarkDirty = true;
				}
			}
			else
			{
				// Restore the vertex delta mean and scale, as we aborted, and they could have changed when training
				// on a smaller subset of frames/samples. If we don't do this, the mesh will deform incorrectly.
				EditorData->GetDeformerAsset()->VertexDeltaMean = EditorData->VertexDeltaMeanBackup;
				EditorData->GetDeformerAsset()->VertexDeltaScale = EditorData->VertexDeltaScaleBackup;

				ShowNotification(LOCTEXT("TrainingAborted", "Training aborted!"), SNotificationItem::ECompletionState::CS_None, true);
			}
		}
		break;

		// Training aborted but we cannot use the current network.
		case ETrainingResult::AbortedCantUse:
		{
			ShowNotification(LOCTEXT("TrainingAborted", "Training aborted!"), SNotificationItem::ECompletionState::CS_None, true);
			WindowMessage = LOCTEXT("TrainingAbortedCantUse", "Training aborted by user.");
		}
		break;

		// Training data had issues.
		case ETrainingResult::FailOnData:
		{
			GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
			WindowMessage = LOCTEXT("TrainingFailedOnData", "Training failed!\nCheck input parameters or sequence length.");
		}
		break;

		// Unknown failure, probably something in the python code.
		case ETrainingResult::FailUnknown:
		{
			GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
			WindowMessage = LOCTEXT("TrainingFailedUnknown", "Training failed!\nUnknown error, please check the output log.");
		}
		break;

		// Unhandled error codes.
		default: check(false);
	}

	// Show a message window.
	if (!WindowMessage.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, WindowMessage, &WindowTitle);
	}

	return bMarkDirty;
}

bool FMLDeformerEditorToolkit::TryLoadOnnxFile() const
{
	FString OnnxFile = FPaths::ProjectIntermediateDir() + "MLDeformerModels/latest_net_G.onnx";
	OnnxFile = FPaths::ConvertRelativePathToFull(OnnxFile);
	if (FPaths::FileExists(OnnxFile))
	{
		UE_LOG(LogMLDeformer, Display, TEXT("Loading Onnx file '%s'..."), *OnnxFile);
		UNeuralNetwork* Network = NewObject<UNeuralNetwork>(EditorData->GetDeformerAsset(), UNeuralNetwork::StaticClass());		
		if (Network->Load(OnnxFile))
		{
			Network->SetDeviceType(/*DeviceType*/ENeuralDeviceType::GPU, /*InputDeviceType*/ENeuralDeviceType::CPU, /*OutputDeviceType*/ENeuralDeviceType::GPU);
			
			EditorData->GetDeformerAsset()->SetInferenceNeuralNetwork(Network);

			// Recreate the data providers.
			// This is needed for the neural network GPU buffers to be valid.
			SetComputeGraphDataProviders();

			UE_LOG(LogMLDeformer, Display, TEXT("Successfully loaded Onnx file '%s'..."), *OnnxFile);
			return true;
		}
		else
		{
			UE_LOG(LogMLDeformer, Error, TEXT("Failed to load Onnx file '%s'"), *OnnxFile);
		}
	}
	else
	{
		UE_LOG(LogMLDeformer, Error, TEXT("Onnx file '%s' does not exist!"), *OnnxFile);
	}

	return false;
}

FName FMLDeformerEditorToolkit::GetToolkitFName() const
{
	return FName("MLDeformerEditor");
}

FText FMLDeformerEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("MLDeformerEditorAppLabel", "ML Deformer Editor");
}

void FMLDeformerEditorToolkit::ShowNotification(const FText& Message, SNotificationItem::ECompletionState State, bool PlaySound) const
{
	FNotificationInfo Info(Message);
	Info.FadeInDuration = 0.1f;
	Info.FadeOutDuration = 0.5f;
	Info.ExpireDuration = 3.5f;
	Info.bUseThrobber = false;
	Info.bUseSuccessFailIcons = true;
	Info.bUseLargeFont = true;
	Info.bFireAndForget = true;
	Info.bAllowThrottleWhenFrameRateIsLow = false;
	auto NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	NotificationItem->SetCompletionState(State);
	NotificationItem->ExpireAndFadeout();

	if (PlaySound)
	{
		switch (State)
		{
			case SNotificationItem::ECompletionState::CS_Success: GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue")); break;
			case SNotificationItem::ECompletionState::CS_Fail: GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue")); break;
			case SNotificationItem::ECompletionState::CS_Pending: GEditor->PlayEditorSound(TEXT("/Engine/EditorSounds/Notifications/CompileStart_Cue.CompileStart_Cue")); break;
			default:;
		};
	}
}

FText FMLDeformerEditorToolkit::GetToolkitName() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(EditorData->GetDeformerAsset()->GetName()));
	return FText::Format(LOCTEXT("DemoEditorToolkitName", "{AssetName}"), Args);
}

FLinearColor FMLDeformerEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FMLDeformerEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("MLDeformerEditor");
}

void FMLDeformerEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	UMLDeformerAsset* Asset = EditorData->GetDeformerAsset();
	Collector.AddReferencedObject(Asset);
}

TStatId FMLDeformerEditorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FMLDeformerEditorToolkit, STATGROUP_Tickables);
}

UTextRenderComponent* FMLDeformerEditorToolkit::CreateLabelForActor(AActor* Actor, UWorld* World, FLinearColor Color, const FText& Text) const
{
	const float DefaultLabelScale = FMLDeformerEditorStyle::Get().GetFloat("MLDeformer.DefaultLabelScale");
	UTextRenderComponent* TargetLabelComponent = NewObject<UTextRenderComponent>(Actor);
	TargetLabelComponent->SetMobility(EComponentMobility::Movable);
	TargetLabelComponent->SetHorizontalAlignment(EHTA_Center);
	TargetLabelComponent->SetVerticalAlignment(EVRTA_TextCenter);
	TargetLabelComponent->SetText(Text);
	TargetLabelComponent->SetRelativeScale3D(FVector(DefaultLabelScale));
	TargetLabelComponent->SetGenerateOverlapEvents(false);
	TargetLabelComponent->SetCanEverAffectNavigation(false);
	TargetLabelComponent->SetTextRenderColor(Color.ToFColor(true));
	TargetLabelComponent->RegisterComponent();
	return TargetLabelComponent;
}

void FMLDeformerEditorToolkit::CreateSkinnedActor(EMLDeformerEditorActorIndex ActorIndex, const FName& Name, UWorld* World, USkeletalMesh* Mesh, FLinearColor LabelColor, FLinearColor WireframeColor) const
{
	// Spawn the actor.
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = Name;
	AActor* Actor = World->SpawnActor<AActor>(SpawnParams);
	Actor->SetFlags(RF_Transient);

	// Create the skeletal mesh component.
	UDebugSkelMeshComponent* SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
	SkelMeshComponent->SetSkeletalMesh(Mesh);
	Actor->SetRootComponent(SkelMeshComponent);
	SkelMeshComponent->RegisterComponent();
	SkelMeshComponent->SetWireframeMeshOverlayColor(WireframeColor);
	SkelMeshComponent->MarkRenderStateDirty();

	// Register the editor actor.
	FMLDeformerEditorActor EditorActor;
	EditorActor.Actor = Actor;
	EditorActor.SkelMeshComponent = SkelMeshComponent;
	EditorActor.LabelComponent = CreateLabelForActor(Actor, World, LabelColor, FText::FromString(Name.ToString()));
	EditorData->SetEditorActor(ActorIndex, EditorActor);
}

UMLDeformerComponent* FMLDeformerEditorToolkit::AddMLDeformerComponentToActor(EMLDeformerEditorActorIndex ActorIndex)
{
	FMLDeformerEditorActor& EditorActor = EditorData->GetEditorActor(ActorIndex);
	AActor* Actor = EditorActor.Actor;
	UMLDeformerComponent* Component = NewObject<UMLDeformerComponent>(Actor);
	Component->SetDeformerAsset(EditorData->GetDeformerAsset());
	Component->RegisterComponent();
	EditorActor.MLDeformerComponent = Component;
	return Component;
}

void FMLDeformerEditorToolkit::AddMeshDeformerToActor(EMLDeformerEditorActorIndex ActorIndex, UMeshDeformer* MeshDeformer) const
{
	FMLDeformerEditorActor& EditorActor = EditorData->GetEditorActor(ActorIndex);
	USkinnedMeshComponent* SkelMeshComponent = EditorActor.SkelMeshComponent;
	SkelMeshComponent->SetMeshDeformer(MeshDeformer);
}

void FMLDeformerEditorToolkit::CreateBaseActor(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene, const FName& Name, FLinearColor LabelColor, FLinearColor WireframeColor)
{
	UWorld* World = InPersonaPreviewScene->GetWorld();

	// Spawn the linear skinned actor.
	FActorSpawnParameters BaseSpawnParams;
	BaseSpawnParams.Name = Name;
	AAnimationEditorPreviewActor* Actor = World->SpawnActor<AAnimationEditorPreviewActor>(AAnimationEditorPreviewActor::StaticClass(), FTransform::Identity, BaseSpawnParams);
	Actor->SetFlags(RF_Transient);
	InPersonaPreviewScene->SetActor(Actor);

	// Create the preview skeletal mesh component.
	UDebugSkelMeshComponent* SkelMeshComponent = NewObject<UDebugSkelMeshComponent>(Actor);
	SkelMeshComponent->SetWireframeMeshOverlayColor(WireframeColor);
	SkelMeshComponent->MarkRenderStateDirty();

	// Setup an apply an anim instance to the skeletal mesh component.
	UAnimPreviewInstance* AnimPreviewInstance = NewObject<UAnimPreviewInstance>(SkelMeshComponent, TEXT("MLDeformerAnimInstance"));
	SkelMeshComponent->PreviewInstance = AnimPreviewInstance;
	AnimPreviewInstance->InitializeAnimation();

	// Set the skeletal mesh on the component.
	// NOTE: This must be done AFTER setting the AnimInstance so that the correct root anim node is loaded.
	USkeletalMesh* Mesh = EditorData->GetDeformerAsset()->GetSkeletalMesh();
	SkelMeshComponent->SetSkeletalMesh(Mesh);

	// Apply mesh to the preview scene.
	InPersonaPreviewScene->SetPreviewMeshComponent(SkelMeshComponent);
	InPersonaPreviewScene->AddComponent(SkelMeshComponent, FTransform::Identity);
	InPersonaPreviewScene->SetAdditionalMeshesSelectable(false);
	InPersonaPreviewScene->SetPreviewMesh(Mesh);

	// Register the editor actor.
	FMLDeformerEditorActor BaseEditorActor;
	BaseEditorActor.Actor = Actor;
	BaseEditorActor.SkelMeshComponent = SkelMeshComponent;
	BaseEditorActor.LabelComponent = CreateLabelForActor(Actor, World, LabelColor, FText::FromString(Name.ToString()));
	EditorData->SetEditorActor(EMLDeformerEditorActorIndex::Base, BaseEditorActor);
}

void FMLDeformerEditorToolkit::CreateGeomCacheActor(EMLDeformerEditorActorIndex ActorIndex, UWorld* World, const FName& Name, UGeometryCache* GeomCache, FLinearColor LabelColor, FLinearColor WireframeColor)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = Name;
	AActor* Actor = World->SpawnActor<AActor>(SpawnParams);
	Actor->SetFlags(RF_Transient);

	// Create the Geometry Cache Component.
	UGeometryCacheComponent* GeomCacheComponent = NewObject<UGeometryCacheComponent>(Actor);
	GeomCacheComponent->SetGeometryCache(GeomCache);
	GeomCacheComponent->RegisterComponent();
	GeomCacheComponent->SetOverrideWireframeColor(true);
	GeomCacheComponent->SetWireframeOverrideColor(WireframeColor);
	GeomCacheComponent->MarkRenderStateDirty();
	Actor->SetRootComponent(GeomCacheComponent);

	UTextRenderComponent* TargetLabelComponent = CreateLabelForActor(Actor, World, LabelColor, FText::FromString(Name.ToString()));

	// Register the editor actor.
	FMLDeformerEditorActor EditorActor;
	EditorActor.Actor = Actor;
	EditorActor.GeomCacheComponent = GeomCacheComponent;
	EditorActor.LabelComponent = TargetLabelComponent;
	EditorData->SetEditorActor(ActorIndex, EditorActor);
}

void FMLDeformerEditorToolkit::HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
{
	// Load the default ML deformer graph asset.
	EditorData->SetDefaultDeformerGraphIfNeeded();

	// Set the world.
	UWorld* World = InPersonaPreviewScene->GetWorld();
	EditorData->SetWorld(World);

	// Create the Linear Skinned (Base) actor.
	const FLinearColor BaseLabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.BaseMesh.LabelColor");
	const FLinearColor BaseWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.BaseMesh.WireframeColor");
	CreateBaseActor(InPersonaPreviewScene, "Training Base", BaseLabelColor, BaseWireColor);

	// Create the target actor (with Geometry Cache).
	const FLinearColor TargetLabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.TargetMesh.LabelColor");
	const FLinearColor TargetWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.TargetMesh.WireframeColor");
	CreateGeomCacheActor(
		EMLDeformerEditorActorIndex::Target, 
		World, 
		"Training Target", 
		EditorData->GetDeformerAsset()->GetGeometryCache(), 
		TargetLabelColor,
		TargetWireColor);

	// Create the linear skinned actor.
	USkeletalMesh* Mesh = EditorData->GetDeformerAsset()->GetSkeletalMesh();
	CreateSkinnedActor(EMLDeformerEditorActorIndex::Test, "Linear Skinned", World, Mesh, BaseLabelColor, BaseWireColor);

	// Create the ML deformed actor.
	const FLinearColor MLDeformedLabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.MLDeformedMesh.LabelColor");
	const FLinearColor MLDeformedWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.MLDeformedMesh.WireframeColor");
	CreateSkinnedActor(EMLDeformerEditorActorIndex::DeformedTest, "ML Deformed", World, Mesh, MLDeformedLabelColor, MLDeformedWireColor);
	UDebugSkelMeshComponent* SkelMeshComponent = EditorData->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).SkelMeshComponent;
	UMLDeformerComponent* MLDeformerComponent = AddMLDeformerComponentToActor(EMLDeformerEditorActorIndex::DeformedTest);
	MLDeformerComponent->SetupComponent(EditorData->GetDeformerAsset(), SkelMeshComponent);
	SkelMeshComponent->SkinCacheUsage.SetNum(SkelMeshComponent->LODInfo.Num());
	for (int32 Index = 0; Index < SkelMeshComponent->LODInfo.Num(); ++Index)
	{
		SkelMeshComponent->SkinCacheUsage[Index] = ESkinCacheUsage::Enabled;
	}

	// Create the component with the deformer graph on it.
	UMLDeformerVizSettings* VizSettings = EditorData->GetDeformerAsset()->GetVizSettings();
	UMeshDeformer* MeshDeformer = VizSettings->GetDeformerGraph();
	AddMeshDeformerToActor(EMLDeformerEditorActorIndex::DeformedTest, MeshDeformer);

	// Create the ground truth actor.
	const FLinearColor GroundTruthLabelColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.GroundTruth.LabelColor");
	const FLinearColor GroundTruthWireColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.GroundTruth.WireframeColor");
	CreateGeomCacheActor(
		EMLDeformerEditorActorIndex::GroundTruth, 
		World, 
		"Ground Truth", 
		EditorData->GetDeformerAsset()->GetGeometryCache(), 
		GroundTruthLabelColor,
		GroundTruthWireColor);

	// Create visualization assets.
	EditorData->CreateHeatMapAssets();
	EditorData->SetHeatMapMaterialEnabled(EditorData->GetDeformerAsset()->GetVizSettings()->GetShowHeatMap());

	// Start playing the animations.
	EditorData->InitAssets();

	VizSettings->SetTempVisualizationMode(VizSettings->GetVisualizationMode());
	EditorData->GetDeformerAsset()->SetTempTrainingInputs(EditorData->GetDeformerAsset()->GetTrainingInputs());
	OnSwitchedVisualizationMode();
}

void FMLDeformerEditorToolkit::HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView)
{
	EditorData->SetDetailsView(InDetailsView);
	InDetailsView->OnFinishedChangingProperties().AddSP(this, &FMLDeformerEditorToolkit::OnFinishedChangingDetails);
	InDetailsView->SetObject(EditorData->GetDeformerAsset());
}

void FMLDeformerEditorToolkit::OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent)
{
	const FProperty* Property = PropertyChangedEvent.Property;
	if (Property == nullptr)
	{
		return;
	}

	// When we change one of these properties below, restart animations etc.
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, SkeletalMesh))
	{
		EditorData->InitAssets();

		SetComputeGraphDataProviders();

		FMLDeformerEditorActor& DeformedTestActor = EditorData->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest);
		USkeletalMeshComponent* SkelMeshComponent = DeformedTestActor.SkelMeshComponent;
		UMLDeformerComponent* MLDeformerComponent = DeformedTestActor.MLDeformerComponent;

		MLDeformerComponent->SetupComponent(EditorData->GetDeformerAsset(), SkelMeshComponent);

		EditorData->GetDetailsView()->ForceRefresh();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, GeometryCache) ||
		Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, AnimSequence) ||
		Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, TestAnimSequence) ||
		Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, GroundTruth))
	{
		EditorData->InitAssets();
		EditorData->GetDetailsView()->ForceRefresh();
		EditorData->GetVizSettingsDetailsView()->ForceRefresh();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, DeltaCutoffLength) ||
	    Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, AlignmentTransform) ||
	    Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, MaxTrainingFrames))
	{
		EditorData->InitAssets();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, TrainingInputs))
	{
		UMLDeformerAsset* DeformerAsset = EditorData->GetDeformerAsset();
		if (DeformerAsset->GetTempTrainingInputs() != DeformerAsset->GetTrainingInputs())
		{
			DeformerAsset->SetTempTrainingInputs(DeformerAsset->GetTrainingInputs());
			EditorData->UpdateIsReadyForTrainingState();
			EditorData->GetDetailsView()->ForceRefresh();
		}
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerAsset, NoiseAmount))
	{
		EditorData->GetDetailsView()->ForceRefresh();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, AnimPlaySpeed))
	{
		EditorData->UpdateTestAnimPlaySpeed();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, DeformerGraph))
	{
		SetComputeGraphDataProviders();
		EditorData->GetVizSettingsDetailsView()->ForceRefresh();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bShowHeatMap))
	{
		EditorData->SetHeatMapMaterialEnabled(EditorData->GetDeformerAsset()->GetVizSettings()->GetShowHeatMap());
		EditorData->UpdateDeformerGraph();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, VisualizationMode))
	{
		OnSwitchedVisualizationMode();
	}
	else
	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawLinearSkinnedActor) ||
		Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawMLDeformedActor) ||
		Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerVizSettings, bDrawGroundTruthActor))
	{
		UpdateActorVisibility();
	}
}


void FMLDeformerEditorToolkit::UpdateActorVisibility()
{
	UMLDeformerAsset* DeformerAsset = EditorData->GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();

	// Change visibility.
	const bool bShowTrainingData = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData);
	const bool bShowTestData = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData);
	EditorData->SetActorVisibility(EMLDeformerEditorActorIndex::Base, bShowTrainingData);
	EditorData->SetActorVisibility(EMLDeformerEditorActorIndex::Target, bShowTrainingData);
	EditorData->SetActorVisibility(EMLDeformerEditorActorIndex::Test, bShowTestData && VizSettings->GetDrawLinearSkinnedActor());
	EditorData->SetActorVisibility(EMLDeformerEditorActorIndex::DeformedTest, bShowTestData && VizSettings->GetDrawMLDeformedActor());
	EditorData->SetActorVisibility(EMLDeformerEditorActorIndex::GroundTruth, bShowTestData && VizSettings->GetDrawGroundTruthActor());
}

void FMLDeformerEditorToolkit::OnSwitchedVisualizationMode()
{
	UMLDeformerAsset* DeformerAsset = EditorData->GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();

	UpdateActorVisibility();

	// Only trigger a force refresh when the value really changed.
	// This is required due to a bug in the UI system that triggers a changed event already when clicking the combo box.
	if (VizSettings->GetTempVisualizationMode() != VizSettings->GetVisualizationMode())
	{
		EditorData->GetVizSettingsDetailsView()->ForceRefresh();
		VizSettings->SetTempVisualizationMode(VizSettings->GetVisualizationMode());
	}

	// Make sure the time slider is updated to reflect the right animation range.
	EditorData->UpdateTimeSlider();
}

void FMLDeformerEditorToolkit::HandleViewportCreated(const TSharedRef<IPersonaViewport>& InPersonaViewport)
{
	InPersonaViewport->AddOverlayWidget(
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(FMargin(5.0f, 40.0f))
		[
			SNew(STextBlock)
			.Text(this, &FMLDeformerEditorToolkit::GetOverlayText)
			.Visibility(EVisibility::HitTestInvisible)
			.ColorAndOpacity(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f))
		]
	);
}

FText FMLDeformerEditorToolkit::GetOverlayText() const
{
	if (EditorData == nullptr)
	{
		return FText::GetEmpty();
	}

	return EditorData->GetOverlayText();
}

void FMLDeformerEditorToolkit::SetComputeGraphDataProviders() const
{
	UMLDeformerVizSettings* VizSettings = EditorData->GetDeformerAsset()->GetVizSettings();
	AddMeshDeformerToActor(EMLDeformerEditorActorIndex::DeformedTest, VizSettings->GetDeformerGraph());
}

#undef LOCTEXT_NAMESPACE
