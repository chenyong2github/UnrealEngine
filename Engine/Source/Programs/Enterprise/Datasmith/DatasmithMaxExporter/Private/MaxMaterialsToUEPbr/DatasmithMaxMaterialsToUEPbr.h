// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DatasmithDefinitions.h"
#include "Containers/IndirectArray.h"
#include "Templates/SharedPointer.h"

class Class_ID;
class FDatasmithMaxMaterialsToUEPbr;
class IDatasmithBaseMaterialElement;
class IDatasmithMaterialExpression;
class IDatasmithMaterialExpressionTexture;
class IDatasmithMaxTexmapToUEPbr;
class IDatasmithScene;
class IDatasmithUEPbrMaterialElement;
class Mtl;
class Texmap;

namespace DatasmithMaxTexmapParser
{
	struct FMapParameter;
}

class FDatasmithMaxMaterialsToUEPbrManager
{
public:
	static FDatasmithMaxMaterialsToUEPbr* GetMaterialConverter( Mtl* Material );
};

class FDatasmithMaxMaterialsToUEPbr
{
public:
	FDatasmithMaxMaterialsToUEPbr();
	virtual ~FDatasmithMaxMaterialsToUEPbr() = default;

	virtual bool IsSupported( Mtl* Material ) = 0;
	virtual void Convert( TSharedRef< IDatasmithScene > DatasmithScene, TSharedPtr< IDatasmithBaseMaterialElement >& MaterialElement, Mtl* Material, const TCHAR* AssetsPath ) = 0;

	virtual bool IsTexmapSupported( Texmap* InTexmap ) const;

	/**
	 * Returns a LinearInterpolate expression if the map weight is smaller than 1.f, input 0 is not connected in that case
	 */
	IDatasmithMaterialExpression* ConvertTexmap( const DatasmithMaxTexmapParser::FMapParameter& MapParameter );

	struct FConvertState
	{
		TSharedPtr< IDatasmithScene > DatasmithScene;
		TSharedPtr< IDatasmithUEPbrMaterialElement > MaterialElement;
		FString AssetsPath;

		bool bCanBake = true;
		bool bIsMonoChannel = false; // true if we are parsing a mono channel (ie: opacity)
		bool bTreatNormalMapsAsLinear = false; // Corona has an option that treats all normal map inputs as linear

		EDatasmithTextureMode DefaultTextureMode = EDatasmithTextureMode::Diffuse;

	} ConvertState;

protected:
	TIndirectArray< IDatasmithMaxTexmapToUEPbr > TexmapConverters;

	//Structure used to have recursive import state.
	struct FScopedConvertState
	{
		FScopedConvertState(FConvertState& InCurrentConvertState) : CurrentConvertStateRef(InCurrentConvertState), PreviousConvertState(InCurrentConvertState)
		{
			CurrentConvertStateRef = FConvertState();
		}

		~FScopedConvertState()
		{
			CurrentConvertStateRef = PreviousConvertState;
		}
	private:
		FConvertState& CurrentConvertStateRef;
		const FConvertState PreviousConvertState;
	};
};

class IDatasmithMaxTexmapToUEPbr
{
public:
	virtual ~IDatasmithMaxTexmapToUEPbr() = default;

	virtual bool IsSupported( const FDatasmithMaxMaterialsToUEPbr* MaxMaterialToUEPbr, Texmap* InTexmap ) const = 0;
	virtual IDatasmithMaterialExpression* Convert( FDatasmithMaxMaterialsToUEPbr* MaxMaterialToUEPbr, Texmap* InTexmap ) = 0;
};


class IDatasmithMaterialExpression;
class IDatasmithMaterialExpressionScalar;
class IDatasmithMaterialExpressionColor;
class IDatasmithMaterialExpressionGeneric;
class IDatasmithExpressionInput;

class FDatasmithMaxMaterialsToUEPbrExpressions: public FDatasmithMaxMaterialsToUEPbr
{
public:
	TSharedPtr<IDatasmithUEPbrMaterialElement> GetMaterialElement();

	IDatasmithMaterialExpressionScalar& Scalar(float Value);

	IDatasmithMaterialExpressionColor& Color(const FLinearColor& Value);

	IDatasmithMaterialExpression* WeightTextureOrScalar(const DatasmithMaxTexmapParser::FMapParameter& TextureWeight, float Weight);

	IDatasmithMaterialExpressionGeneric& Add(IDatasmithMaterialExpression& A, IDatasmithMaterialExpression& B);

	IDatasmithMaterialExpressionGeneric& Subtract(IDatasmithMaterialExpression& A, IDatasmithMaterialExpression& B);

	IDatasmithMaterialExpressionGeneric& Multiply(IDatasmithMaterialExpression& A, IDatasmithMaterialExpression& B);

	IDatasmithMaterialExpressionGeneric& Divide(IDatasmithMaterialExpression& A, IDatasmithMaterialExpression& B);

	IDatasmithMaterialExpressionGeneric& Desaturate(IDatasmithMaterialExpression& A);

	IDatasmithMaterialExpressionGeneric& Power(IDatasmithMaterialExpression& A, IDatasmithMaterialExpression& B);

	IDatasmithMaterialExpressionGeneric& Lerp(IDatasmithMaterialExpression& A, IDatasmithMaterialExpression& B, IDatasmithMaterialExpression& Alpha);

	IDatasmithMaterialExpressionGeneric& Fresnel(IDatasmithMaterialExpression* Exponent=nullptr, IDatasmithMaterialExpression* BaseReflectFraction=nullptr); // Any input can be null


	IDatasmithMaterialExpression* ApplyWeightExpression(IDatasmithMaterialExpression* ValueExpression, IDatasmithMaterialExpression* WeightExpression);

	IDatasmithMaterialExpression& CalcIORComplex(double IORn, double IORk, IDatasmithMaterialExpression& ToBeConnected90, IDatasmithMaterialExpression& ToBeConnected0);


	void Connect(IDatasmithExpressionInput& Input, IDatasmithMaterialExpression& ValueExpression);
	bool Connect(IDatasmithExpressionInput& Input, IDatasmithMaterialExpression* ValueExpression); // Connect if not null

	IDatasmithMaterialExpression* TextureOrColor(const TCHAR* Name,
	                                             const DatasmithMaxTexmapParser::FMapParameter& Map, FLinearColor Color);

	IDatasmithMaterialExpression* TextureOrScalar(const TCHAR* Name,
	                                              const DatasmithMaxTexmapParser::FMapParameter& Map, float Value);

	IDatasmithMaterialExpressionGeneric& OneMinus(IDatasmithMaterialExpression& Expression);
};

// Create expression from expression parameters when they are all non-null or return default value
// Pass params by value in order to prevent statement creating expression to be evaluated twice
// e.g. COMPOSE_OR_DEFAULT1(nullptr, Multiply, &Scalar(1.0), SomeExpression) won't create two expressions of each Scalar
// Func should be a method of this of free function - so it can be just substituted 
#define COMPOSE_OR_DEFAULT1(Default, Func, Param0) ([this](IDatasmithMaterialExpression* P0, IDatasmithMaterialExpression* D) {return P0 ? &Func(*P0) : D; } (Param0, Default))
#define COMPOSE_OR_DEFAULT2(Default, Func, Param0, Param1) ([this](IDatasmithMaterialExpression* P0, IDatasmithMaterialExpression* P1, IDatasmithMaterialExpression* D) { return (P0 && P1) ? &Func(*P0, *P1) : D;} (Param0, Param1, Default))

