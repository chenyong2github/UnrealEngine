// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeScene.h"

// Datasmith facade.
#include "DatasmithFacadeActor.h"
#include "DatasmithFacadeElement.h"
#include "DatasmithFacadeMaterial.h"
#include "DatasmithFacadeMesh.h"
#include "DatasmithFacadeMetaData.h"
#include "DatasmithFacadeTexture.h"

// Datasmith SDK.
#include "DatasmithExporterManager.h"
#include "DatasmithSceneExporter.h"

#include "Misc/Paths.h"

FDatasmithFacadeScene::FDatasmithFacadeScene(
	const TCHAR* InApplicationHostName,
	const TCHAR* InApplicationVendorName,
	const TCHAR* InApplicationProductName,
	const TCHAR* InApplicationProductVersion
) :
	ApplicationHostName(InApplicationHostName),
	ApplicationVendorName(InApplicationVendorName),
	ApplicationProductName(InApplicationProductName),
	ApplicationProductVersion(InApplicationProductVersion),
	SceneRef(FDatasmithSceneFactory::CreateScene(TEXT(""))),
	ExportedTextureSet(MakeShared<TSet<FString>>()),
	bCleanUpNeeded(true)
{
}

void FDatasmithFacadeScene::AddActor(
	FDatasmithFacadeActor* InActorPtr
)
{
	InActorPtr->BuildScene(*this);
}

int32 FDatasmithFacadeScene::GetActorsCount() const
{
	return SceneRef->GetActorsCount();
}

FDatasmithFacadeActor* FDatasmithFacadeScene::GetNewActor(
	int32 ActorIndex
)
{
	if (TSharedPtr<IDatasmithActorElement> ActorElement = SceneRef->GetActor(ActorIndex))
	{
		return FDatasmithFacadeActor::GetNewFacadeActorFromSharedPtr(ActorElement);
	}

	return nullptr;
}

void FDatasmithFacadeScene::RemoveActor(
	FDatasmithFacadeActor* InActorPtr,
	EActorRemovalRule RemovalRule
)
{
	SceneRef->RemoveActor(InActorPtr->GetDatasmithActorElement(), static_cast<EDatasmithActorRemovalRule>( RemovalRule ));
}

void FDatasmithFacadeScene::AddMaterial(
	FDatasmithFacadeBaseMaterial* InMaterialPtr
)
{
	InMaterialPtr->BuildScene( *this );
}

int32 FDatasmithFacadeScene::GetMaterialsCount() const
{
	return SceneRef->GetMaterialsCount();
}

FDatasmithFacadeBaseMaterial* FDatasmithFacadeScene::GetNewMaterial(
	int32 MaterialIndex
)
{
	if (TSharedPtr<IDatasmithBaseMaterialElement> ActorElement = SceneRef->GetMaterial( MaterialIndex ))
	{
		return FDatasmithFacadeBaseMaterial::GetNewFacadeBaseMaterialFromSharedPtr( ActorElement );
	}

	return nullptr;
}

void FDatasmithFacadeScene::RemoveMaterial(
	FDatasmithFacadeBaseMaterial* InMaterialPtr
)
{
	if (InMaterialPtr)
	{
		SceneRef->RemoveMaterial( InMaterialPtr->GetDatasmithBaseMaterial() );
	}
}

void FDatasmithFacadeScene::AddMesh(
	FDatasmithFacadeMesh* InMeshPtr
)
{
	SceneElementSet.Add( TSharedPtr<FDatasmithFacadeElement>( InMeshPtr ) );
}

void FDatasmithFacadeScene::AddTexture(
	FDatasmithFacadeTexture* InTexturePtr
)
{
	InTexturePtr->BuildScene( *this );
}

int32 FDatasmithFacadeScene::GetTexturesCount() const
{
	return SceneRef->GetTexturesCount();
}

FDatasmithFacadeTexture* FDatasmithFacadeScene::GetNewTexture(
	int32 TextureIndex
)
{
	if (TSharedPtr<IDatasmithTextureElement> TextureElement = SceneRef->GetTexture(TextureIndex))
	{
		return new FDatasmithFacadeTexture(TextureElement.ToSharedRef());
	}

	return nullptr;
}

void FDatasmithFacadeScene::RemoveTexture(
	FDatasmithFacadeTexture* InTexturePtr
)
{
	SceneRef->RemoveTexture(InTexturePtr->GetDatasmithTextureElement());
}

void FDatasmithFacadeScene::AddMetaData(
	FDatasmithFacadeMetaData* InMetaData
)
{
	InMetaData->BuildScene( *this );
}

int32 FDatasmithFacadeScene::GetMetaDataCount() const
{
	return SceneRef->GetMetaDataCount();
}

FDatasmithFacadeMetaData* FDatasmithFacadeScene::GetNewMetaData(
	int32 MetaDataIndex
)
{
	if (TSharedPtr<IDatasmithMetaDataElement> MetaDataElement = SceneRef->GetMetaData(MetaDataIndex))
	{
		return new FDatasmithFacadeMetaData(MetaDataElement.ToSharedRef());
	}

	return nullptr;
}

void FDatasmithFacadeScene::RemoveMetaData(
	FDatasmithFacadeMetaData* InMetaDataPtr
)
{
	SceneRef->RemoveMetaData(InMetaDataPtr->GetDatasmithMetaDataElement());
}

void FDatasmithFacadeScene::ExportAssets(
	const TCHAR* InAssetFolder
)
{
	// If applicable, update SceneExporter with new folder
	if (FCString::Strlen(SceneExporterRef->GetAssetsOutputPath()) == 0)
	{
		SceneExporterRef->SetOutputPath(InAssetFolder);
	}
	else
	{
		ensure(FCString::Strcmp(SceneExporterRef->GetAssetsOutputPath(), InAssetFolder) == 0);
	}

	FString AssetFolder(InAssetFolder);

	// Build and export the Datasmith scene element assets.
	for (TSharedPtr<FDatasmithFacadeElement> ElementPtr : SceneElementSet)
	{
		ElementPtr->ExportAsset(AssetFolder);
	}
}

//This function is a temporary workaround to make sure Materials and texture are not deleted from the scene.
//There won't be a need to reset the scene once all the Facade elements will stop generating DatasmithElement on the fly (and duplicating the data) during BuildElement()
void ResetBuiltFacadeElement(TSharedRef<IDatasmithScene>& SceneRef)
{
	TArray<TSharedPtr<IDatasmithMeshElement>> MeshesArray;
	TArray<TSharedPtr<IDatasmithBaseMaterialElement>> MaterialArray;
	TArray<TSharedPtr<IDatasmithTextureElement>> TextureArray;
	TArray<TSharedPtr<IDatasmithMetaDataElement>> MetaDataArray;
	TArray<TSharedPtr<IDatasmithActorElement>> ActorArray;
	TArray<TSharedPtr<IDatasmithLevelSequenceElement>> LevelSequences;
	TArray<TSharedPtr<IDatasmithLevelVariantSetsElement>> LevelVariantSets;

	const int32 MeshCount = SceneRef->GetMeshesCount();
	const int32 MaterialCount = SceneRef->GetMaterialsCount();
	const int32 TextureCount = SceneRef->GetTexturesCount();
	const int32 MetaDataCount = SceneRef->GetMetaDataCount();
	const int32 ActorCount = SceneRef->GetActorsCount();
	const int32 LevelSequencesCount = SceneRef->GetLevelSequencesCount();
	const int32 LevelVariantSetsCount = SceneRef->GetLevelVariantSetsCount();

	MeshesArray.Reserve( MeshCount );
	MaterialArray.Reserve( MaterialCount );
	TextureArray.Reserve( TextureCount );
	MetaDataArray.Reserve( MetaDataCount );
	ActorArray.Reserve( ActorCount );
	LevelSequences.Reserve( LevelSequencesCount );
	LevelVariantSets.Reserve( LevelVariantSetsCount );

	//Backup Meshes, Materials and Textures before reseting the scene in order to restore them.
	for ( int32 MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex )
	{
		MeshesArray.Add( SceneRef->GetMesh( MeshIndex ) );
	}

	for ( int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex )
	{
		MaterialArray.Add( SceneRef->GetMaterial( MaterialIndex ) );
	}

	for ( int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex )
	{
		TextureArray.Add( SceneRef->GetTexture( TextureIndex ) );
	}

	for ( int32 MetaDataIndex = 0; MetaDataIndex < MetaDataCount; ++MetaDataIndex )
	{
		MetaDataArray.Add( SceneRef->GetMetaData( MetaDataIndex ) );
	}

	for ( int32 ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex )
	{
		ActorArray.Add( SceneRef->GetActor( ActorIndex ) );
	}

	for ( int32 LevelSequenceIndex = 0; LevelSequenceIndex < LevelSequencesCount; ++LevelSequenceIndex )
	{
		LevelSequences.Add( SceneRef->GetLevelSequence( LevelSequenceIndex ) );
	}

	for ( int32 LevelVariantSetIndex = 0; LevelVariantSetIndex < LevelVariantSetsCount; ++LevelVariantSetIndex )
	{
		LevelVariantSets.Add( SceneRef->GetLevelVariantSets( LevelVariantSetIndex ) );
	}

	SceneRef->Reset();

	for ( TSharedPtr<IDatasmithMeshElement>& CurrentMesh : MeshesArray )
	{
		SceneRef->AddMesh( CurrentMesh );
	}

	for ( TSharedPtr<IDatasmithBaseMaterialElement>& CurrentMaterial : MaterialArray )
	{
		SceneRef->AddMaterial( CurrentMaterial );
	}

	for ( TSharedPtr<IDatasmithTextureElement>& CurrentTexture: TextureArray )
	{
		SceneRef->AddTexture( CurrentTexture );
	}

	for ( TSharedPtr<IDatasmithMetaDataElement>& CurrentMetaData : MetaDataArray )
	{
		SceneRef->AddMetaData( CurrentMetaData );
	}

	for ( TSharedPtr<IDatasmithLevelSequenceElement>& CurrentLevelSequence : LevelSequences )
	{
		SceneRef->AddLevelSequence( CurrentLevelSequence.ToSharedRef() );
	}

	for ( TSharedPtr<IDatasmithLevelVariantSetsElement>& CurrentLevelVariantSet : LevelVariantSets )
	{
		SceneRef->AddLevelVariantSets( CurrentLevelVariantSet );
	}

	for ( TSharedPtr<IDatasmithActorElement>& CurrentActor : ActorArray )
	{
		SceneRef->AddActor( CurrentActor );
	}
}

void FDatasmithFacadeScene::BuildScene(
	const TCHAR* InSceneName
)
{
	// Initialize the Datasmith scene.
	SceneRef->SetName(InSceneName);
	ResetBuiltFacadeElement(SceneRef);

	// Set the name of the host application used to build the scene.
	SceneRef->SetHost(*ApplicationHostName);

	// Set the vendor name of the application used to build the scene.
	SceneRef->SetVendor(*ApplicationVendorName);

	// Set the product name of the application used to build the scene.
	SceneRef->SetProductName(*ApplicationProductName);

	// Set the product version of the application used to build the scene.
	SceneRef->SetProductVersion(*ApplicationProductVersion);

	// Set Sets the original path resources were stored.
	SceneRef->SetResourcePath(SceneExporterRef->GetOutputPath());

	// Build the collected scene elements and add them to the Datasmith scene.
	for (TSharedPtr<FDatasmithFacadeElement> ElementPtr : SceneElementSet)
	{
		ElementPtr->BuildScene(*this);
	}

	if (bCleanUpNeeded)
	{
		// Remove unused assets
		FDatasmithSceneUtils::CleanUpScene(SceneRef, true);
	}

	SceneElementSet.Empty();
}

void FDatasmithFacadeScene::PreExport()
{
	// Initialize the Datasmith exporter module.
	FDatasmithExporterManager::Initialize();

	// Create a Datasmith scene exporter.
	SceneExporterRef = MakeShared<FDatasmithSceneExporter>();

	// Start measuring the time taken to export the scene.
	SceneExporterRef->PreExport();
}

void FDatasmithFacadeScene::Shutdown()
{
	FDatasmithExporterManager::Shutdown();
}

void FDatasmithFacadeScene::ExportScene(
	const TCHAR* InOutputPath
)
{
	if (!SceneExporterRef.IsValid())
	{
		return;
	}

	FString OutputPath = InOutputPath;

	// Set the name of the scene to export and let Datasmith sanitize it when required.
	FString SceneName = FPaths::GetBaseFilename(OutputPath);
	SceneExporterRef->SetName(*SceneName);

	// Set the output folder where this scene will be exported.
	FString SceneFolder = FPaths::GetPath(OutputPath);
	SceneExporterRef->SetOutputPath(*SceneFolder);

	// Build and export the Datasmith scene element assets.
	ExportAssets(SceneExporterRef->GetAssetsOutputPath());

	// No clean up needed as it will be performed by the SceneExporter
	bCleanUpNeeded = false;

	// Build the Datasmith scene instance.
	BuildScene(*SceneName);

	// Restore bCleanUpNeeded to its default value
	bCleanUpNeeded = true;

	// Export the Datasmith scene instance into its file.
	SceneExporterRef->Export(SceneRef);
}

TSharedRef<IDatasmithScene> FDatasmithFacadeScene::GetScene() const
{
	return SceneRef;
}

TSharedRef<TSet<FString>> FDatasmithFacadeScene::GetExportedTextures() const
{
	return ExportedTextureSet;
}

void FDatasmithFacadeScene::SetLabel(const TCHAR* InSceneLabel)
{
	SceneRef->SetLabel(InSceneLabel);
}

const TCHAR* FDatasmithFacadeScene::GetLabel() const
{
	return SceneRef->GetLabel();
}