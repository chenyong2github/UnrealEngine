// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlUIModule.h"

#include "AssetToolsModule.h"
#include "AssetTools/RemoteControlPresetActions.h"
#include "Commands/RemoteControlCommands.h"
#include "EditorStyleSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IMainFrameModule.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "PropertyHandle.h"
#include "RemoteControlActor.h"
#include "RemoteControlField.h"
#include "RemoteControlInstanceMaterial.h"
#include "RemoteControlPreset.h"
#include "RemoteControlSettings.h"
#include "AssetEditor/RemoteControlPresetEditorToolkit.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "UI/Customizations/RemoteControlEntityCustomization.h"
#include "UI/SRCPanelExposedField.h"
#include "UI/SRCPanelExposedActor.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRCPanelExposedEntitiesList.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#define LOCTEXT_NAMESPACE "RemoteControlUI"

const FName FRemoteControlUIModule::EntityDetailsTabName = "RemoteControl_EntityDetails";
const FName FRemoteControlUIModule::RemoteControlPanelTabName = "RemoteControl_RemoteControlPanel";

static const FName DetailsTabIdentifiers[] = {
	"LevelEditorSelectionDetails",
	"LevelEditorSelectionDetails2",
	"LevelEditorSelectionDetails3",
	"LevelEditorSelectionDetails4"
};

namespace RemoteControlUIModule
{
	const TArray<FName>& GetCustomizedStructNames()
	{
		static TArray<FName> CustomizedStructNames =
		{
			FRemoteControlProperty::StaticStruct()->GetFName(),
			FRemoteControlFunction::StaticStruct()->GetFName(),
			FRemoteControlActor::StaticStruct()->GetFName()
		};

		return CustomizedStructNames;
	};
	
	static void OnOpenRemoteControlPanel(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& EditWithinLevelEditor, class URemoteControlPreset* Preset)
	{
		TSharedRef<FRemoteControlPresetEditorToolkit> Toolkit = FRemoteControlPresetEditorToolkit::CreateEditor(Mode, EditWithinLevelEditor,  Preset);
		Toolkit->InitRemoteControlPresetEditor(Mode, EditWithinLevelEditor, Preset);
	}

	bool IsStaticOrSkeletalMaterial(TSharedPtr<IPropertyHandle> InParentPropertyHandle)
	{
		if (!InParentPropertyHandle.IsValid() || !InParentPropertyHandle->IsValidHandle() || InParentPropertyHandle->GetNumOuterObjects() == 0)
		{
			return false;
		}

		if (FProperty* OwnerProperty = InParentPropertyHandle->GetProperty())
		{
			TArray<UObject*> OuterObjects;

			InParentPropertyHandle->GetOuterObjects(OuterObjects);

			const bool bIsStaticMaterial = OwnerProperty->GetFName() == UStaticMesh::GetStaticMaterialsName() && OuterObjects[0]->GetClass()->IsChildOf(UStaticMesh::StaticClass());
			
			const bool bIsSkeletalMaterial = OwnerProperty->GetFName() == USkeletalMesh::GetMaterialsMemberName() && OuterObjects[0]->GetClass()->IsChildOf(USkeletalMesh::StaticClass());

			return bIsStaticMaterial || bIsSkeletalMaterial;
		}

		return false;
	}

	bool IsTransientObjectAllowListed(UObject* Object)
	{
		return Object && Object->IsA<UDEditorParameterValue>();
	}
}

void FRemoteControlUIModule::StartupModule()
{
	FRemoteControlPanelStyle::Initialize();
	BindRemoteControlCommands();
	RegisterAssetTools();
	RegisterDetailRowExtension();
	RegisterContextMenuExtender();
	RegisterEvents();
	RegisterStructCustomizations();
	RegisterSettings();
	RegisterWidgetFactories();
}

void FRemoteControlUIModule::ShutdownModule()
{
	UnregisterSettings();
	UnregisterStructCustomizations();
	UnregisterEvents();
	UnregisterContextMenuExtender();
	UnregisterDetailRowExtension();
	UnregisterAssetTools();
	UnbindRemoteControlCommands();
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

TSharedRef<SRemoteControlPanel> FRemoteControlUIModule::CreateRemoteControlPanel(URemoteControlPreset* Preset, const TSharedPtr<IToolkitHost>& ToolkitHost)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		Panel->SetEditMode(false);
	}

	TSharedRef<SRemoteControlPanel> PanelRef = SAssignNew(WeakActivePanel, SRemoteControlPanel, Preset, ToolkitHost)
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

	// NOTE : Reregister the module with the detail panel when this panel is created.

	if (!SharedDetailsPanel.IsValid())
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		for (const FName& DetailsTabIdentifier : DetailsTabIdentifiers)
		{
			SharedDetailsPanel = PropertyEditor.FindDetailView(DetailsTabIdentifier);
			
			if (SharedDetailsPanel.IsValid())
			{
				break;
			}
		}
	}

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

void FRemoteControlUIModule::RegisterEvents()
{
	FEditorDelegates::PostUndoRedo.AddRaw(this, &FRemoteControlUIModule::RefreshPanels);
}

void FRemoteControlUIModule::UnregisterEvents()
{
	FEditorDelegates::PostUndoRedo.RemoveAll(this);
}

void FRemoteControlUIModule::ToggleEditMode()
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin()) // Check whether the panel is active.
	{
		Panel->SetEditMode(Panel->IsInEditMode() ? false : true);
	}
}

bool FRemoteControlUIModule::CanToggleEditMode() const
{
	return WeakActivePanel.IsValid();
}

bool FRemoteControlUIModule::IsInEditMode() const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin()) // Check whether the panel is active.
	{
		return Panel->IsInEditMode();
	}

	return false;
}

URemoteControlPreset* FRemoteControlUIModule::GetActivePreset() const
{
	if (const TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		return Panel->GetPreset();
	}
	return nullptr;
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

void FRemoteControlUIModule::BindRemoteControlCommands()
{
	FRemoteControlCommands::Register();

	const FRemoteControlCommands& Commands = FRemoteControlCommands::Get();

	IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

	FUICommandList& ActionList = *MainFrame.GetMainFrameCommandBindings();

	// Toggle Edit Mode

	ActionList.MapAction(Commands.ToggleEditMode,
		FExecuteAction::CreateRaw(this, &FRemoteControlUIModule::ToggleEditMode),
		FCanExecuteAction::CreateRaw(this, &FRemoteControlUIModule::CanToggleEditMode),
		FIsActionChecked::CreateRaw(this, &FRemoteControlUIModule::IsInEditMode),
		FIsActionButtonVisible::CreateRaw(this, &FRemoteControlUIModule::CanToggleEditMode)
	);
}

void FRemoteControlUIModule::UnbindRemoteControlCommands()
{
	const FRemoteControlCommands& Commands = FRemoteControlCommands::Get();

	IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

	FUICommandList& ActionList = *MainFrame.GetMainFrameCommandBindings();

	ActionList.UnmapAction(Commands.ToggleEditMode);

	FRemoteControlCommands::Unregister();
}

void FRemoteControlUIModule::HandleCreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, TArray<FPropertyRowExtensionButton>& OutExtensions)
{
	// Expose/Unexpose button.

	FPropertyRowExtensionButton& ExposeButton = OutExtensions.AddDefaulted_GetRef();
	ExposeButton.Icon = TAttribute<FSlateIcon>::Create(
		[this, PropertyHandle = InArgs.PropertyHandle]
		{
			return OnGetExposedIcon(PropertyHandle);
		});

	ExposeButton.Label = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FRemoteControlUIModule::GetExposePropertyButtonText, InArgs.PropertyHandle));
	ExposeButton.ToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FRemoteControlUIModule::GetExposePropertyButtonTooltip, InArgs.PropertyHandle));
	ExposeButton.UIAction = FUIAction(
		FExecuteAction::CreateRaw(this, &FRemoteControlUIModule::OnToggleExposeProperty, InArgs.PropertyHandle),
		FCanExecuteAction::CreateRaw(this, &FRemoteControlUIModule::CanToggleExposeProperty, InArgs.PropertyHandle),
		FGetActionCheckState::CreateRaw(this, &FRemoteControlUIModule::GetPropertyExposedCheckState, InArgs.PropertyHandle),
		FIsActionButtonVisible::CreateRaw(this, &FRemoteControlUIModule::CanToggleExposeProperty, InArgs.PropertyHandle)
	);

	// Override material(s) warning.

	FPropertyRowExtensionButton& OverrideMaterialButton = OutExtensions.AddDefaulted_GetRef();

	OverrideMaterialButton.Icon = TAttribute<FSlateIcon>::Create(
		[this, PropertyHandle = InArgs.PropertyHandle]
		{
			return OnGetOverrideMaterialsIcon(PropertyHandle);
		}
		);

	OverrideMaterialButton.Label = LOCTEXT("OverrideMaterial", "Override Material");

	OverrideMaterialButton.ToolTip = LOCTEXT("OverrideMaterialToolTip", "Click to override this material in order to expose this property to Remote Control.");

	OverrideMaterialButton.UIAction = FUIAction(
		FExecuteAction::CreateRaw(this, &FRemoteControlUIModule::TryOverridingMaterials, InArgs.PropertyHandle),
		FCanExecuteAction::CreateRaw(this, &FRemoteControlUIModule::IsStaticOrSkeletalMaterialProperty, InArgs.PropertyHandle),
		FGetActionCheckState(),
		FIsActionButtonVisible::CreateRaw(this, &FRemoteControlUIModule::IsStaticOrSkeletalMaterialProperty, InArgs.PropertyHandle)
	);

	WeakDetailsTreeNode = InArgs.OwnerTreeNode;
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

	return FSlateIcon(FAppStyle::Get().GetStyleSetName(), BrushName);
}

bool FRemoteControlUIModule::CanToggleExposeProperty(TSharedPtr<IPropertyHandle> Handle) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		return Handle && ShouldDisplayExposeIcon(Handle.ToSharedRef());
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

FSlateIcon FRemoteControlUIModule::OnGetOverrideMaterialsIcon(TSharedPtr<IPropertyHandle> Handle) const
{
	FName BrushName("NoBrush");

	if (IsStaticOrSkeletalMaterialProperty(Handle))
	{
		BrushName = "Icons.Warning";
	}

	return FSlateIcon(FAppStyle::Get().GetStyleSetName(), BrushName);
}

bool FRemoteControlUIModule::IsStaticOrSkeletalMaterialProperty(TSharedPtr<IPropertyHandle> Handle) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin()) // Check whether the panel is active.
	{
		if (Panel->GetPreset() && Handle && Handle->IsValidHandle()) // Ensure that we have a valid preset and handle.
		{
			if (TSharedPtr<IPropertyHandle> ParentHandle = Handle->GetParentHandle())
			{
				return RemoteControlUIModule::IsStaticOrSkeletalMaterial(ParentHandle);
			}
		}
	}

	return false;
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
		Extender->AddMenuExtension("ActorTypeTools", EExtensionHook::After, CommandList,
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

		// Don't display an expose icon for Static or Skeletal Materials as they need to be overriden.
		if (TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle())
		{
			if (RemoteControlUIModule::IsStaticOrSkeletalMaterial(ParentHandle))
			{
				return false;
			}
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
				if (OuterObjects[0]->GetOutermost()->HasAnyFlags(RF_Transient) && !RemoteControlUIModule::IsTransientObjectAllowListed(OuterObjects[0]))
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
	
	for (FName Name : RemoteControlUIModule::GetCustomizedStructNames())
	{
		PropertyEditorModule.RegisterCustomClassLayout(Name, FOnGetDetailCustomizationInstance::CreateStatic(&FRemoteControlEntityCustomization::MakeInstance));
	}
}

void FRemoteControlUIModule::UnregisterStructCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	// Unregister delegates in reverse order.
	for (int8 NameIndex = RemoteControlUIModule::GetCustomizedStructNames().Num() - 1; NameIndex >= 0; NameIndex--)
	{
		PropertyEditorModule.UnregisterCustomClassLayout(RemoteControlUIModule::GetCustomizedStructNames()[NameIndex]);
	}
}

void FRemoteControlUIModule::RegisterSettings()
{
	GetMutableDefault<URemoteControlSettings>()->OnSettingChanged().AddRaw(this, &FRemoteControlUIModule::OnSettingsModified);
}

void FRemoteControlUIModule::UnregisterSettings()
{
	if (UObjectInitialized())
	{
		GetMutableDefault<URemoteControlSettings>()->OnSettingChanged().RemoveAll(this);
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

void FRemoteControlUIModule::RegisterWidgetFactoryForType(UScriptStruct* RemoteControlEntityType, const FOnGenerateRCWidget& OnGenerateRCWidgetDelegate)
{
	if (!GenerateWidgetDelegates.Contains(RemoteControlEntityType))
	{
		GenerateWidgetDelegates.Add(RemoteControlEntityType, OnGenerateRCWidgetDelegate);
	}
}

void FRemoteControlUIModule::UnregisterWidgetFactoryForType(UScriptStruct* RemoteControlEntityType)
{
	GenerateWidgetDelegates.Remove(RemoteControlEntityType);
}

void FRemoteControlUIModule::RegisterWidgetFactories()
{
	RegisterWidgetFactoryForType(FRemoteControlActor::StaticStruct(), FOnGenerateRCWidget::CreateStatic(&SRCPanelExposedActor::MakeInstance));
	RegisterWidgetFactoryForType(FRemoteControlProperty::StaticStruct(), FOnGenerateRCWidget::CreateStatic(&SRCPanelExposedField::MakeInstance));
	RegisterWidgetFactoryForType(FRemoteControlFunction::StaticStruct(), FOnGenerateRCWidget::CreateStatic(&SRCPanelExposedField::MakeInstance));
	RegisterWidgetFactoryForType(FRemoteControlInstanceMaterial::StaticStruct(), FOnGenerateRCWidget::CreateStatic(&SRCPanelExposedField::MakeInstance));
}

FText FRemoteControlUIModule::GetExposePropertyButtonTooltip(TSharedPtr<IPropertyHandle> Handle) const
{
	if (URemoteControlPreset* Preset = GetActivePreset())
	{
		const FText PresetName = FText::FromString(Preset->GetName());
		if (GetPropertyExposeStatus(Handle) == EPropertyExposeStatus::Exposed)
		{
			return FText::Format(LOCTEXT("ExposePropertyToolTip", "Unexpose this property from RemoteControl Preset '{0}'."), PresetName);
		}
		else
		{
			return FText::Format(LOCTEXT("UnexposePropertyToolTip", "Expose this property in RemoteControl Preset '{0}'."), PresetName);
		}
	}

	return LOCTEXT("InvalidExposePropertyTooltip", "Invalid Preset");
}

FText FRemoteControlUIModule::GetExposePropertyButtonText(TSharedPtr<IPropertyHandle> Handle) const
{
	if (GetPropertyExposeStatus(Handle) == EPropertyExposeStatus::Exposed)
	{
		return LOCTEXT("ExposePropertyToolTip", "Unexpose property");
	}
	else
	{
		return LOCTEXT("UnexposePropertyToolTip", "Expose property");
	}
}

void FRemoteControlUIModule::TryOverridingMaterials(TSharedPtr<IPropertyHandle> ForThisProperty)
{
	if (!ensureMsgf(ForThisProperty && ForThisProperty->IsValidHandle(), TEXT("Property could not be exposed because the handle was invalid.")))
	{
		return;
	}

	UObject* OriginalMaterialAsObject;

	if (ForThisProperty->GetValue(OriginalMaterialAsObject) == FPropertyAccess::Success)
	{
		if (UMaterialInterface* OriginalMaterial = Cast<UMaterialInterface>(OriginalMaterialAsObject))
		{
			FName MaterialSlotNameToBeOverriden;

			// NOTE : Obtain the source object information from the property.

			if (ForThisProperty->GetNumOuterObjects() == 1)
			{
				TArray<UObject*> OuterObjects;

				ForThisProperty->GetOuterObjects(OuterObjects);

				if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(OuterObjects[0]))
				{
					// NOTE : As 'StaticMaterials' must be protected for async build, always use the accessors even internally.

					for (FStaticMaterial StaticMaterial : StaticMesh->GetStaticMaterials())
					{
						if (StaticMaterial.MaterialInterface == OriginalMaterial)
						{
							MaterialSlotNameToBeOverriden = StaticMaterial.MaterialSlotName;

							break;
						}
					}
				}
				else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(OuterObjects[0]))
				{
					// NOTE : As 'Materials' must be protected for async build, always use the accessors even internally.

					for (FSkeletalMaterial SkeletalMaterial : SkeletalMesh->GetMaterials())
					{
						if (SkeletalMaterial.MaterialInterface == OriginalMaterial)
						{
							MaterialSlotNameToBeOverriden = SkeletalMaterial.MaterialSlotName;

							break;
						}
					}
				}
			}

			if (!ensureMsgf(!MaterialSlotNameToBeOverriden.IsNone(), TEXT("Material Property could not be exposed because the property does not contain any valid slot names.")))
			{
				return;
			}
		
			// NOTE : Obtain the actor and/or object information from the details panel.

			TArray<TWeakObjectPtr<AActor>> SelectedActors;

			TArray<TWeakObjectPtr<UObject>> SelectedObjects;

			if (TSharedPtr<IDetailTreeNode> OwnerTreeNode = WeakDetailsTreeNode.Pin())
			{
				if (IDetailsView* DetailsView = OwnerTreeNode->GetNodeDetailsView())
				{
					SelectedActors = DetailsView->GetSelectedActors();

					SelectedObjects = DetailsView->GetSelectedObjects();
				}
			}
			else if (SharedDetailsPanel.IsValid()) // Fallback to global detail panel reference if Detail Tree Node is invalid.
			{
				SelectedActors = SharedDetailsPanel->GetSelectedActors();
			
				SelectedObjects = SharedDetailsPanel->GetSelectedObjects();
			}

			TWeakObjectPtr<UMeshComponent> MeshComponentToBeModified;

			if (SelectedActors.Num()) // If user selected actor then get the component from it.
			{
				// NOTE : Allow single selection only.

				if (!ensureMsgf(SelectedActors.Num() == 1, TEXT("Property could not be exposed as multiple actor(s) are selected.")))
				{
					return;
				}

				for (TWeakObjectPtr<AActor> SelectedActor : SelectedActors)
				{
					TInlineComponentArray<UMeshComponent*> MeshComponents(SelectedActor.Get());

					// NOTE : First mesh component that has the material slot gets served (FCFS approach).

					for (UMeshComponent* MeshComponent : MeshComponents)
					{
						TArray<FName> MaterialSlots = MeshComponent->GetMaterialSlotNames();

						if (MaterialSlots.Contains(MaterialSlotNameToBeOverriden))
						{
							MeshComponentToBeModified = MeshComponent;

							break;
						}
					}
				}
			}
			else if (SelectedObjects.Num()) // If user selected a component then proceed with it.
			{
				// NOTE : Allow single selection only.

				if (!ensureMsgf(SelectedObjects.Num() == 1, TEXT("Property could not be exposed as multiple component(s) are selected.")))
				{
					return;
				}

				for (TWeakObjectPtr<UObject> SelectedObject : SelectedObjects)
				{
					if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(SelectedObject))
					{
						TArray<FName> MaterialSlots = MeshComponent->GetMaterialSlotNames();

						if (MaterialSlots.Contains(MaterialSlotNameToBeOverriden))
						{
							MeshComponentToBeModified = MeshComponent;

							break;
						}
					}
				}
			}

			if (MeshComponentToBeModified.IsValid())
			{
				FScopedTransaction Transaction(LOCTEXT("OverrideMaterial", "Override Material"));

				const int32 TargetMaterialIndex = MeshComponentToBeModified->GetMaterialIndex(MaterialSlotNameToBeOverriden);

				if (FComponentEditorUtils::AttemptApplyMaterialToComponent(MeshComponentToBeModified.Get(), OriginalMaterial, TargetMaterialIndex))
				{
					RefreshPanels();
				}
			}
		}

	}
}

void FRemoteControlUIModule::RefreshPanels()
{
	if (TSharedPtr<IDetailTreeNode> OwnerTreeNode = WeakDetailsTreeNode.Pin())
	{
		if (IDetailsView* DetailsView = OwnerTreeNode->GetNodeDetailsView())
		{
			DetailsView->ForceRefresh();
		}
	}
	else if (SharedDetailsPanel.IsValid()) // Fallback to global detail panel reference if Detail Tree Node is invalid.
	{
		SharedDetailsPanel->ForceRefresh();
	}

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		Panel->Refresh();
	}
}

TSharedPtr<SRCPanelTreeNode> FRemoteControlUIModule::GenerateEntityWidget(const FGenerateWidgetArgs& Args)
{
	if (Args.Preset && Args.Entity)
	{
		const UScriptStruct* EntityType = Args.Preset->GetExposedEntityType(Args.Entity->GetId());
		
		if (FOnGenerateRCWidget* OnGenerateWidget = GenerateWidgetDelegates.Find(const_cast<UScriptStruct*>(EntityType)))
		{
			return OnGenerateWidget->Execute(Args);
		}
	}

	return nullptr;
}

IMPLEMENT_MODULE(FRemoteControlUIModule, RemoteControlUI);

#undef LOCTEXT_NAMESPACE /*RemoteControlUI*/
