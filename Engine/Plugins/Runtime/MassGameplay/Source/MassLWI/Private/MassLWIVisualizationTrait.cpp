// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLWIVisualizationTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "MassLWIRepresentationSubsystem.h"
#include "MassLWIRepresentationActorManagement.h"
//#include "MassInstancedStaticMeshComponent.h"


UMassLWIVisualizationTrait::UMassLWIVisualizationTrait(const FObjectInitializer& ObjectInitializer)
{
	bAllowServerSideVisualization = true;
	bCanModifyRepresentationSubsystemClass = false;
	RepresentationSubsystemClass = UMassLWIRepresentationSubsystem::StaticClass();
	Params.bCanModifyRepresentationActorManagementClass = false;
	Params.RepresentationActorManagementClass = UMassLWIRepresentationActorManagement::StaticClass();
}
