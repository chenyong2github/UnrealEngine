// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "Tools/UEdMode.h"
#include "Classes/EditorStyleSettings.h"

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
		IconBrush = FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditorModes");
	}
}

FEditorModeRegistry* GModeRegistry = nullptr;

void FEditorModeRegistry::Initialize()
{
	Get();

	if(!GetDefault<UEditorStyleSettings>()->bEnableLegacyEditorModeUI)
	{
		// Add default editor modes
		FEditorModeRegistry::Get().RegisterMode<FEdModeDefault>(
			FBuiltinEditorModes::EM_Default,
			NSLOCTEXT("DefaultMode", "DisplayName", "Select"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.SelectMode", "LevelEditor.SelectMode.Small"),
			true, 0);
	}
	else
	{
		GModeRegistry->RegisterMode<FEdModeDefault>(FBuiltinEditorModes::EM_Default);
	}

	GModeRegistry->RegisterMode<FEdModeInterpEdit>(FBuiltinEditorModes::EM_InterpEdit);

	// Load editor mode modules that will automatically register their editor modes, and clean themselves up on unload.
	//@TODO: ROCKET: These are probably good plugin candidates, that shouldn't have to be force-loaded here but discovery loaded somehow
	FModuleManager::LoadModuleChecked<IPlacementModeModule>(TEXT("PlacementMode"));
	FModuleManager::LoadModuleChecked<FActorPickerModeModule>(TEXT("ActorPickerMode"));
	FModuleManager::LoadModuleChecked<FSceneDepthPickerModeModule>(TEXT("SceneDepthPickerMode"));
	FModuleManager::LoadModuleChecked<IMeshPaintModule>(TEXT("MeshPaintMode"));
	FModuleManager::LoadModuleChecked<ILandscapeEditorModule>(TEXT("LandscapeEditor"));
	FModuleManager::LoadModuleChecked<IFoliageEditModule>(TEXT("FoliageEdit"));
	FModuleManager::LoadModuleChecked<IVirtualTexturingEditorModule>(TEXT("VirtualTexturingEditor"));
}

void FEditorModeRegistry::Shutdown()
{
	delete GModeRegistry;
	GModeRegistry = nullptr;
}

FEditorModeRegistry& FEditorModeRegistry::Get()
{
	if (!GModeRegistry)
	{
		GModeRegistry = new FEditorModeRegistry;
	}
	return *GModeRegistry;	
}

TArray<FEditorModeInfo> FEditorModeRegistry::GetSortedModeInfo() const
{
	TArray<FEditorModeInfo> ModeInfoArray;
	
	for (const auto& Pair : ModeFactories)
	{
		ModeInfoArray.Add(Pair.Value->GetModeInfo());
	}

	ModeInfoArray.Sort([](const FEditorModeInfo& A, const FEditorModeInfo& B){
		return A.PriorityOrder < B.PriorityOrder;
	});

	return ModeInfoArray;
}

FEditorModeInfo FEditorModeRegistry::GetModeInfo(FEditorModeID ModeID) const
{
	FEditorModeInfo Result;
	const TSharedRef<IEditorModeFactory>* ModeFactory = ModeFactories.Find(ModeID);
	if (ModeFactory)
	{
		Result = (*ModeFactory)->GetModeInfo();
	}
	
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

		// This binding ensures the mode is destroyed if the type is unregistered
		OnModeUnregistered().AddSP(Instance, &FEdMode::OnModeUnregistered);

		Instance->Initialize();

		return Instance;
	}

	return nullptr;
}

UEdMode* FEditorModeRegistry::CreateScriptableMode(FEditorModeID ModeID, FEditorModeTools& Owner)
{
	const TSharedRef<IEditorModeFactory>* ModeFactory = ModeFactories.Find(ModeID);
	if (ModeFactory)
	{
		UEdMode* Instance = (*ModeFactory)->CreateScriptableMode();

		// Assign the mode info from the factory before we initialize
		Instance->Info = (*ModeFactory)->GetModeInfo();
		Instance->Owner = &Owner;

		// This binding ensures the mode is destroyed if the type is unregistered
		OnModeUnregistered().AddUObject(Instance, &UEdMode::OnModeUnregistered);

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

	OnModeRegisteredEvent.Broadcast(ModeID);
	RegisteredModesChanged.Broadcast();
}

void FEditorModeRegistry::UnregisterMode(FEditorModeID ModeID)
{
	// First off delete the factory
	if (ModeFactories.Remove(ModeID) > 0)
	{
		OnModeUnregisteredEvent.Broadcast(ModeID);
		RegisteredModesChanged.Broadcast();
	}
}
