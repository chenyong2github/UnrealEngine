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
