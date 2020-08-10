// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosCachingEditorPlugin.h"

#include "AssetToolsModule.h"
#include "Chaos/Adapters/CacheAdapter.h"
#include "Chaos/AssetTypeActions_ChaosCacheCollection.h"
#include "Chaos/CacheCollectionCustomization.h"
#include "Chaos/CacheEditorCommands.h"
#include "Chaos/CacheManagerActor.h"
#include "Chaos/CacheManagerCustomization.h"
#include "CoreMinimal.h"
#include "Engine/Selection.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"

IMPLEMENT_MODULE(IChaosCachingEditorPlugin, ChaosCachingEditor)

#define LOCTEXT_NAMESPACE "CacheEditorPlugin"

void IChaosCachingEditorPlugin::StartupModule()
{
	AssetTypeActions_ChaosCacheCollection = new FAssetTypeActions_ChaosCacheCollection();

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	IAssetTools&       AssetTools       = AssetToolsModule.Get();
	AssetTools.RegisterAssetTypeActions(MakeShareable(AssetTypeActions_ChaosCacheCollection));

	FCachingEditorCommands::Register();

	StartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &IChaosCachingEditorPlugin::RegisterMenus));

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("ChaosCacheCollection", FOnGetDetailCustomizationInstance::CreateStatic(&FCacheCollectionDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("ChaosCacheManager", FOnGetDetailCustomizationInstance::CreateStatic(&FCacheManagerDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("ObservedComponent", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FObservedComponentDetails::MakeInstance));
}

void IChaosCachingEditorPlugin::ShutdownModule()
{
	if(UObjectInitialized())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("ObservedComponent");
		PropertyModule.UnregisterCustomClassLayout("ChaosCacheManager");
		PropertyModule.UnregisterCustomClassLayout("ChaosCacheCollection");

		UToolMenus::UnRegisterStartupCallback(StartupHandle);

		FCachingEditorCommands::Unregister();

		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		IAssetTools&       AssetTools       = AssetToolsModule.Get();

		AssetTools.UnregisterAssetTypeActions(AssetTypeActions_ChaosCacheCollection->AsShared());
	}
}

bool IsCreateCacheManagerVisible();
bool IsSetAllRecordVisible();
bool IsSetAllPlayVisible();

void IChaosCachingEditorPlugin::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScope(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu");

	FToolMenuSection* Section = Menu->FindSection("Chaos");
	if(!Section)
	{
		Section = &Menu->AddSection("Chaos", LOCTEXT("ChaosSectionLabel", "Chaos"));
	}

	Section->InitSection("Chaos", LOCTEXT("ChaosSectionLabel", "Chaos"), FToolMenuInsert());

	Section->AddSubMenu("CachingSub",
						LOCTEXT("SubMenu_Caching", "Caching"),
						LOCTEXT("Tooltip_Caching", "Options for manipulating cache managers and their observed components"),
						FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InMenu) {
							FToolMenuSection& CacheSubMenuSection = InMenu->AddSection("Caching");
							RegisterCachingSubMenu(InMenu, &CacheSubMenuSection);
						}),
						FUIAction(FExecuteAction(), FCanExecuteAction(), FIsActionChecked(), FIsActionButtonVisible::CreateLambda([]() -> bool 
						{
							return IsCreateCacheManagerVisible() || IsSetAllPlayVisible() || IsSetAllRecordVisible();
						})),
						EUserInterfaceActionType::Button);
}

void IChaosCachingEditorPlugin::RegisterCachingSubMenu(UToolMenu* InMenu, FToolMenuSection* InSection)
{
	InSection->AddMenuEntry("CreateCacheManager",
							LOCTEXT("MenuItem_CreateCacheManager", "Create Cache Manager"),
							LOCTEXT("MenuItem_CreateCacheManager_ToolTip", "Adds a cache manager to observe compatible components in the selection set."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateRaw(this, &IChaosCachingEditorPlugin::OnCreateCacheManager),
									  FCanExecuteAction(),
									  FIsActionChecked(),
									  FIsActionButtonVisible::CreateStatic(&IsCreateCacheManagerVisible)));

	InSection->AddMenuEntry("SetRecordAll",
							LOCTEXT("MenuItem_SetRecordAll", "Set All Record"),
							LOCTEXT("MenuItem_SetRecordAll_ToolTip", "Sets selected cache managers to record all of their observed components."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateRaw(this, &IChaosCachingEditorPlugin::OnSetAllRecord),
									  FCanExecuteAction(),
									  FIsActionChecked(),
									  FIsActionButtonVisible::CreateStatic(&IsSetAllRecordVisible)));

	InSection->AddMenuEntry("SetPlayAll",
							LOCTEXT("MenuItem_SetPlayAll", "Set All Play"),
							LOCTEXT("MenuItem_SetPlayAll_ToolTip", "Sets selected cache managers to playback all of their observed components."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateRaw(this, &IChaosCachingEditorPlugin::OnSetAllPlay),
									  FCanExecuteAction(),
									  FIsActionChecked(),
									  FIsActionButtonVisible::CreateStatic(&IsSetAllPlayVisible)));
}

void IChaosCachingEditorPlugin::OnCreateCacheManager()
{
	auto SpawnManager = [](UWorld* InWorld) -> AChaosCacheManager* {
		FActorSpawnParameters Params;
		return InWorld->SpawnActor<AChaosCacheManager>();
	};

	AChaosCacheManager* Manager = nullptr;

	// Get the implementation of our adapters for identifying compatible components
	IModularFeatures&                      ModularFeatures = IModularFeatures::Get();
	TArray<Chaos::FComponentCacheAdapter*> Adapters = ModularFeatures.GetModularFeatureImplementations<Chaos::FComponentCacheAdapter>(Chaos::FComponentCacheAdapter::FeatureName);

	USelection* SelectedActors = GEditor->GetSelectedActors();

	TArray<AActor*> Actors;
	SelectedActors->GetSelectedObjects<AActor>(Actors);

	TArray<UActorComponent*> ComponentArray;
	for(AActor* Actor : Actors)
	{
		ComponentArray.Reset();
		Actor->GetComponents(ComponentArray);

		for(UActorComponent* Component : ComponentArray)
		{
			if(Component->CreationMethod == EComponentCreationMethod::UserConstructionScript)
			{
				// Can't hold references to UCS created components (See FComponentEditorUtils::MakeComponentReference)
				continue;
			}

			if(UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(Component))
			{
				Chaos::FComponentCacheAdapter* BestFitAdapter = Chaos::FAdapterUtil::GetBestAdapterForClass(PrimitiveComp->GetClass());

				// Can't be observed
				if(!BestFitAdapter)
				{
					continue;
				}

				// If we get here without a manager, lazy spawn one
				if(!Manager)
				{
					Manager = SpawnManager(Component->GetWorld());
				}

				check(Manager);

				FObservedComponent* Existing = Manager->ObservedComponents.FindByPredicate([PrimitiveComp](const FObservedComponent& InItem) {
					return InItem.GetComponent() == PrimitiveComp;
				});

				if(!Existing)
				{
					FObservedComponent& NewEntry = Manager->AddNewObservedComponent(PrimitiveComp);
				}
			}
		}
	}
}

bool IsCreateCacheManagerVisible()
{
	IModularFeatures&                      ModularFeatures = IModularFeatures::Get();
	TArray<Chaos::FComponentCacheAdapter*> Adapters = ModularFeatures.GetModularFeatureImplementations<Chaos::FComponentCacheAdapter>(Chaos::FComponentCacheAdapter::FeatureName);

	USelection* SelectedActors = GEditor->GetSelectedActors();

	TArray<AActor*> Actors;
	SelectedActors->GetSelectedObjects<AActor>(Actors);

	TArray<UActorComponent*> ComponentArray;
	for(AActor* Actor : Actors)
	{
		ComponentArray.Reset();
		Actor->GetComponents(ComponentArray);

		for(UActorComponent* Component : ComponentArray)
		{
			if(Component->CreationMethod == EComponentCreationMethod::UserConstructionScript)
			{
				// Can't hold references to UCS created components (See FComponentEditorUtils::MakeComponentReference)
				continue;
			}

			if(UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(Component))
			{
				Chaos::FComponentCacheAdapter* BestFitAdapter = Chaos::FAdapterUtil::GetBestAdapterForClass(PrimitiveComp->GetClass());

				// Can't be observed
				if(!BestFitAdapter)
				{
					continue;
				}

				// We have an adapter which means it's possible to observe this component so the option to create a manager should be visible
				return true;
			}
		}
	}

	return false;
}

void IChaosCachingEditorPlugin::OnSetAllPlay()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();

	TArray<AChaosCacheManager*> CacheManagers;
	SelectedActors->GetSelectedObjects<AChaosCacheManager>(CacheManagers);

	for(AChaosCacheManager* Manager : CacheManagers)
	{
		if(Manager)
		{
			Manager->SetAllMode(ECacheMode::Play);
		}
	}
}

template<typename T>
bool SelectionContains()
{
	static_assert(TIsDerivedFrom<T, AActor>::Value, "Must be an actor type to be a selected actor.");

	USelection* SelectedActors = GEditor->GetSelectedActors();

	TArray<T*> Items;
	SelectedActors->GetSelectedObjects<T>(Items);

	return Items.Num() > 0;
}

bool IsSetAllPlayVisible()
{
	return SelectionContains<AChaosCacheManager>();
}

void IChaosCachingEditorPlugin::OnSetAllRecord()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();

	TArray<AChaosCacheManager*> CacheManagers;
	SelectedActors->GetSelectedObjects<AChaosCacheManager>(CacheManagers);

	for(AChaosCacheManager* Manager : CacheManagers)
	{
		if(Manager)
		{
			Manager->SetAllMode(ECacheMode::Record);
		}
	}
}

bool IsSetAllRecordVisible()
{
	return SelectionContains<AChaosCacheManager>();
}

#undef LOCTEXT_NAMESPACE
