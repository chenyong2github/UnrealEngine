// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorModule.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorCommands.h"
#include "DisplayClusterConfiguratorAssetTypeActions.h"
#include "DisplayClusterConfiguratorVersionUtils.h"
#include "Settings/DisplayClusterConfiguratorSettings.h"
#include "Views/Details/DisplayClusterRootActorDetailsCustomization.h"

#include "Views/Details/DisplayClusterConfiguratorDetailCustomization.h"
#include "Views/Details/Policies/DisplayClusterConfiguratorPolicyDetailCustomization.h"

#include "Blueprints/DisplayClusterBlueprint.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"
#include "Misc/DisplayClusterObjectRef.h"
#include "DisplayClusterRootActor.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ISettingsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetTypeCategories.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Views/Details/DisplayClusterICVFXCameraComponentDetailsCustomization.h"


#define LOCTEXT_NAMESPACE "DisplayClusterConfigurator"

FOnDisplayClusterConfiguratorReadOnlyChanged FDisplayClusterConfiguratorModule::OnDisplayClusterConfiguratorReadOnlyChanged;

static TAutoConsoleVariable<bool> CVarDisplayClusterConfiguratorReadOnly(
	TEXT("nDisplay.configurator.ReadOnly"),
	true,
	TEXT("Enable or disable editing functionality")
	);

static FAutoConsoleVariableSink CVarDisplayClusterConfiguratorReadOnlySink(FConsoleCommandDelegate::CreateStatic(&FDisplayClusterConfiguratorModule::ReadOnlySink));

void FDisplayClusterConfiguratorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	/*
	 * Hack for instanced property sync.
	 *
	 * We must clear CPF_EditConst for these properties. They are VisibleInstanceOnly but we are modifying them through their handles
	 * programmatically. If CPF_EditConst is present that operation will fail. We do not want them to be editable on the details panel either.
	 */
	{
		FProperty* Property = FindFProperty<FProperty>(UDisplayClusterConfigurationCluster::StaticClass(), GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Nodes));
		Property->ClearPropertyFlags(CPF_EditConst);
	}
	
	{
		FProperty* Property = FindFProperty<FProperty>(UDisplayClusterConfigurationClusterNode::StaticClass(), GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationClusterNode, Viewports));
		Property->ClearPropertyFlags(CPF_EditConst);
	}

	// Create a custom menu category.
	const EAssetTypeCategories::Type AssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(
	FName(TEXT("nDisplay")), LOCTEXT("nDisplayAssetCategory", "nDisplay"));
	
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FDisplayClusterConfiguratorAssetTypeActions(AssetCategoryBit)));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FDisplayClusterConfiguratorActorAssetTypeActions(EAssetTypeCategories::None)));

	RegisterCustomLayouts();
	RegisterSettings();
	
	FDisplayClusterConfiguratorStyle::Initialize();
	FDisplayClusterConfiguratorCommands::Register();
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	// Register blueprint compiler -- primarily seems to be used when creating a new BP.
	IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(KISMET_COMPILER_MODULENAME);
	KismetCompilerModule.GetCompilers().Add(&BlueprintCompiler);

	// This is needed for actually pressing compile on the BP.
	FKismetCompilerContext::RegisterCompilerForBP(UDisplayClusterBlueprint::StaticClass(), &FDisplayClusterConfiguratorModule::GetCompilerForDisplayClusterBP);

	const UDisplayClusterConfiguratorEditorSettings* Settings = GetDefault<UDisplayClusterConfiguratorEditorSettings>();
	if (Settings->bUpdateAssetsOnStartup)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FilesLoadedHandle = AssetRegistryModule.Get().OnFilesLoaded().AddStatic(&FDisplayClusterConfiguratorVersionUtils::UpdateBlueprintsToNewVersion);
	}
}

void FDisplayClusterConfiguratorModule::ShutdownModule()
{
	if (FAssetToolsModule* AssetTools = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		for (int32 IndexAction = 0; IndexAction < CreatedAssetTypeActions.Num(); ++IndexAction)
		{
			AssetTools->Get().UnregisterAssetTypeActions(CreatedAssetTypeActions[IndexAction].ToSharedRef());
		}
	}

	UnregisterSettings();
	UnregisterCustomLayouts();

	FDisplayClusterConfiguratorStyle::Shutdown();
	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

	IKismetCompilerInterface& KismetCompilerModule = FModuleManager::GetModuleChecked<IKismetCompilerInterface>("KismetCompiler");
	KismetCompilerModule.GetCompilers().Remove(&BlueprintCompiler);

	if (FilesLoadedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().OnFilesLoaded().Remove(FilesLoadedHandle);
	}
}

const FDisplayClusterConfiguratorCommands& FDisplayClusterConfiguratorModule::GetCommands() const
{
	return FDisplayClusterConfiguratorCommands::Get();
}

void FDisplayClusterConfiguratorModule::ReadOnlySink()
{
	bool bNewDisplayClusterConfiguratorReadOnly = CVarDisplayClusterConfiguratorReadOnly.GetValueOnGameThread();

	// By default we assume the ReadOnly is true
	static bool GReadOnly = true;

	if (GReadOnly != bNewDisplayClusterConfiguratorReadOnly)
	{
		GReadOnly = bNewDisplayClusterConfiguratorReadOnly;

		// Broadcast the changes
		OnDisplayClusterConfiguratorReadOnlyChanged.Broadcast(GReadOnly);
	}
}

FDelegateHandle FDisplayClusterConfiguratorModule::RegisterOnReadOnly(const FOnDisplayClusterConfiguratorReadOnlyChangedDelegate& Delegate)
{
	return OnDisplayClusterConfiguratorReadOnlyChanged.Add(Delegate);
}

void FDisplayClusterConfiguratorModule::UnregisterOnReadOnly(FDelegateHandle DelegateHandle)
{
	OnDisplayClusterConfiguratorReadOnlyChanged.Remove(DelegateHandle);
}

void FDisplayClusterConfiguratorModule::RegisterAssetTypeAction(IAssetTools& AssetTools,
	TSharedRef<IAssetTypeActions> Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

void FDisplayClusterConfiguratorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Editor", "Plugins", "nDisplayEditor",
			LOCTEXT("nDisplayEditorName", "nDisplay Editor"),
			LOCTEXT("nDisplayEditorDescription", "Configure settings for the nDisplay Editor."),
			GetMutableDefault<UDisplayClusterConfiguratorEditorSettings>());
	}
}

void FDisplayClusterConfiguratorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "ContentEditors", "nDisplayEditor");
	}
}

void FDisplayClusterConfiguratorModule::RegisterCustomLayouts()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	/**
	 * CLASSES
	 */
	{
		const FName LayoutName = UDisplayClusterConfigurationData::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(ADisplayClusterRootActor::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterRootActorDetailsCustomization::MakeInstance));
	}
	
	{
		const FName LayoutName = UDisplayClusterConfigurationData::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(LayoutName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorDataDetailCustomization>));
	}

	{
		const FName LayoutName = UDisplayClusterConfigurationCluster::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(LayoutName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorClusterDetailCustomization>));
	}

	{
		const FName LayoutName = UDisplayClusterConfigurationClusterNode::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(LayoutName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorDetailCustomization>));
	}
	
	{
		const FName LayoutName = UDisplayClusterConfigurationViewport::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(LayoutName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorViewportDetailCustomization>));
	}

	{
		const FName LayoutName = UDisplayClusterScreenComponent::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(LayoutName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorScreenDetailCustomization::MakeInstance));
	}
	
	{
		const FName LayoutName = UDisplayClusterICVFXCameraComponent::StaticClass()->GetFName();
		RegisteredClassLayoutNames.Add(LayoutName);
		PropertyModule.RegisterCustomClassLayout(LayoutName,
			FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterICVFXCameraComponentDetailsCustomization::MakeInstance));
	}
	
	/**
	 * STRUCTS
	 */
	{
		const FName LayoutName = FDisplayClusterConfigurationProjection::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorProjectionCustomization>));
	}

	{
		const FName LayoutName = FDisplayClusterConfigurationClusterSync::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorClusterSyncTypeCustomization>));
	}
	
	{
		const FName LayoutName = FDisplayClusterConfigurationRenderSyncPolicy::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorRenderSyncPolicyCustomization>));
	}

	{
		// TODO: Input sync policy needed with Input / VRPN changes?
		
		const FName LayoutName = FDisplayClusterConfigurationInputSyncPolicy::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorInputSyncPolicyCustomization>));
	}

	{
		const FName LayoutName = FDisplayClusterConfigurationExternalImage::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorExternalImageTypeCustomization>));
	}

	{
		const FName LayoutName = FDisplayClusterComponentRef::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorComponentRefCustomization::MakeInstance<FDisplayClusterConfiguratorComponentRefCustomization>));
	}
	
	{
		const FName LayoutName = FDisplayClusterConfigurationOCIOProfile::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorOCIOProfileCustomization::MakeInstance<FDisplayClusterConfiguratorOCIOProfileCustomization>));
	}

	{
		const FName LayoutName = FDisplayClusterConfigurationViewport_PerViewportColorGrading::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorPerViewportColorGradingCustomization::MakeInstance<FDisplayClusterConfiguratorPerViewportColorGradingCustomization>));
	}

	{
		const FName LayoutName = FDisplayClusterConfigurationViewport_PerNodeColorGrading::StaticStruct()->GetFName();
		RegisteredPropertyLayoutNames.Add(LayoutName);

		PropertyModule.RegisterCustomPropertyTypeLayout(LayoutName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorPerNodeColorGradingCustomization::MakeInstance<FDisplayClusterConfiguratorPerNodeColorGradingCustomization>));
	}
}

void FDisplayClusterConfiguratorModule::UnregisterCustomLayouts()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	for (const FName& LayoutName : RegisteredClassLayoutNames)
	{
		PropertyModule.UnregisterCustomClassLayout(LayoutName);
	}
	
	for (const FName& LayoutName : RegisteredPropertyLayoutNames)
	{
		PropertyModule.UnregisterCustomPropertyTypeLayout(LayoutName);
	}

	RegisteredPropertyLayoutNames.Empty();
}

TSharedPtr<FKismetCompilerContext> FDisplayClusterConfiguratorModule::GetCompilerForDisplayClusterBP(UBlueprint* BP,
                                                                                                     FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
	return TSharedPtr<FKismetCompilerContext>(new FDisplayClusterConfiguratorKismetCompilerContext(CastChecked<UDisplayClusterBlueprint>(BP), InMessageLog, InCompileOptions));
}

IMPLEMENT_MODULE(FDisplayClusterConfiguratorModule, DisplayClusterConfigurator);

#undef LOCTEXT_NAMESPACE
