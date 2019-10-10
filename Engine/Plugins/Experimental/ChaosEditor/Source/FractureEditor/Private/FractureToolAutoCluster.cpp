// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FractureToolAutoCluster.h"

#include "FractureEditorStyle.h"
#include "FractureEditorCommands.h"

#include "AutoClusterFracture.h"

#include "GeometryCollection/GeometryCollectionComponent.h"

#define LOCTEXT_NAMESPACE "FractureAutoCluster"


UFractureToolAutoCluster::UFractureToolAutoCluster(const FObjectInitializer& ObjInit)
  : Super(ObjInit) 
{
	Settings = NewObject<UFractureAutoClusterSettings>(GetTransientPackage(), UFractureAutoClusterSettings::StaticClass());
}


// UFractureTool Interface
FText UFractureToolAutoCluster::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolAutoCluster", "Auto")); 
}


FText UFractureToolAutoCluster::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolAutoClusterToolTip", "Auto Cluster")); 
}

FText UFractureToolAutoCluster::GetApplyText() const 
{ 
	return FText(NSLOCTEXT("Fracture", "ExecuteAutoCluster", "Auto Cluster")); 
}

FSlateIcon UFractureToolAutoCluster::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.AutoCluster");
}


TArray<UObject*> UFractureToolAutoCluster::GetSettingsObjects() const 
{
	TArray<UObject*> AllSettings; 
	AllSettings.Add(Settings);
	return AllSettings;
}

void UFractureToolAutoCluster::RegisterUICommand( FFractureEditorCommands* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "AutoCluster", "Auto", "Auto Cluster", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->AutoCluster = UICommandInfo;
}

void UFractureToolAutoCluster::ExecuteFracture(const FFractureContext& FractureContext)
{
	if (UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(FractureContext.OriginalPrimitiveComponent))
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		int32 CurrentLevelView = EditBoneColor.GetViewLevel();

		UAutoClusterFractureCommand::ClusterChildBonesOfASingleMesh(GeometryCollectionComponent, Settings->AutoClusterMode, Settings->SiteCount);

		EditBoneColor.ResetBoneSelection();
		EditBoneColor.SetLevelViewMode(0);
		EditBoneColor.SetLevelViewMode(CurrentLevelView);
	}
}

#undef LOCTEXT_NAMESPACE

