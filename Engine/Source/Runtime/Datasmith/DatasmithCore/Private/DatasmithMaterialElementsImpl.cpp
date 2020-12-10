// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithMaterialElementsImpl.h"


IDatasmithMaterialExpression* FDatasmithUEPbrInternalHelper::ConvertElementToMaterialExpression( IDatasmithElement* InElement, EDatasmithMaterialExpressionType ExpressionType )
{
	switch (ExpressionType)
	{
	case EDatasmithMaterialExpressionType::ConstantBool:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionBool >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::ConstantColor:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionColor >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::ConstantScalar:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionScalar >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::FlattenNormal:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionFlattenNormal >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::FunctionCall:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionFunctionCall >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::Generic:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionGeneric >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::Texture:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionTexture >*>(InElement);
		break;
	case EDatasmithMaterialExpressionType::TextureCoordinate:
		return static_cast<FDatasmithMaterialExpressionElement< IDatasmithMaterialExpressionTextureCoordinate >*>(InElement);
		break;
	default:
		return nullptr;
		break;
	}
}

TSharedPtr<IDatasmithElement> FDatasmithUEPbrInternalHelper::ConvertMaterialExpressionToElementSharedPtr( IDatasmithMaterialExpression* InExpression )
{
	if (InExpression)
	{
		EDatasmithMaterialExpressionType InExpressionType = InExpression->GetType();
		switch (InExpressionType)
		{
		case EDatasmithMaterialExpressionType::ConstantBool:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionBool>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::ConstantColor:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionColor>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::ConstantScalar:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionScalar>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::FlattenNormal:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionFlattenNormal>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::FunctionCall:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionFunctionCall>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::Generic:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionGeneric>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::Texture:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionTexture>*>(InExpression)->AsShared();
			break;
		case EDatasmithMaterialExpressionType::TextureCoordinate:
			return static_cast<FDatasmithMaterialExpressionImpl<IDatasmithMaterialExpressionTextureCoordinate>*>(InExpression)->AsShared();
			break;
		default:
			break;
		}
	}

	return nullptr;
}

TSharedPtr< IDatasmithMaterialExpression > FDatasmithUEPbrInternalHelper::CreateMaterialExpression( EDatasmithMaterialExpressionType MaterialExpression )
{
	TSharedPtr<IDatasmithMaterialExpression> Expression;

	switch (MaterialExpression)
	{
	case EDatasmithMaterialExpressionType::ConstantBool:
		Expression = MakeShared<FDatasmithMaterialExpressionBoolImpl>();
		break;
	case EDatasmithMaterialExpressionType::ConstantColor:
		Expression = MakeShared<FDatasmithMaterialExpressionColorImpl>();
		break;
	case EDatasmithMaterialExpressionType::ConstantScalar:
		Expression = MakeShared<FDatasmithMaterialExpressionScalarImpl>();
		break;
	case EDatasmithMaterialExpressionType::FlattenNormal:
		Expression = MakeShared<FDatasmithMaterialExpressionFlattenNormalImpl>();
		break;
	case EDatasmithMaterialExpressionType::FunctionCall:
		Expression = MakeShared<FDatasmithMaterialExpressionFunctionCallImpl>();
		break;
	case EDatasmithMaterialExpressionType::Generic:
		Expression = MakeShared<FDatasmithMaterialExpressionGenericImpl>();
		break;
	case EDatasmithMaterialExpressionType::Texture:
		Expression = MakeShared<FDatasmithMaterialExpressionTextureImpl>();
		break;
	case EDatasmithMaterialExpressionType::TextureCoordinate:
		Expression = MakeShared<FDatasmithMaterialExpressionTextureCoordinateImpl>();
		break;
	default:
		check( false );
		break;
	}

	return Expression;
}

FDatasmithExpressionInputImpl::FDatasmithExpressionInputImpl( const TCHAR* InInputName )
	: FDatasmithElementImpl< FDatasmithExpressionInputElement >( InInputName, static_cast< EDatasmithElementType >(FDatasmithUEPbrInternalHelper::MaterialExpressionInputType ) )
	, Expression()
	, OutputIndex( 0 )
{
	RegisterReferenceProxy( Expression, "Expression" );

	Store.RegisterParameter( ExpressionType, "ExpressionType" );
	Store.RegisterParameter( OutputIndex, "OutputIndex" );
}

IDatasmithMaterialExpression* FDatasmithExpressionInputImpl::GetExpression() 
{ 
	return FDatasmithUEPbrInternalHelper::ConvertElementToMaterialExpression(Expression.View().Get(), ExpressionType);
}

const IDatasmithMaterialExpression* FDatasmithExpressionInputImpl::GetExpression() const
{
	return FDatasmithUEPbrInternalHelper::ConvertElementToMaterialExpression(Expression.View().Get(), ExpressionType);
}

void FDatasmithExpressionInputImpl::SetExpression( IDatasmithMaterialExpression* InExpression )
{
	if (InExpression)
	{
		Expression.Edit() = FDatasmithUEPbrInternalHelper::ConvertMaterialExpressionToElementSharedPtr( InExpression );
		ExpressionType = InExpression->GetType();
	}
	else
	{
		Expression.Edit() = nullptr;
	}
}

FDatasmithMaterialExpressionBoolImpl::FDatasmithMaterialExpressionBoolImpl()
	: FDatasmithExpressionParameterImpl( EDatasmithMaterialExpressionType::ConstantBool )
{
	Store.RegisterParameter( bValue, "bValue" );

	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "Out" ) ) );
}

FDatasmithMaterialExpressionColorImpl::FDatasmithMaterialExpressionColorImpl()
	: FDatasmithExpressionParameterImpl( EDatasmithMaterialExpressionType::ConstantColor )
{
	Store.RegisterParameter( LinearColor, "LinearColor" );

	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "RGB" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "R" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "G" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "B" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "A" ) ) );
}

FDatasmithMaterialExpressionScalarImpl::FDatasmithMaterialExpressionScalarImpl()
	: FDatasmithExpressionParameterImpl( EDatasmithMaterialExpressionType::ConstantScalar )
{
	Store.RegisterParameter( Scalar, "Scalar" );

	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "Out" ) ) );
}

FDatasmithMaterialExpressionTextureImpl::FDatasmithMaterialExpressionTextureImpl()
	: FDatasmithExpressionParameterImpl( EDatasmithMaterialExpressionType::Texture )
	, TextureCoordinate( MakeShared< FDatasmithExpressionInputImpl >( TEXT("Coordinates") ) )
{
	Store.RegisterParameter( TexturePathName, "TexturePathName" );
	RegisterReferenceProxy( TextureCoordinate, "TextureCoordinate" );

	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "RGB" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "R" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "G" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "B" ) ) );
	Outputs.Add( MakeShared< FDatasmithExpressionOutputImpl >( TEXT( "A" ) ) );
}

FDatasmithMaterialExpressionTextureCoordinateImpl::FDatasmithMaterialExpressionTextureCoordinateImpl()
	: FDatasmithMaterialExpressionImpl( EDatasmithMaterialExpressionType::TextureCoordinate )
	, CoordinateIndex( 0 )
	, UTiling( 1.f )
	, VTiling( 1.f )
{
	Store.RegisterParameter( CoordinateIndex, "CoordinateIndex" );
	Store.RegisterParameter( UTiling, "UTiling" );
	Store.RegisterParameter( VTiling, "VTiling" );
}

FDatasmithMaterialExpressionFlattenNormalImpl::FDatasmithMaterialExpressionFlattenNormalImpl()
	: FDatasmithMaterialExpressionImpl( EDatasmithMaterialExpressionType::FlattenNormal )
	, Normal( MakeShared< FDatasmithExpressionInputImpl >( TEXT("Normal") ) )
	, Flatness( MakeShared< FDatasmithExpressionInputImpl >( TEXT("Flatness") ) )
{
	RegisterReferenceProxy( Normal, "Normal" );
	RegisterReferenceProxy( Flatness, "Flatness" );

	Outputs.Add( MakeShared<FDatasmithExpressionOutputImpl>( TEXT( "RGB" ) ) );
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
	const TSharedPtr< IDatasmithKeyValueProperty >* FindResult = Properties.View().FindByPredicate( [&InName]( const TSharedPtr<IDatasmithKeyValueProperty>& CurrentKeyValue ) 
		{ 
			return FCString::Strcmp( CurrentKeyValue->GetName(), InName ) == 0; 
		});
	
	return FindResult ? *FindResult : NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty >& FDatasmithMaterialExpressionGenericImpl::GetPropertyByName( const TCHAR* InName )
{
	TSharedPtr< IDatasmithKeyValueProperty >* FindResult = Properties.Edit().FindByPredicate( [&InName]( const TSharedPtr<IDatasmithKeyValueProperty>& CurrentKeyValue )
	{
		return FCString::Strcmp( CurrentKeyValue->GetName(), InName ) == 0;
	} );

	return FindResult ? *FindResult : NullPropertyPtr;
}

void FDatasmithMaterialExpressionGenericImpl::AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty )
{
	if ( !GetPropertyByName( InProperty->GetName() ) )
	{
		Properties.Add( InProperty );
	}
}

FDatasmithUEPbrMaterialElementImpl::FDatasmithUEPbrMaterialElementImpl( const TCHAR* InName )
	: FDatasmithBaseMaterialElementImpl( InName, EDatasmithElementType::UEPbrMaterial )
	, BaseColor(          MakeShared< FDatasmithExpressionInputImpl >( TEXT("BaseColor") ) )
	, Metallic(           MakeShared< FDatasmithExpressionInputImpl >( TEXT("Metallic") ) )
	, Specular(           MakeShared< FDatasmithExpressionInputImpl >( TEXT("Specular") ) )
	, Roughness(          MakeShared< FDatasmithExpressionInputImpl >( TEXT("Roughness") ) )
	, EmissiveColor(      MakeShared< FDatasmithExpressionInputImpl >( TEXT("EmissiveColor") ) )
	, Opacity(            MakeShared< FDatasmithExpressionInputImpl >( TEXT("Opacity") ) )
	, Normal(             MakeShared< FDatasmithExpressionInputImpl >( TEXT("Normal") ) )
	, WorldDisplacement(  MakeShared< FDatasmithExpressionInputImpl >( TEXT("WorldDisplacement") ) )
	, Refraction(         MakeShared< FDatasmithExpressionInputImpl >( TEXT("Refraction") ) )
	, AmbientOcclusion(   MakeShared< FDatasmithExpressionInputImpl >( TEXT("AmbientOcclusion") ) )
	, MaterialAttributes( MakeShared< FDatasmithExpressionInputImpl >( TEXT("MaterialAttributes") ) )
    , BlendMode(0)
	, bTwoSided( false )
	, bUseMaterialAttributes( false )
	, bMaterialFunctionOnly ( false )
	, OpacityMaskClipValue( 0.3333f )
	, ShadingModel( EDatasmithShadingModel::DefaultLit )
{
	RegisterReferenceProxy( BaseColor, "BaseColor" );
	RegisterReferenceProxy( Metallic, "Metallic" );
	RegisterReferenceProxy( Specular, "Specular" );
	RegisterReferenceProxy( Roughness, "Roughness" );
	RegisterReferenceProxy( EmissiveColor, "EmissiveColor" );
	RegisterReferenceProxy( Opacity, "Opacity" );
	RegisterReferenceProxy( Normal, "Normal" );
	RegisterReferenceProxy( WorldDisplacement, "WorldDisplacement" );
	RegisterReferenceProxy( Refraction, "Refraction" );
	RegisterReferenceProxy( AmbientOcclusion, "AmbientOcclusion" );
	RegisterReferenceProxy( MaterialAttributes, "MaterialAttributes" );


	RegisterReferenceProxy( Expressions, "Expressions" );
	Store.RegisterParameter( ExpressionTypes, "ExpressionTypes" );

	Store.RegisterParameter( BlendMode, "BlendMode" );
	Store.RegisterParameter( bTwoSided, "bTwoSided" );
	Store.RegisterParameter( bUseMaterialAttributes, "bUseMaterialAttributes" );
	Store.RegisterParameter( bMaterialFunctionOnly, "bMaterialFunctionOnly" );

	Store.RegisterParameter( OpacityMaskClipValue, "OpacityMaskClipValue" );

	Store.RegisterParameter( ParentLabel, "ParentLabel" );
	Store.RegisterParameter( ShadingModel, "ShadingModel" );
}

IDatasmithMaterialExpression* FDatasmithUEPbrMaterialElementImpl::GetExpression( int32 Index )
{
	return Expressions.IsValidIndex( Index ) ? FDatasmithUEPbrInternalHelper::ConvertElementToMaterialExpression( Expressions[Index].Get(), ExpressionTypes.Get( Store )[Index] ) : nullptr;
}


int32 FDatasmithUEPbrMaterialElementImpl::GetExpressionIndex( const IDatasmithMaterialExpression* Expression ) const
{
	int32 ExpressionIndex = INDEX_NONE;

	for ( int32 Index = 0; Index < Expressions.Num(); ++Index )
	{
		IDatasmithMaterialExpression* CurrentElement = FDatasmithUEPbrInternalHelper::ConvertElementToMaterialExpression( Expressions[Index].Get(), ExpressionTypes.Get( Store )[Index] );
		if ( Expression == CurrentElement)
		{
			ExpressionIndex = Index;
			break;
		}
	}

	return ExpressionIndex;
}

IDatasmithMaterialExpression* FDatasmithUEPbrMaterialElementImpl::AddMaterialExpression( const EDatasmithMaterialExpressionType ExpressionType )
{
	TSharedPtr<IDatasmithMaterialExpression> Expression = nullptr;
	TSharedPtr<IDatasmithElement> ExpressionAsElement = nullptr;

	switch ( ExpressionType )
	{
	case EDatasmithMaterialExpressionType::ConstantBool:
	{
		TSharedPtr<FDatasmithMaterialExpressionBoolImpl> ExpressionImpl = MakeShared< FDatasmithMaterialExpressionBoolImpl >();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::ConstantColor:
	{
		TSharedPtr<FDatasmithMaterialExpressionColorImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionColorImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::ConstantScalar:
	{
		TSharedPtr<FDatasmithMaterialExpressionScalarImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionScalarImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::FlattenNormal:
	{
		TSharedPtr<FDatasmithMaterialExpressionFlattenNormalImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionFlattenNormalImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::FunctionCall:
	{
		TSharedPtr<FDatasmithMaterialExpressionFunctionCallImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionFunctionCallImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::Generic:
	{
		TSharedPtr<FDatasmithMaterialExpressionGenericImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionGenericImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::Texture:
	{
		TSharedPtr<FDatasmithMaterialExpressionTextureImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionTextureImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	case EDatasmithMaterialExpressionType::TextureCoordinate:
	{
		TSharedPtr<FDatasmithMaterialExpressionTextureCoordinateImpl> ExpressionImpl = MakeShared < FDatasmithMaterialExpressionTextureCoordinateImpl>();
		Expression = ExpressionImpl;
		ExpressionAsElement = ExpressionImpl;
		break;
	}
	default:
		check( false );
		break;
	}
	Expressions.Add( ExpressionAsElement );
	ExpressionTypes.Edit( Store ).Add( ExpressionType );

	return Expression.Get();
}

const TCHAR* FDatasmithUEPbrMaterialElementImpl::GetParentLabel() const
{
	if ( ParentLabel.Get(Store).IsEmpty() )
	{
		return GetLabel();
	}
	else
	{
		return *ParentLabel.Get(Store);
	}
}