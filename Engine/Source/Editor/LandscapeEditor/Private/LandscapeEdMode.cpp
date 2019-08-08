// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeEdMode.h"
#include "SceneView.h"
#include "Engine/Texture2D.h"
#include "EditorViewportClient.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "EditorModeManager.h"
#include "LandscapeFileFormatInterface.h"
#include "LandscapeEditorModule.h"
#include "LandscapeEditorObject.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"

#include "EditorSupportDelegates.h"
#include "ScopedTransaction.h"
#include "LandscapeEdit.h"
#include "LandscapeEditorUtils.h"
#include "LandscapeRender.h"
#include "LandscapeDataAccess.h"
#include "Framework/Commands/UICommandList.h"
#include "LevelEditor.h"
#include "Toolkits/ToolkitManager.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "InstancedFoliageActor.h"
#include "EditorWorldExtension.h"
#include "ViewportWorldInteraction.h"
#include "VREditorInteractor.h"
#include "LandscapeEdModeTools.h"
#include "LandscapeInfoMap.h"

//Slate dependencies
#include "Misc/FeedbackContext.h"
#include "ILevelViewport.h"
#include "SLandscapeEditor.h"
#include "Framework/Application/SlateApplication.h"

// VR Editor
#include "VREditorMode.h"

// Classes
#include "LandscapeMaterialInstanceConstant.h"
#include "LandscapeSplinesComponent.h"
#include "ComponentReregisterContext.h"
#include "EngineUtils.h"
#include "IVREditorModule.h"
#include "Misc/ScopedSlowTask.h"
#include "LandscapeEditorCommands.h"
#include "Framework/Commands/InputBindingManager.h"
#include "MouseDeltaTracker.h"
#include "Interfaces/IMainFrameModule.h"
#include "LandscapeBlueprintBrushBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Settings/EditorExperimentalSettings.h"
#include "ComponentRecreateRenderStateContext.h"

#define LOCTEXT_NAMESPACE "Landscape"

DEFINE_LOG_CATEGORY(LogLandscapeEdMode);

struct HNewLandscapeGrabHandleProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	ELandscapeEdge::Type Edge;

	HNewLandscapeGrabHandleProxy(ELandscapeEdge::Type InEdge) :
		HHitProxy(HPP_Wireframe),
		Edge(InEdge)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		switch (Edge)
		{
		case ELandscapeEdge::X_Negative:
		case ELandscapeEdge::X_Positive:
			return EMouseCursor::ResizeLeftRight;
		case ELandscapeEdge::Y_Negative:
		case ELandscapeEdge::Y_Positive:
			return EMouseCursor::ResizeUpDown;
		case ELandscapeEdge::X_Negative_Y_Negative:
		case ELandscapeEdge::X_Positive_Y_Positive:
			return EMouseCursor::ResizeSouthEast;
		case ELandscapeEdge::X_Negative_Y_Positive:
		case ELandscapeEdge::X_Positive_Y_Negative:
			return EMouseCursor::ResizeSouthWest;
		}

		return EMouseCursor::SlashedCircle;
	}
};

IMPLEMENT_HIT_PROXY(HNewLandscapeGrabHandleProxy, HHitProxy)

ENGINE_API extern bool GDisableAutomaticTextureMaterialUpdateDependencies;

void ALandscape::SplitHeightmap(ULandscapeComponent* Comp, ALandscapeProxy* TargetProxy,FMaterialUpdateContext* InOutUpdateContext, TArray<FComponentRecreateRenderStateContext>* InOutRecreateRenderStateContext, bool InReregisterComponent)
{
	ULandscapeInfo* Info = Comp->GetLandscapeInfo();
	
	// Make sure the heightmap UVs are powers of two.
	int32 ComponentSizeVerts = Comp->NumSubsections * (Comp->SubsectionSizeQuads + 1);
	int32 HeightmapSizeU = (1 << FMath::CeilLogTwo(ComponentSizeVerts));
	int32 HeightmapSizeV = (1 << FMath::CeilLogTwo(ComponentSizeVerts));

	ALandscapeProxy* SrcProxy = Comp->GetLandscapeProxy();
	ALandscapeProxy* DstProxy = TargetProxy ? TargetProxy : SrcProxy;
	SrcProxy->Modify();
	DstProxy->Modify();
	
	UTexture2D* OldHeightmapTexture = Comp->GetHeightmap(false);
	UTexture2D* NewHeightmapTexture = NULL;
	FVector4 OldHeightmapScaleBias = Comp->HeightmapScaleBias;
	FVector4 NewHeightmapScaleBias = FVector4(1.0f / (float)HeightmapSizeU, 1.0f / (float)HeightmapSizeV, 0.0f, 0.0f);

	{
		// Read old data and split
		FLandscapeEditDataInterface LandscapeEdit(Info);
		TArray<uint8> HeightData;
		HeightData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads)*sizeof(uint16));
		// Because of edge problem, normal would be just copy from old component data
		TArray<uint8> NormalData;
		NormalData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads)*sizeof(uint16));
		LandscapeEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, (uint16*)NormalData.GetData());

		// Create the new heightmap texture
		NewHeightmapTexture = DstProxy->CreateLandscapeTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8);
		ULandscapeComponent::CreateEmptyTextureMips(NewHeightmapTexture, true);
		Comp->HeightmapScaleBias = NewHeightmapScaleBias;
		Comp->SetHeightmap(NewHeightmapTexture);

		check(Comp->GetHeightmap(false) == Comp->GetHeightmap(true));
		LandscapeEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, false, (uint16*)NormalData.GetData());
	}

	// End material update
	if (InOutUpdateContext != nullptr && InOutRecreateRenderStateContext != nullptr)
	{
		Comp->UpdateMaterialInstances(*InOutUpdateContext, *InOutRecreateRenderStateContext);
	}
	else
	{
		Comp->UpdateMaterialInstances();
	}

	// We disable automatic material update context, to manage it manually if we have a custom update context specified
	GDisableAutomaticTextureMaterialUpdateDependencies = (InOutUpdateContext != nullptr);

	NewHeightmapTexture->PostEditChange();

	if (InOutUpdateContext != nullptr)
	{
		// Build a list of all unique materials the landscape uses
		TArray<UMaterialInterface*> LandscapeMaterials;

		int8 MaxLOD = FMath::CeilLogTwo(Comp->SubsectionSizeQuads + 1) - 1;

		for (int8 LODIndex = 0; LODIndex < MaxLOD; ++LODIndex)
		{
			UMaterialInterface* Material = Comp->GetLandscapeMaterial(LODIndex);
			LandscapeMaterials.AddUnique(Material);
		}

		TSet<UMaterial*> BaseMaterialsThatUseThisTexture;

		for (UMaterialInterface* MaterialInterface : LandscapeMaterials)
		{
			if (DoesMaterialUseTexture(MaterialInterface, NewHeightmapTexture))
			{
				UMaterial* Material = MaterialInterface->GetMaterial();
				bool MaterialAlreadyCompute = false;
				BaseMaterialsThatUseThisTexture.Add(Material, &MaterialAlreadyCompute);

				if (!MaterialAlreadyCompute)
				{
					if (Material->IsTextureForceRecompileCacheRessource(NewHeightmapTexture))
					{
						InOutUpdateContext->AddMaterial(Material);
						Material->UpdateMaterialShaderCacheAndTextureReferences();
					}
				}
			}
		}
	}
	
	GDisableAutomaticTextureMaterialUpdateDependencies = false;

#if WITH_EDITORONLY_DATA
	check(Comp->GetLandscapeProxy()->HasLayersContent() == DstProxy->CanHaveLayersContent());
	if (Comp->GetLandscapeProxy()->HasLayersContent() && DstProxy->CanHaveLayersContent())
	{
		FLandscapeLayersTexture2DCPUReadBackResource* NewCPUReadBackResource = new FLandscapeLayersTexture2DCPUReadBackResource(NewHeightmapTexture->Source.GetSizeX(), NewHeightmapTexture->Source.GetSizeY(), NewHeightmapTexture->GetPixelFormat(), NewHeightmapTexture->Source.GetNumMips());
		BeginInitResource(NewCPUReadBackResource);
		DstProxy->HeightmapsCPUReadBack.Add(NewHeightmapTexture, NewCPUReadBackResource);

		// Free OldHeightmapTexture's CPUReadBackResource if not used by any component
		bool FreeCPUReadBack = true;
		for (ULandscapeComponent* Component : SrcProxy->LandscapeComponents)
		{
			if (Component != Comp && Component->GetHeightmap(false) == OldHeightmapTexture)
			{
				FreeCPUReadBack = false;
				break;
			}
		}
		if (FreeCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource** OldCPUReadBackResource = SrcProxy->HeightmapsCPUReadBack.Find(OldHeightmapTexture);
			if (OldCPUReadBackResource)
			{
				if (FLandscapeLayersTexture2DCPUReadBackResource* ResourceToDelete = *OldCPUReadBackResource)
				{
					ReleaseResourceAndFlush(ResourceToDelete);
					delete ResourceToDelete;
					SrcProxy->HeightmapsCPUReadBack.Remove(OldHeightmapTexture);
				}
			}
		}

		// Move layer content to new layer heightmap
		FLandscapeEditDataInterface LandscapeEdit(Info);
		ALandscape* Landscape = Info->LandscapeActor.Get();
		Comp->ForEachLayer([&](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
		{
			UTexture2D* OldLayerHeightmap = LayerData.HeightmapData.Texture;
			if (OldLayerHeightmap != nullptr)
			{
				FScopedSetLandscapeEditingLayer Scope(Landscape, LayerGuid);
				// Read old data and split
				TArray<uint8> LayerHeightData;
				LayerHeightData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads) * sizeof(uint16));
				// Because of edge problem, normal would be just copy from old component data
				TArray<uint8> LayerNormalData;
				LayerNormalData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads) * sizeof(uint16));

				// Read using old heightmap scale/bias
				Comp->HeightmapScaleBias = OldHeightmapScaleBias;
				LandscapeEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, (uint16*)LayerNormalData.GetData());
				// Restore new heightmap scale/bias
				Comp->HeightmapScaleBias = NewHeightmapScaleBias;
				{
					UTexture2D* LayerHeightmapTexture = DstProxy->CreateLandscapeTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8);
					ULandscapeComponent::CreateEmptyTextureMips(LayerHeightmapTexture, true);
					LayerHeightmapTexture->PostEditChange();
					// Set Layer heightmap texture
					LayerData.HeightmapData.Texture = LayerHeightmapTexture;
					LandscapeEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, false, (uint16*)LayerNormalData.GetData());
				}
			}
		});

		Landscape->RequestLayersContentUpdateForceAll();
	}
#endif

	// Reregister
	if (InReregisterComponent)
	{
		FComponentReregisterContext ReregisterContext(Comp);
	}
}



void FLandscapeTool::SetEditRenderType()
{
	GLandscapeEditRenderMode = ELandscapeEditRenderMode::SelectRegion | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask);
}

namespace LandscapeTool
{
	UMaterialInstance* CreateMaterialInstance(UMaterialInterface* BaseMaterial)
	{
		ULandscapeMaterialInstanceConstant* MaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(GetTransientPackage());
		MaterialInstance->bEditorToolUsage = true;
		MaterialInstance->SetParentEditorOnly(BaseMaterial);
		MaterialInstance->PostEditChange();
		return MaterialInstance;
	}
}

//
// FEdModeLandscape
//

/** Constructor */
FEdModeLandscape::FEdModeLandscape()
	: FEdMode()
	, UISettings(nullptr)
	, CurrentToolMode(nullptr)
	, CurrentTool(nullptr)
	, CurrentBrush(nullptr)
	, GizmoBrush(nullptr)
	, CurrentToolIndex(INDEX_NONE)
	, CurrentBrushSetIndex(0)
	, NewLandscapePreviewMode(ENewLandscapePreviewMode::None)
	, DraggingEdge(ELandscapeEdge::None)
	, DraggingEdge_Remainder(0)
	, CurrentGizmoActor(nullptr)
	, CopyPasteTool(nullptr)
	, SplinesTool(nullptr)
	, LandscapeRenderAddCollision(nullptr)
	, CachedLandscapeMaterial(nullptr)
	, ToolActiveViewport(nullptr)
	, bIsPaintingInVR(false)
	, InteractorPainting( nullptr )
	, bNeedsUpdateShownLayerList(false)
{
	GLayerDebugColorMaterial = LandscapeTool::CreateMaterialInstance(LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorLandscapeResources/LayerVisMaterial.LayerVisMaterial")));
	GSelectionColorMaterial  = LandscapeTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial_Selected.SelectBrushMaterial_Selected")));
	GSelectionRegionMaterial = LandscapeTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial_SelectedRegion.SelectBrushMaterial_SelectedRegion")));
	GMaskRegionMaterial      = LandscapeTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/MaskBrushMaterial_MaskedRegion.MaskBrushMaterial_MaskedRegion")));
	GColorMaskRegionMaterial = LandscapeTool::CreateMaterialInstance(LoadObject<UMaterialInstanceConstant>(nullptr, TEXT("/Engine/EditorLandscapeResources/ColorMaskBrushMaterial_MaskedRegion.ColorMaskBrushMaterial_MaskedRegion")));
	GLandscapeBlackTexture   = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
	GLandscapeLayerUsageMaterial = LandscapeTool::CreateMaterialInstance(LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorLandscapeResources/LandscapeLayerUsageMaterial.LandscapeLayerUsageMaterial")));
	
	// Initialize modes
	UpdateToolModes();
	
	// Initialize tools.
	InitializeTool_Paint();
	InitializeTool_Smooth();
	InitializeTool_Flatten();
	InitializeTool_Erosion();
	InitializeTool_HydraErosion();
	InitializeTool_Noise();
	InitializeTool_Retopologize();
	InitializeTool_NewLandscape();
	InitializeTool_ResizeLandscape();
	InitializeTool_Select();
	InitializeTool_AddComponent();
	InitializeTool_DeleteComponent();
	InitializeTool_MoveToLevel();
	InitializeTool_Mask();
	InitializeTool_CopyPaste();
	InitializeTool_Visibility();
	InitializeTool_Splines();
	InitializeTool_Ramp();
	InitializeTool_Mirror();
	InitializeTool_BlueprintBrush();

	// Initialize brushes
	InitializeBrushes();

	CurrentBrush = LandscapeBrushSets[0].Brushes[0];
	
	CurrentToolTarget.LandscapeInfo = nullptr;
	CurrentToolTarget.TargetType = ELandscapeToolTargetType::Heightmap;
	CurrentToolTarget.LayerInfo = nullptr;

	UISettings = NewObject<ULandscapeEditorObject>(GetTransientPackage(), TEXT("UISettings"), RF_Transactional);
	UISettings->SetParent(this);

	ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");
	TSharedPtr<FUICommandList> CommandList = LandscapeEditorModule.GetLandscapeLevelViewportCommandList();

	const FLandscapeEditorCommands& LandscapeActions = FLandscapeEditorCommands::Get();
	CommandList->MapAction(LandscapeActions.IncreaseBrushSize, FExecuteAction::CreateRaw(this, &FEdModeLandscape::ChangeBrushSize, true), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(LandscapeActions.DecreaseBrushSize, FExecuteAction::CreateRaw(this, &FEdModeLandscape::ChangeBrushSize, false), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(LandscapeActions.IncreaseBrushFalloff, FExecuteAction::CreateRaw(this, &FEdModeLandscape::ChangeBrushFalloff, true), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(LandscapeActions.DecreaseBrushFalloff, FExecuteAction::CreateRaw(this, &FEdModeLandscape::ChangeBrushFalloff, false), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(LandscapeActions.IncreaseBrushStrength, FExecuteAction::CreateRaw(this, &FEdModeLandscape::ChangeBrushStrength, true), FCanExecuteAction(), FIsActionChecked());
	CommandList->MapAction(LandscapeActions.DecreaseBrushStrength, FExecuteAction::CreateRaw(this, &FEdModeLandscape::ChangeBrushStrength, false), FCanExecuteAction(), FIsActionChecked());
}

/** Destructor */
FEdModeLandscape::~FEdModeLandscape()
{
	// Destroy tools.
	LandscapeTools.Empty();

	// Destroy brushes
	LandscapeBrushSets.Empty();

	// Clean up Debug Materials
	FlushRenderingCommands();
	GLayerDebugColorMaterial = NULL;
	GSelectionColorMaterial = NULL;
	GSelectionRegionMaterial = NULL;
	GMaskRegionMaterial = NULL;
	GColorMaskRegionMaterial = NULL;
	GLandscapeBlackTexture = NULL;
	GLandscapeLayerUsageMaterial = NULL;

	InteractorPainting = nullptr;
}

/** FGCObject interface */
void FEdModeLandscape::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Call parent implementation
	FEdMode::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(UISettings);

	Collector.AddReferencedObject(GLayerDebugColorMaterial);
	Collector.AddReferencedObject(GSelectionColorMaterial);
	Collector.AddReferencedObject(GSelectionRegionMaterial);
	Collector.AddReferencedObject(GMaskRegionMaterial);
	Collector.AddReferencedObject(GColorMaskRegionMaterial);
	Collector.AddReferencedObject(GLandscapeBlackTexture);
	Collector.AddReferencedObject(GLandscapeLayerUsageMaterial);
}

void FEdModeLandscape::UpdateToolModes()
{
	// Keep mapping of CurrentTool and CurrentTargetLayer
	TMap<FName, FName> PreviousTools;
	TMap<FName, FName> PreviousTargetLayerNames;
	for (const FLandscapeToolMode& Previous : LandscapeToolModes)
	{
		PreviousTools.Add(Previous.ToolModeName, Previous.CurrentToolName);
		PreviousTargetLayerNames.Add(Previous.ToolModeName, Previous.CurrentTargetLayerName);
	}

	LandscapeToolModes.Reset();
	
	FLandscapeToolMode* ToolMode_Manage = new(LandscapeToolModes)FLandscapeToolMode(TEXT("ToolMode_Manage"), ELandscapeToolTargetTypeMask::NA);
	ToolMode_Manage->ValidTools.Add(TEXT("NewLandscape"));
	ToolMode_Manage->ValidTools.Add(TEXT("Select"));
	ToolMode_Manage->ValidTools.Add(TEXT("AddComponent"));
	ToolMode_Manage->ValidTools.Add(TEXT("DeleteComponent"));
	ToolMode_Manage->ValidTools.Add(TEXT("MoveToLevel"));
	ToolMode_Manage->ValidTools.Add(TEXT("ResizeLandscape"));
	ToolMode_Manage->ValidTools.Add(TEXT("Splines"));
	
	// Restore
	FName* PreviousToolName = PreviousTools.Find(ToolMode_Manage->ToolModeName);
	ToolMode_Manage->CurrentToolName = PreviousToolName ? *PreviousToolName : TEXT("Select");

	FName* PreviousTargetLayerName = PreviousTargetLayerNames.Find(ToolMode_Manage->ToolModeName);
	ToolMode_Manage->CurrentTargetLayerName = PreviousTargetLayerName ? *PreviousTargetLayerName : NAME_None;
		

	FLandscapeToolMode* ToolMode_Sculpt = new(LandscapeToolModes)FLandscapeToolMode(TEXT("ToolMode_Sculpt"), ELandscapeToolTargetTypeMask::Heightmap | ELandscapeToolTargetTypeMask::Visibility);
	ToolMode_Sculpt->ValidTools.Add(TEXT("Sculpt"));
	if (CanHaveLandscapeLayersContent())
	{
		ToolMode_Sculpt->ValidTools.Add(TEXT("Erase"));
	}
	ToolMode_Sculpt->ValidTools.Add(TEXT("Smooth"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Flatten"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Ramp"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Noise"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Erosion"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("HydraErosion"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Retopologize"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Visibility"));

	if (CanHaveLandscapeLayersContent())
	{
		ToolMode_Sculpt->ValidTools.Add(TEXT("BlueprintBrush"));
	}

	ToolMode_Sculpt->ValidTools.Add(TEXT("Mask"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("CopyPaste"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Mirror"));
	
	// Restore
	PreviousToolName = PreviousTools.Find(ToolMode_Sculpt->ToolModeName);
	ToolMode_Sculpt->CurrentToolName = PreviousToolName ? *PreviousToolName : TEXT("Sculpt");

	PreviousTargetLayerName = PreviousTargetLayerNames.Find(ToolMode_Sculpt->ToolModeName);
	ToolMode_Sculpt->CurrentTargetLayerName = PreviousTargetLayerName ? *PreviousTargetLayerName : NAME_None;

	FLandscapeToolMode* ToolMode_Paint = new(LandscapeToolModes)FLandscapeToolMode(TEXT("ToolMode_Paint"), ELandscapeToolTargetTypeMask::Weightmap);
	ToolMode_Paint->ValidTools.Add(TEXT("Paint"));
	ToolMode_Paint->ValidTools.Add(TEXT("Smooth"));
	ToolMode_Paint->ValidTools.Add(TEXT("Flatten"));
	ToolMode_Paint->ValidTools.Add(TEXT("Noise"));
	ToolMode_Paint->ValidTools.Add(TEXT("Visibility"));

	if (CanHaveLandscapeLayersContent())
	{
		ToolMode_Paint->ValidTools.Add(TEXT("BlueprintBrush"));
	}

	PreviousToolName = PreviousTools.Find(ToolMode_Paint->ToolModeName);
	ToolMode_Paint->CurrentToolName = PreviousToolName ? *PreviousToolName : TEXT("Paint");

	PreviousTargetLayerName = PreviousTargetLayerNames.Find(ToolMode_Paint->ToolModeName);
	ToolMode_Paint->CurrentTargetLayerName = PreviousTargetLayerName ? *PreviousTargetLayerName : NAME_None;

	// Since available tools might have changed try and reset the current tool
	if (CurrentToolMode && CurrentToolIndex != INDEX_NONE)
	{
		SetCurrentTool(CurrentToolIndex, CurrentToolMode->CurrentTargetLayerName);
	}
}

bool FEdModeLandscape::UsesToolkits() const
{
	return true;
}

TSharedRef<FUICommandList> FEdModeLandscape::GetUICommandList() const
{
	check(Toolkit.IsValid());
	return Toolkit->GetToolkitCommands();
}

void FEdModeLandscape::OnCanHaveLayersContentChanged()
{
	RefreshDetailPanel();
	UpdateToolModes();
}

void FEdModeLandscape::PostUpdateLayerContent()
{
	if (bNeedsUpdateShownLayerList)
	{
		UpdateShownLayerList();
	}
}

ELandscapeToolTargetType::Type FEdModeLandscape::GetLandscapeToolTargetType() const
{
	if (CurrentToolMode)
	{
		if (CurrentToolMode->ToolModeName == "ToolMode_Sculpt")
		{
			return CurrentToolTarget.TargetType == ELandscapeToolTargetType::Visibility ? ELandscapeToolTargetType::Visibility : ELandscapeToolTargetType::Heightmap;
		}
		else if (CurrentToolMode->ToolModeName == "ToolMode_Paint")
		{
			return ELandscapeToolTargetType::Weightmap;
		}
	}
	return ELandscapeToolTargetType::Invalid;
}

const FLandscapeLayer* FEdModeLandscape::GetLandscapeSelectedLayer() const
{
	return GetCurrentLayer();
}

ULandscapeLayerInfoObject* FEdModeLandscape::GetSelectedLandscapeLayerInfo() const
{
	return CurrentToolTarget.LayerInfo.Get();
}

void FEdModeLandscape::SetLandscapeInfo(ULandscapeInfo* InLandscapeInfo)
{
	if (CurrentToolTarget.LandscapeInfo != InLandscapeInfo)
	{
		CurrentToolTarget.LandscapeInfo = InLandscapeInfo;
		UpdateToolModes();
	}
}

/** FEdMode: Called when the mode is entered */
void FEdModeLandscape::Enter()
{
	ErrorReasonOnMouseUp = FText::GetEmpty();

	// Call parent implementation
	FEdMode::Enter();

	if (UWorld* World = GetWorld())
	{
		for (auto It = ULandscapeInfoMap::GetLandscapeInfoMap(World).Map.CreateIterator(); It; ++It)
		{
			if (ULandscapeInfo* LandscapeInfo = It.Value())
			{
				if (ALandscape* Landscape = !LandscapeInfo->IsPendingKill() ? LandscapeInfo->LandscapeActor.Get() : nullptr)
				{
					Landscape->RegisterLandscapeEdMode(this);
				}
			}
		}
	}

	OnLevelActorDeletedDelegateHandle = GEngine->OnLevelActorDeleted().AddSP(this, &FEdModeLandscape::OnLevelActorRemoved);
	OnLevelActorAddedDelegateHandle = GEngine->OnLevelActorAdded().AddSP(this, &FEdModeLandscape::OnLevelActorAdded);
		
	UpdateToolModes();

	ALandscapeProxy* SelectedLandscape = GEditor->GetSelectedActors()->GetTop<ALandscapeProxy>();
	if (SelectedLandscape)
	{
		SetLandscapeInfo(SelectedLandscape->GetLandscapeInfo());
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(SelectedLandscape, true, false);
	}
	else
	{
		GEditor->SelectNone(true, true);
	}

	for (TActorIterator<ALandscapeGizmoActiveActor> It(GetWorld()); It; ++It)
	{
		CurrentGizmoActor = *It;
		break;
	}

	if (!CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor = GetWorld()->SpawnActor<ALandscapeGizmoActiveActor>();
		CurrentGizmoActor->ImportFromClipboard();
	}

	// Update list of landscapes and layers
	// For now depends on the SpawnActor() above in order to get the current editor world as edmodes don't get told
	UpdateLandscapeList();
	UpdateTargetList();
	UpdateBrushList();

	OnWorldChangeDelegateHandle                 = FEditorSupportDelegates::WorldChange.AddRaw(this, &FEdModeLandscape::HandleLevelsChanged, true);
	OnLevelsChangedDelegateHandle				= GetWorld()->OnLevelsChanged().AddRaw(this, &FEdModeLandscape::HandleLevelsChanged, true);
	OnMaterialCompilationFinishedDelegateHandle = UMaterial::OnMaterialCompilationFinished().AddRaw(this, &FEdModeLandscape::OnMaterialCompilationFinished);

	if (CurrentToolTarget.LandscapeInfo.IsValid())
	{
		ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		LandscapeProxy->OnMaterialChangedDelegate().AddRaw(this, &FEdModeLandscape::OnLandscapeMaterialChangedDelegate);

		if (ALandscape* Landscape = GetLandscape())
		{
			Landscape->OnBlueprintBrushChangedDelegate().AddRaw(this, &FEdModeLandscape::RefreshDetailPanel);
			if (Landscape->HasLayersContent())
			{
				if (Landscape->GetLandscapeSplinesReservedLayer())
				{
					Landscape->UpdateLandscapeSplines();
				}
				Landscape->RequestLayersContentUpdateForceAll();
			}
		}
	}

	if (CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor->SetTargetLandscape(CurrentToolTarget.LandscapeInfo.Get());

		CurrentGizmoActor.Get()->bSnapToLandscapeGrid = UISettings->bSnapGizmo;
	}

	int32 SquaredDataTex = ALandscapeGizmoActiveActor::DataTexSize * ALandscapeGizmoActiveActor::DataTexSize;

	if (CurrentGizmoActor.IsValid() && !CurrentGizmoActor->GizmoTexture)
	{
		// Init Gizmo Texture...
		CurrentGizmoActor->GizmoTexture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		if (CurrentGizmoActor->GizmoTexture)
		{
			CurrentGizmoActor->GizmoTexture->Source.Init(
				ALandscapeGizmoActiveActor::DataTexSize,
				ALandscapeGizmoActiveActor::DataTexSize,
				1,
				1,
				TSF_G8
				);
			CurrentGizmoActor->GizmoTexture->SRGB = false;
			CurrentGizmoActor->GizmoTexture->CompressionNone = true;
			CurrentGizmoActor->GizmoTexture->MipGenSettings = TMGS_NoMipmaps;
			CurrentGizmoActor->GizmoTexture->AddressX = TA_Clamp;
			CurrentGizmoActor->GizmoTexture->AddressY = TA_Clamp;
			CurrentGizmoActor->GizmoTexture->LODGroup = TEXTUREGROUP_Terrain_Weightmap;
			uint8* TexData = CurrentGizmoActor->GizmoTexture->Source.LockMip(0);
			FMemory::Memset(TexData, 0, SquaredDataTex*sizeof(uint8));
			// Restore Sampled Data if exist...
			if (CurrentGizmoActor->CachedScaleXY > 0.0f)
			{
				int32 SizeX = FMath::CeilToInt(CurrentGizmoActor->CachedWidth / CurrentGizmoActor->CachedScaleXY);
				int32 SizeY = FMath::CeilToInt(CurrentGizmoActor->CachedHeight / CurrentGizmoActor->CachedScaleXY);
				for (int32 Y = 0; Y < CurrentGizmoActor->SampleSizeY; ++Y)
				{
					for (int32 X = 0; X < CurrentGizmoActor->SampleSizeX; ++X)
					{
						float TexX = X * SizeX / CurrentGizmoActor->SampleSizeX;
						float TexY = Y * SizeY / CurrentGizmoActor->SampleSizeY;
						int32 LX = FMath::FloorToInt(TexX);
						int32 LY = FMath::FloorToInt(TexY);

						float FracX = TexX - LX;
						float FracY = TexY - LY;

						FGizmoSelectData* Data00 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX, LY));
						FGizmoSelectData* Data10 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX + 1, LY));
						FGizmoSelectData* Data01 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX, LY + 1));
						FGizmoSelectData* Data11 = CurrentGizmoActor->SelectedData.Find(FIntPoint(LX + 1, LY + 1));

						TexData[X + Y*ALandscapeGizmoActiveActor::DataTexSize] = FMath::Lerp(
							FMath::Lerp(Data00 ? Data00->Ratio : 0, Data10 ? Data10->Ratio : 0, FracX),
							FMath::Lerp(Data01 ? Data01->Ratio : 0, Data11 ? Data11->Ratio : 0, FracX),
							FracY
							) * 255;
					}
				}
			}
			CurrentGizmoActor->GizmoTexture->Source.UnlockMip(0);
			CurrentGizmoActor->GizmoTexture->PostEditChange();
			FlushRenderingCommands();
		}
	}

	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->SampledHeight.Num() != SquaredDataTex)
	{
		CurrentGizmoActor->SampledHeight.Empty(SquaredDataTex);
		CurrentGizmoActor->SampledHeight.AddZeroed(SquaredDataTex);
		CurrentGizmoActor->DataType = LGT_None;
	}

	if (CurrentGizmoActor.IsValid()) // Update Scene Proxy
	{
		CurrentGizmoActor->ReregisterAllComponents();
	}

	GLandscapeEditRenderMode = ELandscapeEditRenderMode::None;
	GLandscapeEditModeActive = true;

	// Load UI settings from config file
	UISettings->Load();

	UpdateShownLayerList();

	// Initialize current tool prior to creating the landscape toolkit in case it has a dependency on it
	if (LandscapeList.Num() == 0)
	{
		SetCurrentToolMode("ToolMode_Manage", false);
		SetCurrentTool("NewLandscape");
	}
	else
	{
		if (CurrentToolMode == nullptr || (CurrentToolMode->CurrentToolName == FName("NewLandscape")) || CurrentToolMode->CurrentToolName == NAME_None)
		{
			SetCurrentToolMode("ToolMode_Sculpt", false);
			SetCurrentTool("Sculpt");
		}
		else
		{
			SetCurrentTool(CurrentToolMode->CurrentToolName);
		}
	}

	// Create the landscape editor window
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FLandscapeToolKit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	// Force real-time viewports.  We'll back up the current viewport state so we can restore it when the
	// user exits this mode.
	const bool bWantRealTime = true;
	const bool bRememberCurrentState = true;
	ForceRealTimeViewports(bWantRealTime, bRememberCurrentState);

	CurrentBrush->EnterBrush();
	if (GizmoBrush)
	{
		GizmoBrush->EnterBrush();
	}

	// Register to find out about VR input events
	UViewportWorldInteraction* ViewportWorldInteraction = Cast<UViewportWorldInteraction>(GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld())->FindExtension(UViewportWorldInteraction::StaticClass()));
	if (ViewportWorldInteraction != nullptr)
	{

			ViewportWorldInteraction->OnViewportInteractionInputAction().RemoveAll(this);
			ViewportWorldInteraction->OnViewportInteractionInputAction().AddRaw(this, &FEdModeLandscape::OnVRAction);

			ViewportWorldInteraction->OnViewportInteractionHoverUpdate().RemoveAll(this);
			ViewportWorldInteraction->OnViewportInteractionHoverUpdate().AddRaw(this, &FEdModeLandscape::OnVRHoverUpdate);

	}
}

/** FEdMode: Called when the mode is exited */
void FEdModeLandscape::Exit()
{
	if (UWorld* World = GetWorld())
	{
		for (auto It = ULandscapeInfoMap::GetLandscapeInfoMap(World).Map.CreateIterator(); It; ++It)
		{
			if (ULandscapeInfo* LandscapeInfo = It.Value())
			{
				if (ALandscape* Landscape = !LandscapeInfo->IsPendingKill() ? LandscapeInfo->LandscapeActor.Get() : nullptr)
				{
					Landscape->UnregisterLandscapeEdMode();
				}
			}
		}
	}

	// Unregister VR mode from event handlers
	UViewportWorldInteraction* ViewportWorldInteraction = Cast<UViewportWorldInteraction>(GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld())->FindExtension(UViewportWorldInteraction::StaticClass()));
	if (ViewportWorldInteraction != nullptr)
	{
		ViewportWorldInteraction->OnViewportInteractionInputAction().RemoveAll(this);
		ViewportWorldInteraction->OnViewportInteractionHoverUpdate().RemoveAll(this);
	}

	GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedDelegateHandle);
	GEngine->OnLevelActorAdded().Remove(OnLevelActorAddedDelegateHandle);
	
	FEditorSupportDelegates::WorldChange.Remove(OnWorldChangeDelegateHandle);
	GetWorld()->OnLevelsChanged().Remove(OnLevelsChangedDelegateHandle);
	UMaterial::OnMaterialCompilationFinished().Remove(OnMaterialCompilationFinishedDelegateHandle);

	if (CurrentToolTarget.LandscapeInfo.IsValid())
	{
		ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		LandscapeProxy->OnMaterialChangedDelegate().RemoveAll(this);
		if (ALandscape* Landscape = GetLandscape())
		{
			Landscape->OnBlueprintBrushChangedDelegate().RemoveAll(this);
		}
	}

	// Restore real-time viewport state if we changed it
	const bool bWantRealTime = false;
	const bool bRememberCurrentState = false;
	ForceRealTimeViewports(bWantRealTime, bRememberCurrentState);

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	CurrentBrush->LeaveBrush();
	if (GizmoBrush)
	{
		GizmoBrush->LeaveBrush();
	}

	if (CurrentTool)
	{
		CurrentTool->PreviousBrushIndex = CurrentBrushSetIndex;
		CurrentTool->ExitTool();
	}
	CurrentTool = NULL;
	// Leave CurrentToolIndex set so we can restore the active tool on re-opening the landscape editor

	LandscapeList.Empty();
	LandscapeTargetList.Empty();

	// Save UI settings to config file
	UISettings->Save();
	GLandscapeViewMode = ELandscapeViewMode::Normal;
	GLandscapeEditRenderMode = ELandscapeEditRenderMode::None;
	GLandscapeEditModeActive = false;

	CurrentGizmoActor = NULL;

	GEditor->SelectNone(false, true);

	// Clear all GizmoActors if there is no Landscape in World
	bool bIsLandscapeExist = false;
	for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
	{
		bIsLandscapeExist = true;
		break;
	}

	if (!bIsLandscapeExist)
	{
		for (TActorIterator<ALandscapeGizmoActor> It(GetWorld()); It; ++It)
		{
			GetWorld()->DestroyActor(*It, false, false);
		}
	}

	// Redraw one last time to remove any landscape editor stuff from view
	GEditor->RedrawLevelEditingViewports();

	// Call parent implementation
	FEdMode::Exit();
}


void FEdModeLandscape::OnVRHoverUpdate(UViewportInteractor* Interactor, FVector& HoverImpactPoint, bool& bWasHandled)
{
	if (InteractorPainting != nullptr && InteractorPainting == Interactor && IVREditorModule::Get().IsVREditorModeActive())
	{
		UVREditorMode* VREditorMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UVREditorMode::StaticClass() ) );
		if( VREditorMode != nullptr && VREditorMode->IsActive() && Interactor != nullptr && Interactor->GetDraggingMode() == EViewportInteractionDraggingMode::Nothing )
		{
			const UVREditorInteractor* VREditorInteractor = Cast<UVREditorInteractor>(Interactor);

			if (VREditorInteractor != nullptr && !VREditorInteractor->IsHoveringOverPriorityType() && CurrentTool && (CurrentTool->GetSupportedTargetTypes() == ELandscapeToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ELandscapeToolTargetType::Invalid))
			{
				FVector HitLocation;
				FVector LaserPointerStart, LaserPointerEnd;
				if (Interactor->GetLaserPointer( /* Out */ LaserPointerStart, /* Out */ LaserPointerEnd))
				{
					if( LandscapeTrace( LaserPointerStart, LaserPointerEnd, HitLocation ) )
					{
						if (CurrentTool && CurrentTool->IsToolActive())
						{
							CurrentTool->SetExternalModifierPressed(Interactor->IsModifierPressed());
							CurrentTool->MouseMove(nullptr, nullptr, HitLocation.X, HitLocation.Y);
						}

						if (CurrentBrush)
						{
							// Inform the brush of the current location, to update the cursor
							CurrentBrush->MouseMove(HitLocation.X, HitLocation.Y);
						}
					}
				}
			}
		}
	}
}

void FEdModeLandscape::OnPreSaveWorld(uint32 InSaveFlags, const class UWorld* InWorld)
{
	// Avoid doing this during cooking to keep determinism and we don't want to do this on GameWorlds.
	if (!InWorld->IsGameWorld() && !GIsCookerLoadingPackage)
	{
		ULandscapeInfoMap& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(InWorld);
		for (const TPair<FGuid, ULandscapeInfo*>& Pair : LandscapeInfoMap.Map)
		{
			if (const ULandscapeInfo* LandscapeInfo = Pair.Value)
			{
				if (ALandscape* LandscapeActor = LandscapeInfo->LandscapeActor.Get())
				{
					LandscapeActor->OnPreSave();
				}
			}
		}
	}
}

void FEdModeLandscape::OnVRAction(FEditorViewportClient& ViewportClient, UViewportInteractor* Interactor, const struct FViewportActionKeyInput& Action, bool& bOutIsInputCaptured, bool& bWasHandled)
{
	UVREditorMode* VREditorMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UVREditorMode::StaticClass() ) );
	// Never show the traditional Unreal transform widget.  It doesn't work in VR because we don't have hit proxies.
	ViewportClient.EngineShowFlags.SetModeWidgets(false);

	if (VREditorMode != nullptr && VREditorMode->IsActive() && Interactor != nullptr && Interactor->GetDraggingMode() == EViewportInteractionDraggingMode::Nothing)
	{
		if (Action.ActionType == ViewportWorldActionTypes::SelectAndMove)
		{
			const UVREditorInteractor* VREditorInteractor = Cast<UVREditorInteractor>(Interactor);

			// Begin landscape brush
			if (Action.Event == IE_Pressed && !VREditorInteractor->IsHoveringOverUI() && !VREditorInteractor->IsHoveringOverPriorityType() && CurrentTool)
			{
				if (ViewportClient.Viewport != nullptr && ViewportClient.Viewport == ToolActiveViewport)
				{
					CurrentTool->EndTool(&ViewportClient);
					ToolActiveViewport = nullptr;
				}

				if (CurrentTool->GetSupportedTargetTypes() == ELandscapeToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ELandscapeToolTargetType::Invalid)
				{
					FVector HitLocation;
					FVector LaserPointerStart, LaserPointerEnd;
					if (Interactor->GetLaserPointer( /* Out */ LaserPointerStart, /* Out */ LaserPointerEnd))
					{
						if (LandscapeTrace(LaserPointerStart, LaserPointerEnd, HitLocation))
						{
							if (!(CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap && CurrentToolTarget.LayerInfo == NULL))
							{
								CurrentTool->SetExternalModifierPressed(Interactor->IsModifierPressed());
								if( CurrentTool->BeginTool(&ViewportClient, CurrentToolTarget, HitLocation))
								{
									ToolActiveViewport = ViewportClient.Viewport;
								}
							}

							bIsPaintingInVR = true;
							bWasHandled = true;
							bOutIsInputCaptured = false;

							InteractorPainting = Interactor;
						}
					}
				}
			}

			// End landscape brush
			else if (Action.Event == IE_Released)
			{
				if (CurrentTool && ViewportClient.Viewport != nullptr && ViewportClient.Viewport == ToolActiveViewport)
				{
					CurrentTool->EndTool(&ViewportClient);
					ToolActiveViewport = nullptr;
				}

				bIsPaintingInVR = false;
			}
		}
	}

}

/** FEdMode: Called once per frame */
void FEdModeLandscape::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (!IsEditingEnabled())
	{
		return;
	}

	FViewport* const Viewport = ViewportClient->Viewport;

	if (ToolActiveViewport && ToolActiveViewport == Viewport && ensure(CurrentTool) && !bIsPaintingInVR)
	{
		// Require Ctrl or not as per user preference
		const ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		if (!Viewport->KeyState(EKeys::LeftMouseButton) ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && !IsCtrlDown(Viewport)))
		{
			// Don't end the current tool if we are just modifying it
			if (!IsAdjustingBrush(Viewport) && CurrentTool->IsToolActive())
			{
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}
		}
	}

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		const bool bStaleTargetLandscapeInfo = CurrentToolTarget.LandscapeInfo.IsStale();
		const bool bStaleTargetLandscape = CurrentToolTarget.LandscapeInfo.IsValid() && (CurrentToolTarget.LandscapeInfo->GetLandscapeProxy() != nullptr);

		if (bStaleTargetLandscapeInfo || bStaleTargetLandscape)
		{
			UpdateLandscapeList();
		}

		if (CurrentToolTarget.LandscapeInfo.IsValid())
		{
			ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
			
			if (LandscapeProxy == NULL ||
				LandscapeProxy->GetLandscapeMaterial() != CachedLandscapeMaterial)
			{
				UpdateTargetList();
			}
			else
			{
				if (CurrentTool)
				{
					CurrentTool->Tick(ViewportClient, DeltaTime);
				}
			
				if (CurrentBrush)
				{
					CurrentBrush->Tick(ViewportClient, DeltaTime);
				}
			
				if (CurrentBrush != GizmoBrush && CurrentGizmoActor.IsValid() && GizmoBrush && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo))
				{
					GizmoBrush->Tick(ViewportClient, DeltaTime);
				}
			}
		}
	}
}


/** FEdMode: Called when the mouse is moved over the viewport */
bool FEdModeLandscape::MouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 MouseX, int32 MouseY)
{
	// due to mouse capture this should only ever be called on the active viewport
	// if it ever gets called on another viewport the mouse has been released without us picking it up
	if (ToolActiveViewport && ensure(CurrentTool) && !bIsPaintingInVR)
	{
		int32 MouseXDelta = MouseX - InViewportClient->GetCachedMouseX();
		int32 MouseYDelta = MouseY - InViewportClient->GetCachedMouseY();

		if (FMath::Abs(MouseXDelta) > 0 || FMath::Abs(MouseYDelta) > 0)
		{
			const bool bSizeChange = FMath::Abs(MouseXDelta) > FMath::Abs(MouseYDelta) ?
				MouseXDelta > 0 : 
				MouseYDelta < 0; // The way y position is stored here is inverted relative to expected mouse movement to change brush size
			// Are we altering something about the brush?
			FInputChord CompareChord;
			FInputBindingManager::Get().GetUserDefinedChord(FLandscapeEditorCommands::LandscapeContext, TEXT("DragBrushSize"), EMultipleKeyBindingIndex::Primary, CompareChord);
			if (InViewport->KeyState(CompareChord.Key))
			{
				ChangeBrushSize(bSizeChange);
				return true;
			}

			FInputBindingManager::Get().GetUserDefinedChord(FLandscapeEditorCommands::LandscapeContext, TEXT("DragBrushStrength"), EMultipleKeyBindingIndex::Primary, CompareChord);
			if (InViewport->KeyState(CompareChord.Key))
			{
				ChangeBrushStrength(bSizeChange);
				return true;
			}

			FInputBindingManager::Get().GetUserDefinedChord(FLandscapeEditorCommands::LandscapeContext, TEXT("DragBrushFalloff"), EMultipleKeyBindingIndex::Primary, CompareChord);
			if (InViewport->KeyState(CompareChord.Key))
			{
				ChangeBrushFalloff(bSizeChange);
				return true;
			}
		}

		// Require Ctrl or not as per user preference
		const ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		if (ToolActiveViewport != InViewport ||
			!InViewport->KeyState(EKeys::LeftMouseButton) ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && !IsCtrlDown(InViewport)))
		{
			if (CurrentTool->IsToolActive())
			{
				CurrentTool->EndTool(InViewportClient);
			}
			InViewport->CaptureMouse(false);
			ToolActiveViewport = nullptr;
		}
	}

	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->MouseMove(InViewportClient, InViewport, MouseX, MouseY);
			InViewportClient->Invalidate(false, false);
		}
	}
	return Result;
}

bool FEdModeLandscape::GetCursor(EMouseCursor::Type& OutCursor) const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->GetCursor(OutCursor);
		}
	}

	return Result;
}

bool FEdModeLandscape::GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->GetOverrideCursorVisibility(bWantsOverride, bHardwareCursorVisible, bSoftwareCursorVisible);
		}
	}

	return Result;
}

bool FEdModeLandscape::PreConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->PreConvertMouseMovement(InViewportClient);
		}
	}

	return Result;
}

bool FEdModeLandscape::PostConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->PostConvertMouseMovement(InViewportClient);
		}
	}

	return Result;
}

bool FEdModeLandscape::DisallowMouseDeltaTracking() const
{
	// We never want to use the mouse delta tracker while painting
	return (ToolActiveViewport != nullptr);
}

/**
 * Called when the mouse is moved while a window input capture is in effect
 *
 * @param	InViewportClient	Level editor viewport client that captured the mouse input
 * @param	InViewport			Viewport that captured the mouse input
 * @param	InMouseX			New mouse cursor X coordinate
 * @param	InMouseY			New mouse cursor Y coordinate
 *
 * @return	true if input was handled
 */
bool FEdModeLandscape::CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	return MouseMove(ViewportClient, Viewport, MouseX, MouseY);
}

namespace
{
	bool GIsGizmoDragging = false;
}

/** FEdMode: Called when a mouse button is pressed */
bool FEdModeLandscape::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected() && GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo)
	{
		GIsGizmoDragging = true;
		return true;
	}
	return false;
}



/** FEdMode: Called when the a mouse button is released */
bool FEdModeLandscape::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (GIsGizmoDragging)
	{
		GIsGizmoDragging = false;
		return true;
	}
	return false;
}

namespace
{
	bool RayIntersectTriangle(const FVector& Start, const FVector& End, const FVector& A, const FVector& B, const FVector& C, FVector& IntersectPoint)
	{
		const FVector BA = A - B;
		const FVector CB = B - C;
		const FVector TriNormal = BA ^ CB;

		bool bCollide = FMath::SegmentPlaneIntersection(Start, End, FPlane(A, TriNormal), IntersectPoint);
		if (!bCollide)
		{
			return false;
		}

		FVector BaryCentric = FMath::ComputeBaryCentric2D(IntersectPoint, A, B, C);
		if (BaryCentric.X > 0.0f && BaryCentric.Y > 0.0f && BaryCentric.Z > 0.0f)
		{
			return true;
		}
		return false;
	}
};

/** Trace under the mouse cursor and return the landscape hit and the hit location (in landscape quad space) */
bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, float& OutHitX, float& OutHitY)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return LandscapeMouseTrace(ViewportClient, MouseX, MouseY, OutHitX, OutHitY);
}

bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, FVector& OutHitLocation)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return LandscapeMouseTrace(ViewportClient, MouseX, MouseY, OutHitLocation);
}

/** Trace under the specified coordinates and return the landscape hit and the hit location (in landscape quad space) */
bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, float& OutHitX, float& OutHitY)
{
	FVector HitLocation;
	bool bResult = LandscapeMouseTrace(ViewportClient, MouseX, MouseY, HitLocation);
	OutHitX = HitLocation.X;
	OutHitY = HitLocation.Y;
	return bResult;
}

bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, FVector& OutHitLocation)
{
	// Cache a copy of the world pointer	
	UWorld* World = ViewportClient->GetWorld();

	// Compute a world space ray from the screen space mouse coordinates
	FSceneViewFamilyContext ViewFamily(FSceneViewFamilyContext::ConstructionValues(ViewportClient->Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));

	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, ViewportClient, MouseX, MouseY);
	FVector MouseViewportRayDirection = MouseViewportRay.GetDirection();

	FVector Start = MouseViewportRay.GetOrigin();
	FVector End = Start + WORLD_MAX * MouseViewportRayDirection;
	if (ViewportClient->IsOrtho())
	{
		Start -= WORLD_MAX * MouseViewportRayDirection;
	}

	return LandscapeTrace(Start, End, OutHitLocation);
}

bool FEdModeLandscape::LandscapeTrace(const FVector& InRayOrigin, const FVector& InRayEnd, FVector& OutHitLocation)
{
	FVector Start = InRayOrigin;
	FVector End = InRayEnd;

	// Cache a copy of the world pointer
	UWorld* World = GetWorld();

	TArray<FHitResult> Results;
	// Each landscape component has 2 collision shapes, 1 of them is specific to landscape editor
	// Trace only ECC_Visibility channel, so we do hit only Editor specific shape
	World->LineTraceMultiByObjectType(Results, Start, End, FCollisionObjectQueryParams(ECollisionChannel::ECC_Visibility), FCollisionQueryParams(SCENE_QUERY_STAT(LandscapeTrace), true));

	for (int32 i = 0; i < Results.Num(); i++)
	{
		const FHitResult& Hit = Results[i];
		ULandscapeHeightfieldCollisionComponent* CollisionComponent = Cast<ULandscapeHeightfieldCollisionComponent>(Hit.Component.Get());
		if (CollisionComponent)
		{
			ALandscapeProxy* HitLandscape = CollisionComponent->GetLandscapeProxy();
			if (HitLandscape &&
				CurrentToolTarget.LandscapeInfo.IsValid() &&
				CurrentToolTarget.LandscapeInfo->LandscapeGuid == HitLandscape->GetLandscapeGuid())
			{
				OutHitLocation = HitLandscape->LandscapeActorToWorld().InverseTransformPosition(Hit.Location);
				return true;
			}
		}
	}

	// For Add Landscape Component Mode
	if (CurrentTool->GetToolName() == FName("AddComponent") &&
		CurrentToolTarget.LandscapeInfo.IsValid())
	{
		bool bCollided = false;
		FVector IntersectPoint;
		LandscapeRenderAddCollision = NULL;
		// Need to optimize collision for AddLandscapeComponent...?
		for (auto& XYToAddCollisionPair : CurrentToolTarget.LandscapeInfo->XYtoAddCollisionMap)
		{
			FLandscapeAddCollision& AddCollision = XYToAddCollisionPair.Value;
			// Triangle 1
			bCollided = RayIntersectTriangle(Start, End, AddCollision.Corners[0], AddCollision.Corners[3], AddCollision.Corners[1], IntersectPoint);
			if (bCollided)
			{
				LandscapeRenderAddCollision = &AddCollision;
				break;
			}
			// Triangle 2
			bCollided = RayIntersectTriangle(Start, End, AddCollision.Corners[0], AddCollision.Corners[2], AddCollision.Corners[3], IntersectPoint);
			if (bCollided)
			{
				LandscapeRenderAddCollision = &AddCollision;
				break;
			}
		}

		if (bCollided &&
			CurrentToolTarget.LandscapeInfo.IsValid())
		{
			ALandscapeProxy* Proxy = CurrentToolTarget.LandscapeInfo.Get()->GetCurrentLevelLandscapeProxy(true);
			if (Proxy)
			{
				OutHitLocation = Proxy->LandscapeActorToWorld().InverseTransformPosition(IntersectPoint);
				return true;
			}
		}
	}

	return false;
}

bool FEdModeLandscape::LandscapePlaneTrace(FEditorViewportClient* ViewportClient, const FPlane& Plane, FVector& OutHitLocation)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return LandscapePlaneTrace(ViewportClient, MouseX, MouseY, Plane, OutHitLocation);
}

bool FEdModeLandscape::LandscapePlaneTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, const FPlane& Plane, FVector& OutHitLocation)
{
	// Compute a world space ray from the screen space mouse coordinates
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, ViewportClient, MouseX, MouseY);

	FVector Start = MouseViewportRay.GetOrigin();
	FVector End = Start + WORLD_MAX * MouseViewportRay.GetDirection();

	OutHitLocation = FMath::LinePlaneIntersection(Start, End, Plane);

	return true;
}

namespace
{
	const int32 SelectionSizeThresh = 2 * 256 * 256;
	FORCEINLINE bool IsSlowSelect(ULandscapeInfo* LandscapeInfo)
	{
		if (LandscapeInfo)
		{
			int32 MinX = MAX_int32, MinY = MAX_int32, MaxX = MIN_int32, MaxY = MIN_int32;
			LandscapeInfo->GetSelectedExtent(MinX, MinY, MaxX, MaxY);
			return (MinX != MAX_int32 && ((MaxX - MinX) * (MaxY - MinY)));
		}
		return false;
	}
};

EEditAction::Type FEdModeLandscape::GetActionEditDuplicate()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditDuplicate();
		}
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditDelete()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditDelete();
		}

		if (Result == EEditAction::Skip)
		{
			// Prevent deleting Gizmo during LandscapeEdMode
			if (CurrentGizmoActor.IsValid())
			{
				if (CurrentGizmoActor->IsSelected())
				{
					if (GEditor->GetSelectedActors()->Num() > 1)
					{
						GEditor->GetSelectedActors()->Deselect(CurrentGizmoActor.Get());
						Result = EEditAction::Skip;
					}
					else
					{
						Result = EEditAction::Halt;
					}
				}
			}
		}
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditCut()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditCut();
		}
	}

	if (Result == EEditAction::Skip)
	{
		// Special case: we don't want the 'normal' cut operation to be possible at all while in this mode, 
		// so we need to stop evaluating the others in-case they come back as true.
		return EEditAction::Halt;
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditCopy()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditCopy();
		}

		if (Result == EEditAction::Skip)
		{
			if (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo || GLandscapeEditRenderMode & (ELandscapeEditRenderMode::Select))
			{
				if (CurrentGizmoActor.IsValid() && GizmoBrush && CurrentGizmoActor->TargetLandscapeInfo)
				{
					Result = EEditAction::Process;
				}
			}
		}
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditPaste()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditPaste();
		}

		if (Result == EEditAction::Skip)
		{
			if (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo || GLandscapeEditRenderMode & (ELandscapeEditRenderMode::Select))
			{
				if (CurrentGizmoActor.IsValid() && GizmoBrush && CurrentGizmoActor->TargetLandscapeInfo)
				{
					Result = EEditAction::Process;
				}
			}
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditDuplicate()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditDuplicate();
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditDelete()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditDelete();
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditCut()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditCut();
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditCopy()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditCopy();
		}

		if (!Result)
		{
			ALandscapeBlueprintBrushBase* CurrentlySelectedBPBrush = nullptr;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				CurrentlySelectedBPBrush = Cast<ALandscapeBlueprintBrushBase>(*It);
				if (CurrentlySelectedBPBrush)
				{
					break;
				}
			}

			if (!CurrentlySelectedBPBrush)
			{
				bool IsSlowTask = IsSlowSelect(CurrentGizmoActor->TargetLandscapeInfo);
				if (IsSlowTask)
				{
					GWarn->BeginSlowTask(LOCTEXT("BeginFitGizmoAndCopy", "Fit Gizmo to Selected Region and Copy Data..."), true);
				}

				FScopedTransaction Transaction(LOCTEXT("LandscapeGizmo_Copy", "Copy landscape data to Gizmo"));
				CurrentGizmoActor->Modify();
				CurrentGizmoActor->FitToSelection();
				CopyDataToGizmo();
				SetCurrentTool(FName("CopyPaste"));

				if (IsSlowTask)
				{
					GWarn->EndSlowTask();
				}

				Result = true;
			}			
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditPaste()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	
	FLandscapeLayer* SplinesLayer = nullptr;
	if (CurrentTool == (FLandscapeTool*)SplinesTool)
	{
		ALandscape* Landscape = GetLandscape();
		SplinesLayer = Landscape ? Landscape->GetLandscapeSplinesReservedLayer() : nullptr;
	}
	FText Reason;
	if (!CanEditLayer(&Reason, SplinesLayer))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Reason);
		return Result;
	}
	
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditPaste();
		}

		if (!Result)
		{
			ALandscapeBlueprintBrushBase* CurrentlySelectedBPBrush = nullptr;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				CurrentlySelectedBPBrush = Cast<ALandscapeBlueprintBrushBase>(*It);
				if (CurrentlySelectedBPBrush)
				{
					break;
				}
			}

			if (!CurrentlySelectedBPBrush)
			{
				bool IsSlowTask = IsSlowSelect(CurrentGizmoActor->TargetLandscapeInfo);
				if (IsSlowTask)
				{
					GWarn->BeginSlowTask(LOCTEXT("BeginPasteGizmoDataTask", "Paste Gizmo Data..."), true);
				}
				PasteDataFromGizmo();
				SetCurrentTool(FName("CopyPaste"));
				if (IsSlowTask)
				{
					GWarn->EndSlowTask();
				}

				Result = true;
			}
		}
	}

	return Result;
}

bool FEdModeLandscape::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return false;
	}

	// Override Click Input for Splines Tool
	if (CurrentTool && CurrentTool->HandleClick(HitProxy, Click))
	{
		return true;
	}

	return false;
}

bool FEdModeLandscape::IsAdjustingBrush(FViewport* InViewport) const
{
	FInputChord CompareChord;
	FInputBindingManager::Get().GetUserDefinedChord(FLandscapeEditorCommands::LandscapeContext, TEXT("DragBrushSize"), EMultipleKeyBindingIndex::Primary, CompareChord);
	if (InViewport->KeyState(CompareChord.Key))
	{
		return true;
	}
	FInputBindingManager::Get().GetUserDefinedChord(FLandscapeEditorCommands::LandscapeContext, TEXT("DragBrushFalloff"), EMultipleKeyBindingIndex::Primary, CompareChord);
	if (InViewport->KeyState(CompareChord.Key))
	{
		return true;
	}
	FInputBindingManager::Get().GetUserDefinedChord(FLandscapeEditorCommands::LandscapeContext, TEXT("DragBrushStrength"), EMultipleKeyBindingIndex::Primary, CompareChord);
	if (InViewport->KeyState(CompareChord.Key))
	{
		return true;
	}
	return false;
}

void FEdModeLandscape::ChangeBrushSize(bool bIncrease)
{
	UISettings->Modify();
	if (CurrentBrush->GetBrushType() == ELandscapeBrushType::Component)
	{
		int32 Radius = UISettings->BrushComponentSize;
		if (bIncrease)
		{
			++Radius;
		}
		else
		{
			--Radius;
		}
		Radius = (int32)FMath::Clamp(Radius, 1, 64);
		UISettings->BrushComponentSize = Radius;
	}
	else
	{
		float Radius = UISettings->BrushRadius;
		const float SliderMin = 10.0f;
		const float SliderMax = 8192.0f;
		float Diff = 0.05f; //6.0f / SliderMax;
		if (!bIncrease)
		{
			Diff = -Diff;
		}

		float NewValue = Radius * (1.0f + Diff);

		if (bIncrease)
		{
			NewValue = FMath::Max(NewValue, Radius + 1.0f);
		}
		else
		{
			NewValue = FMath::Min(NewValue, Radius - 1.0f);
		}

		NewValue = (int32)FMath::Clamp(NewValue, SliderMin, SliderMax);
		UISettings->BrushRadius = NewValue;
	}
}


void FEdModeLandscape::ChangeBrushFalloff(bool bIncrease)
{
	UISettings->Modify();
	float Falloff = UISettings->BrushFalloff;
	const float SliderMin = 0.0f;
	const float SliderMax = 1.0f;
	float Diff = 0.05f; 
	if (!bIncrease)
	{
		Diff = -Diff;
	}

	float NewValue = Falloff * (1.0f + Diff);

	if (bIncrease)
	{
		NewValue = FMath::Max(NewValue, Falloff + 0.05f);
	}
	else
	{
		NewValue = FMath::Min(NewValue, Falloff - 0.05f);
	}

	NewValue = FMath::Clamp(NewValue, SliderMin, SliderMax);
	UISettings->BrushFalloff = NewValue;
}


void FEdModeLandscape::ChangeBrushStrength(bool bIncrease)
{
	UISettings->Modify();
	float Strength = UISettings->ToolStrength;
	const float SliderMin = 0.01f;
	const float SliderMax = 10.0f;
	float Diff = 0.05f; //6.0f / SliderMax;
	if (!bIncrease)
	{
		Diff = -Diff;
	}

	float NewValue = Strength * (1.0f + Diff);

	if (bIncrease)
	{
		NewValue = FMath::Max(NewValue, Strength + 0.05f);
	}
	else
	{
		NewValue = FMath::Min(NewValue, Strength - 0.05f);
	}

	NewValue = FMath::Clamp(NewValue, SliderMin, SliderMax);
	UISettings->ToolStrength = NewValue;
}


/** FEdMode: Called when a key is pressed */
bool FEdModeLandscape::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (!ErrorReasonOnMouseUp.IsEmpty() && Key == EKeys::LeftMouseButton && Event == IE_Released)
	{
		FMessageDialog::Open(EAppMsgType::Ok, ErrorReasonOnMouseUp);
		ErrorReasonOnMouseUp = FText::GetEmpty();
		return false;
	}


	if(IsAdjustingBrush(Viewport))
	{
		ToolActiveViewport = Viewport;
		return false; // false to let FEditorViewportClient.InputKey start mouse tracking and enable InputDelta() so we can use it
	}

	if (Event != IE_Released)
	{
		ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");

		if (LandscapeEditorModule.GetLandscapeLevelViewportCommandList()->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
		{
			return true;
		}
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		if (Key == EKeys::LeftMouseButton)
		{
			// Press mouse button
			if (Event == IE_Pressed && !IsAltDown(Viewport))
			{
				// See if we clicked on a new landscape handle..
				int32 HitX = Viewport->GetMouseX();
				int32 HitY = Viewport->GetMouseY();
				HHitProxy*	HitProxy = Viewport->GetHitProxy(HitX, HitY);
				if (HitProxy)
				{
					if (HitProxy->IsA(HNewLandscapeGrabHandleProxy::StaticGetType()))
					{
						HNewLandscapeGrabHandleProxy* EdgeProxy = (HNewLandscapeGrabHandleProxy*)HitProxy;
						DraggingEdge = EdgeProxy->Edge;
						DraggingEdge_Remainder = 0;

						return false; // false to let FEditorViewportClient.InputKey start mouse tracking and enable InputDelta() so we can use it
					}
				}
			}
			else if (Event == IE_Released)
			{
				if (DraggingEdge)
				{
					DraggingEdge = ELandscapeEdge::None;
					DraggingEdge_Remainder = 0;

					return false; // false to let FEditorViewportClient.InputKey end mouse tracking
				}
			}
		}
	}
	else
	{
		// Override Key Input for Selection Brush
		if (CurrentBrush)
		{
			TOptional<bool> BrushKeyOverride = CurrentBrush->InputKey(ViewportClient, Viewport, Key, Event);
			if (BrushKeyOverride.IsSet())
			{
				return BrushKeyOverride.GetValue();
			}
		}

		if (CurrentTool && CurrentTool->InputKey(ViewportClient, Viewport, Key, Event) == true)
		{
			return true;
		}

		// Require Ctrl or not as per user preference
		ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		// HACK - Splines tool has not yet been updated to support not using ctrl
		if (CurrentBrush->GetBrushType() == ELandscapeBrushType::Splines)
		{
			LandscapeEditorControlType = ELandscapeFoliageEditorControlType::RequireCtrl;
		}

		// Special case to handle where user paint with Left Click then pressing a moving camera input, we do not want to process them so as long as the tool is active ignore other input
		if (CurrentTool != nullptr && CurrentTool->IsToolActive())
		{
			return true;
		}

		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
		{
			// When debugging it's possible to miss the "mouse released" event, if we get a "mouse pressed" event when we think it's already pressed then treat it as release first
			if (ToolActiveViewport)
			{
				CurrentTool->EndTool(ViewportClient); //-V595
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}

			// Only activate tool if we're not already moving the camera and we're not trying to drag a transform widget
			// Not using "if (!ViewportClient->IsMovingCamera())" because it's wrong in ortho viewports :D
			bool bMovingCamera = Viewport->KeyState(EKeys::MiddleMouseButton) || Viewport->KeyState(EKeys::RightMouseButton) || IsAltDown(Viewport);

			if ((Viewport->IsPenActive() && Viewport->GetTabletPressure() > 0.0f) ||
				(!bMovingCamera && ViewportClient->GetCurrentWidgetAxis() == EAxisList::None &&
					((LandscapeEditorControlType == ELandscapeFoliageEditorControlType::IgnoreCtrl) ||
					 (LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl   && IsCtrlDown(Viewport)) ||
					 (LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireNoCtrl && !IsCtrlDown(Viewport)))))
			{
				if (CurrentTool && (CurrentTool->GetSupportedTargetTypes() == ELandscapeToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ELandscapeToolTargetType::Invalid))
				{
					FVector HitLocation;
					if (LandscapeMouseTrace(ViewportClient, HitLocation))
					{
						if (!CanEditLayer(&ErrorReasonOnMouseUp))
						{
							return true;
						}

						Viewport->CaptureMouse(true);

						if (CurrentTool->CanToolBeActivated())
						{
							bool bToolActive = CurrentTool->BeginTool(ViewportClient, CurrentToolTarget, HitLocation);
							if (bToolActive)
							{
								ToolActiveViewport = Viewport;
							}
							else
							{
								ToolActiveViewport = nullptr;
								Viewport->CaptureMouse(false);
							}
							ViewportClient->Invalidate(false, false);
							return bToolActive;
						}
					}
				}
				return true;
			}
		}

		if (Key == EKeys::LeftMouseButton ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && (Key == EKeys::LeftControl || Key == EKeys::RightControl)))
		{
			if (Event == IE_Released && CurrentTool && CurrentTool->IsToolActive() && ToolActiveViewport)
			{
				//Set the cursor position to that of the slate cursor so it wont snap back
				Viewport->SetPreCaptureMousePosFromSlateCursor();
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
				return true;
			}
		}

		// Prev tool
		if (Event == IE_Pressed && Key == EKeys::Comma)
		{
			if (CurrentTool && CurrentTool->IsToolActive() && ToolActiveViewport)
			{
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}

			int32 OldToolIndex = CurrentToolMode->ValidTools.Find(CurrentTool->GetToolName());
			int32 NewToolIndex = FMath::Max(OldToolIndex - 1, 0);
			SetCurrentTool(CurrentToolMode->ValidTools[NewToolIndex]);

			return true;
		}

		// Next tool
		if (Event == IE_Pressed && Key == EKeys::Period)
		{
			if (CurrentTool && ToolActiveViewport)
			{
				CurrentTool->EndTool(ViewportClient);
				Viewport->CaptureMouse(false);
				ToolActiveViewport = nullptr;
			}

			int32 OldToolIndex = CurrentToolMode->ValidTools.Find(CurrentTool->GetToolName());
			int32 NewToolIndex = FMath::Min(OldToolIndex + 1, CurrentToolMode->ValidTools.Num() - 1);
			SetCurrentTool(CurrentToolMode->ValidTools[NewToolIndex]);

			return true;
		}
	}

	return false;
}

/** FEdMode: Called when mouse drag input is applied */
bool FEdModeLandscape::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
		{
			FVector DeltaScale = InScale;
			DeltaScale.X = DeltaScale.Y = (FMath::Abs(InScale.X) > FMath::Abs(InScale.Y)) ? InScale.X : InScale.Y;

			UISettings->Modify();
			UISettings->NewLandscape_Location += InDrag;
			UISettings->NewLandscape_Rotation += InRot;
			UISettings->NewLandscape_Scale += DeltaScale;

			return true;
		}
		else if (DraggingEdge != ELandscapeEdge::None)
		{
			FVector HitLocation;
			LandscapePlaneTrace(InViewportClient, FPlane(UISettings->NewLandscape_Location, FVector(0, 0, 1)), HitLocation);

			FTransform Transform(UISettings->NewLandscape_Rotation, UISettings->NewLandscape_Location, UISettings->NewLandscape_Scale * UISettings->NewLandscape_QuadsPerSection * UISettings->NewLandscape_SectionsPerComponent);
			HitLocation = Transform.InverseTransformPosition(HitLocation);

			UISettings->Modify();
			switch (DraggingEdge)
			{
			case ELandscapeEdge::X_Negative:
			case ELandscapeEdge::X_Negative_Y_Negative:
			case ELandscapeEdge::X_Negative_Y_Positive:
			{
				const int32 InitialComponentCountX = UISettings->NewLandscape_ComponentCount.X;
				const int32 Delta = FMath::RoundToInt(HitLocation.X + (float)InitialComponentCountX / 2);
				UISettings->NewLandscape_ComponentCount.X = InitialComponentCountX - Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.X - InitialComponentCountX;
				UISettings->NewLandscape_Location -= Transform.TransformVector(FVector(((float)ActualDelta / 2), 0, 0));
			}
				break;
			case ELandscapeEdge::X_Positive:
			case ELandscapeEdge::X_Positive_Y_Negative:
			case ELandscapeEdge::X_Positive_Y_Positive:
			{
				const int32 InitialComponentCountX = UISettings->NewLandscape_ComponentCount.X;
				int32 Delta = FMath::RoundToInt(HitLocation.X - (float)InitialComponentCountX / 2);
				UISettings->NewLandscape_ComponentCount.X = InitialComponentCountX + Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.X - InitialComponentCountX;
				UISettings->NewLandscape_Location += Transform.TransformVector(FVector(((float)ActualDelta / 2), 0, 0));
			}
				break;
			case  ELandscapeEdge::Y_Negative:
			case  ELandscapeEdge::Y_Positive:
				break;
			}

			switch (DraggingEdge)
			{
			case ELandscapeEdge::Y_Negative:
			case ELandscapeEdge::X_Negative_Y_Negative:
			case ELandscapeEdge::X_Positive_Y_Negative:
			{
				const int32 InitialComponentCountY = UISettings->NewLandscape_ComponentCount.Y;
				int32 Delta = FMath::RoundToInt(HitLocation.Y + (float)InitialComponentCountY / 2);
				UISettings->NewLandscape_ComponentCount.Y = InitialComponentCountY - Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.Y - InitialComponentCountY;
				UISettings->NewLandscape_Location -= Transform.TransformVector(FVector(0, (float)ActualDelta / 2, 0));
			}
				break;
			case ELandscapeEdge::Y_Positive:
			case ELandscapeEdge::X_Negative_Y_Positive:
			case ELandscapeEdge::X_Positive_Y_Positive:
			{
				const int32 InitialComponentCountY = UISettings->NewLandscape_ComponentCount.Y;
				int32 Delta = FMath::RoundToInt(HitLocation.Y - (float)InitialComponentCountY / 2);
				UISettings->NewLandscape_ComponentCount.Y = InitialComponentCountY + Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.Y - InitialComponentCountY;
				UISettings->NewLandscape_Location += Transform.TransformVector(FVector(0, (float)ActualDelta / 2, 0));
			}
				break;
			case  ELandscapeEdge::X_Negative:
			case  ELandscapeEdge::X_Positive:
				break;
			}

			return true;
		}
	}

	if (CurrentTool && CurrentTool->InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale))
	{
		return true;
	}

	return false;
}

void FEdModeLandscape::SetCurrentToolMode(FName ToolModeName, bool bRestoreCurrentTool /*= true*/)
{
	if (CurrentToolMode == NULL || ToolModeName != CurrentToolMode->ToolModeName)
	{
		for (int32 i = 0; i < LandscapeToolModes.Num(); ++i)
		{
			if (LandscapeToolModes[i].ToolModeName == ToolModeName)
			{
				CurrentToolMode = &LandscapeToolModes[i];
				if (bRestoreCurrentTool)
				{
					if (CurrentToolMode->CurrentToolName == NAME_None)
					{
						CurrentToolMode->CurrentToolName = CurrentToolMode->ValidTools[0];
						CurrentToolMode->CurrentTargetLayerName = NAME_None;
					}
					SetCurrentTool(CurrentToolMode->CurrentToolName, CurrentToolMode->CurrentTargetLayerName);
				}
				break;
			}
		}
	}
}

void FEdModeLandscape::SetCurrentTool(FName ToolName, FName TargetLayerName)
{
	// Several tools have identically named versions for sculpting and painting
	// Prefer the one with the same target type as the current mode

	int32 BackupToolIndex = INDEX_NONE;
	int32 ToolIndex = INDEX_NONE;
	for (int32 i = 0; i < LandscapeTools.Num(); ++i)
	{
		FLandscapeTool* Tool = LandscapeTools[i].Get();
		if (ToolName == Tool->GetToolName())
		{
			if ((Tool->GetSupportedTargetTypes() & CurrentToolMode->SupportedTargetTypes) != 0)
			{
				ToolIndex = i;
				break;
			}
			else if (BackupToolIndex == INDEX_NONE)
			{
				BackupToolIndex = i;
			}
		}
	}

	if (ToolIndex == INDEX_NONE)
	{
		checkf(BackupToolIndex != INDEX_NONE, TEXT("Tool '%s' not found, please check name is correct!"), *ToolName.ToString());
		ToolIndex = BackupToolIndex;
	}
	check(ToolIndex != INDEX_NONE);

	SetCurrentTool(ToolIndex, TargetLayerName);
}

void FEdModeLandscape::SetCurrentTargetLayer(FName TargetLayerName, TWeakObjectPtr<ULandscapeLayerInfoObject> LayerInfo)
{
	if (CurrentToolMode)
	{
		// Cache current Layer Name so we can set it back when switching between Modes
		CurrentToolMode->CurrentTargetLayerName = TargetLayerName;
	}
	CurrentToolTarget.LayerName = TargetLayerName;
	CurrentToolTarget.LayerInfo = LayerInfo;
}

void FEdModeLandscape::SetCurrentTool(int32 ToolIndex, FName TargetLayerName)
{
	if (CurrentTool)
	{
		CurrentTool->PreviousBrushIndex = CurrentBrushSetIndex;
		CurrentTool->ExitTool();
		CurrentTool = nullptr;
	}
	CurrentToolIndex = LandscapeTools.IsValidIndex(ToolIndex) ? ToolIndex : 0;
	FLandscapeTool* NewTool = LandscapeTools[CurrentToolIndex].Get();
	if (!CurrentToolMode->ValidTools.Contains(NewTool->GetToolName()))
	{
		// if tool isn't valid for this mode then automatically switch modes
		// this mostly happens with shortcut keys
		bool bFoundValidMode = false;
		for (int32 i = 0; i < LandscapeToolModes.Num(); ++i)
		{
			if (LandscapeToolModes[i].ValidTools.Contains(NewTool->GetToolName()))
			{
				SetCurrentToolMode(LandscapeToolModes[i].ToolModeName, false);
				bFoundValidMode = true;
				break;
			}
		}
		
		// default to first valid tool of current mode
		if (!bFoundValidMode)
		{		
			SetCurrentTool(CurrentToolMode->ValidTools[0]);
			return;
		}
	}

	// Assign 
	CurrentTool = NewTool;

	// Set target type appropriate for tool
	if (CurrentTool->GetSupportedTargetTypes() == ELandscapeToolTargetTypeMask::NA)
	{
		CurrentToolTarget.TargetType = ELandscapeToolTargetType::Invalid;
		SetCurrentTargetLayer(NAME_None, nullptr);
	}
	else
	{
		const uint8 TargetTypeMask = CurrentToolMode->SupportedTargetTypes & CurrentTool->GetSupportedTargetTypes();
		checkSlow(TargetTypeMask != 0);

		if ((TargetTypeMask & ELandscapeToolTargetTypeMask::FromType(CurrentToolTarget.TargetType)) == 0)
		{
			auto filter = [TargetTypeMask, TargetLayerName](const TSharedRef<FLandscapeTargetListInfo>& Target){ return (TargetTypeMask & ELandscapeToolTargetTypeMask::FromType(Target->TargetType)) != 0 && (TargetLayerName == NAME_None || TargetLayerName == Target->LayerName); };
			const TSharedRef<FLandscapeTargetListInfo>* Target = LandscapeTargetList.FindByPredicate(filter);
			if (Target != nullptr)
			{
				check(CurrentToolTarget.LandscapeInfo == (*Target)->LandscapeInfo);
				CurrentToolTarget.TargetType = (*Target)->TargetType;
				SetCurrentTargetLayer((*Target)->LayerName, (*Target)->LayerInfoObj);
			}
			else // can happen with for example paint tools if there are no paint layers defined
			{
				CurrentToolTarget.TargetType = ELandscapeToolTargetType::Invalid;
				SetCurrentTargetLayer(NAME_None, nullptr);
			}
		}
	}

	CurrentTool->EnterTool();

	CurrentTool->SetEditRenderType();
	//bool MaskEnabled = CurrentTool->SupportsMask() && CurrentToolTarget.LandscapeInfo.IsValid() && CurrentToolTarget.LandscapeInfo->SelectedRegion.Num();

	CurrentToolMode->CurrentToolName = CurrentTool->GetToolName();

	// Set Brush
	if (!LandscapeBrushSets.IsValidIndex(CurrentTool->PreviousBrushIndex))
	{
		SetCurrentBrushSet(CurrentTool->ValidBrushes[0]);
	}
	else
	{
		SetCurrentBrushSet(CurrentTool->PreviousBrushIndex);
	}

	// Update GizmoActor Landscape Target (is this necessary?)
	if (CurrentGizmoActor.IsValid() && CurrentToolTarget.LandscapeInfo.IsValid())
	{
		CurrentGizmoActor->SetTargetLandscape(CurrentToolTarget.LandscapeInfo.Get());
	}

	if (Toolkit.IsValid())
	{
		StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->NotifyToolChanged();
	}

	GEditor->RedrawLevelEditingViewports();
}

void FEdModeLandscape::RefreshDetailPanel()
{
	if (Toolkit.IsValid())
	{
		StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->RefreshDetailPanel();
	}
}

void FEdModeLandscape::SetCurrentBrushSet(FName BrushSetName)
{
	for (int32 BrushIndex = 0; BrushIndex < LandscapeBrushSets.Num(); BrushIndex++)
	{
		if (BrushSetName == LandscapeBrushSets[BrushIndex].BrushSetName)
		{
			SetCurrentBrushSet(BrushIndex);
			return;
		}
	}
}

void FEdModeLandscape::SetCurrentBrushSet(int32 BrushSetIndex)
{
	if (CurrentBrushSetIndex != BrushSetIndex)
	{
		LandscapeBrushSets[CurrentBrushSetIndex].PreviousBrushIndex = LandscapeBrushSets[CurrentBrushSetIndex].Brushes.IndexOfByKey(CurrentBrush);

		CurrentBrushSetIndex = BrushSetIndex;
		if (CurrentTool)
		{
			CurrentTool->PreviousBrushIndex = BrushSetIndex;
		}

		SetCurrentBrush(LandscapeBrushSets[CurrentBrushSetIndex].PreviousBrushIndex);
	}
}

void FEdModeLandscape::SetCurrentBrush(FName BrushName)
{
	for (int32 BrushIndex = 0; BrushIndex < LandscapeBrushSets[CurrentBrushSetIndex].Brushes.Num(); BrushIndex++)
	{
		if (BrushName == LandscapeBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex]->GetBrushName())
		{
			SetCurrentBrush(BrushIndex);
			return;
		}
	}
}

void FEdModeLandscape::SetCurrentBrush(int32 BrushIndex)
{
	if (CurrentBrush != LandscapeBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex])
	{
		CurrentBrush->LeaveBrush();
		CurrentBrush = LandscapeBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex];
		CurrentBrush->EnterBrush();

		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->NotifyBrushChanged();
		}
	}
}

const TArray<ALandscapeBlueprintBrushBase*>& FEdModeLandscape::GetBrushList() const
{
	return BrushList;
}

const TArray<TSharedRef<FLandscapeTargetListInfo>>& FEdModeLandscape::GetTargetList() const
{
	return LandscapeTargetList;
}

const TArray<FLandscapeListInfo>& FEdModeLandscape::GetLandscapeList()
{
	return LandscapeList;
}

void FEdModeLandscape::AddLayerInfo(ULandscapeLayerInfoObject* LayerInfo)
{
	if (CurrentToolTarget.LandscapeInfo.IsValid() && CurrentToolTarget.LandscapeInfo->GetLayerInfoIndex(LayerInfo) == INDEX_NONE)
	{
		ALandscapeProxy* Proxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		CurrentToolTarget.LandscapeInfo->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, Proxy));
		UpdateTargetList();
	}
}

int32 FEdModeLandscape::UpdateLandscapeList()
{
	LandscapeList.Empty();

	if (!CurrentGizmoActor.IsValid())
	{
		ALandscapeGizmoActiveActor* GizmoActor = NULL;
		for (TActorIterator<ALandscapeGizmoActiveActor> It(GetWorld()); It; ++It)
		{
			GizmoActor = *It;
			break;
		}
	}

	int32 CurrentIndex = INDEX_NONE;
	UWorld* World = GetWorld();
	
	if (World)
	{
		int32 Index = 0;
		auto& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(World);

		for (auto It = LandscapeInfoMap.Map.CreateIterator(); It; ++It)
		{
			ULandscapeInfo* LandscapeInfo = It.Value();
			if (LandscapeInfo && !LandscapeInfo->IsPendingKill())
			{
				if (ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get())
				{
					Landscape->RegisterLandscapeEdMode(this);
				}

				ALandscapeProxy* LandscapeProxy = LandscapeInfo->GetLandscapeProxy();
				if (LandscapeProxy)
				{
					if (CurrentToolTarget.LandscapeInfo == LandscapeInfo)
					{
						CurrentIndex = Index;

						// Update GizmoActor Landscape Target (is this necessary?)
						if (CurrentGizmoActor.IsValid())
						{
							CurrentGizmoActor->SetTargetLandscape(LandscapeInfo);
						}
					}

					int32 MinX, MinY, MaxX, MaxY;
					int32 Width = 0, Height = 0;
					if (LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
					{
						Width = MaxX - MinX + 1;
						Height = MaxY - MinY + 1;
					}

					LandscapeList.Add(FLandscapeListInfo(*LandscapeProxy->GetName(), LandscapeInfo,
						LandscapeInfo->ComponentSizeQuads, LandscapeInfo->ComponentNumSubsections, Width, Height));
					Index++;
				}
			}
		}
	}

	if (CurrentIndex == INDEX_NONE)
	{
		if (LandscapeList.Num() > 0)
		{
			FName CurrentToolName = CurrentTool != nullptr ? CurrentTool->GetToolName() : FName();
			SetLandscapeInfo(LandscapeList[0].Info);
			CurrentIndex = 0;

			SetCurrentLayer(0);

			// Init UI to saved value
			ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();

			if (LandscapeProxy != nullptr)
			{
				UISettings->TargetDisplayOrder = LandscapeProxy->TargetDisplayOrder;
			}

			UpdateTargetList();
			UpdateShownLayerList();
						
			if (!CurrentToolName.IsNone())
			{
				SetCurrentTool(CurrentToolName);
			}
		}
		else
		{
			// no landscape, switch to "new landscape" tool
			SetLandscapeInfo(nullptr);
			UpdateTargetList();
			SetCurrentToolMode("ToolMode_Manage", false);
			SetCurrentTool("NewLandscape");
		}
	}

	if (!CanEditCurrentTarget())
	{
		SetCurrentToolMode("ToolMode_Manage", false);
		SetCurrentTool("NewLandscape");
	}

	return CurrentIndex;
}

void FEdModeLandscape::SetTargetLandscape(const TWeakObjectPtr<ULandscapeInfo>& InLandscapeInfo)
{
	if ((CurrentToolTarget.LandscapeInfo == InLandscapeInfo) || !InLandscapeInfo.IsValid())
	{
		return;
	}

	// Unregister from old one
	if (CurrentToolTarget.LandscapeInfo.IsValid())
	{
		ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		LandscapeProxy->OnMaterialChangedDelegate().RemoveAll(this);
		if (ALandscape* Landscape = GetLandscape())
		{
			Landscape->OnBlueprintBrushChangedDelegate().RemoveAll(this);
		}
	}

	SetLandscapeInfo(InLandscapeInfo.Get());
	UpdateTargetList();
	// force a Leave and Enter the current tool, in case it has something about the current landscape cached
	SetCurrentTool(CurrentToolIndex);
	if (CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor->SetTargetLandscape(CurrentToolTarget.LandscapeInfo.Get());
	}

	// register to new one
	if (CurrentToolTarget.LandscapeInfo.IsValid())
	{
		ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		LandscapeProxy->OnMaterialChangedDelegate().AddRaw(this, &FEdModeLandscape::OnLandscapeMaterialChangedDelegate);
		if (ALandscape* Landscape = GetLandscape())
		{
			Landscape->OnBlueprintBrushChangedDelegate().AddRaw(this, &FEdModeLandscape::RefreshDetailPanel);
		}
	}

	UpdateTargetList();
	UpdateShownLayerList();
}

bool FEdModeLandscape::CanEditCurrentTarget(FText* Reason) const
{
	static FText DummyReason;
	FText& LocalReason = Reason ? *Reason : DummyReason;

	if (!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		LocalReason = NSLOCTEXT("UnrealEd", "LandscapeInvalidTarget", "No landscape selected.");
		return false;
	}

	// Landscape Layer Editing not available without a loaded Landscape Actor
	if (GetLandscape() == nullptr)
	{
		ALandscapeProxy* Proxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		if (!Proxy)
		{
            LocalReason = NSLOCTEXT("UnrealEd", "LandscapeNotFound", "No Landscape found.");
			return false;
		}
		
		if (Proxy->HasLayersContent())
		{
			LocalReason = NSLOCTEXT("UnrealEd", "LandscapeActorNotLoaded", "Landscape actor is not loaded. It is needed to do layer editing.");
			return false;
		}
	}

	return true;
}

void FEdModeLandscape::UpdateTargetList()
{
	LandscapeTargetList.Empty();

	if (CurrentToolTarget.LandscapeInfo.IsValid())
	{
		ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();

		if (LandscapeProxy != nullptr)
		{
			CachedLandscapeMaterial = LandscapeProxy->GetLandscapeMaterial();

			bool bFoundSelected = false;

			// Add heightmap
			LandscapeTargetList.Add(MakeShareable(new FLandscapeTargetListInfo(LOCTEXT("Heightmap", "Heightmap"), ELandscapeToolTargetType::Heightmap, CurrentToolTarget.LandscapeInfo.Get(), UISettings->CurrentLayerIndex)));

			if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap)
			{
				bFoundSelected = true;
			}

			// Add visibility
			FLandscapeInfoLayerSettings VisibilitySettings(ALandscapeProxy::VisibilityLayer, LandscapeProxy);
			LandscapeTargetList.Add(MakeShareable(new FLandscapeTargetListInfo(LOCTEXT("Visibility", "Visibility"), ELandscapeToolTargetType::Visibility, VisibilitySettings, UISettings->CurrentLayerIndex)));

			if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Visibility)
			{
				bFoundSelected = true;
			}

			// Add layers
			UTexture2D* ThumbnailWeightmap = NULL;
			UTexture2D* ThumbnailHeightmap = NULL;

			TargetLayerStartingIndex = LandscapeTargetList.Num();

			for (auto It = CurrentToolTarget.LandscapeInfo->Layers.CreateIterator(); It; It++)
			{
				FLandscapeInfoLayerSettings& LayerSettings = *It;

				FName LayerName = LayerSettings.GetLayerName();

				if (LayerSettings.LayerInfoObj == ALandscapeProxy::VisibilityLayer)
				{
					// Already handled above
					continue;
				}

				if (!bFoundSelected &&
					CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap &&
					CurrentToolTarget.LayerInfo == LayerSettings.LayerInfoObj &&
					CurrentToolTarget.LayerName == LayerSettings.LayerName)
				{
					bFoundSelected = true;
				}

				// Ensure thumbnails are up valid
				if (LayerSettings.ThumbnailMIC == NULL)
				{
					if (ThumbnailWeightmap == NULL)
					{
						ThumbnailWeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailWeightmap.LandscapeThumbnailWeightmap"), NULL, LOAD_None, NULL);
					}
					if (ThumbnailHeightmap == NULL)
					{
						ThumbnailHeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailHeightmap.LandscapeThumbnailHeightmap"), NULL, LOAD_None, NULL);
					}

					// Construct Thumbnail MIC
					UMaterialInterface* LandscapeMaterial = LayerSettings.Owner ? LayerSettings.Owner->GetLandscapeMaterial() : UMaterial::GetDefaultMaterial(MD_Surface);
					LayerSettings.ThumbnailMIC = ALandscapeProxy::GetLayerThumbnailMIC(LandscapeMaterial, LayerName, ThumbnailWeightmap, ThumbnailHeightmap, LayerSettings.Owner);
				}

				// Add the layer
				LandscapeTargetList.Add(MakeShareable(new FLandscapeTargetListInfo(FText::FromName(LayerName), ELandscapeToolTargetType::Weightmap, LayerSettings, UISettings->CurrentLayerIndex)));
			}

			if (!bFoundSelected)
			{
				CurrentToolTarget.TargetType = ELandscapeToolTargetType::Invalid;
				SetCurrentTargetLayer(NAME_None, nullptr);
			}

			UpdateTargetLayerDisplayOrder(UISettings->TargetDisplayOrder);
		}
	}

	TargetsListUpdated.Broadcast();
}

void FEdModeLandscape::UpdateTargetLayerDisplayOrder(ELandscapeLayerDisplayMode InTargetDisplayOrder)
{
	if (!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return;
	}

	ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();

	if (LandscapeProxy == nullptr)
	{
		return;
	}

	bool DetailPanelRefreshRequired = false;

	// Save value to landscape
	LandscapeProxy->TargetDisplayOrder = InTargetDisplayOrder;
	TArray<FName>& SavedTargetNameList = LandscapeProxy->TargetDisplayOrderList;

	switch (InTargetDisplayOrder)
	{
		case ELandscapeLayerDisplayMode::Default:
		{
			SavedTargetNameList.Empty();

			for (const TSharedRef<FLandscapeTargetListInfo>& TargetInfo : LandscapeTargetList)
			{
				SavedTargetNameList.Add(TargetInfo->LayerName);
			}

			DetailPanelRefreshRequired = true;
		}
		break;

		case ELandscapeLayerDisplayMode::Alphabetical:
		{
			SavedTargetNameList.Empty();

			// Add only layers to be able to sort them by name
			for (int32 i = GetTargetLayerStartingIndex(); i < LandscapeTargetList.Num(); ++i)
			{
				SavedTargetNameList.Add(LandscapeTargetList[i]->LayerName);
			}

			SavedTargetNameList.Sort(FNameLexicalLess());

			// Then insert the non layer target that shouldn't be sorted
			for (int32 i = 0; i < GetTargetLayerStartingIndex(); ++i)
			{
				SavedTargetNameList.Insert(LandscapeTargetList[i]->LayerName, i);
			}

			DetailPanelRefreshRequired = true;
		}
		break;

		case ELandscapeLayerDisplayMode::UserSpecific:
		{
			for (const TSharedRef<FLandscapeTargetListInfo>& TargetInfo : LandscapeTargetList)
			{
				bool Found = false;

				for (const FName& LayerName : SavedTargetNameList)
				{
					if (TargetInfo->LayerName == LayerName)
					{
						Found = true;
						break;
					}
				}

				if (!Found)
				{
					DetailPanelRefreshRequired = true;
					SavedTargetNameList.Add(TargetInfo->LayerName);
				}
			}

			// Handle the removing of elements from material
			for (int32 i = SavedTargetNameList.Num() - 1; i >= 0; --i)
			{
				bool Found = false;

				for (const TSharedRef<FLandscapeTargetListInfo>& TargetInfo : LandscapeTargetList)
				{
					if (SavedTargetNameList[i] == TargetInfo->LayerName)
					{
						Found = true;
						break;
					}
				}

				if (!Found)
				{
					DetailPanelRefreshRequired = true;
					SavedTargetNameList.RemoveSingle(SavedTargetNameList[i]);
				}
			}
		}
		break;
	}	

	if (DetailPanelRefreshRequired)
	{
		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->RefreshDetailPanel();
		}
	}
}

void FEdModeLandscape::OnLandscapeMaterialChangedDelegate()
{
	UpdateTargetList();
	UpdateShownLayerList();
}

void FEdModeLandscape::RequestUpdateShownLayerList()
{
	bNeedsUpdateShownLayerList = true;

	if (CurrentToolTarget.LandscapeInfo.IsValid() && !CurrentToolTarget.LandscapeInfo->CanHaveLayersContent())
	{
		UpdateShownLayerList(); // do it sync when not in lanscape mode.
	}
}

void FEdModeLandscape::UpdateShownLayerList()
{
	bNeedsUpdateShownLayerList = false;

	if (!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return;
	}

	// Make sure usage information is up to date
	UpdateLayerUsageInformation();

	bool DetailPanelRefreshRequired = false;

	ShownTargetLayerList.Empty();

	const TArray<FName>* DisplayOrderList = GetTargetDisplayOrderList();

	if (DisplayOrderList == nullptr)
	{
		return;
	}

	for (const FName& LayerName : *DisplayOrderList)
	{
		for (const TSharedRef<FLandscapeTargetListInfo>& TargetInfo : GetTargetList())
		{
			if (TargetInfo->LayerName == LayerName)
			{
				// Keep a mapping of visible layer name to display order list so we can drag & drop proper items
				if (ShouldShowLayer(TargetInfo))
				{
					ShownTargetLayerList.Add(TargetInfo->LayerName);
					DetailPanelRefreshRequired = true;
				}

				break;
			}
		}
	}	

	if (DetailPanelRefreshRequired)
	{
		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->RefreshDetailPanel();
		}
	}
}

void FEdModeLandscape::UpdateLayerUsageInformation(TWeakObjectPtr<ULandscapeLayerInfoObject>* LayerInfoObjectThatChanged)
{
	if (!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return;
	}

	bool DetailPanelRefreshRequired = false;
	
	TArray<TWeakObjectPtr<ULandscapeLayerInfoObject>> LayerInfoObjectToProcess;
	const TArray<TSharedRef<FLandscapeTargetListInfo>>& TargetList = GetTargetList();

	if (LayerInfoObjectThatChanged != nullptr)
	{
		if ((*LayerInfoObjectThatChanged).IsValid())
		{
			LayerInfoObjectToProcess.Add(*LayerInfoObjectThatChanged);
		}
	}
	else
	{
		LayerInfoObjectToProcess.Reserve(TargetList.Num());

		for (const TSharedRef<FLandscapeTargetListInfo>& TargetInfo : TargetList)
		{
			if (!TargetInfo->LayerInfoObj.IsValid() || TargetInfo->TargetType != ELandscapeToolTargetType::Weightmap)
			{
				continue;
			}

			LayerInfoObjectToProcess.Add(TargetInfo->LayerInfoObj);
		}
	}
	
	TArray<ULandscapeLayerInfoObject*> UsedLayerInfos;
	CurrentToolTarget.LandscapeInfo->GetUsedPaintLayers(FGuid(), UsedLayerInfos);
		
	for (const TWeakObjectPtr<ULandscapeLayerInfoObject>& LayerInfoObj : LayerInfoObjectToProcess)
	{
		if (ULandscapeLayerInfoObject* LayerInfo = LayerInfoObj.Get())
		{
			bool bUsed = UsedLayerInfos.Contains(LayerInfo);
			if (LayerInfo->IsReferencedFromLoadedData != bUsed)
			{
				LayerInfo->IsReferencedFromLoadedData = bUsed;
				DetailPanelRefreshRequired = true;
			}
		}
	}
	
	if (DetailPanelRefreshRequired)
	{
		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->RefreshDetailPanel();
		}
	}
}

bool FEdModeLandscape::ShouldShowLayer(TSharedRef<FLandscapeTargetListInfo> Target) const
{
	if (!UISettings->ShowUnusedLayers)
	{
		return Target->LayerInfoObj.IsValid() && Target->LayerInfoObj.Get()->IsReferencedFromLoadedData;
	}

	return true;
}

const TArray<FName>& FEdModeLandscape::GetTargetShownList() const
{
	return ShownTargetLayerList;
}

int32 FEdModeLandscape::GetTargetLayerStartingIndex() const
{
	return TargetLayerStartingIndex;
}

const TArray<FName>* FEdModeLandscape::GetTargetDisplayOrderList() const
{
	if (!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return nullptr;
	}

	ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();

	if (LandscapeProxy == nullptr)
	{
		return nullptr;
	}

	return &LandscapeProxy->TargetDisplayOrderList;
}

void FEdModeLandscape::MoveTargetLayerDisplayOrder(int32 IndexToMove, int32 IndexToDestination)
{
	if (!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return;
	}

	ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();

	if (LandscapeProxy == nullptr)
	{
		return;
	}
	
	FName Data = LandscapeProxy->TargetDisplayOrderList[IndexToMove];
	LandscapeProxy->TargetDisplayOrderList.RemoveAt(IndexToMove);
	LandscapeProxy->TargetDisplayOrderList.Insert(Data, IndexToDestination);

	LandscapeProxy->TargetDisplayOrder = ELandscapeLayerDisplayMode::UserSpecific;
	UISettings->TargetDisplayOrder = ELandscapeLayerDisplayMode::UserSpecific;

	// Everytime we move something from the display order we must rebuild the shown layer list
	UpdateShownLayerList();
}

FEdModeLandscape::FTargetsListUpdated FEdModeLandscape::TargetsListUpdated;

void FEdModeLandscape::HandleLevelsChanged(bool ShouldExitMode)
{
	bool bHadLandscape = (NewLandscapePreviewMode == ENewLandscapePreviewMode::None);

	UpdateLandscapeList();
	UpdateTargetList();
	UpdateShownLayerList();
	UpdateBrushList();

	// if the Landscape is deleted then close the landscape editor
	if (ShouldExitMode && bHadLandscape && CurrentToolTarget.LandscapeInfo == nullptr)
	{
		RequestDeletion();
	}

	// if a landscape is added somehow then switch to sculpt
	if (!bHadLandscape && CanEditCurrentTarget())
	{
		SetCurrentTool("Select");
		SetCurrentTool("Sculpt");
	}
}

void FEdModeLandscape::OnMaterialCompilationFinished(UMaterialInterface* MaterialInterface)
{
	if (CurrentToolTarget.LandscapeInfo.IsValid() &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy() != NULL &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy()->GetLandscapeMaterial() != NULL &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy()->GetLandscapeMaterial()->IsDependent(MaterialInterface))
	{
		CurrentToolTarget.LandscapeInfo->UpdateLayerInfoMap();
		UpdateTargetList();
		UpdateShownLayerList();
	}
}

/** FEdMode: Render the mesh paint tool */
void FEdModeLandscape::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	/** Call parent implementation */
	FEdMode::Render(View, Viewport, PDI);

	if (!IsEditingEnabled())
	{
		return;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		static const float        CornerSize = 0.33f;
		static const FLinearColor CornerColour(1.0f, 1.0f, 0.5f);
		static const FLinearColor EdgeColour(1.0f, 1.0f, 0.0f);
		static const FLinearColor ComponentBorderColour(0.0f, 0.85f, 0.0f);
		static const FLinearColor SectionBorderColour(0.0f, 0.4f, 0.0f);
		static const FLinearColor InnerColour(0.0f, 0.25f, 0.0f);

		const ELevelViewportType ViewportType = ((FEditorViewportClient*)Viewport->GetClient())->ViewportType;

		const int32 ComponentCountX = UISettings->NewLandscape_ComponentCount.X;
		const int32 ComponentCountY = UISettings->NewLandscape_ComponentCount.Y;
		const int32 QuadsPerComponent = UISettings->NewLandscape_SectionsPerComponent * UISettings->NewLandscape_QuadsPerSection;
		const float ComponentSize = QuadsPerComponent;
		const FVector Offset = UISettings->NewLandscape_Location + FTransform(UISettings->NewLandscape_Rotation, FVector::ZeroVector, UISettings->NewLandscape_Scale).TransformVector(FVector(-ComponentCountX * ComponentSize / 2, -ComponentCountY * ComponentSize / 2, 0));
		const FTransform Transform = FTransform(UISettings->NewLandscape_Rotation, Offset, UISettings->NewLandscape_Scale);

		if (NewLandscapePreviewMode == ENewLandscapePreviewMode::ImportLandscape)
		{
			const TArray<uint16>& ImportHeights = UISettings->GetImportLandscapeData();
			if (ImportHeights.Num() != 0)
			{
				const float InvQuadsPerComponent = 1.0f / (float)QuadsPerComponent;
				const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
				const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;
				const int32 ImportSizeX = UISettings->ImportLandscape_Width;
				const int32 ImportSizeY = UISettings->ImportLandscape_Height;
				const int32 OffsetX = (SizeX - ImportSizeX) / 2;
				const int32 OffsetY = (SizeY - ImportSizeY) / 2;

				for (int32 ComponentY = 0; ComponentY < ComponentCountY; ComponentY++)
				{
					const int32 Y0 = ComponentY * QuadsPerComponent;
					const int32 Y1 = (ComponentY + 1) * QuadsPerComponent;

					const int32 ImportY0 = FMath::Clamp<int32>(Y0 - OffsetY, 0, ImportSizeY - 1);
					const int32 ImportY1 = FMath::Clamp<int32>(Y1 - OffsetY, 0, ImportSizeY - 1);

					for (int32 ComponentX = 0; ComponentX < ComponentCountX; ComponentX++)
					{
						const int32 X0 = ComponentX * QuadsPerComponent;
						const int32 X1 = (ComponentX + 1) * QuadsPerComponent;
						const int32 ImportX0 = FMath::Clamp<int32>(X0 - OffsetX, 0, ImportSizeX - 1);
						const int32 ImportX1 = FMath::Clamp<int32>(X1 - OffsetX, 0, ImportSizeX - 1);
						const float Z00 = ((float)ImportHeights[ImportX0 + ImportY0 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z01 = ((float)ImportHeights[ImportX0 + ImportY1 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z10 = ((float)ImportHeights[ImportX1 + ImportY0 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z11 = ((float)ImportHeights[ImportX1 + ImportY1 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;

						if (ComponentX == 0)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0, Z00)), Transform.TransformPosition(FVector(X0, Y1, Z01)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}

						if (ComponentX == ComponentCountX - 1)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive));
							PDI->DrawLine(Transform.TransformPosition(FVector(X1, Y0, Z10)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
						else
						{
							PDI->DrawLine(Transform.TransformPosition(FVector(X1, Y0, Z10)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
						}

						if (ComponentY == 0)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Negative));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0, Z00)), Transform.TransformPosition(FVector(X1, Y0, Z10)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}

						if (ComponentY == ComponentCountY - 1)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Positive));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y1, Z01)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
						else
						{
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y1, Z01)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
						}

						// intra-component lines - too slow for big landscapes
						/*
						for (int32 x=1;x<QuadsPerComponent;x++)
						{
						PDI->DrawLine(Transform.TransformPosition(FVector(X0+x, Y0, FMath::Lerp(Z00,Z10,(float)x*InvQuadsPerComponent))), Transform.TransformPosition(FVector(X0+x, Y1, FMath::Lerp(Z01,Z11,(float)x*InvQuadsPerComponent))), ComponentBorderColour, SDPG_World);
						}
						for (int32 y=1;y<QuadsPerComponent;y++)
						{
						PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0+y, FMath::Lerp(Z00,Z01,(float)y*InvQuadsPerComponent))), Transform.TransformPosition(FVector(X1, Y0+y, FMath::Lerp(Z10,Z11,(float)y*InvQuadsPerComponent))), ComponentBorderColour, SDPG_World);
						}
						*/
					}
				}
			}
		}
		else //if (NewLandscapePreviewMode == ENewLandscapePreviewMode::NewLandscape)
		{
			if (ViewportType == LVT_Perspective || ViewportType == LVT_OrthoXY || ViewportType == LVT_OrthoNegativeXY)
			{
				for (int32 x = 0; x <= ComponentCountX * QuadsPerComponent; x++)
				{
					if (x == 0)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (x == ComponentCountX * QuadsPerComponent)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (x % QuadsPerComponent == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), ComponentBorderColour, SDPG_Foreground);
					}
					else if (x % UISettings->NewLandscape_QuadsPerSection == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), SectionBorderColour, SDPG_Foreground);
					}
					else
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), InnerColour, SDPG_World);
					}
				}
			}
			else
			{
				// Don't allow dragging to resize in side-view
				// and there's no point drawing the inner lines as only the outer is visible
				PDI->DrawLine(Transform.TransformPosition(FVector(0, 0, 0)), Transform.TransformPosition(FVector(0, ComponentCountY * ComponentSize, 0)), EdgeColour, SDPG_World);
				PDI->DrawLine(Transform.TransformPosition(FVector(ComponentCountX * QuadsPerComponent, 0, 0)), Transform.TransformPosition(FVector(ComponentCountX * QuadsPerComponent, ComponentCountY * ComponentSize, 0)), EdgeColour, SDPG_World);
			}

			if (ViewportType == LVT_Perspective || ViewportType == LVT_OrthoXY || ViewportType == LVT_OrthoNegativeXY)
			{
				for (int32 y = 0; y <= ComponentCountY * QuadsPerComponent; y++)
				{
					if (y == 0)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (y == ComponentCountY * QuadsPerComponent)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (y % QuadsPerComponent == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), ComponentBorderColour, SDPG_Foreground);
					}
					else if (y % UISettings->NewLandscape_QuadsPerSection == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), SectionBorderColour, SDPG_Foreground);
					}
					else
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), InnerColour, SDPG_World);
					}
				}
			}
			else
			{
				// Don't allow dragging to resize in side-view
				// and there's no point drawing the inner lines as only the outer is visible
				PDI->DrawLine(Transform.TransformPosition(FVector(0, 0, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, 0, 0)), EdgeColour, SDPG_World);
				PDI->DrawLine(Transform.TransformPosition(FVector(0, ComponentCountY * QuadsPerComponent, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, ComponentCountY * QuadsPerComponent, 0)), EdgeColour, SDPG_World);
			}
		}

		return;
	}

	if (LandscapeRenderAddCollision)
	{
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[0], LandscapeRenderAddCollision->Corners[3], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[3], LandscapeRenderAddCollision->Corners[1], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[1], LandscapeRenderAddCollision->Corners[0], FColor(0, 255, 128), SDPG_Foreground);

		PDI->DrawLine(LandscapeRenderAddCollision->Corners[0], LandscapeRenderAddCollision->Corners[2], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[2], LandscapeRenderAddCollision->Corners[3], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[3], LandscapeRenderAddCollision->Corners[0], FColor(0, 255, 128), SDPG_Foreground);
	}

	// Override Rendering for Splines Tool
	if (CurrentTool)
	{
		CurrentTool->Render(View, Viewport, PDI);
	}
}

/** FEdMode: Render HUD elements for this tool */
void FEdModeLandscape::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
}

bool FEdModeLandscape::UsesTransformWidget() const
{
	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return true;
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->UsesTransformWidget())
	{
		return true;
	}

	return (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected() && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo));
}

bool FEdModeLandscape::ShouldDrawWidget() const
{
	return UsesTransformWidget();
}

EAxisList::Type FEdModeLandscape::GetWidgetAxisToDraw(FWidget::EWidgetMode InWidgetMode) const
{
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		// Override Widget for Splines Tool
		if (CurrentTool)
		{
			return CurrentTool->GetWidgetAxisToDraw(InWidgetMode);
		}
	}

	switch (InWidgetMode)
	{
	case FWidget::WM_Translate:
		return EAxisList::XYZ;
	case FWidget::WM_Rotate:
		return EAxisList::Z;
	case FWidget::WM_Scale:
		return EAxisList::XYZ;
	default:
		return EAxisList::None;
	}
}

FVector FEdModeLandscape::GetWidgetLocation() const
{
	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return UISettings->NewLandscape_Location;
	}

	if (CurrentGizmoActor.IsValid() && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo) && CurrentGizmoActor->IsSelected())
	{
		if (CurrentGizmoActor->TargetLandscapeInfo && CurrentGizmoActor->TargetLandscapeInfo->GetLandscapeProxy())
		{
			// Apply Landscape transformation when it is available
			ULandscapeInfo* LandscapeInfo = CurrentGizmoActor->TargetLandscapeInfo;
			return CurrentGizmoActor->GetActorLocation()
				+ FQuatRotationMatrix(LandscapeInfo->GetLandscapeProxy()->GetActorQuat()).TransformPosition(FVector(0, 0, CurrentGizmoActor->GetLength()));
		}
		return CurrentGizmoActor->GetActorLocation();
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->OverrideWidgetLocation())
	{
		return CurrentTool->GetWidgetLocation();
	}

	return FEdMode::GetWidgetLocation();
}

bool FEdModeLandscape::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		InMatrix = FRotationMatrix(UISettings->NewLandscape_Rotation);
		return true;
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->OverrideWidgetRotation())
	{
		InMatrix = CurrentTool->GetWidgetRotation();
		return true;
	}

	return false;
}

bool FEdModeLandscape::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

/** FEdMode: Handling SelectActor */
bool FEdModeLandscape::Select(AActor* InActor, bool bInSelected)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (InActor->IsA<ALandscapeProxy>() && bInSelected)
	{
		ALandscapeProxy* Landscape = CastChecked<ALandscapeProxy>(InActor);

		if (CurrentToolTarget.LandscapeInfo != Landscape->GetLandscapeInfo())
		{
			SetLandscapeInfo(Landscape->GetLandscapeInfo());
			UpdateTargetList();

			// If we were in "New Landscape" mode and we select a landscape then switch to editing mode
			if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
			{
				SetCurrentTool("Sculpt");
			}
		}
	}

	if (IsSelectionAllowed(InActor, bInSelected))
	{
		// false means "we haven't handled the selection", which allows the editor to perform the selection
		// so false means "allow"
		return false;
	}

	// true means "we have handled the selection", which effectively blocks the selection from happening
	// so true means "block"
	return true;
}

/** FEdMode: Check to see if an actor can be selected in this mode - no side effects */
bool FEdModeLandscape::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	// Override Selection for Splines Tool
	if (CurrentTool && CurrentTool->OverrideSelection())
	{
		return CurrentTool->IsSelectionAllowed(InActor, bInSelection);
	}

	if (!bInSelection)
	{
		// always allow de-selection
		return true;
	}

	if (InActor->IsA(ALandscapeProxy::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ALandscapeGizmoActor::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ALight::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ALandscapeBlueprintBrushBase::StaticClass()))
	{
		return true;
	}

	return true;
}

/** FEdMode: Called when the currently selected actor has changed */
void FEdModeLandscape::ActorSelectionChangeNotify()
{
	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, false, true);
	}
	/*
		USelection* EditorSelection = GEditor->GetSelectedActors();
		for ( FSelectionIterator Itor(EditorSelection) ; Itor ; ++Itor )
		{
		if (((*Itor)->IsA(ALandscapeGizmoActor::StaticClass())) )
		{
		bIsGizmoSelected = true;
		break;
		}
		}
	*/
}

void FEdModeLandscape::ActorMoveNotify()
{
	//GUnrealEd->UpdateFloatingPropertyWindows();
}

void FEdModeLandscape::PostUndo()
{
	HandleLevelsChanged(false);
}

/** Forces all level editor viewports to realtime mode */
void FEdModeLandscape::ForceRealTimeViewports(const bool bEnable, const bool bStoreCurrentState)

{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<ILevelViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<ILevelViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetLevelViewportClient();
				if (bEnable)
				{
					Viewport.SetRealtime(bEnable, bStoreCurrentState);

					// @todo vreditor: Force game view to true in VREditor since we can't use hitproxies and debug objects yet
					UVREditorMode* VREditorMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UVREditorMode::StaticClass() ) );
					if( VREditorMode != nullptr && VREditorMode->IsActive())
					{
						Viewport.SetVREditView(true);
					} 
					else
					{
						Viewport.SetVREditView(false);
					}
				}
				else
				{
					const bool bAllowDisable = true;
					Viewport.RestoreRealtime(bAllowDisable);
				}
			}
		}
	}
}

void FEdModeLandscape::ReimportData(const FLandscapeTargetListInfo& TargetInfo)
{
	const FString& SourceFilePath = TargetInfo.GetReimportFilePath();
	if (SourceFilePath.Len())
	{
		FScopedSetLandscapeEditingLayer Scope(GetLandscape(), GetCurrentLayerGuid(), [&] { RequestLayersContentUpdateForceAll(); });
		ImportData(TargetInfo, SourceFilePath);
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "LandscapeReImport_BadFileName", "Reimport Source Filename is invalid"));
	}
}

void FEdModeLandscape::ImportData(const FLandscapeTargetListInfo& TargetInfo, const FString& Filename)
{
	ULandscapeInfo* LandscapeInfo = TargetInfo.LandscapeInfo.Get();
	int32 MinX, MinY, MaxX, MaxY;
	if (LandscapeInfo && LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		const FLandscapeFileResolution LandscapeResolution = {(uint32)(1 + MaxX - MinX), (uint32)(1 + MaxY - MinY)};

		ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");

		if (TargetInfo.TargetType == ELandscapeToolTargetType::Heightmap)
		{
			const ILandscapeHeightmapFileFormat* HeightmapFormat = LandscapeEditorModule.GetHeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));

			if (!HeightmapFormat)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_UnknownFileType", "File type not recognised"));

				return;
			}

			FLandscapeFileResolution ImportResolution = {0, 0};

			const FLandscapeHeightmapInfo HeightmapInfo = HeightmapFormat->Validate(*Filename);

			// display error message if there is one, and abort the import
			if (HeightmapInfo.ResultCode == ELandscapeImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, HeightmapInfo.ErrorMessage);

				return;
			}

			// if the file is a raw format with multiple possibly resolutions, only attempt import if one matches the current landscape
			if (HeightmapInfo.PossibleResolutions.Num() > 1)
			{
				if (!HeightmapInfo.PossibleResolutions.Contains(LandscapeResolution))
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("LandscapeSizeX"), LandscapeResolution.Width);
					Args.Add(TEXT("LandscapeSizeY"), LandscapeResolution.Height);

					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_HeightmapSizeMismatchRaw", "The heightmap file does not match the current Landscape extent ({LandscapeSizeX}\u00D7{LandscapeSizeY}), and its exact resolution could not be determined"), Args));

					return;
				}
				else
				{
					ImportResolution = LandscapeResolution;
				}
			}

			// display warning message if there is one and allow user to cancel
			if (HeightmapInfo.ResultCode == ELandscapeImportResult::Warning)
			{
				auto Result = FMessageDialog::Open(EAppMsgType::OkCancel, HeightmapInfo.ErrorMessage);

				if (Result != EAppReturnType::Ok)
				{
					return;
				}
			}

			// if the file is a format with resolution information, warn the user if the resolution doesn't match the current landscape
			// unlike for raw this is only a warning as we can pad/clip the data if we know what resolution it is
			if (HeightmapInfo.PossibleResolutions.Num() == 1)
			{
				ImportResolution = HeightmapInfo.PossibleResolutions[0];
				if (ImportResolution != LandscapeResolution)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("FileSizeX"), ImportResolution.Width);
					Args.Add(TEXT("FileSizeY"), ImportResolution.Height);
					Args.Add(TEXT("LandscapeSizeX"), LandscapeResolution.Width);
					Args.Add(TEXT("LandscapeSizeY"), LandscapeResolution.Height);

					auto Result = FMessageDialog::Open(EAppMsgType::OkCancel,
						FText::Format(NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_HeightmapSizeMismatch", "The heightmap file's size ({FileSizeX}\u00D7{FileSizeY}) does not match the current Landscape extent ({LandscapeSizeX}\u00D7{LandscapeSizeY}), if you continue it will be padded/clipped to fit"), Args));

					if (Result != EAppReturnType::Ok)
					{
						return;
					}
				}
			}

			FLandscapeHeightmapImportData ImportData = HeightmapFormat->Import(*Filename, ImportResolution);

			if (ImportData.ResultCode == ELandscapeImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, ImportData.ErrorMessage);

				return;
			}

			{
				ALandscape* Landscape = GetLandscape();
				FScopedSetLandscapeEditingLayer Scope(Landscape, GetCurrentLayerGuid(), [&] { check(Landscape); Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All); });

				TArray<uint16> Data;
				if (ImportResolution != LandscapeResolution)
				{
					// Cloned from FLandscapeEditorDetailCustomization_NewLandscape.OnCreateButtonClicked
					// so that reimports behave the same as the initial import :)

					const int32 OffsetX = (int32)(LandscapeResolution.Width - ImportResolution.Width) / 2;
					const int32 OffsetY = (int32)(LandscapeResolution.Height - ImportResolution.Height) / 2;

					Data.SetNumUninitialized(LandscapeResolution.Width * LandscapeResolution.Height * sizeof(uint16));

					LandscapeEditorUtils::ExpandData<uint16>(Data.GetData(), ImportData.Data.GetData(),
						0, 0, ImportResolution.Width - 1, ImportResolution.Height - 1,
						-OffsetX, -OffsetY, LandscapeResolution.Width - OffsetX - 1, LandscapeResolution.Height - OffsetY - 1);
				}
				else
				{
					Data = MoveTemp(ImportData.Data);
				}

				FScopedTransaction Transaction(LOCTEXT("Undo_ImportHeightmap", "Importing Landscape Heightmap"));

				FHeightmapAccessor<false> HeightmapAccessor(LandscapeInfo);
				HeightmapAccessor.SetData(MinX, MinY, MaxX, MaxY, Data.GetData());
			}
		}
		else
		{
			const ILandscapeWeightmapFileFormat* WeightmapFormat = LandscapeEditorModule.GetWeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));

			if (!WeightmapFormat)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_UnknownFileType", "File type not recognised"));

				return;
			}

			FLandscapeFileResolution ImportResolution = {0, 0};

			const FLandscapeWeightmapInfo WeightmapInfo = WeightmapFormat->Validate(*Filename, TargetInfo.LayerName);

			// display error message if there is one, and abort the import
			if (WeightmapInfo.ResultCode == ELandscapeImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, WeightmapInfo.ErrorMessage);

				return;
			}

			// if the file is a raw format with multiple possibly resolutions, only attempt import if one matches the current landscape
			if (WeightmapInfo.PossibleResolutions.Num() > 1)
			{
				if (!WeightmapInfo.PossibleResolutions.Contains(LandscapeResolution))
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("LandscapeSizeX"), LandscapeResolution.Width);
					Args.Add(TEXT("LandscapeSizeY"), LandscapeResolution.Height);

					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_LayerSizeMismatch_ResNotDetermined", "The layer file does not match the current Landscape extent ({LandscapeSizeX}\u00D7{LandscapeSizeY}), and its exact resolution could not be determined"), Args));

					return;
				}
				else
				{
					ImportResolution = LandscapeResolution;
				}
			}

			// display warning message if there is one and allow user to cancel
			if (WeightmapInfo.ResultCode == ELandscapeImportResult::Warning)
			{
				auto Result = FMessageDialog::Open(EAppMsgType::OkCancel, WeightmapInfo.ErrorMessage);

				if (Result != EAppReturnType::Ok)
				{
					return;
				}
			}

			// if the file is a format with resolution information, warn the user if the resolution doesn't match the current landscape
			// unlike for raw this is only a warning as we can pad/clip the data if we know what resolution it is
			if (WeightmapInfo.PossibleResolutions.Num() == 1)
			{
				ImportResolution = WeightmapInfo.PossibleResolutions[0];
				if (ImportResolution != LandscapeResolution)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("FileSizeX"), ImportResolution.Width);
					Args.Add(TEXT("FileSizeY"), ImportResolution.Height);
					Args.Add(TEXT("LandscapeSizeX"), LandscapeResolution.Width);
					Args.Add(TEXT("LandscapeSizeY"), LandscapeResolution.Height);

					auto Result = FMessageDialog::Open(EAppMsgType::OkCancel,
						FText::Format(NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_LayerSizeMismatch_WillClamp", "The layer file's size ({FileSizeX}\u00D7{FileSizeY}) does not match the current Landscape extent ({LandscapeSizeX}\u00D7{LandscapeSizeY}), if you continue it will be padded/clipped to fit"), Args));

					if (Result != EAppReturnType::Ok)
					{
						return;
					}
				}
			}

			FLandscapeWeightmapImportData ImportData = WeightmapFormat->Import(*Filename, TargetInfo.LayerName, ImportResolution);

			if (ImportData.ResultCode == ELandscapeImportResult::Error)
			{
				FMessageDialog::Open(EAppMsgType::Ok, ImportData.ErrorMessage);

				return;
			}

			{
				ALandscape* Landscape = GetLandscape();
				FScopedSetLandscapeEditingLayer Scope(Landscape, GetCurrentLayerGuid(), [&] { check(Landscape); Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All); });

				TArray<uint8> Data;
				if (ImportResolution != LandscapeResolution)
				{
					// Cloned from FLandscapeEditorDetailCustomization_NewLandscape.OnCreateButtonClicked
					// so that reimports behave the same as the initial import :)

					const int32 OffsetX = (int32)(LandscapeResolution.Width - ImportResolution.Width) / 2;
					const int32 OffsetY = (int32)(LandscapeResolution.Height - ImportResolution.Height) / 2;

					Data.SetNumUninitialized(LandscapeResolution.Width * LandscapeResolution.Height * sizeof(uint8));

					LandscapeEditorUtils::ExpandData<uint8>(Data.GetData(), ImportData.Data.GetData(),
						0, 0, ImportResolution.Width - 1, ImportResolution.Height - 1,
						-OffsetX, -OffsetY, LandscapeResolution.Width - OffsetX - 1, LandscapeResolution.Height - OffsetY - 1);
				}
				else
				{
					Data = MoveTemp(ImportData.Data);
				}

				FScopedTransaction Transaction(LOCTEXT("Undo_ImportWeightmap", "Importing Landscape Layer"));

				FAlphamapAccessor<false, false> AlphamapAccessor(LandscapeInfo, TargetInfo.LayerInfoObj.Get());
				AlphamapAccessor.SetData(MinX, MinY, MaxX, MaxY, Data.GetData(), ELandscapeLayerPaintingRestriction::None);
			}
		}
	}
}

void FEdModeLandscape::DeleteLandscapeComponents(ULandscapeInfo* LandscapeInfo, TSet<ULandscapeComponent*> ComponentsToDelete)
{
	LandscapeInfo->Modify();
	ALandscapeProxy* Proxy = LandscapeInfo->GetLandscapeProxy();
	Proxy->Modify();

	for (ULandscapeComponent* Component : ComponentsToDelete)
	{
		Component->Modify();
		ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		if (CollisionComp)
		{
			CollisionComp->Modify();
		}
	}

	int32 ComponentSizeVerts = LandscapeInfo->ComponentNumSubsections * (LandscapeInfo->SubsectionSizeQuads + 1);
	int32 NeedHeightmapSize = 1 << FMath::CeilLogTwo(ComponentSizeVerts);

	TSet<ULandscapeComponent*> HeightmapUpdateComponents;
	// Need to split all the component which share Heightmap with selected components
	// Search neighbor only
	for (ULandscapeComponent* Component : ComponentsToDelete)
	{
		int32 SearchX = Component->GetHeightmap()->Source.GetSizeX() / NeedHeightmapSize;
		int32 SearchY = Component->GetHeightmap()->Source.GetSizeY() / NeedHeightmapSize;
		FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;

		for (int32 Y = 0; Y < SearchY; ++Y)
		{
			for (int32 X = 0; X < SearchX; ++X)
			{
				// Search for four directions...
				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					int32 XDir = (Dir >> 1) ? 1 : -1;
					int32 YDir = (Dir % 2) ? 1 : -1;
					ULandscapeComponent* Neighbor = LandscapeInfo->XYtoComponentMap.FindRef(ComponentBase + FIntPoint(XDir*X, YDir*Y));
					if (Neighbor && Neighbor->GetHeightmap() == Component->GetHeightmap() && !HeightmapUpdateComponents.Contains(Neighbor))
					{
						Neighbor->Modify();
						HeightmapUpdateComponents.Add(Neighbor);
					}
				}
			}
		}
	}

	TArray<FComponentRecreateRenderStateContext> RecreateRenderStateContexts;

	{
		TArray<UActorComponent*> ComponentsToReregister(HeightmapUpdateComponents.Array());
		FMaterialUpdateContext MaterialUpdateContext(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);

		// Changing Heightmap format for selected components
		for (ULandscapeComponent* Component : HeightmapUpdateComponents)
		{
			ALandscape::SplitHeightmap(Component, nullptr, &MaterialUpdateContext, &RecreateRenderStateContexts, false);
		}

		FMultiComponentReregisterContext RegisterContext(ComponentsToReregister);
	}

	RecreateRenderStateContexts.Empty();

	// Remove attached foliage
	for (ULandscapeComponent* Component : ComponentsToDelete)
	{
		ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		if (CollisionComp)
		{
			AInstancedFoliageActor::DeleteInstancesForComponent(Proxy->GetWorld(), CollisionComp);
		}
	}

	TArray<UActorComponent*> NeighborsComponentToReregister;

	// Check which ones are need for height map change
	for (ULandscapeComponent* Component : ComponentsToDelete)
	{
		// Reset neighbors LOD information
		FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;
		FIntPoint NeighborKeys[8] =
		{
			ComponentBase + FIntPoint(-1, -1),
			ComponentBase + FIntPoint(+0, -1),
			ComponentBase + FIntPoint(+1, -1),
			ComponentBase + FIntPoint(-1, +0),
			ComponentBase + FIntPoint(+1, +0),
			ComponentBase + FIntPoint(-1, +1),
			ComponentBase + FIntPoint(+0, +1),
			ComponentBase + FIntPoint(+1, +1)
		};

		for (const FIntPoint& NeighborKey : NeighborKeys)
		{
			ULandscapeComponent* NeighborComp = LandscapeInfo->XYtoComponentMap.FindRef(NeighborKey);
			if (NeighborComp && !ComponentsToDelete.Contains(NeighborComp))
			{
				NeighborComp->Modify();
				NeighborComp->InvalidateLightingCache();

				NeighborsComponentToReregister.AddUnique(NeighborComp);
			}
		}

		// Remove Selected Region in deleted Component
		for (int32 Y = 0; Y < Component->ComponentSizeQuads; ++Y)
		{
			for (int32 X = 0; X < Component->ComponentSizeQuads; ++X)
			{
				LandscapeInfo->SelectedRegion.Remove(FIntPoint(X, Y) + Component->GetSectionBase());
			}
		}

		UTexture2D* HeightmapTexture = Component->GetHeightmap();

		if (HeightmapTexture)
		{
			HeightmapTexture->SetFlags(RF_Transactional);
			HeightmapTexture->Modify();
			HeightmapTexture->MarkPackageDirty();
			HeightmapTexture->ClearFlags(RF_Standalone); // Remove when there is no reference for this Heightmap...
		}

		TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();

		for (UTexture2D* WeightmapTexture : ComponentWeightmapTextures)
		{
			WeightmapTexture->SetFlags(RF_Transactional);
			WeightmapTexture->Modify();
			WeightmapTexture->MarkPackageDirty();
			WeightmapTexture->ClearFlags(RF_Standalone);
		}

		if (Component->XYOffsetmapTexture)
		{
			Component->XYOffsetmapTexture->SetFlags(RF_Transactional);
			Component->XYOffsetmapTexture->Modify();
			Component->XYOffsetmapTexture->MarkPackageDirty();
			Component->XYOffsetmapTexture->ClearFlags(RF_Standalone);
		}

		ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		if (CollisionComp)
		{
			CollisionComp->DestroyComponent();
		}
		Component->DestroyComponent();
	}

	{
		FMultiComponentReregisterContext RegisterContext(NeighborsComponentToReregister);
	}

	// Remove Selection
	LandscapeInfo->ClearSelectedRegion(true);
	//EdMode->SetMaskEnable(Landscape->SelectedRegion.Num());
	GEngine->BroadcastLevelActorListChanged();
}

ALandscape* FEdModeLandscape::ChangeComponentSetting(int32 NumComponentsX, int32 NumComponentsY, int32 NumSubsections, int32 SubsectionSizeQuads, bool bResample)
{
	FScopedSlowTask Progress(3, LOCTEXT("LandscapeChangeComponentSetting", "Changing Landscape Component Settings..."));
	Progress.MakeDialog();
	int32 CurrentTaskProgress = 0;

	check(NumComponentsX > 0);
	check(NumComponentsY > 0);
	check(NumSubsections > 0);
	check(SubsectionSizeQuads > 0);

	const int32 NewComponentSizeQuads = NumSubsections * SubsectionSizeQuads;

	ALandscape* NewLandscape = NULL;

	ULandscapeInfo* LandscapeInfo = CurrentToolTarget.LandscapeInfo.Get();
	if (ensure(LandscapeInfo != NULL))
	{
		int32 OldMinX, OldMinY, OldMaxX, OldMaxY;
		if (LandscapeInfo->GetLandscapeExtent(OldMinX, OldMinY, OldMaxX, OldMaxY))
		{
			ALandscape* OldLandscape = LandscapeInfo->LandscapeActor.Get();
			check(OldLandscape != nullptr);

			const int32 OldVertsX = OldMaxX - OldMinX + 1;
			const int32 OldVertsY = OldMaxY - OldMinY + 1;
			const int32 NewVertsX = NumComponentsX * NewComponentSizeQuads + 1;
			const int32 NewVertsY = NumComponentsY * NewComponentSizeQuads + 1;

			TMap<FGuid, TArray<uint16>> HeightDataPerLayers;			
			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> ImportMaterialLayerInfosPerLayers;

			FVector LandscapeOffset = FVector::ZeroVector;
			FIntPoint LandscapeOffsetQuads = FIntPoint::ZeroValue;
			float LandscapeScaleFactor = bResample ? (float)OldLandscape->ComponentSizeQuads / NewComponentSizeQuads : 1.0f;

			int32 NewMinX, NewMinY, NewMaxX, NewMaxY;

			{ // Scope to flush the texture update before doing the import
				FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);

				if (bResample)
				{
					NewMinX = OldMinX / LandscapeInfo->ComponentSizeQuads * NewComponentSizeQuads;
					NewMinY = OldMinY / LandscapeInfo->ComponentSizeQuads * NewComponentSizeQuads;
					NewMaxX = NewMinX + NewVertsX - 1;
					NewMaxY = NewMinY + NewVertsY - 1;
				}
				else
				{
					NewMinX = OldMinX + (OldVertsX - NewVertsX) / 2;
					NewMinY = OldMinY + (OldVertsY - NewVertsY) / 2;
					NewMaxX = NewMinX + NewVertsX - 1;
					NewMaxY = NewMinY + NewVertsY - 1;

					// offset landscape to component boundary
					LandscapeOffset = FVector(NewMinX, NewMinY, 0) * OldLandscape->GetActorScale();
					LandscapeOffsetQuads = FIntPoint(NewMinX, NewMinY);
				}

				auto ExtractHeightmapWeightmapContent = [&](TArray<uint16>& OutHeightData, TArray<FLandscapeImportLayerInfo>& OutImportMaterialLayerInfos)
				{
					if (bResample)
					{
						OutHeightData.AddZeroed(OldVertsX * OldVertsY * sizeof(uint16));

						// GetHeightData alters its args, so make temp copies to avoid screwing things up
						int32 TMinX = OldMinX, TMinY = OldMinY, TMaxX = OldMaxX, TMaxY = OldMaxY;
						LandscapeEdit.GetHeightData(TMinX, TMinY, TMaxX, TMaxY, OutHeightData.GetData(), 0);

						OutHeightData = LandscapeEditorUtils::ResampleData(OutHeightData, OldVertsX, OldVertsY, NewVertsX, NewVertsY);

						for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
						{
							if (LayerSettings.LayerInfoObj != NULL)
							{
								auto ImportLayerInfo = new(OutImportMaterialLayerInfos) FLandscapeImportLayerInfo(LayerSettings);
								ImportLayerInfo->LayerData.AddZeroed(OldVertsX * OldVertsY * sizeof(uint8));

								TMinX = OldMinX; TMinY = OldMinY; TMaxX = OldMaxX; TMaxY = OldMaxY;
								LandscapeEdit.GetWeightData(LayerSettings.LayerInfoObj, TMinX, TMinY, TMaxX, TMaxY, ImportLayerInfo->LayerData.GetData(), 0);

								ImportLayerInfo->LayerData = LandscapeEditorUtils::ResampleData(ImportLayerInfo->LayerData, OldVertsX, OldVertsY, NewVertsX, NewVertsY);
							}
						}
					}
					else
					{
						const int32 RequestedMinX = FMath::Max(OldMinX, NewMinX);
						const int32 RequestedMinY = FMath::Max(OldMinY, NewMinY);
						const int32 RequestedMaxX = FMath::Min(OldMaxX, NewMaxX);
						const int32 RequestedMaxY = FMath::Min(OldMaxY, NewMaxY);

						const int32 RequestedVertsX = RequestedMaxX - RequestedMinX + 1;
						const int32 RequestedVertsY = RequestedMaxY - RequestedMinY + 1;

						OutHeightData.AddZeroed(RequestedVertsX * RequestedVertsY * sizeof(uint16));

						// GetHeightData alters its args, so make temp copies to avoid screwing things up
						int32 TMinX = RequestedMinX, TMinY = RequestedMinY, TMaxX = RequestedMaxX, TMaxY = RequestedMaxY;
						LandscapeEdit.GetHeightData(TMinX, TMinY, TMaxX, OldMaxY, OutHeightData.GetData(), 0);

						OutHeightData = LandscapeEditorUtils::ExpandData(OutHeightData, RequestedMinX, RequestedMinY, RequestedMaxX, RequestedMaxY, NewMinX, NewMinY, NewMaxX, NewMaxY);

						for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
						{
							if (LayerSettings.LayerInfoObj != NULL)
							{
								auto ImportLayerInfo = new(OutImportMaterialLayerInfos) FLandscapeImportLayerInfo(LayerSettings);
								ImportLayerInfo->LayerData.AddZeroed(NewVertsX * NewVertsY * sizeof(uint8));

								TMinX = RequestedMinX; TMinY = RequestedMinY; TMaxX = RequestedMaxX; TMaxY = RequestedMaxY;
								LandscapeEdit.GetWeightData(LayerSettings.LayerInfoObj, TMinX, TMinY, TMaxX, TMaxY, ImportLayerInfo->LayerData.GetData(), 0);

								ImportLayerInfo->LayerData = LandscapeEditorUtils::ExpandData(ImportLayerInfo->LayerData, RequestedMinX, RequestedMinY, RequestedMaxX, RequestedMaxY, NewMinX, NewMinY, NewMaxX, NewMaxY);
							}
						}
					}
				};

				if (HasLandscapeLayersContent())
				{
					int32 HeightCount = 0;

					for (const FLandscapeLayer& OldLayer : OldLandscape->LandscapeLayers)
					{
						FScopedSetLandscapeEditingLayer Scope(OldLandscape, OldLayer.Guid);

						TArray<uint16> HeightData;
						TArray<FLandscapeImportLayerInfo> ImportMaterialLayerInfos;

						ExtractHeightmapWeightmapContent(HeightData, ImportMaterialLayerInfos);

						HeightCount = FMath::Max(HeightCount, HeightData.Num());
						HeightDataPerLayers.Add(OldLayer.Guid, MoveTemp(HeightData));
						ImportMaterialLayerInfosPerLayers.Add(OldLayer.Guid, MoveTemp(ImportMaterialLayerInfos));
					}

					TArray<uint16> DefaultHeightData;
					DefaultHeightData.AddUninitialized(HeightCount);

					uint16 DefaultValue = LandscapeDataAccess::GetTexHeight(0.f);

					// Initialize blank heightmap data
					for (int32 i = 0; i < DefaultHeightData.Num(); i++)
					{
						DefaultHeightData[i] = DefaultValue;
					}

					HeightDataPerLayers.Add(FGuid(), DefaultHeightData);

					TArray<FLandscapeImportLayerInfo> EmptyImportLayer;
					ImportMaterialLayerInfosPerLayers.Add(FGuid(), EmptyImportLayer);
				}
				else
				{
					TArray<uint16> HeightData;
					TArray<FLandscapeImportLayerInfo> ImportMaterialLayerInfos;

					ExtractHeightmapWeightmapContent(HeightData, ImportMaterialLayerInfos);

					HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
					ImportMaterialLayerInfosPerLayers.Add(FGuid(), MoveTemp(ImportMaterialLayerInfos));
				}

				if (!bResample)
				{
					NewMinX = 0;
					NewMinY = 0;
					NewMaxX = NewVertsX - 1;
					NewMaxY = NewVertsY - 1;
				}
			}

			Progress.EnterProgressFrame(CurrentTaskProgress++);

			const FVector Location = OldLandscape->GetActorLocation() + LandscapeOffset;
			FActorSpawnParameters SpawnParams;
			SpawnParams.OverrideLevel = OldLandscape->GetLevel();
			NewLandscape = OldLandscape->GetWorld()->SpawnActor<ALandscape>(Location, OldLandscape->GetActorRotation(), SpawnParams);
			NewLandscape->bCanHaveLayersContent = OldLandscape->bCanHaveLayersContent;
			const FVector OldScale = OldLandscape->GetActorScale();
			NewLandscape->SetActorRelativeScale3D(FVector(OldScale.X * LandscapeScaleFactor, OldScale.Y * LandscapeScaleFactor, OldScale.Z));

			NewLandscape->LandscapeMaterial = OldLandscape->LandscapeMaterial;
			NewLandscape->LandscapeMaterialsOverride = OldLandscape->LandscapeMaterialsOverride;
			NewLandscape->CollisionMipLevel = OldLandscape->CollisionMipLevel;
			NewLandscape->MaxLODLevel = OldLandscape->MaxLODLevel;
			NewLandscape->LODDistanceFactor_DEPRECATED = OldLandscape->LODDistanceFactor_DEPRECATED;
			NewLandscape->LODFalloff_DEPRECATED = OldLandscape->LODFalloff_DEPRECATED;
			NewLandscape->TessellationComponentScreenSize = OldLandscape->TessellationComponentScreenSize;
			NewLandscape->ComponentScreenSizeToUseSubSections = OldLandscape->ComponentScreenSizeToUseSubSections;
			NewLandscape->UseTessellationComponentScreenSizeFalloff = OldLandscape->UseTessellationComponentScreenSizeFalloff;
			NewLandscape->TessellationComponentScreenSizeFalloff = OldLandscape->TessellationComponentScreenSizeFalloff;
			NewLandscape->LODDistributionSetting = OldLandscape->LODDistributionSetting;
			NewLandscape->LOD0DistributionSetting = OldLandscape->LOD0DistributionSetting;
			NewLandscape->OccluderGeometryLOD = OldLandscape->OccluderGeometryLOD;
			NewLandscape->ExportLOD = OldLandscape->ExportLOD;
			NewLandscape->StaticLightingLOD = OldLandscape->StaticLightingLOD;
			NewLandscape->NegativeZBoundsExtension = OldLandscape->NegativeZBoundsExtension;
			NewLandscape->PositiveZBoundsExtension = OldLandscape->PositiveZBoundsExtension;
			NewLandscape->DefaultPhysMaterial = OldLandscape->DefaultPhysMaterial;
			NewLandscape->StreamingDistanceMultiplier = OldLandscape->StreamingDistanceMultiplier;
			NewLandscape->LandscapeHoleMaterial = OldLandscape->LandscapeHoleMaterial;
			NewLandscape->StaticLightingResolution = OldLandscape->StaticLightingResolution;
			NewLandscape->bCastStaticShadow = OldLandscape->bCastStaticShadow;
			NewLandscape->bCastShadowAsTwoSided = OldLandscape->bCastShadowAsTwoSided;
			NewLandscape->LightingChannels = OldLandscape->LightingChannels;
			NewLandscape->bRenderCustomDepth = OldLandscape->bRenderCustomDepth;
			NewLandscape->CustomDepthStencilValue = OldLandscape->CustomDepthStencilValue;
			NewLandscape->LightmassSettings = OldLandscape->LightmassSettings;
			NewLandscape->CollisionThickness = OldLandscape->CollisionThickness;
			NewLandscape->BodyInstance.SetCollisionProfileName(OldLandscape->BodyInstance.GetCollisionProfileName());
			if (NewLandscape->BodyInstance.DoesUseCollisionProfile() == false)
			{
				NewLandscape->BodyInstance.SetCollisionEnabled(OldLandscape->BodyInstance.GetCollisionEnabled());
				NewLandscape->BodyInstance.SetObjectType(OldLandscape->BodyInstance.GetObjectType());
				NewLandscape->BodyInstance.SetResponseToChannels(OldLandscape->BodyInstance.GetResponseToChannels());
			}
			NewLandscape->EditorLayerSettings = OldLandscape->EditorLayerSettings;
			NewLandscape->bUsedForNavigation = OldLandscape->bUsedForNavigation;
			NewLandscape->MaxPaintedLayersPerComponent = OldLandscape->MaxPaintedLayersPerComponent;

			TArray<FLandscapeLayer>* LandscapeLayers = CanHaveLandscapeLayersContent() ? &OldLandscape->LandscapeLayers : nullptr;

			NewLandscape->Import(FGuid::NewGuid(), NewMinX, NewMinY, NewMaxX, NewMaxY, NumSubsections, SubsectionSizeQuads, HeightDataPerLayers, *OldLandscape->ReimportHeightmapFilePath, ImportMaterialLayerInfosPerLayers, ELandscapeImportAlphamapType::Additive, LandscapeLayers);

			ULandscapeInfo* NewLandscapeInfo = NewLandscape->GetLandscapeInfo();
			check(NewLandscapeInfo);

			// Clone landscape splines
			ALandscape* OldLandscapeActor = LandscapeInfo->LandscapeActor.Get();
			if (OldLandscapeActor != nullptr && OldLandscapeActor->SplineComponent != NULL)
			{
				ULandscapeSplinesComponent* OldSplines = OldLandscapeActor->SplineComponent;
				ULandscapeSplinesComponent* NewSplines = DuplicateObject<ULandscapeSplinesComponent>(OldSplines, NewLandscape, OldSplines->GetFName());
				NewSplines->AttachToComponent(NewLandscape->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

				const FVector OldSplineScale = OldSplines->GetRelativeTransform().GetScale3D();
				NewSplines->SetRelativeScale3D(FVector(OldSplineScale.X / LandscapeScaleFactor, OldSplineScale.Y / LandscapeScaleFactor, OldSplineScale.Z));
				NewLandscape->SplineComponent = NewSplines;
				NewSplines->RegisterComponent();

				// TODO: Foliage on spline meshes
			}

			Progress.EnterProgressFrame(CurrentTaskProgress++);

			if (bResample)
			{
				// Remap foliage to the resampled components
				for (const TPair<FIntPoint, ULandscapeComponent*>& Entry : LandscapeInfo->XYtoComponentMap)
				{
					ULandscapeComponent* NewComponent = NewLandscapeInfo->XYtoComponentMap.FindRef(Entry.Key);
					if (NewComponent)
					{
						ULandscapeHeightfieldCollisionComponent* OldCollisionComponent = Entry.Value->CollisionComponent.Get();
						ULandscapeHeightfieldCollisionComponent* NewCollisionComponent = NewComponent->CollisionComponent.Get();

						if (OldCollisionComponent && NewCollisionComponent)
						{
							AInstancedFoliageActor::MoveInstancesToNewComponent(OldCollisionComponent->GetWorld(), OldCollisionComponent, NewCollisionComponent);
							NewCollisionComponent->SnapFoliageInstances(FBox(FVector(-WORLD_MAX), FVector(WORLD_MAX)));
						}
					}
				}

				Progress.EnterProgressFrame(CurrentTaskProgress++);

				// delete any components that were deleted in the original
				TSet<ULandscapeComponent*> ComponentsToDelete;
				for (const TPair<FIntPoint, ULandscapeComponent*>& Entry : NewLandscapeInfo->XYtoComponentMap)
				{
					if (!LandscapeInfo->XYtoComponentMap.Contains(Entry.Key))
					{
						ComponentsToDelete.Add(Entry.Value);
					}
				}
				if (ComponentsToDelete.Num() > 0)
				{
					DeleteLandscapeComponents(NewLandscapeInfo, ComponentsToDelete);
				}
			}
			else
			{
				// Move instances
				for (const TPair<FIntPoint, ULandscapeComponent*>& OldEntry : LandscapeInfo->XYtoComponentMap)
				{
					ULandscapeHeightfieldCollisionComponent* OldCollisionComponent = OldEntry.Value->CollisionComponent.Get();

					if (OldCollisionComponent)
					{
						UWorld* World = OldCollisionComponent->GetWorld();

						for (const TPair<FIntPoint, ULandscapeComponent*>& NewEntry : NewLandscapeInfo->XYtoComponentMap)
						{
							ULandscapeHeightfieldCollisionComponent* NewCollisionComponent = NewEntry.Value->CollisionComponent.Get();

							if (NewCollisionComponent && FBoxSphereBounds::BoxesIntersect(NewCollisionComponent->Bounds, OldCollisionComponent->Bounds))
							{
								FBox Box = NewCollisionComponent->Bounds.GetBox();
								Box.Min.Z = -WORLD_MAX;
								Box.Max.Z = WORLD_MAX;

								AInstancedFoliageActor::MoveInstancesToNewComponent(World, OldCollisionComponent, Box, NewCollisionComponent);
							}
						}
					}
				}

				// Snap them to the bounds
				for (const TPair<FIntPoint, ULandscapeComponent*>& NewEntry : NewLandscapeInfo->XYtoComponentMap)
				{
					ULandscapeHeightfieldCollisionComponent* NewCollisionComponent = NewEntry.Value->CollisionComponent.Get();

					if (NewCollisionComponent)
					{
						FBox Box = NewCollisionComponent->Bounds.GetBox();
						Box.Min.Z = -WORLD_MAX;
						Box.Max.Z = WORLD_MAX;

						NewCollisionComponent->SnapFoliageInstances(Box);
					}
				}

				Progress.EnterProgressFrame(CurrentTaskProgress++);

				// delete any components that are in areas that were entirely deleted in the original
				TSet<ULandscapeComponent*> ComponentsToDelete;
				for (const TPair<FIntPoint, ULandscapeComponent*>& Entry : NewLandscapeInfo->XYtoComponentMap)
				{
					float OldX = Entry.Key.X * NewComponentSizeQuads + LandscapeOffsetQuads.X;
					float OldY = Entry.Key.Y * NewComponentSizeQuads + LandscapeOffsetQuads.Y;
					TSet<ULandscapeComponent*> OverlapComponents;
					LandscapeInfo->GetComponentsInRegion(OldX, OldY, OldX + NewComponentSizeQuads, OldY + NewComponentSizeQuads, OverlapComponents, false);
					if (OverlapComponents.Num() == 0)
					{
						ComponentsToDelete.Add(Entry.Value);
					}
				}
				if (ComponentsToDelete.Num() > 0)
				{
					DeleteLandscapeComponents(NewLandscapeInfo, ComponentsToDelete);
				}
			}

			// Delete the old Landscape and all its proxies
			for (ALandscapeStreamingProxy* Proxy : TActorRange<ALandscapeStreamingProxy>(OldLandscape->GetWorld()))
			{
				if (Proxy->LandscapeActor == OldLandscapeActor)
				{
					Proxy->Destroy();
				}
			}
			OldLandscape->Destroy();
		}
	}

	GEditor->RedrawLevelEditingViewports();

	return NewLandscape;
}

ELandscapeEditingState FEdModeLandscape::GetEditingState() const
{
	UWorld* World = GetWorld();

	if (GEditor->bIsSimulatingInEditor)
	{
		return ELandscapeEditingState::SIEWorld;
	}
	else if (GEditor->PlayWorld != NULL)
	{
		return ELandscapeEditingState::PIEWorld;
	}
	else if (World == nullptr)
	{
		return ELandscapeEditingState::Unknown;
	}
	else if (World->FeatureLevel < ERHIFeatureLevel::SM4)
	{
		return ELandscapeEditingState::BadFeatureLevel;
	}
	else if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None &&
		!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return ELandscapeEditingState::NoLandscape;
	}

	return ELandscapeEditingState::Enabled;
}

bool FEdModeLandscape::CanHaveLandscapeLayersContent() const
{
	ALandscape* Landscape = GetLandscape();
	return Landscape != nullptr ? Landscape->CanHaveLayersContent() : false;
}

bool FEdModeLandscape::HasLandscapeLayersContent() const
{
	ALandscape* Landscape = GetLandscape();	
	return Landscape != nullptr ? Landscape->HasLayersContent() : false;
}

int32 FEdModeLandscape::GetLayerCount() const
{
	ALandscape* Landscape = GetLandscape();
	return Landscape ? Landscape->GetLayerCount() : 0;
}

void FEdModeLandscape::SetCurrentLayer(int32 InLayerIndex)
{
	UISettings->Modify();
	UISettings->CurrentLayerIndex = InLayerIndex;
	RefreshDetailPanel();
	RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Client_Editing);
}

int32 FEdModeLandscape::GetCurrentLayerIndex() const
{
	return UISettings ? UISettings->CurrentLayerIndex : INDEX_NONE;
}

ALandscape* FEdModeLandscape::GetLandscape() const
{
	return CurrentToolTarget.LandscapeInfo.IsValid() ? CurrentToolTarget.LandscapeInfo->LandscapeActor.Get() : nullptr;
}

FLandscapeLayer* FEdModeLandscape::GetLayer(int32 InLayerIndex) const
{
	ALandscape* Landscape = GetLandscape();
	return Landscape ? Landscape->GetLayer(InLayerIndex) : nullptr;
}

FName FEdModeLandscape::GetLayerName(int32 InLayerIndex) const
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	return Layer ? Layer->Name : NAME_None;
}

bool FEdModeLandscape::CanRenameLayerTo(int32 InLayerIndex, const FName& InNewName)
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		int32 LayerCount = GetLayerCount();
		for (int32 LayerIdx = 0; LayerIdx < LayerCount; ++LayerIdx)
		{
			if (LayerIdx != InLayerIndex && GetLayerName(LayerIdx) == InNewName)
			{
				return false;
			}
		}
	}
	return true;
}

void FEdModeLandscape::SetLayerName(int32 InLayerIndex, const FName& InName)
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		Landscape->SetLayerName(InLayerIndex, InName);
	}
}

bool FEdModeLandscape::IsLayerAlphaVisible(int32 InLayerIndex) const
{
	return (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap || CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap);
}

float FEdModeLandscape::GetClampedLayerAlpha(float InLayerAlpha) const
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap || CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap)
		{
			return Landscape->GetClampedLayerAlpha(InLayerAlpha, CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap);
		}
	}
	return InLayerAlpha;
}

float FEdModeLandscape::GetLayerAlpha(int32 InLayerIndex) const
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap || CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap)
		{
			return Landscape->GetLayerAlpha(InLayerIndex, CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap);
		}
	}
	return 1.0f;
}

void FEdModeLandscape::SetLayerAlpha(int32 InLayerIndex, float InAlpha)
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap || CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap)
		{
			Landscape->SetLayerAlpha(InLayerIndex, InAlpha, CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap);
		}
	}
}

bool FEdModeLandscape::IsLayerVisible(int32 InLayerIndex) const
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	return Layer ? Layer->bVisible : false;
}

void FEdModeLandscape::SetLayerVisibility(bool bInVisible, int32 InLayerIndex)
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		Landscape->SetLayerVisibility(InLayerIndex, bInVisible);
	}
}

bool FEdModeLandscape::IsLayerLocked(int32 InLayerIndex) const
{
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	return Layer && Layer->bLocked;
}

void FEdModeLandscape::SetLayerLocked(int32 InLayerIndex, bool bInLocked)
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape)
	{
		Landscape->SetLayerLocked(InLayerIndex, bInLocked);
	}
}

void FEdModeLandscape::RequestLayersContentUpdate(ELandscapeLayerUpdateMode InUpdateMode)
{
	if (ALandscape* Landscape = GetLandscape())
	{
		Landscape->RequestLayersContentUpdate(InUpdateMode);
	}
}

void FEdModeLandscape::RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode InUpdateMode)
{
	if (ALandscape* Landscape = GetLandscape())
	{
		Landscape->RequestLayersContentUpdateForceAll(InUpdateMode);
	}
}

void FEdModeLandscape::AddBrushToCurrentLayer(ALandscapeBlueprintBrushBase* InBrush)
{
	ALandscape* Landscape = GetLandscape();
	if (Landscape == nullptr)
	{
		return;
	}

	Landscape->AddBrushToLayer(GetCurrentLayerIndex(), InBrush);
	RefreshDetailPanel();
}

void FEdModeLandscape::RemoveBrushFromCurrentLayer(ALandscapeBlueprintBrushBase* InBrush)
{
	ALandscape* Landscape = GetLandscape();

	if (Landscape == nullptr)
	{
		return;
	}

	Landscape->RemoveBrushFromLayer(GetCurrentLayerIndex(), InBrush);
	RefreshDetailPanel();
}

ALandscapeBlueprintBrushBase* FEdModeLandscape::GetBrushForCurrentLayer(int8 InBrushIndex) const
{
	if (ALandscape* Landscape = GetLandscape())
	{
		return Landscape->GetBrushForLayer(GetCurrentLayerIndex(), InBrushIndex);
	}
	return nullptr;
}

TArray<ALandscapeBlueprintBrushBase*> FEdModeLandscape::GetBrushesForCurrentLayer()
{
	TArray<ALandscapeBlueprintBrushBase*> Brushes;
	if (ALandscape* Landscape = GetLandscape())
	{
		Brushes = Landscape->GetBrushesForLayer(GetCurrentLayerIndex());
	}
	return Brushes;
}

bool FEdModeLandscape::IsCurrentLayerBlendSubstractive(const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj) const
{
	ALandscape* Landscape = GetLandscape();

	if (Landscape != nullptr)
	{
		return Landscape->IsLayerBlendSubstractive(GetCurrentLayerIndex(), InLayerInfoObj);
	}

	return false;
}

void FEdModeLandscape::SetCurrentLayerSubstractiveBlendStatus(bool InStatus, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj)
{
	ALandscape* Landscape = GetLandscape();

	if (Landscape != nullptr)
	{
		return Landscape->SetLayerSubstractiveBlendStatus(GetCurrentLayerIndex(), InStatus, InLayerInfoObj);
	}
}

FLandscapeLayer* FEdModeLandscape::GetCurrentLayer() const
{
	return GetLayer(GetCurrentLayerIndex());
}

void FEdModeLandscape::AutoUpdateDirtyLandscapeSplines()
{
	if (HasLandscapeLayersContent() && GEditor->IsTransactionActive())
	{
		// Only auto-update if a layer is reserved for landscape splines
		ALandscape* Landscape = GetLandscape();
		if (Landscape && Landscape->GetLandscapeSplinesReservedLayer())
		{
			// TODO : Only update dirty regions
			UpdateLandscapeSplines();
		}
	}
}

bool FEdModeLandscape::CanEditLayer(FText* Reason /*=nullptr*/, FLandscapeLayer* InLayer /*= nullptr*/)
{
	if (CanHaveLandscapeLayersContent())
	{
		ALandscape* Landscape = GetLandscape();
		FLandscapeLayer* TargetLayer = InLayer ? InLayer : GetCurrentLayer();
		if (!TargetLayer)
		{
			if (Reason)
			{
				*Reason = NSLOCTEXT("UnrealEd", "LandscapeInvalidLayer", "No layer selected. You must first chose a layer to work on.");
			}
			return false;
		}
		else if (!TargetLayer->bVisible)
		{
			if (Reason)
			{
				*Reason = NSLOCTEXT("UnrealEd", "LandscapeLayerHidden", "Painting or sculping in a hidden layer is not allowed.");
			}
			return false;
		}
		else if (TargetLayer->bLocked)
		{
			if (Reason)
			{
				*Reason = NSLOCTEXT("UnrealEd", "LandscapeLayerLocked", "This layer is locked. You must unlock it before you can work on this layer.");
			}
			return false;
		}
		else if (CurrentTool)
		{
			int32 TargetLayerIndex = Landscape ? Landscape->LandscapeLayers.IndexOfByPredicate([TargetLayeyGuid = TargetLayer->Guid](const FLandscapeLayer& OtherLayer) { return OtherLayer.Guid == TargetLayeyGuid; }) : INDEX_NONE;

			if ((CurrentTool != (FLandscapeTool*)SplinesTool) && Landscape && (TargetLayer == Landscape->GetLandscapeSplinesReservedLayer()))
			{
				if (Reason)
				{
					*Reason = NSLOCTEXT("UnrealEd", "LandscapeLayerReservedForSplines", "This layer is reserved for Landscape Splines.");
				}
				return false;
			}
			else if (CurrentTool->GetToolName() == FName("Retopologize"))
			{
				if (Reason)
				{
					*Reason = FText::Format(NSLOCTEXT("UnrealEd", "LandscapeLayersNoSupportForRetopologize", "{0} Tool is not available with the Landscape Layer System."), CurrentTool->GetDisplayName());
				}
				return false;
			}
		}
	}

	if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap && CurrentToolTarget.LayerInfo == NULL && CurrentTool->GetToolName() != FName("BlueprintBrush"))
	{
		if (Reason)
		{
			*Reason = NSLOCTEXT("UnrealEd", "LandscapeNeedToCreateLayerInfo", "This layer has no layer info assigned yet. You must create or assign a layer info before you can paint this layer.");
		}
		return false;
		// TODO: FName to LayerInfo: do we want to create the layer info here?
		//if (FMessageDialog::Open(EAppMsgType::YesNo, NSLOCTEXT("UnrealEd", "LandscapeNeedToCreateLayerInfo", "This layer has no layer info assigned yet. You must create or assign a layer info before you can paint this layer.")) == EAppReturnType::Yes)
		//{
		//	CurrentToolTarget.LandscapeInfo->LandscapeProxy->CreateLayerInfo(*CurrentToolTarget.PlaceholderLayerName.ToString());
		//}
	}
	return true;
}

void FEdModeLandscape::UpdateLandscapeSplines(bool bUpdateOnlySelected /* = false*/)
{
	if (HasLandscapeLayersContent())
	{
		ALandscape* Landscape = GetLandscape();
		if (Landscape)
		{
			Landscape->UpdateLandscapeSplines(GetCurrentLayerGuid(), bUpdateOnlySelected);
		}
	}
	else
	{
		if (CurrentToolTarget.LandscapeInfo.IsValid())
		{
			CurrentToolTarget.LandscapeInfo->ApplySplines(bUpdateOnlySelected);
		}
	}
}

FGuid FEdModeLandscape::GetCurrentLayerGuid() const
{
	FLandscapeLayer* CurrentLayer = GetCurrentLayer();
	return CurrentLayer ? CurrentLayer->Guid : FGuid();
}

bool FEdModeLandscape::NeedToFillEmptyMaterialLayers() const
{
	if (!CurrentToolTarget.LandscapeInfo.IsValid() || !CurrentToolTarget.LandscapeInfo->LandscapeActor.IsValid())
	{
		return false;
	}

	bool bCanFill = true;

	CurrentToolTarget.LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		if (!bCanFill)
		{
			return;
		}

		ALandscape* Landscape = Proxy->GetLandscapeActor();

		if (Landscape != nullptr)
		{
			for (FLandscapeLayer& Layer : Landscape->LandscapeLayers)
			{
				for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
				{
					const FLandscapeLayerComponentData* LayerComponentData = Component->GetLayerData(Layer.Guid);

					if (LayerComponentData != nullptr)
					{
						for (const FWeightmapLayerAllocationInfo& Alloc : LayerComponentData->WeightmapData.LayerAllocations)
						{
							if (Alloc.LayerInfo != nullptr)
							{
								bCanFill = false;
								return;
							}
						}
					}
				}
			}
		}
	});	

	return bCanFill;
}

void FEdModeLandscape::UpdateBrushList()
{
	BrushList.Empty();
	for (TObjectIterator<ALandscapeBlueprintBrushBase> BrushIt(RF_Transient|RF_ClassDefaultObject|RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill); BrushIt; ++BrushIt)
	{
		ALandscapeBlueprintBrushBase* Brush = *BrushIt;
		if (Brush->GetTypedOuter<UPackage>() != GetTransientPackage())
		{
			BrushList.Add(Brush);
		}
	}
}


void FEdModeLandscape::OnLevelActorAdded(AActor* InActor)
{
	if (ALandscape* Landscape = Cast <ALandscape>(InActor))
	{
		Landscape->RegisterLandscapeEdMode(this);
	}

	ALandscapeBlueprintBrushBase* Brush = Cast<ALandscapeBlueprintBrushBase>(InActor);
	if (Brush && Brush->GetTypedOuter<UPackage>() != GetTransientPackage())
	{
		if (!GIsReinstancing)
		{
			AddBrushToCurrentLayer(Brush);
		}
		UpdateBrushList();
		RefreshDetailPanel();
	}
}

void FEdModeLandscape::OnLevelActorRemoved(AActor* InActor)
{
	if (ALandscape* Landscape = Cast <ALandscape>(InActor))
	{
		Landscape->UnregisterLandscapeEdMode();
	}

	ALandscapeBlueprintBrushBase* Brush = Cast<ALandscapeBlueprintBrushBase>(InActor);
	if (Brush && Brush->GetTypedOuter<UPackage>() != GetTransientPackage())
	{
		UpdateBrushList();
		RefreshDetailPanel();
	}
}

bool LandscapeEditorUtils::SetHeightmapData(ALandscapeProxy* Landscape, const TArray<uint16>& Data)
{
	FIntRect ComponentsRect = Landscape->GetBoundingRect() + Landscape->LandscapeSectionOffset;

	if (Data.Num() == (1 + ComponentsRect.Width())*(1 + ComponentsRect.Height()))
	{
		FHeightmapAccessor<false> HeightmapAccessor(Landscape->GetLandscapeInfo());
		HeightmapAccessor.SetData(ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, Data.GetData());
		return true;
	}

	return false;
}

bool LandscapeEditorUtils::SetWeightmapData(ALandscapeProxy* Landscape, ULandscapeLayerInfoObject* LayerObject, const TArray<uint8>& Data)
{
	FIntRect ComponentsRect = Landscape->GetBoundingRect() + Landscape->LandscapeSectionOffset;

	if (Data.Num() == (1 + ComponentsRect.Width())*(1 + ComponentsRect.Height()))
	{
		FAlphamapAccessor<false, true> AlphamapAccessor(Landscape->GetLandscapeInfo(), LayerObject);
		AlphamapAccessor.SetData(ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, Data.GetData(), ELandscapeLayerPaintingRestriction::None);
		return true;
	}

	return false;
}

FName FLandscapeTargetListInfo::GetLayerName() const
{
	return LayerInfoObj.IsValid() ? LayerInfoObj->LayerName : LayerName;
}

#undef LOCTEXT_NAMESPACE