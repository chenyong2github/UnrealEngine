// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureEditorStyle.h"

#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"

FName FFractureEditorStyle::StyleName("FractureEditorStyle");

FFractureEditorStyle::FFractureEditorStyle()
	: FSlateStyleSet(StyleName)
{
	const FVector2D IconSize(20.0f, 20.0f);
	const FVector2D SmallIconSize(20.0f, 20.0f);
	const FVector2D LabelIconSize(16.0f, 16.0f);


	// const FVector2D Icon8x8(8.0f, 8.0f);
	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Experimental/ChaosEditor/Content"));
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	Set("FractureEditor.Slice", new FSlateImageBrush(RootToContentDir(TEXT("FractureSlice_48x.png")), IconSize));
	Set("FractureEditor.Slice.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSlice_48x.png")), SmallIconSize));
	Set("FractureEditor.Uniform", new FSlateImageBrush(RootToContentDir(TEXT("FractureUniformVoronoi_48x.png")), IconSize));
	Set("FractureEditor.Uniform.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureUniformVoronoi_48x.png")), SmallIconSize));
	Set("FractureEditor.Radial", new FSlateImageBrush(RootToContentDir(TEXT("FractureRadialVoronoi_48x.png")), IconSize));
	Set("FractureEditor.Radial.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureRadialVoronoi_48x.png")), SmallIconSize));
	Set("FractureEditor.Clustered", new FSlateImageBrush(RootToContentDir(TEXT("FractureClusteredVoronoi_48x.png")), IconSize));
	Set("FractureEditor.Clustered.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureClusteredVoronoi_48x.png")), SmallIconSize));
	Set("FractureEditor.Planar", new FSlateImageBrush(RootToContentDir(TEXT("FracturePlanar_48x.png")), IconSize));
	Set("FractureEditor.Planar.Small", new FSlateImageBrush(RootToContentDir(TEXT("FracturePlanar_48x.png")), SmallIconSize));
	Set("FractureEditor.Brick", new FSlateImageBrush(RootToContentDir(TEXT("FractureBrick_48x.png")), IconSize));
	Set("FractureEditor.Brick.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureBrick_48x.png")), SmallIconSize));
	Set("FractureEditor.Texture", new FSlateImageBrush(RootToContentDir(TEXT("FractureTexture_48x.png")), IconSize));
	Set("FractureEditor.Texture.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureTexture_48x.png")), SmallIconSize));

	// This is a bit of magic.  When you pass a command your Builder.AddToolBarButton, it will automatically try to find 
	// and Icon with the same name as the command and TCommand<> Context Name.  
	// format <Context>.<CommandName>[.Small]
	Set("FractureEditor.SelectAll", new FSlateImageBrush(RootToContentDir(TEXT("SelectAll_48x.png")), IconSize));
	Set("FractureEditor.SelectAll.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectAll_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectNone", new FSlateImageBrush(RootToContentDir(TEXT("DeselectAll_48x.png")), IconSize));
	Set("FractureEditor.SelectNone.Small", new FSlateImageBrush(RootToContentDir(TEXT("DeselectAll_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectNeighbors", new FSlateImageBrush(RootToContentDir(TEXT("SelectNeighbor_48x.png")), IconSize));
	Set("FractureEditor.SelectNeighbors.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectNeighbor_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectSiblings", new FSlateImageBrush(RootToContentDir(TEXT("SelectSiblings_48x.png")), IconSize));
	Set("FractureEditor.SelectSiblings.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectSiblings_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectAllInCluster", new FSlateImageBrush(RootToContentDir(TEXT("SelectAllInCluster_48x.png")), IconSize));
	Set("FractureEditor.SelectAllInCluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectAllInCluster_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectInvert", new FSlateImageBrush(RootToContentDir(TEXT("SelectInvert_48x.png")), IconSize));
	Set("FractureEditor.SelectInvert.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectInvert_48x.png")), SmallIconSize));

	Set("FractureEditor.AutoCluster", new FSlateImageBrush(RootToContentDir(TEXT("AutoCluster_48x.png")), IconSize));
	Set("FractureEditor.AutoCluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("AutoCluster_48x.png")), SmallIconSize));
	Set("FractureEditor.Cluster", new FSlateImageBrush(RootToContentDir(TEXT("Cluster_48x.png")), IconSize));
	Set("FractureEditor.Cluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("Cluster_48x.png")), SmallIconSize));
	Set("FractureEditor.Uncluster", new FSlateImageBrush(RootToContentDir(TEXT("Uncluster_48x.png")), IconSize));
	Set("FractureEditor.Uncluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("Uncluster_48x.png")), SmallIconSize));
	Set("FractureEditor.FlattenToLevel", new FSlateImageBrush(RootToContentDir(TEXT("FlattenToLevel_48x.png")), IconSize));
	Set("FractureEditor.FlattenToLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("FlattenToLevel_48x.png")), SmallIconSize));
	Set("FractureEditor.Flatten", new FSlateImageBrush(RootToContentDir(TEXT("Flatten_48x.png")), IconSize));
	Set("FractureEditor.Flatten.Small", new FSlateImageBrush(RootToContentDir(TEXT("Flatten_48x.png")), SmallIconSize));
	Set("FractureEditor.Merge", new FSlateImageBrush(RootToContentDir(TEXT("Merge_48x.png")), IconSize));
	Set("FractureEditor.Merge.Small", new FSlateImageBrush(RootToContentDir(TEXT("Merge_48x.png")), SmallIconSize));
	Set("FractureEditor.MoveUp", new FSlateImageBrush(RootToContentDir(TEXT("MoveUp_48x.png")), IconSize));
	Set("FractureEditor.MoveUp.Small", new FSlateImageBrush(RootToContentDir(TEXT("MoveUp_48x.png")), SmallIconSize));


	// View Settings
	Set("FractureEditor.Exploded", new FSlateImageBrush(RootToContentDir(TEXT("MiniExploded_32x.png")), LabelIconSize));
	Set("FractureEditor.Levels", new FSlateImageBrush(RootToContentDir(TEXT("MiniLevel_32x.png")), LabelIconSize));
	Set("FractureEditor.Visibility", new FSlateImageBrush(RootToContentDir(TEXT("GeneralVisibility_48x.png")), SmallIconSize));
	Set("FractureEditor.ToggleShowBoneColors", new FSlateImageBrush(RootToContentDir(TEXT("GeneralVisibility_48x.png")), IconSize));
	Set("FractureEditor.ToggleShowBoneColors.Small", new FSlateImageBrush(RootToContentDir(TEXT("GeneralVisibility_48x.png")), SmallIconSize));
	Set("FractureEditor.ViewUpOneLevel", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewUp_48x.png")), IconSize));
	Set("FractureEditor.ViewUpOneLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewUp_48x.png")), SmallIconSize));
	Set("FractureEditor.ViewDownOneLevel", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewDown_48x.png")), IconSize));
	Set("FractureEditor.ViewDownOneLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewDown_48x.png")), SmallIconSize));

	Set("FractureEditor.SpinBox", FSpinBoxStyle(FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
		.SetTextPadding(FMargin(0))
		.SetBackgroundBrush(FSlateNoResource())
		.SetHoveredBackgroundBrush(FSlateNoResource())
		.SetInactiveFillBrush(FSlateNoResource())
		.SetActiveFillBrush(FSlateNoResource())
		.SetForegroundColor(FSlateColor::UseSubduedForeground())
		.SetArrowsImage(FSlateNoResource())
	);

	// Set("FractureEditor.Exploded.Small", new FSlateImageBrush(RootToContentDir(TEXT("Exploded_32x.png")), SmallIconSize));

	Set("FractureEditor.GenerateAsset", new FSlateImageBrush(RootToContentDir(TEXT("GenerateAsset_48x.png")), IconSize));
	Set("FractureEditor.GenerateAsset.Small", new FSlateImageBrush(RootToContentDir(TEXT("GenerateAsset_48x.png")), SmallIconSize));


	Set("LevelEditor.FractureMode", new FSlateImageBrush(RootToContentDir(TEXT("FractureMode.png")), FVector2D(40.0f, 40.0f)));
	Set("LevelEditor.FractureMode.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureMode.png")), FVector2D(20.0f, 20.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FFractureEditorStyle::~FFractureEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FFractureEditorStyle& FFractureEditorStyle::Get()
{
	static FFractureEditorStyle Inst;
	return Inst;
}


