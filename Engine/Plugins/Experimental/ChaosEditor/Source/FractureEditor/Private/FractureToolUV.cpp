// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolUV.h"

#include "FractureToolContext.h"

#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
// for content-browser things
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"

#include "FractureAutoUV.h"

#define LOCTEXT_NAMESPACE "FractureToolAutoUV"

using namespace UE::Geometry;


void UFractureAutoUVSettings::SetNumUVChannels(int32 NumUVChannels)
{
	NumUVChannels = FMath::Clamp<int32>(NumUVChannels, 1, GeometryCollectionUV::MAX_NUM_UV_CHANNELS);
	UVChannelNamesList.Reset();
	for (int32 k = 0; k < NumUVChannels; ++k)
	{
		UVChannelNamesList.Add(FString::Printf(TEXT("UV %d"), k));
	}
	if (GetSelectedChannelIndex(false) == INDEX_NONE)
	{
		UVChannel = UVChannelNamesList[0];
	}
}

int32 UFractureAutoUVSettings::GetSelectedChannelIndex(bool bForceToZeroOnFailure)
{
	int32 FoundIndex = UVChannelNamesList.IndexOfByKey(UVChannel);
	if (FoundIndex == INDEX_NONE && bForceToZeroOnFailure)
	{
		FoundIndex = 0;
	}
	return FoundIndex;
}

void UFractureAutoUVSettings::ChangeNumUVChannels(int32 Delta)
{
	int32 Target = UVChannelNamesList.Num() + Delta;
	if (Target > 0 && Target < GeometryCollectionUV::MAX_NUM_UV_CHANNELS)
	{
		UFractureToolAutoUV* AutoUVTool = Cast<UFractureToolAutoUV>(OwnerTool.Get());
		AutoUVTool->UpdateUVChannels(Target);
	}
}

void UFractureAutoUVSettings::DisableBoneColors()
{
	UFractureToolAutoUV* AutoUVTool = Cast<UFractureToolAutoUV>(OwnerTool.Get());
	AutoUVTool->DisableBoneColors();
}

void UFractureAutoUVSettings::BoxProjectUVs()
{
	UFractureToolAutoUV* AutoUVTool = Cast<UFractureToolAutoUV>(OwnerTool.Get());
	AutoUVTool->BoxProjectUVs();
}

void UFractureAutoUVSettings::LayoutUVs()
{
	UFractureToolAutoUV* AutoUVTool = Cast<UFractureToolAutoUV>(OwnerTool.Get());
	AutoUVTool->LayoutUVs();
}

void UFractureAutoUVSettings::BakeTexture()
{
	UFractureToolAutoUV* AutoUVTool = Cast<UFractureToolAutoUV>(OwnerTool.Get());
	AutoUVTool->BakeTexture();
}

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
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "AutoUV", "AutoUV", "Autogenerate UVs and textures for geometry collections (especially for internal fracture surfaces).", EUserInterfaceActionType::ToggleButton, FInputChord());
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
	UpdateUVChannels(-1);
}

void UFractureToolAutoUV::UpdateUVChannels(int32 TargetNumUVChannels)
{
	int32 MinUVChannels = GeometryCollectionUV::MAX_NUM_UV_CHANNELS;

	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		int32 NumChannels = GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection()->NumUVLayers();
		MinUVChannels = FMath::Min(MinUVChannels, NumChannels);
	}

	if (TargetNumUVChannels > -1 && TargetNumUVChannels != MinUVChannels)
	{
		FScopedTransaction Transaction(LOCTEXT("UpdateUVChannels", "Update UV Channels"), !GeomCompSelection.IsEmpty());

		bool bIsIncreasing = TargetNumUVChannels > MinUVChannels;
		for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
		{
			int32 NumChannels = GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection()->NumUVLayers();
			// not if num channels is decreasing, all channels will be >= min channels so we apply the size change to all
			// but if we're increasing the number of channels, some geometry collections may have more channels already and we leave those alone
			if (NumChannels < TargetNumUVChannels || !bIsIncreasing)
			{
				FGeometryCollectionEdit Edit(GeometryCollectionComponent, GeometryCollection::EEditUpdate::Rest, true);
				GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection()->SetNumUVLayers(TargetNumUVChannels);
				if (bIsIncreasing)
				{
					// Copy UV layer 0 into new UV layer
					FGeometryCollection& Collection = *GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection();
					for (int32 Idx = 0; Idx < Collection.UVs.Num(); Idx++)
					{
						for (int32 Ch = NumChannels; Ch < TargetNumUVChannels; Ch++)
						{
							Collection.UVs[Idx][Ch] = Collection.UVs[Idx][0];
						}
					}
				}
				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();
			}
		}
		MinUVChannels = TargetNumUVChannels;
	}

	AutoUVSettings->SetNumUVChannels(MinUVChannels);
}

void UFractureToolAutoUV::DisableBoneColors()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	FScopedTransaction Transaction(LOCTEXT("DisableBoneColors", "Disable Bone Colors"), !GeomCompSelection.IsEmpty());
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		GeometryCollectionComponent->Modify();
		GeometryCollectionComponent->SetShowBoneColors(false);
		GeometryCollectionComponent->MarkRenderStateDirty();
	}
}

void UFractureToolAutoUV::BoxProjectUVs()
{
	int32 UVLayer = AutoUVSettings->GetSelectedChannelIndex();

	TArray<int32> EmptyMaterialIDs;
	UE::PlanarCut::EUseMaterials UseMaterialIDs =
		AutoUVSettings->TargetMaterialIDs == ETargetMaterialIDs::SelectedIDs ? UE::PlanarCut::EUseMaterials::NoDefaultMaterials :
		(AutoUVSettings->TargetMaterialIDs == ETargetMaterialIDs::AllIDs ? UE::PlanarCut::EUseMaterials::AllMaterials : UE::PlanarCut::EUseMaterials::OddMaterials);

	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	FScopedTransaction Transaction(LOCTEXT("BoxProjectUVs", "Box Project UVs"), !GeomCompSelection.IsEmpty());
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit Edit(GeometryCollectionComponent, GeometryCollection::EEditUpdate::Rest, true);
		UE::PlanarCut::BoxProjectUVs(UVLayer,
			*GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection(),
			(FVector3d)AutoUVSettings->ProjectionScale,
			UseMaterialIDs,
			AutoUVSettings->TargetMaterialIDs == ETargetMaterialIDs::OddIDs ? EmptyMaterialIDs : AutoUVSettings->MaterialIDs);

		GeometryCollectionComponent->MarkRenderDynamicDataDirty();
		GeometryCollectionComponent->MarkRenderStateDirty();
	}
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


bool UFractureToolAutoUV::SaveGeneratedTexture(UE::Geometry::TImageBuilder<FVector4f>& ImageBuilder, FString ObjectBaseName, const UObject* RelativeToAsset, bool bPromptToSave, bool bAllowReplace)
{
	check(RelativeToAsset);

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
		if (!CurrentPath.IsEmpty() && !bAllowReplace)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			FString UnusedPackageName;
			AssetToolsModule.Get().CreateUniqueAssetName(FPaths::Combine(PackageFolderPath, ObjectBaseName), TEXT(""), UnusedPackageName, UseDefaultAssetName);
		}

		FSaveAssetDialogConfig Config;
		Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
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

	FString NewAssetPath = FPaths::Combine(PackageFolderPath, ObjectBaseName);

	UTexture2D* GeneratedTexture = nullptr;
	bool bNeedsNewPackage = true;
	if (bAllowReplace)
	{
		// Modifying the static mesh in place. Delete existing asset so that we can have a clean duplicate
		bool bNewAssetExists = UEditorAssetLibrary::DoesAssetExist(NewAssetPath);
		if (bNewAssetExists)
		{
			UObject* OldObject = UEditorAssetLibrary::LoadAsset(NewAssetPath);
			if (UTexture2D* OldTexture = Cast<UTexture2D>(OldObject))
			{
				FTexture2DBuilder TextureBuilder;
				TextureBuilder.InitializeAndReplaceExistingTexture(OldTexture, FTexture2DBuilder::ETextureType::Color, ImageBuilder.GetDimensions());
				TextureBuilder.Copy(ImageBuilder);
				TextureBuilder.Commit(false);
				GeneratedTexture = TextureBuilder.GetTexture2D();
				FTexture2DBuilder::CopyPlatformDataToSourceData(GeneratedTexture, FTexture2DBuilder::ETextureType::Color);
				bNeedsNewPackage = false;
			}
			else // old asset was wrong type; delete to replace
			{
				bool bDeleteOK = UEditorAssetLibrary::DeleteAsset(NewAssetPath);
				ensure(bDeleteOK);
			}
		}
	}
	if (!GeneratedTexture)
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, ImageBuilder.GetDimensions());
		TextureBuilder.Copy(ImageBuilder);
		TextureBuilder.Commit(false);
		GeneratedTexture = TextureBuilder.GetTexture2D();
		FTexture2DBuilder::CopyPlatformDataToSourceData(GeneratedTexture, FTexture2DBuilder::ETextureType::Color);
	}
	AutoUVSettings->Result = GeneratedTexture;
	check(GeneratedTexture);
	check(GeneratedTexture->Source.IsValid());	// texture needs to have valid source data to be savd

	if (bNeedsNewPackage)
	{
		check(GeneratedTexture->GetOuter() == GetTransientPackage());

		// create new package
		FString UniqueAssetName;
		FString UniquePackageName;

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath, TEXT(""), UniquePackageName, UniqueAssetName);

		UPackage* AssetPackage = CreatePackage(*UniquePackageName);

		// move texture from Transient package to real package
		GeneratedTexture->Rename(*UniqueAssetName, AssetPackage, REN_None);
	}
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

namespace
{
	UE::PlanarCut::EUseMaterials GetUseMaterials(ETargetMaterialIDs TargetIDs)
	{
		return TargetIDs == ETargetMaterialIDs::SelectedIDs ? UE::PlanarCut::EUseMaterials::NoDefaultMaterials :
			(TargetIDs == ETargetMaterialIDs::AllIDs ? UE::PlanarCut::EUseMaterials::AllMaterials : UE::PlanarCut::EUseMaterials::OddMaterials);
	}
}

void UFractureToolAutoUV::LayoutUVs()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	FScopedTransaction Transaction(LOCTEXT("LayoutUVs", "Layout UVs"), !GeomCompSelection.IsEmpty());
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		FGeometryCollectionEdit Edit(GeometryCollectionComponent, GeometryCollection::EEditUpdate::None);
		Edit.GetRestCollection()->Modify();
		LayoutUVsForComponent(GeometryCollectionComponent);

		GeometryCollectionComponent->MarkRenderDynamicDataDirty();
		GeometryCollectionComponent->MarkRenderStateDirty();
	}
}


bool UFractureToolAutoUV::LayoutUVsForComponent(UGeometryCollectionComponent* Component)
{
	int32 OutputRes = (int32)AutoUVSettings->Resolution;
	TArray<int32> EmptyMaterialIDs;

	int32 UVLayer = AutoUVSettings->GetSelectedChannelIndex();

	UE::PlanarCut::EUseMaterials UseMaterialIDs = GetUseMaterials(AutoUVSettings->TargetMaterialIDs);

	bool bLayoutSuccess =
		UE::PlanarCut::UVLayout(UVLayer, *Component->GetRestCollection()->GetGeometryCollection(), OutputRes, AutoUVSettings->GutterSize,
			UseMaterialIDs,
			AutoUVSettings->TargetMaterialIDs == ETargetMaterialIDs::OddIDs ? EmptyMaterialIDs : AutoUVSettings->MaterialIDs);
	
	return bLayoutSuccess;
}


void UFractureToolAutoUV::BakeTexture()
{
	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);

	FScopedTransaction Transaction(LOCTEXT("BakeTexture", "Bake Texture"), !GeomCompSelection.IsEmpty());
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		BakeTextureForComponent(GeometryCollectionComponent);
	}
}

void UFractureToolAutoUV::BakeTextureForComponent(UGeometryCollectionComponent* Component, TFunction<void(int32, const FText&)> Progress)
{
	FGeometryCollection& Collection = *Component->GetRestCollection()->GetGeometryCollection();

	int32 OutputRes = (int32)AutoUVSettings->Resolution;
	TArray<int32> EmptyMaterialIDs;

	int32 UVLayer = AutoUVSettings->GetSelectedChannelIndex();

	UE::PlanarCut::EUseMaterials UseMaterialIDs = GetUseMaterials(AutoUVSettings->TargetMaterialIDs);

	FImageDimensions Dimensions(OutputRes, OutputRes);

	UE::Geometry::TImageBuilder<FVector4f> ImageBuilder;
	ImageBuilder.SetDimensions(Dimensions);
	ImageBuilder.Clear(FVector4f(0, 0, 0, 0));

	typedef UE::PlanarCut::EBakeAttributes EBakeAttributes;
	FIndex4i Attributes;
	// Note: Ordering of these attributes should match the order and comments in the AutoUVSettings struct
	//		 Update the order and comments there if you change the ordering here.
	if (AutoUVSettings->BakeTextureType == ETextureType::ThicknessAndSurfaceAttributes)
	{
		Attributes = FIndex4i(
			AutoUVSettings->bDistToOuter ? (int32)EBakeAttributes::DistanceToExternal : 0,
			AutoUVSettings->bAmbientOcclusion ? (int32)EBakeAttributes::AmbientOcclusion : 0,
			AutoUVSettings->bSmoothedCurvature ? (int32)EBakeAttributes::Curvature : 0,
			0
		);
	}
	else if (AutoUVSettings->BakeTextureType == ETextureType::SpatialGradients)
	{
		Attributes = FIndex4i(
			(int32)EBakeAttributes::PositionX,
			(int32)EBakeAttributes::PositionY,
			(int32)EBakeAttributes::PositionZ,
			0
		);
	}
	else // ETextureType::Normals
	{
		Attributes = FIndex4i(
			(int32)EBakeAttributes::NormalX,
			(int32)EBakeAttributes::NormalY,
			(int32)EBakeAttributes::NormalZ,
			0
		);
	}
	UE::PlanarCut::FTextureAttributeSettings AttribSettings;
	AttribSettings.ToExternal_MaxDistance = AutoUVSettings->MaxDistance;
	AttribSettings.AO_Rays = AutoUVSettings->OcclusionRays;
	AttribSettings.AO_BlurRadius = AutoUVSettings->OcclusionBlurRadius;
	AttribSettings.Curvature_BlurRadius = AutoUVSettings->CurvatureBlurRadius;
	AttribSettings.Curvature_SmoothingSteps = AutoUVSettings->SmoothingIterations;
	AttribSettings.Curvature_VoxelRes = AutoUVSettings->VoxelResolution;
	AttribSettings.Curvature_ThicknessFactor = AutoUVSettings->ThicknessFactor;
	AttribSettings.Curvature_MaxValue = AutoUVSettings->MaxCurvature;
	AttribSettings.ClearGutterChannel = 3; // default clear the gutters for the alpha channel, so it shows more clearly the island boundaries
	UE::PlanarCut::TextureInternalSurfaces(UVLayer, Collection, AutoUVSettings->GutterSize, Attributes, AttribSettings, ImageBuilder,
		UseMaterialIDs,
		AutoUVSettings->TargetMaterialIDs == ETargetMaterialIDs::OddIDs ? EmptyMaterialIDs : AutoUVSettings->MaterialIDs);

	if (Progress)
	{
		Progress(1, LOCTEXT("SavingTexture", "Saving result"));
	}

	// choose default texture name based on corresponding geometry collection name
	FString BaseName = Component->GetRestCollection()->GetName();
	FString Suffix = "_AutoUV";
	if (AutoUVSettings->BakeTextureType == ETextureType::SpatialGradients)
	{
		Suffix = "_AutoUV_Spatial";
	}
	else if (AutoUVSettings->BakeTextureType == ETextureType::Normals)
	{
		Suffix = "_AutoUV_Normals";
	}
	Suffix = FPaths::MakeValidFileName(Suffix);
	SaveGeneratedTexture(ImageBuilder, FString::Printf(TEXT("%s%s"), *BaseName, *Suffix), Component->GetRestCollection(), AutoUVSettings->bPromptToSave, AutoUVSettings->bReplaceExisting);
}

int32 UFractureToolAutoUV::ExecuteFracture(const FFractureToolContext& FractureContext)
{
	if (FractureContext.GetGeometryCollection().IsValid())
	{
		bool bDoUVLayout = true;

		FScopedSlowTask UVTask(bDoUVLayout ? 3 : 2, LOCTEXT("StartingAutoUV", "Automatically laying out and texturing internal surfaces"));
		UVTask.MakeDialog();

		UGeometryCollectionComponent* Component = FractureContext.GetGeometryCollectionComponent();

		if (bDoUVLayout)
		{
			UVTask.EnterProgressFrame(1, LOCTEXT("LayOutUVIslands", "Laying out UV islands"));
			LayoutUVsForComponent(Component);
		}

		UVTask.EnterProgressFrame(1, LOCTEXT("TexturingSurfaces", "Texturing internal surfaces"));
		BakeTextureForComponent(Component, [&](int32 AmtWork, const FText& Msg) { UVTask.EnterProgressFrame(AmtWork, Msg); });
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
