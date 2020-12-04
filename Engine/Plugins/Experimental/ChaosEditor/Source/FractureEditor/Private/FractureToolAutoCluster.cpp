// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolAutoCluster.h"

#include "FractureEditorStyle.h"
#include "FractureEditorCommands.h"

#include "AutoClusterFracture.h"

#include "GeometryCollection/GeometryCollectionComponent.h"

#define LOCTEXT_NAMESPACE "FractureAutoCluster"


UFractureToolAutoCluster::UFractureToolAutoCluster(const FObjectInitializer& ObjInit)
  : Super(ObjInit) 
{
	AutoClusterSettings = NewObject<UFractureAutoClusterSettings>(GetTransientPackage(), UFractureAutoClusterSettings::StaticClass());
	AutoClusterSettings->OwnerTool = this;
}


// UFractureTool Interface
FText UFractureToolAutoCluster::GetDisplayText() const
{
	return FText(NSLOCTEXT("FractureAutoCluster", "FractureToolAutoCluster", "Auto")); 
}


FText UFractureToolAutoCluster::GetTooltipText() const
{
	return FText(NSLOCTEXT("FractureAutoCluster", "FractureToolAutoClusterToolTip", "Automatically group together pieces of a fractured mesh (based on your settings) and assign them within the Geometry Collection.")); 
}

FText UFractureToolAutoCluster::GetApplyText() const 
{ 
	return FText(NSLOCTEXT("FractureAutoCluster", "ExecuteAutoCluster", "Auto Cluster")); 
}

FSlateIcon UFractureToolAutoCluster::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.AutoCluster");
}


TArray<UObject*> UFractureToolAutoCluster::GetSettingsObjects() const 
{
	TArray<UObject*> AllSettings; 
	AllSettings.Add(AutoClusterSettings);
	return AllSettings;
}

void UFractureToolAutoCluster::RegisterUICommand( FFractureEditorCommands* BindingContext )
{
	UI_COMMAND_EXT( BindingContext, UICommandInfo, "AutoCluster", "Auto", "Auto Cluster", EUserInterfaceActionType::ToggleButton, FInputChord() );
	BindingContext->AutoCluster = UICommandInfo;
}

void UFractureToolAutoCluster::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (InToolkit.IsValid())
	{
		TSet<UGeometryCollectionComponent*> GeomCompSelection;
		GetSelectedGeometryCollectionComponents(GeomCompSelection);
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
			int32 CurrentLevelView = EditBoneColor.GetViewLevel();

			UAutoClusterFractureCommand::ClusterChildBonesOfASingleMesh(GeometryCollectionComponent, AutoClusterSettings->AutoClusterMode, AutoClusterSettings->SiteCount);

			EditBoneColor.ResetBoneSelection();
			EditBoneColor.SetLevelViewMode(CurrentLevelView);

			GeometryCollectionComponent->MarkRenderDynamicDataDirty();
			GeometryCollectionComponent->MarkRenderStateDirty();
		}

		InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
	}
}

#undef LOCTEXT_NAMESPACE

