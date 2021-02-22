// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeUEPbrMaterial.h"

#include "DatasmithFacadeKeyValueProperty.h"
#include "DatasmithFacadeScene.h"

#include "DatasmithUtils.h"
#include "Misc/Paths.h"

FDatasmithFacadeMaterialExpression* CreateFacadeExpression( const TSharedPtr<IDatasmithMaterialExpression>& MaterialExpression )
{
	if ( MaterialExpression )
	{
		const TSharedRef<IDatasmithMaterialExpression> MaterialExpressionRef = MaterialExpression.ToSharedRef();

		switch ( MaterialExpression->GetExpressionType() )
		{
		case EDatasmithMaterialExpressionType::ConstantBool:
			return new FDatasmithFacadeMaterialExpressionBool( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::ConstantColor:
			return new FDatasmithFacadeMaterialExpressionColor( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::ConstantScalar:
			return new FDatasmithFacadeMaterialExpressionScalar( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::FlattenNormal:
			return new FDatasmithFacadeMaterialExpressionFlattenNormal( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::FunctionCall:
			return new FDatasmithFacadeMaterialExpressionFunctionCall( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::Generic:
			return new FDatasmithFacadeMaterialExpressionGeneric( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::Texture:
			return new FDatasmithFacadeMaterialExpressionTexture( MaterialExpressionRef );
		case EDatasmithMaterialExpressionType::TextureCoordinate:
			return new FDatasmithFacadeMaterialExpressionTextureCoordinate( MaterialExpressionRef );
		default:
			break;
		}
	}
	
	return nullptr;
}

FDatasmithFacadeExpressionInput::FDatasmithFacadeExpressionInput( const TSharedRef<IDatasmithExpressionInput>& InExpressionInput )
	: FDatasmithFacadeElement( InExpressionInput )
{}

TSharedRef<IDatasmithExpressionInput> FDatasmithFacadeExpressionInput::GetExpressionInput() const 
{ 
	return StaticCastSharedRef< IDatasmithExpressionInput >( InternalDatasmithElement );
}

FDatasmithFacadeMaterialExpression* FDatasmithFacadeExpressionInput::GetNewFacadeExpression()
{
	return CreateFacadeExpression( GetExpressionInput()->GetExpression() );
}

void FDatasmithFacadeExpressionInput::SetExpression( FDatasmithFacadeMaterialExpression* InExpression )
{
	GetExpressionInput()->SetExpression( InExpression->GetMaterialExpression() );
}

int32 FDatasmithFacadeExpressionInput::GetOutputIndex() const
{
	return GetExpressionInput()->GetOutputIndex();
}

void FDatasmithFacadeExpressionInput::SetOutputIndex( int32 InOutputIndex )
{
	GetExpressionInput()->SetOutputIndex( InOutputIndex );
}

FDatasmithFacadeMaterialExpression::FDatasmithFacadeMaterialExpression( const TSharedRef<IDatasmithMaterialExpression>& InMaterialExpression )
	: FDatasmithFacadeElement(InMaterialExpression)
{}

TSharedRef<IDatasmithMaterialExpression> FDatasmithFacadeMaterialExpression::GetMaterialExpression() const
{
	return StaticCastSharedRef<IDatasmithMaterialExpression>(InternalDatasmithElement);
}

EDatasmithFacadeMaterialExpressionType FDatasmithFacadeMaterialExpression::GetExpressionType() const
{
	return static_cast<EDatasmithFacadeMaterialExpressionType>( GetMaterialExpression()->GetExpressionType() );
}

void FDatasmithFacadeMaterialExpression::ConnectExpression( FDatasmithFacadeExpressionInput& ExpressionInput )
{
	GetMaterialExpression()->ConnectExpression( ExpressionInput.GetExpressionInput() );
}

void FDatasmithFacadeMaterialExpression::ConnectExpression( FDatasmithFacadeExpressionInput& ExpressionInput, int32 OutputIndex )
{
	GetMaterialExpression()->ConnectExpression( ExpressionInput.GetExpressionInput(), OutputIndex );
}

int32 FDatasmithFacadeMaterialExpression::GetInputCount() const
{
	return GetMaterialExpression()->GetInputCount();
}

FDatasmithFacadeExpressionInput* FDatasmithFacadeMaterialExpression::GetNewFacadeInput( int32 Index )
{
	if ( TSharedPtr<IDatasmithExpressionInput> ExpressionInput = GetMaterialExpression()->GetInput( Index ) )
	{
		return new FDatasmithFacadeExpressionInput( ExpressionInput.ToSharedRef() );
	}

	return nullptr;
}

int32 FDatasmithFacadeMaterialExpression::GetDefaultOutputIndex() const
{
	return GetMaterialExpression()->GetDefaultOutputIndex();
}

void FDatasmithFacadeMaterialExpression::SetDefaultOutputIndex( int32 OutputIndex )
{
	GetMaterialExpression()->SetDefaultOutputIndex( OutputIndex );
}

bool FDatasmithFacadeMaterialExpressionBool::GetBool() const
{
	return static_cast<IDatasmithMaterialExpressionBool*>( &GetMaterialExpression().Get() )->GetBool();
}

void FDatasmithFacadeMaterialExpressionBool::SetBool( bool InValue )
{
	static_cast<IDatasmithMaterialExpressionBool*>( &GetMaterialExpression().Get() )->GetBool() = InValue;
}

const TCHAR* FDatasmithFacadeMaterialExpressionBool::GetGroupName() const
{
	return static_cast<IDatasmithMaterialExpressionBool*>( &GetMaterialExpression().Get() )->GetGroupName();
}

void FDatasmithFacadeMaterialExpressionBool::SetGroupName( const TCHAR* InGroupName )
{
	static_cast<IDatasmithMaterialExpressionBool*>( &GetMaterialExpression().Get() )->SetGroupName( InGroupName );
}

void FDatasmithFacadeMaterialExpressionColor::GetsRGBColor( uint8& OutR, uint8& OutG, uint8& OutB, uint8& OutA ) const
{
	const FLinearColor& ExpressionColor = static_cast<IDatasmithMaterialExpressionColor*>( &GetMaterialExpression().Get() )->GetColor();
	FColor Color = ExpressionColor.ToFColor( /*bSRGB=*/true );
	OutR = Color.R;
	OutG = Color.G;
	OutB = Color.B;
	OutA = Color.A;
}

void FDatasmithFacadeMaterialExpressionColor::SetsRGBColor( uint8 R, uint8 G, uint8 B, uint8 A )
{
	//Passing a FColor to the FLinearColor constructor will do the proper color space conversion.
	static_cast<IDatasmithMaterialExpressionColor*>( &GetMaterialExpression().Get() )->GetColor() = FLinearColor( FColor( R, G, B, A ) );
}

void FDatasmithFacadeMaterialExpressionColor::GetColor( float& OutR, float& OutG, float& OutB, float& OutA ) const
{
	const FLinearColor& ExpressionColor = static_cast<IDatasmithMaterialExpressionColor*>( &GetMaterialExpression().Get() )->GetColor();
	OutR = ExpressionColor.R;
	OutG = ExpressionColor.G;
	OutB = ExpressionColor.B;
	OutA = ExpressionColor.A;
}

void FDatasmithFacadeMaterialExpressionColor::SetColor( float R, float G, float B, float A )
{
	static_cast<IDatasmithMaterialExpressionColor*>( &GetMaterialExpression().Get() )->GetColor() = FLinearColor( R, G, B, A );
}

const TCHAR* FDatasmithFacadeMaterialExpressionColor::GetGroupName() const
{
	return static_cast<IDatasmithMaterialExpressionColor*>( &GetMaterialExpression().Get() )->GetGroupName();
}

void FDatasmithFacadeMaterialExpressionColor::SetGroupName( const TCHAR* InGroupName )
{
	static_cast<IDatasmithMaterialExpressionColor*>( &GetMaterialExpression().Get() )->SetGroupName( InGroupName );
}

float FDatasmithFacadeMaterialExpressionScalar::GetScalar() const
{
	return static_cast<IDatasmithMaterialExpressionScalar*>( &GetMaterialExpression().Get() )->GetScalar();
}

void FDatasmithFacadeMaterialExpressionScalar::SetScalar( float InScalar )
{
	static_cast<IDatasmithMaterialExpressionScalar*>( &GetMaterialExpression().Get() )->GetScalar() = InScalar;
}

const TCHAR* FDatasmithFacadeMaterialExpressionScalar::GetGroupName() const
{
	return static_cast<IDatasmithMaterialExpressionScalar*>( &GetMaterialExpression().Get() )->GetGroupName();
}

void FDatasmithFacadeMaterialExpressionScalar::SetGroupName( const TCHAR* InGroupName )
{
	static_cast<IDatasmithMaterialExpressionScalar*>( &GetMaterialExpression().Get() )->SetGroupName( InGroupName );
}

const TCHAR* FDatasmithFacadeMaterialExpressionTexture::GetTexturePathName() const
{
	return static_cast<IDatasmithMaterialExpressionTexture*>( &GetMaterialExpression().Get() )->GetTexturePathName();
}

void FDatasmithFacadeMaterialExpressionTexture::SetTexturePathName( const TCHAR* InTexturePathName )
{
	static_cast<IDatasmithMaterialExpressionTexture*>( &GetMaterialExpression().Get() )->SetTexturePathName( InTexturePathName );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeMaterialExpressionTexture::GetInputCoordinate()
{
	return FDatasmithFacadeExpressionInput( static_cast<IDatasmithMaterialExpressionTexture*>( &GetMaterialExpression().Get() )->GetInputCoordinate().ToSharedRef() );
}

const TCHAR* FDatasmithFacadeMaterialExpressionTexture::GetGroupName() const
{
	return static_cast<IDatasmithMaterialExpressionTexture*>( &GetMaterialExpression().Get() )->GetGroupName();
}

void FDatasmithFacadeMaterialExpressionTexture::SetGroupName( const TCHAR* InGroupName )
{
	static_cast<IDatasmithMaterialExpressionTexture*>( &GetMaterialExpression().Get() )->SetGroupName( InGroupName );
}

int32 FDatasmithFacadeMaterialExpressionTextureCoordinate::GetCoordinateIndex() const
{
	return static_cast<IDatasmithMaterialExpressionTextureCoordinate*>( &GetMaterialExpression().Get() )->GetCoordinateIndex();
}

void FDatasmithFacadeMaterialExpressionTextureCoordinate::SetCoordinateIndex( int32 InCoordinateIndex )
{
	static_cast<IDatasmithMaterialExpressionTextureCoordinate*>( &GetMaterialExpression().Get() )->SetCoordinateIndex( InCoordinateIndex );
}

float FDatasmithFacadeMaterialExpressionTextureCoordinate::GetUTiling() const
{
	return static_cast<IDatasmithMaterialExpressionTextureCoordinate*>( &GetMaterialExpression().Get() )->GetUTiling();
}

void FDatasmithFacadeMaterialExpressionTextureCoordinate::SetUTiling( float InUTiling )
{
	static_cast<IDatasmithMaterialExpressionTextureCoordinate*>( &GetMaterialExpression().Get() )->SetUTiling( InUTiling );
}

float FDatasmithFacadeMaterialExpressionTextureCoordinate::GetVTiling() const
{
	return static_cast<IDatasmithMaterialExpressionTextureCoordinate*>( &GetMaterialExpression().Get() )->GetVTiling();
}

void FDatasmithFacadeMaterialExpressionTextureCoordinate::SetVTiling( float InVTiling )
{
	static_cast<IDatasmithMaterialExpressionTextureCoordinate*>( &GetMaterialExpression().Get() )->SetVTiling( InVTiling );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeMaterialExpressionFlattenNormal::GetNormal() const
{
	return FDatasmithFacadeExpressionInput( static_cast<IDatasmithMaterialExpressionFlattenNormal*>( &GetMaterialExpression().Get() )->GetNormal().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeMaterialExpressionFlattenNormal::GetFlatness() const
{
	return FDatasmithFacadeExpressionInput( static_cast<IDatasmithMaterialExpressionFlattenNormal*>( &GetMaterialExpression().Get() )->GetFlatness().ToSharedRef() );
}

void FDatasmithFacadeMaterialExpressionGeneric::SetExpressionName( const TCHAR* InExpressionName )
{
	static_cast<IDatasmithMaterialExpressionGeneric*>( &GetMaterialExpression().Get() )->SetExpressionName( InExpressionName );
}

const TCHAR* FDatasmithFacadeMaterialExpressionGeneric::GetExpressionName() const
{
	return static_cast<IDatasmithMaterialExpressionGeneric*>( &GetMaterialExpression().Get() )->GetExpressionName();
}

int32 FDatasmithFacadeMaterialExpressionGeneric::GetPropertiesCount() const
{
	return static_cast<IDatasmithMaterialExpressionGeneric*>( &GetMaterialExpression().Get() )->GetPropertiesCount();
}

void FDatasmithFacadeMaterialExpressionGeneric::AddProperty( const FDatasmithFacadeKeyValueProperty* InPropertyPtr )
{
	if ( InPropertyPtr )
	{
		static_cast<IDatasmithMaterialExpressionGeneric*>( &GetMaterialExpression().Get() )->AddProperty( InPropertyPtr->GetDatasmithKeyValueProperty() );
	}
}

FDatasmithFacadeKeyValueProperty* FDatasmithFacadeMaterialExpressionGeneric::GetNewProperty( int32 Index )
{
	if ( const TSharedPtr<IDatasmithKeyValueProperty>& Property = static_cast<IDatasmithMaterialExpressionGeneric*>( &GetMaterialExpression().Get() )->GetProperty( Index ) )
	{
		return new FDatasmithFacadeKeyValueProperty( Property.ToSharedRef() );
	}

	return nullptr;
}

void FDatasmithFacadeMaterialExpressionFunctionCall::SetFunctionPathName( const TCHAR* InFunctionPathName )
{
	static_cast<IDatasmithMaterialExpressionFunctionCall*>( &GetMaterialExpression().Get() )->SetFunctionPathName( InFunctionPathName );
}

const TCHAR* FDatasmithFacadeMaterialExpressionFunctionCall::GetFunctionPathName() const
{
	return static_cast<IDatasmithMaterialExpressionFunctionCall*>( &GetMaterialExpression().Get() )->GetFunctionPathName();
}

FDatasmithFacadeUEPbrMaterial::FDatasmithFacadeUEPbrMaterial( const TCHAR* InElementName )
	: FDatasmithFacadeBaseMaterial( FDatasmithSceneFactory::CreateUEPbrMaterial( InElementName ) )
{}

FDatasmithFacadeUEPbrMaterial::FDatasmithFacadeUEPbrMaterial( const TSharedRef<IDatasmithUEPbrMaterialElement>& InMaterialRef )
	: FDatasmithFacadeBaseMaterial( InMaterialRef )
{}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetBaseColor() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetBaseColor().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetMetallic() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetMetallic().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetSpecular() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetSpecular().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetRoughness() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetRoughness().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetEmissiveColor() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetEmissiveColor().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetOpacity() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetOpacity().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetNormal() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetNormal().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetWorldDisplacement() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetWorldDisplacement().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetRefraction() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetRefraction().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetAmbientOcclusion() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetAmbientOcclusion().ToSharedRef() );
}

FDatasmithFacadeExpressionInput FDatasmithFacadeUEPbrMaterial::GetMaterialAttributes() const
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return FDatasmithFacadeExpressionInput( UEPbrMaterial->GetMaterialAttributes().ToSharedRef() );
}

int FDatasmithFacadeUEPbrMaterial::GetBlendMode() const
{
	return GetDatasmithUEPbrMaterialElement()->GetBlendMode();
}

void FDatasmithFacadeUEPbrMaterial::SetBlendMode( int bInBlendMode )
{
	GetDatasmithUEPbrMaterialElement()->SetBlendMode( bInBlendMode );
}

bool FDatasmithFacadeUEPbrMaterial::GetTwoSided() const
{
	return GetDatasmithUEPbrMaterialElement()->GetTwoSided();
}

void FDatasmithFacadeUEPbrMaterial::SetTwoSided( bool bTwoSided )
{
	GetDatasmithUEPbrMaterialElement()->SetTwoSided( bTwoSided );
}

bool FDatasmithFacadeUEPbrMaterial::GetUseMaterialAttributes() const
{
	return GetDatasmithUEPbrMaterialElement()->GetUseMaterialAttributes();
}

void FDatasmithFacadeUEPbrMaterial::SetUseMaterialAttributes( bool bInUseMaterialAttributes )
{
	GetDatasmithUEPbrMaterialElement()->SetUseMaterialAttributes( bInUseMaterialAttributes );
}

bool FDatasmithFacadeUEPbrMaterial::GetMaterialFunctionOnly() const
{
	return GetDatasmithUEPbrMaterialElement()->GetMaterialFunctionOnly();
}

void FDatasmithFacadeUEPbrMaterial::SetMaterialFunctionOnly( bool bInMaterialFunctionOnly )
{
	GetDatasmithUEPbrMaterialElement()->SetMaterialFunctionOnly( bInMaterialFunctionOnly );
}

float FDatasmithFacadeUEPbrMaterial::GetOpacityMaskClipValue() const
{
	return GetDatasmithUEPbrMaterialElement()->GetOpacityMaskClipValue();
}

void FDatasmithFacadeUEPbrMaterial::SetOpacityMaskClipValue( float InClipValue )
{
	GetDatasmithUEPbrMaterialElement()->SetOpacityMaskClipValue( InClipValue );
}

int32 FDatasmithFacadeUEPbrMaterial::GetExpressionsCount() const
{
	return GetDatasmithUEPbrMaterialElement()->GetExpressionsCount();
}

FDatasmithFacadeMaterialExpression* FDatasmithFacadeUEPbrMaterial::GetNewFacadeExpression( int32 Index )
{
	TSharedPtr<IDatasmithUEPbrMaterialElement> UEPbrMaterial = GetDatasmithUEPbrMaterialElement();
	return CreateFacadeExpression( UEPbrMaterial->GetExpression( Index ) );
}

int32 FDatasmithFacadeUEPbrMaterial::GetExpressionIndex( const FDatasmithFacadeMaterialExpression& Expression ) const
{
	return GetDatasmithUEPbrMaterialElement()->GetExpressionIndex( Expression.GetMaterialExpression() );
}

TSharedPtr<IDatasmithMaterialExpression> FDatasmithFacadeUEPbrMaterial::AddMaterialExpression( const EDatasmithFacadeMaterialExpressionType ExpressionType )
{
	return GetDatasmithUEPbrMaterialElement()->AddMaterialExpression( static_cast<EDatasmithMaterialExpressionType>( ExpressionType ) );
}

void FDatasmithFacadeUEPbrMaterial::SetParentLabel( const TCHAR* InParentLabel )
{
	GetDatasmithUEPbrMaterialElement()->SetParentLabel( InParentLabel );
}

const TCHAR* FDatasmithFacadeUEPbrMaterial::GetParentLabel() const
{
	return GetDatasmithUEPbrMaterialElement()->GetParentLabel();
}

TSharedRef<IDatasmithUEPbrMaterialElement> FDatasmithFacadeUEPbrMaterial::GetDatasmithUEPbrMaterialElement() const
{
	return StaticCastSharedRef<IDatasmithUEPbrMaterialElement>( InternalDatasmithElement );
}