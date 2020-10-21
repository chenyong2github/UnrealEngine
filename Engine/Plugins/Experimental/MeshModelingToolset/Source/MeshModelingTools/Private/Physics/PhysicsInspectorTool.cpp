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


#define LOCTEXT_NAMESPACE "UPhysicsInspectorTool"



bool UPhysicsInspectorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	int32 NumStaticMeshes = ToolBuilderUtil::CountComponents(SceneState, [&](UActorComponent* Comp) { return Cast<UStaticMeshComponent>(Comp) != nullptr; });
	int32 NumComponentTargets = ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget);
	return (NumStaticMeshes > 0 && NumStaticMeshes == NumComponentTargets);
}


UInteractiveTool* UPhysicsInspectorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPhysicsInspectorTool* NewTool = NewObject<UPhysicsInspectorTool>(SceneState.ToolManager);

	TArray<UActorComponent*> ValidComponents = ToolBuilderUtil::FindAllComponents(SceneState,
		[&](UActorComponent* Comp) { return Cast<UStaticMeshComponent>(Comp) != nullptr; });
	check(ValidComponents.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : ValidComponents)
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ActorComponent);
		if (MeshComponent)
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
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

	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		const UStaticMeshComponent* Component = CastChecked<UStaticMeshComponent>(ComponentTarget->GetOwnerComponent());
		const UStaticMesh* StaticMesh = Component->GetStaticMesh();
		if (ensure(StaticMesh && StaticMesh->GetBodySetup()))
		{
			TSharedPtr<FPhysicsDataCollection> PhysicsData = MakeShared<FPhysicsDataCollection>();
			PhysicsData->SourceComponent = Component;
			PhysicsData->BodySetup = StaticMesh->GetBodySetup();
			PhysicsData->AggGeom = StaticMesh->GetBodySetup()->AggGeom;

			PhysicsInfos.Add(PhysicsData);

			UPreviewGeometry* PreviewGeom = NewObject<UPreviewGeometry>(this);
			FTransform TargetTransform = ComponentTarget->GetWorldTransform();
			PhysicsData->ExternalScale3D = TargetTransform.GetScale3D();
			TargetTransform.SetScale3D(FVector::OneVector);
			PreviewGeom->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), TargetTransform);
			PreviewElements.Add(PreviewGeom);

			InitializeGeometry(*PhysicsData, PreviewGeom);

			UPhysicsObjectToolPropertySet* ObjectProps = NewObject<UPhysicsObjectToolPropertySet>(this);
			InitializeObjectProperties(*PhysicsData, ObjectProps);
			AddToolPropertySource(ObjectProps);
		}
	}


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