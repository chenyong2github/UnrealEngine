// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithMaterialImporter.h"

#include "DatasmithImporterModule.h"
#include "DatasmithMaterialElements.h"
#include "DatasmithMaterialExpressions.h"

#include "MasterMaterials/DatasmithMasterMaterial.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MasterMaterials/DatasmithMasterMaterialSelector.h"
#include "ObjectTemplates/DatasmithMaterialInstanceTemplate.h"
#include "Utility/DatasmithImporterUtils.h"

#include "AssetRegistryModule.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"

#include "ObjectTools.h"


namespace DatasmithMaterialImporterUtils
{
	int32 ComputeMaterialExpressionHash( IDatasmithMaterialExpression* MaterialExpression );

	int32 ComputeExpressionInputHash( IDatasmithExpressionInput* ExpressionInput )
	{
		if ( !ExpressionInput )
		{
			return 0;
		}

		uint32 Hash = 0;

		if ( ExpressionInput->GetExpression() )
		{
			int32 ExpressionHash = ComputeMaterialExpressionHash( ExpressionInput->GetExpression() );
			Hash = HashCombine( Hash, ExpressionHash );
		}

		Hash = HashCombine( Hash, GetTypeHash( ExpressionInput->GetOutputIndex() ) );

		return Hash;
	}

	int32 ComputeMaterialExpressionHash( IDatasmithMaterialExpression* MaterialExpression )
	{
		uint32 Hash = GetTypeHash( MaterialExpression->GetType() );
		Hash = HashCombine( Hash, GetTypeHash( FString( MaterialExpression->GetName() ) ) );

		if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::TextureCoordinate ) )
		{
			IDatasmithMaterialExpressionTextureCoordinate* TextureCoordinate = static_cast< IDatasmithMaterialExpressionTextureCoordinate* >( MaterialExpression );
			Hash = HashCombine( Hash, GetTypeHash( TextureCoordinate->GetCoordinateIndex() ) );
			Hash = HashCombine( Hash, GetTypeHash( TextureCoordinate->GetUTiling() ) );
			Hash = HashCombine( Hash, GetTypeHash( TextureCoordinate->GetVTiling() ) );
		}
		else if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::ConstantColor ) )
		{
			if ( FCString::Strlen( MaterialExpression->GetName() ) == 0 )
			{
				IDatasmithMaterialExpressionColor* ColorExpression = static_cast< IDatasmithMaterialExpressionColor* >( MaterialExpression );

				Hash = HashCombine( Hash, GetTypeHash( ColorExpression->GetColor() ) );
			}
		}
		else if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::ConstantScalar ) )
		{
			if ( FCString::Strlen( MaterialExpression->GetName() ) == 0 )
			{
				IDatasmithMaterialExpressionScalar* ScalarExpression = static_cast< IDatasmithMaterialExpressionScalar* >( MaterialExpression );

				Hash = HashCombine( Hash, GetTypeHash( ScalarExpression->GetScalar() ) );
			}
		}
		else if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::Generic ) )
		{
			IDatasmithMaterialExpressionGeneric* GenericExpression = static_cast< IDatasmithMaterialExpressionGeneric* >( MaterialExpression );

			UClass* ExpressionClass = FindObject< UClass >( ANY_PACKAGE, *( FString( TEXT("MaterialExpression") ) + GenericExpression->GetExpressionName() ) );

			UMaterialExpression* MaterialCDO = nullptr;

			if ( ExpressionClass )
			{
				MaterialCDO = ExpressionClass->GetDefaultObject< UMaterialExpression >();
			}

			for ( int32 PropertyIndex = 0; PropertyIndex < GenericExpression->GetPropertiesCount(); ++PropertyIndex )
			{
				const TSharedPtr< IDatasmithKeyValueProperty >& KeyValue = GenericExpression->GetProperty( PropertyIndex );

				if ( KeyValue )
				{
					Hash = HashCombine( Hash, GetTypeHash( KeyValue->GetName() ) );
					Hash = HashCombine( Hash, GetTypeHash( KeyValue->GetPropertyType() ) );

					// Only hash values if it's not the parameter
					// Currently, if we're setting values on multiple properties, we're not sure which one is the parameter so we hash them all
					if ( MaterialCDO && ( !MaterialCDO->HasAParameterName() || GenericExpression->GetPropertiesCount() > 1 ) )
					{
						Hash = HashCombine( Hash, GetTypeHash( KeyValue->GetValue() ) );
					}
				}
			}
		}
		else if ( MaterialExpression->IsA( EDatasmithMaterialExpressionType::FunctionCall ) )
		{
			// Hash the path to the function as calling different functions should result in different hash values
			IDatasmithMaterialExpressionFunctionCall* FunctionCallExpression = static_cast< IDatasmithMaterialExpressionFunctionCall* >( MaterialExpression );

			Hash = HashCombine( Hash, GetTypeHash( FunctionCallExpression->GetFunctionPathName() ) );
		}

		for ( int32 InputIndex = 0; InputIndex < MaterialExpression->GetInputCount(); ++InputIndex )
		{
			Hash = HashCombine( Hash, ComputeExpressionInputHash( MaterialExpression->GetInput( InputIndex ) ) );
		}

		return Hash;
	}

	int32 ComputeMaterialHash( TSharedRef< IDatasmithUEPbrMaterialElement > MaterialElement )
	{
		int32 Hash = GetTypeHash( MaterialElement->GetTwoSided() );

		Hash = HashCombine( Hash, GetTypeHash( MaterialElement->GetUseMaterialAttributes() ) );
		Hash = HashCombine( Hash, GetTypeHash( MaterialElement->GetBlendMode() ) );

		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetBaseColor() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetMetallic() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetSpecular() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetRoughness() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetEmissiveColor() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetOpacity() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetNormal() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetWorldDisplacement() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetRefraction() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetAmbientOcclusion() ) );
		Hash = HashCombine( Hash, ComputeExpressionInputHash( &MaterialElement->GetMaterialAttributes() ) );

		return Hash;
	}
}

UMaterialFunction* FDatasmithMaterialImporter::CreateMaterialFunction( FDatasmithImportContext& ImportContext, const TSharedRef< IDatasmithBaseMaterialElement >& BaseMaterialElement )
{
	UMaterialFunction* MaterialFunction = nullptr;

	if ( BaseMaterialElement->IsA( EDatasmithElementType::UEPbrMaterial ) )
	{
		const TSharedRef< IDatasmithUEPbrMaterialElement > MaterialElement = StaticCastSharedRef< IDatasmithUEPbrMaterialElement >( BaseMaterialElement );
		UPackage* MaterialPackage = ImportContext.AssetsContext.MaterialFunctionsImportPackage.Get();
		MaterialFunction = FDatasmithMaterialExpressions::CreateUEPbrMaterialFunction( MaterialPackage, MaterialElement, ImportContext.AssetsContext, nullptr, ImportContext.ObjectFlags );
	}
	else
	{
		//Only UEPbr materials should end up here.
		check(false)
		return nullptr;
	}

	if ( MaterialFunction != nullptr )
	{
		ImportContext.ImportedMaterialFunctions.Add( BaseMaterialElement ) = MaterialFunction;
		ImportContext.ImportedMaterialFunctionsByName.Add( BaseMaterialElement->GetName(), BaseMaterialElement );
	}

	return MaterialFunction;
}

UMaterialInterface* FDatasmithMaterialImporter::CreateMaterial( FDatasmithImportContext& ImportContext,
	const TSharedRef< IDatasmithBaseMaterialElement >& BaseMaterialElement, UMaterialInterface* ExistingMaterial )
{
	UMaterialInterface* Material = nullptr;

	if ( BaseMaterialElement->IsA( EDatasmithElementType::Material ) )
	{
		const TSharedRef< IDatasmithMaterialElement >& MaterialElement = StaticCastSharedRef< IDatasmithMaterialElement >( BaseMaterialElement );

		UPackage* MaterialPackage = ImportContext.AssetsContext.MaterialsImportPackage.Get();

		Material = FDatasmithMaterialExpressions::CreateDatasmithMaterial(MaterialPackage, MaterialElement, ImportContext.AssetsContext, nullptr, ImportContext.ObjectFlags);
	}
	else if ( BaseMaterialElement->IsA( EDatasmithElementType::MasterMaterial ) )
	{
		const TSharedRef< IDatasmithMasterMaterialElement >& MasterMaterialElement = StaticCastSharedRef< IDatasmithMasterMaterialElement >( BaseMaterialElement );
		Material = ImportMasterMaterial( ImportContext, MasterMaterialElement, ExistingMaterial );
	}
	else if ( BaseMaterialElement->IsA( EDatasmithElementType::UEPbrMaterial ) )
	{
		const TSharedRef< IDatasmithUEPbrMaterialElement > MaterialElement = StaticCastSharedRef< IDatasmithUEPbrMaterialElement >( BaseMaterialElement );
		if ( MaterialElement->GetMaterialFunctionOnly() )
		{
			//No need to instantiate a MaterialElement that is only used as a material function
			return nullptr;
		}

		int32 MaterialHash = DatasmithMaterialImporterUtils::ComputeMaterialHash( MaterialElement );

		if ( !ImportContext.ImportedParentMaterials.Contains( MaterialHash ) )
		{
			UMaterialInterface* ParentMaterial = FDatasmithMaterialExpressions::CreateUEPbrMaterial( ImportContext.AssetsContext.MasterMaterialsImportPackage.Get(), MaterialElement, ImportContext.AssetsContext, nullptr, ImportContext.ObjectFlags );

			if (ParentMaterial == nullptr)
			{
				return nullptr;
			}

			ImportContext.ImportedParentMaterials.Add( MaterialHash ) = ParentMaterial;
		}

		// Always create a material instance
		{
			Material = FDatasmithMaterialExpressions::CreateUEPbrMaterialInstance( ImportContext.AssetsContext.MaterialsImportPackage.Get(), MaterialElement, ImportContext.AssetsContext,
				Cast< UMaterial >( ImportContext.ImportedParentMaterials[ MaterialHash ] ), ImportContext.ObjectFlags );
		}
	}

	if (Material != nullptr)
	{
		ImportContext.ImportedMaterials.Add( BaseMaterialElement ) = Material;
	}

	return Material;
}

UMaterialInterface* FDatasmithMaterialImporter::ImportMasterMaterial( FDatasmithImportContext& ImportContext, const TSharedRef< IDatasmithMasterMaterialElement >& MaterialElement, UMaterialInterface* ExistingMaterial )
{
	// Verify existing material is of the right class for further processing
	UMaterialInstanceConstant* FoundConstantMaterial = Cast<UMaterialInstanceConstant>(ExistingMaterial);

	FString Host = FDatasmithMasterMaterialManager::Get().GetHostFromString(ImportContext.Scene->GetHost());
	TSharedPtr< FDatasmithMasterMaterialSelector > MaterialSelector = FDatasmithMasterMaterialManager::Get().GetSelector( *Host );

	const FDatasmithMasterMaterial* MasterMaterial = nullptr;
	FDatasmithMasterMaterial CustomMasterMaterial; // MasterMaterial might point on this so keep them in the same scope

	if ( MaterialElement->GetMaterialType() == EDatasmithMasterMaterialType::Custom )
	{
		CustomMasterMaterial.FromSoftObjectPath( FSoftObjectPath( MaterialElement->GetCustomMaterialPathName() ) );

		MasterMaterial = &CustomMasterMaterial;
	}
	else if ( MaterialSelector.IsValid() && MaterialSelector->IsValid() )
	{
		MasterMaterial = &MaterialSelector->GetMasterMaterial(MaterialElement);
	}

	if ( MasterMaterial && MasterMaterial->IsValid() )
	{
		const FString MaterialLabel = MaterialElement->GetLabel();
		const FString MaterialName = MaterialLabel.Len() > 0 ? ImportContext.AssetsContext.MaterialNameProvider.GenerateUniqueName(MaterialLabel) : MaterialElement->GetName();

		// Verify that the material could be created in final package
		FText FailReason;
		if (!FDatasmithImporterUtils::CanCreateAsset<UMaterialInstanceConstant>( ImportContext.AssetsContext.MaterialsFinalPackage.Get(), MaterialName, FailReason ))
		{
			ImportContext.LogError(FailReason);
			return nullptr;
		}

		UMaterialInstanceConstant* MaterialInstance = FoundConstantMaterial;

		if (MaterialInstance == nullptr)
		{
			MaterialInstance = NewObject<UMaterialInstanceConstant>(ImportContext.AssetsContext.MaterialsImportPackage.Get(), *MaterialName, ImportContext.ObjectFlags);
			MaterialInstance->Parent = MasterMaterial->GetMaterial();

			FAssetRegistryModule::AssetCreated(MaterialInstance);
		}
		else
		{
			MaterialInstance = DuplicateObject< UMaterialInstanceConstant >(MaterialInstance, ImportContext.AssetsContext.MaterialsImportPackage.Get(), *MaterialName);
			IDatasmithImporterModule::Get().ResetOverrides(MaterialInstance); // Don't copy the existing overrides
		}

		UDatasmithMaterialInstanceTemplate* MaterialInstanceTemplate = NewObject< UDatasmithMaterialInstanceTemplate >( MaterialInstance );

		MaterialInstanceTemplate->ParentMaterial = MaterialInstance->Parent;

		// Find matching master material parameters
		for (int i = 0; i < MaterialElement->GetPropertiesCount(); ++i)
		{
			const TSharedPtr< IDatasmithKeyValueProperty > Property = MaterialElement->GetProperty(i);
			FString PropertyName(Property->GetName());

			// Vector Params
			if ( MasterMaterial->VectorParams.Contains(PropertyName) )
			{
				FLinearColor Color;
				if ( MaterialSelector->GetColor( Property, Color ) )
				{
					MaterialInstanceTemplate->VectorParameterValues.Add( FName(*PropertyName), Color );
				}
			}
			// Scalar Params
			else if ( MasterMaterial->ScalarParams.Contains(PropertyName) )
			{
				float Value;
				if ( MaterialSelector->GetFloat( Property, Value ) )
				{
					MaterialInstanceTemplate->ScalarParameterValues.Add( FName(*PropertyName), Value );
				}
			}
			// Bool Params
			else if (MasterMaterial->BoolParams.Contains(PropertyName))
			{
				bool bValue;
				if ( MaterialSelector->GetBool( Property, bValue ) )
				{
					MaterialInstanceTemplate->StaticParameters.StaticSwitchParameters.Add( FName( Property->GetName() ), bValue );
				}
			}
			// Texture Params
			else if (MasterMaterial->TextureParams.Contains(PropertyName))
			{
				FString TexturePath;
				if ( MaterialSelector->GetTexture( Property, TexturePath ) )
				{
					FString TextureName = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename( TexturePath ));

					UTexture* Texture = FDatasmithImporterUtils::FindAsset< UTexture >( ImportContext.AssetsContext, *TextureName );
					MaterialInstanceTemplate->TextureParameterValues.Add( FName(*PropertyName), Texture );

					//If we are adding a virtual texture to a non-virtual texture streamer then we will need to convert back that Virtual texture.
					UTexture* DefaultTextureValue = nullptr;
					UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
					if (Texture2D && Texture2D->VirtualTextureStreaming &&
						MaterialInstance->GetTextureParameterDefaultValue(FName(*PropertyName), DefaultTextureValue) && DefaultTextureValue)
					{
						if (!DefaultTextureValue->VirtualTextureStreaming)
						{
							ImportContext.AssetsContext.VirtualTexturesToConvert.Add(Texture2D);
						}
					}
				}
			}
		}

		MaterialInstanceTemplate->Apply( MaterialInstance );

		if (MaterialSelector.IsValid() && MaterialSelector->IsValid())
		{
			MaterialSelector->FinalizeMaterialInstance(MaterialElement, MaterialInstance);
		}

		return MaterialInstance;
	}

	return nullptr;
}

int32 FDatasmithMaterialImporter::GetMaterialRequirements(UMaterialInterface * MaterialInterface)
{
	if (MaterialInterface == nullptr || MaterialInterface->GetMaterial() == nullptr)
	{
		return EMaterialRequirements::RequiresNothing;
	}
	// Currently all Datasmith materials require at least normals and tangents
	// @todo: Adjust initial value and logic based on future materials' requirements
	int32 MaterialRequirement = EMaterialRequirements::RequiresNormals | EMaterialRequirements::RequiresTangents;

	UMaterial* Material = MaterialInterface->GetMaterial();
	// Material with displacement or support for PNT requires adjacency and has their TessellationMultiplier set
	if (Material->TessellationMultiplier.Expression != nullptr || Material->D3D11TessellationMode != EMaterialTessellationMode::MTM_NoTessellation)
	{
		MaterialRequirement |= EMaterialRequirements::RequiresAdjacency;
	}

	return MaterialRequirement;
}
