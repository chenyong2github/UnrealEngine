// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamComponent.h"

#include "CineCameraComponent.h"
#include "ILiveLinkClient.h"
#include "VCamModifier.h"
#include "VCamModifierContext.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkTransformRole.h"
#include "GameDelegates.h"
#include "Engine/GameEngine.h"

#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "SLevelViewport.h"

#include "IConcertModule.h"
#include "IConcertClient.h"
#include "IConcertSession.h"
#include "IConcertSyncClient.h"
#include "IMultiUserClientModule.h"

#include "VPSettings.h"
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
		else
		{
			UE_LOG(LogVCamComponent, Error, TEXT("LiveLink is not available. Some VCamCore features may not work as expected"));
		}

#if WITH_EDITOR
		// Add the necessary event listeners so we can start/end properly
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamComponent::LevelEditorName))
		{
			LevelEditorModule->OnMapChanged().AddUObject(this, &UVCamComponent::OnMapChanged);
		}

		FEditorDelegates::BeginPIE.AddUObject(this, &UVCamComponent::OnBeginPIE);
		FEditorDelegates::EndPIE.AddUObject(this, &UVCamComponent::OnEndPIE);

		if (GEditor)
		{
			GEditor->OnObjectsReplaced().AddUObject(this, &UVCamComponent::HandleObjectReplaced);
		}
		MultiUserStartup();
#endif
	}
}

void UVCamComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bLockViewportToCamera = false;
	UpdateActorLock();

	for (UVCamOutputProviderBase* Provider : OutputProviders)
	{
		if (Provider)
		{
			Provider->Deinitialize();
		}
	}

#if WITH_EDITOR
	// Remove all event listeners
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamComponent::LevelEditorName))
	{
		LevelEditorModule->OnMapChanged().RemoveAll(this);
	}

	FEditorDelegates::BeginPIE.RemoveAll(this);
	FEditorDelegates::EndPIE.RemoveAll(this);

	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}
	MultiUserShutdown();
#endif
}

void UVCamComponent::HandleObjectReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	for (const TPair<UObject*, UObject*> ReplacementPair : ReplacementMap)
	{
		UObject* FromObject = ReplacementPair.Key;
		UObject* ToObject = ReplacementPair.Value;

		if (ToObject == this)
		{
			if (UVCamComponent* OldComponent = Cast<UVCamComponent>(FromObject))
			{
				OldComponent->NotifyComponentWasReplaced(this);
			}

			OnComponentReplaced.Broadcast(this);
		}
	}
}


void UVCamComponent::NotifyComponentWasReplaced(UVCamComponent* ReplacementComponent)
{
	// This function should only ever be called when we have a valid component replacing us
	check(ReplacementComponent);

	// Make sure to copy over our delegate bindings to the component replacing us
	ReplacementComponent->OnComponentReplaced = OnComponentReplaced;

	OnComponentReplaced.Clear();

	DestroyComponent();
}

bool UVCamComponent::CanUpdate() const
{
	UWorld* World = GetWorld();
	if (bEnabled && !IsPendingKill() && !bIsEditorObjectButPIEIsRunning && World)
	{
		// Check for an Inactive type of world which means nothing should ever execute on this object
		// @TODO: This is far from optimal as it means a zombie object has been created that never gets GC'ed
		// Apparently, we should be using OnRegister/OnUnregister() instead of doing everything in the constructor, but it was throwing GC errors when trying that
		if (World->WorldType != EWorldType::Inactive)
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
		static FName NAME_ModifierStack = GET_MEMBER_NAME_CHECKED(UVCamComponent, ModifierStack);
		// Name property withing the Modifier Stack Entry struct. Possible collision due to just being called "Name"
		static FName NAME_ModifierStackEntryName = GET_MEMBER_NAME_CHECKED(FModifierStackEntry, Name);
		static FName NAME_Enabled = GET_MEMBER_NAME_CHECKED(UVCamComponent, bEnabled);

		const FName PropertyThatWillChangeName = PropertyThatWillChange->GetFName();

		if (PropertyThatWillChangeName == NAME_OutputProviders)
		{
			SavedOutputProviders.Empty();
			SavedOutputProviders = OutputProviders;
		}
		else if (PropertyThatWillChangeName == NAME_ModifierStack || PropertyThatWillChangeName == NAME_ModifierStackEntryName)
		{
			SavedModifierStack = ModifierStack;
		}
		else if (PropertyThatWillChangeName == NAME_Enabled)
		{
			// If the property's owner is a struct (like FModifierStackEntry), act on it in PostEditChangeProperty(), not here
			if (PropertyThatWillChange->GetOwner<UClass>())
			{
				void* PropertyData = PropertyThatWillChange->ContainerPtrToValuePtr<void>(this);
				bool bWasEnabled = false;
				PropertyThatWillChange->CopySingleValue(&bWasEnabled, PropertyData);

				// Changing the enabled state needs to be done here instead of PostEditChange
				SetEnabled(!bWasEnabled);
			}
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
		static FName NAME_Enabled = GET_MEMBER_NAME_CHECKED(UVCamComponent, bEnabled);
		static FName NAME_ModifierStack = GET_MEMBER_NAME_CHECKED(UVCamComponent, ModifierStack);
		static FName NAME_TargetViewport = GET_MEMBER_NAME_CHECKED(UVCamComponent, TargetViewport);

		const FName PropertyName = Property->GetFName();

		if (PropertyName == NAME_LockViewportToCamera)
		{
			UpdateActorLock();
		}
		else if (PropertyName == NAME_Enabled)
		{
			// Only act here if we are a struct (like FModifierStackEntry)
			if (!Property->GetOwner<UClass>())
			{
				SetEnabled(bEnabled);
			}
		}
		else if (PropertyName == NAME_ModifierStack)
		{
			EnforceModifierStackNameUniqueness();
		}
		else if (PropertyName == NAME_TargetViewport)
		{
			if (bEnabled)
			{
				SetEnabled(false);
				SetEnabled(true);

				if (bLockViewportToCamera)
				{
					SetActorLock(false);
					SetActorLock(true);
				}
			}
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
					if (OutputProviders.IsValidIndex(ChangedIndex))
					{
						UVCamOutputProviderBase* ChangedProvider = OutputProviders[ChangedIndex];

						// If we changed the output type, be sure to delete the old one before setting up the new one
						if (SavedOutputProviders.IsValidIndex(ChangedIndex) && (SavedOutputProviders[ChangedIndex] != ChangedProvider))
						{
							DestroyOutputProvider(SavedOutputProviders[ChangedIndex]);
						}

						if (ChangedProvider)
						{
							ChangedProvider->Initialize();
						}
					}
				}
				else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove)
				{
					if (SavedOutputProviders.IsValidIndex(ChangedIndex))
					{
						DestroyOutputProvider(SavedOutputProviders[ChangedIndex]);
					}
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
	if (!CanUpdate())
	{
		return;
	}

	// If requested then disable the component if we're spawned by sequencer
	if (bDisableComponentWhenSpawnedBySequencer)
	{
		static const FName SequencerActorTag(TEXT("SequencerActor"));
		AActor* OwningActor = GetOwner();
		if (OwningActor && OwningActor->ActorHasTag(SequencerActorTag))
		{
			UE_LOG(LogVCamComponent, Warning, TEXT("%s was spawned by Sequencer. Disabling the component because \"Disable Component When Spawned By Sequencer\" was true."), *GetFullName(OwningActor->GetOuter()));
			SetEnabled(false);
			return;
		}
	}
	UCineCameraComponent* CameraComponent = GetTargetCamera();

	if (!CameraComponent)
	{
		UE_LOG(LogVCamComponent, Error, TEXT("Parent component wasn't valid for Update"));
		return;
	}

	const float DeltaTime = GetDeltaTime();

	if (CanEvaluateModifierStack())
	{
		// Ensure the actor lock reflects the state of the lock property
		// This is needed as UActorComponent::ConsolidatedPostEditChange will cause the component to be reconstructed on PostEditChange
		// if the component is inherited
		if (bLockViewportToCamera != bIsLockedToViewport)
		{
			UpdateActorLock();
		}

		FLiveLinkCameraBlueprintData InitialLiveLinkData;
		GetLiveLinkDataForCurrentFrame(InitialLiveLinkData);

		CopyLiveLinkDataToCamera(InitialLiveLinkData, CameraComponent);

		for (FModifierStackEntry& ModifierStackEntry : ModifierStack)
		{
			if (!ModifierStackEntry.bEnabled)
			{
				continue;
			}

			if (UVCamModifier* Modifier = ModifierStackEntry.GeneratedModifier)
			{
				// Initialize the Modifier if required
				if (Modifier->DoesRequireInitialization())
				{
					Modifier->Initialize(ModifierContext);
				}

				Modifier->Apply(ModifierContext, CameraComponent, DeltaTime);
			}
		}

		SendCameraDataViaMultiUser();
	}

	for (UVCamOutputProviderBase* Provider : OutputProviders)
	{
		if (Provider)
		{
			// Initialize the Provider if required
			if (!Provider->IsInitialized())
			{
				Provider->Initialize();
			}

			Provider->Tick(DeltaTime);
		}
	}
}

void UVCamComponent::SetEnabled(bool bNewEnabled)
{
	// Disable all outputs if we're no longer enabled
	// NOTE this must be done BEFORE setting the actual bEnabled variable because OutputProviderBase now checks the component enabled state
	if (!bNewEnabled)
	{
		for (UVCamOutputProviderBase* Provider : OutputProviders)
		{
			if (Provider)
			{
				Provider->Deinitialize();
			}
		}
	}

	bEnabled = bNewEnabled;

	// Enable any outputs that are set to active
	// NOTE this must be done AFTER setting the actual bEnabled variable because OutputProviderBase now checks the component enabled state
	if (bNewEnabled)
	{
		for (UVCamOutputProviderBase* Provider : OutputProviders)
		{
			if (Provider)
			{
				Provider->Initialize();
			}
		}
	}
}

UCineCameraComponent* UVCamComponent::GetTargetCamera() const
{
	return Cast<UCineCameraComponent>(GetAttachParent());
}

bool UVCamComponent::AddModifier(const FName Name, const TSubclassOf<UVCamModifier> ModifierClass, UVCamModifier*& CreatedModifier)
{
	CreatedModifier = nullptr;

	if (GetModifierByName(Name))
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Unable to add Modifier to Stack as another Modifier with the name \"%s\" exists"), *Name.ToString());
		return false;
	}

	ModifierStack.Emplace(Name, ModifierClass, this);
	FModifierStackEntry& NewModifierEntry = ModifierStack.Last();
	CreatedModifier = NewModifierEntry.GeneratedModifier;

	return CreatedModifier != nullptr;
}

bool UVCamComponent::InsertModifier(const FName Name, int32 Index, const TSubclassOf<UVCamModifier> ModifierClass, UVCamModifier*& CreatedModifier)
{
	CreatedModifier = nullptr;

	if (GetModifierByName(Name))
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Unable to add Modifier to Stack as another Modifier with the name \"%s\" exists"), *Name.ToString());
		return false;
	}

	if (Index < 0 || Index > ModifierStack.Num())
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Insert Modifier failed with invalid index %d for stack of size %d."), Index, ModifierStack.Num());
		return false;
	}
	
	ModifierStack.EmplaceAt(Index, Name, ModifierClass, this);
	FModifierStackEntry& NewModifierEntry = ModifierStack[Index];
	CreatedModifier = NewModifierEntry.GeneratedModifier;

	return CreatedModifier != nullptr;
}

bool UVCamComponent::SetModifierIndex(int32 OriginalIndex, int32 NewIndex)
{
	if (!ModifierStack.IsValidIndex(OriginalIndex))
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Set Modifier Index failed as the Original Index, %d, was out of range for stack of size %d"), OriginalIndex, ModifierStack.Num());
		return false;
	}

	if (!ModifierStack.IsValidIndex(NewIndex))
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Set Modifier Index failed as the New Index, %d, was out of range for stack of size %d"), NewIndex, ModifierStack.Num());
		return false;
	}

	FModifierStackEntry StackEntry = ModifierStack[OriginalIndex];
	ModifierStack.RemoveAtSwap(OriginalIndex);
	ModifierStack.Insert(StackEntry, NewIndex);

	return true;
}

void UVCamComponent::RemoveAllModifiers()
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

int32 UVCamComponent::GetNumberOfModifiers() const
{
	return ModifierStack.Num();
}

void UVCamComponent::GetAllModifiers(TArray<UVCamModifier*>& Modifiers) const
{
	Modifiers.Empty();

	for (const FModifierStackEntry& StackEntry : ModifierStack)
	{
		Modifiers.Add(StackEntry.GeneratedModifier);
	}
}

UVCamModifier* UVCamComponent::GetModifierByIndex(const int32 Index) const
{
	if (ModifierStack.IsValidIndex(Index))
	{
		return ModifierStack[Index].GeneratedModifier;
	}

	return nullptr;
}

UVCamModifier* UVCamComponent::GetModifierByName(const FName Name) const
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

void UVCamComponent::GetModifiersByClass(TSubclassOf<UVCamModifier> ModifierClass,
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

void UVCamComponent::GetModifiersByInterface(TSubclassOf<UInterface> InterfaceClass, TArray<UVCamModifier*>& FoundModifiers) const
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

	CreatedContext = ModifierContext;
}

UVCamModifierContext* UVCamComponent::GetModifierContext() const
{
	return ModifierContext;
}

bool UVCamComponent::AddOutputProvider(TSubclassOf<UVCamOutputProviderBase> ProviderClass, UVCamOutputProviderBase*& CreatedProvider)
{
	CreatedProvider = nullptr;

	if (ProviderClass)
	{
		int NewItemIndex = OutputProviders.Emplace(NewObject<UVCamOutputProviderBase>(this, ProviderClass.Get()));
		CreatedProvider = OutputProviders[NewItemIndex];
	}

	return CreatedProvider != nullptr;
}

bool UVCamComponent::InsertOutputProvider(int32 Index, TSubclassOf<UVCamOutputProviderBase> ProviderClass, UVCamOutputProviderBase*& CreatedProvider)
{
	CreatedProvider = nullptr;

	if (Index < 0 || Index > OutputProviders.Num())
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Insert Output Provider failed with invalid index %d for stack of size %d."), Index, OutputProviders.Num());
		return false;
	}

	if (ProviderClass)
	{
		OutputProviders.EmplaceAt(Index, NewObject<UVCamOutputProviderBase>(this, ProviderClass.Get()));
		CreatedProvider = OutputProviders[Index];
	}

	return CreatedProvider != nullptr;
}

bool UVCamComponent::SetOutputProviderIndex(int32 OriginalIndex, int32 NewIndex)
{
	if (!OutputProviders.IsValidIndex(OriginalIndex))
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Set Output Provider Index failed as the Original Index, %d, was out of range for stack of size %d"), OriginalIndex, OutputProviders.Num());
		return false;
	}

	if (!OutputProviders.IsValidIndex(NewIndex))
	{
		UE_LOG(LogVCamComponent, Warning, TEXT("Set Output Provider Index failed as the New Index, %d, was out of range for stack of size %d"), NewIndex, OutputProviders.Num());
		return false;
	}

	UVCamOutputProviderBase* Provider = OutputProviders[OriginalIndex];
	OutputProviders.RemoveAtSwap(OriginalIndex);
	OutputProviders.Insert(Provider, NewIndex);

	return true;
}

void UVCamComponent::RemoveAllOutputProviders()
{
	OutputProviders.Empty();
}

bool UVCamComponent::RemoveOutputProvider(const UVCamOutputProviderBase* Provider)
{
	int32 NumRemoved = OutputProviders.RemoveAll([Provider](const UVCamOutputProviderBase* ProviderInArray) { return ProviderInArray == Provider; });
	return NumRemoved > 0;
}

bool UVCamComponent::RemoveOutputProviderByIndex(const int32 ProviderIndex)
{
	if (OutputProviders.IsValidIndex(ProviderIndex))
	{
		OutputProviders.RemoveAt(ProviderIndex);
		return true;
	}
	return false;
}

int32 UVCamComponent::GetNumberOfOutputProviders() const
{
	return OutputProviders.Num();
}

void UVCamComponent::GetAllOutputProviders(TArray<UVCamOutputProviderBase*>& Providers) const
{
	Providers = OutputProviders;
}

UVCamOutputProviderBase* UVCamComponent::GetOutputProviderByIndex(const int32 ProviderIndex) const
{
	if (OutputProviders.IsValidIndex(ProviderIndex))
	{
		return OutputProviders[ProviderIndex];
	}
	return nullptr;
}

void UVCamComponent::GetOutputProvidersByClass(TSubclassOf<UVCamOutputProviderBase> ProviderClass, TArray<UVCamOutputProviderBase*>& FoundProviders) const
{
	FoundProviders.Empty();

	if (ProviderClass)
	{
		FoundProviders = OutputProviders.FilterByPredicate([ProviderClass](const UVCamOutputProviderBase* ProviderInArray) { return ProviderInArray->IsA(ProviderClass); });
	}
}

void UVCamComponent::GetLiveLinkDataForCurrentFrame(FLiveLinkCameraBlueprintData& LiveLinkData)
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
					FLiveLinkBlueprintDataStruct WrappedBlueprintData(FLiveLinkCameraBlueprintData::StaticStruct(), &LiveLinkData);
					GetDefault<ULiveLinkCameraRole>()->InitializeBlueprintData(EvaluatedFrame, WrappedBlueprintData);
				}
			}
			else if (LiveLinkClient.DoesSubjectSupportsRole(*FoundSubjectKey, ULiveLinkTransformRole::StaticClass()))
			{
				if (LiveLinkClient.EvaluateFrame_AnyThread(LiveLinkSubject, ULiveLinkTransformRole::StaticClass(), EvaluatedFrame))
				{
					LiveLinkData.FrameData.Transform = EvaluatedFrame.FrameData.Cast<FLiveLinkTransformFrameData>()->Transform;
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
			if (FLevelEditorViewportClient* LevelViewportClient = GetTargetLevelViewportClient())
			{
				if (bLockViewportToCamera)
				{
					Backup_ActorLock = LevelViewportClient->GetActiveActorLock();
					LevelViewportClient->SetActorLock(GetTargetCamera()->GetOwner());
					// If bLockedCameraView is not true then the viewport is locked to the actor's transform and not the camera component
					LevelViewportClient->bLockedCameraView = true;
					bIsLockedToViewport = true;
				}
				else if (Backup_ActorLock.IsValid())
				{
					LevelViewportClient->SetActorLock(Backup_ActorLock.Get());
					Backup_ActorLock = nullptr;
					// If bLockedCameraView is not true then the viewport is locked to the actor's transform and not the camera component
					LevelViewportClient->bLockedCameraView = true;
					bIsLockedToViewport = false;
				}
				else
				{
					LevelViewportClient->SetActorLock(nullptr);
					bIsLockedToViewport = false;
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
						bIsLockedToViewport = true;
					}
					else if (Backup_ViewTarget.IsValid())
					{
						PlayerController->SetViewTarget(Backup_ViewTarget.Get());
						Backup_ViewTarget = nullptr;
						bIsLockedToViewport = false;
					}
					else
					{
						PlayerController->SetViewTarget(nullptr);
						bIsLockedToViewport = false;
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
		Provider->Deinitialize();
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
			// Initialization will also recover active state 
			Provider->Deinitialize();
			Provider->Initialize();
		}
	}
}

void UVCamComponent::EnforceModifierStackNameUniqueness(const FString BaseName /*= "NewModifier"*/)
{
	int32 ModifiedStackIndex;
	bool bIsNewEntry;

	FindModifiedStackEntry(ModifiedStackIndex, bIsNewEntry);

	// Early out in the case of no modified entry
	if (ModifiedStackIndex == INDEX_NONE)
	{
		return;
	}

	// Addition
	if (bIsNewEntry)
	{
		// Keep trying to append an ever increasing int to the base name until we find a unique name
		int32 DuplicatedCount = 1;
		FString UniqueName = BaseName;

		while (DoesNameExistInSavedStack(FName(*UniqueName)))
		{
			UniqueName = BaseName + FString::FromInt(DuplicatedCount++);
		}

		ModifierStack[ModifiedStackIndex].Name = FName(*UniqueName);
	}
	// Edit
	else
	{
		FName NewModifierName = ModifierStack[ModifiedStackIndex].Name;

		// Check if the new name is a duplicate
		bool bIsDuplicate = false;
		for (int32 ModifierIndex = 0; ModifierIndex < ModifierStack.Num(); ++ModifierIndex)
		{
			// Don't check ourselves
			if (ModifierIndex == ModifiedStackIndex)
			{
				continue;
			}
			
			if (ModifierStack[ModifierIndex].Name.IsEqual(NewModifierName))
			{
				bIsDuplicate = true;
				break;
			}
		}

		// If it's a duplicate then reset to the old name
		if (bIsDuplicate)
		{
			ModifierStack[ModifiedStackIndex].Name = SavedModifierStack[ModifiedStackIndex].Name;

			// Add a warning to the log
			UE_LOG(LogVCamComponent, Warning, TEXT("Unable to set Modifier Name to \"%s\" as it is already in use. Resetting Name to previous value \"%s\""),
				*NewModifierName.ToString(),
				*SavedModifierStack[ModifiedStackIndex].Name.ToString());
		}
	}	
}

bool UVCamComponent::DoesNameExistInSavedStack(const FName InName) const
{
	return SavedModifierStack.ContainsByPredicate([InName](const FModifierStackEntry& StackEntry)
		{
			return StackEntry.Name.IsEqual(InName);
		}
	);
}

void UVCamComponent::FindModifiedStackEntry(int32& ModifiedStackIndex, bool& bIsNewEntry) const
{
	ModifiedStackIndex = INDEX_NONE;
	bIsNewEntry = false;

	// Deletion
	if (ModifierStack.Num() < SavedModifierStack.Num())
	{
		// Early out as there's no modified entry remaining
		return;
	}
	// Addition
	else if (ModifierStack.Num() > SavedModifierStack.Num())
	{
		bIsNewEntry = true;
	}
	
	// Try to find the modified or inserted entry
	for (int32 i = 0; i < SavedModifierStack.Num(); i++)
	{
		if (SavedModifierStack[i] != ModifierStack[i])
		{
			ModifiedStackIndex = i;
			break;
		}
	}

	// If we didn't find a difference then the new item was appended to the end
	if (ModifiedStackIndex == INDEX_NONE)
	{
		ModifiedStackIndex = ModifierStack.Num() - 1;
	}

}

TSharedPtr<FSceneViewport> UVCamComponent::GetTargetSceneViewport() const
{
	TSharedPtr<FSceneViewport> SceneViewport;

#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						SceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
					}
					else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						SceneViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
					}

					// If PIE is active always choose it
					break;
				}
			}
			else if (Context.WorldType == EWorldType::Editor)
			{
				if (FLevelEditorViewportClient* LevelViewportClient = GetTargetLevelViewportClient())
				{
					TSharedPtr<SEditorViewport> ViewportWidget = LevelViewportClient->GetEditorViewportWidget();
					if (ViewportWidget.IsValid())
					{
						SceneViewport = ViewportWidget->GetSceneViewport();
					}
				}
			}
		}
	}
#else
	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		SceneViewport = GameEngine->SceneViewport;
	}
#endif

	return SceneViewport;
}

TWeakPtr<SWindow> UVCamComponent::GetTargetInputWindow() const
{
	TWeakPtr<SWindow> InputWindow;

#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						InputWindow = FSlateApplication::Get().FindWidgetWindow(DestinationLevelViewport->AsWidget());
					}
					else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						InputWindow = SlatePlayInEditorSession->SlatePlayInEditorWindow;
					}

					// If PIE is active always choose it
					break;
				}
			}
			else if (Context.WorldType == EWorldType::Editor)
			{
				if (FLevelEditorViewportClient* LevelViewportClient = GetTargetLevelViewportClient())
				{
					TSharedPtr<SEditorViewport> ViewportWidget = LevelViewportClient->GetEditorViewportWidget();
					if (ViewportWidget.IsValid())
					{
						InputWindow = FSlateApplication::Get().FindWidgetWindow(ViewportWidget.ToSharedRef());
					}
				}
			}
		}
	}
#else
	if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		InputWindow = GameEngine->GameViewportWindow;
	}
#endif

	return InputWindow;
}

#if WITH_EDITOR
FLevelEditorViewportClient* UVCamComponent::GetTargetLevelViewportClient() const
{
	FLevelEditorViewportClient* OutClient = nullptr;

	TSharedPtr<SLevelViewport> LevelViewport = GetTargetLevelViewport();
	if (LevelViewport.IsValid())
	{
		OutClient = &LevelViewport->GetLevelViewportClient();
	}

	return OutClient;
}

TSharedPtr<SLevelViewport> UVCamComponent::GetTargetLevelViewport() const
{
	TSharedPtr<SLevelViewport> OutLevelViewport = nullptr;

	if (TargetViewport == EVCamTargetViewportID::CurrentlySelected)
	{
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(VCamComponent::LevelEditorName))
		{
			OutLevelViewport = LevelEditorModule->GetFirstActiveLevelViewport();
		}
	}
	else
	{
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
			{
				// We only care about the fully rendered 3D viewport...seems like there should be a better way to check for this
				if (!Client->IsOrtho())
				{
					TSharedPtr<SLevelViewport> LevelViewport = StaticCastSharedPtr<SLevelViewport>(Client->GetEditorViewportWidget());
					if (LevelViewport.IsValid())
					{
						const FString WantedViewportString = FString::Printf(TEXT("Viewport %d.Viewport"), (int32)TargetViewport);
						const FString ViewportConfigKey = LevelViewport->GetConfigKey().ToString();
						if (ViewportConfigKey.Contains(*WantedViewportString, ESearchCase::CaseSensitive, ESearchDir::FromStart))
						{
							OutLevelViewport = LevelViewport;
							break;
						}
					}
				}
			}
		}
	}

	return OutLevelViewport;
}

void UVCamComponent::OnMapChanged(UWorld* World, EMapChangeType ChangeType)
{
	UWorld* ComponentWorld = GetWorld();
	if (World == ComponentWorld && ChangeType == EMapChangeType::TearDownWorld)
	{
		OnComponentDestroyed(true);
	}
}

void UVCamComponent::OnBeginPIE(const bool bInIsSimulating)
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	if (World->WorldType == EWorldType::Editor)
	{
		// Deinitialize all output providers in the editor world
		for (UVCamOutputProviderBase* Provider : OutputProviders)
		{
			if (Provider)
			{
				Provider->Deinitialize();
			}
		}

		// Ensure the Editor components do not update during PIE
		bIsEditorObjectButPIEIsRunning = true;
	}
}

void UVCamComponent::OnEndPIE(const bool bInIsSimulating)
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	if (World->WorldType == EWorldType::PIE)
	{
		// Disable all output providers in the PIE world
		for (UVCamOutputProviderBase* Provider : OutputProviders)
		{
			if (Provider)
			{
				Provider->Deinitialize();
			}
		}
	}
	else if (World->WorldType == EWorldType::Editor)
	{
		// Allow the Editor components to start updating again
		bIsEditorObjectButPIEIsRunning = false;
	}
}

void UVCamComponent::SessionStartup(TSharedRef<IConcertClientSession> InSession)
{
	WeakSession = InSession;

	InSession->RegisterCustomEventHandler<FMultiUserVCamCameraComponentEvent>(this, &UVCamComponent::HandleCameraComponentEventData);
	PreviousUpdateTime = FPlatformTime::Seconds();
}

void UVCamComponent::SessionShutdown(TSharedRef<IConcertClientSession> /*InSession*/ )
{
	TSharedPtr<IConcertClientSession> Session = WeakSession.Pin();
	if (Session.IsValid())
	{
		Session->UnregisterCustomEventHandler<FMultiUserVCamCameraComponentEvent>(this);
		for (UVCamOutputProviderBase* Provider : OutputProviders)
		{
			Provider->RestoreOutput();
		}
	}

	WeakSession.Reset();
}

FString UVCamComponent::GetNameForMultiUser() const
{
	return GetOwner()->GetPathName();
}

void UVCamComponent::HandleCameraComponentEventData(const FConcertSessionContext& InEventContext, const FMultiUserVCamCameraComponentEvent& InEvent)
{
	if (InEvent.TrackingName == GetNameForMultiUser())
	{
		// If the role matches the currently defined VP Role then we should not update the camera
		// data for this actor and the modifier stack is the "owner"
		//
		if (!IsCameraInVPRole())
		{
			InEvent.CameraData.ApplyTo(GetOwner(), GetTargetCamera());
			if (bDisableOutputOnMultiUserReceiver)
			{
				for (UVCamOutputProviderBase* Provider : OutputProviders)
				{
					Provider->SuspendOutput();
				}
			}
		}
	}
}

void UVCamComponent::MultiUserStartup()
{
	if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
	{
		IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();

		OnSessionStartupHandle = ConcertClient->OnSessionStartup().AddUObject(this, &UVCamComponent::SessionStartup);
		OnSessionShutdownHandle = ConcertClient->OnSessionShutdown().AddUObject(this, &UVCamComponent::SessionShutdown);

		TSharedPtr<IConcertClientSession> ConcertClientSession = ConcertClient->GetCurrentSession();
		if (ConcertClientSession.IsValid())
		{
			SessionStartup(ConcertClientSession.ToSharedRef());
		}
	}
}
void UVCamComponent::MultiUserShutdown()
{
	if (IMultiUserClientModule::IsAvailable())
	{
		if (TSharedPtr<IConcertSyncClient> ConcertSyncClient = IMultiUserClientModule::Get().GetClient())
		{
			IConcertClientRef ConcertClient = ConcertSyncClient->GetConcertClient();

			TSharedPtr<IConcertClientSession> ConcertClientSession = ConcertClient->GetCurrentSession();
			if (ConcertClientSession.IsValid())
			{
				SessionShutdown(ConcertClientSession.ToSharedRef());
			}

			ConcertClient->OnSessionStartup().Remove(OnSessionStartupHandle);
			OnSessionStartupHandle.Reset();

			ConcertClient->OnSessionShutdown().Remove(OnSessionShutdownHandle);
			OnSessionShutdownHandle.Reset();
		}
	}
}
#endif

// Multi-user support
void UVCamComponent::SendCameraDataViaMultiUser()
{
	if (!IsCameraInVPRole())
	{
		return;
	}
#if WITH_EDITOR
	// Update frequency 15 Hz
	const double LocationUpdateFrequencySeconds = UpdateFrequencyMs / 1000.0;
	const double CurrentTime = FPlatformTime::Seconds();

	double DeltaTime = CurrentTime - PreviousUpdateTime;
	SecondsSinceLastLocationUpdate += DeltaTime;

	if (SecondsSinceLastLocationUpdate >= LocationUpdateFrequencySeconds)
	{
		TSharedPtr<IConcertClientSession> Session = WeakSession.Pin();
		if (Session.IsValid())
		{
			TArray<FGuid> ClientIds = Session->GetSessionClientEndpointIds();
			FMultiUserVCamCameraComponentEvent CameraEvent{GetNameForMultiUser(),{GetOwner(),GetTargetCamera()}};
			Session->SendCustomEvent(CameraEvent, ClientIds, EConcertMessageFlags::None);
		}
		SecondsSinceLastLocationUpdate = 0;
	}
	PreviousUpdateTime = CurrentTime;
#endif
}

bool UVCamComponent::IsCameraInVPRole() const
{
#if WITH_EDITOR
	UVPSettings* Settings = UVPSettings::GetVPSettings();
	// We are in a valid camera role if the user has not assigned a role or the current VPSettings role matches the
	// assigned role.
	//
	return !Role.IsValid() || Settings->GetRoles().HasTag(Role);
#else
	return true;
#endif
}

bool UVCamComponent::CanEvaluateModifierStack() const
{
	return !IsMultiUserSession() || (IsMultiUserSession() && IsCameraInVPRole());
}

bool UVCamComponent::IsMultiUserSession() const
{
#if WITH_EDITOR
	return WeakSession.IsValid();
#else
	return false;
#endif
}

