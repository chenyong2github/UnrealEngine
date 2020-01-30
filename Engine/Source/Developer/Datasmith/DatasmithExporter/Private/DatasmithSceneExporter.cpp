// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSceneExporter.h"

#include "DatasmithAnimationSerializer.h"
#include "DatasmithExportOptions.h"
#include "DatasmithLogger.h"
#include "DatasmithMaterialElements.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneXmlWriter.h"
#include "DatasmithUtils.h"
#include "DatasmithProgressManager.h"

#include "GenericPlatform/GenericPlatformFile.h"
#include "Containers/Array.h"
#include "Algo/Find.h"
#include "Containers/Set.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/GarbageCollection.h"

class FDatasmithSceneExporterImpl
{
public:
	FDatasmithSceneExporterImpl();

	// remove unused
	void CleanUnusedMaterials( TSharedRef< IDatasmithScene > DatasmithScene );
	void CleanUnusedMaps( TSharedRef< IDatasmithScene > DatasmithScene );
	void CleanUnusedMeshes( TSharedRef< IDatasmithScene > DatasmithScene );
	int32 CleanLevelSequences( TSharedRef< IDatasmithScene > DatasmithScene );

	// call this before export the actual bitmaps
	void CheckBumpMaps( TSharedRef< IDatasmithScene > DatasmithScene );
	void CheckTextures( TSharedRef< IDatasmithScene > DatasmithScene );
	void UpdateTextureElements( TSharedRef< IDatasmithScene > DatasmithScene );

	void FillLightData(const TSharedPtr< IDatasmithActorElement >& ActorElement, IPlatformFile& PlatformFile);

	static EDatasmithTextureMode GetTextureModeFromPropertyName(const FString& PropertyName);
	static FString GetFileNameWithHash(const FString& FullPath);

	FString Name;
	FString OutputPath;
	FString AssetsOutputPath;
	FString Host;
	FString Vendor;
	FString ProductName;
	FString ProductVersion;
	FString Renderer;

	uint64 ExportStartCycles;

	TSharedPtr<IDatasmithProgressManager> ProgressManager;
	TSharedPtr<FDatasmithLogger> Logger;

private:
	void GatherListOfTexmap(const TSharedPtr<IDatasmithCompositeTexture>& CompositeTexture, TArray<FString>& ListOfTextures);
	void GatherListOfUsedMeshes(const TSharedPtr<IDatasmithActorElement>& InMeshActor, TSet<FString>& InListOfMeshNames);
	void GatherListOfUsedMaterials(const TSharedPtr<IDatasmithMeshElement>& InMesh, TArray<FString>& ListOfMaterials);
	void GatherListOfUsedMaterials(const TSharedPtr<IDatasmithActorElement>& InActor, TArray<FString>& ListOfMaterials);
	void GatherListOfUsedMaterials(const TSharedPtr<IDatasmithMeshActorElement>& InMeshActor, TArray<FString>& ListOfMaterials);
	void GatherListOfUsedMaterials(const TSharedPtr<IDatasmithLightActorElement>& InLightActor, TArray<FString>& ListOfMaterials);
	void GatherListOfUsedMaterials(const TSharedPtr<IDatasmithBaseMaterialElement>& InMaterial, TArray<FString>& ListOfMaterials, const TSharedRef<IDatasmithScene>& InDatasmithScene);
	int32 OptimizeTransformFrames(const TSharedRef<IDatasmithTransformAnimationElement>& Animation, EDatasmithTransformType TransformType);
};

FDatasmithSceneExporterImpl::FDatasmithSceneExporterImpl()
	: ExportStartCycles(0)
{
}

void FDatasmithSceneExporterImpl::GatherListOfUsedMaterials(const TSharedPtr<IDatasmithMeshElement>& InMesh, TArray<FString>& InListOfMaterials)
{
	for (int32 j = 0; j < InMesh->GetMaterialSlotCount(); ++j)
	{
		const TCHAR* MaterialName = InMesh->GetMaterialSlotAt(j)->GetName();

		if (MaterialName != nullptr && Algo::Find(InListOfMaterials, MaterialName) == nullptr)
		{
			InListOfMaterials.Add(MaterialName);
		}
	}
}

void FDatasmithSceneExporterImpl::GatherListOfUsedMaterials(const TSharedPtr<IDatasmithActorElement>& InActor, TArray<FString>& InListOfMaterials)
{
	if ( InActor->IsA( EDatasmithElementType::StaticMeshActor ) )
	{
		GatherListOfUsedMaterials( StaticCastSharedPtr< IDatasmithMeshActorElement >(InActor), InListOfMaterials );
	}
	else if ( InActor->IsA( EDatasmithElementType::Light ) )
	{
		GatherListOfUsedMaterials( StaticCastSharedPtr< IDatasmithLightActorElement >(InActor), InListOfMaterials );
	}

	int32 ChildrenCount = InActor->GetChildrenCount();
	for (int32 i = 0; i < ChildrenCount; i++)
	{
		GatherListOfUsedMaterials( InActor->GetChild(i), InListOfMaterials );
	}
}

void FDatasmithSceneExporterImpl::GatherListOfUsedMaterials(const TSharedPtr<IDatasmithMeshActorElement>& InMeshActor, TArray<FString>& InListOfMaterials)
{

	for (int32 j = 0; j < InMeshActor->GetMaterialOverridesCount(); ++j)
	{
		const TCHAR* MaterialName = InMeshActor->GetMaterialOverride(j)->GetName();

		if (MaterialName != nullptr && Algo::Find(InListOfMaterials, MaterialName) == nullptr)
		{
			InListOfMaterials.Add(MaterialName);
		}
	}
}

void FDatasmithSceneExporterImpl::GatherListOfUsedMaterials(const TSharedPtr<IDatasmithLightActorElement>& InLightActor, TArray<FString>& InListOfMaterials)
{
	if (InLightActor->GetLightFunctionMaterial().IsValid())
	{
		const TCHAR* MaterialName = InLightActor->GetLightFunctionMaterial()->GetName();

		if (MaterialName != nullptr && Algo::Find(InListOfMaterials, MaterialName) == nullptr)
		{
			InListOfMaterials.Add(MaterialName);
		}
	}
}

void FDatasmithSceneExporterImpl::GatherListOfUsedMaterials(const TSharedPtr<IDatasmithBaseMaterialElement>& InMaterial, TArray<FString>& InListOfMaterials, const TSharedRef< IDatasmithScene >& InDatasmithScene)
{
	if (!InMaterial->IsA(EDatasmithElementType::UEPbrMaterial))
	{
		return;
	}

	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMat = StaticCastSharedPtr< IDatasmithUEPbrMaterialElement >(InMaterial);

	for (int32 ExpressionIndex = 0; ExpressionIndex < UEPbrMat->GetExpressionsCount(); ++ExpressionIndex)
	{
		IDatasmithMaterialExpression* Expression = UEPbrMat->GetExpression(ExpressionIndex);
		if (Expression->IsA(EDatasmithMaterialExpressionType::FunctionCall))
		{
			const FString FunctionPathName(StaticCast<IDatasmithMaterialExpressionFunctionCall*>(Expression)->GetFunctionPathName());

			//If the function path name is not absolute then we know it's referencing a datasmith asset.
			if (FPaths::IsRelative(FunctionPathName) && Algo::Find(InListOfMaterials, FunctionPathName) == nullptr)
			{
				InListOfMaterials.Add(FunctionPathName);

				//The newly added material may reference other materials too.
				for (int32 MaterialIndex = 0; MaterialIndex < InDatasmithScene->GetMaterialsCount(); ++MaterialIndex)
				{
					if (FCString::Stricmp(InDatasmithScene->GetMaterial(MaterialIndex)->GetName(), *FunctionPathName) == 0)
					{
						GatherListOfUsedMaterials(InDatasmithScene->GetMaterial(MaterialIndex), InListOfMaterials, InDatasmithScene);
					}
				}
			}
		}
	}
}

void FDatasmithSceneExporterImpl::CleanUnusedMaterials( TSharedRef< IDatasmithScene > DatasmithScene )
{
	TArray< FString > ListOfMaterials;
	for (int32 MeshIndex = 0; MeshIndex < DatasmithScene->GetMeshesCount(); ++MeshIndex)
	{
		GatherListOfUsedMaterials(DatasmithScene->GetMesh(MeshIndex), ListOfMaterials);
	}

	for (int32 ActorIndex = 0; ActorIndex < DatasmithScene->GetActorsCount(); ++ActorIndex)
	{
		const TSharedPtr<IDatasmithActorElement >& Actor = DatasmithScene->GetActor( ActorIndex );
		GatherListOfUsedMaterials( Actor, ListOfMaterials );
	}

	//SubMaterials are materials used as building blocks (MaterialFunctions) inside other materials, like blend materials.
	TArray< FString > ListOfSubMaterials;
	for (int32 MaterialIndex = 0; MaterialIndex < DatasmithScene->GetMaterialsCount(); ++MaterialIndex)
	{
		if ( Algo::Find( ListOfMaterials, DatasmithScene->GetMaterial(MaterialIndex)->GetName() ) != nullptr )
		{
			GatherListOfUsedMaterials( DatasmithScene->GetMaterial(MaterialIndex), ListOfSubMaterials, DatasmithScene );
		}
	}

	for (int32 i = DatasmithScene->GetMaterialsCount() - 1; i >= 0; i--)
	{
		TSharedPtr<IDatasmithBaseMaterialElement> CurrentMaterial = DatasmithScene->GetMaterial(i);

		if ( Algo::Find( ListOfMaterials, CurrentMaterial->GetName() ) == nullptr )
		{
			if ( Algo::Find( ListOfSubMaterials, CurrentMaterial->GetName() ) != nullptr )
			{
				//The material is not directly used in the scene and is only referenced by other materials, no material instance required.
				TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMat = StaticCastSharedPtr<IDatasmithUEPbrMaterialElement>( CurrentMaterial );
				UEPbrMat->SetMaterialFunctionOnly( true );
			}
			else
			{
				// Not found delete it
				DatasmithScene->RemoveMaterial( CurrentMaterial );
			}
		}
	}
}

void FDatasmithSceneExporterImpl::GatherListOfTexmap(const TSharedPtr<IDatasmithCompositeTexture>& CompositeTexture, TArray< FString >& ListOfTextures)
{
	for (int32 i = 0; i < CompositeTexture->GetParamSurfacesCount(); ++i)
	{
		const FString Texture = CompositeTexture->GetParamTexture(i);

		if (!Texture.IsEmpty() && Algo::Find(ListOfTextures, Texture) == nullptr)
		{
			// someName not in name, add it
			ListOfTextures.Add(Texture);
			ListOfTextures.Add(Texture + TEXT("_Tex"));
		}
	}

	for (int32 i = 0; i < CompositeTexture->GetParamMaskSurfacesCount(); i++)
	{
		GatherListOfTexmap(CompositeTexture->GetParamMaskSubComposite(i), ListOfTextures);
	}

	for (int32 i = 0; i < CompositeTexture->GetParamSurfacesCount(); i++)
	{
		GatherListOfTexmap(CompositeTexture->GetParamSubComposite(i), ListOfTextures);
	}
}

void FDatasmithSceneExporterImpl::CleanUnusedMaps( TSharedRef< IDatasmithScene > DatasmithScene )
{
	TArray< FString > ListOfTextures;
	for ( int32 MaterialIndex = 0; MaterialIndex < DatasmithScene->GetMaterialsCount(); ++MaterialIndex )
	{
		const TSharedPtr< IDatasmithBaseMaterialElement >& BaseMaterialElement = DatasmithScene->GetMaterial( MaterialIndex );

		if ( BaseMaterialElement->IsA( EDatasmithElementType::Material ) )
		{
			const TSharedPtr< IDatasmithMaterialElement > MaterialElement = StaticCastSharedPtr< IDatasmithMaterialElement >( BaseMaterialElement );

			for (int32 j = 0; j < MaterialElement->GetShadersCount(); ++j )
			{
				const TSharedPtr< IDatasmithShaderElement >& Shader = MaterialElement->GetShader(j);

				GatherListOfTexmap(Shader->GetDiffuseComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetRefleComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetRoughnessComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetNormalComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetBumpComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetTransComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetMaskComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetDisplaceComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetMetalComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetEmitComp(), ListOfTextures);
				GatherListOfTexmap(Shader->GetWeightComp(), ListOfTextures);
			}
		}
		else if ( BaseMaterialElement->IsA( EDatasmithElementType::MasterMaterial ) )
		{
			const TSharedPtr< IDatasmithMasterMaterialElement > MasterMaterialElement = StaticCastSharedPtr< IDatasmithMasterMaterialElement >( BaseMaterialElement );

			for ( int32 j = 0; j < MasterMaterialElement->GetPropertiesCount(); ++j )
			{
				const TSharedPtr< IDatasmithKeyValueProperty >& MaterialProperty = MasterMaterialElement->GetProperty( j );

				if ( MaterialProperty->GetPropertyType() == EDatasmithKeyValuePropertyType::Texture )
				{
					ListOfTextures.Add( MaterialProperty->GetValue() );
				}
			}
		}
		else if ( BaseMaterialElement->IsA( EDatasmithElementType::UEPbrMaterial ) )
		{
			const TSharedPtr< IDatasmithUEPbrMaterialElement > MaterialElement = StaticCastSharedPtr< IDatasmithUEPbrMaterialElement >( BaseMaterialElement );

			for ( int32 ExpressionIndex = 0; ExpressionIndex < MaterialElement->GetExpressionsCount(); ++ExpressionIndex )
			{
				IDatasmithMaterialExpression* MaterialExpression =  MaterialElement->GetExpression( ExpressionIndex );

				if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::Texture ) )
				{
					IDatasmithMaterialExpressionTexture* TextureExpression = static_cast< IDatasmithMaterialExpressionTexture* >( MaterialExpression );

					ListOfTextures.Add( TextureExpression->GetTexturePathName() );
				}
				else if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::Generic ) )
				{
					IDatasmithMaterialExpressionGeneric* GenericExpression = static_cast< IDatasmithMaterialExpressionGeneric* >( MaterialExpression );

					for ( int32 PropertyIndex = 0; PropertyIndex < GenericExpression->GetPropertiesCount(); ++PropertyIndex )
					{
						const TSharedPtr< IDatasmithKeyValueProperty >& ExpressionProperty = GenericExpression->GetProperty( PropertyIndex );

						if ( ExpressionProperty->GetPropertyType() == EDatasmithKeyValuePropertyType::Texture )
						{
							ListOfTextures.Add( ExpressionProperty->GetValue() );
						}
					}
				}
			}
		}
	}

	for ( int32 ActorIndex = 0; ActorIndex < DatasmithScene->GetActorsCount(); ++ActorIndex )
	{
		const TSharedPtr< IDatasmithActorElement >& Actor = DatasmithScene->GetActor( ActorIndex );

		if ( Actor->IsA( EDatasmithElementType::EnvironmentLight ) )
		{
			TSharedPtr< IDatasmithEnvironmentElement > EnvironmentElement = StaticCastSharedPtr< IDatasmithEnvironmentElement >(Actor);
			if (EnvironmentElement.IsValid())
			{
				GatherListOfTexmap(EnvironmentElement->GetEnvironmentComp(), ListOfTextures);
			}
		}
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	for (int32 i = DatasmithScene->GetTexturesCount() - 1; i >= 0; i--)
	{
		TSharedPtr< IDatasmithTextureElement > TextureElement = DatasmithScene->GetTexture(i);

		if ( TextureElement.IsValid() && Algo::Find( ListOfTextures, TextureElement->GetName() ) == nullptr && PlatformFile.FileExists( TextureElement->GetFile() ) )
		{
			// Not found delete it
			DatasmithScene->RemoveTexture( TextureElement );
		}
	}
}

void FDatasmithSceneExporterImpl::GatherListOfUsedMeshes(const TSharedPtr< IDatasmithActorElement>& InActor, TSet< FString >& InListOfMeshNames)
{
	if (InActor->IsA(EDatasmithElementType::StaticMeshActor))
	{
		const TSharedPtr<IDatasmithMeshActorElement>& MeshActorElement = StaticCastSharedPtr< IDatasmithMeshActorElement >(InActor);
		if ( FCString::Strlen( MeshActorElement->GetStaticMeshPathName() ) >= 0 && !InListOfMeshNames.Contains( MeshActorElement->GetStaticMeshPathName() ))
		{
			InListOfMeshNames.Add( MeshActorElement->GetStaticMeshPathName() );
		}
	}

	int32 ChildrenCount = InActor->GetChildrenCount();
	for (int32 i = 0; i < ChildrenCount; ++i)
	{
		GatherListOfUsedMeshes(InActor->GetChild(i), InListOfMeshNames);
	}
}

void FDatasmithSceneExporterImpl::CleanUnusedMeshes( TSharedRef< IDatasmithScene > DatasmithScene )
{
	// Used meshes must also be checked recursively through each list of actor types
	TSet< FString > ListOfMeshNames;
	for (int32 i = 0; i < DatasmithScene->GetActorsCount(); ++i)
	{
		GatherListOfUsedMeshes(DatasmithScene->GetActor(i), ListOfMeshNames);
	}

	// If ALL meshes are referenced, no need to do anything
	if (ListOfMeshNames.Num() == DatasmithScene->GetMeshesCount())
	{
		return;
	}

	for (int32 i = DatasmithScene->GetMeshesCount() - 1; i >= 0; i--)
	{
		if ( Algo::Find( ListOfMeshNames, FString( DatasmithScene->GetMesh(i)->GetName() ) ) == nullptr )
		{
			// Not found delete it
			DatasmithScene->RemoveMesh( DatasmithScene->GetMesh(i) );
		}
	}
}

int32 FDatasmithSceneExporterImpl::CleanLevelSequences( TSharedRef< IDatasmithScene > DatasmithScene )
{
	int32 NumSequences = DatasmithScene->GetLevelSequencesCount();
	for (int32 SequenceIndex = NumSequences - 1; SequenceIndex >= 0; --SequenceIndex)
	{
		TSharedPtr< IDatasmithLevelSequenceElement > LevelSequence = DatasmithScene->GetLevelSequence(SequenceIndex);

		if (!LevelSequence.IsValid())
		{
			continue;
		}

		int32 NumAnims = LevelSequence->GetAnimationsCount();
		for (int32 AnimIndex = NumAnims - 1; AnimIndex >= 0; --AnimIndex)
		{
			TSharedPtr< IDatasmithBaseAnimationElement > Animation = LevelSequence->GetAnimation(AnimIndex);
			if (Animation.IsValid() && Animation->IsA(EDatasmithElementType::Animation) && Animation->IsSubType((uint64)EDatasmithElementAnimationSubType::TransformAnimation))
			{
				const TSharedRef< IDatasmithTransformAnimationElement > TransformAnimation = StaticCastSharedRef< IDatasmithTransformAnimationElement >(Animation.ToSharedRef());

				// Optimize the frames for each transform type
				int32 NumFrames = OptimizeTransformFrames(TransformAnimation, EDatasmithTransformType::Translation);
				NumFrames += OptimizeTransformFrames(TransformAnimation, EDatasmithTransformType::Rotation);
				NumFrames += OptimizeTransformFrames(TransformAnimation, EDatasmithTransformType::Scale);

				// Remove animation that has no frame
				if (NumFrames == 0)
				{
					LevelSequence->RemoveAnimation(TransformAnimation);
				}
			}
		}
		if (LevelSequence->GetAnimationsCount() == 0)
		{
			DatasmithScene->RemoveLevelSequence(LevelSequence.ToSharedRef());
		}
	}
	return DatasmithScene->GetLevelSequencesCount();
}

int32 FDatasmithSceneExporterImpl::OptimizeTransformFrames( const TSharedRef<IDatasmithTransformAnimationElement>& Animation, EDatasmithTransformType TransformType )
{
	int32 NumFrames = Animation->GetFramesCount(TransformType);
	if (NumFrames > 3)
	{
		// First pass: determine which redundant frames can be removed safely
		TArray<int32> FramesToDelete;
		for (int32 FrameIndex = 1; FrameIndex < NumFrames - 2; ++FrameIndex)
		{
			const FDatasmithTransformFrameInfo& PreviousFrameInfo = Animation->GetFrame(TransformType, FrameIndex - 1);
			const FDatasmithTransformFrameInfo& CurrentFrameInfo = Animation->GetFrame(TransformType, FrameIndex);
			const FDatasmithTransformFrameInfo& NextFrameInfo = Animation->GetFrame(TransformType, FrameIndex + 1);

			// Remove the in-between frames that have the same transform as the previous and following frames
			// Need to keep the frames on the boundaries of sharp transitions to avoid interpolated frames at import
			if (CurrentFrameInfo.IsValid() && PreviousFrameInfo.IsValid() && NextFrameInfo.IsValid() && CurrentFrameInfo == PreviousFrameInfo && CurrentFrameInfo == NextFrameInfo)
			{
				FramesToDelete.Add(FrameIndex);
			}
		}
		// Second pass: remove the frames determined in the previous pass
		for (int32 FrameIndex = FramesToDelete.Num() - 1; FrameIndex > 0; --FrameIndex)
		{
			Animation->RemoveFrame(TransformType, FramesToDelete[FrameIndex]);
		}
	}
	// Note that a one-frame animation could be an instantaneous state change (eg. teleport), so keep it
	return Animation->GetFramesCount(TransformType);
}

void FDatasmithSceneExporterImpl::UpdateTextureElements( TSharedRef< IDatasmithScene > DatasmithScene )
{
	FDatasmithTextureUtils::CalculateTextureHashes(DatasmithScene);

	// No need to do anything if user required to keep images at original location or no output path is set
	if (FDatasmithExportOptions::PathTexturesMode == EDSResizedTexturesPath::OriginalFolder || AssetsOutputPath.IsEmpty())
	{
		return;
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TSet<FString> ExportedTextures;

	for (int32 i = 0; i < DatasmithScene->GetTexturesCount(); i++)
	{
		TSharedPtr< IDatasmithTextureElement > TextureElement = DatasmithScene->GetTexture(i);

		FString TextureFileName = TextureElement->GetFile();

		float RatioDone = float(i + 1) / float( DatasmithScene->GetTexturesCount() );
		if (ProgressManager.IsValid())
		{
			ProgressManager->ProgressEvent(RatioDone, *FPaths::GetBaseFilename( TextureFileName ));
		}

		FString NewFilename = FPaths::Combine( AssetsOutputPath, FPaths::GetCleanFilename( TextureFileName ) );

		// Update texture element and copy image file to new location if applicable
		if (TextureFileName != NewFilename)
		{
			// Copy image file to new location if necessary
			if (!ExportedTextures.Find(NewFilename))
			{
				PlatformFile.CopyFile(*NewFilename, *TextureFileName);
				ExportedTextures.Add(NewFilename);
			}

			TextureElement->SetFile(*NewFilename);
		}
	}
}

void FDatasmithSceneExporterImpl::CheckBumpMaps( TSharedRef< IDatasmithScene > DatasmithScene )
{
	for ( int32 MaterialIndex = 0; MaterialIndex < DatasmithScene->GetMaterialsCount(); ++MaterialIndex )
	{
		TSharedPtr< IDatasmithBaseMaterialElement > BaseMaterialElement = DatasmithScene->GetMaterial( MaterialIndex );

		if ( BaseMaterialElement->IsA( EDatasmithElementType::Material ) )
		{
			const TSharedPtr< IDatasmithMaterialElement >& MaterialElement = StaticCastSharedPtr< IDatasmithMaterialElement >( BaseMaterialElement );

			for (int32 j = 0; j < MaterialElement->GetShadersCount(); ++j )
			{
				TSharedPtr< IDatasmithShaderElement >& Shader = MaterialElement->GetShader(j);

				if (Shader->GetBumpComp()->GetMode() == EDatasmithCompMode::Regular && Shader->GetBumpComp()->GetParamSurfacesCount() == 1 &&
					Shader->GetNormalComp()->GetParamSurfacesCount() == 0)
				{
					FString TextureName = Shader->GetBumpComp()->GetParamTexture(0);
					FString NormalTextureName = TextureName + TEXT("_Norm");

					if ( !TextureName.IsEmpty() )
					{
						FDatasmithTextureSampler UVs = Shader->GetBumpComp()->GetParamTextureSampler(0);

						TSharedPtr< IDatasmithTextureElement > TextureElement;
						TSharedPtr< IDatasmithTextureElement > NormalTextureElement;
						for ( int32 TextureIndex = 0; TextureIndex < DatasmithScene->GetTexturesCount(); ++TextureIndex )
						{
							if ( DatasmithScene->GetTexture( TextureIndex )->GetName() == TextureName )
							{
								TextureElement = DatasmithScene->GetTexture( TextureIndex );
							}
							else if ( DatasmithScene->GetTexture( TextureIndex )->GetName() == NormalTextureName )
							{
								NormalTextureElement = DatasmithScene->GetTexture( TextureIndex );
							}
						}

						if ( TextureElement )
						{
							if ( !NormalTextureElement )
							{
								NormalTextureElement = FDatasmithSceneFactory::CreateTexture( *NormalTextureName );

								NormalTextureElement->SetRGBCurve( 1.f );
								NormalTextureElement->SetFile( TextureElement->GetFile() );
								NormalTextureElement->SetFileHash( TextureElement->GetFileHash() );
								NormalTextureElement->SetTextureMode( EDatasmithTextureMode::Bump );

								DatasmithScene->AddTexture( NormalTextureElement );
							}

							Shader->GetNormalComp()->AddSurface( *NormalTextureName, UVs );
							Shader->GetBumpComp()->ClearSurface();
						}
					}
				}
			}
		}
	}
}

EDatasmithTextureMode FDatasmithSceneExporterImpl::GetTextureModeFromPropertyName(const FString& PropertyName)
{
	if (PropertyName.Find(TEXT("BUMP")) != INDEX_NONE)
	{
		return EDatasmithTextureMode::Bump;
	}
	else if (PropertyName.Find(TEXT("SPECULAR")) != INDEX_NONE)
	{
		return EDatasmithTextureMode::Specular;
	}
	else if (PropertyName.Find(TEXT("NORMAL")) != INDEX_NONE)
	{
		return EDatasmithTextureMode::Normal;
	}

	return EDatasmithTextureMode::Diffuse;
};

FString FDatasmithSceneExporterImpl::GetFileNameWithHash(const FString& FullPath)
{
	FString Hash = FMD5::HashAnsiString(*FullPath);
	FString FileName = FPaths::GetBaseFilename(FullPath);
	FString Extension = FPaths::GetExtension(FileName);
	FileName = FileName + TEXT("_") + Hash + Extension;

	return FileName;
}

void FDatasmithSceneExporterImpl::CheckTextures( TSharedRef< IDatasmithScene > DatasmithScene )
{
	TSet<FString> ExportedTextures;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();


	for( int32 MaterialIndex = 0; MaterialIndex < DatasmithScene->GetMaterialsCount(); ++MaterialIndex )
	{
		const TSharedPtr< IDatasmithBaseMaterialElement >& BaseMaterial = DatasmithScene->GetMaterial( MaterialIndex );

		if ( BaseMaterial->IsA( EDatasmithElementType::MasterMaterial ) )
		{
			const TSharedPtr< IDatasmithMasterMaterialElement >& Material = StaticCastSharedPtr< IDatasmithMasterMaterialElement >( BaseMaterial );

			for ( int32 i = 0; i < Material->GetPropertiesCount(); ++i )
			{
				TSharedPtr< IDatasmithKeyValueProperty > Property = Material->GetProperty(i);

				if (Property->GetPropertyType() == EDatasmithKeyValuePropertyType::Texture && !FString(Property->GetValue()).IsEmpty())
				{
					FString TextureName = Property->GetValue();

					if ( PlatformFile.FileExists( *TextureName ) )
					{
						// Add TextureElement associated to NewPath if it has not been yet
						if (ExportedTextures.Find(TextureName) == nullptr)
						{

							TSharedPtr< IDatasmithTextureElement > TextureElement = FDatasmithSceneFactory::CreateTexture(*FPaths::GetBaseFilename(TextureName));

							TextureElement->SetTextureMode( GetTextureModeFromPropertyName(Property->GetName()) );
							TextureElement->SetFile( *TextureName );

							DatasmithScene->AddTexture( TextureElement );

							ExportedTextures.Add(TextureName);
						}

						Property->SetValue( *FPaths::GetCleanFilename(TextureName) );
					}
				}
			}
		}
	}
}

void FDatasmithSceneExporterImpl::FillLightData(const TSharedPtr< IDatasmithActorElement >& ActorElement, IPlatformFile& PlatformFile)
{
	if ( ActorElement->IsA( EDatasmithElementType::Light ) )
	{
		TSharedPtr< IDatasmithLightActorElement > LightActorElement = StaticCastSharedPtr< IDatasmithLightActorElement >( ActorElement );

		FString IesFileNameWithPath = LightActorElement->GetIesFile();

		if (FDatasmithExportOptions::PathTexturesMode != EDSResizedTexturesPath::OriginalFolder && !AssetsOutputPath.IsEmpty() && !IesFileNameWithPath.IsEmpty())
		{
			IesFileNameWithPath = FPaths::Combine(AssetsOutputPath, FDatasmithUtils::SanitizeFileName(*FPaths::GetBaseFilename(LightActorElement->GetIesFile())) + FPaths::GetExtension(LightActorElement->GetIesFile(), true));

			PlatformFile.CreateDirectoryTree(*AssetsOutputPath);
			PlatformFile.CopyFile(*IesFileNameWithPath, LightActorElement->GetIesFile());
		}

		FString AbsoluteDir = OutputPath + TEXT("/");
		FPaths::MakePathRelativeTo(IesFileNameWithPath, *AbsoluteDir);
		LightActorElement->SetIesFile(*IesFileNameWithPath);
	}

	int32 ChildrenCount = ActorElement->GetChildrenCount();
	for (int32 i = 0; i < ChildrenCount; i++)
	{
		FillLightData( ActorElement->GetChild(i), PlatformFile );
	}
}

FDatasmithSceneExporter::FDatasmithSceneExporter()
	: Impl( MakeUnique< FDatasmithSceneExporterImpl >() )
{
	Reset();
}

FDatasmithSceneExporter::~FDatasmithSceneExporter()
{
	Reset();
}

void FDatasmithSceneExporter::PreExport()
{
	// Collect start time to log amount of time spent to export scene
	Impl->ExportStartCycles = FPlatformTime::Cycles64();
}

void FDatasmithSceneExporter::Export( TSharedRef< IDatasmithScene > DatasmithScene, bool bCleanupUnusedElements )
{
	if ( Impl->ExportStartCycles == 0 )
	{
		Impl->ExportStartCycles = FPlatformTime::Cycles64();
	}

	if ( bCleanupUnusedElements )
	{
		Impl->CleanUnusedMeshes( DatasmithScene );
		Impl->CleanUnusedMaterials( DatasmithScene );
		Impl->CleanUnusedMaps( DatasmithScene );
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree( *Impl->AssetsOutputPath );

	// Add Bump maps from Material objects to scene as TextureElement
	Impl->CheckBumpMaps( DatasmithScene );

	// Add texture maps from MasterMaterial objects to scene as  TextureElement
	Impl->CheckTextures( DatasmithScene );

	// Update TextureElements
	Impl->UpdateTextureElements( DatasmithScene );

	FString FilePath = FPaths::Combine(Impl->OutputPath, Impl->Name ) + TEXT(".") + FDatasmithUtils::GetFileExtension();

	TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileWriter( *FilePath ) );

	if ( !Archive.IsValid() )
	{
		if ( Impl->Logger.IsValid() )
		{
			Impl->Logger->AddGeneralError( *( TEXT("Unable to create file ") + FilePath + TEXT(", Aborting the export process") ) );
		}
		return;
	}

	// Convert paths to relative
	FString AbsoluteDir = Impl->OutputPath + TEXT("/");

	for ( int32 MeshIndex = 0; MeshIndex < DatasmithScene->GetMeshesCount(); ++MeshIndex )
	{
		TSharedPtr< IDatasmithMeshElement > Mesh = DatasmithScene->GetMesh( MeshIndex );

		FString RelativePath = Mesh->GetFile();
		FPaths::MakePathRelativeTo( RelativePath, *AbsoluteDir );

		Mesh->SetFile( *RelativePath );
	}

	for ( int32 TextureIndex = 0; TextureIndex < DatasmithScene->GetTexturesCount(); ++TextureIndex )
	{
		TSharedPtr< IDatasmithTextureElement > Texture = DatasmithScene->GetTexture( TextureIndex );

		FString TextureFile = Texture->GetFile();
		FPaths::MakePathRelativeTo( TextureFile, *AbsoluteDir );
		Texture->SetFile( *TextureFile );
	}

	for ( int32 ActorIndex = 0; ActorIndex < DatasmithScene->GetActorsCount(); ++ActorIndex )
	{
		TSharedPtr< IDatasmithActorElement > Actor = DatasmithScene->GetActor( ActorIndex );
		Impl->FillLightData(Actor, PlatformFile);
	}

	FDatasmithAnimationSerializer AnimSerializer;
	int32 NumSequences = Impl->CleanLevelSequences(DatasmithScene);
	for (int32 SequenceIndex = 0; SequenceIndex < NumSequences; ++SequenceIndex)
	{
		const TSharedPtr<IDatasmithLevelSequenceElement>& LevelSequence = DatasmithScene->GetLevelSequence(SequenceIndex);
		if (LevelSequence.IsValid())
		{
			FString AnimFilePath = FPaths::Combine(Impl->AssetsOutputPath, LevelSequence->GetName()) + DATASMITH_ANIMATION_EXTENSION;

			if (AnimSerializer.Serialize(LevelSequence.ToSharedRef(), *AnimFilePath))
			{
				TUniquePtr<FArchive> AnimArchive(IFileManager::Get().CreateFileReader(*AnimFilePath));
				if (AnimArchive)
				{
					LevelSequence->SetFileHash(FMD5Hash::HashFileFromArchive(AnimArchive.Get()));
				}

				FPaths::MakePathRelativeTo(AnimFilePath, *AbsoluteDir);
				LevelSequence->SetFile(*AnimFilePath);
			}
		}
	}

	// Environments cleanup

	// environment only can have Textures
	for (int32 i = DatasmithScene->GetActorsCount() - 1; i >= 0; i--)
	{
		if ( DatasmithScene->GetActor(i)->IsA( EDatasmithElementType::EnvironmentLight ) )
		{
			TSharedPtr< IDatasmithEnvironmentElement > EnvironmentElement = StaticCastSharedPtr< IDatasmithEnvironmentElement >( DatasmithScene->GetActor(i) );

			if ( EnvironmentElement->GetEnvironmentComp()->GetMode() != EDatasmithCompMode::Regular || EnvironmentElement->GetEnvironmentComp()->GetParamSurfacesCount() != 1 )
			{
				DatasmithScene->RemoveActor( EnvironmentElement, EDatasmithActorRemovalRule::RemoveChildren );
			}
			else if ( !EnvironmentElement->GetEnvironmentComp()->GetUseTexture(0) )
			{
				DatasmithScene->RemoveActor( EnvironmentElement, EDatasmithActorRemovalRule::RemoveChildren );
			}
		}
	}

	// remove duplicated Environments
	for ( int32 i = DatasmithScene->GetActorsCount() - 1; i >= 0; i-- )
	{
		if ( DatasmithScene->GetActor(i)->IsA( EDatasmithElementType::EnvironmentLight ) )
		{
			TSharedPtr< IDatasmithEnvironmentElement > EnvironmentElement = StaticCastSharedPtr< IDatasmithEnvironmentElement >( DatasmithScene->GetActor(i) );

			bool bIsIlluminationMap = EnvironmentElement->GetIsIlluminationMap();

			bool bIsADuplicate = false;
			for ( int32 j = i - 1; j >= 0; j-- )
			{
				if (DatasmithScene->GetActor(j)->IsA(EDatasmithElementType::EnvironmentLight))
				{
					TSharedPtr< IDatasmithEnvironmentElement > PreviousEnvElement = StaticCastSharedPtr< IDatasmithEnvironmentElement >( DatasmithScene->GetActor(j) );
					if (PreviousEnvElement->GetIsIlluminationMap() == bIsIlluminationMap)
					{
						bIsADuplicate = true;
						break;
					}
				}
			}

			if ( bIsADuplicate )
			{
				DatasmithScene->RemoveActor( EnvironmentElement, EDatasmithActorRemovalRule::RemoveChildren );
			}
		}
	}

	// Log time spent to export scene in seconds
	int ElapsedTime = (int)FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Impl->ExportStartCycles);
	DatasmithScene->SetExportDuration( ElapsedTime );

	FDatasmithSceneXmlWriter DatasmithSceneXmlWriter;
	DatasmithSceneXmlWriter.Serialize( DatasmithScene, *Archive );

	Archive->Close();

	// Run the garbage collector at this point so that we're in a good state for the next export
	CollectGarbage( GARBAGE_COLLECTION_KEEPFLAGS );
}

void FDatasmithSceneExporter::Reset()
{
	Impl->Host = TEXT("");
	Impl->Renderer = TEXT("");
	Impl->ProgressManager = nullptr;
	Impl->Logger = nullptr;

	Impl->ExportStartCycles = 0;
}

void FDatasmithSceneExporter::SetProgressManager( const TSharedPtr< IDatasmithProgressManager >& InProgressManager )
{
	Impl->ProgressManager = InProgressManager;
}

void FDatasmithSceneExporter::SetLogger( const TSharedPtr< FDatasmithLogger >& InLogger )
{
	Impl->Logger = InLogger;
}

void FDatasmithSceneExporter::SetName(const TCHAR* InName)
{
	Impl->Name = InName;
}

void FDatasmithSceneExporter::SetOutputPath( const TCHAR* InOutputPath )
{
	Impl->OutputPath = InOutputPath;
	FPaths::NormalizeDirectoryName( Impl->OutputPath );

	Impl->AssetsOutputPath = FPaths::Combine( Impl->OutputPath, Impl->Name + TEXT("_Assets") );
}

const TCHAR* FDatasmithSceneExporter::GetOutputPath() const
{
	return *Impl->OutputPath;
}

const TCHAR* FDatasmithSceneExporter::GetAssetsOutputPath() const
{
	return *Impl->AssetsOutputPath;
}
