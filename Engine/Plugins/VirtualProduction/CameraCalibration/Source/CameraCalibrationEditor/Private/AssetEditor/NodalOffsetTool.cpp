// Copyright Epic Games, Inc. All Rights Reserved.

#include "NodalOffsetTool.h"

#include "CoreMinimal.h"

#include "AssetRegistry/AssetData.h"
#include "CalibrationPointComponent.h"
#include "Camera/CameraActor.h"
#include "CameraCalibrationEditorLog.h"
#include "CameraCalibrationSubsystem.h"
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
#include "TimeSynchronizableMediaSource.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodalOffsetTool"


namespace NodalOffsetTool
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
			for (const TPair<TSubclassOf<ULiveLinkRole>, ULiveLinkControllerBase*> Pair: LLComponentController->ControllerMap)
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

FNodalOffsetTool::FNodalOffsetTool(ULensFile* InLensFile)
{
	check(InLensFile);
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);

	CreateComp();

	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FNodalOffsetTool::OnTick), 0.0f);
}

FNodalOffsetTool::~FNodalOffsetTool()
{
	if (TickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

	Cleanup();
}

bool FNodalOffsetTool::OnTick(float DeltaTime)
{
	// Update the lens file eval data
	LensFileEvalData = NodalOffsetTool::LensFileEvalDataFromCamera(Camera.Get(), LensFile.Get());

	if (NodalOffsetAlgo.IsValid())
	{
		NodalOffsetAlgo->Tick(DeltaTime);
	}

	return true;
}

TWeakObjectPtr<ACompositingElement> FNodalOffsetTool::AddElement(ACompositingElement* Parent, FString& ClassPath, FString& ElementName) const
{
	UClass* ElementClass = StaticLoadClass(ACompositingElement::StaticClass(), nullptr, *ClassPath, nullptr, LOAD_None, nullptr);

	if (!ElementClass)
	{
		return nullptr;
	}

	ICompElementManager* CompElementManager = NodalOffsetTool::GetCompElementManager();

	if (!CompElementManager)
	{
		return nullptr;
	}

	TWeakObjectPtr<ACompositingElement> Element = CompElementManager->CreateElement(*ElementName, ElementClass, nullptr, GetTransientPackage());

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


ACompositingElement* FNodalOffsetTool::FindElement(const FString& Name) const
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

void FNodalOffsetTool::Cleanup()
{
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

	if (NodalOffsetAlgo.IsValid())
	{
		NodalOffsetAlgo->Shutdown();
		NodalOffsetAlgo->RemoveFromRoot();
		NodalOffsetAlgo.Reset();
	}
}

FString FNodalOffsetTool::NamespacedName(const FString&& Name) const
{
	return FString::Printf(TEXT("CameraCalib_%s_%s"), *LensFile->GetName(), *Name);
}

UWorld* FNodalOffsetTool::GetWorld() const
{
	return GEditor? GEditor->GetEditorWorldContext().World() : nullptr;
}

UTextureRenderTarget2D* FNodalOffsetTool::GetRenderTarget() const
{
	return RenderTarget.Get();
}

void FNodalOffsetTool::CreateComp()
{
	// Don't do anything if we already created it.
	if (Comp.IsValid())
	{
		// Some items are exposed in the World Outliner, and they will get cleaned up and re-created
		// when the configurator is closed and re-opened.
		 
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Tried to create Comp that already existed"));
		return;
	}

	ICompElementManager* CompElementManager = NodalOffsetTool::GetCompElementManager();

	if (!CompElementManager)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Could not find ICompElementManager"));
		return;
	}

	// Create Comp parent
	{
		const FString CompName = NamespacedName(TEXT("Comp"));

		Comp = FindElement(CompName);

		if(!Comp.IsValid())
		{
			Comp = CompElementManager->CreateElement(*CompName, ACompositingElement::StaticClass(), nullptr, GetTransientPackage());
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

		if(!Element.IsValid())
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

		MediaPlayer->PlayOnOpen = false;

		// Create MediaTexture

		MediaTexture = NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient);

		if (MediaTexture.IsValid())
		{
			MediaTexture->AutoClear = true;
			MediaTexture->SetMediaPlayer(MediaPlayer.Get());
			MediaTexture->UpdateResource();

			MediaInput->MediaSource = MediaTexture.Get();
		}

		// Play the first time-synchronizable media source.
		if (const UMediaProfile* MediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile())
		{
			for (int32 MediaSourceIdx = 0; MediaSourceIdx < MediaProfile->NumMediaSources(); ++MediaSourceIdx)
			{
				if (UTimeSynchronizableMediaSource* TSMediaSource = Cast<UTimeSynchronizableMediaSource>(
					MediaProfile->GetMediaSource(MediaSourceIdx)))
				{
					MediaPlayer->OpenSource(TSMediaSource);
					MediaPlayer->Play();

					// Break since we don't need to look for more MediaSources.
					break;
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

	RenderTarget->RenderTargetFormat = RTF_RGBA8;
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

float FNodalOffsetTool::GetWiperWeight() const
{
	float Weight = 0.5f;

	if (!MaterialPass.IsValid())
	{
		return Weight;
	}

	MaterialPass->Material.GetScalarOverride(TEXT("WiperWeight"), Weight);
	return Weight;
}

void FNodalOffsetTool::SetWiperWeight(float InWeight)
{
	if (MaterialPass.IsValid())
	{
		MaterialPass->Material.SetScalarOverride(TEXT("WiperWeight"), InWeight);
	}
}

void FNodalOffsetTool::SetCamera(ACameraActor* InCamera)
{
	Camera = InCamera;

	// Update the Comp with this camera
	if (Comp.IsValid())
	{
		Comp->SetTargetCamera(InCamera);
		EnableDistortionInCG();
	}
}

void FNodalOffsetTool::EnableDistortionInCG()
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

	if (!DistortionHandler)
	{
		UE_LOG(LogCameraCalibrationEditor, Warning, TEXT("Could not find a distortion handler in the selected camera"));
		return;
	}

	for (ACompositingElement* Element : Comp->GetChildElements())
	{
		ACompositingCaptureBase* CaptureBase = Cast<ACompositingCaptureBase>(Element);

		if (!CaptureBase)
		{
			continue;
		}

		CaptureBase->SetApplyDistortion(true);
		CaptureBase->SetDistortionHandler(DistortionHandler);
	}
}

ACameraActor* FNodalOffsetTool::GetCamera() const
{
	return Camera.Get();
}

void FNodalOffsetTool::OnSimulcamViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!NodalOffsetAlgo.IsValid())
	{
		return;
	}

	// If the algorithm handled the event, we're done
	if (NodalOffsetAlgo->OnViewportClicked(MyGeometry, MouseEvent))
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

ULiveLinkCameraController* FNodalOffsetTool::FindLiveLinkCameraControllerWithLens(const ACameraActor* InCamera, const ULensFile* InLensFile) const
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

ACameraActor* FNodalOffsetTool::FindFirstCameraWithCurrentLens() const
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

void FNodalOffsetTool::TogglePlay()
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

void FNodalOffsetTool::SetNodalOffsetAlgo(const FName& AlgoName)
{
	// Ask subsystem for the selected nodal offset algo class

	UCameraCalibrationSubsystem* Subsystem = GEngine->GetEngineSubsystem<UCameraCalibrationSubsystem>();
	check(Subsystem);

	TSubclassOf<UCameraNodalOffsetAlgo> AlgoClass = Subsystem->GetCameraNodalOffsetAlgo(AlgoName);

	// If it is the same as the existing one, do nothing.
	if (!NodalOffsetAlgo.IsValid() && !AlgoClass)
	{
		return;
	}
	else if (NodalOffsetAlgo.IsValid() && (NodalOffsetAlgo->GetClass() == AlgoClass))
	{
		return;
	}

	// Remove old Algo
	if (NodalOffsetAlgo.IsValid())
	{
		NodalOffsetAlgo->Shutdown();
		NodalOffsetAlgo->RemoveFromRoot();
		NodalOffsetAlgo.Reset();
	}

	// If AlgoClass is none, we're done here.
	if (!AlgoClass)
	{
		return;
	}

	// Create new algo
	NodalOffsetAlgo = NewObject<UCameraNodalOffsetAlgo>(
		GetTransientPackage(),
		AlgoClass,
		MakeUniqueObjectName(GetTransientPackage(), AlgoClass));

	if (NodalOffsetAlgo.IsValid())
	{
		NodalOffsetAlgo->Initialize(SharedThis(this));
		NodalOffsetAlgo->AddToRoot();
	}
}

UCameraNodalOffsetAlgo* FNodalOffsetTool::GetNodalOffsetAlgo() const
{
	if (!NodalOffsetAlgo.IsValid())
	{
		return nullptr;
	}

	return NodalOffsetAlgo.Get();
}

bool FNodalOffsetTool::IsPaused() const
{
	if (MediaPlayer.IsValid())
	{
		return !MediaPlayer->IsPlaying();
	}

	return true;
}

const FLensFileEvalData* FNodalOffsetTool::GetLensFileEvalData() const
{
	return LensFileEvalData;
}

const ULensFile* FNodalOffsetTool::GetLensFile()
{
	if (LensFile.IsValid())
	{
		return LensFile.Get();
	}

	return nullptr;
}

const ULensDistortionModelHandlerBase* FNodalOffsetTool::GetDistortionHandler() const
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

void FNodalOffsetTool::OnSaveCurrentNodalOffset()
{
	UCameraNodalOffsetAlgo* Algo = GetNodalOffsetAlgo();

	if (!Algo)
	{
		FText ErrorMessage = LOCTEXT("NoAlgoFound", "No algo found");
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		return;
	}

	FText ErrorMessage;

	float Focus = 0.0f;
	float Zoom = 0.0f;
	FNodalPointOffset NodalOffset;
	float ReprojectionError;

	if (!Algo->GetNodalOffset(NodalOffset, Focus, Zoom, ReprojectionError, ErrorMessage))
	{
		FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
		return;
	}

	LensFile->AddNodalOffsetPoint(Focus, Zoom, NodalOffset);

	// Force bApplyNodalOffset in the LiveLinkCameraController so that we can see the effect right away
	if (ULiveLinkCameraController* LiveLinkCameraController = FindLiveLinkCameraControllerWithLens(Camera.Get(), LensFile.Get()))
	{
		LiveLinkCameraController->SetApplyNodalOffset(true);
	}

	Algo->OnSavedNodalOffset();
}

bool FNodalOffsetTool::SetMediaSourceUrl(const FString& InMediaSourceUrl)
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
		UTimeSynchronizableMediaSource* TSMediaSource = Cast<UTimeSynchronizableMediaSource>(
			MediaProfile->GetMediaSource(MediaSourceIdx));

		if (!TSMediaSource || TSMediaSource->GetUrl() != InMediaSourceUrl)
		{
			continue;
		}

		MediaPlayer->OpenSource(TSMediaSource);
		MediaPlayer->Play();

		return true;
	}

	return false;
}

/** Gets the current media source url being played. Empty if None */
FString FNodalOffsetTool::GetMediaSourceUrl() const
{
	if (!MediaPlayer.IsValid())
	{
		return TEXT("");
	}

	return MediaPlayer->GetUrl();
}

void FNodalOffsetTool::FindMediaSourceUrls(TArray<TSharedPtr<FString>>& OutMediaSourceUrls) const
{
	const UMediaProfile* MediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();

	if (!MediaProfile)
	{
		return;
	}

	for (int32 MediaSourceIdx = 0; MediaSourceIdx < MediaProfile->NumMediaSources(); ++MediaSourceIdx)
	{
		if (UTimeSynchronizableMediaSource* TSMediaSource = Cast<UTimeSynchronizableMediaSource>(
			MediaProfile->GetMediaSource(MediaSourceIdx)))
		{
			OutMediaSourceUrls.Add(MakeShareable(new FString(TSMediaSource->GetUrl())));
		}
	}
}

#undef LOCTEXT_NAMESPACE
