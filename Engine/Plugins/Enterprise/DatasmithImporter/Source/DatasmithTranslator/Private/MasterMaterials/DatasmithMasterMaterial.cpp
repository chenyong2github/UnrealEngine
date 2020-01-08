// Copyright Epic Games, Inc. All Rights Reserved.

#include "MasterMaterials/DatasmithMasterMaterial.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"

#include "UObject/SoftObjectPath.h"

FDatasmithMasterMaterial::FDatasmithMasterMaterial()
	: Material( nullptr )
{
}

void FDatasmithMasterMaterial::FromMaterial( UMaterial* InMaterial )
{
#if WITH_EDITOR
	if ( InMaterial )
	{
		for ( UMaterialExpression* Expression : InMaterial->Expressions )
		{
			FString ExpressionName = Expression->GetName();

			if ( Expression->IsA< UMaterialExpressionVectorParameter >() )
			{
				VectorParams.Add( Expression->GetParameterName().ToString() );
			}
			else if ( Expression->IsA< UMaterialExpressionScalarParameter >() )
			{
				ScalarParams.Add( Expression->GetParameterName().ToString() );
			}
			else if ( Expression->IsA< UMaterialExpressionTextureSampleParameter >() )
			{
				TextureParams.Add( Expression->GetParameterName().ToString() );
			}
			else if ( Expression->IsA< UMaterialExpressionStaticBoolParameter >() )
			{
				BoolParams.Add( Expression->GetParameterName().ToString() );
			}
		}
	}
#endif

	Material = InMaterial;
}

void FDatasmithMasterMaterial::FromSoftObjectPath( const FSoftObjectPath& InObjectPath)
{
	FromMaterial( Cast< UMaterial >(InObjectPath.TryLoad() ) );
}
