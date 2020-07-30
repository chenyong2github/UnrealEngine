// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorModeRegistry.h"
#include "Modules/ModuleManager.h"
#include "EditorStyleSet.h"
#include "EdMode.h"
#include "EditorModes.h"
#include "EditorModeInterpolation.h"

#include "Editor/PlacementMode/Public/IPlacementModeModule.h"
#include "Editor/LandscapeEditor/Public/LandscapeEditorModule.h"
#include "Editor/MeshPaint/Public/MeshPaintModule.h"
#include "Editor/ActorPickerMode/Public/ActorPickerMode.h"
#include "Editor/SceneDepthPickerMode/Public/SceneDepthPickerMode.h"
#include "Editor/FoliageEdit/Public/FoliageEditModule.h"
#include "Editor/VirtualTexturingEditor/Public/VirtualTexturingEditorModule.h"
#include "Classes/EditorStyleSettings.h"
#include "Subsystems/AssetEditorSubsystem.h"

FEditorModeFactory::FEditorModeFactory(const FEditorModeInfo& InModeInfo)
	: ModeInfo(InModeInfo)
{
}

FEditorModeFactory::FEditorModeFactory(FEditorModeInfo&& InModeInfo)
	: ModeInfo(InModeInfo)
{
}

FEditorModeFactory::~FEditorModeFactory()
{
}

FEditorModeInfo FEditorModeFactory::GetModeInfo() const
{
	return ModeInfo;
}

TSharedRef<FEdMode> FEditorModeFactory::CreateMode() const
{
	return FactoryCallback.Execute();
}

FEditorModeInfo::FEditorModeInfo()
	: ID(NAME_None)
	, bVisible(false)
	, PriorityOrder(MAX_int32)
{
}

FEditorModeInfo::FEditorModeInfo(
	FEditorModeID InID,
	FText InName,
	FSlateIcon InIconBrush,
	bool InIsVisible,
	int32 InPriorityOrder
	)
	: ID(InID)
	, ToolbarCustomizationName(*(InID.ToString()+TEXT("Toolbar")))
	, Name(InName)
	, IconBrush(InIconBrush)
	, bVisible(InIsVisible)
	, PriorityOrder(InPriorityOrder)
{
	if (!InIconBrush.IsSet())
	{
		FModuleManager::Get().LoadModule("EditorStyle"); // Need to ensure the EditorStyle module is loaded before we access static functions from one of its headers
		IconBrush = FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditorModes");
	}
}

void FEditorModeRegistry::Initialize()
{
	// Send notifications for any legacy modes that were registered before the asset subsytem started up
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	for (const FactoryMap::ElementType& ModeEntry : ModeFactories)
	{
		AssetEditorSubsystem->OnEditorModeRegistered().Broadcast(ModeEntry.Key);
	}

	if(!GetDefault<UEditorStyleSettings>()->bEnableLegacyEditorModeUI)
	{
		// Add default editor modes
		RegisterMode<FEdModeDefault>(
			FBuiltinEditorModes::EM_Default,
			NSLOCTEXT("DefaultMode", "DisplayName", "Select"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SelectMode", "LevelEditor.SelectMode.Small"),
			true, 0);
	}
	else
	{
		RegisterMode<FEdModeDefault>(FBuiltinEditorModes::EM_Default);
	}

	RegisterMode<FEdModeInterpEdit>(FBuiltinEditorModes::EM_InterpEdit);

	FModuleManager::LoadModuleChecked<IPlacementModeModule>(TEXT("PlacementMode"));
	FModuleManager::LoadModuleChecked<FActorPickerModeModule>(TEXT("ActorPickerMode"));
	FModuleManager::LoadModuleChecked<FSceneDepthPickerModeModule>(TEXT("SceneDepthPickerMode"));
	FModuleManager::LoadModuleChecked<IMeshPaintModule>(TEXT("MeshPaintMode"));
	FModuleManager::LoadModuleChecked<ILandscapeEditorModule>(TEXT("LandscapeEditor"));
	FModuleManager::LoadModuleChecked<IFoliageEditModule>(TEXT("FoliageEdit"));
	FModuleManager::LoadModuleChecked<IVirtualTexturingEditorModule>(TEXT("VirtualTexturingEditor"));

	bInitialized = true;
}

void FEditorModeRegistry::Shutdown()
{
	bInitialized = false;

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	for (auto& ModeEntry : ModeFactories)
	{
		AssetEditorSubsystem->OnEditorModeUnregistered().Broadcast(ModeEntry.Key);
	}

	ModeFactories.Empty();
}

FEditorModeRegistry& FEditorModeRegistry::Get()
{
	static TSharedRef<FEditorModeRegistry> GModeRegistry = MakeShared<FEditorModeRegistry>();
	return GModeRegistry.Get();	
}

TArray<FEditorModeInfo> FEditorModeRegistry::GetSortedModeInfo() const
{
	return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetEditorModeInfoOrderedByPriority();
}

FEditorModeInfo FEditorModeRegistry::GetModeInfo(FEditorModeID ModeID) const
{
	FEditorModeInfo Result;
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorModeInfo(ModeID, Result);
	return Result;
}

TSharedPtr<FEdMode> FEditorModeRegistry::CreateMode(FEditorModeID ModeID, FEditorModeTools& Owner)
{
	const TSharedRef<IEditorModeFactory>* ModeFactory = ModeFactories.Find(ModeID);
	if (ModeFactory)
	{
		TSharedRef<FEdMode> Instance = (*ModeFactory)->CreateMode();

		// Assign the mode info from the factory before we initialize
		Instance->Info = (*ModeFactory)->GetModeInfo();
		Instance->Owner = &Owner;

		Instance->Initialize();

		return Instance;
	}

	return nullptr;
}

void FEditorModeRegistry::RegisterMode(FEditorModeID ModeID, TSharedRef<IEditorModeFactory> Factory)
{
	check(ModeID != FBuiltinEditorModes::EM_None);
	check(!ModeFactories.Contains(ModeID));

	ModeFactories.Add(ModeID, Factory);

	if (bInitialized)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		AssetEditorSubsystem->OnEditorModeRegistered().Broadcast(ModeID);
		AssetEditorSubsystem->OnEditorModesChanged().Broadcast();
	}
}

void FEditorModeRegistry::UnregisterMode(FEditorModeID ModeID)
{
	// First off delete the factory
	if (ModeFactories.Remove(ModeID) > 0 && bInitialized)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		AssetEditorSubsystem->OnEditorModeUnregistered().Broadcast(ModeID);
		AssetEditorSubsystem->OnEditorModesChanged().Broadcast();
	}
}

FRegisteredModesChangedEvent& FEditorModeRegistry::OnRegisteredModesChanged()
{
	return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModesChanged();
}


FOnModeRegistered& FEditorModeRegistry::OnModeRegistered()
{
	return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModeRegistered();
}


FOnModeUnregistered& FEditorModeRegistry::OnModeUnregistered()
{
	return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModeUnregistered();
}
