// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithLandscapeImporter.h"

#include "DatasmithActorImporter.h"
#include "DatasmithImportContext.h"
#include "IDatasmithSceneElements.h"
#include "ObjectTemplates/DatasmithLandscapeTemplate.h"

#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEditorModule.h"
#include "LandscapeEditorObject.h"
#include "LandscapeEditorUtils.h"
#include "LandscapeFileFormatInterface.h"
#include "NewLandscapeUtils.h"

#include "Modules/ModuleManager.h"
#include "Utility/DatasmithImporterUtils.h"

#define LOCTEXT_NAMESPACE "DatasmithImportFactory"

AActor* FDatasmithLandscapeImporter::ImportLandscapeActor( const TSharedRef< IDatasmithLandscapeElement >& LandscapeActorElement, FDatasmithImportContext& ImportContext, EDatasmithImportActorPolicy ImportActorPolicy )
{
	TStrongObjectPtr< ULandscapeEditorObject > LandscapeEditorObject( NewObject< ULandscapeEditorObject >() );

	LandscapeEditorObject->ImportLandscape_HeightmapFilename = LandscapeActorElement->GetHeightmap();
	LandscapeEditorObject->NewLandscape_Scale = LandscapeActorElement->GetScale();

	TArray< FLandscapeFileResolution > ImportResolutions;
	FNewLandscapeUtils::ImportLandscapeData( LandscapeEditorObject.Get(), ImportResolutions );

	const int32 ComponentCountX = LandscapeEditorObject->NewLandscape_ComponentCount.X;
	const int32 ComponentCountY = LandscapeEditorObject->NewLandscape_ComponentCount.Y;
	const int32 QuadsPerComponent = LandscapeEditorObject->NewLandscape_SectionsPerComponent * LandscapeEditorObject->NewLandscape_QuadsPerSection;
	const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;

	TOptional< TArray< FLandscapeImportLayerInfo > > ImportLayers = FNewLandscapeUtils::CreateImportLayersInfo( LandscapeEditorObject.Get(), ENewLandscapePreviewMode::ImportLandscape );

	if ( !ImportLayers )
	{
		return nullptr;
	}

	TArray<uint16> HeightData = FNewLandscapeUtils::ComputeHeightData( LandscapeEditorObject.Get(), ImportLayers.GetValue(), ENewLandscapePreviewMode::ImportLandscape );

	const ELandscapeImportAlphamapType ImportLandscape_AlphamapType = ELandscapeImportAlphamapType::Additive;

	const FVector Offset = FTransform( LandscapeActorElement->GetRotation(), FVector::ZeroVector, 
		LandscapeActorElement->GetScale() ).TransformVector( FVector( -ComponentCountX * QuadsPerComponent / 2, -ComponentCountY * QuadsPerComponent / 2, 0 ) );

	FVector OriginalTranslation = LandscapeActorElement->GetTranslation();
	FVector OriginalScale = LandscapeActorElement->GetScale();

	LandscapeActorElement->SetTranslation( LandscapeActorElement->GetTranslation() + Offset );
	LandscapeActorElement->SetScale( LandscapeActorElement->GetScale() );

	ALandscape* Landscape = Cast< ALandscape >(FDatasmithActorImporter::ImportActor( ALandscape::StaticClass(), LandscapeActorElement, ImportContext, ImportActorPolicy,
		[ &HeightData, &ImportLayers, SizeX, SizeY, LandscapeEditorObject, ImportLandscape_AlphamapType, ActorScale = LandscapeActorElement->GetScale() ]( AActor* NewActor )
	{
		check( Cast< ALandscape >( NewActor ) );

		NewActor->SetActorRelativeScale3D( ActorScale );

		TMap<FGuid, TArray<uint16>> HeightmapDataPerLayers;
		HeightmapDataPerLayers.Add(FGuid(), HeightData);
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
		MaterialLayerDataPerLayer.Add(FGuid(), ImportLayers.GetValue());

		Cast< ALandscape >( NewActor )->Import( FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, LandscapeEditorObject->NewLandscape_SectionsPerComponent, LandscapeEditorObject->NewLandscape_QuadsPerSection,
			HeightmapDataPerLayers, nullptr, MaterialLayerDataPerLayer, ImportLandscape_AlphamapType );
	} ) );

	LandscapeActorElement->SetTranslation( OriginalTranslation );
	LandscapeActorElement->SetScale( OriginalScale );

	if ( !Landscape )
	{
		return nullptr;
	}

	UDatasmithLandscapeTemplate* LandscapeTemplate = NewObject< UDatasmithLandscapeTemplate >( Landscape->GetRootComponent() );
	LandscapeTemplate->LandscapeMaterial = FDatasmithImporterUtils::FindAsset< UMaterialInterface >( ImportContext.AssetsContext, LandscapeActorElement->GetMaterial() );

	// automatically calculate a lighting LOD that won't crash lightmass (hopefully)
	// < 2048x2048 -> LOD0
	// >=2048x2048 -> LOD1
	// >= 4096x4096 -> LOD2
	// >= 8192x8192 -> LOD3
	LandscapeTemplate->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);
	
	LandscapeTemplate->Apply( Landscape );

	Landscape->ReimportHeightmapFilePath = LandscapeEditorObject->ImportLandscape_HeightmapFilename;

	ULandscapeInfo* LandscapeInfo = Landscape->CreateLandscapeInfo();
	LandscapeInfo->UpdateLayerInfoMap(Landscape);

	// Import doesn't fill in the LayerInfo for layers with no data, do that now
	const TArray< FLandscapeImportLayer >& ImportLandscapeLayersList = LandscapeEditorObject->ImportLandscape_Layers;
	for(int32 i = 0; i < ImportLandscapeLayersList.Num(); i++)
	{
		if(ImportLandscapeLayersList[i].LayerInfo != nullptr)
		{
			Landscape->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(ImportLandscapeLayersList[i].LayerInfo, ImportLandscapeLayersList[i].SourceFilePath));

			int32 LayerInfoIndex = LandscapeInfo->GetLayerInfoIndex(ImportLandscapeLayersList[i].LayerName);
			if(ensure(LayerInfoIndex != INDEX_NONE))
			{
				FLandscapeInfoLayerSettings& LayerSettings = LandscapeInfo->Layers[LayerInfoIndex];
				LayerSettings.LayerInfoObj = ImportLandscapeLayersList[i].LayerInfo;
			}
		}
	}

	Landscape->RegisterAllComponents();

	// Need to explicitly call PostEditChange on the LandscapeMaterial property or the landscape proxy won't update its material
	FPropertyChangedEvent MaterialPropertyChangedEvent( FindFieldChecked< FProperty >( Landscape->GetClass(), FName("LandscapeMaterial") ) );
	Landscape->PostEditChangeProperty( MaterialPropertyChangedEvent );
	Landscape->PostEditChange();

	return Landscape;
}

#undef LOCTEXT_NAMESPACE
