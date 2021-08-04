// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorCompiler.h"
#include "DisplayClusterConfiguratorUtils.h"

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterRootActor.h"
#include "Blueprints/DisplayClusterBlueprint.h"
#include "Blueprints/DisplayClusterBlueprintGeneratedClass.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Camera/CameraComponent.h"

#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/KismetReinstanceUtilities.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfiguratorCompiler"


bool FDisplayClusterConfiguratorKismetCompiler::CanCompile(const UBlueprint* Blueprint)
{
	return Blueprint->IsA<UDisplayClusterBlueprint>();
}

void FDisplayClusterConfiguratorKismetCompiler::Compile(UBlueprint* Blueprint,
	const FKismetCompilerOptions& CompileOptions, FCompilerResultsLog& Results)
{
	FDisplayClusterConfiguratorKismetCompilerContext Compiler(Blueprint, Results, CompileOptions);
	Compiler.Compile();
}

bool FDisplayClusterConfiguratorKismetCompiler::GetBlueprintTypesForClass(UClass* ParentClass,
	UClass*& OutBlueprintClass, UClass*& OutBlueprintGeneratedClass) const
{
	if (ParentClass && ParentClass->IsChildOf<ADisplayClusterRootActor>())
	{
		OutBlueprintClass = UDisplayClusterBlueprint::StaticClass();
		OutBlueprintGeneratedClass = UDisplayClusterBlueprintGeneratedClass::StaticClass();
		return true;
	}

	return false;
}

FDisplayClusterConfiguratorKismetCompilerContext::FDisplayClusterConfiguratorKismetCompilerContext(UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog,
	const FKismetCompilerOptions& InCompilerOptions) : FKismetCompilerContext(InBlueprint, InMessageLog,
	                                                                          InCompilerOptions), DCGeneratedBP(nullptr)
{
}

void FDisplayClusterConfiguratorKismetCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	DCGeneratedBP = FindObject<UDisplayClusterBlueprintGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);

	if (DCGeneratedBP == nullptr)
	{
		DCGeneratedBP = NewObject<UDisplayClusterBlueprintGeneratedClass>(Blueprint->GetOutermost(), *NewClassName, RF_Public | RF_Transactional);
	}
	else
	{
		// Already existed, but wasn't linked in the Blueprint yet due to load ordering issues
		FBlueprintCompileReinstancer::Create(DCGeneratedBP);
	}
	NewClass = DCGeneratedBP;
}

void FDisplayClusterConfiguratorKismetCompilerContext::OnNewClassSet(UBlueprintGeneratedClass* ClassToUse)
{
	DCGeneratedBP = CastChecked<UDisplayClusterBlueprintGeneratedClass>(ClassToUse);
}

void FDisplayClusterConfiguratorKismetCompilerContext::PreCompile()
{
	Super::PreCompile();
	ValidateConfiguration();
}

void FDisplayClusterConfiguratorKismetCompilerContext::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	Super::CopyTermDefaultsToDefaultObject(DefaultObject);

	UDisplayClusterBlueprint* DCBlueprint = CastChecked<UDisplayClusterBlueprint>(Blueprint);
	if (DCBlueprint->HasAnyFlags(RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_NeedInitialization))
	{
		return;
	}
	
	if (Blueprint->bIsNewlyCreated)
	{
		ADisplayClusterRootActor* RootActor = CastChecked<ADisplayClusterRootActor>(DefaultObject);
		RootActor->PreviewNodeId = DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll;
	}
}

void FDisplayClusterConfiguratorKismetCompilerContext::ValidateConfiguration()
{
	UDisplayClusterBlueprint* DCBlueprint = CastChecked<UDisplayClusterBlueprint>(Blueprint);
	if (Blueprint->bIsNewlyCreated)
	{
		return;
	}
	
	UDisplayClusterConfigurationData* BlueprintData = DCBlueprint->GetOrLoadConfig();
	if (!BlueprintData)
	{
		MessageLog.Error(*LOCTEXT("NoConfigError", "Critical Error: Configuration data not found!").ToString());
		return;
	}

	if (!BlueprintData->Cluster)
	{
		MessageLog.Error(*LOCTEXT("NoClusterError", "No cluster information found!").ToString());
		return;
	}

	if (BlueprintData->Cluster->Nodes.Num() == 0)
	{
		MessageLog.Warning(*LOCTEXT("NoClusterNodesWarning", "No cluster nodes found. Please add a cluster node.").ToString());
		return;
	}
	
	if (!FDisplayClusterConfiguratorUtils::IsMasterNodeInConfig(BlueprintData))
	{
		MessageLog.Warning(*LOCTEXT("NoMasterNodeWarning", "Master cluster node not set. Please set a master node.").ToString());
	}

	bool bAtLeastOneViewportFound = false;

	for (const auto& ClusterNodes : BlueprintData->Cluster->Nodes)
	{
		if (ClusterNodes.Value->Viewports.Num() > 0)
		{
			// Pass with at least one set.
			bAtLeastOneViewportFound = true;
			
			for (const auto& Viewport : ClusterNodes.Value->Viewports)
			{
				if (Viewport.Value->ProjectionPolicy.Type.IsEmpty())
				{
					MessageLog.Warning(*LOCTEXT("NoPolicyError", "No projection policy assigned to viewport @@.").ToString(), Viewport.Value);
				}
			}
		}
	}

	if (!bAtLeastOneViewportFound)
	{
		MessageLog.Warning(*LOCTEXT("NoViewportsError", "No viewports found. Please add a viewport.").ToString());
	}
}

void FDisplayClusterConfiguratorKismetCompilerContext::OnPostCDOCompiled()
{
	// Clear the undo buffer; since the root actor's CDO was just recreated, any pointers to its UObject children such as CurrentConfigData
	// are now stale, such as those that may be in the undo buffer. This is a temporary measure since UE's undo system doesn't currently easily
	// support UObject subobjects that aren't flagged as DefaultSubObject, and can't handle TMaps of subobjects well.
	GEditor->ResetTransaction(LOCTEXT("ResetUndoBufferOnCompile", "nDisplay Config Compiled"));
}

#undef LOCTEXT_NAMESPACE
