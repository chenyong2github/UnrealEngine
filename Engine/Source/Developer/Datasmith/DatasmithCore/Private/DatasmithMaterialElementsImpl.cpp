// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithMaterialElementsImpl.h"

FDatasmithMaterialExpressionBoolImpl::FDatasmithMaterialExpressionBoolImpl()
{
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("Out") ) );
}

FDatasmithMaterialExpressionColorImpl::FDatasmithMaterialExpressionColorImpl()
{
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("RGB") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("R") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("G") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("B") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("A") ) );
}

FDatasmithMaterialExpressionScalarImpl::FDatasmithMaterialExpressionScalarImpl()
{
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("Out") ) );
}

FDatasmithMaterialExpressionTextureImpl::FDatasmithMaterialExpressionTextureImpl()
	: TextureCoordinate( TEXT("Coordinates") )
{
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("RGB") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("R") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("G") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("B") ) );
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("A") ) );
}

FDatasmithMaterialExpressionTextureCoordinateImpl::FDatasmithMaterialExpressionTextureCoordinateImpl()
	: CoordinateIndex( 0 )
	, UTiling( 1.f )
	, VTiling( 1.f )
{
}

FDatasmithMaterialExpressionFlattenNormalImpl::FDatasmithMaterialExpressionFlattenNormalImpl()
	: Normal( TEXT("Normal") )
	, Flatness( TEXT("Flatness") )
{
	Outputs.Emplace( FDatasmithExpressionOutputImpl( TEXT("RGB") ) );
}

TSharedPtr< IDatasmithKeyValueProperty > FDatasmithMaterialExpressionGenericImpl::NullPropertyPtr;

const TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMaterialExpressionGenericImpl::GetProperty( int32 InIndex ) const
{
	if ( Properties.IsValidIndex( InIndex ) )
	{
		return Properties[InIndex];
	}

	return NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMaterialExpressionGenericImpl::GetProperty( int32 InIndex )
{
	if ( Properties.IsValidIndex( InIndex ) )
	{
		return Properties[InIndex];
	}

	return NullPropertyPtr;
}

const TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMaterialExpressionGenericImpl::GetPropertyByName( const TCHAR* InName ) const
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMaterialExpressionGenericImpl::GetPropertyByName( const TCHAR* InName )
{
	const int* Index = PropertyIndexMap.Find(InName);
	return Index != nullptr ? GetProperty( *Index ) : NullPropertyPtr;
}

void FDatasmithMaterialExpressionGenericImpl::AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty )
{
	if ( !PropertyIndexMap.Contains( InProperty->GetName() ) )
	{
		PropertyIndexMap.Add( InProperty->GetName() ) = Properties.Add( InProperty );
	}
}

FDatasmithUEPbrMaterialElementImpl::FDatasmithUEPbrMaterialElementImpl( const TCHAR* InName )
	: FDatasmithBaseMaterialElementImpl( InName, EDatasmithElementType::UEPbrMaterial )
	, BaseColor( TEXT("BaseColor") )
	, Metallic( TEXT("Metallic") )
	, Specular( TEXT("Specular") )
	, Roughness( TEXT("Roughness") )
	, EmissiveColor( TEXT("EmissiveColor") )
	, Opacity( TEXT("Opacity") )
	, Normal( TEXT("Normal") )
	, WorldDisplacement( TEXT("WorldDisplacement") )
	, Refraction( TEXT("Refraction") )
	, AmbientOcclusion( TEXT("AmbientOcclusion") )
	, MaterialAttributes( TEXT("MaterialAttributes") )
    , BlendMode(0)
	, bTwoSided( false )
	, bUseMaterialAttributes( false )
	, bMaterialFunctionOnly ( false )
{
}

FDatasmithUEPbrMaterialElementImpl::~FDatasmithUEPbrMaterialElementImpl()
{
	for ( IDatasmithMaterialExpression* MaterialExpression : Expressions )
	{
		delete MaterialExpression;
	}
}

int32 FDatasmithUEPbrMaterialElementImpl::GetExpressionIndex( const IDatasmithMaterialExpression* Expression ) const
{
	int32 ExpressionIndex = INDEX_NONE;

	for ( int32 Index = 0; Index < Expressions.Num(); ++Index )
	{
		if ( Expression == Expressions[ Index ] )
		{
			ExpressionIndex = Index;
			break;
		}
	}

	return ExpressionIndex;
}

IDatasmithMaterialExpression* FDatasmithUEPbrMaterialElementImpl::AddMaterialExpression( const EDatasmithMaterialExpressionType ExpressionType )
{
	IDatasmithMaterialExpression* Expression = nullptr;

	switch ( ExpressionType )
	{
	case EDatasmithMaterialExpressionType::ConstantBool:
		Expression = new FDatasmithMaterialExpressionBoolImpl();
		break;
	case EDatasmithMaterialExpressionType::ConstantColor:
		Expression = new FDatasmithMaterialExpressionColorImpl();
		break;
	case EDatasmithMaterialExpressionType::ConstantScalar:
		Expression = new FDatasmithMaterialExpressionScalarImpl();
		break;
	case EDatasmithMaterialExpressionType::FlattenNormal:
		Expression = new FDatasmithMaterialExpressionFlattenNormalImpl();
		break;
	case EDatasmithMaterialExpressionType::FunctionCall:
		Expression = new FDatasmithMaterialExpressionFunctionCallImpl();
		break;
	case EDatasmithMaterialExpressionType::Generic:
		Expression = new FDatasmithMaterialExpressionGenericImpl();
		break;
	case EDatasmithMaterialExpressionType::Texture:
		Expression = new FDatasmithMaterialExpressionTextureImpl();
		break;
	case EDatasmithMaterialExpressionType::TextureCoordinate:
		Expression = new FDatasmithMaterialExpressionTextureCoordinateImpl();
		break;
	default:
		check( false );
		break;
	}

	Expressions.Add( Expression );

	return Expression;
}

const TCHAR* FDatasmithUEPbrMaterialElementImpl::GetParentLabel() const
{
	if ( ParentLabel.IsEmpty() )
	{
		return GetLabel();
	}
	else
	{
		return *ParentLabel;
	}
}
