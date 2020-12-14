// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DatasmithMaterialElements.h"
#include "DatasmithSceneElementsImpl.h"

#include "Algo/Find.h"
#include "Containers/Array.h"

template<typename>
class FDatasmithMaterialExpressionImpl;

/**
 * Class created for direct link serialization of PbrMaterial in 4.26 post-feature-freeze.
 * It's a temporary class to avoid changing the IDatasmithExpressionInput and it should be removed at a later date before 4.27 is released.
 */
class FDatasmithExpressionInputElement : public IDatasmithExpressionInput, public IDatasmithElement {};

/**
 * Class created for direct link serialization of PbrMaterial in 4.26 post-feature-freeze.
 * It's a temporary class to avoid changing the IDatasmithExpressionOutput and it should be removed at a later date before 4.27 is released.
 */
class FDatasmithExpressionOutputElement : public IDatasmithExpressionOutput, public IDatasmithElement {};

/**
 * Class created for direct link serialization of PbrMaterial in 4.26 post-feature-freeze.
 * It's a temporary class to avoid changing the IDatasmithMaterialExpression and it should be removed at a later date before 4.27 is released.
 */
template< typename InterfaceType >
class FDatasmithMaterialExpressionElement : public InterfaceType, public IDatasmithElement {};


class FDatasmithExpressionInputImpl : public FDatasmithElementImpl < FDatasmithExpressionInputElement >
{
public:
	explicit FDatasmithExpressionInputImpl( const TCHAR* InInputName );
	virtual ~FDatasmithExpressionInputImpl() = default;

	virtual const TCHAR* GetInputName() const override { return GetName(); }

	virtual IDatasmithMaterialExpression* GetExpression() override;
	virtual const IDatasmithMaterialExpression* GetExpression() const override;
	virtual void SetExpression( IDatasmithMaterialExpression* InExpression ) override;

	virtual int32 GetOutputIndex() const override { return OutputIndex; }
	virtual void SetOutputIndex( int32 InOutputIndex ) override { OutputIndex = InOutputIndex; }

protected:
	TDatasmithReferenceProxy<IDatasmithElement> Expression;
	TReflected<EDatasmithMaterialExpressionType, int32> ExpressionType;
	TReflected<int32> OutputIndex;
};

/**
 * Helper class created for direct link serialization of PbrMaterial in 4.26 post-feature-freeze.
 * This is a temporary class that should be removed at a later date before 4.27 is released.
 */
class FDatasmithUEPbrInternalHelper
{
public:
	/**
	 * Those values are cast as EDatasmithElementType and are stored as the Element type.
	 * Even if they are outside the declared enum values they are still inside the underlying type range, as so the C++ standard tells us the value won't be changed.
	 */
	constexpr static uint64 MaterialExpressionType = 1ull << 31;
	constexpr static uint64 MaterialExpressionInputType = 1ull << 32;
	constexpr static uint64 MaterialExpressionOutputType = 1ull << 33;

	static TSharedPtr<IDatasmithElement> ConvertMaterialExpressionToElementSharedPtr( IDatasmithMaterialExpression* InExpression );
	static IDatasmithMaterialExpression* ConvertElementToMaterialExpression( IDatasmithElement* InElement, EDatasmithMaterialExpressionType ExpressionType );
	static TSharedPtr< IDatasmithMaterialExpression > CreateMaterialExpression( EDatasmithMaterialExpressionType MaterialExpression );
};

class FDatasmithExpressionOutputImpl : public FDatasmithElementImpl < FDatasmithExpressionOutputElement >
{
public:
	explicit FDatasmithExpressionOutputImpl( const TCHAR* InOutputName )
		: FDatasmithElementImpl< FDatasmithExpressionOutputElement >( InOutputName, static_cast< const EDatasmithElementType >(FDatasmithUEPbrInternalHelper::MaterialExpressionOutputType ) )
	{}

	virtual const TCHAR* GetOutputName() const override { return GetName(); }
	virtual void SetOutputName( const TCHAR* InOutputName ) override { SetName( InOutputName ); }
};

template< typename InterfaceType >
class FDatasmithMaterialExpressionImpl : public FDatasmithElementImpl < FDatasmithMaterialExpressionElement<InterfaceType> >, public TSharedFromThis< FDatasmithMaterialExpressionImpl< InterfaceType > >
{
public:
	FDatasmithMaterialExpressionImpl( EDatasmithMaterialExpressionType InSubType );

	virtual ~FDatasmithMaterialExpressionImpl() = default;

	virtual EDatasmithMaterialExpressionType GetType() const override { return static_cast<EDatasmithMaterialExpressionType>( this->Subtype.Get( this->Store ) ); }

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

	virtual IDatasmithExpressionInput& GetNormal() override { return *Normal.Inner.Get(); }
	virtual const IDatasmithExpressionInput& GetNormal() const override { return *Normal.Inner.Get(); }

	virtual IDatasmithExpressionInput& GetFlatness() override { return *Flatness.Inner.Get(); }
	virtual const IDatasmithExpressionInput& GetFlatness() const override { return *Flatness.Inner.Get(); }

	virtual int32 GetInputCount() const override { return 2; }
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) override { return Index == 0 ? Normal.Inner.Get() : Flatness.Inner.Get(); }
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const override { return Index == 0 ? Normal.Inner.Get() : Flatness.Inner.Get(); }

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

class DATASMITHCORE_API FDatasmithUEPbrMaterialElementImpl : public FDatasmithBaseMaterialElementImpl< IDatasmithUEPbrMaterialElement >
{
public:
	explicit FDatasmithUEPbrMaterialElementImpl( const TCHAR* InName );
	virtual ~FDatasmithUEPbrMaterialElementImpl() = default;

	virtual IDatasmithExpressionInput& GetBaseColor() override { return *BaseColor.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetMetallic() override { return *Metallic.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetSpecular() override { return *Specular.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetRoughness() override { return *Roughness.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetEmissiveColor() override { return *EmissiveColor.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetOpacity() override { return *Opacity.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetNormal() override { return *Normal.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetWorldDisplacement() override { return *WorldDisplacement.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetRefraction() override { return *Refraction.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetAmbientOcclusion() override { return *AmbientOcclusion.Inner.Get(); }
	virtual IDatasmithExpressionInput& GetMaterialAttributes() override { return *MaterialAttributes.Inner.Get(); }

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

	virtual int32 GetExpressionsCount() const override { return Expressions.Inner.Num(); }
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

	TDatasmithReferenceArrayProxy< IDatasmithElement > Expressions;
	TReflected<TArray<EDatasmithMaterialExpressionType>, TArray<int32>> ExpressionTypes;

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
	: FDatasmithElementImpl< FDatasmithMaterialExpressionElement< InterfaceType > >( nullptr, static_cast< const EDatasmithElementType >(FDatasmithUEPbrInternalHelper::MaterialExpressionType ), (uint64)InSubType )
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