// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolUV.h"

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
// for content-browser things
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "Misc/ScopedSlowTask.h"

#include "FractureAutoUV.h"

#define LOCTEXT_NAMESPACE "FractureToolAutoUV"

using namespace UE::Geometry;

UFractureToolAutoUV::UFractureToolAutoUV(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	AutoUVSettings = NewObject<UFractureAutoUVSettings>(GetTransientPackage(), UFractureAutoUVSettings::StaticClass());
	AutoUVSettings->OwnerTool = this;
}

bool UFractureToolAutoUV::CanExecute() const
{
	if (!IsGeometryCollectionSelected())
	{
		return false;
	}

	return true;
}

FText UFractureToolAutoUV::GetDisplayText() const
{
	return FText(LOCTEXT("FractureToolAutoUV", "AutoUV Fracture"));
}

FText UFractureToolAutoUV::GetTooltipText() const
{
	return FText(LOCTEXT("FractureToolAutoUVTooltip", "This enables you to automatically layout UVs for internal fracture pieces, and procedurally fill a corresponding texture."));
}

FSlateIcon UFractureToolAutoUV::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.AutoUV");
}

void UFractureToolAutoUV::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "AutoUV", "AutoUV", "Autogenerate UV and texture for internal faces", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->AutoUV = UICommandInfo;
}

TArray<UObject*> UFractureToolAutoUV::GetSettingsObjects() const
{
	TArray<UObject*> Settings;
	Settings.Add(AutoUVSettings);
	return Settings;
}

void UFractureToolAutoUV::FractureContextChanged()
{
}

void UFractureToolAutoUV::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
}

void UFractureToolAutoUV::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// update any cached data 
}

TArray<FFractureToolContext> UFractureToolAutoUV::GetFractureToolContexts() const
{
	TArray<FFractureToolContext> Contexts;

	// A context is gathered for each selected GeometryCollection component, or for each individual bone if Group Fracture is not used.
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		// Generate a context for each selected node
		FFractureToolContext FullSelection(GeometryCollectionComponent);
		FullSelection.ConvertSelectionToRigidNodes();
		
		// Update global transforms and bounds -- TODO: pull this bounds update out to a shared function?
		const TManagedArray<FTransform>& Transform = FullSelection.GetGeometryCollection()->Transform;
		const TManagedArray<int32>& TransformToGeometryIndex = FullSelection.GetGeometryCollection()->TransformToGeometryIndex;
		const TManagedArray<FBox>& BoundingBoxes = FullSelection.GetGeometryCollection()->BoundingBox;

		TArray<FTransform> Transforms;
		GeometryCollectionAlgo::GlobalMatrices(Transform, FullSelection.GetGeometryCollection()->Parent, Transforms);

		FBox Bounds(ForceInit);
		for (int32 BoneIndex : FullSelection.GetSelection())
		{
			int32 GeometryIndex = TransformToGeometryIndex[BoneIndex];
			if (GeometryIndex > INDEX_NONE)
			{
				FBox BoneBound = BoundingBoxes[GeometryIndex].TransformBy(Transforms[BoneIndex]);
				Bounds += BoneBound;
			}
		}
		FullSelection.SetBounds(Bounds);

		Contexts.Add(FullSelection);
	}

	return Contexts;
}

bool UFractureToolAutoUV::SaveGeneratedTexture(UTexture2D* GeneratedTexture, FString ObjectBaseName, const UObject* RelativeToAsset, bool bPromptToSave)
{
	check(RelativeToAsset);
	check(GeneratedTexture);
	check(GeneratedTexture->GetOuter() == GetTransientPackage());
	check(GeneratedTexture->Source.IsValid());	// texture needs to have valid source data to be savd

	// find path to reference asset
	UPackage* AssetOuterPackage = CastChecked<UPackage>(RelativeToAsset->GetOuter());
	FString AssetPackageName = AssetOuterPackage->GetName();
	FString PackageFolderPath = FPackageName::GetLongPackagePath(AssetPackageName);

	// Show the modal dialog and then get the path/name.
	// If the user cancels, texture is left as transient
	if (bPromptToSave)
	{
		IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

		FString UseDefaultAssetName = ObjectBaseName;
		FString CurrentPath = PackageFolderPath;
		if (CurrentPath.IsEmpty() == false)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			FString UnusedPackageName;
			AssetToolsModule.Get().CreateUniqueAssetName(PackageFolderPath + TEXT("/") + ObjectBaseName, TEXT(""), UnusedPackageName, UseDefaultAssetName);
		}

		FSaveAssetDialogConfig Config;
		Config.DefaultAssetName = UseDefaultAssetName;
		Config.DialogTitleOverride = LOCTEXT("GenerateTexture2DAssetPathDialogWarning", "Choose Folder Path and Name for New Asset. Cancel to Discard New Asset.");
		Config.DefaultPath = CurrentPath;
		FString SelectedPath = ContentBrowser.CreateModalSaveAssetDialog(Config);

		if (SelectedPath.IsEmpty() == false)
		{
			PackageFolderPath = FPaths::GetPath(SelectedPath);
			ObjectBaseName = FPaths::GetBaseFilename(SelectedPath, true);
		}
		else
		{
			return false;
		}
	}

	FString UseBaseName = ObjectBaseName;

	// create new package
	FString UniqueAssetName;
	FString UniquePackageName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(PackageFolderPath + TEXT("/") + ObjectBaseName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(*UniquePackageName);
	UPackage* AssetPackage = CreatePackage(*UniquePackageName);

	// move texture from Transient package to real package
	GeneratedTexture->Rename(*UniqueAssetName, AssetPackage, REN_None);
	// remove transient flag, add public/standalone/transactional
	GeneratedTexture->ClearFlags(RF_Transient);
	GeneratedTexture->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	// mark things as modified / dirtied
	GeneratedTexture->Modify();
	GeneratedTexture->UpdateResource();
	GeneratedTexture->PostEditChange();
	GeneratedTexture->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(GeneratedTexture);

	return true;
}


int32 UFractureToolAutoUV::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.IsValid())
	{
		FScopedSlowTask UVTask(3, LOCTEXT("StartingAutoUV", "Automatically laying out and texturing internal surfaces"));
		UVTask.MakeDialog();

		FGeometryCollection& Collection = *FractureContext.GetGeometryCollection();
		
		int32 OutputRes = (int32)AutoUVSettings->Resolution;

		UVTask.EnterProgressFrame(1, LOCTEXT("LayOutUVIslands", "Laying out UV islands"));
		if (!UE::PlanarCut::UVLayout(Collection, OutputRes, AutoUVSettings->GutterSize))
		{
			// failed to do layout
			return INDEX_NONE;
		}

		UVTask.EnterProgressFrame(1, LOCTEXT("TexturingSurfaces", "Texturing internal surfaces"));

		FTexture2DBuilder TextureBuilder;
		FImageDimensions Dimensions(OutputRes, OutputRes);
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, Dimensions);

		UE::Geometry::TImageBuilder<UE::Geometry::FVector3f> ImageBuilder;
		ImageBuilder.SetDimensions(Dimensions);
		ImageBuilder.Clear(UE::Geometry::FVector3f(0, 0, 0));

		UE::PlanarCut::TextureInternalSurfaces(Collection, AutoUVSettings->MaxDistance, FMath::CeilToInt(AutoUVSettings->GutterSize), ImageBuilder);

		UVTask.EnterProgressFrame(1, LOCTEXT("SavingTexture", "Saving result"));

		TextureBuilder.Copy(ImageBuilder);
		TextureBuilder.Commit(false);
		AutoUVSettings->Result = TextureBuilder.GetTexture2D();

		// choose default texture name based on corresponding geometry collection name
		FString BaseName = FractureContext.GetFracturedGeometryCollection()->GetName();
		FTexture2DBuilder::CopyPlatformDataToSourceData(AutoUVSettings->Result, FTexture2DBuilder::ETextureType::Color);
		SaveGeneratedTexture(AutoUVSettings->Result, FString::Printf(TEXT("%s_AutoUV"), *BaseName), FractureContext.GetFracturedGeometryCollection(), AutoUVSettings->bPromptToSave);
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
