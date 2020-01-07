// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaxMaterialsToUEPbr/DatasmithMaxScanlineMaterialsToUEPbr.h"

#include "DatasmithMaxClassIDs.h"
#include "DatasmithMaxTexmapParser.h"
#include "DatasmithMaxWriter.h"
#include "DatasmithSceneFactory.h"
#include "MaxMaterialsToUEPbr/DatasmithMaxTexmapToUEPbr.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "max.h"
	#include "mtl.h"
MAX_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace DatasmithMaxScanlineMaterialsToUEPbrImpl
{
	enum class EScanlineMaterialMaps
	{
		Ambient,
		Diffuse,
		SpecularColor,
		SpecularLevel,
		Glossiness,
		SelfIllumination,
		Opacity,
		FilterColor,
		Bump,
		Reflection,
		Refraction,
		Displacement
	};

	struct FMaxScanlineMaterial
	{
		bool bIsTwoSided = false;

		// Diffuse
		FLinearColor DiffuseColor;
		DatasmithMaxTexmapParser::FMapParameter DiffuseMap;

		// Specular color
		FLinearColor SpecularColor;
		DatasmithMaxTexmapParser::FMapParameter SpecularColorMap;

		// Specular level
		float SpecularLevel;
		DatasmithMaxTexmapParser::FMapParameter SpecularLevelMap;

		// Glossiness
		float Glossiness = 0.f;
		DatasmithMaxTexmapParser::FMapParameter GlossinessMap;

		// Opacity
		float Opacity = 1.f;
		DatasmithMaxTexmapParser::FMapParameter OpacityMap;

		// Bump
		DatasmithMaxTexmapParser::FMapParameter BumpMap;

		// Displacement
		DatasmithMaxTexmapParser::FMapParameter DisplacementMap;

		// Self-illumination
		bool bUseSelfIllumColor = false;
		FLinearColor SelfIllumColor;
		DatasmithMaxTexmapParser::FMapParameter SelfIllumMap;
	};

	FMaxScanlineMaterial ParseScanlineMaterialProperties( Mtl& Material )
	{
		FMaxScanlineMaterial ScanlineMaterialProperties;

		const int NumParamBlocks = Material.NumParamBlocks();

		ScanlineMaterialProperties.DiffuseColor = FDatasmithMaxMatHelper::MaxLinearColorToFLinearColor( (BMM_Color_fl)Material.GetDiffuse() );
		ScanlineMaterialProperties.SpecularColor = FDatasmithMaxMatHelper::MaxLinearColorToFLinearColor( (BMM_Color_fl) Material.GetSpecular() );
		ScanlineMaterialProperties.SpecularLevel = Material.GetShinStr();
		ScanlineMaterialProperties.Glossiness = Material.GetShininess();
		ScanlineMaterialProperties.bUseSelfIllumColor = ( Material.GetSelfIllumColorOn() != 0 );
		ScanlineMaterialProperties.SelfIllumColor = FDatasmithMaxMatHelper::MaxColorToFLinearColor( (BMM_Color_fl)Material.GetSelfIllumColor() );

		const TimeValue CurrentTime = GetCOREInterface()->GetTime();

		for (int j = 0; j < NumParamBlocks; j++)
		{
			IParamBlock2* ParamBlock2 = Material.GetParamBlockByID((short)j);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();
			
			for (int i = 0; i < ParamBlockDesc->count; i++)
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[i];

				// Maps
				if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("maps") ) == 0 )
				{
					ScanlineMaterialProperties.DiffuseMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Diffuse );
					ScanlineMaterialProperties.SpecularColorMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SpecularColor );
					ScanlineMaterialProperties.SpecularLevelMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SpecularLevel );
					ScanlineMaterialProperties.GlossinessMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Glossiness );
					ScanlineMaterialProperties.OpacityMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Opacity );
					ScanlineMaterialProperties.BumpMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Bump );
					ScanlineMaterialProperties.DisplacementMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Displacement );
					ScanlineMaterialProperties.SelfIllumMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SelfIllumination );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("mapEnables") ) == 0 )
				{
					ScanlineMaterialProperties.DiffuseMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Diffuse ) != 0 );
					ScanlineMaterialProperties.SpecularColorMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SpecularColor ) != 0 );
					ScanlineMaterialProperties.SpecularLevelMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SpecularLevel ) != 0 );
					ScanlineMaterialProperties.GlossinessMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Glossiness ) != 0 );
					ScanlineMaterialProperties.OpacityMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Opacity ) != 0 );
					ScanlineMaterialProperties.BumpMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Bump ) != 0 );
					ScanlineMaterialProperties.DisplacementMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Displacement ) != 0 );
					ScanlineMaterialProperties.SelfIllumMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SelfIllumination ) != 0 );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("mapAmounts") ) == 0 )
				{
					ScanlineMaterialProperties.DiffuseMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Diffuse );
					ScanlineMaterialProperties.SpecularColorMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SpecularColor );
					ScanlineMaterialProperties.SpecularLevelMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SpecularLevel );
					ScanlineMaterialProperties.GlossinessMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Glossiness );
					ScanlineMaterialProperties.OpacityMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Opacity );
					ScanlineMaterialProperties.BumpMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Bump );
					ScanlineMaterialProperties.DisplacementMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::Displacement );
					ScanlineMaterialProperties.SelfIllumMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime(), (int)EScanlineMaterialMaps::SelfIllumination );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("Opacity") ) == 0 )
				{
					ScanlineMaterialProperties.Opacity = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime() );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("twoSided") ) == 0 )
				{
					ScanlineMaterialProperties.bIsTwoSided = ( ParamBlock2->GetInt(ParamDefinition.ID, GetCOREInterface()->GetTime()) != 0 );
				}
			}
			ParamBlock2->ReleaseDesc();
		}

		return ScanlineMaterialProperties;
	}
}

bool FDatasmithMaxScanlineMaterialsToUEPbr::IsSupported( Mtl* Material )
{
	return true;
}

void FDatasmithMaxScanlineMaterialsToUEPbr::Convert( TSharedRef< IDatasmithScene > DatasmithScene, TSharedPtr< IDatasmithBaseMaterialElement >& MaterialElement, Mtl* Material, const TCHAR* AssetsPath )
{
	if ( !Material )
	{
		return;
	}

	TSharedRef< IDatasmithUEPbrMaterialElement > PbrMaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial( Material->GetName().data() );
	FScopedConvertState ScopedConvertState(ConvertState);
	ConvertState.DatasmithScene = DatasmithScene;
	ConvertState.MaterialElement = PbrMaterialElement;
	ConvertState.AssetsPath = AssetsPath;

	DatasmithMaxScanlineMaterialsToUEPbrImpl::FMaxScanlineMaterial ScanlineMaterialProperties = DatasmithMaxScanlineMaterialsToUEPbrImpl::ParseScanlineMaterialProperties( *Material );

	ConvertState.DefaultTextureMode = EDatasmithTextureMode::Diffuse;

	// Diffuse
	IDatasmithMaterialExpression* DiffuseExpression = nullptr;
	{
		DiffuseExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.DiffuseMap, TEXT("Diffuse Color"),
			ScanlineMaterialProperties.DiffuseColor, TOptional< float >() );
	}

	// Glossiness
	IDatasmithMaterialExpression* GlossinessExpression = nullptr;
	{
		TGuardValue< bool > SetIsMonoChannel( ConvertState.bIsMonoChannel, true );

		ConvertState.DefaultTextureMode = EDatasmithTextureMode::Specular;

		GlossinessExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.GlossinessMap, TEXT("Glossiness"), TOptional< FLinearColor >(), ScanlineMaterialProperties.Glossiness );

		if ( GlossinessExpression )
		{
			IDatasmithMaterialExpressionGeneric* OneMinusRougnessExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
			OneMinusRougnessExpression->SetExpressionName( TEXT("OneMinus") );

			GlossinessExpression->ConnectExpression( *OneMinusRougnessExpression->GetInput(0) );

			OneMinusRougnessExpression->ConnectExpression( PbrMaterialElement->GetRoughness() );
		}
	}

	// Specular
	ConvertState.DefaultTextureMode = EDatasmithTextureMode::Specular;

	IDatasmithMaterialExpression* SpecularColorExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.SpecularColorMap, TEXT("Specular Color"), ScanlineMaterialProperties.SpecularColor, TOptional< float >() );
	IDatasmithMaterialExpression* SpecularExpression = SpecularColorExpression;

	if ( SpecularColorExpression )
	{
		SpecularColorExpression->SetName( TEXT("Specular") );
		
		IDatasmithMaterialExpressionScalar* SpecularLevelExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		SpecularLevelExpression->SetName( TEXT("Specular Level") );
		SpecularLevelExpression->GetScalar() = ScanlineMaterialProperties.SpecularLevel;

		IDatasmithMaterialExpressionGeneric* SpecularGlossinessExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		SpecularGlossinessExpression->SetExpressionName( TEXT("Multiply") );

		SpecularLevelExpression->ConnectExpression( *SpecularGlossinessExpression->GetInput(0), 0 );
		GlossinessExpression->ConnectExpression( *SpecularGlossinessExpression->GetInput(1), 0 );

		IDatasmithMaterialExpressionGeneric* WeightedSpecularExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		WeightedSpecularExpression->SetExpressionName( TEXT("Multiply") );

		SpecularColorExpression->ConnectExpression( *WeightedSpecularExpression->GetInput(0), 0 );
		SpecularGlossinessExpression->ConnectExpression( *WeightedSpecularExpression->GetInput(1), 0 );

		SpecularExpression = WeightedSpecularExpression;
	}

	// Opacity
	IDatasmithMaterialExpression* OpacityExpression = nullptr;
	{
		TGuardValue< bool > SetIsMonoChannel( ConvertState.bIsMonoChannel, true );

		TOptional< float > OpacityValue;
		if ( !FMath::IsNearlyEqual( ScanlineMaterialProperties.Opacity, 1.f ) )
		{
			OpacityValue = ScanlineMaterialProperties.Opacity;
		}

		OpacityExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.OpacityMap, TEXT("Opacity"), TOptional< FLinearColor >(), OpacityValue );

		if ( OpacityExpression )
		{
			OpacityExpression->ConnectExpression( PbrMaterialElement->GetOpacity() );
		}
	}

	// Bump
	{
		ConvertState.DefaultTextureMode = EDatasmithTextureMode::Bump; // Will change to normal if we pass through a normal map texmap
		ConvertState.bCanBake = false; // Current baking fails to produce proper normal maps

		IDatasmithMaterialExpression* BumpExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.BumpMap, TEXT("Bump Map"), TOptional< FLinearColor >(), TOptional< float >() );

		if ( BumpExpression )
		{
			BumpExpression->ConnectExpression( PbrMaterialElement->GetNormal() );
		}

		ConvertState.bCanBake = true;
	}
	
	// Displacement
	{
		ConvertState.DefaultTextureMode = EDatasmithTextureMode::Displace;

		IDatasmithMaterialExpression* DisplacementExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.DisplacementMap, TEXT("Displacement Map"), TOptional< FLinearColor >(), TOptional< float >() );

		if ( DisplacementExpression )
		{
			DisplacementExpression->ConnectExpression( PbrMaterialElement->GetWorldDisplacement() );
		}
	}

	// ConvertFromDiffSpec
	{
		IDatasmithMaterialExpressionFunctionCall* ConvertFromDiffSpecExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionFunctionCall >();
		ConvertFromDiffSpecExpression->SetFunctionPathName( TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions01/Shading/ConvertFromDiffSpec.ConvertFromDiffSpec'") );

		DiffuseExpression->ConnectExpression( *ConvertFromDiffSpecExpression->GetInput(0), 0 );
		SpecularExpression->ConnectExpression( *ConvertFromDiffSpecExpression->GetInput(1), 0 );

		ConvertFromDiffSpecExpression->ConnectExpression( PbrMaterialElement->GetBaseColor(), 0 );
		ConvertFromDiffSpecExpression->ConnectExpression( PbrMaterialElement->GetMetallic(), 1 );
		ConvertFromDiffSpecExpression->ConnectExpression( PbrMaterialElement->GetSpecular(), 2 );
	}

	// Emissive
	{
		TOptional< FLinearColor > SelfIllumColor;

		if ( ScanlineMaterialProperties.bUseSelfIllumColor )
		{
			SelfIllumColor = ScanlineMaterialProperties.SelfIllumColor;
		}

		IDatasmithMaterialExpression* EmissiveExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, ScanlineMaterialProperties.SelfIllumMap, TEXT("Self illumination"), SelfIllumColor, TOptional< float >() );

		if ( EmissiveExpression )
		{
			EmissiveExpression->ConnectExpression( PbrMaterialElement->GetEmissiveColor() );
		}
	}

	PbrMaterialElement->SetTwoSided( ScanlineMaterialProperties.bIsTwoSided );

	MaterialElement = PbrMaterialElement;
}
