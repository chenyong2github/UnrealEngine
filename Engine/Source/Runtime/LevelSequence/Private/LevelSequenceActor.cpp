// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceActor.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Texture2D.h"
#include "Components/BillboardComponent.h"
#include "LevelSequenceBurnIn.h"
#include "DefaultLevelSequenceInstanceData.h"
#include "Evaluation/MovieScene3DTransformTemplate.h"
#include "Engine/ActorChannel.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Net/UnrealNetwork.h"
#include "LevelSequenceModule.h"

#if WITH_EDITOR
	#include "PropertyCustomizationHelpers.h"
	#include "ActorPickerMode.h"
	#include "SceneOutlinerFilters.h"
#endif

bool GLevelSequenceActor_InvalidBindingTagWarnings = true;
FAutoConsoleVariableRef CVarLevelSequenceActor_InvalidBindingTagWarnings(
	TEXT("LevelSequence.InvalidBindingTagWarnings"),
	GLevelSequenceActor_InvalidBindingTagWarnings,
	TEXT("Whether to emit a warning when invalid object binding tags are used to override bindings or not.\n"),
	ECVF_Default
);

ALevelSequenceActor::ALevelSequenceActor(const FObjectInitializer& Init)
	: Super(Init)
	, bShowBurnin(true)
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));
	RootComponent = SceneComponent;

#if WITH_EDITORONLY_DATA
	UBillboardComponent* SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> DecalTexture;
			FConstructorStatics() : DecalTexture(TEXT("/Engine/EditorResources/S_LevelSequence")) {}
		};
		static FConstructorStatics ConstructorStatics;

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.DecalTexture.Get();
			SpriteComponent->SetupAttachment(RootComponent);
			SpriteComponent->bIsScreenSizeScaled = true;
			SpriteComponent->SetUsingAbsoluteScale(true);
			SpriteComponent->bReceivesDecals = false;
			SpriteComponent->bHiddenInGame = true;
		}
	}
#endif //WITH_EDITORONLY_DATA

	BindingOverrides = Init.CreateDefaultSubobject<UMovieSceneBindingOverrides>(this, "BindingOverrides");
	BurnInOptions = Init.CreateDefaultSubobject<ULevelSequenceBurnInOptions>(this, "BurnInOptions");
	DefaultInstanceData = Init.CreateDefaultSubobject<UDefaultLevelSequenceInstanceData>(this, "InstanceData");

	// SequencePlayer must be a default sub object for it to be replicated correctly
	SequencePlayer = Init.CreateDefaultSubobject<ULevelSequencePlayer>(this, "AnimationPlayer");

	bOverrideInstanceData = false;

	// The level sequence actor defaults to never ticking by the tick manager because it is ticked separately in LevelTick
	//PrimaryActorTick.bCanEverTick = false;

	bAutoPlay_DEPRECATED = false;

	bReplicates = true;
	bReplicatePlayback = false;
}

void ALevelSequenceActor::PostInitProperties()
{
	Super::PostInitProperties();

	// Have to initialize this here as any properties set on default subobjects inside the constructor
	// Get stomped by the CDO's properties when the constructor exits.
	SequencePlayer->SetPlaybackClient(this);
}

bool ALevelSequenceActor::RetrieveBindingOverrides(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	return BindingOverrides->LocateBoundObjects(InBindingId, InSequenceID, OutObjects);
}

UObject* ALevelSequenceActor::GetInstanceData() const
{
	return bOverrideInstanceData ? DefaultInstanceData : nullptr;
}

ULevelSequencePlayer* ALevelSequenceActor::GetSequencePlayer() const
{
	return SequencePlayer && SequencePlayer->GetSequence() ? SequencePlayer : nullptr;
}

void ALevelSequenceActor::SetReplicatePlayback(bool bInReplicatePlayback)
{
	bReplicatePlayback = bInReplicatePlayback;
	SetReplicates(bReplicatePlayback);
}

bool ALevelSequenceActor::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	bWroteSomething |= Channel->ReplicateSubobject(SequencePlayer, *Bunch, *RepFlags);

	return bWroteSomething;
}

void ALevelSequenceActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALevelSequenceActor, SequencePlayer);
}

void ALevelSequenceActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (HasAuthority())
	{
		SetReplicates(bReplicatePlayback);
	}
	
	InitializePlayer();
}

void ALevelSequenceActor::BeginPlay()
{
	GetWorld()->LevelSequenceActors.Add(this);

	Super::BeginPlay();

	RefreshBurnIn();

	if (PlaybackSettings.bAutoPlay)
	{
		SequencePlayer->Play();
	}
}

void ALevelSequenceActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SequencePlayer)
	{
		// Stop may modify a lot of actor state so it needs to be called
		// during EndPlay (when Actors + World are still valid) instead
		// of waiting for the UObject to be destroyed by GC.
		SequencePlayer->Stop();
	}

 	GetWorld()->LevelSequenceActors.Remove(this);

	Super::EndPlay(EndPlayReason);
}

void ALevelSequenceActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SequencePlayer)
	{
		// If the global instance data implements a transform origin interface, use its transform as an origin for this frame
		{
			UObject*                          InstanceData = GetInstanceData();
			const IMovieSceneTransformOrigin* RawInterface = Cast<IMovieSceneTransformOrigin>(InstanceData);

			const bool bHasInterface = RawInterface || (InstanceData && InstanceData->GetClass()->ImplementsInterface(UMovieSceneTransformOrigin::StaticClass()));
			if (bHasInterface)
			{
				static FSharedPersistentDataKey GlobalTransformDataKey = FGlobalTransformPersistentData::GetDataKey();

				// Retrieve the current origin
				FTransform TransformOrigin = RawInterface ? RawInterface->GetTransformOrigin() : IMovieSceneTransformOrigin::Execute_BP_GetTransformOrigin(InstanceData);

				// Assign the transform origin to the peristent data so it can be queried in Evaluate
				FPersistentEvaluationData PersistentData(*SequencePlayer);
				PersistentData.GetOrAdd<FGlobalTransformPersistentData>(GlobalTransformDataKey).Origin = TransformOrigin;
			}
		}

		SequencePlayer->Update(DeltaSeconds);
	}
}

void ALevelSequenceActor::PostLoad()
{
	Super::PostLoad();

	// If autoplay was previously enabled, initialize the playback settings to autoplay
	if (bAutoPlay_DEPRECATED)
	{
		PlaybackSettings.bAutoPlay = bAutoPlay_DEPRECATED;
		bAutoPlay_DEPRECATED = false;
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// We intentionally do not attempt to load any asset in PostLoad other than by way of LoadPackageAsync
	// since under some circumstances it is possible for the sequence to only be partially loaded.
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	UWorld* LocalWorld = GetWorld();
	if (LevelSequence.IsValid() && LocalWorld && LocalWorld->IsGameWorld())
	{
		// If we're async loading and we don't have the sequence asset loaded, schedule a load for it
		ULevelSequence* LevelSequenceAsset = GetSequence();
		if (!LevelSequenceAsset && IsAsyncLoading())
		{
			LoadPackageAsync(LevelSequence.GetLongPackageName(), FLoadPackageAsyncDelegate::CreateUObject(this, &ALevelSequenceActor::OnSequenceLoaded));
		}
	}

#if WITH_EDITORONLY_DATA
	// Fix sprite component so that it's attached to the root component. In the past, the sprite component was the root component.
	UBillboardComponent* SpriteComponent = FindComponentByClass<UBillboardComponent>();
	if (SpriteComponent && SpriteComponent->GetAttachParent() != RootComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
	}
#endif
}

ULevelSequence* ALevelSequenceActor::GetSequence() const
{
	return Cast<ULevelSequence>(LevelSequence.ResolveObject());
}

ULevelSequence* ALevelSequenceActor::LoadSequence() const
{
	return Cast<ULevelSequence>(LevelSequence.TryLoad());
}

void ALevelSequenceActor::SetSequence(ULevelSequence* InSequence)
{
	if (!SequencePlayer->IsPlaying())
	{
		LevelSequence = InSequence;

		// cbb: should ideally null out the template and player when no sequence is assigned, but that's currently not possible
		if (InSequence)
		{
			SequencePlayer->Initialize(InSequence, GetLevel(), PlaybackSettings, CameraSettings);
		}
	}
}

void ALevelSequenceActor::InitializePlayer()
{
	if (LevelSequence.IsValid() && GetWorld()->IsGameWorld())
	{
		// Attempt to resolve the asset without loading it
		ULevelSequence* LevelSequenceAsset = GetSequence();
		if (LevelSequenceAsset)
		{
			// Level sequence is already loaded. Initialize the player if it's not already initialized with this sequence
			if (LevelSequenceAsset != SequencePlayer->GetSequence())
			{
				SequencePlayer->Initialize(LevelSequenceAsset, GetLevel(), PlaybackSettings, CameraSettings);
			}
		}
		else if (!IsAsyncLoading())
		{
			LevelSequenceAsset = LoadSequence();
			if (LevelSequenceAsset != SequencePlayer->GetSequence())
			{
				SequencePlayer->Initialize(LevelSequenceAsset, GetLevel(), PlaybackSettings, CameraSettings);
			}
		}
		else
		{
			LoadPackageAsync(LevelSequence.GetLongPackageName(), FLoadPackageAsyncDelegate::CreateUObject(this, &ALevelSequenceActor::OnSequenceLoaded));
		}
	}
}

void ALevelSequenceActor::OnSequenceLoaded(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result)
{
	if (Result == EAsyncLoadingResult::Succeeded)
	{
		ULevelSequence* LevelSequenceAsset = GetSequence();
		if (SequencePlayer && SequencePlayer->GetSequence() != LevelSequenceAsset)
		{
			SequencePlayer->Initialize(LevelSequenceAsset, GetLevel(), PlaybackSettings, CameraSettings);
		}
	}
}

void ALevelSequenceActor::HideBurnin()
{
	bShowBurnin = false;
	RefreshBurnIn();

	if (!BurnInOptions)
	{
		UE_LOG(LogLevelSequence, Warning, TEXT("Burnin is not enabled"));
	}
}

void ALevelSequenceActor::ShowBurnin()
{
	bShowBurnin = true;
	RefreshBurnIn();

	if (!BurnInOptions || !BurnInOptions->bUseBurnIn)
	{
		UE_LOG(LogLevelSequence, Warning, TEXT("Burnin will not be visible because it is not enabled"));
	}
}

void ALevelSequenceActor::RefreshBurnIn()
{
	if (BurnInInstance)
	{
		BurnInInstance->RemoveFromViewport();
		BurnInInstance = nullptr;
	}
	
	if (BurnInOptions && BurnInOptions->bUseBurnIn && bShowBurnin)
	{
		// Create the burn-in if necessary
		UClass* Class = BurnInOptions->BurnInClass.TryLoadClass<ULevelSequenceBurnIn>();
		if (Class)
		{
			BurnInInstance = CreateWidget<ULevelSequenceBurnIn>(GetWorld(), Class);
			if (BurnInInstance)
			{
				// Ensure we have a valid settings object if possible
				BurnInOptions->ResetSettings();

				BurnInInstance->SetSettings(BurnInOptions->Settings);
				BurnInInstance->TakeSnapshotsFrom(*this);
				BurnInInstance->AddToViewport();
			}
		}
	}
}

void ALevelSequenceActor::SetBinding(FMovieSceneObjectBindingID Binding, const TArray<AActor*>& Actors, bool bAllowBindingsFromAsset)
{
	if (!Binding.IsValid())
	{
		FMessageLog("PIE")
			.Warning(NSLOCTEXT("LevelSequenceActor", "SetBinding_Warning", "The specified binding ID is not valid"))
			->AddToken(FUObjectToken::Create(this));
	}
	else
	{
		BindingOverrides->SetBinding(Binding, TArray<UObject*>(Actors), bAllowBindingsFromAsset);
		if (SequencePlayer)
		{
			SequencePlayer->State.Invalidate(Binding.GetGuid(), Binding.GetSequenceID());
		}
	}
}

void ALevelSequenceActor::SetBindingByTag(FName BindingTag, const TArray<AActor*>& Actors, bool bAllowBindingsFromAsset)
{
	const UMovieSceneSequence*         Sequence = GetSequence();
	const FMovieSceneObjectBindingIDs* Bindings = Sequence ? Sequence->GetMovieScene()->AllTaggedBindings().Find(BindingTag) : nullptr;
	if (Bindings)
	{
		for (FMovieSceneObjectBindingID ID : Bindings->IDs)
		{
			SetBinding(ID, Actors, bAllowBindingsFromAsset);
		}
	}
	else if (GLevelSequenceActor_InvalidBindingTagWarnings)
	{
		FMessageLog("PIE")
			.Warning(FText::Format(NSLOCTEXT("LevelSequenceActor", "SetBindingByTag", "Sequence did not contain any bindings with the tag '{0}'"), FText::FromName(BindingTag)))
			->AddToken(FUObjectToken::Create(this));
	}
}

void ALevelSequenceActor::AddBinding(FMovieSceneObjectBindingID Binding, AActor* Actor, bool bAllowBindingsFromAsset)
{
	if (!Binding.IsValid())
	{
		FMessageLog("PIE")
			.Warning(NSLOCTEXT("LevelSequenceActor", "AddBinding_Warning", "The specified binding ID is not valid"))
			->AddToken(FUObjectToken::Create(this));
	}
	else
	{
		BindingOverrides->AddBinding(Binding, Actor, bAllowBindingsFromAsset);
		if (SequencePlayer)
		{
			SequencePlayer->State.Invalidate(Binding.GetGuid(), Binding.GetSequenceID());
		}
	}
}

void ALevelSequenceActor::AddBindingByTag(FName BindingTag, AActor* Actor, bool bAllowBindingsFromAsset)
{
	const UMovieSceneSequence*         Sequence = GetSequence();
	const FMovieSceneObjectBindingIDs* Bindings = Sequence ? Sequence->GetMovieScene()->AllTaggedBindings().Find(BindingTag) : nullptr;
	if (Bindings)
	{
		for (FMovieSceneObjectBindingID ID : Bindings->IDs)
		{
			AddBinding(ID, Actor, bAllowBindingsFromAsset);
		}
	}
	else if (GLevelSequenceActor_InvalidBindingTagWarnings)
	{
		FMessageLog("PIE")
			.Warning(FText::Format(NSLOCTEXT("LevelSequenceActor", "AddBindingByTag", "Sequence did not contain any bindings with the tag '{0}'"), FText::FromName(BindingTag)))
			->AddToken(FUObjectToken::Create(this));
	}
}

void ALevelSequenceActor::RemoveBinding(FMovieSceneObjectBindingID Binding, AActor* Actor)
{
	if (!Binding.IsValid())
	{
		FMessageLog("PIE")
			.Warning(NSLOCTEXT("LevelSequenceActor", "RemoveBinding_Warning", "The specified binding ID is not valid"))
			->AddToken(FUObjectToken::Create(this));
	}
	else
	{
		BindingOverrides->RemoveBinding(Binding, Actor);
		if (SequencePlayer)
		{
			SequencePlayer->State.Invalidate(Binding.GetGuid(), Binding.GetSequenceID());
		}
	}
}

void ALevelSequenceActor::RemoveBindingByTag(FName BindingTag, AActor* Actor)
{
	const UMovieSceneSequence*         Sequence = GetSequence();
	const FMovieSceneObjectBindingIDs* Bindings = Sequence ? Sequence->GetMovieScene()->AllTaggedBindings().Find(BindingTag) : nullptr;
	if (Bindings)
	{
		for (FMovieSceneObjectBindingID ID : Bindings->IDs)
		{
			RemoveBinding(ID, Actor);
		}
	}
	else if (GLevelSequenceActor_InvalidBindingTagWarnings)
	{
		FMessageLog("PIE")
			.Warning(FText::Format(NSLOCTEXT("LevelSequenceActor", "RemoveBindingByTag", "Sequence did not contain any bindings with the tag '{0}'"), FText::FromName(BindingTag)))
			->AddToken(FUObjectToken::Create(this));
	}
}

void ALevelSequenceActor::ResetBinding(FMovieSceneObjectBindingID Binding)
{
	if (!Binding.IsValid())
	{
		FMessageLog("PIE")
			.Warning(NSLOCTEXT("LevelSequenceActor", "ResetBinding_Warning", "The specified binding ID is not valid"))
			->AddToken(FUObjectToken::Create(this));
	}
	else
	{
		BindingOverrides->ResetBinding(Binding);
		if (SequencePlayer)
		{
			SequencePlayer->State.Invalidate(Binding.GetGuid(), Binding.GetSequenceID());
		}
	}
}

void ALevelSequenceActor::ResetBindings()
{
	BindingOverrides->ResetBindings();
	if (SequencePlayer)
	{
		SequencePlayer->State.ClearObjectCaches(*SequencePlayer);
	}
}

FMovieSceneObjectBindingID ALevelSequenceActor::FindNamedBinding(FName InBindingName) const
{
	if (ensureAlways(SequencePlayer))
	{
		return SequencePlayer->GetSequence()->FindBindingByTag(InBindingName);
	}
	return FMovieSceneObjectBindingID();
}

const TArray<FMovieSceneObjectBindingID>& ALevelSequenceActor::FindNamedBindings(FName InBindingName) const
{
	if (ensureAlways(SequencePlayer))
	{
		return SequencePlayer->GetSequence()->FindBindingsByTag(InBindingName);
	}

	static TArray<FMovieSceneObjectBindingID> EmptyBindings;
	return EmptyBindings;
}

#if WITH_EDITOR

void FBoundActorProxy::Initialize(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	ReflectedProperty = InPropertyHandle;

	UObject* Object = nullptr;
	ReflectedProperty->GetValue(Object);
	BoundActor = Cast<AActor>(Object);

	ReflectedProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FBoundActorProxy::OnReflectedPropertyChanged));
}

void FBoundActorProxy::OnReflectedPropertyChanged()
{
	UObject* Object = nullptr;
	ReflectedProperty->GetValue(Object);
	BoundActor = Cast<AActor>(Object);
}

TSharedPtr<FStructOnScope> ALevelSequenceActor::GetObjectPickerProxy(TSharedPtr<IPropertyHandle> ObjectPropertyHandle)
{
	TSharedRef<FStructOnScope> Struct = MakeShared<FStructOnScope>(FBoundActorProxy::StaticStruct());
	reinterpret_cast<FBoundActorProxy*>(Struct->GetStructMemory())->Initialize(ObjectPropertyHandle);
	return Struct;
}

void ALevelSequenceActor::UpdateObjectFromProxy(FStructOnScope& Proxy, IPropertyHandle& ObjectPropertyHandle)
{
	UObject* BoundActor = reinterpret_cast<FBoundActorProxy*>(Proxy.GetStructMemory())->BoundActor;
	ObjectPropertyHandle.SetValue(BoundActor);
}

bool ALevelSequenceActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	ULevelSequence* LevelSequenceAsset = LoadSequence();

	if (LevelSequenceAsset)
	{
		Objects.Add(LevelSequenceAsset);
	}

	Super::GetReferencedContentObjects(Objects);

	return true;
}

#endif



ULevelSequenceBurnInOptions::ULevelSequenceBurnInOptions(const FObjectInitializer& Init)
	: Super(Init)
	, bUseBurnIn(false)
	, BurnInClass(TEXT("/Engine/Sequencer/DefaultBurnIn.DefaultBurnIn_C"))
	, Settings(nullptr)
{
}

void ULevelSequenceBurnInOptions::SetBurnIn(FSoftClassPath InBurnInClass)
{
	BurnInClass = InBurnInClass;
	
	// Attempt to load the settings class from the BurnIn class and assign it to our local Settings object.
	ResetSettings();
}


void ULevelSequenceBurnInOptions::ResetSettings()
{
	UClass* Class = BurnInClass.TryLoadClass<ULevelSequenceBurnIn>();
	if (Class)
	{
		TSubclassOf<ULevelSequenceBurnInInitSettings> SettingsClass = Cast<ULevelSequenceBurnIn>(Class->GetDefaultObject())->GetSettingsClass();
		if (SettingsClass)
		{
			if (!Settings || !Settings->IsA(SettingsClass))
			{
				if (Settings)
				{
					Settings->Rename(*MakeUniqueObjectName(this, ULevelSequenceBurnInInitSettings::StaticClass(), "Settings_EXPIRED").ToString());
				}
				
				Settings = NewObject<ULevelSequenceBurnInInitSettings>(this, SettingsClass, "Settings");
				Settings->SetFlags(GetMaskedFlags(RF_PropagateToSubObjects));
			}
		}
		else
		{
			Settings = nullptr;
		}
	}
	else
	{
		Settings = nullptr;
	}
}

#if WITH_EDITOR

void ULevelSequenceBurnInOptions::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULevelSequenceBurnInOptions, bUseBurnIn) || PropertyName == GET_MEMBER_NAME_CHECKED(ULevelSequenceBurnInOptions, BurnInClass))
	{
		ResetSettings();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR
