// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlUIModule.h"

#include "AssetToolsModule.h"
#include "AssetTools/RemoteControlPresetActions.h"
#include "EditorStyleSet.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "PropertyHandle.h"
#include "RemoteControlActor.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"
#include "RemoteControlUISettings.h"
#include "Textures/SlateIcon.h"
#include "UI/Customizations/RemoteControlEntityCustomization.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRCPanelExposedEntitiesList.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "RemoteControlUI"

namespace RemoteControlUIModule
{
	const TArray<FName> CustomizedStructNames = {
		FRemoteControlProperty::StaticStruct()->GetFName(),
        FRemoteControlFunction::StaticStruct()->GetFName(),
        FRemoteControlActor::StaticStruct()->GetFName()
    };
}

void FRemoteControlUIModule::StartupModule()
{
	FRemoteControlPanelStyle::Initialize();
	RegisterAssetTools();
	RegisterDetailRowExtension();
	RegisterContextMenuExtender();
	RegisterStructCustomizations();
	RegisterSettings();
}

void FRemoteControlUIModule::ShutdownModule()
{
	UnregisterSettings();
	UnregisterStructCustomizations();
	UnregisterContextMenuExtender();
	UnregisterDetailRowExtension();
	UnregisterAssetTools();
	FRemoteControlPanelStyle::Shutdown();
}

FDelegateHandle FRemoteControlUIModule::AddPropertyFilter(FOnDisplayExposeIcon OnDisplayExposeIcon)
{
	FDelegateHandle Handle = OnDisplayExposeIcon.GetHandle();
	ExternalFilterDelegates.Add(Handle, MoveTemp(OnDisplayExposeIcon));
	return Handle;
}

void FRemoteControlUIModule::RemovePropertyFilter(const FDelegateHandle& Handle)
{
	ExternalFilterDelegates.Remove(Handle);
}

void FRemoteControlUIModule::RegisterMetadataCustomization(FName MetadataKey, FOnCustomizeMetadataEntry OnCustomizeCallback)
{
	ExternalEntityMetadataCustomizations.FindOrAdd(MetadataKey) = MoveTemp(OnCustomizeCallback);
}

void FRemoteControlUIModule::UnregisterMetadataCustomization(FName MetadataKey)
{
	ExternalEntityMetadataCustomizations.Remove(MetadataKey);
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
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		Module.GetGlobalRowExtensionDelegate().RemoveAll(this);
	}
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

void FRemoteControlUIModule::HandleCreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, TArray<FPropertyRowExtensionButton>& OutExtensions)
{
	FPropertyRowExtensionButton& ExposeButton = OutExtensions.AddDefaulted_GetRef();
	ExposeButton.Icon = TAttribute<FSlateIcon>::Create(
		[this, PropertyHandle = InArgs.PropertyHandle]
		{
			return OnGetExposedIcon(PropertyHandle);
		});

	ExposeButton.Label = LOCTEXT("ExposeProperty", "Expose Property");
	ExposeButton.ToolTip = LOCTEXT("ExposePropertyToolTip", "Expose this property to Remote Control.");
	ExposeButton.UIAction = FUIAction(
		FExecuteAction::CreateRaw(this, &FRemoteControlUIModule::OnToggleExposeProperty, InArgs.PropertyHandle),
		FCanExecuteAction::CreateRaw(this, &FRemoteControlUIModule::CanToggleExposeProperty, InArgs.PropertyHandle),
		FGetActionCheckState::CreateRaw(this, &FRemoteControlUIModule::GetPropertyExposedCheckState, InArgs.PropertyHandle),
		FIsActionButtonVisible::CreateRaw(this, &FRemoteControlUIModule::CanToggleExposeProperty, InArgs.PropertyHandle)
	);
}

FSlateIcon FRemoteControlUIModule::OnGetExposedIcon(TSharedPtr<IPropertyHandle> Handle) const
{
	FName BrushName("NoBrush");

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Panel->GetPreset())
		{
			EPropertyExposeStatus Status = GetPropertyExposeStatus(Handle);
			if (Status == EPropertyExposeStatus::Exposed)
			{
				BrushName = "Level.VisibleIcon16x";
			}
			else
			{
				BrushName = "Level.NotVisibleIcon16x";
			}
		}
	}

	return FSlateIcon(FEditorStyle::Get().GetStyleSetName(), BrushName);
}

bool FRemoteControlUIModule::CanToggleExposeProperty(TSharedPtr<IPropertyHandle> Handle) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Handle)
		{
			if (!ShouldDisplayExposeIcon(Handle.ToSharedRef()))
			{
				return true;
			}
		}
	}

	return false;
}

ECheckBoxState FRemoteControlUIModule::GetPropertyExposedCheckState(TSharedPtr<IPropertyHandle> Handle) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Panel->GetPreset())
		{
			EPropertyExposeStatus ExposeStatus = GetPropertyExposeStatus(Handle);
			if (ExposeStatus == EPropertyExposeStatus::Exposed)
			{
				return ECheckBoxState::Checked;
			}
		}
	}

	return ECheckBoxState::Unchecked;
}

void FRemoteControlUIModule::OnToggleExposeProperty(TSharedPtr<IPropertyHandle> Handle)
{
	if (!ensureMsgf(Handle && Handle->IsValidHandle(), TEXT("Property could not be exposed because the handle was invalid.")))
	{
		return;
	}

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		Panel->ToggleProperty(Handle);
	}
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

bool FRemoteControlUIModule::ShouldDisplayExposeIcon(const TSharedRef<IPropertyHandle>& PropertyHandle) const
{
	if (FProperty* Prop = PropertyHandle->GetProperty())
	{
		// Don't display an expose icon for RCEntities since they're only displayed in the Remote Control Panel.
		if (Prop->GetOwnerStruct() && Prop->GetOwnerStruct()->IsChildOf(FRemoteControlEntity::StaticStruct()))
		{
			return false;
		}

		if (PropertyHandle->GetNumOuterObjects() == 1)
		{
			TArray<UObject*> OuterObjects;
			PropertyHandle->GetOuterObjects(OuterObjects);

			if (OuterObjects[0])
			{
				// Don't display an expose icon for default objects.
				if (OuterObjects[0]->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
				{
					return false;
				}

				// Don't display an expose icon for transient objects such as material editor parameters.
				if (OuterObjects[0]->GetOutermost()->HasAnyFlags(RF_Transient))
				{
					return false;
				}
			}
		}
	}

	for (const TPair<FDelegateHandle, FOnDisplayExposeIcon>& DelegatePair : ExternalFilterDelegates)
	{
		if (DelegatePair.Value.IsBound())
		{
			if (!DelegatePair.Value.Execute(PropertyHandle))
			{
				return false;
			}
		}
	}

	return true;
}

void FRemoteControlUIModule::RegisterStructCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	for (FName Name : RemoteControlUIModule::CustomizedStructNames)
	{
		PropertyEditorModule.RegisterCustomClassLayout(Name, FOnGetDetailCustomizationInstance::CreateStatic(&FRemoteControlEntityCustomization::MakeInstance));
	}
}

void FRemoteControlUIModule::UnregisterStructCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	// Unregister delegates in reverse order.
	for (int8 NameIndex = RemoteControlUIModule::CustomizedStructNames.Num() - 1; NameIndex >= 0; NameIndex--)
	{
		PropertyEditorModule.UnregisterCustomClassLayout(RemoteControlUIModule::CustomizedStructNames[NameIndex]);
	}
}

void FRemoteControlUIModule::RegisterSettings()
{
	GetMutableDefault<URemoteControlUISettings>()->OnSettingChanged().AddRaw(this, &FRemoteControlUIModule::OnSettingsModified);
}

void FRemoteControlUIModule::UnregisterSettings()
{
	if (UObjectInitialized())
	{
		GetMutableDefault<URemoteControlUISettings>()->OnSettingChanged().RemoveAll(this);
	}
}

void FRemoteControlUIModule::OnSettingsModified(UObject*, FPropertyChangedEvent&)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (TSharedPtr<SRCPanelExposedEntitiesList> EntityList = Panel->GetEntityList())
		{
			EntityList->Refresh();
		}	
	}
}

IMPLEMENT_MODULE(FRemoteControlUIModule, RemoteControlUI);

#undef LOCTEXT_NAMESPACE /*RemoteControlUI*/