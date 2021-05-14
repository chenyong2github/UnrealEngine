// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/GenerateStaticMeshLODAssetTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Generators/SphereGenerator.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"
#include "MeshTangents.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryVisualization.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Misc/Paths.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

#include "Graphs/GenerateStaticMeshLODProcess.h"
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Polygroups/PolygroupSet.h"
#include "Polygroups/PolygroupUtil.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ToolTargetManager.h"

static_assert(WITH_EDITOR, "Tool being compiled without editor");
#include "Misc/ScopedSlowTask.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGenerateStaticMeshLODAssetTool"

//
// Local Op Stuff
//

namespace GenerateStaticMeshLODAssetLocals
{

	class FGenerateStaticMeshLODAssetOperatorOp : public FDynamicMeshOperator, public FGCObject
	{
	public:

		// Inputs
		UGenerateStaticMeshLODProcess* GenerateProcess;
		FGenerateStaticMeshLODProcessSettings GeneratorSettings;
		
		// Outputs
		// Inherited: 	TUniquePtr<FDynamicMesh3> ResultMesh;
		// 				FTransform3d ResultTransform;
		FMeshTangentsd ResultTangents;
		FSimpleShapeSet3d ResultCollision;

		void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObject(GenerateProcess);
		}

		void CalculateResult(FProgressCancel* Progress) override
		{
			auto DoCompute = [this](FProgressCancel* Progress)		// bracket this computation with lock/unlock
			{
				if (Progress && Progress->Cancelled())
				{
					return;
				}

				GenerateProcess->UpdateSettings(GeneratorSettings);

				if (Progress && Progress->Cancelled())
				{
					return;
				}

				GenerateProcess->ComputeDerivedSourceData(Progress);

				if (Progress && Progress->Cancelled())
				{
					return;
				}

				*ResultMesh = GenerateProcess->GetDerivedLOD0Mesh();
				ResultTangents = GenerateProcess->GetDerivedLOD0MeshTangents();
				ResultCollision = GenerateProcess->GetDerivedCollision();
			};

			GenerateProcess->GraphEvalCriticalSection.Lock();
			DoCompute(Progress);
			GenerateProcess->GraphEvalCriticalSection.Unlock();
		}
	};

	class FGenerateStaticMeshLODAssetOperatorFactory : public IDynamicMeshOperatorFactory
	{

	public:

		FGenerateStaticMeshLODAssetOperatorFactory(UGenerateStaticMeshLODAssetTool* AutoLODTool, UE::Geometry::FTransform3d ResultTransform) :
			AutoLODTool(AutoLODTool), 
			ResultTransform(ResultTransform) 
		{}

		// IDynamicMeshOperatorFactory API
		virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override
		{
			check(AutoLODTool);
			TUniquePtr<FGenerateStaticMeshLODAssetOperatorOp> Op = MakeUnique<FGenerateStaticMeshLODAssetOperatorOp>();		
			Op->GenerateProcess = AutoLODTool->GenerateProcess;
			Op->GeneratorSettings = AutoLODTool->BasicProperties->GeneratorSettings;
			Op->GeneratorSettings.CollisionGroupLayerName = AutoLODTool->BasicProperties->CollisionGroupLayerName;
			Op->SetResultTransform(ResultTransform);
			return Op;
		}

		UGenerateStaticMeshLODAssetTool* AutoLODTool = nullptr;
		UE::Geometry::FTransform3d ResultTransform;
	};

}


/*
 * ToolBuilder
 */


const FToolTargetTypeRequirements& UGenerateStaticMeshLODAssetToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UPrimitiveComponentBackedTarget::StaticClass(),
		UStaticMeshBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UGenerateStaticMeshLODAssetToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// hack to make multi-tool look like single-tool
	return (AssetAPI != nullptr && SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1);
}

UInteractiveTool* UGenerateStaticMeshLODAssetToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UGenerateStaticMeshLODAssetTool* NewTool = NewObject<UGenerateStaticMeshLODAssetTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




/*
 * Tool
 */


void UGenerateStaticMeshLODAssetTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UGenerateStaticMeshLODAssetTool::Setup()
{
	using GenerateStaticMeshLODAssetLocals::FGenerateStaticMeshLODAssetOperatorFactory;

	UInteractiveTool::Setup();

	SetToolDisplayName(LOCTEXT("ToolName", "AutoLOD"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartStaticMeshLODAssetTool", "Create a new LOD asset"),
		EToolMessageLevel::UserNotification);

	GenerateProcess = NewObject<UGenerateStaticMeshLODProcess>(this);

	IPrimitiveComponentBackedTarget* SourceComponent = TargetComponentInterface(0);
	UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(SourceComponent->GetOwnerComponent());
	UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();

	FProgressCancel Progress;
	FScopedSlowTask SlowTask(2, LOCTEXT("UGenerateStaticMeshLODAssetTool_Setup", "Initializing tool ..."));
	SlowTask.MakeDialog();

	if (StaticMesh)
	{
		SlowTask.EnterProgressFrame(1);

		bool bInitializeOK = GenerateProcess->Initialize(StaticMesh, &Progress);		// Must happen on main thread

		if (Progress.Warnings.Num() > 0)
		{
			const FProgressCancel::FMessageInfo& Warning = Progress.Warnings[0];
			GetToolManager()->DisplayMessage(Warning.MessageText, (EToolMessageLevel)Warning.MessageLevel);
		}
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("GenerateStaticMeshLODAssetTool_NoStaticMesh", "Could not find Static Mesh in selected input"),
			EToolMessageLevel::UserError);
	}

	

	SlowTask.EnterProgressFrame(1);
	BasicProperties = NewObject<UGenerateStaticMeshLODAssetToolProperties>(this);
	AddToolPropertySource(BasicProperties);
	BasicProperties->RestoreProperties(this);
	FString FullPathWithExtension = UEditorAssetLibrary::GetPathNameForLoadedAsset(StaticMesh);
	BasicProperties->NewAssetName = FPaths::GetBaseFilename(FullPathWithExtension, true);
	BasicProperties->GeneratedSuffix = TEXT("_AutoLOD");
	BasicProperties->GeneratorSettings = GenerateProcess->GetCurrentSettings();
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.FilterGroupLayer, [this](FName) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.ThickenWeightMapName, [this](FName) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.ThickenAmount, [this](float) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.SolidifyVoxelResolution, [this](int) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.WindingThreshold, [this](float) { OnSettingsModified(); });
	//BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.MorphologyVoxelResolution, [this](int) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.ClosureDistance, [this](float) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.SimplifyTriangleCount, [this](int) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.NumAutoUVCharts, [this](int) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.BakeResolution, [this](EGenerateStaticMeshLODBakeResolution) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.BakeThickness, [this](float) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.bCombineTextures, [this](bool) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.CollisionType, [this](EGenerateStaticMeshLODSimpleCollisionGeometryType) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.ConvexTriangleCount, [this](int) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.bPrefilterVertices, [this](bool) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.PrefilterGridResolution, [this](float) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.bSimplifyPolygons, [this](bool) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.HullTolerance, [this](float) { OnSettingsModified(); });
	BasicProperties->WatchProperty(BasicProperties->GeneratorSettings.SweepAxis, [this](EGenerateStaticMeshLODProjectedHullAxisMode) { OnSettingsModified(); });
	
	// Collision layer name property
	BasicProperties->WatchProperty(BasicProperties->CollisionGroupLayerName, [this](FName) { OnSettingsModified(); });
	BasicProperties->InitializeGroupLayers(&(GenerateProcess->GetSourceMesh()));

	FBoxSphereBounds Bounds = StaticMeshComponent->Bounds;
	FTransform PreviewTransform = SourceComponent->GetWorldTransform();
	PreviewTransform.AddToTranslation(FVector(0, 2.5f*Bounds.BoxExtent.Y, 0));

	this->OpFactory = MakeUnique<FGenerateStaticMeshLODAssetOperatorFactory>(this, (UE::Geometry::FTransform3d)PreviewTransform);
	PreviewWithBackgroundCompute = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	PreviewWithBackgroundCompute->Setup(this->TargetWorld, this->OpFactory.Get());

	// For the first computation, display a bounding box with the working material. Otherwise it looks like nothing
	// is happening. And we don't want to copy over the potentially huge input mesh to be the preview mesh.
	FGridBoxMeshGenerator MeshGen;
	MeshGen.Box = FOrientedBox3d((FVector3d)Bounds.Origin, (FVector3d)Bounds.BoxExtent);
	MeshGen.Generate();
	FDynamicMesh3 BoxMesh(&MeshGen);
	PreviewWithBackgroundCompute->PreviewMesh->UpdatePreview(MoveTemp(BoxMesh));
	PreviewWithBackgroundCompute->PreviewMesh->SetTransform(FTransform(FVector(0, 2.5f * Bounds.BoxExtent.Y, 0)));

	PreviewWithBackgroundCompute->OnOpCompleted.AddLambda([this](const FDynamicMeshOperator* Op)
	{
		const GenerateStaticMeshLODAssetLocals::FGenerateStaticMeshLODAssetOperatorOp* GenerateLODOp =
			(const GenerateStaticMeshLODAssetLocals::FGenerateStaticMeshLODAssetOperatorOp*)(Op);
		check(GenerateLODOp);

		// Must happen on main thread
		FPhysicsDataCollection PhysicsData;
		PhysicsData.Geometry = GenerateLODOp->ResultCollision;
		PhysicsData.CopyGeometryToAggregate();
		UE::PhysicsTools::InitializePreviewGeometryLines(PhysicsData,
														 CollisionPreview,
														 CollisionVizSettings->Color, CollisionVizSettings->LineThickness, 0.0f, 16);

		// Must happen on main thread, and GenerateProcess might be in use by an Op somewhere else
		GenerateProcess->GraphEvalCriticalSection.Lock();

		UGenerateStaticMeshLODProcess::FPreviewMaterials PreviewMaterialSet;
		GenerateProcess->GetDerivedMaterialsPreview(PreviewMaterialSet);
		if (PreviewMaterialSet.Materials.Num() > 0)
		{
			PreviewTextures = PreviewMaterialSet.Textures;
			PreviewMaterials = PreviewMaterialSet.Materials;
			PreviewWithBackgroundCompute->PreviewMesh->SetMaterials(PreviewMaterials);
			BasicProperties->PreviewTextures = PreviewTextures;
		}

		GenerateProcess->GraphEvalCriticalSection.Unlock();
	});

	PreviewWithBackgroundCompute->ConfigureMaterials(
		ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	CollisionVizSettings = NewObject<UCollisionGeometryVisualizationProperties>(this);
	CollisionVizSettings->RestoreProperties(this);
	AddToolPropertySource(CollisionVizSettings);
	CollisionVizSettings->WatchProperty(CollisionVizSettings->LineThickness, [this](float NewValue) { bCollisionVisualizationDirty = true; });
	CollisionVizSettings->WatchProperty(CollisionVizSettings->Color, [this](FColor NewValue) { bCollisionVisualizationDirty = true; });
	CollisionVizSettings->WatchProperty(CollisionVizSettings->bShowHidden, [this](bool bNewValue) { bCollisionVisualizationDirty = true; });

	CollisionPreview = NewObject<UPreviewGeometry>(this);
	CollisionPreview->CreateInWorld(TargetWorld, PreviewTransform);

	// Pop up notifications for any warnings
	for ( const FProgressCancel::FMessageInfo& Warning : Progress.Warnings )
	{
		FNotificationInfo NotificationInfo(Warning.MessageText);
		NotificationInfo.ExpireDuration = 6.0f;
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); });
		NotificationInfo.HyperlinkText = LOCTEXT("ShowOutputLogHyperlink", "Show Output Log");
		FSlateNotificationManager::Get().AddNotification(NotificationInfo)->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void UGenerateStaticMeshLODAssetTool::OnSettingsModified()
{
	PreviewWithBackgroundCompute->InvalidateResult();
}

void UGenerateStaticMeshLODAssetTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);
	CollisionVizSettings->SaveProperties(this);

	CollisionPreview->Disconnect();
	CollisionPreview = nullptr;

	if (ShutdownType == EToolShutdownType::Accept)
	{
		if (BasicProperties->OutputMode == EGenerateLODAssetOutputMode::UpdateExistingAsset)
		{
			UpdateExistingAsset();
		}
		else
		{
			CreateNewAsset();
		}
	}

	FDynamicMeshOpResult Result = PreviewWithBackgroundCompute->Shutdown();
}

void UGenerateStaticMeshLODAssetTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


bool UGenerateStaticMeshLODAssetTool::CanAccept() const
{
	return (PreviewWithBackgroundCompute && PreviewWithBackgroundCompute->HaveValidResult());
}


void UGenerateStaticMeshLODAssetTool::OnTick(float DeltaTime)
{
	if (PreviewWithBackgroundCompute)
	{
		PreviewWithBackgroundCompute->Tick(DeltaTime);
	}

	if (bCollisionVisualizationDirty)
	{
		UpdateCollisionVisualization();
		bCollisionVisualizationDirty = false;
	}
}



void UGenerateStaticMeshLODAssetTool::UpdateCollisionVisualization()
{
	float UseThickness = CollisionVizSettings->LineThickness;
	FColor UseColor = CollisionVizSettings->Color;
	LineMaterial = ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager(), !CollisionVizSettings->bShowHidden);

	CollisionPreview->UpdateAllLineSets([&](ULineSetComponent* LineSet)
	{
		LineSet->SetAllLinesThickness(UseThickness);
		LineSet->SetAllLinesColor(UseColor);
	});
	CollisionPreview->SetAllLineSetsMaterial(LineMaterial);
}


void UGenerateStaticMeshLODAssetTool::CreateNewAsset()
{
	check(PreviewWithBackgroundCompute->HaveValidResult());
	GenerateProcess->CalculateDerivedPathName(BasicProperties->NewAssetName, BasicProperties->GeneratedSuffix);

	check(GenerateProcess->GraphEvalCriticalSection.TryLock());		// No ops should be running
	GenerateProcess->WriteDerivedAssetData();
	GenerateProcess->GraphEvalCriticalSection.Unlock();
}



void UGenerateStaticMeshLODAssetTool::UpdateExistingAsset()
{
	check(PreviewWithBackgroundCompute->HaveValidResult());
	GenerateProcess->CalculateDerivedPathName(BasicProperties->NewAssetName, BasicProperties->GeneratedSuffix);

	check(GenerateProcess->GraphEvalCriticalSection.TryLock());		// No ops should be running

	// only updated HD source if we have no HD source asset. Otherwise we are overwriting with existing lowpoly LOD0.
	bool bUpdateHDSource =
		BasicProperties->bSaveInputAsHiResSource &&
		(GenerateProcess->GetSourceStaticMesh()->IsHiResMeshDescriptionValid() == false);

	GenerateProcess->UpdateSourceAsset(bUpdateHDSource);
	GenerateProcess->GraphEvalCriticalSection.Unlock();
}







#undef LOCTEXT_NAMESPACE
