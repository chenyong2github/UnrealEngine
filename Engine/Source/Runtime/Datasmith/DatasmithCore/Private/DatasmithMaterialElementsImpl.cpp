// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithMaterialElementsImpl.h"


FDatasmithExpressionInputImpl::FDatasmithExpressionInputImpl( const TCHAR* InInputName )
	: FDatasmithElementImpl< IDatasmithExpressionInput >( InInputName, EDatasmithElementType::MaterialExpressionInput )
	, Expression()
	, OutputIndex( 0 )
{
	RegisterReferenceProxy( Expression, "Expression" );

	Store.RegisterParameter( OutputIndex, "OutputIndex" );
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

TSharedPtr< const IDatasmithKeyValueProperty > FDatasmithMaterialExpressionGenericImpl::GetProperty( int32 InIndex ) const
{
	if ( Properties.IsValidIndex( InIndex ) )
	{
		return Properties[InIndex];
	}

	return NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty > FDatasmithMaterialExpressionGenericImpl::GetProperty( int32 InIndex )
{
	if ( Properties.IsValidIndex( InIndex ) )
	{
		return Properties[InIndex];
	}

	return NullPropertyPtr;
}

TSharedPtr< const IDatasmithKeyValueProperty > FDatasmithMaterialExpressionGenericImpl::GetPropertyByName( const TCHAR* InName ) const
{
	const TSharedPtr< IDatasmithKeyValueProperty >* FindResult = Properties.View().FindByPredicate( [&InName]( const TSharedPtr<IDatasmithKeyValueProperty>& CurrentKeyValue )
		{
			return FCString::Strcmp( CurrentKeyValue->GetName(), InName ) == 0;
		});

	return FindResult ? *FindResult : NullPropertyPtr;
}

TSharedPtr< IDatasmithKeyValueProperty > FDatasmithMaterialExpressionGenericImpl::GetPropertyByName( const TCHAR* InName )
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

	Store.RegisterParameter( BlendMode, "BlendMode" );
	Store.RegisterParameter( bTwoSided, "bTwoSided" );
	Store.RegisterParameter( bUseMaterialAttributes, "bUseMaterialAttributes" );
	Store.RegisterParameter( bMaterialFunctionOnly, "bMaterialFunctionOnly" );
	Store.RegisterParameter( OpacityMaskClipValue, "OpacityMaskClipValue" );

	Store.RegisterParameter( ParentLabel, "ParentLabel" );
	Store.RegisterParameter( ShadingModel, "ShadingModel" );
}

FMD5Hash FDatasmithUEPbrMaterialElementImpl::CalculateElementHash(bool bForce)
{
	if (ElementHash.IsValid() && !bForce)
	{
		return ElementHash;
	}

	FMD5 MD5;
	MD5.Update(reinterpret_cast<const uint8*>(&BlendMode), sizeof(BlendMode));
	MD5.Update(reinterpret_cast<const uint8*>(&bTwoSided), sizeof(bTwoSided));
	MD5.Update(reinterpret_cast<const uint8*>(&bUseMaterialAttributes), sizeof(bUseMaterialAttributes));
	MD5.Update(reinterpret_cast<const uint8*>(&bMaterialFunctionOnly), sizeof(bMaterialFunctionOnly));
	MD5.Update(reinterpret_cast<const uint8*>(&OpacityMaskClipValue), sizeof(OpacityMaskClipValue));
	MD5.Update(reinterpret_cast<const uint8*>(&ShadingModel), sizeof(ShadingModel));

	const FString& NativeParentLabel = ParentLabel.Get(Store);
	if (!NativeParentLabel.IsEmpty())
	{
		MD5.Update(reinterpret_cast<const uint8*>(*NativeParentLabel), NativeParentLabel.Len() * sizeof(TCHAR));
	}

	TFunction<void(IDatasmithExpressionInput&)> UpdateMD5 = [&](IDatasmithExpressionInput& Input) -> void
	{
		const FMD5Hash& InputHashValue = Input.CalculateElementHash(bForce);
		MD5.Update(InputHashValue.GetBytes(), InputHashValue.GetSize());
	};

	UpdateMD5(*BaseColor.Edit());
	UpdateMD5(*Metallic.Edit());
	UpdateMD5(*Specular.Edit());
	UpdateMD5(*Roughness.Edit());
	UpdateMD5(*EmissiveColor.Edit());
	UpdateMD5(*Opacity.Edit());
	UpdateMD5(*Normal.Edit());
	UpdateMD5(*WorldDisplacement.Edit());
	UpdateMD5(*Refraction.Edit());
	UpdateMD5(*AmbientOcclusion.Edit());
	UpdateMD5(*MaterialAttributes.Edit());

	ElementHash.Set(MD5);
	return ElementHash;
}

TSharedPtr<IDatasmithMaterialExpression> FDatasmithUEPbrMaterialElementImpl::GetExpression( int32 Index ) const
{
	return Expressions.IsValidIndex( Index ) ? Expressions[Index] : nullptr;
}

int32 FDatasmithUEPbrMaterialElementImpl::GetExpressionIndex( const TSharedPtr<const IDatasmithMaterialExpression>& Expression ) const
{
	int32 ExpressionIndex = INDEX_NONE;

	for ( int32 Index = 0; Index < Expressions.Num(); ++Index )
	{
		const TSharedPtr<IDatasmithMaterialExpression>& CurrentElement = Expressions[Index];
		if ( Expression == CurrentElement)
		{
			ExpressionIndex = Index;
			break;
		}
	}

	return ExpressionIndex;
}

TSharedPtr<IDatasmithMaterialExpression> FDatasmithUEPbrMaterialElementImpl::AddMaterialExpression( const EDatasmithMaterialExpressionType ExpressionType )
{
	TSharedPtr<IDatasmithMaterialExpression> Expression = FDatasmithSceneFactory::CreateMaterialExpression( ExpressionType );

	Expressions.Add( Expression );

	return Expression;
}

const TCHAR* FDatasmithUEPbrMaterialElementImpl::GetParentLabel() const
{
	if ( ParentLabel.Get( Store ).IsEmpty() )
	{
		return GetLabel();
	}
	else
	{
		return *ParentLabel.Get( Store );
	}
}


void FDatasmithUEPbrMaterialElementImpl::CustomSerialize(class DirectLink::FSnapshotProxy& Ar)
{
	// [4.26.1 .. 4.27.0[ compatibility
	if (Ar.IsSaving())
	{
		// In 4.26, an ExpressionTypes array was used alongside the expressions array.
		// namely: TReflected<TArray<EDatasmithMaterialExpressionType>, TArray<int32>> FDatasmithUEPbrMaterialElementImpl::ExpressionTypes;
		// This field was required. In order to be readable by 4.26, that array is recreated here.
		// Without it, a 4.26 DirectLink receiver could crash on 4.27 data usage.
		TArray<int32> ExpressionTypes;
		for (const TSharedPtr<IDatasmithMaterialExpression>& Expression : Expressions.View())
		{
			EDatasmithMaterialExpressionType ExpressionType = Expression.IsValid() ? Expression->GetExpressionType() : EDatasmithMaterialExpressionType::None;
			ExpressionTypes.Add(int32(ExpressionType));
		}
		Ar.TagSerialize("ExpressionTypes", ExpressionTypes);
	}
}


FDatasmithMaterialExpressionCustomImpl::FDatasmithMaterialExpressionCustomImpl() : FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionCustom >(EDatasmithMaterialExpressionType::Custom)
{
	RegisterReferenceProxy(Inputs, "Inputs");
	Store.RegisterParameter(Code, "Code");
	Store.RegisterParameter(Description, "Description");
	Store.RegisterParameter(OutputType, "OutputType");
	Store.RegisterParameter(IncludeFilePaths, "IncludeFilePaths");
	Store.RegisterParameter(Defines, "Defines");
	Store.RegisterParameter(ArgNames, "ArgNames");
}


TSharedPtr<IDatasmithExpressionInput> FDatasmithMaterialExpressionCustomImpl::GetInput(int32 Index)
{
	if (!ensure(Index >= 0))
	{
		return nullptr;
	}

	while (!Inputs.IsValidIndex(Index))
	{
		Inputs.Add(MakeShared< FDatasmithExpressionInputImpl >(*FString::FromInt(Inputs.Num())));
	}

	return Inputs[Index];
}


void FDatasmithMaterialExpressionCustomImpl::SetArgumentName(int32 ArgIndex, const TCHAR* ArgName)
{
	if (!ensure(ArgIndex >= 0))
	{
		return;
	}

	auto& Names = ArgNames.Edit(Store);
	while (!Names.IsValidIndex(ArgIndex))
	{
		int32 CurrentIndex = Names.Num();
		Names.Add(FString::Printf(TEXT("Arg%d"), CurrentIndex));
	}
	Names[ArgIndex] = ArgName;
}

