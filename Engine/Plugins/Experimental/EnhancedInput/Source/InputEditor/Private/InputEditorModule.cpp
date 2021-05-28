// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputEditorModule.h"

#include "AssetRegistryModule.h"
#include "AssetTypeActions_Base.h"
#include "EnhancedInputModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFilemanager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCustomizations.h"
#include "InputModifiers.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "IDetailsView.h"
#include "ISettingsModule.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_GetInputActionValue.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "SSettingsEditorCheckoutNotice.h"
#include "TickableEditorObject.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "InputEditor"

class FInputEditorModule : public IModuleInterface, public FTickableEditorObject
{
public:

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface interface

	// FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FInputEditorModule, STATGROUP_Tickables); }
	// End FTickableEditorObject interface

	static EAssetTypeCategories::Type GetInputAssetsCategory() { return InputAssetsCategory; }

private:
	void RegisterAssetTypeActions(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		CreatedAssetTypeActions.Add(Action);
	}
	void PostEngineInit();
	TSharedRef<SWidget> CreateSettingsPanel();
	void OnSettingChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	void RebuildDetailsViewForAsset(const FAssetData& AssetData, bool bIgnoreAsset);
	void OnAssetAdded(const FAssetData& AssetData) { RebuildDetailsViewForAsset(AssetData, false); }
	void OnAssetRemoved(const FAssetData& AssetData) { RebuildDetailsViewForAsset(AssetData, true); }
	void OnAssetRenamed(const FAssetData& AssetData, const FString&) { RebuildDetailsViewForAsset(AssetData, true); }

	template<typename T>
	TSharedPtr<IDetailsView> AddClassDetailsView();

	struct FClassDetailsView
	{
		FClassDetailsView() = default;
		FClassDetailsView(UClass* InClass, TSharedPtr<IDetailsView>& InView) : Class(InClass), View(InView) {}
		bool IsValid() const { return Class != nullptr; }

		UClass* Class = nullptr;
		TSharedPtr<IDetailsView> View;
	};

	FClassDetailsView FindClassDetailsViewForAsset(const FAssetData& AssetData);
	TArray<UObject*> GatherClassDetailsCDOs(UClass* Class, const FAssetData* IgnoreAsset);

	static EAssetTypeCategories::Type InputAssetsCategory;

	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;

	TMap<UClass*, TSharedPtr<IDetailsView>> DetailsViews;

	TSharedPtr<SWidget> Panel;
};

EAssetTypeCategories::Type FInputEditorModule::InputAssetsCategory;

IMPLEMENT_MODULE(FInputEditorModule, InputEditor)

// Asset factories

// InputContext
UInputMappingContext_Factory::UInputMappingContext_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
	SupportedClass = UInputMappingContext::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UInputMappingContext_Factory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UInputMappingContext::StaticClass()));
	return NewObject<UInputMappingContext>(InParent, Class, Name, Flags | RF_Transactional, Context);
}

// InputAction
UInputAction_Factory::UInputAction_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
	SupportedClass = UInputAction::StaticClass();
	bEditAfterNew = true;
	bCreateNew = true;
}

UObject* UInputAction_Factory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UInputAction::StaticClass()));
	return NewObject<UInputAction>(InParent, Class, Name, Flags | RF_Transactional, Context);
}

//
//// InputTrigger
//UInputTrigger_Factory::UInputTrigger_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
//	ParentClass = UInputTrigger::StaticClass();
//	SupportedClass = UInputTrigger::StaticClass();
//	bEditAfterNew = true;
//	bCreateNew = true;
//}
//
//// InputModifier
//UInputModifier_Factory::UInputModifier_Factory(const class FObjectInitializer& OBJ) : Super(OBJ) {
//	ParentClass = UInputModifier::StaticClass();
//	SupportedClass = UInputModifier::StaticClass();
//	bEditAfterNew = true;
//	bCreateNew = true;
//}



// Asset type actions
// TODO: Move asset type action definitions out?

class FAssetTypeActions_InputContext : public FAssetTypeActions_Base {
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputMappingContext", "Input Mapping Context"); }
	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
	virtual FColor GetTypeColor() const override { return FColor(255, 255, 127); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputContextDesc", "A collection of device input to action mappings."); }
	virtual UClass* GetSupportedClass() const override { return UInputMappingContext::StaticClass(); }
};

class FAssetTypeActions_InputAction : public FAssetTypeActions_Base {
public:
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputAction", "Input Action"); }
	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
	virtual FColor GetTypeColor() const override { return FColor(127, 255, 255); }
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputActionDesc", "Represents an an abstract game action that can be mapped to arbitrary hardware input devices."); }
	virtual UClass* GetSupportedClass() const override { return UInputAction::StaticClass(); }
};

//class FAssetTypeActions_InputTrigger : public FAssetTypeActions_Base {
//public:
//	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputTrigger", "Input Trigger"); }
//	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
//	virtual FColor GetTypeColor() const override { return FColor(127, 0, 255); }
//	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputTriggerDesc", "Supports custom trigger rules for actions and device inputs."); }
//	virtual UClass* GetSupportedClass() const override { return UInputTrigger::StaticClass(); }
//};
//
//class FAssetTypeActions_InputModifier : public FAssetTypeActions_Base {
//public:
//	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputModifier", "Input Modifier"); }
//	virtual uint32 GetCategories() override { return FInputEditorModule::GetInputAssetsCategory(); }
//	virtual FColor GetTypeColor() const override { return FColor(127, 255, 0); }
//	virtual FText GetAssetDescription(const FAssetData& AssetData) const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_InputModifierDesc", "Applied to actions and raw device inputs to modify their output."); }
//	virtual UClass* GetSupportedClass() const override { return UInputModifier::StaticClass(); }
//};

void FInputEditorModule::OnSettingChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// TODO: Copy of SSettingsEditor::NotifyPostChange
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		check(PropertyChangedEvent.GetNumObjectsBeingEdited() <= 1);
		if(PropertyChangedEvent.GetNumObjectsBeingEdited() > 0)
		{
			UObject* ObjectBeingEdited = (UObject*)PropertyChangedEvent.GetObjectBeingEdited(0);

			// Attempt to checkout the file automatically
			if (!ObjectBeingEdited->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig))
			{
				return;
			}
			FString RelativePath = ObjectBeingEdited->GetDefaultConfigFilename();

			FString FullPath = FPaths::ConvertRelativePathToFull(RelativePath);

			bool bIsNewFile = !FPlatformFileManager::Get().GetPlatformFile().FileExists(*FullPath);

			if (!SettingsHelpers::CheckOutOrAddFile(FullPath))
			{
				SettingsHelpers::MakeWritable(FullPath);
			}

			// Determine if the Property is an Array or Array Element
			bool bIsArrayOrArrayElement = PropertyChangedEvent.Property->IsA(FArrayProperty::StaticClass())
				|| PropertyChangedEvent.Property->ArrayDim > 1
				|| PropertyChangedEvent.Property->GetOwner<FArrayProperty>();

			bool bIsSetOrSetElement = PropertyChangedEvent.Property->IsA(FSetProperty::StaticClass())
				|| PropertyChangedEvent.Property->GetOwner<FSetProperty>();

			bool bIsMapOrMapElement = PropertyChangedEvent.Property->IsA(FMapProperty::StaticClass())
				|| PropertyChangedEvent.Property->GetOwner<FMapProperty>();

			if (ObjectBeingEdited->GetClass()->HasAnyClassFlags(CLASS_DefaultConfig) && !bIsArrayOrArrayElement && !bIsSetOrSetElement && !bIsMapOrMapElement)
			{
				if(PropertyChangedEvent.Property->HasAnyPropertyFlags(CPF_Config | CPF_GlobalConfig))
				{
					ObjectBeingEdited->UpdateSinglePropertyInConfigFile(PropertyChangedEvent.Property, ObjectBeingEdited->GetDefaultConfigFilename());
				}
			}

			if (bIsNewFile)
			{
				SettingsHelpers::CheckOutOrAddFile(FullPath);
			}
		}
	}
}

template<typename T>
class FPerCDOSettingsCustomization : public IDetailCustomization
{
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
		DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

		TWeakObjectPtr<UObject>& Object = CustomizedObjects[0];
		if (Object.IsValid() && Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			FString CategoryName = Object->GetClass()->GetName();
			CategoryName.RemoveFromStart(T::StaticClass()->GetName());
			CategoryName.RemoveFromEnd(TEXT("_C"));
			IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(Object->GetClass()->GetFName(), FText::FromString(FName::NameToDisplayString(CategoryName, false)));	// TODO: Category FName should be FullName for non-native objects

			// TODO: Apply property categories as sub-categories?
			UClass* BaseClass = Object->GetClass();
			while (BaseClass)
			{
				for (FProperty* Property : TFieldRange<FProperty>(BaseClass, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated))
				{
					if (Property->HasAnyPropertyFlags(CPF_Config))
					{
						CategoryBuilder.AddProperty(Property->GetFName(), BaseClass);
					}
				}

				BaseClass = BaseClass != T::StaticClass() ? BaseClass->GetSuperClass() : nullptr; // Stop searching at the base type. We don't care about configurable properties lower than that.
			}
		}
	}
};


template<typename T>
TSharedPtr<IDetailsView> FInputEditorModule::AddClassDetailsView()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;

	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->OnFinishedChangingProperties().AddRaw(this, &FInputEditorModule::OnSettingChanged);
	DetailsView->RegisterInstancedCustomPropertyLayout(T::StaticClass(), FOnGetDetailCustomizationInstance::CreateLambda(&MakeShared<FPerCDOSettingsCustomization<T>>));

	// Init CDOs for view
	DetailsView->SetObjects(GatherClassDetailsCDOs(T::StaticClass(), nullptr));

	return DetailsViews.Add(T::StaticClass(), DetailsView);
}

TArray<UObject*> FInputEditorModule::GatherClassDetailsCDOs(UClass* Class, const FAssetData* IgnoreAsset)
{
	TArray<UObject*> CDOs;

	// Search native classes
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		if (!ClassIt->IsNative() || !ClassIt->IsChildOf(Class))
		{
			continue;
		}

		// Ignore abstract, hidedropdown, and deprecated.
		if (ClassIt->HasAnyClassFlags(CLASS_Abstract | CLASS_HideDropDown | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}

		CDOs.AddUnique(ClassIt->GetDefaultObject());
	}

	// Search BPs via asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> BlueprintAssetData;
	AssetRegistry.GetAssets(Filter, BlueprintAssetData);

	for (FAssetData& Asset : BlueprintAssetData)
	{
		if (IgnoreAsset && Asset == *IgnoreAsset)
		{
			continue;
		}

		FAssetDataTagMapSharedView::FFindTagResult Result = Asset.TagsAndValues.FindTag(TEXT("NativeParentClass"));
		if (Result.IsSet())
		{
			const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(Result.GetValue());
			const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);
			if (UClass* ParentClass = FindObjectSafe<UClass>(ANY_PACKAGE, *ClassName, true))
			{
				if (ParentClass->IsChildOf(Class))
				{
					// TODO: Forcibly loading these assets could cause problems on projects with a large number of them.
					UBlueprint* BP = CastChecked<UBlueprint>(Asset.GetAsset());
					CDOs.AddUnique(BP->GeneratedClass->GetDefaultObject());
				}
			}
		}
	}

	// Strip objects with no config stored properties
	CDOs.RemoveAll([Class](UObject* Object) {
		UClass* ObjectClass = Object->GetClass();
		if (ObjectClass->GetMetaData(TEXT("NotInputConfigurable")).ToBool())
		{
			return true;
		}
		while (ObjectClass)
		{
			for (FProperty* Property : TFieldRange<FProperty>(ObjectClass, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated))
			{
				if (Property->HasAnyPropertyFlags(CPF_Config))
				{
					return false;
				}
			}

			ObjectClass = ObjectClass != Class ? ObjectClass->GetSuperClass() : nullptr; // Stop searching at the base type. We don't care about configurable properties lower than that.
		}
		return true;
	});

	return CDOs;
}

void FInputEditorModule::PostEngineInit()
{
	// Register input settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule && FSlateApplication::IsInitialized())
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "EnhancedInput",
			LOCTEXT("EnhancedInputSettingsName", "Enhanced Input"),
			LOCTEXT("EnhancedInputSettingsDescription", "Modify defaults for configurable triggers and modifiers."),
			CreateSettingsPanel()
		);
	}
}

TSharedRef<SWidget> FInputEditorModule::CreateSettingsPanel()
{
	TSharedPtr<IDetailsView> TriggerDetailsView = AddClassDetailsView<UInputTrigger>();
	TSharedPtr<IDetailsView> ModifierDetailsView = AddClassDetailsView<UInputModifier>();

	FSlateFontInfo HeaderFont = FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle");
	HeaderFont.Size = 32;

	SAssignNew(Panel, SScrollBox)
	+SScrollBox::Slot()
	.Padding(0.f, 4.f, 0.f, 4.f)
	[
		SNew(STextBlock)
		.Font(HeaderFont)
		.Text(LOCTEXT("EngineInputSettingsTriggers", "Trigger Defaults"))
	]

	+SScrollBox::Slot()
	.Padding(0.f, 4.f, 0.f, 0.f)
	[
		TriggerDetailsView.ToSharedRef()
	]

	+SScrollBox::Slot()
	.Padding(0.f, 12.f, 0.f, 4.f)
	[
		SNew(STextBlock)
		.Font(HeaderFont)
		.Text(LOCTEXT("EngineInputSettingsModifiers", "Modifier Defaults"))
	]

	+SScrollBox::Slot()
	.Padding(0.f, 8.f, 0.f, 0.f)
	[
		ModifierDetailsView.ToSharedRef()
	];

	return Panel.ToSharedRef();
}

void FInputEditorModule::StartupModule()
{
	// Register customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("InputMappingContext", FOnGetDetailCustomizationInstance::CreateStatic(&FInputContextDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("EnhancedActionKeyMapping", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FEnhancedActionMappingCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

	// Register input assets
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	InputAssetsCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Input")), LOCTEXT("InputAssetsCategory", "Input"));
	{
		RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputAction));
		RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputContext));
		// TODO: Build these off a button on the InputContext Trigger/Mapping pickers? Would be good to have both.
		//RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputTrigger));
		//RegisterAssetTypeActions(AssetTools, MakeShareable(new FAssetTypeActions_InputModifier));
	}

	// Support for updating blueprint based triggers and modifiers in the settings panel
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.OnAssetAdded().AddRaw(this, &FInputEditorModule::OnAssetAdded);
	AssetRegistry.OnAssetRemoved().AddRaw(this, &FInputEditorModule::OnAssetRemoved);
	AssetRegistry.OnAssetRenamed().AddRaw(this, &FInputEditorModule::OnAssetRenamed);
	// TODO: Update settings whenever a config variable is added/removed to an asset

	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FInputEditorModule::PostEngineInit);
}

void FInputEditorModule::ShutdownModule()
{
	// Unregister settings panel listeners
	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		AssetRegistryModule->Get().OnAssetAdded().RemoveAll(this);
		AssetRegistryModule->Get().OnAssetRemoved().RemoveAll(this);
		AssetRegistryModule->Get().OnAssetRenamed().RemoveAll(this);
	}

	// Unregister input assets
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		for (TSharedPtr<IAssetTypeActions>& AssetAction : CreatedAssetTypeActions)
		{
			AssetToolsModule->Get().UnregisterAssetTypeActions(AssetAction.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();

	// Unregister input settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Engine", "Enhanced Input");
	}

	// Unregister customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomClassLayout("InputContext");
	PropertyModule.UnregisterCustomPropertyTypeLayout("EnhancedActionKeyMapping");
	PropertyModule.NotifyCustomizationModuleChanged();

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
}

FInputEditorModule::FClassDetailsView FInputEditorModule::FindClassDetailsViewForAsset(const FAssetData& AssetData)
{
	if (AssetData.AssetClass == UBlueprint::StaticClass()->GetFName())
	{
		FAssetDataTagMapSharedView::FFindTagResult Result = AssetData.TagsAndValues.FindTag(TEXT("NativeParentClass"));
		if (Result.IsSet())
		{
			const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(Result.GetValue());
			const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);
			if (UClass* ParentClass = FindObjectSafe<UClass>(ANY_PACKAGE, *ClassName, true))
			{
				if (TSharedPtr<IDetailsView>* DetailsView = DetailsViews.Find(ParentClass))
				{
					return FClassDetailsView(ParentClass, *DetailsView);
				}
			}
		}
	}

	return FClassDetailsView();
}

void FInputEditorModule::RebuildDetailsViewForAsset(const FAssetData& AssetData, bool bIgnoreAsset)
{
	FClassDetailsView CDV = FindClassDetailsViewForAsset(AssetData);
	if (CDV.IsValid())
	{
		CDV.View->SetObjects(GatherClassDetailsCDOs(CDV.Class, bIgnoreAsset ? &AssetData : nullptr));
	}
}

void FInputEditorModule::Tick(float DeltaTime)
{
	// Update any blueprints that are referencing an input action with a modified value type
	if (UInputAction::ActionsWithModifiedValueTypes.Num())
	{
		TSet<UBlueprint*> BPsModified;
		for (TObjectIterator<UK2Node_EnhancedInputAction> NodeIt; NodeIt; ++NodeIt)
		{
			if (UInputAction::ActionsWithModifiedValueTypes.Contains(NodeIt->InputAction))
			{
				NodeIt->ReconstructNode();
				BPsModified.Emplace(NodeIt->GetBlueprint());
			}
		}
		for (TObjectIterator<UK2Node_GetInputActionValue> NodeIt; NodeIt; ++NodeIt)
		{
			if (UInputAction::ActionsWithModifiedValueTypes.Contains(NodeIt->InputAction))
			{
				NodeIt->ReconstructNode();
				BPsModified.Emplace(NodeIt->GetBlueprint());
			}
		}

		if (BPsModified.Num())
		{
			FNotificationInfo Info(FText::Format(LOCTEXT("ActionValueTypeChange", "Changing action value type affected {0} blueprint(s)!"), BPsModified.Num()));
			Info.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		UInputAction::ActionsWithModifiedValueTypes.Reset();
	}
}

#undef LOCTEXT_NAMESPACE