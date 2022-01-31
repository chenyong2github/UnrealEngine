// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithMaxMaterialsToUEPbr.h"

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

class FDatasmithMaxPhysicalMaterialToUEPbr : public FDatasmithMaxMaterialsToUEPbrExpressions
{
public:
	virtual bool IsSupported( Mtl* Material ) override;
	virtual void Convert( TSharedRef< IDatasmithScene > DatasmithScene, TSharedPtr< IDatasmithBaseMaterialElement >& MaterialElement, Mtl* Material, const TCHAR* AssetsPath ) override;
};

