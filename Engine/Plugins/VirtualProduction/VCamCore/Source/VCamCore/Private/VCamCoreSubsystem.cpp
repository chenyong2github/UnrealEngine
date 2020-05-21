// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamCoreSubsystem.h"

#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkTransformRole.h"

DEFINE_LOG_CATEGORY(LogVCamCore);

FModifierStackEntry::FModifierStackEntry(const FName& InName, const TSubclassOf<UVCamModifier> InModifierClass)
	: Name(InName)
{
	if (InModifierClass)
	{
		Modifier = NewObject<UVCamModifier>(GetTransientPackage(), InModifierClass.Get());
	}
}

UVCamCoreSubsystem::UVCamCoreSubsystem()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& LiveLinkClient = ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkClient.OnLiveLinkTicked().AddUObject(this, &UVCamCoreSubsystem::Tick);
	}

	// Registering the input processor is only valid in the actual subsystem and not the CDO
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		InputProcessor = MakeShared<FEditorInputProcessor>();
		if (FSlateApplication::IsInitialized())
		{
			bIsRegisterd = FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
		}

		OutputProvider = NewObject<UVCamOutputProvider>(GetTransientPackage(), UVCamOutputProvider::StaticClass());
		if (OutputProvider)
		{
			OutputProvider->Initialize();
		}

		NewCameraTransform = FTransform::Identity;
	}
}

UVCamCoreSubsystem::~UVCamCoreSubsystem()
{
	if (FSlateApplication::IsInitialized() && InputProcessor.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}

	if (OutputProvider)
	{
		OutputProvider = nullptr;
	}
}

void UVCamCoreSubsystem::SetActive(const bool InActive)
{
	bIsActive = InActive;

	if (bIsActive && !TargetCamera)
	{
		SpawnDefaultCamera();
	}

	if (!bIsActive && bIsOutputActive)
	{
		SetOutputActive(false);
	}
}

void UVCamCoreSubsystem::SetOutputActive(const bool InOutputActive)
{
	bIsOutputActive = InOutputActive;

	if (OutputProvider)
	{
		OutputProvider->SetActive(bIsOutputActive);
	}
}

void UVCamCoreSubsystem::AddModifier(const FName& Name, const TSubclassOf<UVCamModifier> ModifierClass, bool& bSuccess,
                                     UVCamModifier*& CreatedModifier)
{
	bSuccess = false;
	CreatedModifier = nullptr;

	if (FindModifier(Name))
	{
		UE_LOG(LogVCamCore, Warning, TEXT("Unable to add Modifier to Stack as another Modifier with the name \"%s\" exists"), *Name.ToString());
		return;
	}

	ModifierStack.Emplace(Name, ModifierClass);
	FModifierStackEntry& NewModifierEntry = ModifierStack.Last();
	CreatedModifier = NewModifierEntry.Modifier;

	if (CreatedModifier)
	{
		CreatedModifier->Initialize();
		bSuccess = true;
	}
}

UVCamModifier* UVCamCoreSubsystem::FindModifier(const FName& Name) const
{
	const FModifierStackEntry* StackEntry = ModifierStack.FindByPredicate([Name](const FModifierStackEntry& StackEntry)
	{
		return StackEntry.Name.IsEqual(Name);
	});

	if (StackEntry)
	{
		return StackEntry->Modifier;
	}
	return nullptr;
}

void UVCamCoreSubsystem::ClearModifiers()
{
	ModifierStack.Empty();
}

void UVCamCoreSubsystem::SetTargetCamera(UCineCameraComponent* InTargetCamera)
{
	// If we're already set to the same value then early out.
	if (TargetCamera == InTargetCamera)
	{
		return;
	}

	TargetCamera = InTargetCamera;

	if (OutputProvider)
	{
		OutputProvider->SetTargetCamera(InTargetCamera);
	}

	// If the system is running then we may need to make changes to the Default Camera system
	if (bIsActive)
	{
		if (TargetCamera)
		{
			// If we now have a Target Camera then remove the Default Camera if it exists
			if (DefaultCameraActor)
			{
				DefaultCameraActor->Destroy();
				DefaultCameraActor = nullptr;
			}
		}
		else
		{
			// If we no longer have a Target Camera then add a new Default Camera
			SpawnDefaultCamera();
		}
	}
}

void UVCamCoreSubsystem::SetLiveLinkSubject(const FLiveLinkSubjectName& SubjectName)
{
	LiveLinkSubjectName = SubjectName;
}

void UVCamCoreSubsystem::SetUMGClassForOutput(const TSubclassOf<UUserWidget> InUMGClass)
{
	if (OutputProvider)
	{
		OutputProvider->SetUMGClass(InUMGClass);
	}
}


void UVCamCoreSubsystem::SetShouldConsumeGamepadInput(const bool bInShouldConsumeGamepadInput)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->bShouldConsumeGamepadInput = bInShouldConsumeGamepadInput;
	}
}

bool UVCamCoreSubsystem::GetShouldConsumeGamepadInput() const
{
	bool bShouldConsumeGamepadInput = false;
	if (InputProcessor.IsValid())
	{
		bShouldConsumeGamepadInput = InputProcessor->bShouldConsumeGamepadInput;
	}
	return bShouldConsumeGamepadInput;
}

void UVCamCoreSubsystem::BindKeyDownEvent(const FKey Key, FKeyInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->KeyDownDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindKeyUpEvent(const FKey Key, FKeyInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->KeyUpDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindAnalogEvent(const FKey Key, FAnalogInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->AnalogDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseMoveEvent(FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseMoveDelegateStore.AddDelegate(EKeys::Invalid, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseButtonDownEvent(const FKey Key, FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseButtonDownDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseButtonUpEvent(const FKey Key, FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseButtonUpDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseDoubleClickEvent(const FKey Key, FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseButtonDoubleClickDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseWheelEvent(FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseWheelDelegateStore.AddDelegate(EKeys::Invalid, Delegate);
	}
}

void UVCamCoreSubsystem::Tick()
{
	if (bIsActive)
	{
		FLiveLinkCameraBlueprintData InitialData;
		GetInitialLiveLinkData(InitialData);
		UCineCameraComponent* CameraComponent = GetActiveCamera();
		float DeltaTime = GetDeltaTime();

		CopyLiveLinkDataToCamera(InitialData, CameraComponent);

		for (const FModifierStackEntry& ModifierStackEntry : ModifierStack)
		{
			if (UVCamModifier* Modifier = ModifierStackEntry.Modifier)
			{
				if (Modifier->IsActive())
				{
					Modifier->Apply(InitialData, CameraComponent, DeltaTime);
				}
			}
		}

		if (OutputProvider)
		{
			OutputProvider->Tick(DeltaTime);
		}
	}
}

UCineCameraComponent* UVCamCoreSubsystem::GetActiveCamera() const
{
	if (TargetCamera.IsValid())
	{
		return TargetCamera.Get();
	}
	else
	{
		UE_LOG(LogVCamCore, Warning, TEXT("No TargetCamera. We should have a spawned default camera to use here."));
	}
	return nullptr;
}

void UVCamCoreSubsystem::SpawnDefaultCamera()
{
	// We shouldn't be trying to spawn a default camera if one exists
	ensure(DefaultCameraActor == nullptr);
	UE_LOG(LogVCamCore, Warning, TEXT("TargetCamera was nullptr. We should spawn a default camera to drive here instead."))
}

void UVCamCoreSubsystem::GetInitialLiveLinkData(FLiveLinkCameraBlueprintData& InitialLiveLinkData)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& LiveLinkClient = ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		FLiveLinkSubjectFrameData EvaluatedFrame;

		// Manually get all enabled and virtual LiveLink subjects so we can test roles without generating warnings
		const bool bIncludeDisabledSubjects = false;
		const bool bIncludeVirtualSubjects = true;
		TArray<FLiveLinkSubjectKey> AllEnabledSubjectKeys = LiveLinkClient.GetSubjects(bIncludeDisabledSubjects, bIncludeVirtualSubjects);
		const FLiveLinkSubjectKey* FoundSubjectKey = AllEnabledSubjectKeys.FindByPredicate([=](FLiveLinkSubjectKey& InSubjectKey) { return InSubjectKey.SubjectName == LiveLinkSubjectName; } );

		if (FoundSubjectKey)
		{
			if (LiveLinkClient.DoesSubjectSupportsRole(*FoundSubjectKey, ULiveLinkCameraRole::StaticClass()))
			{
				if (LiveLinkClient.EvaluateFrame_AnyThread(LiveLinkSubjectName, ULiveLinkCameraRole::StaticClass(), EvaluatedFrame))
				{
					FLiveLinkBlueprintDataStruct WrappedBlueprintData(FLiveLinkCameraBlueprintData::StaticStruct(), &InitialLiveLinkData);
					GetDefault<ULiveLinkCameraRole>()->InitializeBlueprintData(EvaluatedFrame, WrappedBlueprintData);
				}
			}
			else if (LiveLinkClient.DoesSubjectSupportsRole(*FoundSubjectKey, ULiveLinkTransformRole::StaticClass()))
			{
				if (LiveLinkClient.EvaluateFrame_AnyThread(LiveLinkSubjectName, ULiveLinkTransformRole::StaticClass(), EvaluatedFrame))
				{
					InitialLiveLinkData.FrameData.Transform = EvaluatedFrame.FrameData.Cast<FLiveLinkTransformFrameData>()->Transform;
				}
			}
		}
	}
}

// Pretty much a copy of ULiveLinkCameraController::Tick.
// TODO: Figure out what to do here properly
void UVCamCoreSubsystem::CopyLiveLinkDataToCamera(FLiveLinkCameraBlueprintData& LiveLinkData,
	UCineCameraComponent* CameraComponent)
{
	const FLiveLinkCameraStaticData& StaticData = LiveLinkData.StaticData;
	const FLiveLinkCameraFrameData& FrameData = LiveLinkData.FrameData;


	if (CameraComponent)
	{
		if (StaticData.bIsFieldOfViewSupported) { CameraComponent->SetFieldOfView(FrameData.FieldOfView); }
		if (StaticData.bIsAspectRatioSupported) { CameraComponent->SetAspectRatio(FrameData.AspectRatio); }
		if (StaticData.bIsProjectionModeSupported) { CameraComponent->SetProjectionMode(FrameData.ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

		if (StaticData.bIsFocalLengthSupported) { CameraComponent->CurrentFocalLength = FrameData.FocalLength; }
		if (StaticData.bIsApertureSupported) { CameraComponent->CurrentAperture = FrameData.Aperture; }
		if (StaticData.FilmBackWidth > 0.0f) { CameraComponent->Filmback.SensorWidth = StaticData.FilmBackWidth; }
		if (StaticData.FilmBackHeight > 0.0f) { CameraComponent->Filmback.SensorHeight = StaticData.FilmBackHeight; }
		if (StaticData.bIsFocusDistanceSupported) { CameraComponent->FocusSettings.ManualFocusDistance = FrameData.FocusDistance; }

		// Naive Transform copy. Should really use something like FLiveLinkTransformControllerData
		CameraComponent->SetWorldTransform(FrameData.Transform);
	}
}
float UVCamCoreSubsystem::GetDeltaTime()
{
	float DeltaTime = 0.f;
	const double CurrentEvaluationTime = FPlatformTime::Seconds();

	if (LastEvaluationTime >= 0.0)
	{
		DeltaTime = CurrentEvaluationTime - LastEvaluationTime;
	}

	LastEvaluationTime = CurrentEvaluationTime;
	return DeltaTime;
}
