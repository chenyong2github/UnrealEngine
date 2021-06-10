// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/IMenu.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Engine/EngineTypes.h"
#include "Animation/Skeleton.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Matinee/MatineeAnimInterface.h"
#include "AssetData.h"
#include "Sound/SoundBase.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Matinee/InterpGroup.h"
#include "Matinee/InterpData.h"
#include "Matinee.h"
#include "InterpTrackHelper.h"
#include "MatineeTrackAnimControlHelper.h"
#include "MatineeTrackBoolPropHelper.h"
#include "MatineeTrackDirectorHelper.h"
#include "MatineeTrackEventHelper.h"
#include "MatineeTrackFloatPropHelper.h"
#include "MatineeTrackParticleReplayHelper.h"
#include "MatineeTrackSoundHelper.h"
#include "MatineeTrackToggleHelper.h"
#include "MatineeTrackVectorPropHelper.h"
#include "MatineeTrackColorPropHelper.h"
#include "MatineeTrackLinearColorPropHelper.h"
#include "MatineeTrackVisibilityHelper.h"
#include "MatineeUtils.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Matinee/MatineeActor.h"
#include "Matinee/InterpTrackAnimControl.h"
#include "Matinee/InterpTrackDirector.h"
#include "Matinee/InterpTrackSound.h"
#include "Matinee/InterpTrackEvent.h"
#include "Matinee/InterpTrackToggle.h"
#include "Matinee/InterpTrackVisibility.h"
#include "Matinee/InterpTrackBoolProp.h"
#include "Matinee/InterpTrackColorProp.h"
#include "Matinee/InterpTrackFloatProp.h"
#include "Matinee/InterpTrackVectorProp.h"
#include "Matinee/InterpTrackLinearColorProp.h"
#include "Matinee/InterpGroupInst.h"
#include "Widgets/Input/STextComboPopup.h"
#include "Widgets/Input/STextEntryPopup.h"

#define LOCTEXT_NAMESPACE "MatineeTrackHelpers"

FName	UInterpTrackHelper::KeyframeAddDataName = NAME_None;

UInterpTrackHelper::UInterpTrackHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

AActor* UInterpTrackHelper::GetGroupActor(const UInterpTrack* Track) const
{
	return NULL;
}

// Common FName used just for storing name information while adding Keyframes to tracks.
static UAnimSequence*	KeyframeAddAnimSequence = NULL;
static USoundBase*		KeyframeAddSound = NULL;
static FName			TrackAddPropName = NAME_None;
static FName			AnimSlotName = NAME_None;
static TWeakPtr< class IMenu > EntryMenu;

/**
 * Sets the global property name to use for newly created property tracks
 *
 * @param NewName The property name
 */
void FMatinee::SetTrackAddPropName( const FName NewName )
{
	TrackAddPropName = NewName;
}

UMatineeTrackAnimControlHelper::UMatineeTrackAnimControlHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackAnimControlHelper::PreCreateTrack( UInterpGroup* Group, const UInterpTrack *TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const
{
	
	return false;
}

void UMatineeTrackAnimControlHelper::OnCreateTrackTextEntry(const FString& ChosenText, TSharedRef<SWindow> Window, FString* OutputString)
{
	*OutputString = ChosenText;
	Window->RequestDestroyWindow();
}

void  UMatineeTrackAnimControlHelper::PostCreateTrack( UInterpTrack *Track, bool bDuplicatingTrack, int32 TrackIndex ) const
{
	UInterpTrackAnimControl* AnimTrack = CastChecked<UInterpTrackAnimControl>(Track);
	AnimTrack->SlotName = AnimSlotName;

	// When you change the SlotName, change the TrackTitle to reflect that.
	UInterpTrackAnimControl* DefAnimTrack = AnimTrack->GetClass()->GetDefaultObject<UInterpTrackAnimControl>();
	FString DefaultTrackTitle = DefAnimTrack->TrackTitle;

	if(AnimTrack->SlotName == NAME_None)
	{
		AnimTrack->TrackTitle = DefaultTrackTitle;
	}
	else
	{
		AnimTrack->TrackTitle = FString::Printf( TEXT("%s:%s"), *DefaultTrackTitle, *AnimTrack->SlotName.ToString() );
	}
}


bool UMatineeTrackAnimControlHelper::PreCreateKeyframe( UInterpTrack *Track, float fTime ) const
{
	

	return false;
}

void UMatineeTrackAnimControlHelper::OnAddKeyTextEntry(const FAssetData& AssetData, IMatineeBase* Matinee, UInterpTrack* Track)
{
	if (EntryMenu.IsValid())
	{
		EntryMenu.Pin()->Dismiss();
	}

	UObject* SelectedObject = AssetData.GetAsset();
	if (SelectedObject && SelectedObject->IsA(UAnimSequence::StaticClass()))
	{
		KeyframeAddAnimSequence = CastChecked<UAnimSequence>(AssetData.GetAsset());

		Matinee->FinishAddKey(Track, true);
	}
}

void  UMatineeTrackAnimControlHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	UInterpTrackAnimControl	*AnimTrack = CastChecked<UInterpTrackAnimControl>(Track);
	FAnimControlTrackKey& NewSeqKey = AnimTrack->AnimSeqs[ KeyIndex ];
	NewSeqKey.AnimSeq = KeyframeAddAnimSequence;
	KeyframeAddAnimSequence = NULL;
}


UMatineeTrackDirectorHelper::UMatineeTrackDirectorHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackDirectorHelper::PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const

{
	

	return false;
}

void UMatineeTrackDirectorHelper::OnAddKeyTextEntry(const FString& ChosenText, IMatineeBase* Matinee, UInterpTrack* Track)
{
	if (EntryMenu.IsValid())
	{
		EntryMenu.Pin()->Dismiss();
	}
	
	KeyframeAddDataName = FName( *ChosenText );
	Matinee->FinishAddKey(Track,true);
}


void  UMatineeTrackDirectorHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	UInterpTrackDirector	*DirectorTrack = CastChecked<UInterpTrackDirector>(Track);
	FDirectorTrackCut& NewDirCut = DirectorTrack->CutTrack[ KeyIndex ];
	NewDirCut.TargetCamGroup = KeyframeAddDataName;
	KeyframeAddDataName = NAME_None;
}



UMatineeTrackEventHelper::UMatineeTrackEventHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


bool UMatineeTrackEventHelper::PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const
{
	

	return false;
}

void UMatineeTrackEventHelper::OnAddKeyTextEntry(const FText& ChosenText, ETextCommit::Type CommitInfo, IMatineeBase* Matinee, UInterpTrack* Track)
{
	if (EntryMenu.IsValid())
	{
		EntryMenu.Pin()->Dismiss();
	}

	if (CommitInfo == ETextCommit::OnEnter)
	{
		FString TempString = ChosenText.ToString().Left(NAME_SIZE);
		TempString = TempString.Replace(TEXT(" "),TEXT("_"));
		KeyframeAddDataName = FName( *TempString );

		Matinee->FinishAddKey(Track,true);
	}
}

void  UMatineeTrackEventHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	UInterpTrackEvent	*EventTrack = CastChecked<UInterpTrackEvent>(Track);
	FEventTrackKey& NewEventKey = EventTrack->EventTrack[ KeyIndex ];
	NewEventKey.EventName = KeyframeAddDataName;

	// Update AllEventNames array now we have given it a name
	UInterpGroup* Group = CastChecked<UInterpGroup>( EventTrack->GetOuter() );
	UInterpData* IData = CastChecked<UInterpData>( Group->GetOuter() );
	IData->Modify();
	IData->UpdateEventNames();

	KeyframeAddDataName = NAME_None;
}

UMatineeTrackSoundHelper::UMatineeTrackSoundHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackSoundHelper::PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const
{
	FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();
	KeyframeAddSound = GEditor->GetSelectedObjects()->GetTop<USoundBase>();
	if ( KeyframeAddSound )
	{
		return true;
	}

	FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "NoSoundCueSelected", "Cannot Add Sound. No SoundCue Selected In Browser.") );
	return false;
}


void  UMatineeTrackSoundHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	UInterpTrackSound	*SoundTrack = CastChecked<UInterpTrackSound>(Track);

	// Assign the chosen SoundCue to the new key.
	FSoundTrackKey& NewSoundKey = SoundTrack->Sounds[ KeyIndex ];
	NewSoundKey.Sound = KeyframeAddSound;
	KeyframeAddSound = NULL;
}

UMatineeTrackFloatPropHelper::UMatineeTrackFloatPropHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackFloatPropHelper::PreCreateTrack( UInterpGroup* Group, const UInterpTrack *TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const
{
	return false;
}

void  UMatineeTrackFloatPropHelper::OnCreateTrackTextEntry(const FString& ChosenText, TSharedRef<SWindow> Window, FString* OutputString)
{
	*OutputString = ChosenText;
	Window->RequestDestroyWindow();
}

void  UMatineeTrackFloatPropHelper::PostCreateTrack( UInterpTrack *Track, bool bDuplicatingTrack, int32 TrackIndex ) const
{
	if(bDuplicatingTrack == false)
	{
		UInterpTrackFloatProp	*PropTrack = CastChecked<UInterpTrackFloatProp>(Track);

		// Set track title to property name (cut off component name if there is one).
		FString PropString = TrackAddPropName.ToString();
		int32 PeriodPos = PropString.Find(TEXT("."), ESearchCase::CaseSensitive);
		if(PeriodPos != INDEX_NONE)
		{
			PropString.MidInline(PeriodPos+1, MAX_int32, false);
		}

		PropTrack->PropertyName = TrackAddPropName;
		PropTrack->TrackTitle = MoveTemp(PropString);

		TrackAddPropName = NAME_None;
	}
}




UMatineeTrackBoolPropHelper::UMatineeTrackBoolPropHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


bool UMatineeTrackBoolPropHelper::PreCreateTrack( UInterpGroup* Group, const UInterpTrack* TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const
{
	return false;
}

void UMatineeTrackBoolPropHelper::OnCreateTrackTextEntry(const FString& ChosenText, TWeakPtr<SWindow> Window, FString* OutputString)
{
	*OutputString = ChosenText;
	if( Window.IsValid() )
	{
		Window.Pin()->RequestDestroyWindow();
	}
}

void UMatineeTrackBoolPropHelper::PostCreateTrack( UInterpTrack* Track, bool bDuplicatingTrack, int32 TrackIndex ) const
{
	if(bDuplicatingTrack == false)
	{
		UInterpTrackBoolProp* PropTrack = CastChecked<UInterpTrackBoolProp>(Track);

		// Set track title to property name (cut off component name if there is one).
		FString PropString = TrackAddPropName.ToString();
		int32 PeriodPos = PropString.Find(TEXT("."), ESearchCase::CaseSensitive);
		if(PeriodPos != INDEX_NONE)
		{
			PropString.MidInline(PeriodPos+1, MAX_int32, false);
		}

		PropTrack->PropertyName = TrackAddPropName;
		PropTrack->TrackTitle = MoveTemp(PropString);

		TrackAddPropName = NAME_None;
	}
}


UMatineeTrackToggleHelper::UMatineeTrackToggleHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


bool UMatineeTrackToggleHelper::PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const
{
	return false;
}

void UMatineeTrackToggleHelper::OnAddKeyTextEntry(const FString& ChosenText, IMatineeBase* Matinee, UInterpTrack* Track)
{
	if (EntryMenu.IsValid())
	{
		EntryMenu.Pin()->Dismiss();
	}

	KeyframeAddDataName = FName(*ChosenText);
	Matinee->FinishAddKey(Track,true);
}

void  UMatineeTrackToggleHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	UInterpTrackToggle* ToggleTrack = CastChecked<UInterpTrackToggle>(Track);

	FToggleTrackKey& NewToggleKey = ToggleTrack->ToggleTrack[KeyIndex];
	if (KeyframeAddDataName == FName(TEXT("On")))
	{
		NewToggleKey.ToggleAction = ETTA_On;
	}
	else
		if (KeyframeAddDataName == FName(TEXT("Trigger")))
		{
			NewToggleKey.ToggleAction = ETTA_Trigger;
		}
		else
		{
			NewToggleKey.ToggleAction = ETTA_Off;
		}

		KeyframeAddDataName = NAME_None;
}

UMatineeTrackVectorPropHelper::UMatineeTrackVectorPropHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackVectorPropHelper::ChooseProperty(TArray<FName> &PropNames) const
{
	return false;
}

void UMatineeTrackVectorPropHelper::OnCreateTrackTextEntry(const FString& ChosenText, TWeakPtr<SWindow> Window, FString* OutputString)
{
	*OutputString = ChosenText;
	if( Window.IsValid() )
	{
		Window.Pin()->RequestDestroyWindow();
	}
}

bool UMatineeTrackVectorPropHelper::PreCreateTrack( UInterpGroup* Group, const UInterpTrack *TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const
{
	return false;
}

void  UMatineeTrackVectorPropHelper::PostCreateTrack( UInterpTrack *Track, bool bDuplicatingTrack, int32 TrackIndex ) const
{
	if(bDuplicatingTrack == false)
	{
		UInterpTrackVectorProp	*PropTrack = CastChecked<UInterpTrackVectorProp>(Track);

		// Set track title to property name (cut off component name if there is one).
		FString PropString = TrackAddPropName.ToString();
		int32 PeriodPos = PropString.Find(TEXT("."), ESearchCase::CaseSensitive);
		if(PeriodPos != INDEX_NONE)
		{
			PropString.MidInline(PeriodPos+1, MAX_int32, false);
		}

		PropTrack->PropertyName = TrackAddPropName;
		PropTrack->TrackTitle = MoveTemp(PropString);

		TrackAddPropName = NAME_None;
	}
}

UMatineeTrackColorPropHelper::UMatineeTrackColorPropHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackColorPropHelper::PreCreateTrack( UInterpGroup* Group, const UInterpTrack *TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const
{
	return false;
}

void  UMatineeTrackColorPropHelper::PostCreateTrack( UInterpTrack *Track, bool bDuplicatingTrack, int32 TrackIndex ) const
{
	
}
UMatineeTrackLinearColorPropHelper::UMatineeTrackLinearColorPropHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackLinearColorPropHelper::PreCreateTrack( UInterpGroup* Group, const UInterpTrack *TrackDef, bool bDuplicatingTrack, bool bAllowPrompts ) const
{
	return false;
}

void  UMatineeTrackLinearColorPropHelper::PostCreateTrack( UInterpTrack *Track, bool bDuplicatingTrack, int32 TrackIndex ) const
{
	if(bDuplicatingTrack == false)
	{
		UInterpTrackLinearColorProp	*PropTrack = CastChecked<UInterpTrackLinearColorProp>(Track);

		// Set track title to property name (cut off component name if there is one).
		FString PropString = TrackAddPropName.ToString();
		int32 PeriodPos = PropString.Find(TEXT("."), ESearchCase::CaseSensitive);
		if(PeriodPos != INDEX_NONE)
		{
			PropString.MidInline(PeriodPos+1, MAX_int32, false);
		}

		PropTrack->PropertyName = TrackAddPropName;
		PropTrack->TrackTitle = MoveTemp(PropString);

		TrackAddPropName = NAME_None;
	}
}

UMatineeTrackVisibilityHelper::UMatineeTrackVisibilityHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


bool UMatineeTrackVisibilityHelper::PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const
{
	return false;
}

void UMatineeTrackVisibilityHelper::OnAddKeyTextEntry(const FString& ChosenText, IMatineeBase* Matinee, UInterpTrack* Track)
{
	if (EntryMenu.IsValid())
	{
		EntryMenu.Pin()->Dismiss();
	}

	KeyframeAddDataName = FName(*ChosenText);
	Matinee->FinishAddKey(Track,true);
}

void  UMatineeTrackVisibilityHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	UInterpTrackVisibility* VisibilityTrack = CastChecked<UInterpTrackVisibility>(Track);

	FVisibilityTrackKey& NewVisibilityKey = VisibilityTrack->VisibilityTrack[KeyIndex];

	if (KeyframeAddDataName == FName(TEXT("Show")))
	{
		NewVisibilityKey.Action = EVTA_Show;
	}
	else
		if (KeyframeAddDataName == FName(TEXT("Toggle")))
		{
			NewVisibilityKey.Action = EVTA_Toggle;
		}
		else	// "Hide"
		{
			NewVisibilityKey.Action = EVTA_Hide;
		}


		// Default to Always firing this event.  The user can change it later by right clicking on the
		// track keys in the editor.
		NewVisibilityKey.ActiveCondition = EVTC_Always;

		KeyframeAddDataName = NAME_None;
}



UMatineeTrackParticleReplayHelper::UMatineeTrackParticleReplayHelper(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UMatineeTrackParticleReplayHelper::PreCreateKeyframe( UInterpTrack *Track, float KeyTime ) const
{
	// We don't currently need to do anything here

	// @todo: It would be nice to pop up a dialog where the user can select a clip ID number
	//        from a list of replay clips that exist in emitter actor.

	return true;
}

void  UMatineeTrackParticleReplayHelper::PostCreateKeyframe( UInterpTrack *Track, int32 KeyIndex ) const
{
	// We don't currently need to do anything here
}

#undef LOCTEXT_NAMESPACE
