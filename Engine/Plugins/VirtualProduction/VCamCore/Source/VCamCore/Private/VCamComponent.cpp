// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamComponent.h"

#include "CineCameraComponent.h"
#include "ILiveLinkClient.h"
#include "VCamCoreSubsystem.h"
#include "VCamModifier.h"
#include "VCamModifierContext.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkTransformRole.h"
#include "GameDelegates.h"

#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#endif

DEFINE_LOG_CATEGORY(LogVCamComponent);

namespace VCamComponent
{
	static const FName LevelEditorName(TEXT("LevelEditor"));
}

UVCamComponent::UVCamComponent()
{
	// Don't run on CDO
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Hook into the Live Link Client for our Tick
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			ILiveLinkClient& LiveLinkClient = ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
			LiveLinkClient.OnLiveLinkTicked().AddUObject(this, &UVCamComponent::Update);
		}

		FGameDelegates::Get().GetEndPlayMapDelegate().AddUObject(this, &UVCamComponent::OnEndPlayMap);

#if WITH_EDITOR
		// Add the necessary event listeners so we can start/end properly
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamComponent::LevelEditorName))
		{
			LevelEditorModule->OnMapChanged().AddUObject(this, &UVCamComponent::OnMapChanged);
		}

		FEditorDelegates::BeginPIE.AddUObject(this, &UVCamComponent::OnBeginPIE);
		FEditorDelegates::PostPIEStarted.AddUObject(this, &UVCamComponent::OnPostPIEStarted);
#endif
	}
}

void UVCamComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bLockViewportToCamera = false;
	UpdateActorLock();

	for (UVCamOutputProviderBase* Provider : OutputProviders)
	{
		DestroyOutputProvider(Provider);
	}

	OutputProviders.Empty();

	FGameDelegates::Get().GetEndPlayMapDelegate().RemoveAll(this);

#if WITH_EDITOR
	// Remove all event listeners
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamComponent::LevelEditorName))
	{
		LevelEditorModule->OnMapChanged().RemoveAll(this);
	}

	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::PostPIEStarted.RemoveAll(this);
#endif
}

void UVCamComponent::PostInitProperties()
{
	Super::PostInitProperties();

	for (UVCamOutputProviderBase* Provider : OutputProviders)
	{
		if (Provider)
		{
			Provider->InitializeSafe();
		}
	}
}

void UVCamComponent::ConditionallyInitializeModifiers()
{
	for (FModifierStackEntry& StackEntry : ModifierStack)
	{
		UVCamModifier* Modifier = StackEntry.GeneratedModifier;
		if (Modifier && Modifier->DoesRequireInitialization())
		{
			Modifier->Initialize(ModifierContext);
		}
	}
}


bool UVCamComponent::CanUpdate() const
{
	if (bEnabled)
	{
		if (const USceneComponent* ParentComponent = GetAttachParent())
		{
			if (ParentComponent->IsA<UCineCameraComponent>())
			{
				// Component is valid to use if it is enabled, has a parent and that parent is a CineCamera derived component
				return true;
			}
		}
	}
	return false;
}

void UVCamComponent::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();

	// Attachment change event was a detach. We only want to respond to attaches 
	if (GetAttachParent() == nullptr)
	{
		return;
	}

	UCineCameraComponent* TargetCamera = GetTargetCamera();

	// This flag must be false on the attached CameraComponent or the UMG will not render correctly if the aspect ratios are mismatched
	if (TargetCamera)
	{
		TargetCamera->bConstrainAspectRatio = false;
	}

	for (UVCamOutputProviderBase* Provider : OutputProviders)
	{
		if (Provider)
		{
			Provider->SetTargetCamera(TargetCamera);
		}
	}

#if WITH_EDITOR
	CheckForErrors();
#endif
}

#if WITH_EDITOR

void UVCamComponent::CheckForErrors()
{
	Super::CheckForErrors();

	if (!GetTargetCamera())
	{
		UE_LOG(LogVCamComponent, Error, TEXT("Attached Parent should be a CineCamera derived component."));
	}
}

void UVCamComponent::PreEditChange(FProperty* PropertyThatWillChange)
{
	// Copy the property that is going to be changed so we can use it in PostEditChange if needed (for ArrayClear, ArrayRemove, etc.)
	if (PropertyThatWillChange)
	{
		static FName NAME_OutputProviders = GET_MEMBER_NAME_CHECKED(UVCamComponent, OutputProviders);

		if (PropertyThatWillChange->GetFName() == NAME_OutputProviders)
		{
			SavedOutputProviders.Empty();
			SavedOutputProviders = OutputProviders;
		}
	}

	Super::PreEditChange(PropertyThatWillChange);
}

void UVCamComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;
	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_LockViewportToCamera = GET_MEMBER_NAME_CHECKED(UVCamComponent, bLockViewportToCamera);

		if (Property->GetFName() == NAME_LockViewportToCamera)
		{
			UpdateActorLock();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UVCamComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.PropertyChain.GetActiveNode()->GetValue();
	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		static FName NAME_OutputProviders = GET_MEMBER_NAME_CHECKED(UVCamComponent, OutputProviders);

		if (Property->GetFName() == NAME_OutputProviders)
		{
			FProperty* ActualProperty = PropertyChangedEvent.PropertyChain.GetActiveNode()->GetNextNode() ? PropertyChangedEvent.PropertyChain.GetActiveNode()->GetNextNode()->GetValue() : nullptr;
			if (ActualProperty == nullptr)
			{
				const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.GetPropertyName().ToString());
				if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
				{
					UVCamOutputProviderBase* ChangedProvider = OutputProviders[ChangedIndex];

					// If we changed the output type, be sure to delete the old one before setting up the new one
					if (SavedOutputProviders[ChangedIndex] != ChangedProvider)
					{
						DestroyOutputProvider(SavedOutputProviders[ChangedIndex]);
					}

					if (ChangedProvider)
					{
						ChangedProvider->InitializeSafe();
					}
				}
				else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove)
				{
					DestroyOutputProvider(SavedOutputProviders[ChangedIndex]);
				}
				else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear)
				{
					for (UVCamOutputProviderBase* ClearedProvider : SavedOutputProviders)
					{
						DestroyOutputProvider(ClearedProvider);
					}
				}
			}

			// We created this in PreEditChange, so we need to always get rid of it
			SavedOutputProviders.Empty();
		}
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR


void UVCamComponent::Update()
{

	ConditionallyInitializeModifiers();

	if (CanUpdate())
	{
		const float DeltaTime = GetDeltaTime();

		FLiveLinkCameraBlueprintData InitialLiveLinkData;
		GetInitialLiveLinkData(InitialLiveLinkData);
		UCineCameraComponent* CameraComponent = GetTargetCamera();

		if (!CameraComponent)
		{
			UE_LOG(LogVCamComponent, Error, TEXT("Parent component wasn't valid for Update"));
			return;
		}

		CopyLiveLinkDataToCamera(InitialLiveLinkData, CameraComponent);

		for (FModifierStackEntry& ModifierStackEntry : ModifierStack)
		{
			if (!ModifierStackEntry.bEnabled)
			{
				continue;
			}

			if (UVCamModifier* Modifier = ModifierStackEntry.GeneratedModifier)
			{
				Modifier->Apply(ModifierContext, InitialLiveLinkData, CameraComponent, DeltaTime);
			}
		}

		for (UVCamOutputProviderBase* Provider : OutputProviders)
		{
			if (Provider)
			{
				Provider->Tick(DeltaTime);
			}
		}
	}
}

UCineCameraComponent* UVCamComponent::GetTargetCamera() const
{
	return Cast<UCineCameraComponent>(GetAttachParent());
}

void UVCamComponent::AddModifier(const FName Name, const TSubclassOf<UVCamModifier> ModifierClass, bool& bSuccess,
                                 UVCamModifier*& CreatedModifier)
{
	bSuccess = false;
	CreatedModifier = nullptr;

	if (FindModifierByName(Name))
	{
		UE_LOG(LogVCamCore, Warning, TEXT("Unable to add Modifier to Stack as another Modifier with the name \"%s\" exists"), *Name.ToString());
		return;
	}

	ModifierStack.Emplace(Name, ModifierClass, this);
	FModifierStackEntry& NewModifierEntry = ModifierStack.Last();
	CreatedModifier = NewModifierEntry.GeneratedModifier;

	bSuccess = CreatedModifier != nullptr;
}

UVCamModifier* UVCamComponent::FindModifierByName(const FName Name) const
{
	const FModifierStackEntry* StackEntry = ModifierStack.FindByPredicate([Name](const FModifierStackEntry& StackEntry)
	{
		return StackEntry.Name.IsEqual(Name);
	});

	if (StackEntry)
	{
		return StackEntry->GeneratedModifier;
	}
	return nullptr;
}

void UVCamComponent::FindModifiersByClass(TSubclassOf<UVCamModifier> ModifierClass,
	TArray<UVCamModifier*>& FoundModifiers) const
{
	FoundModifiers.Empty();

	for (const FModifierStackEntry& StackEntry : ModifierStack)
	{
		if (StackEntry.GeneratedModifier && StackEntry.GeneratedModifier->IsA(ModifierClass))
		{
			FoundModifiers.Add(StackEntry.GeneratedModifier);
		}
	}
}

void UVCamComponent::FindModifierByInterface(TSubclassOf<UInterface> InterfaceClass, TArray<UVCamModifier*>& FoundModifiers) const
{
	FoundModifiers.Empty();

	for (const FModifierStackEntry& StackEntry : ModifierStack)
	{
		if (StackEntry.GeneratedModifier && StackEntry.GeneratedModifier->GetClass()->ImplementsInterface(InterfaceClass))
		{
			FoundModifiers.Add(StackEntry.GeneratedModifier);
		}
	}
}

void UVCamComponent::SetModifierContextClass(TSubclassOf<UVCamModifierContext> ContextClass, UVCamModifierContext*& CreatedContext)
{
	if (ContextClass)
	{
		if (ContextClass != ModifierContext->StaticClass())
		{
			// Only reinstance if it's a new class
			ModifierContext = NewObject<UVCamModifierContext>(this, ContextClass.Get());
		}
	}
	else
	{
		// If the context class is invalid then clear the modifier context
		ModifierContext = nullptr;
	}
}

UVCamModifierContext* UVCamComponent::GetModifierContext() const
{
	return ModifierContext;
}

void UVCamComponent::ClearModifiers()
{
	ModifierStack.Empty();
}

bool UVCamComponent::RemoveModifier(const UVCamModifier* Modifier)
{
	const int32 RemovedCount = ModifierStack.RemoveAll([Modifier](const FModifierStackEntry& StackEntry)
    {
        return StackEntry.GeneratedModifier && StackEntry.GeneratedModifier == Modifier;
    });
    
	return RemovedCount > 0;
}

bool UVCamComponent::RemoveModifierByIndex(const int ModifierIndex)
{
	if (ModifierStack.IsValidIndex(ModifierIndex))
	{
		ModifierStack.RemoveAt(ModifierIndex);
		return true;
	}
	return false;
}

bool UVCamComponent::RemoveModifierByName(const FName Name)
{
	const int32 RemovedCount = ModifierStack.RemoveAll([Name](const FModifierStackEntry& StackEntry)
    {
        return StackEntry.Name.IsEqual(Name);
    });

	return RemovedCount > 0;
}

void UVCamComponent::GetInitialLiveLinkData(FLiveLinkCameraBlueprintData& InitialLiveLinkData)
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
		const FLiveLinkSubjectKey* FoundSubjectKey = AllEnabledSubjectKeys.FindByPredicate([=](FLiveLinkSubjectKey& InSubjectKey) { return InSubjectKey.SubjectName == LiveLinkSubject; } );

		if (FoundSubjectKey)
		{
			if (LiveLinkClient.DoesSubjectSupportsRole(*FoundSubjectKey, ULiveLinkCameraRole::StaticClass()))
			{
				if (LiveLinkClient.EvaluateFrame_AnyThread(LiveLinkSubject, ULiveLinkCameraRole::StaticClass(), EvaluatedFrame))
				{
					FLiveLinkBlueprintDataStruct WrappedBlueprintData(FLiveLinkCameraBlueprintData::StaticStruct(), &InitialLiveLinkData);
					GetDefault<ULiveLinkCameraRole>()->InitializeBlueprintData(EvaluatedFrame, WrappedBlueprintData);
				}
			}
			else if (LiveLinkClient.DoesSubjectSupportsRole(*FoundSubjectKey, ULiveLinkTransformRole::StaticClass()))
			{
				if (LiveLinkClient.EvaluateFrame_AnyThread(LiveLinkSubject, ULiveLinkTransformRole::StaticClass(), EvaluatedFrame))
				{
					InitialLiveLinkData.FrameData.Transform = EvaluatedFrame.FrameData.Cast<FLiveLinkTransformFrameData>()->Transform;
				}
			}
		}
	}
}

void UVCamComponent::CopyLiveLinkDataToCamera(const FLiveLinkCameraBlueprintData& LiveLinkData, UCineCameraComponent* CameraComponent)
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
		CameraComponent->SetRelativeTransform(FrameData.Transform);
	}
}

float UVCamComponent::GetDeltaTime()
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

void UVCamComponent::UpdateActorLock()
{
	if (GetTargetCamera() == nullptr)
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("UpdateActorLock has been called, but there is no valid TargetCamera!"));
		return;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
#if WITH_EDITOR
		if (Context.WorldType == EWorldType::Editor)
		{
			if (FLevelEditorViewportClient* LevelViewportClient = GetLevelViewportClient())
			{
				if (bLockViewportToCamera)
				{
					Backup_ActorLock = LevelViewportClient->GetActiveActorLock();
					LevelViewportClient->SetActorLock(GetTargetCamera()->GetOwner());
				}
				else if (Backup_ActorLock.IsValid())
				{
					LevelViewportClient->SetActorLock(Backup_ActorLock.Get());
					Backup_ActorLock = nullptr;
				}
				else
				{
					LevelViewportClient->SetActorLock(nullptr);
				}
			}
		}
		else
#endif
		{
			UWorld* ActorWorld = Context.World();
			if (ActorWorld)
			{
				APlayerController* PlayerController = ActorWorld->GetGameInstance()->GetFirstLocalPlayerController(ActorWorld);
				if (PlayerController)
				{
					if (bLockViewportToCamera)
					{
						Backup_ViewTarget = PlayerController->GetViewTarget();
						PlayerController->SetViewTarget(GetTargetCamera()->GetOwner());
					}
					else if (Backup_ViewTarget.IsValid())
					{
						PlayerController->SetViewTarget(Backup_ViewTarget.Get());
						Backup_ViewTarget = nullptr;
					}
					else
					{
						PlayerController->SetViewTarget(nullptr);
					}
				}
			}
		}
	}
}


void UVCamComponent::DestroyOutputProvider(UVCamOutputProviderBase* Provider)
{
	if (Provider)
	{
		Provider->Destroy();
		Provider->ConditionalBeginDestroy();
		Provider = nullptr;
	}
}

void UVCamComponent::ResetAllOutputProviders()
{
	for (UVCamOutputProviderBase* Provider : OutputProviders)
	{
		if (Provider)
		{
			if (Provider->IsActive())
			{
				Provider->SetActive(false);
				Provider->SetActive(true);
			}
			else if (Provider->IsPaused())
			{
				Provider->SetPause(false);
			}
		}
	}
}

void UVCamComponent::OnEndPlayMap()
{
	if (UWorld* World = GetWorld())
	{
		if (World->WorldType == EWorldType::PIE)
		{
			// Destroy all output providers in the PIE world since it's ending anyway
			for (UVCamOutputProviderBase* Provider : OutputProviders)
			{
				DestroyOutputProvider(Provider);
			}
		}
		else if (World->WorldType == EWorldType::Editor)
		{
			// Unpause all output providers in the editor world now that PIE is over
			for (UVCamOutputProviderBase* Provider : OutputProviders)
			{
				if (Provider)
				{
					Provider->SetPause(false);
				}
			}
		}
	}
}

#if WITH_EDITOR
FLevelEditorViewportClient* UVCamComponent::GetLevelViewportClient() const
{
	FLevelEditorViewportClient* OutClient = nullptr;
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamComponent::LevelEditorName))
	{
		TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveLevelViewport();
		if (ActiveLevelViewport.IsValid())
		{
			OutClient = &ActiveLevelViewport->GetLevelViewportClient();
		}
	}

	return OutClient;
}

void UVCamComponent::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	if (ChangeType == EMapChangeType::TearDownWorld)
	{
		OnComponentDestroyed(true);
	}
}

void UVCamComponent::OnBeginPIE(const bool InIsSimulating)
{
	if (UWorld* World = GetWorld())
	{
		if (World->WorldType == EWorldType::PIE)
		{
		}
		else if (World->WorldType == EWorldType::Editor)
		{
			// Pause all output providers in the editor world
			for (UVCamOutputProviderBase* Provider : OutputProviders)
			{
				if (Provider)
				{
					Provider->SetPause(true);
				}
			}
		}
	}
}

void UVCamComponent::OnPostPIEStarted(const bool InIsSimulating)
{
	if (UWorld* World = GetWorld())
	{
		if (World->WorldType == EWorldType::PIE)
		{
			// Reset any active OutputProviders only in the new PIE world
			ResetAllOutputProviders();
		}
		else if (World->WorldType == EWorldType::Editor)
		{
		}
	}
}
#endif
