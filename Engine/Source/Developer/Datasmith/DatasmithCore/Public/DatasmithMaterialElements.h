// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IDatasmithSceneElements.h"

class IDatasmithMaterialExpression;

class IDatasmithExpressionInput
{
public:
	virtual const TCHAR* GetInputName() const = 0;

	virtual IDatasmithMaterialExpression* GetExpression() = 0;
	virtual const IDatasmithMaterialExpression* GetExpression() const = 0;
	virtual void SetExpression( IDatasmithMaterialExpression* InExpression ) = 0;

	virtual int32 GetOutputIndex() const = 0;
	virtual void SetOutputIndex( int32 InOutputIndex ) = 0;
};

class IDatasmithExpressionOutput
{
public:
	virtual ~IDatasmithExpressionOutput() = default;

	virtual const TCHAR* GetOutputName() const = 0;
	virtual void SetOutputName( const TCHAR* InOutputName ) = 0;
};

class IDatasmithExpressionParameter
{
public:
	virtual ~IDatasmithExpressionParameter() = default;

	virtual const TCHAR* GetGroupName() const = 0;
	virtual void SetGroupName( const TCHAR* InGroupName ) = 0;
};

enum class EDatasmithMaterialExpressionType
{
	ConstantBool,
	ConstantColor,
	ConstantScalar,
	FlattenNormal,
	FunctionCall,
	Generic,
	Texture,
	TextureCoordinate
};

class IDatasmithMaterialExpression
{
public:
	virtual ~IDatasmithMaterialExpression() = default;

	/** The name of the expression. Used as parameter name for material instances. */
	virtual const TCHAR* GetName() const = 0;
	virtual void SetName( const TCHAR* InName ) = 0;

	virtual EDatasmithMaterialExpressionType GetType() const = 0;
	bool IsA( const EDatasmithMaterialExpressionType ExpressionType ) const { return ExpressionType == GetType(); }

	/** Connects the default output to an expression input */
	virtual void ConnectExpression( IDatasmithExpressionInput& ExpressionInput ) = 0;

	/** Connects a specific output to an expression input */
	virtual void ConnectExpression( IDatasmithExpressionInput& ExpressionInput, int32 OutputIndex ) = 0;

	virtual int32 GetInputCount() const = 0;
	virtual IDatasmithExpressionInput* GetInput( int32 Index ) = 0;
	virtual const IDatasmithExpressionInput* GetInput( int32 Index ) const = 0;

	/** The output index to use by default for this expression when connecting it to other inputs. */
	virtual int32 GetDefaultOutputIndex() const = 0;
	virtual void SetDefaultOutputIndex( int32 OutputIndex ) = 0;
};

/**
 * Represents a UMaterialExpressionStaticBoolParameter
 */
class IDatasmithMaterialExpressionBool : public IDatasmithMaterialExpression, public IDatasmithExpressionParameter
{
public:
	virtual bool& GetBool() = 0;
	virtual const bool& GetBool() const = 0;
};

class IDatasmithMaterialExpressionColor : public IDatasmithMaterialExpression, public IDatasmithExpressionParameter
{
public:
	virtual FLinearColor& GetColor() = 0;
	virtual const FLinearColor& GetColor() const = 0;
};

class IDatasmithMaterialExpressionScalar : public IDatasmithMaterialExpression, public IDatasmithExpressionParameter
{
public:
	virtual float& GetScalar() = 0;
	virtual const float& GetScalar() const = 0;
};

class IDatasmithMaterialExpressionTexture : public IDatasmithMaterialExpression, public IDatasmithExpressionParameter
{
public:
	virtual const TCHAR* GetTexturePathName() const = 0;
	virtual void SetTexturePathName( const TCHAR* InTexturePathName ) = 0;

	/**
	 * Inputs
	 */
	virtual IDatasmithExpressionInput& GetInputCoordinate() = 0;
	virtual const IDatasmithExpressionInput& GetInputCoordinate() const = 0;

	/**
	 * Outputs:
	 * - RGB
	 * - R
	 * - G
	 * - B
	 * - A
	 */
};

class IDatasmithMaterialExpressionTextureCoordinate : public IDatasmithMaterialExpression
{
public:
	virtual int32 GetCoordinateIndex() const = 0;
	virtual void SetCoordinateIndex( int32 InCoordinateIndex ) = 0;

	virtual float GetUTiling() const = 0;
	virtual void SetUTiling( float InUTiling ) = 0;

	virtual float GetVTiling() const = 0;
	virtual void SetVTiling( float InCoordinateIndex ) = 0;
};

class IDatasmithMaterialExpressionFlattenNormal : public IDatasmithMaterialExpression
{
public:
	/** 
	 * Inputs
	 */
	virtual IDatasmithExpressionInput& GetNormal() = 0;
	virtual const IDatasmithExpressionInput& GetNormal() const = 0;

	virtual IDatasmithExpressionInput& GetFlatness() = 0;
	virtual const IDatasmithExpressionInput& GetFlatness() const = 0;

	/**
	 * Outputs:
	 * - RGB
	 */
};

class IDatasmithMaterialExpressionGeneric : public IDatasmithMaterialExpression
{
public:
	virtual void SetExpressionName( const TCHAR* InExpressionName ) = 0;
	virtual const TCHAR* GetExpressionName() const = 0;

	/** Get the total amount of properties in this expression */
	virtual int32 GetPropertiesCount() const = 0;

	/** Get the property i-th of this expression */
	virtual const TSharedPtr< IDatasmithKeyValueProperty >& GetProperty(int32 i) const = 0;
	virtual TSharedPtr< IDatasmithKeyValueProperty >& GetProperty(int32 i) = 0;

	/** Get a property by its name if it exists */
	virtual const TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName(const TCHAR* Name) const = 0;
	virtual TSharedPtr< IDatasmithKeyValueProperty >& GetPropertyByName(const TCHAR* Name) = 0;

	/** Add a property to this expression*/
	virtual void AddProperty( const TSharedPtr< IDatasmithKeyValueProperty >& Property ) = 0;
};

class IDatasmithMaterialExpressionFunctionCall : public IDatasmithMaterialExpression
{
public:
	virtual void SetFunctionPathName( const TCHAR* InFunctionPathName ) = 0;
	virtual const TCHAR* GetFunctionPathName() const = 0;

};

class DATASMITHCORE_API IDatasmithUEPbrMaterialElement : public IDatasmithBaseMaterialElement
{
public:
	virtual IDatasmithExpressionInput& GetBaseColor() = 0;
	virtual IDatasmithExpressionInput& GetMetallic() = 0;
	virtual IDatasmithExpressionInput& GetSpecular() = 0;
	virtual IDatasmithExpressionInput& GetRoughness() = 0;
	virtual IDatasmithExpressionInput& GetEmissiveColor() = 0;
	virtual IDatasmithExpressionInput& GetOpacity() = 0;
	virtual IDatasmithExpressionInput& GetNormal() = 0;
	virtual IDatasmithExpressionInput& GetWorldDisplacement() = 0;
	virtual IDatasmithExpressionInput& GetRefraction() = 0;
	virtual IDatasmithExpressionInput& GetAmbientOcclusion() = 0;
	virtual IDatasmithExpressionInput& GetMaterialAttributes() = 0;

	virtual int GetBlendMode() const = 0;
	virtual void SetBlendMode( int bInBlendMode ) = 0;

	virtual bool GetTwoSided() const = 0;
	virtual void SetTwoSided( bool bTwoSided ) = 0;

	virtual bool GetUseMaterialAttributes() const = 0;
	virtual void SetUseMaterialAttributes( bool bInUseMaterialAttributes ) = 0;

	/** If a material is only referenced by other materials then it is only used as a material function and there is no need to instantiate it. */
	virtual bool GetMaterialFunctionOnly() const = 0;
	virtual void SetMaterialFunctionOnly(bool bInMaterialFunctionOnly) = 0;

	virtual int32 GetExpressionsCount() const = 0;
	virtual IDatasmithMaterialExpression* GetExpression( int32 Index ) = 0;
	virtual int32 GetExpressionIndex( const IDatasmithMaterialExpression* Expression ) const = 0;

	virtual IDatasmithMaterialExpression* AddMaterialExpression( const EDatasmithMaterialExpressionType ExpressionType ) = 0;

	template< typename T >
	T* AddMaterialExpression()
	{
	}

	/** If a parent material is generated from this material, this will be its label. If none, the instance and the parent will have the same label. */
	virtual void SetParentLabel( const TCHAR* InParentLabel ) = 0;
	virtual const TCHAR* GetParentLabel() const = 0;
};

template<>
inline IDatasmithMaterialExpressionBool* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionBool >()
{
	return static_cast< IDatasmithMaterialExpressionBool* >( AddMaterialExpression( EDatasmithMaterialExpressionType::ConstantBool ) );
}

template<>
inline IDatasmithMaterialExpressionColor* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionColor >()
{
	return static_cast< IDatasmithMaterialExpressionColor* >( AddMaterialExpression( EDatasmithMaterialExpressionType::ConstantColor ) );
}

template<>
inline IDatasmithMaterialExpressionFlattenNormal* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionFlattenNormal >()
{
	return static_cast< IDatasmithMaterialExpressionFlattenNormal* >( AddMaterialExpression( EDatasmithMaterialExpressionType::FlattenNormal ) );
}

template<>
inline IDatasmithMaterialExpressionFunctionCall* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionFunctionCall >()
{
	return static_cast< IDatasmithMaterialExpressionFunctionCall* >( AddMaterialExpression( EDatasmithMaterialExpressionType::FunctionCall ) );
}

template<>
inline IDatasmithMaterialExpressionGeneric* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionGeneric >()
{
	return static_cast< IDatasmithMaterialExpressionGeneric* >( AddMaterialExpression( EDatasmithMaterialExpressionType::Generic ) );
}

template<>
inline IDatasmithMaterialExpressionScalar* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionScalar >()
{
	return static_cast< IDatasmithMaterialExpressionScalar* >( AddMaterialExpression( EDatasmithMaterialExpressionType::ConstantScalar ) );
}

template<>
inline IDatasmithMaterialExpressionTexture* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionTexture >()
{
	return static_cast< IDatasmithMaterialExpressionTexture* >( AddMaterialExpression( EDatasmithMaterialExpressionType::Texture ) );
}

template<>
inline IDatasmithMaterialExpressionTextureCoordinate* IDatasmithUEPbrMaterialElement::AddMaterialExpression< IDatasmithMaterialExpressionTextureCoordinate >()
{
	return static_cast< IDatasmithMaterialExpressionTextureCoordinate* >( AddMaterialExpression( EDatasmithMaterialExpressionType::TextureCoordinate ) );
}
