// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/UserWidget.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundBase.h"
#include "Sound/SlateSound.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Components/NamedSlot.h"
#include "Slate/SObjectWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Animation/UMGSequencePlayer.h"
#include "Animation/UMGSequenceTickManager.h"
#include "UObject/UnrealType.h"
#include "Blueprint/WidgetNavigation.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Interfaces/ITargetPlatform.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "UObject/EditorObjectVersion.h"
#include "UMGPrivate.h"
#include "UObject/UObjectHash.h"
#include "UObject/PropertyPortFlags.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "Editor/WidgetCompilerLog.h"
#include "GameFramework/InputSettings.h"

#define LOCTEXT_NAMESPACE "UMG"

TAutoConsoleVariable<bool> CVarUserWidgetUseParallelAnimation(
	TEXT("Widget.UseParallelAnimation"),
	true,
	TEXT("Use multi-threaded evaluation for widget animations."),
	ECVF_Default
);

uint32 UUserWidget::bInitializingFromWidgetTree = 0;

static FGeometry NullGeometry;
static FSlateRect NullRect;
static FWidgetStyle NullStyle;

FSlateWindowElementList& GetNullElementList()
{
	static FSlateWindowElementList NullElementList(nullptr);
	return NullElementList;
}

FPaintContext::FPaintContext()
	: AllottedGeometry(NullGeometry)
	, MyCullingRect(NullRect)
	, OutDrawElements(GetNullElementList())
	, LayerId(0)
	, WidgetStyle(NullStyle)
	, bParentEnabled(true)
	, MaxLayer(0)
{
}

/////////////////////////////////////////////////////
// UUserWidget
UUserWidget::UUserWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bHasScriptImplementedTick(true)
	, bHasScriptImplementedPaint(true)
	, bInitialized(false)
	, bStoppingAllAnimations(false)
	, TickFrequency(EWidgetTickFrequency::Auto)
{
	ViewportAnchors = FAnchors(0, 0, 1, 1);
	Visibility = ESlateVisibility::SelfHitTestInvisible;

	bSupportsKeyboardFocus_DEPRECATED = true;
	bIsFocusable = false;
	ColorAndOpacity = FLinearColor::White;
	ForegroundColor = FSlateColor::UseForeground();

	MinimumDesiredSize = FVector2D(0, 0);

#if WITH_EDITORONLY_DATA
	DesignTimeSize = FVector2D(100, 100);
	PaletteCategory = LOCTEXT("UserCreated", "User Created");
	DesignSizeMode = EDesignPreviewSizeMode::FillScreen;
#endif

	static bool bStaticInit = false;
	if (!bStaticInit)
	{
		bStaticInit = true;
		FLatentActionManager::OnLatentActionsChanged().AddStatic(&UUserWidget::OnLatentActionsChanged);
	}
}

UWidgetBlueprintGeneratedClass* UUserWidget::GetWidgetTreeOwningClass() const
{
	UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
	if (WidgetClass != nullptr)
	{
		WidgetClass = WidgetClass->FindWidgetTreeOwningClass();
	}

	return WidgetClass;
}

bool UUserWidget::Initialize()
{
	// If it's not initialized initialize it, as long as it's not the CDO, we never initialize the CDO.
	if (!bInitialized && !HasAnyFlags(RF_ClassDefaultObject))
	{
		bInitialized = true;

		// If this is a sub-widget of another UserWidget, default designer flags and player context to match those of the owning widget
		if (UUserWidget* OwningUserWidget = GetTypedOuter<UUserWidget>())
		{
#if WITH_EDITOR
			SetDesignerFlags(OwningUserWidget->GetDesignerFlags());
#endif
			SetPlayerContext(OwningUserWidget->GetPlayerContext());
		}

		UWidgetBlueprintGeneratedClass* BGClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
		if (BGClass)
		{
			BGClass = GetWidgetTreeOwningClass();
		}

		// Only do this if this widget is of a blueprint class
		if (BGClass)
		{
			BGClass->InitializeWidget(this);
		}
		else
		{
			InitializeNativeClassData();
		}

		if ( WidgetTree == nullptr )
		{
			WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
		}
		else
		{
			WidgetTree->SetFlags(RF_Transient);

			const bool bReparentToWidgetTree = false;
			InitializeNamedSlots(bReparentToWidgetTree);
		}

		if (!IsDesignTime() && PlayerContext.IsValid())
		{
			NativeOnInitialized();
		}

		return true;
	}

	return false;
}

void UUserWidget::InitializeNamedSlots(bool bReparentToWidgetTree)
{
	for (const FNamedSlotBinding& Binding : NamedSlotBindings )
	{
		if ( UWidget* BindingContent = Binding.Content )
		{
			FObjectPropertyBase* NamedSlotProperty = FindFProperty<FObjectPropertyBase>(GetClass(), Binding.Name);
#if !WITH_EDITOR
			// In editor, renaming a NamedSlot widget will cause this ensure in UpdatePreviewWidget of widget that use that namedslot
			ensure(NamedSlotProperty);
#endif
			if ( NamedSlotProperty ) 
			{
				UNamedSlot* NamedSlot = Cast<UNamedSlot>(NamedSlotProperty->GetObjectPropertyValue_InContainer(this));
				if ( ensure(NamedSlot) )
				{
					NamedSlot->ClearChildren();
					NamedSlot->AddChild(BindingContent);

					//if ( bReparentToWidgetTree )
					//{
					//	FName NewName = MakeUniqueObjectName(WidgetTree, BindingContent->GetClass(), BindingContent->GetFName());
					//	BindingContent->Rename(*NewName.ToString(), WidgetTree, REN_DontCreateRedirectors | REN_DoNotDirty);
					//}
				}
			}
		}
	}
}

void UUserWidget::DuplicateAndInitializeFromWidgetTree(UWidgetTree* InWidgetTree)
{
	TScopeCounter<uint32> ScopeInitializingFromWidgetTree(bInitializingFromWidgetTree);

	if ( ensure(InWidgetTree) )
	{
		FObjectInstancingGraph ObjectInstancingGraph;
		WidgetTree = NewObject<UWidgetTree>(this, InWidgetTree->GetClass(), TEXT("WidgetTree"), RF_Transactional, InWidgetTree, false, &ObjectInstancingGraph);
		WidgetTree->SetFlags(RF_Transient | RF_DuplicateTransient);

		// After using the widget tree as a template, we need to loop over the instanced sub-objects and
		// initialize any UserWidgets, so that they can repeat the process for their children.
		ObjectInstancingGraph.ForEachObjectInstance([this](UObject* Instanced) {
			if (UUserWidget* InstancedSubUserWidget = Cast<UUserWidget>(Instanced))
			{
#if WITH_EDITOR
				InstancedSubUserWidget->SetDesignerFlags(GetDesignerFlags());
#endif
				InstancedSubUserWidget->SetPlayerContext(GetPlayerContext());
				InstancedSubUserWidget->Initialize();
			}
		});
	}
}

void UUserWidget::BeginDestroy()
{
	Super::BeginDestroy();

	TearDownAnimations();

	if (AnimationTickManager)
	{
		AnimationTickManager->RemoveWidget(this);
		AnimationTickManager = nullptr;
	}

	//TODO: Investigate why this would ever be called directly, RemoveFromParent isn't safe to call during GC,
	// as the widget structure may be in a partially destroyed state.

	// If anyone ever calls BeginDestroy explicitly on a widget we need to immediately remove it from
	// the the parent as it may be owned currently by a slate widget.  As long as it's the viewport we're
	// fine.
	RemoveFromParent();

	// If it's not owned by the viewport we need to take more extensive measures.  If the GC widget still
	// exists after this point we should just reset the widget, which will forcefully cause the SObjectWidget
	// to lose access to this UObject.
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->ResetWidget();
	}
}

void UUserWidget::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	
	if ( bInitializingFromWidgetTree )
	{
		// If this is a sub-widget of another UserWidget, default designer flags to match those of the owning widget before initialize.
		if (UUserWidget* OwningUserWidget = GetTypedOuter<UUserWidget>())
		{
#if WITH_EDITOR
			SetDesignerFlags(OwningUserWidget->GetDesignerFlags());	
#endif
			SetPlayerContext(OwningUserWidget->GetPlayerContext());
		}
		Initialize();
	}
}

void UUserWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	UWidget* RootWidget = GetRootWidget();
	if ( RootWidget )
	{
		RootWidget->ReleaseSlateResources(bReleaseChildren);
	}
}

void UUserWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// We get the GCWidget directly because MyWidget could be the fullscreen host widget if we've been added
	// to the viewport.
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		TAttribute<FLinearColor> ColorBinding = PROPERTY_BINDING(FLinearColor, ColorAndOpacity);
		TAttribute<FSlateColor> ForegroundColorBinding = PROPERTY_BINDING(FSlateColor, ForegroundColor);

		SafeGCWidget->SetColorAndOpacity(ColorBinding);
		SafeGCWidget->SetForegroundColor(ForegroundColorBinding);
		SafeGCWidget->SetPadding(Padding);
	}
}

void UUserWidget::SetColorAndOpacity(FLinearColor InColorAndOpacity)
{
	ColorAndOpacity = InColorAndOpacity;

	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->SetColorAndOpacity(ColorAndOpacity);
	}
}

void UUserWidget::SetForegroundColor(FSlateColor InForegroundColor)
{
	ForegroundColor = InForegroundColor;

	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->SetForegroundColor(ForegroundColor);
	}
}

void UUserWidget::SetPadding(FMargin InPadding)
{
	Padding = InPadding;

	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->SetPadding(Padding);
	}
}

UWorld* UUserWidget::GetWorld() const
{
	if ( UWorld* LastWorld = CachedWorld.Get() )
	{
		return LastWorld;
	}

	if ( HasAllFlags(RF_ClassDefaultObject) )
	{
		// If we are a CDO, we must return nullptr instead of calling Outer->GetWorld() to fool UObject::ImplementsGetWorld.
		return nullptr;
	}

	// Use the Player Context's world, if a specific player context is given, otherwise fall back to
	// following the outer chain.
	if ( PlayerContext.IsValid() )
	{
		if ( UWorld* World = PlayerContext.GetWorld() )
		{
			CachedWorld = World;
			return World;
		}
	}

	// Could be a GameInstance, could be World, could also be a WidgetTree, so we're just going to follow
	// the outer chain to find the world we're in.
	UObject* Outer = GetOuter();

	while ( Outer )
	{
		UWorld* World = Outer->GetWorld();
		if ( World )
		{
			CachedWorld = World;
			return World;
		}

		Outer = Outer->GetOuter();
	}

	return nullptr;
}

UUMGSequencePlayer* UUserWidget::GetSequencePlayer(const UWidgetAnimation* InAnimation) const
{
	UUMGSequencePlayer*const* FoundPlayer = ActiveSequencePlayers.FindByPredicate(
		[&](const UUMGSequencePlayer* Player)
	{
		return Player->GetAnimation() == InAnimation;
	});

	return FoundPlayer ? *FoundPlayer : nullptr;
}

UUMGSequencePlayer* UUserWidget::GetOrAddSequencePlayer(UWidgetAnimation* InAnimation)
{
	if (InAnimation && !bStoppingAllAnimations)
	{
		if (!AnimationTickManager)
		{
			AnimationTickManager = UUMGSequenceTickManager::Get(this);
			AnimationTickManager->AddWidget(this);
		}

		// @todo UMG sequencer - Restart animations which have had Play called on them?
		UUMGSequencePlayer* FoundPlayer = nullptr;
		for (UUMGSequencePlayer* Player : ActiveSequencePlayers)
		{
			// We need to make sure we haven't stopped the animation, otherwise it'll get canceled on the next frame.
			if (Player->GetAnimation() == InAnimation
			 && !StoppedSequencePlayers.Contains(Player))
			{
				FoundPlayer = Player;
				break;
			}
		}

		if (!FoundPlayer)
		{
			UUMGSequencePlayer* NewPlayer = NewObject<UUMGSequencePlayer>(this, NAME_None, RF_Transient);
			ActiveSequencePlayers.Add(NewPlayer);

			NewPlayer->InitSequencePlayer(*InAnimation, *this);

			return NewPlayer;
		}
		else
		{
			return FoundPlayer;
		}
	}

	return nullptr;
}

void UUserWidget::TearDownAnimations()
{
	for (UUMGSequencePlayer* Player : ActiveSequencePlayers)
	{
		if (Player)
		{
			Player->TearDown();
		}
	}

	for (UUMGSequencePlayer* Player : StoppedSequencePlayers)
	{
		Player->TearDown();
	}

	ActiveSequencePlayers.Empty();
	StoppedSequencePlayers.Empty();
}

void UUserWidget::Invalidate()
{
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void UUserWidget::Invalidate(EInvalidateWidgetReason InvalidateReason)
{
	if (TSharedPtr<SWidget> CachedWidget = GetCachedWidget())
	{
		UpdateCanTick();
		CachedWidget->Invalidate(InvalidateReason);
	}
}

void UUserWidget::InvalidateFullScreenWidget(EInvalidateWidgetReason InvalidateReason)
{
	if (TSharedPtr<SWidget> FullScreenWidgetPinned = FullScreenWidget.Pin())
	{
		FullScreenWidgetPinned->Invalidate(InvalidateReason);
	}
}

UUMGSequencePlayer* UUserWidget::PlayAnimation(UWidgetAnimation* InAnimation, float StartAtTime, int32 NumberOfLoops, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	SCOPED_NAMED_EVENT_TEXT("Widget::PlayAnimation", FColor::Emerald);

	UUMGSequencePlayer* Player = GetOrAddSequencePlayer(InAnimation);
	if (Player)
	{
		Player->Play(StartAtTime, NumberOfLoops, PlayMode, PlaybackSpeed, bRestoreState);

		OnAnimationStartedPlaying(*Player);

		UpdateCanTick();
	}

	return Player;
}

UUMGSequencePlayer* UUserWidget::PlayAnimationTimeRange(UWidgetAnimation* InAnimation, float StartAtTime, float EndAtTime, int32 NumberOfLoops, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	SCOPED_NAMED_EVENT_TEXT("Widget::PlayAnimationTimeRange", FColor::Emerald);

	UUMGSequencePlayer* Player = GetOrAddSequencePlayer(InAnimation);
	if (Player)
	{
		Player->PlayTo(StartAtTime, EndAtTime, NumberOfLoops, PlayMode, PlaybackSpeed, bRestoreState);

		OnAnimationStartedPlaying(*Player);

		UpdateCanTick();
	}

	return Player;
}

UUMGSequencePlayer* UUserWidget::PlayAnimationForward(UWidgetAnimation* InAnimation, float PlaybackSpeed, bool bRestoreState)
{
	// Don't create the player, only search for it.
	UUMGSequencePlayer* Player = GetSequencePlayer(InAnimation);
	if (Player)
	{
		if (!Player->IsPlayingForward())
		{
			// Reverse the direction we're playing the animation if we're playing it in reverse currently.
			Player->Reverse();
		}

		return Player;
	}

	return PlayAnimation(InAnimation, 0.0f, 1.0f, EUMGSequencePlayMode::Forward, PlaybackSpeed, bRestoreState);
}

UUMGSequencePlayer* UUserWidget::PlayAnimationReverse(UWidgetAnimation* InAnimation, float PlaybackSpeed, bool bRestoreState)
{
	// Don't create the player, only search for it.
	UUMGSequencePlayer* Player = GetSequencePlayer(InAnimation);
	if (Player)
	{
		if (Player->IsPlayingForward())
		{
			// Reverse the direction we're playing the animation if we're playing it in forward currently.
			Player->Reverse();
		}

		return Player;
	}

	return PlayAnimation(InAnimation, 0.0f, 1.0f, EUMGSequencePlayMode::Reverse, PlaybackSpeed, bRestoreState);
}

void UUserWidget::StopAnimation(const UWidgetAnimation* InAnimation)
{
	if (InAnimation)
	{
		// @todo UMG sequencer - Restart animations which have had Play called on them?
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			FoundPlayer->Stop();

			UpdateCanTick();
		}
	}
}

void UUserWidget::StopAllAnimations()
{
	bStoppingAllAnimations = true;
	
	// Stopping players modifies ActiveSequencePlayers, work on a copy aprray
	TArray<UUMGSequencePlayer*, TInlineAllocator<8>> CurrentActivePlayers(ActiveSequencePlayers);
	for (UUMGSequencePlayer* FoundPlayer : CurrentActivePlayers)
	{
		if (FoundPlayer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing)
		{
			FoundPlayer->Stop();
		}
	}
	bStoppingAllAnimations = false;

	UpdateCanTick();
}

float UUserWidget::PauseAnimation(const UWidgetAnimation* InAnimation)
{
	if (InAnimation)
	{
		// @todo UMG sequencer - Restart animations which have had Play called on them?
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			FoundPlayer->Pause();
			return (float)FoundPlayer->GetCurrentTime().AsSeconds();
		}
	}

	return 0;
}

float UUserWidget::GetAnimationCurrentTime(const UWidgetAnimation* InAnimation) const
{
	if (InAnimation)
	{
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			return (float)FoundPlayer->GetCurrentTime().AsSeconds();
		}
	}

	return 0;
}

void UUserWidget::SetAnimationCurrentTime(const UWidgetAnimation* InAnimation, float InTime)
{
	if (InAnimation)
	{
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			FoundPlayer->SetCurrentTime(InTime);
		}
	}
}

bool UUserWidget::IsAnimationPlaying(const UWidgetAnimation* InAnimation) const
{
	if (InAnimation)
	{
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			return FoundPlayer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing;
		}
	}

	return false;
}

bool UUserWidget::IsAnyAnimationPlaying() const
{
	return ActiveSequencePlayers.Num() > 0;
}

void UUserWidget::SetNumLoopsToPlay(const UWidgetAnimation* InAnimation, int32 InNumLoopsToPlay)
{
	if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
	{
		FoundPlayer->SetNumLoopsToPlay(InNumLoopsToPlay);
	}
}

void UUserWidget::SetPlaybackSpeed(const UWidgetAnimation* InAnimation, float PlaybackSpeed)
{
	if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
	{
		FoundPlayer->SetPlaybackSpeed(PlaybackSpeed);
	}
}

void UUserWidget::ReverseAnimation(const UWidgetAnimation* InAnimation)
{
	if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
		FoundPlayer->Reverse();
	}
}

void UUserWidget::OnAnimationStartedPlaying(UUMGSequencePlayer& Player)
{
	OnAnimationStarted(Player.GetAnimation());

	BroadcastAnimationStateChange(Player, EWidgetAnimationEvent::Started);
}

bool UUserWidget::IsAnimationPlayingForward(const UWidgetAnimation* InAnimation)
{
	if (InAnimation)
	{
		UUMGSequencePlayer** FoundPlayer = ActiveSequencePlayers.FindByPredicate([&](const UUMGSequencePlayer* Player) { return Player->GetAnimation() == InAnimation; });

		if (FoundPlayer)
		{
			return (*FoundPlayer)->IsPlayingForward();
		}
	}

	return true;
}

void UUserWidget::OnAnimationFinishedPlaying(UUMGSequencePlayer& Player)
{
	// This event is called directly by the sequence player when the animation finishes.

	OnAnimationFinished(Player.GetAnimation());

	BroadcastAnimationStateChange(Player, EWidgetAnimationEvent::Finished);

	if ( Player.GetPlaybackStatus() == EMovieScenePlayerStatus::Stopped )
	{
		StoppedSequencePlayers.Add(&Player);
	}

	UpdateCanTick();
}

void UUserWidget::BroadcastAnimationStateChange(const UUMGSequencePlayer& Player, EWidgetAnimationEvent AnimationEvent)
{
	const UWidgetAnimation* Animation = Player.GetAnimation();

	// Make a temporary copy of the animation callbacks so that everyone gets a callback
	// even if they're removed as a result of other calls, we don't want order to matter here.
	TArray<FAnimationEventBinding> TempAnimationCallbacks = AnimationCallbacks;

	for (const FAnimationEventBinding& Binding : TempAnimationCallbacks)
	{
		if (Binding.Animation == Animation && Binding.AnimationEvent == AnimationEvent)
		{
			if (Binding.UserTag == NAME_None || Binding.UserTag == Player.GetUserTag())
			{
				Binding.Delegate.ExecuteIfBound();
			}
		}
	}
}

void UUserWidget::PlaySound(USoundBase* SoundToPlay)
{
	if (SoundToPlay)
	{
		FSlateSound NewSound;
		NewSound.SetResourceObject(SoundToPlay);
		FSlateApplication::Get().PlaySound(NewSound);
	}
}

UWidget* UUserWidget::GetWidgetHandle(TSharedRef<SWidget> InWidget)
{
	return WidgetTree->FindWidget(InWidget);
}

TSharedRef<SWidget> UUserWidget::RebuildWidget()
{
	check(!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject));
	
	// In the event this widget is replaced in memory by the blueprint compiler update
	// the widget won't be properly initialized, so we ensure it's initialized and initialize
	// it if it hasn't been.
	if ( !bInitialized )
	{
		Initialize();
	}

	// Setup the player context on sub user widgets, if we have a valid context
	if (PlayerContext.IsValid())
	{
		WidgetTree->ForEachWidget([&] (UWidget* Widget) {
			if ( UUserWidget* UserWidget = Cast<UUserWidget>(Widget) )
			{
				UserWidget->SetPlayerContext(PlayerContext);
			}
		});
	}

	// Add the first component to the root of the widget surface.
	TSharedRef<SWidget> UserRootWidget = WidgetTree->RootWidget ? WidgetTree->RootWidget->TakeWidget() : TSharedRef<SWidget>(SNew(SSpacer));

	return UserRootWidget;
}

void UUserWidget::OnWidgetRebuilt()
{
	// When a user widget is rebuilt we can safely initialize the navigation now since all the slate
	// widgets should be held onto by a smart pointer at this point.
	WidgetTree->ForEachWidget([&] (UWidget* Widget) {
		Widget->BuildNavigation();
	});

	if (!IsDesignTime())
	{
		// Notify the widget to run per-construct.
		NativePreConstruct();

		// Notify the widget that it has been constructed.
		NativeConstruct();
	}
#if WITH_EDITOR
	else if ( HasAnyDesignerFlags(EWidgetDesignFlags::ExecutePreConstruct) )
	{
		bool bCanCallPreConstruct = true;
		if (UWidgetBlueprintGeneratedClass* GeneratedBPClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass()))
		{
			bCanCallPreConstruct = GeneratedBPClass->bCanCallPreConstruct;
		}

		if (bCanCallPreConstruct)
		{
			NativePreConstruct();
		}
	}
#endif
}

TSharedPtr<SWidget> UUserWidget::GetSlateWidgetFromName(const FName& Name) const
{
	UWidget* WidgetObject = GetWidgetFromName(Name);
	return WidgetObject ? WidgetObject->GetCachedWidget() : TSharedPtr<SWidget>();
}

UWidget* UUserWidget::GetWidgetFromName(const FName& Name) const
{
	return WidgetTree ? WidgetTree->FindWidget(Name) : nullptr;
}

void UUserWidget::GetSlotNames(TArray<FName>& SlotNames) const
{
	// Only do this if this widget is of a blueprint class
	if (UWidgetBlueprintGeneratedClass* BGClass = GetWidgetTreeOwningClass())
	{
		SlotNames.Append(BGClass->NamedSlots);
	}
	else if (WidgetTree) // For non-blueprint widget blueprints we have to go through the widget tree to locate the named slots dynamically.
	{
		WidgetTree->ForEachWidget([&SlotNames] (UWidget* Widget) {
			if ( Widget && Widget->IsA<UNamedSlot>() )
			{
				SlotNames.Add(Widget->GetFName());
			}
		});
	}
}

UWidget* UUserWidget::GetContentForSlot(FName SlotName) const
{
	for ( const FNamedSlotBinding& Binding : NamedSlotBindings )
	{
		if ( Binding.Name == SlotName )
		{
			return Binding.Content;
		}
	}

	return nullptr;
}

void UUserWidget::SetContentForSlot(FName SlotName, UWidget* Content)
{
	bool bFoundExistingSlot = false;

	// Find the binding in the existing set and replace the content for that binding.
	for ( int32 BindingIndex = 0; BindingIndex < NamedSlotBindings.Num(); BindingIndex++ )
	{
		FNamedSlotBinding& Binding = NamedSlotBindings[BindingIndex];

		if ( Binding.Name == SlotName )
		{
			bFoundExistingSlot = true;

			if ( Content )
			{
				Binding.Content = Content;
			}
			else
			{
				NamedSlotBindings.RemoveAt(BindingIndex);
			}

			break;
		}
	}

	if ( !bFoundExistingSlot && Content )
	{
		// Add the new binding to the list of bindings.
		FNamedSlotBinding NewBinding;
		NewBinding.Name = SlotName;
		NewBinding.Content = Content;

		NamedSlotBindings.Add(NewBinding);
	}

	// Dynamically insert the new widget into the hierarchy if it exists.
	if ( WidgetTree )
	{
		UNamedSlot* NamedSlot = Cast<UNamedSlot>(WidgetTree->FindWidget(SlotName));
		if ( NamedSlot )
		{
			NamedSlot->ClearChildren();

			if ( Content )
			{
				NamedSlot->AddChild(Content);
			}
		}
	}
}

UWidget* UUserWidget::GetRootWidget() const
{
	if ( WidgetTree )
	{
		return WidgetTree->RootWidget;
	}

	return nullptr;
}

void UUserWidget::AddToViewport(int32 ZOrder)
{
	AddToScreen(nullptr, ZOrder);
}

bool UUserWidget::AddToPlayerScreen(int32 ZOrder)
{
	if ( ULocalPlayer* LocalPlayer = GetOwningLocalPlayer() )
	{
		AddToScreen(LocalPlayer, ZOrder);
		return true;
	}

	FMessageLog("PIE").Error(LOCTEXT("AddToPlayerScreen_NoPlayer", "AddToPlayerScreen Failed.  No Owning Player!"));
	return false;
}

void UUserWidget::AddToScreen(ULocalPlayer* Player, int32 ZOrder)
{
	if ( !FullScreenWidget.IsValid() )
	{
		if ( UPanelWidget* ParentPanel = GetParent() )
		{
			FMessageLog("PIE").Error(FText::Format(LOCTEXT("WidgetAlreadyHasParent", "The widget '{0}' already has a parent widget.  It can't also be added to the viewport!"),
				FText::FromString(GetClass()->GetName())));
			return;
		}

		// First create and initialize the variable so that users calling this function twice don't
		// attempt to add the widget to the viewport again.
		TSharedRef<SConstraintCanvas> FullScreenCanvas = SNew(SConstraintCanvas);
		FullScreenWidget = FullScreenCanvas;

		TSharedRef<SWidget> UserSlateWidget = TakeWidget();

		FullScreenCanvas->AddSlot()
			.Offset(BIND_UOBJECT_ATTRIBUTE(FMargin, GetFullScreenOffset))
			.Anchors(BIND_UOBJECT_ATTRIBUTE(FAnchors, GetAnchorsInViewport))
			.Alignment(BIND_UOBJECT_ATTRIBUTE(FVector2D, GetAlignmentInViewport))
			[
				UserSlateWidget
			];

		// If this is a game world add the widget to the current worlds viewport.
		UWorld* World = GetWorld();
		if ( World && World->IsGameWorld() )
		{
			if ( UGameViewportClient* ViewportClient = World->GetGameViewport() )
			{
				if ( Player )
				{
					ViewportClient->AddViewportWidgetForPlayer(Player, FullScreenCanvas, ZOrder);
				}
				else
				{
					// We add 10 to the zorder when adding to the viewport to avoid 
					// displaying below any built-in controls, like the virtual joysticks on mobile builds.
					ViewportClient->AddViewportWidgetContent(FullScreenCanvas, ZOrder + 10);
				}

				// Just in case we already hooked this delegate, remove the handler.
				FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);

				// Widgets added to the viewport are automatically removed if the persistent level is unloaded.
				FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UUserWidget::OnLevelRemovedFromWorld);
			}
		}
	}
	else
	{
		FMessageLog("PIE").Warning(FText::Format(LOCTEXT("WidgetAlreadyOnScreen", "The widget '{0}' was already added to the screen."),
			FText::FromString(GetClass()->GetName())));
	}
}

void UUserWidget::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	// If the InLevel is null, it's a signal that the entire world is about to disappear, so
	// go ahead and remove this widget from the viewport, it could be holding onto too many
	// dangerous actor references that won't carry over into the next world.
	if ( InLevel == nullptr && InWorld == GetWorld() )
	{
		RemoveFromParent();
	}
}

void UUserWidget::RemoveFromViewport()
{
	RemoveFromParent();
}

void UUserWidget::RemoveFromParent()
{
	if (!HasAnyFlags(RF_BeginDestroyed))
	{
		if (FullScreenWidget.IsValid())
		{
			TSharedPtr<SWidget> WidgetHost = FullScreenWidget.Pin();

			// If this is a game world remove the widget from the current world's viewport.
			UWorld* World = GetWorld();
			if (World && World->IsGameWorld())
			{
				if (UGameViewportClient* ViewportClient = World->GetGameViewport())
				{
					TSharedRef<SWidget> WidgetHostRef = WidgetHost.ToSharedRef();

					ViewportClient->RemoveViewportWidgetContent(WidgetHostRef);

					if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
					{
						ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer, WidgetHostRef);
					}

					FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
				}
			}
		}
		else
		{
			Super::RemoveFromParent();
		}
	}
}

bool UUserWidget::GetIsVisible() const
{
	return FullScreenWidget.IsValid();
}

void UUserWidget::SetVisibility(ESlateVisibility InVisibility)
{
	Super::SetVisibility(InVisibility);
	OnNativeVisibilityChanged.Broadcast(InVisibility);
	OnVisibilityChanged.Broadcast(InVisibility);
}

bool UUserWidget::IsInViewport() const
{
	return FullScreenWidget.IsValid();
}

void UUserWidget::SetPlayerContext(const FLocalPlayerContext& InPlayerContext)
{
	PlayerContext = InPlayerContext;

	if (WidgetTree)
	{
		WidgetTree->ForEachWidget(
			[&InPlayerContext] (UWidget* Widget) 
			{
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
				{
					UserWidget->SetPlayerContext(InPlayerContext);
				}
			});
	}
	
}

const FLocalPlayerContext& UUserWidget::GetPlayerContext() const
{
	return PlayerContext;
}

ULocalPlayer* UUserWidget::GetOwningLocalPlayer() const
{
	if (PlayerContext.IsValid())
	{
		return PlayerContext.GetLocalPlayer();
	}
	return nullptr;
}

void UUserWidget::SetOwningLocalPlayer(ULocalPlayer* LocalPlayer)
{
	if ( LocalPlayer )
	{
		PlayerContext = FLocalPlayerContext(LocalPlayer, GetWorld());
	}
}

APlayerController* UUserWidget::GetOwningPlayer() const
{
	return PlayerContext.IsValid() ? PlayerContext.GetPlayerController() : nullptr;
}

void UUserWidget::SetOwningPlayer(APlayerController* LocalPlayerController)
{
	if (LocalPlayerController && LocalPlayerController->IsLocalController())
	{
		PlayerContext = FLocalPlayerContext(LocalPlayerController);
	}
}

APawn* UUserWidget::GetOwningPlayerPawn() const
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		return PC->GetPawn();
	}

	return nullptr;
}

APlayerCameraManager* UUserWidget::GetOwningPlayerCameraManager() const
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		return PC->PlayerCameraManager;
	}

	return nullptr;
}

void UUserWidget::SetPositionInViewport(FVector2D Position, bool bRemoveDPIScale )
{
	if (bRemoveDPIScale)
	{
		const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
		Position /= Scale;
	}

	FAnchors Zero{ 0.f, 0.f };
	if (ViewportOffsets.Left != Position.X
		|| ViewportOffsets.Top != Position.Y
		|| ViewportAnchors != Zero)
	{
		ViewportOffsets.Left = Position.X;
		ViewportOffsets.Top = Position.Y;
		ViewportAnchors = Zero;
		InvalidateFullScreenWidget(EInvalidateWidgetReason::Layout);
	}
}

void UUserWidget::SetDesiredSizeInViewport(FVector2D DesiredSize)
{
	FAnchors Zero{0.f, 0.f};
	if (ViewportOffsets.Right != DesiredSize.X
		|| ViewportOffsets.Bottom != DesiredSize.Y
		|| ViewportAnchors != Zero)
	{
		ViewportOffsets.Right = DesiredSize.X;
		ViewportOffsets.Bottom = DesiredSize.Y;
		ViewportAnchors = Zero;
		InvalidateFullScreenWidget(EInvalidateWidgetReason::Layout);
	}

}

void UUserWidget::SetAnchorsInViewport(FAnchors Anchors)
{
	if (ViewportAnchors != Anchors)
	{
		ViewportAnchors = Anchors;
		InvalidateFullScreenWidget(EInvalidateWidgetReason::Layout);
	}
}

void UUserWidget::SetAlignmentInViewport(FVector2D Alignment)
{
	if (ViewportAlignment != Alignment)
	{
		ViewportAlignment = Alignment;
		InvalidateFullScreenWidget(EInvalidateWidgetReason::Layout);
	}
}

FMargin UUserWidget::GetFullScreenOffset() const
{
	// If the size is zero, and we're not stretched, then use the desired size.
	FVector2D FinalSize = FVector2D(ViewportOffsets.Right, ViewportOffsets.Bottom);
	if ( FinalSize.IsZero() && !ViewportAnchors.IsStretchedVertical() && !ViewportAnchors.IsStretchedHorizontal() )
	{
		if (TSharedPtr<SWidget> CachedWidget = GetCachedWidget())
		{
			FinalSize = CachedWidget->GetDesiredSize();
		}
	}

	return FMargin(ViewportOffsets.Left, ViewportOffsets.Top, FinalSize.X, FinalSize.Y);
}

FAnchors UUserWidget::GetAnchorsInViewport() const
{
	return ViewportAnchors;
}

FVector2D UUserWidget::GetAlignmentInViewport() const
{
	return ViewportAlignment;
}

void UUserWidget::RemoveObsoleteBindings(const TArray<FName>& NamedSlots)
{
	for (int32 BindingIndex = 0; BindingIndex < NamedSlotBindings.Num(); BindingIndex++)
	{
		const FNamedSlotBinding& Binding = NamedSlotBindings[BindingIndex];

		if (!NamedSlots.Contains(Binding.Name))
		{
			NamedSlotBindings.RemoveAt(BindingIndex);
			BindingIndex--;
		}
	}
}

#if WITH_EDITOR

const FText UUserWidget::GetPaletteCategory()
{
	return PaletteCategory;
}

void UUserWidget::SetDesignerFlags(EWidgetDesignFlags NewFlags)
{
	UWidget::SetDesignerFlags(NewFlags);

	if (WidgetTree)
	{
		if (WidgetTree->RootWidget)
		{
			WidgetTree->RootWidget->SetDesignerFlags(NewFlags);
		}
	}
}

void UUserWidget::OnDesignerChanged(const FDesignerChangedEventArgs& EventArgs)
{
	Super::OnDesignerChanged(EventArgs);

	if ( ensure(WidgetTree) )
	{
		WidgetTree->ForEachWidget([&EventArgs] (UWidget* Widget) {
			Widget->OnDesignerChanged(EventArgs);
		});
	}
}

void UUserWidget::ValidateBlueprint(const UWidgetTree& BlueprintWidgetTree, IWidgetCompilerLog& CompileLog) const
{
	ValidateCompiledDefaults(CompileLog);
	ValidateCompiledWidgetTree(BlueprintWidgetTree, CompileLog);
	BlueprintWidgetTree.ForEachWidget(
		[&CompileLog] (UWidget* Widget)
		{
			Widget->ValidateCompiledDefaults(CompileLog);
		});
}

void UUserWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if ( PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive )
	{
		TSharedPtr<SWidget> SafeWidget = GetCachedWidget();
		if ( SafeWidget.IsValid() )
		{
			// Re-Run execute PreConstruct when we get a post edit property change, to do something
			// akin to running Sync Properties, so users don't have to recompile to see updates.
			NativePreConstruct();
		}
	}
}

#endif

void UUserWidget::OnAnimationStarted_Implementation(const UWidgetAnimation* Animation)
{

}

void UUserWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{

}

void UUserWidget::BindToAnimationStarted(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	FAnimationEventBinding Binding;
	Binding.Animation = InAnimation;
	Binding.Delegate = InDelegate;
	Binding.AnimationEvent = EWidgetAnimationEvent::Started;

	AnimationCallbacks.Add(Binding);
}

void UUserWidget::UnbindFromAnimationStarted(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	AnimationCallbacks.RemoveAll([InAnimation, &InDelegate](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.Delegate == InDelegate && InBinding.AnimationEvent == EWidgetAnimationEvent::Started;
	});
}

void UUserWidget::UnbindAllFromAnimationStarted(UWidgetAnimation* InAnimation)
{
	AnimationCallbacks.RemoveAll([InAnimation](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.AnimationEvent == EWidgetAnimationEvent::Started;
	});
}

void UUserWidget::UnbindAllFromAnimationFinished(UWidgetAnimation* InAnimation)
{
	AnimationCallbacks.RemoveAll([InAnimation](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.AnimationEvent == EWidgetAnimationEvent::Finished;
	});
}

void UUserWidget::BindToAnimationFinished(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	FAnimationEventBinding Binding;
	Binding.Animation = InAnimation;
	Binding.Delegate = InDelegate;
	Binding.AnimationEvent = EWidgetAnimationEvent::Finished;

	AnimationCallbacks.Add(Binding);
}

void UUserWidget::UnbindFromAnimationFinished(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	AnimationCallbacks.RemoveAll([InAnimation, &InDelegate](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.Delegate == InDelegate && InBinding.AnimationEvent == EWidgetAnimationEvent::Finished;
	});
}

void UUserWidget::BindToAnimationEvent(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate, EWidgetAnimationEvent AnimationEvent, FName UserTag)
{
	FAnimationEventBinding Binding;
	Binding.Animation = InAnimation;
	Binding.Delegate = InDelegate;
	Binding.AnimationEvent = AnimationEvent;
	Binding.UserTag = UserTag;

	AnimationCallbacks.Add(Binding);
}

// Native handling for SObjectWidget

void UUserWidget::NativeOnInitialized()
{
	OnInitialized();
}

void UUserWidget::NativePreConstruct()
{
	PreConstruct(IsDesignTime());
}

void UUserWidget::NativeConstruct()
{
	Construct();
	UpdateCanTick();
}

void UUserWidget::NativeDestruct()
{
	StopListeningForAllInputActions();
	Destruct();
}

void UUserWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{ 
	// If this ensure is hit it is likely UpdateCanTick as not called somewhere
	if(ensureMsgf(TickFrequency != EWidgetTickFrequency::Never, TEXT("SObjectWidget and UUserWidget have mismatching tick states or UUserWidget::NativeTick was called manually (Never do this)")))
	{
		GInitRunaway();

#if WITH_EDITOR
		const bool bTickAnimations = !IsDesignTime();
#else
		const bool bTickAnimations = true;
#endif
		if (bTickAnimations)
		{
			if (!CVarUserWidgetUseParallelAnimation.GetValueOnGameThread())
			{
				TickActionsAndAnimation(InDeltaTime);
				PostTickActionsAndAnimation(InDeltaTime);
			}
			// else: the TickManager object will tick all animations at once.

			UWorld* World = GetWorld();
			if (World)
			{
				// Update any latent actions we have for this actor
				World->GetLatentActionManager().ProcessLatentActions(this, InDeltaTime);
			}
		}

		if (bHasScriptImplementedTick)
		{
			Tick(MyGeometry, InDeltaTime);
		}
	}
}

void UUserWidget::TickActionsAndAnimation(float InDeltaTime)
{
	// Don't tick the animation if inside of a PostLoad
	if (FUObjectThreadContext::Get().IsRoutingPostLoad)
	{
		return;
	}

	// Update active movie scenes, none will be removed here, but new
	// ones can be added during the tick, if a player ends and triggers
	// starting another animation
	for (int32 Index = 0; Index < ActiveSequencePlayers.Num(); Index++)
	{
		UUMGSequencePlayer* Player = ActiveSequencePlayers[Index];
		Player->Tick(InDeltaTime);
	}
}

void UUserWidget::PostTickActionsAndAnimation(float InDeltaTime)
{
	const bool bWasPlayingAnimation = IsPlayingAnimation();
	if (bWasPlayingAnimation)
	{ 
		TSharedPtr<SWidget> CachedWidget = GetCachedWidget();
		if (CachedWidget.IsValid())
		{
			CachedWidget->InvalidatePrepass();
		}
	}

	// The process of ticking the players above can stop them so we remove them after all players have ticked
	for (UUMGSequencePlayer* StoppedPlayer : StoppedSequencePlayers)
	{
		ActiveSequencePlayers.RemoveSwap(StoppedPlayer);
		StoppedPlayer->TearDown();
	}

	StoppedSequencePlayers.Empty();
}

void UUserWidget::FlushAnimations()
{
	UUMGSequenceTickManager::Get(this)->ForceFlush();
}

void UUserWidget::CancelLatentActions()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetLatentActionManager().RemoveActionsForObject(this);
		World->GetTimerManager().ClearAllTimersForObject(this);
		UpdateCanTick();
	}
}

void UUserWidget::StopAnimationsAndLatentActions()
{
	StopAllAnimations();
	CancelLatentActions();
}

void UUserWidget::ListenForInputAction( FName ActionName, TEnumAsByte< EInputEvent > EventType, bool bConsume, FOnInputAction Callback )
{
	if ( !InputComponent )
	{
		InitializeInputComponent();
	}

	if ( InputComponent )
	{
		FInputActionBinding NewBinding( ActionName, EventType.GetValue() );
		NewBinding.bConsumeInput = bConsume;
		NewBinding.ActionDelegate.GetDelegateForManualSet().BindUObject( this, &ThisClass::OnInputAction, Callback );

		InputComponent->AddActionBinding( NewBinding );
	}
}

void UUserWidget::StopListeningForInputAction( FName ActionName, TEnumAsByte< EInputEvent > EventType )
{
	if ( InputComponent )
	{
		for ( int32 ExistingIndex = InputComponent->GetNumActionBindings() - 1; ExistingIndex >= 0; --ExistingIndex )
		{
			const FInputActionBinding& ExistingBind = InputComponent->GetActionBinding( ExistingIndex );
			if ( ExistingBind.GetActionName() == ActionName && ExistingBind.KeyEvent == EventType )
			{
				InputComponent->RemoveActionBinding( ExistingIndex );
			}
		}
	}
}

void UUserWidget::StopListeningForAllInputActions()
{
	if ( InputComponent )
	{
		for ( int32 ExistingIndex = InputComponent->GetNumActionBindings() - 1; ExistingIndex >= 0; --ExistingIndex )
		{
			InputComponent->RemoveActionBinding( ExistingIndex );
		}

		UnregisterInputComponent();

		InputComponent->ClearActionBindings();
		InputComponent->MarkPendingKill();
		InputComponent = nullptr;
	}
}

bool UUserWidget::IsListeningForInputAction( FName ActionName ) const
{
	bool bResult = false;
	if ( InputComponent )
	{
		for ( int32 ExistingIndex = InputComponent->GetNumActionBindings() - 1; ExistingIndex >= 0; --ExistingIndex )
		{
			const FInputActionBinding& ExistingBind = InputComponent->GetActionBinding( ExistingIndex );
			if ( ExistingBind.GetActionName() == ActionName )
			{
				bResult = true;
				break;
			}
		}
	}

	return bResult;
}

void UUserWidget::RegisterInputComponent()
{
	if ( InputComponent )
	{
		if ( APlayerController* Controller = GetOwningPlayer() )
		{
			Controller->PushInputComponent(InputComponent);
		}
	}
}

void UUserWidget::UnregisterInputComponent()
{
	if ( InputComponent )
	{
		if ( APlayerController* Controller = GetOwningPlayer() )
		{
			Controller->PopInputComponent(InputComponent);
		}
	}
}

void UUserWidget::SetInputActionPriority( int32 NewPriority )
{
	if ( InputComponent )
	{
		Priority = NewPriority;
		InputComponent->Priority = Priority;
	}
}

void UUserWidget::SetInputActionBlocking( bool bShouldBlock )
{
	if ( InputComponent )
	{
		bStopAction = bShouldBlock;
		InputComponent->bBlockInput = bStopAction;
	}
}

void UUserWidget::OnInputAction( FOnInputAction Callback )
{
	if ( GetIsEnabled() )
	{
		Callback.ExecuteIfBound();
	}
}

void UUserWidget::InitializeInputComponent()
{
	if ( APlayerController* Controller = GetOwningPlayer() )
	{
		InputComponent = NewObject< UInputComponent >( this, UInputSettings::GetDefaultInputComponentClass(), NAME_None, RF_Transient );
		InputComponent->bBlockInput = bStopAction;
		InputComponent->Priority = Priority;
		Controller->PushInputComponent( InputComponent );
	}
	else
	{
		FMessageLog("PIE").Info(FText::Format(LOCTEXT("NoInputListeningWithoutPlayerController", "Unable to listen to input actions without a player controller in {0}."), FText::FromName(GetClass()->GetFName())));
	}
}

void UUserWidget::UpdateCanTick() 
{
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	UWorld* World = GetWorld();

	if(SafeGCWidget.IsValid() && World)
	{
		// Default to never tick, only recompute for auto
		bool bCanTick = false;
		if (TickFrequency == EWidgetTickFrequency::Auto)
		{
			// Note: WidgetBPClass can be NULL in a cooked build, if the Blueprint has been nativized (in that case, it will be a UDynamicClass type).
			UWidgetBlueprintGeneratedClass* WidgetBPClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
			bCanTick |= !WidgetBPClass || WidgetBPClass->ClassRequiresNativeTick();
			bCanTick |= bHasScriptImplementedTick;
			bCanTick |= World->GetLatentActionManager().GetNumActionsForObject(this) != 0;
			bCanTick |= ActiveSequencePlayers.Num() > 0;
		}

		SafeGCWidget->SetCanTick(bCanTick);
	}
}

int32 UUserWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	if ( bHasScriptImplementedPaint )
	{
		FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		OnPaint( Context );

		return FMath::Max(LayerId, Context.MaxLayer);
	}

	return LayerId;
}

void UUserWidget::SetMinimumDesiredSize(FVector2D InMinimumDesiredSize)
{
	if (MinimumDesiredSize != InMinimumDesiredSize)
	{
		MinimumDesiredSize = InMinimumDesiredSize;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

bool UUserWidget::NativeIsInteractable() const
{
	return IsInteractable();
}

bool UUserWidget::NativeSupportsKeyboardFocus() const
{
	return bIsFocusable;
}

FReply UUserWidget::NativeOnFocusReceived( const FGeometry& InGeometry, const FFocusEvent& InFocusEvent )
{
	return OnFocusReceived( InGeometry, InFocusEvent ).NativeReply;
}

void UUserWidget::NativeOnFocusLost( const FFocusEvent& InFocusEvent )
{
	OnFocusLost( InFocusEvent );
}

void UUserWidget::NativeOnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		const bool bDecendantNewlyFocused = NewWidgetPath.ContainsWidget(SafeGCWidget.ToSharedRef());
		if ( bDecendantNewlyFocused )
		{
			const bool bDecendantPreviouslyFocused = PreviousFocusPath.ContainsWidget(SafeGCWidget.ToSharedRef());
			if ( !bDecendantPreviouslyFocused )
			{
				NativeOnAddedToFocusPath( InFocusEvent );
			}
		}
		else
		{
			NativeOnRemovedFromFocusPath( InFocusEvent );
		}
	}
}

void UUserWidget::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	OnAddedToFocusPath(InFocusEvent);
}

void UUserWidget::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	OnRemovedFromFocusPath(InFocusEvent);
}

FNavigationReply UUserWidget::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent, const FNavigationReply& InDefaultReply)
{
	// No Blueprint Support At This Time

	return InDefaultReply;
}

FReply UUserWidget::NativeOnKeyChar( const FGeometry& InGeometry, const FCharacterEvent& InCharEvent )
{
	return OnKeyChar( InGeometry, InCharEvent ).NativeReply;
}

FReply UUserWidget::NativeOnPreviewKeyDown( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	return OnPreviewKeyDown( InGeometry, InKeyEvent ).NativeReply;
}

FReply UUserWidget::NativeOnKeyDown( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	return OnKeyDown( InGeometry, InKeyEvent ).NativeReply;
}

FReply UUserWidget::NativeOnKeyUp( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	return OnKeyUp( InGeometry, InKeyEvent ).NativeReply;
}

FReply UUserWidget::NativeOnAnalogValueChanged( const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent )
{
	return OnAnalogValueChanged( InGeometry, InAnalogEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMouseButtonDown( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonDown( InGeometry, InMouseEvent ).NativeReply;
}

FReply UUserWidget::NativeOnPreviewMouseButtonDown( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnPreviewMouseButtonDown( InGeometry, InMouseEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMouseButtonUp( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonUp(InGeometry, InMouseEvent).NativeReply;
}

FReply UUserWidget::NativeOnMouseMove( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseMove( InGeometry, InMouseEvent ).NativeReply;
}

void UUserWidget::NativeOnMouseEnter( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	OnMouseEnter( InGeometry, InMouseEvent );
}

void UUserWidget::NativeOnMouseLeave( const FPointerEvent& InMouseEvent )
{
	OnMouseLeave( InMouseEvent );
}

FReply UUserWidget::NativeOnMouseWheel( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseWheel( InGeometry, InMouseEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMouseButtonDoubleClick( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonDoubleClick( InGeometry, InMouseEvent ).NativeReply;
}

void UUserWidget::NativeOnDragDetected( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation )
{
	OnDragDetected( InGeometry, InMouseEvent, OutOperation);
}

void UUserWidget::NativeOnDragEnter( const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	OnDragEnter( InGeometry, InDragDropEvent, InOperation );
}

void UUserWidget::NativeOnDragLeave( const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	OnDragLeave( InDragDropEvent, InOperation );
}

bool UUserWidget::NativeOnDragOver( const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	return OnDragOver( InGeometry, InDragDropEvent, InOperation );
}

bool UUserWidget::NativeOnDrop( const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	return OnDrop( InGeometry, InDragDropEvent, InOperation );
}

void UUserWidget::NativeOnDragCancelled( const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	OnDragCancelled( InDragDropEvent, InOperation );
}

FReply UUserWidget::NativeOnTouchGesture( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchGesture( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchStarted( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchStarted( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchMoved( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchMoved( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchEnded( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchEnded( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMotionDetected( const FGeometry& InGeometry, const FMotionEvent& InMotionEvent )
{
	return OnMotionDetected( InGeometry, InMotionEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchForceChanged(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
	return OnTouchForceChanged(InGeometry, InTouchEvent).NativeReply;
}

FCursorReply UUserWidget::NativeOnCursorQuery( const FGeometry& InGeometry, const FPointerEvent& InCursorEvent )
{
	return (bOverride_Cursor)
		? FCursorReply::Cursor(Cursor)
		: FCursorReply::Unhandled();
}

FNavigationReply UUserWidget::NativeOnNavigation(const FGeometry& InGeometry, const FNavigationEvent& InNavigationEvent)
{
	return FNavigationReply::Escape();
}
	
void UUserWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	OnMouseCaptureLost();
}

bool UUserWidget::IsAsset() const
{
	// This stops widget archetypes from showing up in the content browser
	return false;
}

void UUserWidget::PreSave(const class ITargetPlatform* TargetPlatform)
{
	// Remove bindings that are no longer contained in the class.
	if ( UWidgetBlueprintGeneratedClass* BGClass = GetWidgetTreeOwningClass())
	{
		RemoveObsoleteBindings(BGClass->NamedSlots);
	}

	Super::PreSave(TargetPlatform);
}

void UUserWidget::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UUserWidget* DefaultWidget = Cast<UUserWidget>(GetClass()->GetDefaultObject());
		bHasScriptImplementedTick = DefaultWidget->bHasScriptImplementedTick;
		bHasScriptImplementedPaint = DefaultWidget->bHasScriptImplementedPaint;
	}
#endif
}

void UUserWidget::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	if ( Ar.IsLoading() )
	{
		if ( Ar.UE4Ver() < VER_UE4_USERWIDGET_DEFAULT_FOCUSABLE_FALSE )
		{
			bIsFocusable = bSupportsKeyboardFocus_DEPRECATED;
		}
	}
}

/////////////////////////////////////////////////////

UUserWidget* UUserWidget::CreateWidgetInstance(UWidget& OwningWidget, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	UUserWidget* ParentUserWidget = Cast<UUserWidget>(&OwningWidget);
	if (!ParentUserWidget && OwningWidget.GetOuter())
	{
		// If we were given a UWidget, the nearest parent UserWidget is the outer of the UWidget's WidgetTree outer
		ParentUserWidget = Cast<UUserWidget>(OwningWidget.GetOuter()->GetOuter());
	}

	if (ensure(ParentUserWidget && ParentUserWidget->WidgetTree))
	{
		UUserWidget* NewWidget = CreateInstanceInternal(ParentUserWidget->WidgetTree, UserWidgetClass, WidgetName, ParentUserWidget->GetWorld(), ParentUserWidget->GetOwningLocalPlayer());
#if WITH_EDITOR
		if (NewWidget)
		{
			NewWidget->SetDesignerFlags(OwningWidget.GetDesignerFlags());
		}
#endif
		return NewWidget;
	}

	return nullptr;
}

UUserWidget* UUserWidget::CreateWidgetInstance(UWidgetTree& OwningWidgetTree, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	// If the widget tree we're owned by is outered to a UUserWidget great, initialize it like any old widget.
	if (UUserWidget* OwningUserWidget = Cast<UUserWidget>(OwningWidgetTree.GetOuter()))
	{
		return CreateWidgetInstance(*OwningUserWidget, UserWidgetClass, WidgetName);
	}

	return CreateInstanceInternal(&OwningWidgetTree, UserWidgetClass, WidgetName, nullptr, nullptr);
}

UUserWidget* UUserWidget::CreateWidgetInstance(APlayerController& OwnerPC, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	if (!OwnerPC.IsLocalPlayerController())
		{
		const FText FormatPattern = LOCTEXT("NotLocalPlayer", "Only Local Player Controllers can be assigned to widgets. {PlayerController} is not a Local Player Controller.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("PlayerController"), FText::FromName(OwnerPC.GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
		}
	else if (!OwnerPC.Player)
	{
		const FText FormatPattern = LOCTEXT("NoPlayer", "CreateWidget cannot be used on Player Controller with no attached player. {PlayerController} has no Player attached.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("PlayerController"), FText::FromName(OwnerPC.GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
	}
	else if (UWorld* World = OwnerPC.GetWorld())
	{
		UGameInstance* GameInstance = World->GetGameInstance();
		UObject* Outer = GameInstance ? StaticCast<UObject*>(GameInstance) : StaticCast<UObject*>(World);
		return CreateInstanceInternal(Outer, UserWidgetClass, WidgetName, World, CastChecked<ULocalPlayer>(OwnerPC.Player));
	}
	return nullptr;
}

UUserWidget* UUserWidget::CreateWidgetInstance(UGameInstance& GameInstance, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	return CreateInstanceInternal(&GameInstance, UserWidgetClass, WidgetName, GameInstance.GetWorld(), GameInstance.GetFirstGamePlayer());
}

UUserWidget* UUserWidget::CreateWidgetInstance(UWorld& World, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	if (UGameInstance* GameInstance = World.GetGameInstance())
	{
		return CreateWidgetInstance(*GameInstance, UserWidgetClass, WidgetName);
	}
	return CreateInstanceInternal(&World, UserWidgetClass, WidgetName, &World, World.GetFirstLocalPlayerFromController());
}

UUserWidget* UUserWidget::CreateInstanceInternal(UObject* Outer, TSubclassOf<UUserWidget> UserWidgetClass, FName InstanceName, UWorld* World, ULocalPlayer* LocalPlayer)
{
	//CSV_SCOPED_TIMING_STAT(Slate, CreateWidget);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Only do this on a non-shipping or test build.
	if (!CreateWidgetHelpers::ValidateUserWidgetClass(UserWidgetClass))
	{
		return nullptr;
	}
#else
	if (!UserWidgetClass)
	{
		UE_LOG(LogUMG, Error, TEXT("CreateWidget called with a null class."));
		return nullptr;
	}
#endif

#if !UE_BUILD_SHIPPING
	// Check if the world is being torn down before we create a widget for it.
	if (World)
	{
		// Look for indications that widgets are being created for a dead and dying world.
		ensureMsgf(!World->bIsTearingDown, TEXT("Widget Class %s - Attempting to be created while tearing down the world '%s'"), *UserWidgetClass->GetName(), *World->GetName());
	}
#endif

	if (!Outer)
	{
		FMessageLog("PIE").Error(FText::Format(LOCTEXT("OuterNull", "Unable to create the widget {0}, no outer provided."), FText::FromName(UserWidgetClass->GetFName())));
		return nullptr;
	}

	UUserWidget* NewWidget = NewObject<UUserWidget>(Outer, UserWidgetClass, InstanceName, RF_Transactional);
	
	if (LocalPlayer)
	{
		NewWidget->SetPlayerContext(FLocalPlayerContext(LocalPlayer, World));
	}

	NewWidget->Initialize();

	return NewWidget;
}


void UUserWidget::OnLatentActionsChanged(UObject* ObjectWhichChanged, ELatentActionChangeType ChangeType)
{
	if (UUserWidget* WidgetThatChanged = Cast<UUserWidget>(ObjectWhichChanged))
	{
		TSharedPtr<SObjectWidget> SafeGCWidget = WidgetThatChanged->MyGCWidget.Pin();
		if (SafeGCWidget.IsValid())
		{
			bool bCanTick = SafeGCWidget->GetCanTick();

			WidgetThatChanged->UpdateCanTick();

			if (SafeGCWidget->GetCanTick() && !bCanTick)
			{
				// If the widget can now tick, recache the volatility of the widget.
				WidgetThatChanged->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
			}
		}
	}
}


/////////////////////////////////////////////////////

bool CreateWidgetHelpers::ValidateUserWidgetClass(const UClass* UserWidgetClass)
{
	if (UserWidgetClass == nullptr)
	{
		FMessageLog("PIE").Error(LOCTEXT("WidgetClassNull", "CreateWidget called with a null class."));
		return false;
	}

	if (!UserWidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		const FText FormatPattern = LOCTEXT("NotUserWidget", "CreateWidget can only be used on UUserWidget children. {UserWidgetClass} is not a UUserWidget.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("UserWidgetClass"), FText::FromName(UserWidgetClass->GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
		return false;
	}

	if (UserWidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists | CLASS_Deprecated))
	{
		const FText FormatPattern = LOCTEXT("NotValidClass", "Abstract, Deprecated or Replaced classes are not allowed to be used to construct a user widget. {UserWidgetClass} is one of these.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("UserWidgetClass"), FText::FromName(UserWidgetClass->GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
