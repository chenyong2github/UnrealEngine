// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/PhysicsInspectorTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "Drawing/PreviewGeometryActor.h"

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryVisualization.h"

// physics data
#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"
#include "Engine/Classes/PhysicsEngine/AggregateGeom.h"

#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ToolTargetManager.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UPhysicsInspectorTool"


const FToolTargetTypeRequirements& UPhysicsInspectorToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UPrimitiveComponentBackedTarget::StaticClass(),
		UStaticMeshBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UPhysicsInspectorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) > 0;
}


UInteractiveTool* UPhysicsInspectorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPhysicsInspectorTool* NewTool = NewObject<UPhysicsInspectorTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));

	return NewTool;
}


void UPhysicsInspectorTool::Setup()
{
	UInteractiveTool::Setup();

	VizSettings = NewObject<UCollisionGeometryVisualizationProperties>(this);
	VizSettings->RestoreProperties(this);
	AddToolPropertySource(VizSettings);
	VizSettings->WatchProperty(VizSettings->LineThickness, [this](float NewValue) { bVisualizationDirty = true; });
	VizSettings->WatchProperty(VizSettings->Color, [this](FColor NewValue) { bVisualizationDirty = true; });
	VizSettings->WatchProperty(VizSettings->bShowHidden, [this](bool bNewValue) { bVisualizationDirty = true; });

	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(ComponentIdx);
		const UStaticMeshComponent* Component = CastChecked<UStaticMeshComponent>(TargetComponent->GetOwnerComponent());
		const UStaticMesh* StaticMesh = Component->GetStaticMesh();
		if (ensure(StaticMesh && StaticMesh->GetBodySetup()))
		{
			TSharedPtr<FPhysicsDataCollection> PhysicsData = MakeShared<FPhysicsDataCollection>();
			PhysicsData->SourceComponent = Component;
			PhysicsData->BodySetup = StaticMesh->GetBodySetup();
			PhysicsData->AggGeom = StaticMesh->GetBodySetup()->AggGeom;

			PhysicsInfos.Add(PhysicsData);

			UPreviewGeometry* PreviewGeom = NewObject<UPreviewGeometry>(this);
			FTransform TargetTransform = TargetComponent->GetWorldTransform();
			PhysicsData->ExternalScale3D = TargetTransform.GetScale3D();
			TargetTransform.SetScale3D(FVector::OneVector);
			PreviewGeom->CreateInWorld(TargetComponent->GetOwnerActor()->GetWorld(), TargetTransform);
			PreviewElements.Add(PreviewGeom);

			InitializeGeometry(*PhysicsData, PreviewGeom);

			UPhysicsObjectToolPropertySet* ObjectProps = NewObject<UPhysicsObjectToolPropertySet>(this);
			InitializeObjectProperties(*PhysicsData, ObjectProps);
			AddToolPropertySource(ObjectProps);
		}
	}

	SetToolDisplayName(LOCTEXT("ToolName", "Physics Inspector"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Inspect Physics data for the seleced Static Meshes"),
		EToolMessageLevel::UserNotification);
}


void UPhysicsInspectorTool::Shutdown(EToolShutdownType ShutdownType)
{
	VizSettings->SaveProperties(this);

	for (UPreviewGeometry* Preview : PreviewElements)
	{
		Preview->Disconnect();
	}
}





void UPhysicsInspectorTool::OnTick(float DeltaTime)
{
	if (bVisualizationDirty)
	{
		UpdateVisualization();
		bVisualizationDirty = false;
	}
}



void UPhysicsInspectorTool::UpdateVisualization()
{
	float UseThickness = VizSettings->LineThickness;
	FColor UseColor = VizSettings->Color;
	LineMaterial = ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager(), !VizSettings->bShowHidden);

	for (UPreviewGeometry* Preview : PreviewElements)
	{
		Preview->UpdateAllLineSets([&](ULineSetComponent* LineSet)
		{
			LineSet->SetAllLinesThickness(UseThickness);
			LineSet->SetAllLinesColor(UseColor);
		});
		Preview->SetAllLineSetsMaterial(LineMaterial);
	}
}



void UPhysicsInspectorTool::InitializeObjectProperties(const FPhysicsDataCollection& PhysicsData, UPhysicsObjectToolPropertySet* PropSet)
{
	UE::PhysicsTools::InitializePhysicsToolObjectPropertySet(&PhysicsData, PropSet);
}



void UPhysicsInspectorTool::InitializeGeometry(const FPhysicsDataCollection& PhysicsData, UPreviewGeometry* PreviewGeom)
{
	UE::PhysicsTools::InitializePreviewGeometryLines(PhysicsData, PreviewGeom,
		VizSettings->Color, VizSettings->LineThickness, 0.0f, 16);
}



#undef LOCTEXT_NAMESPACE