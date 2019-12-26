// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithMaterialElements.h"
#include "DatasmithSceneElementsImpl.h"

#include "Algo/Find.h"
#include "Containers/Array.h"

template< typename InterfaceType >
class FDatasmithExpressionInputImpl : public InterfaceType
{
public:
	inline explicit FDatasmithExpressionInputImpl( const TCHAR* InInputName );
	virtual ~FDatasmithExpressionInputImpl() = default;

	virtual const TCHAR* GetInputName() const override { return *InputName; }

	virtual IDatasmithMaterialExpression* GetExpression() override { return Expression; }
	virtual const IDatasmithMaterialExpression* GetExpression() const { return Expression; }
	virtual void SetExpression( IDatasmithMaterialExpression* InExpression ) override { Expression = InExpression; }

	virtual int32 GetOutputIndex() const override { return OutputIndex; }
	virtual void SetOutputIndex( int32 InOutputIndex ) override { OutputIndex = InOutputIndex; }

protected:
	FString InputName;
	IDatasmithMaterialExpression* Expression;
	int32 OutputIndex;
};

class FDatasmithExpressionOutputImpl : public IDatasmithExpressionOutput
{
public:
	explicit FDatasmithExpressionOutputImpl( const TCHAR* InOutputName )
		: OutputName( InOutputName )
	{
	}

	virtual const TCHAR* GetOutputName() const override { return *OutputName; }
	virtual void SetOutputName( const TCHAR* InOutputName ) override { OutputName = InOutputName; }

protected:
	FString OutputName;
};

template< typename InterfaceType >
class FDatasmithMaterialExpressionImpl : public InterfaceType
{
public:
	FDatasmithMaterialExpressionImpl()
		: DefaultOutputIndex( 0 )
	{
	}

	virtual ~FDatasmithMaterialExpressionImpl() = default;

	virtual const TCHAR* GetName() const override { return *Name; }
	virtual void SetName( const TCHAR* InName ) override { Name = InName; }

	virtual void ConnectExpression( IDatasmithExpressionInput& ExpressionInput ) override
	{
		ConnectExpression( ExpressionInput, GetDefaultOutputIndex() );
	}

	inline virtual void ConnectExpression( IDatasmithExpressionInput& ExpressionInput, int32 OutputIndex ) override;

	virtual int32 GetInputCount() const override { return 0; }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override { return nullptr; }
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return nullptr; }

	virtual int32 GetDefaultOutputIndex() const override { return DefaultOutputIndex; }
	virtual void SetDefaultOutputIndex( int32 InDefaultOutputIndex ) override { DefaultOutputIndex = InDefaultOutputIndex; }

protected:
	TArray< FDatasmithExpressionOutputImpl > Outputs;

	FString Name;
	int32 DefaultOutputIndex;
};

template< typename InterfaceType >
class FDatasmithExpressionParameterImpl : public FDatasmithMaterialExpressionImpl< InterfaceType >
{
public:
	virtual const TCHAR* GetGroupName() const override { return *GroupName; }
	virtual void SetGroupName( const TCHAR* InGroupName ) override { GroupName = InGroupName; }

protected:
	FString GroupName;
};

class FDatasmithMaterialExpressionBoolImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionBool >
{
public:
	FDatasmithMaterialExpressionBoolImpl();

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::ConstantBool; }

	virtual bool& GetBool() override { return bValue; }
	virtual const bool& GetBool() const override { return bValue; }

protected:
	bool bValue;
};

class FDatasmithMaterialExpressionColorImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionColor >
{
public:
	FDatasmithMaterialExpressionColorImpl();

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::ConstantColor; }

	virtual FLinearColor& GetColor() override { return LinearColor; }
	virtual const FLinearColor& GetColor() const override { return LinearColor; }

protected:
	FLinearColor LinearColor;
};

class FDatasmithMaterialExpressionScalarImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionScalar >
{
public:
	FDatasmithMaterialExpressionScalarImpl();

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::ConstantScalar; }

	virtual float& GetScalar() override { return Scalar; }
	virtual const float& GetScalar() const override { return Scalar; }

protected:
	float Scalar;
};

class FDatasmithMaterialExpressionTextureImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionTexture >
{
public:
	FDatasmithMaterialExpressionTextureImpl();

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::Texture; }

	virtual const TCHAR* GetTexturePathName() const override { return *TexturePathName; }
	virtual void SetTexturePathName( const TCHAR* InTexturePathName ) { TexturePathName = InTexturePathName; }

	/**
	 * Inputs
	 */
	virtual IDatasmithExpressionInput& GetInputCoordinate() override { return TextureCoordinate; }
	virtual const IDatasmithExpressionInput& GetInputCoordinate() const override { return TextureCoordinate; }

	virtual int32 GetInputCount() const override { return 1; }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override { return &TextureCoordinate; }
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return &TextureCoordinate; }

protected:
	FString TexturePathName;

	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > TextureCoordinate;

	/**
	 * Outputs:
	 * - RGB
	 * - R
	 * - G
	 * - B
	 * - A
	 */
};

class FDatasmithMaterialExpressionTextureCoordinateImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionTextureCoordinate >
{
public:
	FDatasmithMaterialExpressionTextureCoordinateImpl();

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::TextureCoordinate; }

	virtual int32 GetCoordinateIndex() const override { return CoordinateIndex; }
	virtual void SetCoordinateIndex( int32 InCoordinateIndex ) override { CoordinateIndex = InCoordinateIndex; }

	virtual float GetUTiling() const override { return UTiling; }
	virtual void SetUTiling( float InUTiling ) override { UTiling = InUTiling; }

	virtual float GetVTiling() const override { return VTiling;}
	virtual void SetVTiling( float InVTiling ) override { VTiling = InVTiling; }

protected:
	int32 CoordinateIndex;
	float UTiling;
	float VTiling;
};

class FDatasmithMaterialExpressionFlattenNormalImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionFlattenNormal >
{
public:
	FDatasmithMaterialExpressionFlattenNormalImpl();

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::FlattenNormal; }

	virtual IDatasmithExpressionInput& GetNormal() override { return Normal; }
	virtual const IDatasmithExpressionInput& GetNormal() const override { return Normal; }

	virtual IDatasmithExpressionInput& GetFlatness() override { return Normal; }
	virtual const IDatasmithExpressionInput& GetFlatness() const override { return Normal; }

	virtual int32 GetInputCount() const override { return 2; }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override { return Index == 0 ? &Normal : &Flatness; }
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Index == 0 ? &Normal : &Flatness; }

protected:
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Normal;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Flatness;
};

class FDatasmithMaterialExpressionGenericImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionGeneric >
{
public:
	static TSharedPtr< IDatasmithKeyValueProperty > NullPropertyPtr;

	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::Generic; }

	virtual void SetExpressionName( const TCHAR* InExpressionName ) override { ExpressionName = InExpressionName; }
	virtual const TCHAR* GetExpressionName() const override { return *ExpressionName; }

	int32 GetPropertiesCount() const override { return Properties.Num(); }

	const TSharedPtr< IDatasmithKeyValueProperty >& GetProperty( int32 InIndex ) const override;
	TSharedPtr< IDatasmithKeyValueProperty >& GetProperty( int32 InIndex ) override;

	const TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName( const TCHAR* InName ) const override;
	TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName( const TCHAR* InName ) override;

	void AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& InProperty ) override;

	virtual int32 GetInputCount() const override { return Inputs.Num(); }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override
	{
		while ( !Inputs.IsValidIndex( Index ) )
		{
			Inputs.Emplace( *FString::FromInt( Inputs.Num() ) );
		}

		return &Inputs[ Index ];
	}

	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Inputs.IsValidIndex( Index ) ? &Inputs[ Index ] : nullptr; }

protected:
	TArray< FDatasmithExpressionInputImpl< IDatasmithExpressionInput > > Inputs;
	FString ExpressionName;

	TArray< TSharedPtr< IDatasmithKeyValueProperty > > Properties;
	TMap< FString, int > PropertyIndexMap;
};

class FDatasmithMaterialExpressionFunctionCallImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionFunctionCall >
{
public:
	virtual EDatasmithMaterialExpressionType GetType() const override { return EDatasmithMaterialExpressionType::FunctionCall; }

	virtual void SetFunctionPathName( const TCHAR* InFunctionPathName ) override { FunctionPathName = InFunctionPathName; }
	virtual const TCHAR* GetFunctionPathName() const override { return *FunctionPathName; }

	virtual int32 GetInputCount() const override { return Inputs.Num(); }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override
	{
		while ( !Inputs.IsValidIndex( Index ) )
		{
			Inputs.Emplace( *FString::FromInt( Inputs.Num() ) );
		}

		return &Inputs[ Index ];
	}

	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Inputs.IsValidIndex( Index ) ? &Inputs[ Index ] : nullptr; }

protected:
	TArray< FDatasmithExpressionInputImpl< IDatasmithExpressionInput > > Inputs;
	FString FunctionPathName;

};

class DATASMITHCORE_API FDatasmithUEPbrMaterialElementImpl : public FDatasmithBaseMaterialElementImpl< IDatasmithUEPbrMaterialElement >
{
public:
	explicit FDatasmithUEPbrMaterialElementImpl( const TCHAR* InName );
	virtual ~FDatasmithUEPbrMaterialElementImpl();

	virtual IDatasmithExpressionInput& GetBaseColor() override { return BaseColor; }
	virtual IDatasmithExpressionInput& GetMetallic() override { return Metallic; }
	virtual IDatasmithExpressionInput& GetSpecular() override { return Specular; }
	virtual IDatasmithExpressionInput& GetRoughness() override { return Roughness; }
	virtual IDatasmithExpressionInput& GetEmissiveColor() override { return EmissiveColor; }
	virtual IDatasmithExpressionInput& GetOpacity() override { return Opacity; }
	virtual IDatasmithExpressionInput& GetNormal() override { return Normal; }
	virtual IDatasmithExpressionInput& GetWorldDisplacement() override { return WorldDisplacement; }
	virtual IDatasmithExpressionInput& GetRefraction() override { return Refraction; }
	virtual IDatasmithExpressionInput& GetAmbientOcclusion() override { return AmbientOcclusion; }
	virtual IDatasmithExpressionInput& GetMaterialAttributes() override { return MaterialAttributes; }

	virtual int GetBlendMode() const override {return BlendMode; }
	virtual void SetBlendMode( int InBlendMode ) override { BlendMode = InBlendMode; }

	virtual bool GetTwoSided() const override {return bTwoSided; }
	virtual void SetTwoSided( bool bInTwoSided ) override { bTwoSided = bInTwoSided; }

	virtual bool GetUseMaterialAttributes() const override{ return bUseMaterialAttributes; }
	virtual void SetUseMaterialAttributes( bool bInUseMaterialAttributes ) override { bUseMaterialAttributes = bInUseMaterialAttributes; }

	virtual bool GetMaterialFunctionOnly() const override { return bMaterialFunctionOnly; };
	virtual void SetMaterialFunctionOnly(bool bInMaterialFunctionOnly) override { bMaterialFunctionOnly = bInMaterialFunctionOnly; };

	virtual int32 GetExpressionsCount() const override { return Expressions.Num(); }
	virtual IDatasmithMaterialExpression* GetExpression( int32 Index ) override { IDatasmithMaterialExpression* Expression = Expressions.IsValidIndex( Index ) ? Expressions[ Index ] : nullptr; return Expression; }
	virtual int32 GetExpressionIndex( const IDatasmithMaterialExpression* Expression ) const override;

	virtual IDatasmithMaterialExpression* AddMaterialExpression( const EDatasmithMaterialExpressionType ExpressionType ) override;

	virtual void SetParentLabel( const TCHAR* InParentLabel ) override { ParentLabel = InParentLabel; }
	virtual const TCHAR* GetParentLabel() const override;

protected:
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > BaseColor;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Metallic;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Specular;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Roughness;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > EmissiveColor;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Opacity;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Normal;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > WorldDisplacement;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > Refraction;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > AmbientOcclusion;
	FDatasmithExpressionInputImpl< IDatasmithExpressionInput > MaterialAttributes;

	TArray< IDatasmithMaterialExpression* > Expressions;

	int BlendMode;
	bool bTwoSided;
	bool bUseMaterialAttributes;
	bool bMaterialFunctionOnly;

	FString ParentLabel;
};

template< typename InterfaceType >
FDatasmithExpressionInputImpl< InterfaceType >::FDatasmithExpressionInputImpl( const TCHAR* InInputName )
	: InputName( InInputName )
	, Expression( nullptr )
	, OutputIndex( 0 )
{
}

template< typename InterfaceType >
void FDatasmithMaterialExpressionImpl< InterfaceType >::ConnectExpression( IDatasmithExpressionInput& ExpressionInput, int32 InOutputIndex )
{
	while ( !Outputs.IsValidIndex( InOutputIndex ) )
	{
		Outputs.Emplace( TEXT("Ouput") );
	}

	int32 OutputIndex = Outputs.IsValidIndex( InOutputIndex ) ? InOutputIndex : INDEX_NONE;

	if ( OutputIndex != INDEX_NONE )
	{
		ExpressionInput.SetExpression( this );
		ExpressionInput.SetOutputIndex( OutputIndex );
	}
}
