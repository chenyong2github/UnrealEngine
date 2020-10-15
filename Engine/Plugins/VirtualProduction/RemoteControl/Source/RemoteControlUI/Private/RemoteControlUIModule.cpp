// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlUIModule.h"
#include "AssetToolsModule.h"
#include "AssetTools/RemoteControlPresetActions.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/SWidget.h"
#include "PropertyHandle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Widgets/Images/SImage.h"
#include "Textures/SlateIcon.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UI/SRemoteControlPanel.h"
#include "RemoteControlPreset.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "RemoteControlUI"

void FRemoteControlUIModule::StartupModule()
{
	FRemoteControlPanelStyle::Initialize();
	RegisterAssetTools();
	RegisterDetailRowExtension();
	RegisterContextMenuExtender();
}

void FRemoteControlUIModule::ShutdownModule()
{
	UnregisterContextMenuExtender();
	UnregisterDetailRowExtension();
	UnregisterAssetTools();
	FRemoteControlPanelStyle::Shutdown();
}

TSharedRef<SRemoteControlPanel> FRemoteControlUIModule::CreateRemoteControlPanel(URemoteControlPreset* Preset)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		Panel->SetEditMode(false);
	}

	TSharedRef<SRemoteControlPanel> PanelRef = SAssignNew(WeakActivePanel, SRemoteControlPanel, Preset)
		.OnEditModeChange_Lambda(
			[this](TSharedPtr<SRemoteControlPanel> Panel, bool bEditMode) 
			{
				// Activating the edit mode on a panel sets it as the active panel 
				if (bEditMode)
				{
					if (TSharedPtr<SRemoteControlPanel> ActivePanel = WeakActivePanel.Pin())
					{
						if (ActivePanel != Panel)
						{
							ActivePanel->SetEditMode(false);
						}
					}
					WeakActivePanel = MoveTemp(Panel);
				}
			});

	return PanelRef;
}

void FRemoteControlUIModule::RegisterContextMenuExtender()
{
	// Extend the level viewport context menu to add an option to copy the object path.
	LevelViewportContextMenuRemoteControlExtender = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateRaw(this, &FRemoteControlUIModule::ExtendLevelViewportContextMenuForRemoteControl);
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors>& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	MenuExtenders.Add(LevelViewportContextMenuRemoteControlExtender);
	MenuExtenderDelegateHandle = MenuExtenders.Last().GetHandle();
}

void FRemoteControlUIModule::UnregisterContextMenuExtender()
{
	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll(
			[&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate)
			{
				return Delegate.GetHandle() == MenuExtenderDelegateHandle;
			});
	}
}

void FRemoteControlUIModule::RegisterDetailRowExtension()
{
	FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FOnGenerateGlobalRowExtension& RowExtensionDelegate = Module.GetGlobalRowExtensionDelegate();
	RowExtensionDelegate.AddRaw(this, &FRemoteControlUIModule::HandleCreatePropertyRowExtension);
}

void FRemoteControlUIModule::UnregisterDetailRowExtension()
{
	FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	Module.GetGlobalRowExtensionDelegate().RemoveAll(this);
}

void FRemoteControlUIModule::RegisterAssetTools()
{
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		RemoteControlPresetActions = MakeShared<FRemoteControlPresetActions>(FRemoteControlPanelStyle::Get().ToSharedRef());
		AssetToolsModule->Get().RegisterAssetTypeActions(RemoteControlPresetActions.ToSharedRef());
	}
}

void FRemoteControlUIModule::UnregisterAssetTools()
{
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		AssetToolsModule->Get().UnregisterAssetTypeActions(RemoteControlPresetActions.ToSharedRef());
	}
	RemoteControlPresetActions.Reset();
}

void FRemoteControlUIModule::HandleCreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, FOnGenerateGlobalRowExtensionArgs::EWidgetPosition InWidgetPosition, TArray<TSharedRef<SWidget>>& OutExtensions)
{
	if (InWidgetPosition == FOnGenerateGlobalRowExtensionArgs::EWidgetPosition::Right)
	{
		return;
	}

	TSharedRef<SWidget> ExposeButton = 
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.IsFocusable(false)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.Visibility_Raw(this, &FRemoteControlUIModule::OnGetExposeButtonVisibility, InArgs.PropertyHandle)
			.OnClicked_Raw(this, &FRemoteControlUIModule::OnToggleExposeProperty, InArgs.PropertyHandle)
			[
				SNew(SImage)
				.Image_Raw(this, &FRemoteControlUIModule::OnGetExposedIcon, InArgs.PropertyHandle)
			]
		];
		

	OutExtensions.Add(MoveTemp(ExposeButton));
}

const FSlateBrush* FRemoteControlUIModule::OnGetExposedIcon(TSharedPtr<IPropertyHandle> Handle) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Panel->GetPreset())
		{
			EPropertyExposeStatus Status = GetPropertyExposeStatus(Handle);
			if (Status == EPropertyExposeStatus::Exposed)
			{
				return FEditorStyle::GetBrush(TEXT("Level.VisibleIcon16x"));
			}
			else
			{
				return FEditorStyle::GetBrush(TEXT("Level.NotVisibleIcon16x"));
			}
		}
	}

	return FEditorStyle::GetNoBrush();
}

EVisibility FRemoteControlUIModule::OnGetExposeButtonVisibility(TSharedPtr<IPropertyHandle> Handle) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Panel->GetPreset() && Panel->IsInEditMode())
		{
			EPropertyExposeStatus ExposeStatus = GetPropertyExposeStatus(Handle);
			if (ExposeStatus == EPropertyExposeStatus::Exposed || ExposeStatus == EPropertyExposeStatus::Unexposed)
			{
				return EVisibility::Visible;
			}
			else
			{
				// Show no icon when property is unexposable.
				return EVisibility::Hidden;
			}
		}
	}
	return EVisibility::Collapsed;
}

FReply FRemoteControlUIModule::OnToggleExposeProperty(TSharedPtr<IPropertyHandle> Handle)
{
	if (!ensureMsgf(Handle && Handle->IsValidHandle(), TEXT("Property could not be exposed because the handle was invalid.")))
	{
		return FReply::Handled();
	}

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		Panel->ToggleProperty(Handle);
	}
	return FReply::Handled();
}

FRemoteControlUIModule::EPropertyExposeStatus FRemoteControlUIModule::GetPropertyExposeStatus(const TSharedPtr<IPropertyHandle>& Handle) const
{
	if (Handle && Handle->IsValidHandle())
	{
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
		{
			return Panel->IsExposed(Handle) ? EPropertyExposeStatus::Exposed : EPropertyExposeStatus::Unexposed;
		}
	}

	return EPropertyExposeStatus::Unexposable;
}

void FRemoteControlUIModule::AddGetPathOption(FMenuBuilder& MenuBuilder, AActor* SelectedActor)
{
	auto CopyLambda = [SelectedActor]()
	{
		if (SelectedActor)
		{
			FPlatformApplicationMisc::ClipboardCopy(*(SelectedActor->GetPathName()));
		}
	};

	FUIAction CopyObjectPathAction(FExecuteAction::CreateLambda(MoveTemp(CopyLambda)));
	MenuBuilder.BeginSection("RemoteControl", LOCTEXT("RemoteControlHeading", "Remote Control"));
	MenuBuilder.AddMenuEntry(LOCTEXT("CopyObjectPath", "Copy path"), LOCTEXT("CopyObjectPath_Tooltip", "Copy the actor's path."), FSlateIcon(FCoreStyle::Get().GetStyleSetName(), "GenericCommands.Copy"), CopyObjectPathAction);

	MenuBuilder.EndSection();
}

TSharedRef<FExtender> FRemoteControlUIModule::ExtendLevelViewportContextMenuForRemoteControl(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors)
{
	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();

	if (SelectedActors.Num() == 1)
	{
		Extender->AddMenuExtension("ActorControl", EExtensionHook::After, CommandList,
			FMenuExtensionDelegate::CreateRaw(this, &FRemoteControlUIModule::AddGetPathOption, SelectedActors[0]));
	}

	return Extender.ToSharedRef();
}

IMPLEMENT_MODULE(FRemoteControlUIModule, RemoteControlUI);

#undef LOCTEXT_NAMESPACE /*RemoteControlUI*/