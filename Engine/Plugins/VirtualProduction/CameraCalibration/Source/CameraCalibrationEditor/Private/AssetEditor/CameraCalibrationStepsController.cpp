// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationStepsController.h"

#include "AssetRegistry/AssetData.h"
#include "CalibrationPointComponent.h"
#include "Camera/CameraActor.h"
#include "CameraCalibrationEditorLog.h"
#include "CameraCalibrationStep.h"
#include "CameraCalibrationSubsystem.h"
#include "CameraCalibrationToolkit.h"
#include "CameraCalibrationTypes.h"
#include "CineCameraActor.h"
#include "CompElementEditorModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "CompositingCaptureBase.h"
#include "CompositingElement.h"
#include "CompositingElements/CompositingElementInputs.h"
#include "CompositingElements/CompositingElementOutputs.h"
#include "CompositingElements/CompositingElementPasses.h"
#include "CompositingElements/CompositingElementTransforms.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/UserDefinedEnum.h"
#include "EngineUtils.h"
#include "ICompElementManager.h"
#include "Input/Events.h"
#include "LensFile.h"
#include "LiveLinkCameraController.h"
#include "LiveLinkComponentController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "MediaSource.h"
#include "Misc/MessageDialog.h"
#include "Models/SphericalLensModel.h"
#include "Modules/ModuleManager.h"
#include "Profile/IMediaProfileManager.h"
#include "Profile/MediaProfile.h"
#include "SCameraCalibrationSteps.h"
#include "TimeSynchronizableMediaSource.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"


#if WITH_OPENCV
#include "OpenCVHelper.h"
OPENCV_INCLUDES_START
#undef check 
#include "opencv2/opencv.hpp"
OPENCV_INCLUDES_END
#endif


#define LOCTEXT_NAMESPACE "CameraCalibrationStepsController"

namespace CameraCalibrationStepsController
{
	/** Returns the ICompElementManager object */
	ICompElementManager* GetCompElementManager()
	{
		static ICompElementEditorModule* EditorModule = FModuleManager::Get().GetModulePtr<ICompElementEditorModule>("ComposureLayersEditor");

		TSharedPtr<ICompElementManager> CompElementManager;

		if (EditorModule && (CompElementManager = EditorModule->GetCompElementManager()).IsValid())
		{
			return CompElementManager.Get();
		}
		return nullptr;
	}

	/** Retrieves the latest lens evaluation data from the LiveLink controller in the given camera for the given lens file */
	const FLensFileEvalData* LensFileEvalDataFromCamera(const ACameraActor* InCamera, const ULensFile* InLensFile)
	{
		if (!InCamera || !InLensFile)
		{
			return nullptr;
		}

		TArray<ULiveLinkComponentController*> LLComponentControllers;
		InCamera->GetComponents<ULiveLinkComponentController>(LLComponentControllers);

		for (const ULiveLinkComponentController* LLComponentController : LLComponentControllers)
		{
			for (const TPair<TSubclassOf<ULiveLinkRole>, ULiveLinkControllerBase*> Pair : LLComponentController->ControllerMap)
			{
				if (ULiveLinkCameraController* CameraController = Cast<ULiveLinkCameraController>(Pair.Value))
				{
					const FLensFileEvalData* OutLensFileEvalData = &CameraController->GetLensFileEvalDataRef();

					if (OutLensFileEvalData->LensFile == InLensFile)
					{
						return OutLensFileEvalData;
					}
				}
			}
		}

		return nullptr;
	}
}


FCameraCalibrationStepsController::FCameraCalibrationStepsController(TWeakPtr<FCameraCalibrationToolkit> InCameraCalibrationToolkit, ULensFile* InLensFile)
	: CameraCalibrationToolkit(InCameraCalibrationToolkit)
	, LensFile(TWeakObjectPtr<ULensFile>(InLensFile))
{
	check(CameraCalibrationToolkit.IsValid());
	check(LensFile.IsValid());

	TickerHandle = FTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FCameraCalibrationStepsController::OnTick), 0.0f);
}

FCameraCalibrationStepsController::~FCameraCalibrationStepsController()
{
	if (TickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

	Cleanup();
}

void FCameraCalibrationStepsController::CreateSteps()
{
	// Ask subsystem for the registered calibration steps.

	const UCameraCalibrationSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();

	if (!Subsystem)
	{
		UE_LOG(LogCameraCalibrationEditor, Error, TEXT("Could not find UCameraCalibrationSubsystem"));
		return;
	}

	const TArray<FName> StepNames = Subsystem->GetCameraCalibrationSteps();

	// Create the steps

	for (const FName& StepName : StepNames)
	{
		const TSubclassOf<UCameraCalibrationStep> StepClass = Subsystem->GetCameraCalibrationStep(StepName);

		UCameraCalibrationStep* const Step = NewObject<UCameraCalibrationStep>(
			GetTransientPackage(),
			StepClass,
			MakeUniqueObjectName(GetTransientPackage(), StepClass));

		check(Step);

		Step->Initialize(SharedThis(this));

		CalibrationSteps.Add(TStrongObjectPtr<UCameraCalibrationStep>(Step));
	}

	// Sort them according to prerequisites
	//
	// We iterate from the left and move to the right-most existing prerequisite, leaving a null behind
	// At the end we remove all the nulls that were left behind.

	for (int32 StepIdx = 0; StepIdx < CalibrationSteps.Num(); ++StepIdx)
	{
		int32 InsertIdx = StepIdx;

		for (int32 PrereqIdx = StepIdx+1; PrereqIdx < CalibrationSteps.Num(); ++PrereqIdx)
		{
			if (CalibrationSteps[StepIdx]->DependsOnStep(CalibrationSteps[PrereqIdx].Get()))
			{
				InsertIdx = PrereqIdx + 1;
			}
		}

		if (InsertIdx != StepIdx)
		{
			const TStrongObjectPtr<UCameraCalibrationStep> DependentStep = CalibrationSteps[StepIdx];
			CalibrationSteps.Insert(DependentStep, InsertIdx);

			// Invalidate the pointer left behind. This entry will be removed a bit later.
			CalibrationSteps[StepIdx].Reset();
		}
	}

	// Remove the nulled out ones

	for (int32 StepIdx = 0; StepIdx < CalibrationSteps.Num(); ++StepIdx)
	{
		if (!CalibrationSteps[StepIdx].IsValid())
		{
			CalibrationSteps.RemoveAt(StepIdx);
			StepIdx--;
		}
	}
}

TSharedPtr<SWidget> FCameraCalibrationStepsController::BuildUI()
{
	return SNew(SCameraCalibrationSteps, SharedThis(this));
}

bool FCameraCalibrationStepsController::OnTick(float DeltaTime)
{
	// Update the lens file eval data
	LensFileEvalData = CameraCalibrationStepsController::LensFileEvalDataFromCamera(Camera.Get(), LensFile.Get());

	for (TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationSteps)
	{
		if (Step.IsValid())
		{
			Step->Tick(DeltaTime);
		}
	}

	return true;
}

TWeakObjectPtr<ACompositingElement> FCameraCalibrationStepsController::AddElement(ACompositingElement* Parent, FString& ClassPath, FString& ElementName) const
{
	UClass* ElementClass = StaticLoadClass(ACompositingElement::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_None, nullptr);

	if (!ElementClass)
	{
		return nullptr;
	}

	ICompElementManager* CompElementManager = CameraCalibrationStepsController::GetCompElementManager();

	if (!CompElementManager)
	{
		return nullptr;
	}

	TWeakObjectPtr<ACompositingElement> Element = CompElementManager->CreateElement(
		*ElementName, ElementClass, nullptr, EObjectFlags::RF_Transient | EObjectFlags::RF_DuplicateTransient);

	if (!Element.IsValid())
	{
		return nullptr;
	}

	if (Parent)
	{
		// Attach layer to parent
		Parent->AttachAsChildLayer(Element.Get());

		// Place element under Parent to keep things organized.
		Element->AttachToActor(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}

	return Element;
}


ACompositingElement* FCameraCalibrationStepsController::FindElement(const FString& Name) const
{
	// The motivation of finding them is if the level was saved with these objects.
	// A side effect is that we'll destroy them when closing the calibrator.

	const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;

	for (TActorIterator<AActor> It(GetWorld(), ACompositingElement::StaticClass(), Flags); It; ++It)
	{
		if (It->GetName() == Name)
		{
			return CastChecked<ACompositingElement>(*It);
		}
	}

	return nullptr;
}

void FCameraCalibrationStepsController::Cleanup()
{
	for (TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationSteps)
	{
		if (Step.IsValid())
		{
			Step->Shutdown();
			Step.Reset();
		}
	}

	if (MediaPlayer.IsValid())
	{
		MediaPlayer->Close();
		MediaPlayer.Reset();
	}

	if (Comp.IsValid()) // It may have been destroyed manually by the user.
	{
		Comp->Destroy();
		Comp.Reset();
	}

	if (CGLayer.IsValid())
	{
		CGLayer->Destroy();
		CGLayer.Reset();
	}

	if (MediaPlate.IsValid())
	{
		MediaPlate->Destroy();
		MediaPlate.Reset();
	}

	MaterialPass.Reset();
	Camera.Reset();

	MediaPlateRenderTarget.Reset();
}

FString FCameraCalibrationStepsController::NamespacedName(const FString&& Name) const
{
	return FString::Printf(TEXT("CameraCalib_%s_%s"), *LensFile->GetName(), *Name);
}

UWorld* FCameraCalibrationStepsController::GetWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

UTextureRenderTarget2D* FCameraCalibrationStepsController::GetRenderTarget() const
{
	return RenderTarget.Get();
}

void FCameraCalibrationStepsController::CreateComp()
{
	// Don't do anything if we already created it.
	if (Comp.IsValid())
	{
		// Some items are exposed in the World Outliner, and they will get cleaned up and re-created
		// when the configurator is closed and re-opened.

		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Tried to create Comp that already existed"));
		return;
	}

	ICompElementManager* CompElementManager = CameraCalibrationStepsController::GetCompElementManager();

	if (!CompElementManager)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Could not find ICompElementManager"));
		return;
	}

	// Create Comp parent
	{
		const FString CompName = NamespacedName(TEXT("Comp"));

		Comp = FindElement(CompName);

		if (!Comp.IsValid())
		{
			Comp = CompElementManager->CreateElement(
				*CompName, ACompositingElement::StaticClass(), nullptr, EObjectFlags::RF_Transient | EObjectFlags::RF_DuplicateTransient);
		}

		if (!Comp.IsValid())
		{
			UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create 'Comp'"));
			Cleanup();
			return;
		}
	}

	// Convenience function to add comp elements
	auto FindOrAddElementToComp = [&](
		TWeakObjectPtr<ACompositingElement>& Element,
		FString&& ElementClassPath,
		FString&& ElementName) -> bool
	{
		if (Element.IsValid())
		{
			return true;
		}

		Element = FindElement(ElementName);

		if (!Element.IsValid())
		{
			Element = AddElement(Comp.Get(), ElementClassPath, ElementName);
		}

		return Element.IsValid();
	};

	if (!FindOrAddElementToComp(
		CGLayer,
		TEXT("/Composure/Blueprints/CompositingElements/BP_CgCaptureCompElement.BP_CgCaptureCompElement_C"),
		NamespacedName(TEXT("CG"))))
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create 'CG' Layer"));
		Cleanup();
		return;
	}

	if (!FindOrAddElementToComp(
		MediaPlate,
		TEXT("/Composure/Blueprints/CompositingElements/BP_MediaPlateCompElement.BP_MediaPlateCompElement_C"),
		NamespacedName(TEXT("MediaPlate"))))
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create 'MediaPlate'"));
		Cleanup();
		return;
	}

	// This updates the Composure panel view
	CompElementManager->RefreshElementsList();

	// Disable fog on scene capture component of CGLayer
	{
		TArray<USceneCaptureComponent2D*> CaptureComponents;
		CGLayer->GetComponents<USceneCaptureComponent2D>(CaptureComponents);

		for (USceneCaptureComponent2D* CaptureComponent : CaptureComponents)
		{
			// We need to disable Fog via the ShowFlagSettings property instead of directly in ShowFlags,
			// because it will likely be overwritten since they are often replaced by the archetype defaults
			// and then updated by the ShowFlagSettings.

			FEngineShowFlagsSetting NewFlagSetting;
			NewFlagSetting.ShowFlagName = FEngineShowFlags::FindNameByIndex(FEngineShowFlags::EShowFlag::SF_Fog);
			NewFlagSetting.Enabled = false;

			CaptureComponent->ShowFlagSettings.Add(NewFlagSetting);

			if (FProperty* ShowFlagSettingsProperty = CaptureComponent->GetClass()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(USceneCaptureComponent2D, ShowFlagSettings)))
			{
				// This PostEditChange will ensure that ShowFlags is updated.
				FPropertyChangedEvent PropertyChangedEvent(ShowFlagSettingsProperty);
				CaptureComponent->PostEditChangeProperty(PropertyChangedEvent);
			}
		}
	}

	// Create media player and media texture and assign it to the media input of the media plate layer.
	//
	for (UCompositingElementInput* Input : MediaPlate->GetInputsList())
	{
		UMediaTextureCompositingInput* MediaInput = Cast<UMediaTextureCompositingInput>(Input);

		if (!MediaInput)
		{
			continue;
		}

		// Create MediaPlayer

		// Using a strong reference prevents the MediaPlayer from going stale when the level is saved.
		MediaPlayer = TStrongObjectPtr<UMediaPlayer>(NewObject<UMediaPlayer>(
			GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UMediaPlayer::StaticClass())
			));

		if (!MediaPlayer.IsValid())
		{
			UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create MediaPlayer"));
			Cleanup();
			return;
		}

		MediaPlayer->PlayOnOpen = true;

		// Create MediaTexture

		MediaTexture = NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient);

		if (MediaTexture.IsValid())
		{
			MediaTexture->AutoClear = true;
			MediaTexture->SetMediaPlayer(MediaPlayer.Get());
			MediaTexture->UpdateResource();

			MediaInput->MediaSource = MediaTexture.Get();
		}

		// Play the media source, preferring time-synchronizable sources.
		if (const UMediaProfile* MediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile())
		{
			bool bFoundPreferredMediaSource = false;

			for (int32 MediaSourceIdx = 0; MediaSourceIdx < MediaProfile->NumMediaSources(); ++MediaSourceIdx)
			{
				UMediaSource* MediaSource = MediaProfile->GetMediaSource(MediaSourceIdx);

				if (Cast<UTimeSynchronizableMediaSource>(MediaSource))
				{
					MediaPlayer->OpenSource(MediaSource);
					MediaPlayer->Play();

					bFoundPreferredMediaSource = true;

					// Break since we don't need to look for more MediaSources.
					break;
				}
			}

			if (!bFoundPreferredMediaSource)
			{
				for (int32 MediaSourceIdx = 0; MediaSourceIdx < MediaProfile->NumMediaSources(); ++MediaSourceIdx)
				{
					if (UMediaSource* MediaSource = MediaProfile->GetMediaSource(MediaSourceIdx))
					{
						MediaPlayer->OpenSource(MediaSource);
						MediaPlayer->Play();

						// Break since we don't need to look for more MediaSources.
						break;
					}
				}
			}
		}

		// Break since don't need to look at more UMediaTextureCompositingInputs.
		break;
	}

	// Create material pass that blends MediaPlate with CG.
	//@todo Make this material selectable by the user.

	MaterialPass = CastChecked<UCompositingElementMaterialPass>(
		Comp->CreateNewTransformPass(TEXT("CGOverMedia"), UCompositingElementMaterialPass::StaticClass())
		);

	if (!MaterialPass.IsValid())
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create 'CGOverMedia' UCompositingElementMaterialPass"));
		Cleanup();
		return;
	}

	// Create Material
	const FString MaterialPath = TEXT("/CameraCalibration/Materials/M_SimulcamCalib.M_SimulcamCalib");
	UMaterial* Material = Cast<UMaterial>(FSoftObjectPath(MaterialPath).TryLoad());

	if (!Material)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to load %s"), *MaterialPath);
		Cleanup();
		return;
	}

	MaterialPass->SetMaterialInterface(Material);

	// ActorLabel should coincide with ElementName. Name may not.
	MaterialPass->SetParameterMapping(TEXT("CG"), *CGLayer->GetActorLabel());
	MaterialPass->SetParameterMapping(TEXT("MediaPlate"), *MediaPlate->GetActorLabel());

	URenderTargetCompositingOutput* RTOutput = Cast<URenderTargetCompositingOutput>(Comp->CreateNewOutputPass(
		TEXT("SimulcamCalOutput"),
		URenderTargetCompositingOutput::StaticClass())
		);

	if (!RTOutput)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create URenderTargetCompositingOutput"));
		Cleanup();
		return;
	}

	// Create RenderTarget
	RenderTarget = NewObject<UTextureRenderTarget2D>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass())
		);

	if (!RenderTarget.IsValid())
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create UTextureRenderTarget2D"));
		Cleanup();
		return;
	}

	RenderTarget->RenderTargetFormat = RTF_RGBA16f;
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->bAutoGenerateMips = true;
	RenderTarget->InitAutoFormat(1920, 1080);
	RenderTarget->UpdateResourceImmediate(true);

	// Assign the RT to the compositing output
	RTOutput->RenderTarget = RenderTarget.Get();

	// By default, use a camera with our lens. If not found, let composure pick one.
	if (ACameraActor* CameraGuess = FindFirstCameraWithCurrentLens())
	{
		SetCamera(CameraGuess);
	}
	else
	{
		SetCamera(Comp->FindTargetCamera());
	}
}

void FCameraCalibrationStepsController::CreateMediaPlateOutput()
{
	if (!MediaPlate.IsValid())
	{
		return;
	}

	URenderTargetCompositingOutput* RTOutput = Cast<URenderTargetCompositingOutput>(MediaPlate->CreateNewOutputPass(
		TEXT("MediaPlateOutput"),
		URenderTargetCompositingOutput::StaticClass())
	);

	if (!RTOutput)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create URenderTargetCompositingOutput for the MediaPlateOutput"));
		Cleanup();
		return;
	}

	// Create RenderTarget
	MediaPlateRenderTarget = NewObject<UTextureRenderTarget2D>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass())
	);

	if (!MediaPlateRenderTarget.IsValid())
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Failed to create UTextureRenderTarget2D for the MediaPlateOutput"));
		Cleanup();
		return;
	}
	
	MediaPlateRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	MediaPlateRenderTarget->ClearColor = FLinearColor::Black;
	MediaPlateRenderTarget->bAutoGenerateMips = true;
	MediaPlateRenderTarget->InitAutoFormat(1920, 1080);
	MediaPlateRenderTarget->UpdateResourceImmediate(true);

	// Assign the RT to the compositing output
	RTOutput->RenderTarget = MediaPlateRenderTarget.Get();
}


float FCameraCalibrationStepsController::GetWiperWeight() const
{
	float Weight = 0.5f;

	if (!MaterialPass.IsValid())
	{
		return Weight;
	}

	MaterialPass->Material.GetScalarOverride(TEXT("WiperWeight"), Weight);
	return Weight;
}

void FCameraCalibrationStepsController::SetWiperWeight(float InWeight)
{
	if (MaterialPass.IsValid())
	{
		MaterialPass->Material.SetScalarOverride(TEXT("WiperWeight"), InWeight);
	}
}

void FCameraCalibrationStepsController::SetCamera(ACameraActor* InCamera)
{
	Camera = InCamera;

	// Update the Comp with this camera
	if (Comp.IsValid())
	{
		Comp->SetTargetCamera(InCamera);
		EnableDistortionInCG();
	}
}

void FCameraCalibrationStepsController::EnableDistortionInCG()
{
	if (!Comp.IsValid())
	{
		return;
	}

	UCameraCalibrationSubsystem* SubSystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();

	if (!SubSystem)
	{
		UE_LOG(LogCameraCalibrationEditor, Error, TEXT("Could not find UCameraCalibrationSubsystem"));
		return;
	}

	ACineCameraActor* CineCamera = Cast<ACineCameraActor>(Camera.Get());

	if (!CineCamera)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("No cine camera selected when trying to enable distortion."));
		return;
	}

	// Pick first valid handler that the subsystem finds in our	camera
	//

	ULensDistortionModelHandlerBase* DistortionHandler = nullptr;

	for (ULensDistortionModelHandlerBase* Handler : SubSystem->GetDistortionModelHandlers(CineCamera->GetCineCameraComponent()))
	{
		if (!Handler)
		{
			continue;
		}

		DistortionHandler = Handler;

		break;
	}

	for (ACompositingElement* Element : Comp->GetChildElements())
	{
		ACompositingCaptureBase* CaptureBase = Cast<ACompositingCaptureBase>(Element);

		if (!CaptureBase)
		{
			continue;
		}

		// Enable distortion on the CG compositing layer
		CaptureBase->SetApplyDistortion(true);

		// If a distortion handler exists for the target camera, set it on the CG layer. 
		// If no handlers currently exist, log a warning. At some later time, if a distortion source is created
		// the CG layer will automatically pick it up and start using it.
		if (DistortionHandler)
		{
			CaptureBase->SetDistortionHandler(DistortionHandler);
		}
		else
		{
			UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Could not find a distortion handler in the selected camera"));
		}
	}
}

ACameraActor* FCameraCalibrationStepsController::GetCamera() const
{
	return Camera.Get();
}

void FCameraCalibrationStepsController::OnSimulcamViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bool bStepHandled = false;

	for (TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationSteps)
	{
		if (Step.IsValid() && Step->IsActive())
		{
			bStepHandled |= Step->OnViewportClicked(MyGeometry, MouseEvent);
			break;
		}
	}

	// If a step handled the event, we're done
	if (bStepHandled)
	{
		return;
	}

	// Toggle video pause with right click.
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		TogglePlay();
		return;
	}
}

ULiveLinkCameraController* FCameraCalibrationStepsController::FindLiveLinkCameraController() const
{
	return FindLiveLinkCameraControllerWithLens(Camera.Get(), LensFile.Get());
}

ULiveLinkCameraController* FCameraCalibrationStepsController::FindLiveLinkCameraControllerWithLens(const ACameraActor* InCamera, const ULensFile* InLensFile) const
{
	if (!InCamera || !InLensFile)
	{
		return nullptr;
	}

	FAssetData LensFileAssetData(InLensFile);

	TArray<ULiveLinkComponentController*> LiveLinkComponents;
	InCamera->GetComponents<ULiveLinkComponentController>(LiveLinkComponents);

	for (const ULiveLinkComponentController* LiveLinkComponent : LiveLinkComponents)
	{
		for (auto It = LiveLinkComponent->ControllerMap.CreateConstIterator(); It; ++It)
		{
			ULiveLinkCameraController* CameraController = Cast<ULiveLinkCameraController>(It->Value);

			if (!CameraController)
			{
				continue;
			}

			const ULensFile* CameraLensFile = CameraController->LensFilePicker.GetLensFile();

			if (!CameraLensFile)
			{
				continue;
			}

			if (FAssetData(CameraLensFile, true).PackageName == LensFileAssetData.PackageName)
			{
				return CameraController;
			}
		}
	}

	return nullptr;
}

ACameraActor* FCameraCalibrationStepsController::FindFirstCameraWithCurrentLens() const
{
	// We iterate over all cameras in the scene and try to find one that is using the current LensFile

	for (TActorIterator<ACameraActor> CameraItr(GetWorld()); CameraItr; ++CameraItr)
	{
		ACameraActor* CameraActor = *CameraItr;

		if (FindLiveLinkCameraControllerWithLens(CameraActor, LensFile.Get()))
		{
			return CameraActor;
		}
	}

	return nullptr;
}

void FCameraCalibrationStepsController::TogglePlay()
{
	if (!MediaPlayer.IsValid())
	{
		return;
	}

	//@todo Eventually pause should cache the texture instead of relying on player play/pause support.

	if (IsPaused())
	{
		MediaPlayer->Play();
	}
	else
	{
		MediaPlayer->Pause();
	}
}

void FCameraCalibrationStepsController::Play()
{
	if (!MediaPlayer.IsValid())
	{
		return;
	}

	MediaPlayer->Play();
}

void FCameraCalibrationStepsController::Pause()
{
	if (!MediaPlayer.IsValid())
	{
		return;
	}

	MediaPlayer->Pause();
}

bool FCameraCalibrationStepsController::IsPaused() const
{
	if (MediaPlayer.IsValid())
	{
		return MediaPlayer->IsPaused();
	}

	return true;
}

const FLensFileEvalData* FCameraCalibrationStepsController::GetLensFileEvalData() const
{
	return LensFileEvalData;
}

ULensFile* FCameraCalibrationStepsController::GetLensFile() const
{
	if (LensFile.IsValid())
	{
		return LensFile.Get();
	}

	return nullptr;
}

const ULensDistortionModelHandlerBase* FCameraCalibrationStepsController::GetDistortionHandler() const
{
	if (!CGLayer.IsValid())
	{
		return nullptr;
	}

	ACompositingCaptureBase* CaptureBase = Cast<ACompositingCaptureBase>(CGLayer.Get());

	if (!CaptureBase)
	{
		return nullptr;
	}

	return CaptureBase->GetDistortionHandler();
}

bool FCameraCalibrationStepsController::SetMediaSourceUrl(const FString& InMediaSourceUrl)
{
	const UMediaProfile* MediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();

	if (!MediaProfile || !MediaPlayer.IsValid())
	{
		return false;
	}

	// If we're already playing it, we're done
	if (InMediaSourceUrl == GetMediaSourceUrl())
	{
		return true;
	}

	if (InMediaSourceUrl == TEXT("None") || !InMediaSourceUrl.Len())
	{
		MediaPlayer->Close();
		return true;
	}

	for (int32 MediaSourceIdx = 0; MediaSourceIdx < MediaProfile->NumMediaSources(); ++MediaSourceIdx)
	{
		UMediaSource* MediaSource = MediaProfile->GetMediaSource(MediaSourceIdx);

		if (!MediaSource || (MediaSource->GetUrl() != InMediaSourceUrl))
		{
			continue;
		}

		MediaPlayer->OpenSource(MediaSource);
		MediaPlayer->Play();

		return true;
	}

	return false;
}

/** Gets the current media source url being played. Empty if None */
FString FCameraCalibrationStepsController::GetMediaSourceUrl() const
{
	if (!MediaPlayer.IsValid())
	{
		return TEXT("");
	}

	return MediaPlayer->GetUrl();
}

void FCameraCalibrationStepsController::FindMediaSourceUrls(TArray<TSharedPtr<FString>>& OutMediaSourceUrls) const
{
	const UMediaProfile* MediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();

	if (!MediaProfile)
	{
		return;
	}

	for (int32 MediaSourceIdx = 0; MediaSourceIdx < MediaProfile->NumMediaSources(); ++MediaSourceIdx)
	{
		if (const UMediaSource* MediaSource = MediaProfile->GetMediaSource(MediaSourceIdx))
		{
			OutMediaSourceUrls.Add(MakeShareable(new FString(MediaSource->GetUrl())));
		}
	}
}

const FLensFileEvalData* FCameraCalibrationStepsController::GetLensFileEvalData()
{
	return LensFileEvalData;
}

const TConstArrayView<TStrongObjectPtr<UCameraCalibrationStep>> FCameraCalibrationStepsController::GetCalibrationSteps() const
{
	return TConstArrayView<TStrongObjectPtr<UCameraCalibrationStep>>(CalibrationSteps);
}

void FCameraCalibrationStepsController::SelectStep(const FName& Name)
{
	for (TStrongObjectPtr<UCameraCalibrationStep>& Step : CalibrationSteps)
	{
		if (Step.IsValid())
		{
			if (Name == Step->FriendlyName())
			{
				Step->Activate();
			}
			else
			{
				Step->Deactivate();
			}
		}
	}
}

void FCameraCalibrationStepsController::Initialize()
{
	// Not doing these in the constructor so that SharedThis can be used.

	CreateComp();
	CreateMediaPlateOutput();
	CreateSteps();
}

UTextureRenderTarget2D* FCameraCalibrationStepsController::GetMediaPlateRenderTarget() const
{
	return MediaPlateRenderTarget.Get();
}


bool FCameraCalibrationStepsController::CalculateNormalizedMouseClickPosition(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FVector2D& OutPosition) const
{
	// Reject viewports with no area
	if (FMath::IsNearlyZero(MyGeometry.Size.X) || FMath::IsNearlyZero(MyGeometry.Size.Y))
	{
		return false;
	}

	// About the Mouse Event data:
	// 
	// * MouseEvent.GetScreenSpacePosition(): Position in pixels on the screen (independent of window size of position)
	// * MyGeometry.Size                    : Size of viewport (the one with the media, not the whole window)
	// * MyGeometry.AbsolutePosition        : Position of the top-left corner of viewport within screen
	// * MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()) gives you the pixel coordinates local to the viewport.

	const FVector2D LocalInPixels = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	const float XNormalized = LocalInPixels.X / MyGeometry.Size.X;
	const float YNormalized = LocalInPixels.Y / MyGeometry.Size.Y;

	// Position 0~1. Origin at top-left corner of the viewport.
	OutPosition = FVector2D(XNormalized, YNormalized);

	return true;
}

bool FCameraCalibrationStepsController::ReadMediaPixels(TArray<FColor>& Pixels, FIntPoint& Size, ETextureRenderTargetFormat& PixelFormat, FText& OutErrorMessage) const
{
	// Get the media plate texture render target 2d

	if (!MediaPlateRenderTarget.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidMediaPlateRenderTarget", "Invalid MediaPlateRenderTarget");
		return false;
	}

	// Extract its render target resource
	FRenderTarget* MediaRenderTarget = MediaPlateRenderTarget->GameThread_GetRenderTargetResource();

	if (!MediaRenderTarget)
	{
		OutErrorMessage = LOCTEXT("InvalidRenderTargetResource", "MediaPlateRenderTarget did not have a RenderTarget resource");
		return false;
	}

	PixelFormat = MediaPlateRenderTarget->RenderTargetFormat;

	// Read the pixels onto CPU
	const bool bReadPixels = MediaRenderTarget->ReadPixels(Pixels);

	if (!bReadPixels)
	{
		OutErrorMessage = LOCTEXT("ReadPixelsFailed", "ReadPixels from render target failed");
		return false;
	}

	Size = MediaRenderTarget->GetSizeXY();

	check(Pixels.Num() == Size.X * Size.Y);

	return true;
}

#undef LOCTEXT_NAMESPACE
