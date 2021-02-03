// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithMaterialElements.h"
#include "DatasmithSceneElementsImpl.h"

#include "Algo/Find.h"
#include "Containers/Array.h"

template<typename>
class FDatasmithMaterialExpressionImpl;


class FDatasmithExpressionInputImpl : public FDatasmithElementImpl < IDatasmithExpressionInput >
{
public:
	explicit FDatasmithExpressionInputImpl( const TCHAR* InInputName );
	virtual ~FDatasmithExpressionInputImpl() = default;

	virtual IDatasmithMaterialExpression* GetExpression() override { return Expression.Edit().Get(); }
	virtual const IDatasmithMaterialExpression* GetExpression() const { return Expression.View().Get(); }
	virtual void SetExpression( IDatasmithMaterialExpression* InExpression ) override;

	virtual int32 GetOutputIndex() const override { return OutputIndex; }
	virtual void SetOutputIndex( int32 InOutputIndex ) override { OutputIndex = InOutputIndex; }

protected:
	TDatasmithReferenceProxy<IDatasmithMaterialExpression> Expression;
	TReflected<int32> OutputIndex;
};

class FDatasmithExpressionOutputImpl : public FDatasmithElementImpl < IDatasmithExpressionOutput >
{
public:
	explicit FDatasmithExpressionOutputImpl( const TCHAR* InOutputName )
		: FDatasmithElementImpl< IDatasmithExpressionOutput >( InOutputName, EDatasmithElementType::MaterialExpressionOutput )
	{}
};

template< typename InterfaceType >
class FDatasmithMaterialExpressionImpl : public FDatasmithElementImpl< InterfaceType >, public TSharedFromThis< FDatasmithMaterialExpressionImpl< InterfaceType > >
{
public:
	explicit FDatasmithMaterialExpressionImpl( EDatasmithMaterialExpressionType InSubType );

	virtual ~FDatasmithMaterialExpressionImpl() = default;

	virtual EDatasmithMaterialExpressionType GetExpressionType() const override { return static_cast<EDatasmithMaterialExpressionType>( this->Subtype.Get( this->Store ) ); }

	virtual bool IsSubType( const EDatasmithMaterialExpressionType ExpressionType ) const override { return FDatasmithElementImpl< InterfaceType >::IsSubTypeInternal( (uint64)ExpressionType ); }

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
	TDatasmithReferenceArrayProxy<FDatasmithExpressionOutputImpl> Outputs;

	TReflected<int32> DefaultOutputIndex;
};

template< typename InterfaceType >
class FDatasmithExpressionParameterImpl : public FDatasmithMaterialExpressionImpl< InterfaceType >
{
public:

	FDatasmithExpressionParameterImpl( EDatasmithMaterialExpressionType InSubType )
		: FDatasmithMaterialExpressionImpl< InterfaceType >( InSubType )
	{
		this->Store.RegisterParameter( GroupName, "GroupName" );
	}

	virtual const TCHAR* GetGroupName() const override { return *GroupName.Get( this->Store ); }
	virtual void SetGroupName( const TCHAR* InGroupName ) override { GroupName = InGroupName; }

protected:
	TReflected<FString> GroupName;
};

class FDatasmithMaterialExpressionBoolImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionBool >
{
public:
	FDatasmithMaterialExpressionBoolImpl();

	virtual bool& GetBool() override { return bValue.Edit( Store ); }
	virtual const bool& GetBool() const override { return bValue.Get( Store ); }

protected:
	TReflected<bool> bValue;
};

class FDatasmithMaterialExpressionColorImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionColor >
{
public:
	FDatasmithMaterialExpressionColorImpl();

	virtual FLinearColor& GetColor() override { return LinearColor.Edit( Store ); }
	virtual const FLinearColor& GetColor() const override { return LinearColor.Get( Store ); }

protected:
	TReflected<FLinearColor> LinearColor;
};

class FDatasmithMaterialExpressionScalarImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionScalar >
{
public:
	FDatasmithMaterialExpressionScalarImpl();

	virtual float& GetScalar() override { return Scalar.Edit( Store ); }
	virtual const float& GetScalar() const override { return Scalar.Get( Store ); }

protected:
	TReflected<float> Scalar;
};

class FDatasmithMaterialExpressionTextureImpl : public FDatasmithExpressionParameterImpl< IDatasmithMaterialExpressionTexture >
{
public:
	FDatasmithMaterialExpressionTextureImpl();

	virtual const TCHAR* GetTexturePathName() const override { return *TexturePathName.Get( Store ); }
	virtual void SetTexturePathName( const TCHAR* InTexturePathName ) { TexturePathName = InTexturePathName; }

	/**
	 * Inputs
	 */
	virtual IDatasmithExpressionInput& GetInputCoordinate() override { return *TextureCoordinate.Edit(); }
	virtual const IDatasmithExpressionInput& GetInputCoordinate() const override { return *TextureCoordinate.View(); }

	virtual int32 GetInputCount() const override { return 1; }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override { return TextureCoordinate.Edit().Get(); }
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return TextureCoordinate.View().Get(); }

protected:
	TReflected< FString > TexturePathName;

	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > TextureCoordinate;

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

	virtual int32 GetCoordinateIndex() const override { return CoordinateIndex; }
	virtual void SetCoordinateIndex( int32 InCoordinateIndex ) override { CoordinateIndex = InCoordinateIndex; }

	virtual float GetUTiling() const override { return UTiling; }
	virtual void SetUTiling( float InUTiling ) override { UTiling = InUTiling; }

	virtual float GetVTiling() const override { return VTiling;}
	virtual void SetVTiling( float InVTiling ) override { VTiling = InVTiling; }

protected:
	TReflected< int32 > CoordinateIndex;
	TReflected< float > UTiling;
	TReflected< float > VTiling;
};

class FDatasmithMaterialExpressionFlattenNormalImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionFlattenNormal >
{
public:
	FDatasmithMaterialExpressionFlattenNormalImpl();

	virtual IDatasmithExpressionInput& GetNormal() override { return *Normal.Edit(); }
	virtual const IDatasmithExpressionInput& GetNormal() const override { return *Normal.View(); }

	virtual IDatasmithExpressionInput& GetFlatness() override { return *Flatness.Edit(); }
	virtual const IDatasmithExpressionInput& GetFlatness() const override { return *Flatness.View(); }

	virtual int32 GetInputCount() const override { return 2; }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override { return Index == 0 ? Normal.Edit().Get() : Flatness.Edit().Get(); }
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Index == 0 ? Normal.View().Get() : Flatness.View().Get(); }

protected:
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Normal;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Flatness;
};

class FDatasmithMaterialExpressionGenericImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionGeneric >
{
public:
	static TSharedPtr< IDatasmithKeyValueProperty > NullPropertyPtr;

	FDatasmithMaterialExpressionGenericImpl()
		: FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionGeneric >( EDatasmithMaterialExpressionType::Generic )
	{
		RegisterReferenceProxy( Inputs, "Inputs" );
		RegisterReferenceProxy( Properties, "Properties" );
		Store.RegisterParameter( ExpressionName, "ExpressionName" );
	}


	virtual void SetExpressionName( const TCHAR* InExpressionName ) override { ExpressionName = InExpressionName; }
	virtual const TCHAR* GetExpressionName() const override { return *ExpressionName.Get( Store ); }

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
			Inputs.Add( MakeShared< FDatasmithExpressionInputImpl >( *FString::FromInt( Inputs.Num() ) ) );
		}

		return Inputs[Index].Get();
	}

	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Inputs.IsValidIndex( Index ) ? Inputs[Index].Get() : nullptr; }

protected:
	TDatasmithReferenceArrayProxy< FDatasmithExpressionInputImpl > Inputs;
	TReflected<FString> ExpressionName;

	TDatasmithReferenceArrayProxy< IDatasmithKeyValueProperty > Properties;
};

class FDatasmithMaterialExpressionFunctionCallImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionFunctionCall >
{
public:
	FDatasmithMaterialExpressionFunctionCallImpl()
		: FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionFunctionCall >( EDatasmithMaterialExpressionType::FunctionCall )
	{
		RegisterReferenceProxy( Inputs, "Inputs" );
		Store.RegisterParameter( FunctionPathName, "FunctionPathName" );
	}

	virtual void SetFunctionPathName( const TCHAR* InFunctionPathName ) override { FunctionPathName = InFunctionPathName; }
	virtual const TCHAR* GetFunctionPathName() const override { return *FunctionPathName.Get( Store ); }

	virtual int32 GetInputCount() const override { return Inputs.Num(); }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override
	{
		while (!Inputs.IsValidIndex( Index ))
		{
			Inputs.Add( MakeShared< FDatasmithExpressionInputImpl >( *FString::FromInt( Inputs.Num() ) ) );
		}

		return Inputs[Index].Get();
	}

	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Inputs.IsValidIndex( Index ) ? Inputs[Index].Get() : nullptr; }

protected:
	TDatasmithReferenceArrayProxy< FDatasmithExpressionInputImpl > Inputs;
	TReflected<FString> FunctionPathName;
};

class FDatasmithMaterialExpressionCustomImpl : public FDatasmithMaterialExpressionImpl< IDatasmithMaterialExpressionCustom >
{
public:
	FDatasmithMaterialExpressionCustomImpl();

	virtual int32 GetInputCount() const override { return Inputs.Num(); }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override;
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Inputs.IsValidIndex( Index ) ? Inputs[Index].Get() : nullptr; }

	virtual void SetCode(const TCHAR* InCode) override { Code = InCode; }
	virtual const TCHAR* GetCode() const override { return *Code.Get(this->Store); }

	virtual void SetDescription(const TCHAR* InDescription) override { Description = InDescription; }
	virtual const TCHAR* GetDescription() const override { return *Description.Get(this->Store); }

	virtual void SetOutputType(EDatasmithShaderDataType InOutputType) override { OutputType = InOutputType; }
	virtual EDatasmithShaderDataType GetOutputType() const override { return OutputType; }

	virtual int32 GetIncludeFilePathCount() const override { return IncludeFilePaths.Get(Store).Num(); }
	virtual void AddIncludeFilePath(const TCHAR* Path) override { IncludeFilePaths.Edit(Store).Add(Path); }
	virtual const TCHAR* GetIncludeFilePath(int32 Index) const override { return IncludeFilePaths.Get(Store).IsValidIndex(Index) ? *IncludeFilePaths.Get(Store)[Index] : TEXT(""); }

	virtual int32 GetAdditionalDefineCount() const override { return Defines.Get(Store).Num(); }
	virtual void AddAdditionalDefine(const TCHAR* Define) override { Defines.Edit(Store).Add(Define); }
	virtual const TCHAR* GetAdditionalDefine(int32 Index) const override { return Defines.Get(Store).IsValidIndex(Index) ? *Defines.Get(Store)[Index] : TEXT(""); }

	virtual int32 GetArgumentNameCount() const override { return ArgNames.Get(Store).Num(); }
	virtual void SetArgumentName(int32 ArgIndex, const TCHAR* ArgName) override;
	virtual const TCHAR* GetArgumentName(int32 Index) const override { return ArgNames.Get(Store).IsValidIndex(Index) ? *ArgNames.Get(Store)[Index] : TEXT("");}

protected:
	TReflected<FString> Code;
	TReflected<FString> Description;
	TReflected<EDatasmithShaderDataType, uint32> OutputType = EDatasmithShaderDataType::Float1;
	TReflected<TArray<FString>> IncludeFilePaths;
	TReflected<TArray<FString>> Defines;
	TReflected<TArray<FString>> ArgNames;
	TDatasmithReferenceArrayProxy< FDatasmithExpressionInputImpl > Inputs;
};

class DATASMITHCORE_API FDatasmithUEPbrMaterialElementImpl : public FDatasmithBaseMaterialElementImpl< IDatasmithUEPbrMaterialElement >
{
public:
	explicit FDatasmithUEPbrMaterialElementImpl( const TCHAR* InName );
	virtual ~FDatasmithUEPbrMaterialElementImpl() = default;

	virtual IDatasmithExpressionInput& GetBaseColor() override { return *BaseColor.Edit(); }
	virtual IDatasmithExpressionInput& GetMetallic() override { return *Metallic.Edit(); }
	virtual IDatasmithExpressionInput& GetSpecular() override { return *Specular.Edit(); }
	virtual IDatasmithExpressionInput& GetRoughness() override { return *Roughness.Edit(); }
	virtual IDatasmithExpressionInput& GetEmissiveColor() override { return *EmissiveColor.Edit(); }
	virtual IDatasmithExpressionInput& GetOpacity() override { return *Opacity.Edit(); }
	virtual IDatasmithExpressionInput& GetNormal() override { return *Normal.Edit(); }
	virtual IDatasmithExpressionInput& GetWorldDisplacement() override { return *WorldDisplacement.Edit(); }
	virtual IDatasmithExpressionInput& GetRefraction() override { return *Refraction.Edit(); }
	virtual IDatasmithExpressionInput& GetAmbientOcclusion() override { return *AmbientOcclusion.Edit(); }
	virtual IDatasmithExpressionInput& GetMaterialAttributes() override { return *MaterialAttributes.Edit(); }

	virtual int GetBlendMode() const override {return BlendMode; }
	virtual void SetBlendMode( int InBlendMode ) override { BlendMode = InBlendMode; }

	virtual bool GetTwoSided() const override {return bTwoSided; }
	virtual void SetTwoSided( bool bInTwoSided ) override { bTwoSided = bInTwoSided; }

	virtual bool GetUseMaterialAttributes() const override{ return bUseMaterialAttributes; }
	virtual void SetUseMaterialAttributes( bool bInUseMaterialAttributes ) override { bUseMaterialAttributes = bInUseMaterialAttributes; }

	virtual bool GetMaterialFunctionOnly() const override { return bMaterialFunctionOnly; };
	virtual void SetMaterialFunctionOnly(bool bInMaterialFunctionOnly) override { bMaterialFunctionOnly = bInMaterialFunctionOnly; };

	virtual float GetOpacityMaskClipValue() const override { return OpacityMaskClipValue; }
	virtual void SetOpacityMaskClipValue(float InClipValue) override { OpacityMaskClipValue = InClipValue; }

	virtual int32 GetExpressionsCount() const override { return Expressions.View().Num(); }
	virtual IDatasmithMaterialExpression* GetExpression( int32 Index ) override;
	virtual int32 GetExpressionIndex( const IDatasmithMaterialExpression* Expression ) const override;

	virtual IDatasmithMaterialExpression* AddMaterialExpression( const EDatasmithMaterialExpressionType ExpressionType ) override;

	virtual void SetParentLabel( const TCHAR* InParentLabel ) override { ParentLabel = InParentLabel; }
	virtual const TCHAR* GetParentLabel() const override;

	virtual void SetShadingModel( const EDatasmithShadingModel InShadingModel ) override { ShadingModel.Edit( Store ) = InShadingModel; }
	virtual EDatasmithShadingModel GetShadingModel() const override { return ShadingModel.Get( Store ); }

protected:
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > BaseColor;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Metallic;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Specular;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Roughness;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > EmissiveColor;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Opacity;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Normal;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > WorldDisplacement;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > Refraction;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > AmbientOcclusion;
	TDatasmithReferenceProxy< FDatasmithExpressionInputImpl > MaterialAttributes;

	TDatasmithReferenceArrayProxy< IDatasmithMaterialExpression > Expressions;

	TReflected<int32> BlendMode;
	TReflected<bool> bTwoSided;
	TReflected<bool> bUseMaterialAttributes;
	TReflected<bool> bMaterialFunctionOnly;
	TReflected<float> OpacityMaskClipValue;

	TReflected<FString> ParentLabel;
	TReflected<EDatasmithShadingModel, uint8> ShadingModel;
};

template< typename InterfaceType >
FDatasmithMaterialExpressionImpl< InterfaceType >::FDatasmithMaterialExpressionImpl( EDatasmithMaterialExpressionType InSubType )
	: FDatasmithElementImpl< InterfaceType >( nullptr, EDatasmithElementType::MaterialExpression, (uint64)InSubType )
	, DefaultOutputIndex( 0 )
{
	this->RegisterReferenceProxy( Outputs, "Outputs" );
	this->Store.RegisterParameter( DefaultOutputIndex, "DefaultOutputIndex" );
}

template< typename InterfaceType >
void FDatasmithMaterialExpressionImpl< InterfaceType >::ConnectExpression( IDatasmithExpressionInput& ExpressionInput, int32 InOutputIndex )
{
	while ( !Outputs.IsValidIndex( InOutputIndex ) && InOutputIndex >= 0 )
	{
		Outputs.Add( MakeShared<FDatasmithExpressionOutputImpl>( TEXT( "Ouput" ) ) );
	}

	int32 OutputIndex = Outputs.IsValidIndex( InOutputIndex ) ? InOutputIndex : INDEX_NONE;

	if ( OutputIndex != INDEX_NONE )
	{
		ExpressionInput.SetExpression( this );
		ExpressionInput.SetOutputIndex( OutputIndex );
	}
}