// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_SoundClass.h"
#include "Sound/SoundClass.h"
#include "AudioEditorModule.h"
#include "Audio/AudioDebug.h"
#include "AudioDeviceManager.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundClass::GetSupportedClass() const
{
	return USoundClass::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundClass::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		LOCTEXT("AssetSoundClassSubMenu", "Classes")
	};
	return SubMenus;
}

void FAssetTypeActions_SoundClass::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		USoundClass* SoundClass = Cast<USoundClass>(*ObjIt);
		if (SoundClass != NULL)
		{
			IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>( "AudioEditor" );
			AudioEditorModule->CreateSoundClassEditor(Mode, EditWithinLevelEditor, SoundClass);
		}
	}
}


void FAssetTypeActions_SoundClass::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	auto Sounds = GetTypedWeakObjectPtrs<USoundClass>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Sound_MuteSound", "Mute"),
		LOCTEXT("Sound_MuteSoundTooltip", "Mutes anything using this SoundClass"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "MediaAsset.AssetActions.Mute.Small"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_SoundClass::ExecuteMute, Sounds),
			FCanExecuteAction::CreateSP(this, &FAssetTypeActions_SoundClass::CanExecuteMuteCommand, Sounds),
			FIsActionChecked::CreateSP(this, &FAssetTypeActions_SoundClass::IsActionCheckedMute, Sounds)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Sound_SoloSound", "Solo"),
		LOCTEXT("Sound_SoloSoundTooltip", "Mutes anything using this SoundClass"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "MediaAsset.AssetActions.Solo.Small"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FAssetTypeActions_SoundClass::ExecuteSolo, Sounds),
			FCanExecuteAction::CreateSP(this, &FAssetTypeActions_SoundClass::CanExecuteSoloCommand, Sounds),
			FIsActionChecked::CreateSP(this, &FAssetTypeActions_SoundClass::IsActionCheckedSolo, Sounds)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);
}

void FAssetTypeActions_SoundClass::ExecuteMute(TArray<TWeakObjectPtr<USoundClass>> Objects) const
{
	FAudioDebugger& Debugger = GEditor->GetAudioDeviceManager()->GetDebugger();
	for (TWeakObjectPtr<USoundClass> i : Objects)
	{		
		if (USoundClass* Class = Cast<USoundClass>(i.Get()))
		{
			Debugger.ToggleMuteSoundClass(Class->GetFName());
		}
	}
}

void FAssetTypeActions_SoundClass::ExecuteSolo(TArray<TWeakObjectPtr<USoundClass>> Objects) const
{
	FAudioDebugger& Debugger = GEditor->GetAudioDeviceManager()->GetDebugger();
	for (TWeakObjectPtr<USoundClass> i : Objects)
	{
		if (USoundClass* Class = Cast<USoundClass>(i.Get()))
		{
			Debugger.ToggleSoloSoundClass(Class->GetFName());
		}
	}
}

bool FAssetTypeActions_SoundClass::IsActionCheckedMute(TArray<TWeakObjectPtr<USoundClass>> Objects) const
{
	FAudioDebugger& Debugger = GEditor->GetAudioDeviceManager()->GetDebugger();
	for (TWeakObjectPtr<USoundClass> i : Objects)
	{
		if (USoundClass* Class = Cast<USoundClass>(i.Get()))
		{
			if (Debugger.IsMuteSoundClass(Class->GetFName()))
			{
				return true;
			}
		}
	}
	return false;
}

bool FAssetTypeActions_SoundClass::IsActionCheckedSolo(TArray<TWeakObjectPtr<USoundClass>> Objects) const
{
	FAudioDebugger& Debugger = GEditor->GetAudioDeviceManager()->GetDebugger();

	for (TWeakObjectPtr<USoundClass> i : Objects)
	{
		if (USoundClass* Class = Cast<USoundClass>(i.Get()))
		{
			if (Debugger.IsSoloSoundClass(Class->GetFName()))
			{
				return true;
			}
		}
	}

	return false;
}

bool FAssetTypeActions_SoundClass::CanExecuteMuteCommand(TArray<TWeakObjectPtr<USoundClass>> Objects) const
{
	FAudioDebugger& Debugger = GEditor->GetAudioDeviceManager()->GetDebugger();

	// Allow muting if we're not Soloing.
	for (TWeakObjectPtr<USoundClass> i : Objects)
	{
		if (USoundClass* Class = Cast<USoundClass>(i.Get()))
		{
			if (Debugger.IsSoloSoundClass(Class->GetFName()))
			{
				return false;
			}
		}
	}

	// Ok.
	return true;
}

bool FAssetTypeActions_SoundClass::CanExecuteSoloCommand(TArray<TWeakObjectPtr<USoundClass>> Objects) const
{
	FAudioDebugger& Debugger = GEditor->GetAudioDeviceManager()->GetDebugger();

	// Allow Soloing if we're not Muting.
	for (TWeakObjectPtr<USoundClass> i : Objects)
	{
		if (USoundClass* Class = Cast<USoundClass>(i.Get()))
		{
			if (Debugger.IsMuteSoundClass(Class->GetFName()))
			{
				return false;
			}
		}
	}

	// Ok.
	return true;
}


#undef LOCTEXT_NAMESPACE
