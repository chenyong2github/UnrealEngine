// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlUIModule.h"

#include "AssetToolsModule.h"
#include "AssetTools/RemoteControlPresetActions.h"
#include "Commands/RemoteControlCommands.h"
#include "Components/SkeletalMeshComponent.h"
#include "EditorStyleSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IMainFrameModule.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "Materials/Material.h"
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
#include "UI/Customizations/FPassphraseCustomization.h"
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

FRCExposesPropertyArgs::FRCExposesPropertyArgs()
	: PropertyHandle(nullptr)
	, OwnerObject(nullptr)
	, PropertyPath(TEXT(""))
	, Property(nullptr)
	, Id(FGuid::NewGuid())
{
}

FRCExposesPropertyArgs::FRCExposesPropertyArgs(const FOnGenerateGlobalRowExtensionArgs& InExtensionArgs)
	: PropertyHandle(InExtensionArgs.PropertyHandle)
	, OwnerObject(InExtensionArgs.OwnerObject)
	, PropertyPath(InExtensionArgs.PropertyPath)
	, Property(InExtensionArgs.Property)
	, Id(FGuid::NewGuid())
{
}

FRCExposesPropertyArgs::FRCExposesPropertyArgs(FOnGenerateGlobalRowExtensionArgs&& InExtensionArgs)
	: PropertyHandle(InExtensionArgs.PropertyHandle)
	, OwnerObject(InExtensionArgs.OwnerObject)
	, PropertyPath(InExtensionArgs.PropertyPath)
	, Property(InExtensionArgs.Property)
	, Id(FGuid::NewGuid())
{
}

FRCExposesPropertyArgs::FRCExposesPropertyArgs(TSharedPtr<IPropertyHandle>& InPropertyHandle)
	: PropertyHandle(InPropertyHandle)
	, Id(FGuid::NewGuid())
{
}

FRCExposesPropertyArgs::FRCExposesPropertyArgs(UObject* InOwnerObject, const FString& InPropertyPath, FProperty* InProperty)
	: OwnerObject(InOwnerObject)
	, PropertyPath(InPropertyPath)
	, Property(InProperty)
	, Id(FGuid::NewGuid())
{
}

FRCExposesPropertyArgs::EType FRCExposesPropertyArgs::GetType() const
{
	if (PropertyHandle.IsValid())
	{
		return EType::E_Handle;
	}
	if (OwnerObject && !PropertyPath.IsEmpty() && Property)
	{
		return EType::E_OwnerObject;
	}

	return EType::E_None;
}

bool FRCExposesPropertyArgs::IsValid() const
{
	const EType Type = GetType();
	return Type == EType::E_Handle || Type == EType::E_OwnerObject;
}


FProperty* FRCExposesPropertyArgs::GetProperty() const
{
	const EType Type = GetType();
	if (Type == EType::E_Handle)
	{
		PropertyHandle->GetProperty();
	}
	else
	{
		return Property;
	}

	return nullptr;
}

FProperty* FRCExposesPropertyArgs::GetPropertyChecked() const
{
	FProperty* PropertyChecked = GetProperty();
	check(PropertyChecked);
	return PropertyChecked;
}

namespace RemoteControlUIModule
{
	const TArray<FName>& GetCustomizedStructNames()
	{
		static TArray<FName> CustomizedStructNames =
		{
			FRemoteControlProperty::StaticStruct()->GetFName(),
			FRemoteControlFunction::StaticStruct()->GetFName(),
			FRemoteControlActor::StaticStruct()->GetFName(),
		};

		return CustomizedStructNames;
	};
	
	static void OnOpenRemoteControlPanel(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& EditWithinLevelEditor, class URemoteControlPreset* Preset)
	{
		TSharedRef<FRemoteControlPresetEditorToolkit> Toolkit = FRemoteControlPresetEditorToolkit::CreateEditor(Mode, EditWithinLevelEditor,  Preset);
		Toolkit->InitRemoteControlPresetEditor(Mode, EditWithinLevelEditor, Preset);
	}

	int32 GetNumStaticMaterials(UObject* InOuterObject)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(InOuterObject))
		{
			return StaticMesh->GetStaticMaterials().Num();
		}
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(InOuterObject))
		{
			return SkeletalMesh->GetMaterials().Num();
		}

		return 0;
	};

	FObjectProperty* GetObjectProperty(const FRCExposesPropertyArgs& InPropertyArgs)
	{
		if (!ensureMsgf(InPropertyArgs.IsValid(), TEXT("Extension Property Args not valid")))
		{
			return nullptr;
		}

		FRCFieldPathInfo RCFieldPathInfo(InPropertyArgs.PropertyPath);
		RCFieldPathInfo.Resolve(InPropertyArgs.OwnerObject);
		FRCFieldResolvedData ResolvedData = RCFieldPathInfo.GetResolvedData();
		return CastField<FObjectProperty>(InPropertyArgs.GetProperty());
	}

	bool IsStaticOrSkeletalMaterial(const FRCExposesPropertyArgs& InPropertyArgs)
	{
		if (!InPropertyArgs.IsValid())
		{
			return false;
		}

		const FRCExposesPropertyArgs::EType ExtensionArgsType = InPropertyArgs.GetType();

		// Only in case of Owner Object is applicable for MaterialInterface
		if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
		{
			FProperty* Property = InPropertyArgs.GetProperty();

			if (!ensure(Property))
			{
				return false;
			}

			FObjectProperty* ObjectProperty = GetObjectProperty(InPropertyArgs);

			if (!ObjectProperty)
			{
				return false;
			}

			return Property->GetFName() == UMaterialInterface::StaticClass()->GetFName() && (InPropertyArgs.OwnerObject->GetClass()->IsChildOf(UStaticMesh::StaticClass()) || InPropertyArgs.OwnerObject->GetClass()->IsChildOf(USkeletalMesh::StaticClass()));
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
	const FRCExposesPropertyArgs ExposesPropertyArgs(InArgs);

	if (ExposesPropertyArgs.IsValid())
	{
		// Expose/Unexpose button.
		FPropertyRowExtensionButton& ExposeButton = OutExtensions.AddDefaulted_GetRef();
		ExposeButton.Icon = TAttribute<FSlateIcon>::Create(
			[this, ExposesPropertyArgs]
		{
			return OnGetExposedIcon(ExposesPropertyArgs);
		});

		ExposeButton.Label = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FRemoteControlUIModule::GetExposePropertyButtonText, ExposesPropertyArgs));
		ExposeButton.ToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FRemoteControlUIModule::GetExposePropertyButtonTooltip, ExposesPropertyArgs));
		ExposeButton.UIAction = FUIAction(
			FExecuteAction::CreateRaw(this, &FRemoteControlUIModule::OnToggleExposeProperty, ExposesPropertyArgs),
			FCanExecuteAction::CreateRaw(this, &FRemoteControlUIModule::CanToggleExposeProperty, ExposesPropertyArgs),
			FGetActionCheckState::CreateRaw(this, &FRemoteControlUIModule::GetPropertyExposedCheckState, ExposesPropertyArgs),
			FIsActionButtonVisible::CreateRaw(this, &FRemoteControlUIModule::CanToggleExposeProperty, ExposesPropertyArgs)
		);

		// Override material(s) warning.
		FPropertyRowExtensionButton& OverrideMaterialButton = OutExtensions.AddDefaulted_GetRef();

		OverrideMaterialButton.Icon = TAttribute<FSlateIcon>::Create(
			[this, ExposesPropertyArgs]
		{
			return OnGetOverrideMaterialsIcon(ExposesPropertyArgs);
		}
		);

		OverrideMaterialButton.Label = LOCTEXT("OverrideMaterial", "Override Material");
		OverrideMaterialButton.ToolTip = LOCTEXT("OverrideMaterialToolTip", "Click to override this material in order to expose this property to Remote Control.");
		OverrideMaterialButton.UIAction = FUIAction(
			FExecuteAction::CreateRaw(this, &FRemoteControlUIModule::TryOverridingMaterials, ExposesPropertyArgs),
			FCanExecuteAction::CreateRaw(this, &FRemoteControlUIModule::IsStaticOrSkeletalMaterialProperty, ExposesPropertyArgs),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateRaw(this, &FRemoteControlUIModule::IsStaticOrSkeletalMaterialProperty, ExposesPropertyArgs)
		);
	}

	WeakDetailsTreeNode = InArgs.OwnerTreeNode;
}

FSlateIcon FRemoteControlUIModule::OnGetExposedIcon(const FRCExposesPropertyArgs& InPropertyArgs) const
{
	FName BrushName("NoBrush");

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Panel->GetPreset())
		{
			EPropertyExposeStatus Status = GetPropertyExposeStatus(InPropertyArgs);
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

bool FRemoteControlUIModule::CanToggleExposeProperty(const FRCExposesPropertyArgs InPropertyArgs) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		return InPropertyArgs.IsValid() && ShouldDisplayExposeIcon(InPropertyArgs);
	}

	return false;
}


ECheckBoxState FRemoteControlUIModule::GetPropertyExposedCheckState(const FRCExposesPropertyArgs InPropertyArgs) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		if (Panel->GetPreset())
		{
			EPropertyExposeStatus ExposeStatus = GetPropertyExposeStatus(InPropertyArgs);
			if (ExposeStatus == EPropertyExposeStatus::Exposed)
			{
				return ECheckBoxState::Checked;
			}
		}
	}

	return ECheckBoxState::Unchecked;
}

void FRemoteControlUIModule::OnToggleExposeProperty(const FRCExposesPropertyArgs InPropertyArgs)
{
	if (!ensureMsgf(InPropertyArgs.IsValid(), TEXT("Property could not be exposed because the extension args was invalid.")))
	{
		return;
	}

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
	{
		Panel->ToggleProperty(InPropertyArgs);
	}
}

FRemoteControlUIModule::EPropertyExposeStatus FRemoteControlUIModule::GetPropertyExposeStatus(const FRCExposesPropertyArgs& InPropertyArgs) const
{
	if (InPropertyArgs.IsValid())
	{
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin())
		{
			return Panel->IsExposed(InPropertyArgs) ? EPropertyExposeStatus::Exposed : EPropertyExposeStatus::Unexposed;
		}
	}

	return EPropertyExposeStatus::Unexposable;
}


FSlateIcon FRemoteControlUIModule::OnGetOverrideMaterialsIcon(const FRCExposesPropertyArgs& InPropertyArgs) const
{
	FName BrushName("NoBrush");

	if (IsStaticOrSkeletalMaterialProperty(InPropertyArgs))
	{
		BrushName = "Icons.Warning";
	}

	return FSlateIcon(FAppStyle::Get().GetStyleSetName(), BrushName);
}


bool FRemoteControlUIModule::IsStaticOrSkeletalMaterialProperty(const FRCExposesPropertyArgs InPropertyArgs) const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakActivePanel.Pin()) // Check whether the panel is active.
	{
		if (Panel->GetPreset() && InPropertyArgs.IsValid()) // Ensure that we have a valid preset and handle.
		{
			return RemoteControlUIModule::IsStaticOrSkeletalMaterial(InPropertyArgs);
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

bool FRemoteControlUIModule::ShouldDisplayExposeIcon(const FRCExposesPropertyArgs& InPropertyArgs) const
{
	if (!InPropertyArgs.IsValid())
	{
		return false;
	}


	if (RemoteControlUIModule::IsStaticOrSkeletalMaterial(InPropertyArgs))
	{
		return false;
	}

	const FRCExposesPropertyArgs::EType ExtensionArgsType = InPropertyArgs.GetType();

	if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_Handle)
	{
		TSharedPtr<IPropertyHandle> PropertyHandle = InPropertyArgs.PropertyHandle;

		if (PropertyHandle->GetNumOuterObjects() == 1)
		{
			TArray<UObject*> OuterObjects;
			PropertyHandle->GetOuterObjects(OuterObjects);

			if (!IsAllowedOwnerObjects(OuterObjects))
			{
				return false;
			}
		}
	}
	else if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
	{
		if (!IsAllowedOwnerObjects({ InPropertyArgs.OwnerObject }))
		{
			return false;
		}
	}


	for (const TPair<FDelegateHandle, FOnDisplayExposeIcon>& DelegatePair : ExternalFilterDelegates)
	{
		if (DelegatePair.Value.IsBound())
		{
			if (!DelegatePair.Value.Execute(InPropertyArgs))
			{
				return false;
			}
		}
	}


	return true;
}

bool FRemoteControlUIModule::ShouldSkipOwnPanelProperty(const FRCExposesPropertyArgs& InPropertyArgs) const
{
	if (!InPropertyArgs.IsValid())
	{
		return false;
	}

	auto CheckOwnProperty = [](FProperty* InProp)
	{
		if (!InProp)
		{
			return false;
		}


		// Don't display an expose icon for RCEntities since they're only displayed in the Remote Control Panel.
		if (InProp->GetOwnerStruct() && InProp->GetOwnerStruct()->IsChildOf(FRemoteControlEntity::StaticStruct()))
		{
			return true;
		}

		return false;
	};

	const FRCExposesPropertyArgs::EType ArgsType = InPropertyArgs.GetType();

	if (ArgsType == FRCExposesPropertyArgs::EType::E_Handle)
	{
		if (TSharedPtr<IPropertyHandle> PropertyHandle = InPropertyArgs.PropertyHandle)
		{
			CheckOwnProperty(PropertyHandle->GetProperty());
		}
	}
	else if (ArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
	{
		return CheckOwnProperty(InPropertyArgs.Property);
	}

	return false;
}

bool FRemoteControlUIModule::IsAllowedOwnerObjects(TArray<UObject*> InOuterObjects) const
{
	if (InOuterObjects[0])
	{
		// Don't display an expose icon for default objects.
		if (InOuterObjects[0]->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			return false;
		}

		// Don't display an expose icon for transient objects such as material editor parameters.
		if (InOuterObjects[0]->GetOutermost()->HasAnyFlags(RF_Transient) && !RemoteControlUIModule::IsTransientObjectAllowListed(InOuterObjects[0]))
		{
			return false;
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
	
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(FRCPassphrase::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPassphraseCustomization::MakeInstance));
}

void FRemoteControlUIModule::UnregisterStructCustomizations()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	// Unregister delegates in reverse order.
	for (int8 NameIndex = RemoteControlUIModule::GetCustomizedStructNames().Num() - 1; NameIndex >= 0; NameIndex--)
	{
		PropertyEditorModule.UnregisterCustomClassLayout(RemoteControlUIModule::GetCustomizedStructNames()[NameIndex]);
	}

	if (UObjectInitialized())
	{
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FRCPassphrase::StaticStruct()->GetFName());
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

FText FRemoteControlUIModule::GetExposePropertyButtonTooltip(const FRCExposesPropertyArgs InPropertyArgs) const
{
	if (URemoteControlPreset* Preset = GetActivePreset())
	{
		const FText PresetName = FText::FromString(Preset->GetName());
		if (GetPropertyExposeStatus(InPropertyArgs) == EPropertyExposeStatus::Exposed)
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


FText FRemoteControlUIModule::GetExposePropertyButtonText(const FRCExposesPropertyArgs InPropertyArgs) const
{
	if (GetPropertyExposeStatus(InPropertyArgs) == EPropertyExposeStatus::Exposed)
	{
		return LOCTEXT("ExposePropertyText", "Unexpose property");
	}
	else
	{
		return LOCTEXT("UnexposePropertyText", "Expose property");
	}
}

void FRemoteControlUIModule::TryOverridingMaterials(const FRCExposesPropertyArgs InPropertyArgs)
{
	if (!ensureMsgf(InPropertyArgs.IsValid(), TEXT("Property could not be exposed because the handle was invalid.")))
	{
		return;
	}
	
	const FRCExposesPropertyArgs::EType ExtensionArgsType = InPropertyArgs.GetType();

	if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_Handle)
	{
		ensureMsgf(false, TEXT("Override materials can't be done with property handle arguments type"));
	}
	// Override material only if PropertyArgs is OwnerObject
	else if (ExtensionArgsType == FRCExposesPropertyArgs::EType::E_OwnerObject)
	{
		FRCFieldPathInfo RCFieldPathInfo(InPropertyArgs.PropertyPath);
		RCFieldPathInfo.Resolve(InPropertyArgs.OwnerObject);
		const FRCFieldResolvedData ResolvedData = RCFieldPathInfo.GetResolvedData();
		FObjectProperty* ObjectProperty = CastField<FObjectProperty>(InPropertyArgs.Property);

		if (!ObjectProperty)
		{
			return;
		}

		// Material can't be null
		UMaterialInterface* OriginalMaterial = Cast<UMaterialInterface>(ObjectProperty->GetObjectPropertyValue(ResolvedData.ContainerAddress));
		OriginalMaterial = OriginalMaterial ? OriginalMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
		if (OriginalMaterial)
		{
			if (UMeshComponent* MeshComponentToBeModified = GetSelectedMeshComponentToBeModified(InPropertyArgs.OwnerObject, OriginalMaterial))
			{
				const int32 NumStaticMaterials = RemoteControlUIModule::GetNumStaticMaterials(InPropertyArgs.OwnerObject);
				if (NumStaticMaterials > 0)
				{
					FScopedTransaction Transaction(LOCTEXT("OverrideMaterial", "Override Material"));

					if (FComponentEditorUtils::AttemptApplyMaterialToComponent(MeshComponentToBeModified, OriginalMaterial, NumStaticMaterials - 1))
					{
						RefreshPanels();
					}
				}
			}
		}
	}
}

UMeshComponent* FRemoteControlUIModule::GetSelectedMeshComponentToBeModified(UObject* InOwnerObject, UMaterialInterface* InOriginalMaterial)
{
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

	UMeshComponent* MeshComponentToBeModified = nullptr;

	auto GetComponentToBeModified = [InOwnerObject](UMeshComponent* InMeshComponent)
	{
		UMeshComponent* ModifinedComponent = nullptr;

		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(InMeshComponent))
		{
			if (StaticMeshComponent->GetStaticMesh() == InOwnerObject)
			{
				ModifinedComponent = InMeshComponent;
			}
		}
		else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InMeshComponent))
		{
			if (SkeletalMeshComponent->SkeletalMesh == InOwnerObject)
			{
				ModifinedComponent = InMeshComponent;
			}
		}

		return ModifinedComponent;
	};

	if (SelectedActors.Num()) // If user selected actor then get the component from it.
	{
		// NOTE : Allow single selection only.

		if (!ensureMsgf(SelectedActors.Num() == 1, TEXT("Property could not be exposed as multiple actor(s) are selected.")))
		{
			return nullptr;
		}

		for (TWeakObjectPtr<AActor> SelectedActor : SelectedActors)
		{
			TInlineComponentArray<UMeshComponent*> MeshComponents(SelectedActor.Get());

			// NOTE : First mesh component that has the material slot gets served (FCFS approach).

			for (UMeshComponent* MeshComponent : MeshComponents)
			{
				MeshComponentToBeModified = GetComponentToBeModified(MeshComponent);
			}
		}
	}
	else if (SelectedObjects.Num()) // If user selected a component then proceed with it.
	{
		// NOTE : Allow single selection only.
		if (!ensureMsgf(SelectedObjects.Num() == 1, TEXT("Property could not be exposed as multiple component(s) are selected.")))
		{
			return nullptr;
		}

		for (TWeakObjectPtr<UObject> SelectedObject : SelectedObjects)
		{
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(SelectedObject))
			{
				MeshComponentToBeModified = GetComponentToBeModified(MeshComponent);
			}
		}
	}

	return MeshComponentToBeModified;
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
