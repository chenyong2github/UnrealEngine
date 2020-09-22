// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaxMaterialsToUEPbr/DatasmithMaxCoronaMaterialsToUEPbr.h"

#include "DatasmithMaxClassIDs.h"
#include "DatasmithMaxTexmapParser.h"
#include "DatasmithMaxWriter.h"
#include "DatasmithSceneFactory.h"
#include "MaxMaterialsToUEPbr/DatasmithMaxTexmapToUEPbr.h"
#include "MaxMaterialsToUEPbr/DatasmithMaxCoronaTexmapToUEPbr.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "max.h"
	#include "mtl.h"
MAX_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace DatasmithMaxCoronaMaterialsToUEPbrImpl
{
	struct FMaxCoronaMaterial
	{
		// Diffuse
		DatasmithMaxTexmapParser::FWeightedColorParameter Diffuse;
		DatasmithMaxTexmapParser::FMapParameter DiffuseMap;
		float DiffuseLevel = 1.f;

		// Reflection
		DatasmithMaxTexmapParser::FWeightedColorParameter Reflection;
		DatasmithMaxTexmapParser::FMapParameter ReflectionMap;
		float ReflectionLevel = 0.f;

		float ReflectionGlossiness = 0.f;
		DatasmithMaxTexmapParser::FMapParameter ReflectionGlossinessMap;

		// Reflection IOR
		float ReflectionIOR;
		DatasmithMaxTexmapParser::FMapParameter ReflectionIORMap;

		// Refraction
		DatasmithMaxTexmapParser::FWeightedColorParameter Refraction;
		DatasmithMaxTexmapParser::FMapParameter RefractionMap;
		float RefractionLevel = 0.f;

		// Opacity
		DatasmithMaxTexmapParser::FWeightedColorParameter Opacity;
		DatasmithMaxTexmapParser::FMapParameter OpacityMap;
		float OpacityLevel = 0.f;

		// Bump
		DatasmithMaxTexmapParser::FMapParameter BumpMap;

		// Displacement
		DatasmithMaxTexmapParser::FMapParameter DisplacementMap;
	};

	FMaxCoronaMaterial ParseCoronaMaterialProperties( Mtl& Material )
	{
		FMaxCoronaMaterial CoronaMaterialProperties;

		const int NumParamBlocks = Material.NumParamBlocks();

		const TimeValue CurrentTime = GetCOREInterface()->GetTime();

		for (int j = 0; j < NumParamBlocks; j++)
		{
			IParamBlock2* ParamBlock2 = Material.GetParamBlockByID((short)j);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();
			
			for (int i = 0; i < ParamBlockDesc->count; i++)
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[i];

				// Diffuse
				if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("colorDiffuse")) == 0 )
				{
					CoronaMaterialProperties.Diffuse.Value = FDatasmithMaxMatHelper::MaxLinearColorToFLinearColor( (BMM_Color_fl)ParamBlock2->GetColor( ParamDefinition.ID, CurrentTime ) );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapDiffuse")) == 0)
				{
					CoronaMaterialProperties.DiffuseMap.Map = ParamBlock2->GetTexmap(ParamDefinition.ID, CurrentTime);
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("texmapOnDiffuse") ) == 0 )
				{
					CoronaMaterialProperties.DiffuseMap.bEnabled = ( ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime) != 0 );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mapAmountDiffuse")) == 0)
				{
					CoronaMaterialProperties.DiffuseMap.Weight = ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime);
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("levelDiffuse")) == 0)
				{
					CoronaMaterialProperties.DiffuseLevel = ParamBlock2->GetFloat(ParamDefinition.ID, GetCOREInterface()->GetTime());
				}

				// Reflection
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("colorReflect")) == 0 )
				{
					CoronaMaterialProperties.Reflection.Value = FDatasmithMaxMatHelper::MaxLinearColorToFLinearColor( (BMM_Color_fl)ParamBlock2->GetColor( ParamDefinition.ID, CurrentTime ) );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapReflect")) == 0)
				{
					CoronaMaterialProperties.ReflectionMap.Map = ParamBlock2->GetTexmap(ParamDefinition.ID, CurrentTime);
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapOnReflect")) == 0)
				{
					CoronaMaterialProperties.ReflectionMap.bEnabled = ( ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime) != 0 );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mapamountReflect")) == 0)
				{
					CoronaMaterialProperties.ReflectionMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("levelReflect")) == 0)
				{
					CoronaMaterialProperties.ReflectionLevel = ParamBlock2->GetFloat(ParamDefinition.ID, GetCOREInterface()->GetTime());
				}

				// Reflection Glossiness
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("reflectGlossiness") ) == 0 )
				{
					CoronaMaterialProperties.ReflectionGlossiness = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapReflectGlossiness")) == 0)
				{
					CoronaMaterialProperties.ReflectionGlossinessMap.Map = ParamBlock2->GetTexmap(ParamDefinition.ID, CurrentTime);
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapOnReflectGlossiness")) == 0)
				{
					CoronaMaterialProperties.ReflectionGlossinessMap.bEnabled = ( ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime) != 0 );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mapamountReflectGlossiness")) == 0)
				{
					CoronaMaterialProperties.ReflectionGlossinessMap.Weight = ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime);
				}

				// Reflection IOR
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("fresnelIor")) == 0 )
				{
					CoronaMaterialProperties.ReflectionIOR = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapFresnelIor")) == 0 )
				{
					CoronaMaterialProperties.ReflectionIORMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, CurrentTime );
				}
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapOnFresnelIor")) == 0 )
				{
					CoronaMaterialProperties.ReflectionIORMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime ) != 0 );
				}
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("mapamountFresnelIor")) == 0 )
				{
					CoronaMaterialProperties.ReflectionIORMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}

				// Refraction
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("colorRefract")) == 0 )
				{
					CoronaMaterialProperties.Refraction.Value = FDatasmithMaxMatHelper::MaxLinearColorToFLinearColor( (BMM_Color_fl)ParamBlock2->GetColor( ParamDefinition.ID, CurrentTime ) );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapRefract")) == 0)
				{
					CoronaMaterialProperties.RefractionMap.Map = ParamBlock2->GetTexmap(ParamDefinition.ID, CurrentTime);
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapOnRefract")) == 0)
				{
					CoronaMaterialProperties.RefractionMap.bEnabled = ( ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime) != 0 );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mapamountRefract")) == 0)
				{
					CoronaMaterialProperties.RefractionMap.Weight = ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime);
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("levelRefract")) == 0)
				{
					CoronaMaterialProperties.RefractionLevel = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}

				// Opacity
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("ColorOpacity")) == 0)
				{
				CoronaMaterialProperties.Opacity.Value = FDatasmithMaxMatHelper::MaxLinearColorToFLinearColor( (BMM_Color_fl)ParamBlock2->GetColor( ParamDefinition.ID, CurrentTime ) );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapOpacity")) == 0)
				{
					CoronaMaterialProperties.OpacityMap.Map = ParamBlock2->GetTexmap(ParamDefinition.ID, CurrentTime);
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("texmapOnOpacity") ) == 0 )
				{
					CoronaMaterialProperties.OpacityMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime ) != 0 );
				}
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("mapamountOpacity")) == 0 )
				{
					CoronaMaterialProperties.OpacityMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("levelOpacity")) == 0)
				{
					CoronaMaterialProperties.OpacityLevel = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}

				// Bump
				else if ( FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapBump")) == 0 )
				{
					CoronaMaterialProperties.BumpMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, CurrentTime );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mapamountBump")) == 0)
				{
					CoronaMaterialProperties.BumpMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("texmapOnBump") ) == 0 )
				{
					CoronaMaterialProperties.BumpMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime ) != 0 );
				}

				// Displacement
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapDisplace")) == 0)
				{
					CoronaMaterialProperties.DisplacementMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, CurrentTime );
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("texmapOnDisplacement")) == 0)
				{
					CoronaMaterialProperties.DisplacementMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime ) != 0 );
				}
			}
			ParamBlock2->ReleaseDesc();
		}

		return CoronaMaterialProperties;
	}

	struct FMaxCoronaBlendMaterial
	{
		struct FCoronaCoatMaterialProperties
		{
			Mtl* Material = nullptr;
			float Amount = 1.f;

			DatasmithMaxTexmapParser::FMapParameter Mask;
		};
		
		Mtl* BaseMaterial = nullptr;
		static const int32 MaximumNumberOfCoat = 10;
		FCoronaCoatMaterialProperties CoatedMaterials[MaximumNumberOfCoat];
	};

	FMaxCoronaBlendMaterial ParseCoronaBlendMaterialProperties(Mtl& Material)
	{
		FMaxCoronaBlendMaterial CoronaBlendMaterialProperties;
		FMaxCoronaBlendMaterial::FCoronaCoatMaterialProperties* CoatedMaterials = CoronaBlendMaterialProperties.CoatedMaterials;

		const TimeValue CurrentTime = GetCOREInterface()->GetTime();
		const int NumParamBlocks = Material.NumParamBlocks();

		for (int ParemBlockIndex = 0; ParemBlockIndex < NumParamBlocks; ++ParemBlockIndex)
		{
			IParamBlock2* ParamBlock2 = Material.GetParamBlockByID((short)ParemBlockIndex);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();

			for (int DescIndex = 0; DescIndex < ParamBlockDesc->count; ++DescIndex)
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[DescIndex];

				if (FCString::Stricmp(ParamDefinition.int_name, TEXT("baseMtl")) == 0)
				{
					CoronaBlendMaterialProperties.BaseMaterial = ParamBlock2->GetMtl(ParamDefinition.ID, CurrentTime);
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("layers")) == 0)
				{
					for (int CoatIndex = 0; CoatIndex < FMaxCoronaBlendMaterial::MaximumNumberOfCoat; ++CoatIndex)
					{
						CoatedMaterials[CoatIndex].Material = ParamBlock2->GetMtl(ParamDefinition.ID, CurrentTime, CoatIndex);
					}
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("amounts")) == 0)
				{
					for (int CoatIndex = 0; CoatIndex < 9; ++CoatIndex)
					{
						CoatedMaterials[CoatIndex].Amount = ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime, CoatIndex);
					}
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mixmaps")) == 0)
				{
					for (int CoatIndex = 0; CoatIndex < FMaxCoronaBlendMaterial::MaximumNumberOfCoat; ++CoatIndex)
					{
						CoatedMaterials[CoatIndex].Mask.Map = ParamBlock2->GetTexmap(ParamDefinition.ID, CurrentTime, CoatIndex);
					}
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("maskAmounts")) == 0)
				{
					for (int CoatIndex = 0; CoatIndex < 9; ++CoatIndex)
					{
						CoatedMaterials[CoatIndex].Mask.Weight = ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime, CoatIndex);
					}
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("masksOn")) == 0)
				{
					for (int CoatIndex = 0; CoatIndex < 9; ++CoatIndex)
					{
						CoatedMaterials[CoatIndex].Mask.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime, CoatIndex ) != 0 );
					}
				}
			}
			ParamBlock2->ReleaseDesc();
		}

		return CoronaBlendMaterialProperties;
	}
}

FDatasmithMaxCoronaMaterialsToUEPbr::FDatasmithMaxCoronaMaterialsToUEPbr()
{
	TexmapConverters.Add( new FDatasmithMaxCoronaAOToUEPbr() );
	TexmapConverters.Add( new FDatasmithMaxCoronaColorToUEPbr() );
	TexmapConverters.Add( new FDatasmithMaxCoronalNormalToUEPbr() );
	TexmapConverters.Add( new FDatasmithMaxCoronalBitmapToUEPbr() );
}

bool FDatasmithMaxCoronaMaterialsToUEPbr::IsSupported( Mtl* Material )
{
	return true;
}

void FDatasmithMaxCoronaMaterialsToUEPbr::Convert( TSharedRef< IDatasmithScene > DatasmithScene, TSharedPtr< IDatasmithBaseMaterialElement >& MaterialElement, Mtl* Material, const TCHAR* AssetsPath )
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

	DatasmithMaxCoronaMaterialsToUEPbrImpl::FMaxCoronaMaterial CoronaMaterialProperties = DatasmithMaxCoronaMaterialsToUEPbrImpl::ParseCoronaMaterialProperties( *Material );

	ConvertState.DefaultTextureMode = EDatasmithTextureMode::Diffuse; // Both Diffuse and Reflection are considered diffuse maps

	// Diffuse
	IDatasmithMaterialExpression* DiffuseExpression = nullptr;
	{
		DiffuseExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.DiffuseMap, TEXT("Diffuse Color"),
			CoronaMaterialProperties.Diffuse.Value, TOptional< float >() );
	}

	if ( DiffuseExpression )
	{
		DiffuseExpression->SetName( TEXT("Diffuse") );

		IDatasmithMaterialExpressionGeneric* MultiplyDiffuseLevelExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		MultiplyDiffuseLevelExpression->SetExpressionName( TEXT("Multiply") );

		IDatasmithMaterialExpressionScalar* DiffuseLevelExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		DiffuseLevelExpression->SetName( TEXT("Diffuse Level") );
		DiffuseLevelExpression->GetScalar() = CoronaMaterialProperties.DiffuseLevel;

		DiffuseExpression->ConnectExpression( *MultiplyDiffuseLevelExpression->GetInput(0) );
		DiffuseLevelExpression->ConnectExpression( *MultiplyDiffuseLevelExpression->GetInput(1) );

		DiffuseExpression = MultiplyDiffuseLevelExpression;
	}

	// Reflection
	IDatasmithMaterialExpression* ReflectionExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.ReflectionMap, TEXT("Reflection Color"), CoronaMaterialProperties.Reflection.Value, TOptional< float >() );

	if ( ReflectionExpression )
	{
		ReflectionExpression->SetName( TEXT("Reflection") );

		IDatasmithMaterialExpressionGeneric* MultiplyReflectionLevelExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		MultiplyReflectionLevelExpression->SetExpressionName( TEXT("Multiply") );

		IDatasmithMaterialExpressionScalar* ReflectionLevelExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		ReflectionLevelExpression->SetName( TEXT("Reflection Level") );
		ReflectionLevelExpression->GetScalar() = CoronaMaterialProperties.ReflectionLevel;

		ReflectionExpression->ConnectExpression( *MultiplyReflectionLevelExpression->GetInput(0) );
		ReflectionLevelExpression->ConnectExpression( *MultiplyReflectionLevelExpression->GetInput(1) );

		ReflectionExpression = MultiplyReflectionLevelExpression;
	}

	IDatasmithMaterialExpressionGeneric* ReflectionIntensityExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
	ReflectionIntensityExpression->SetExpressionName( TEXT("Desaturation") );

	ReflectionExpression->ConnectExpression( *ReflectionIntensityExpression->GetInput(0) );

	// Glossiness
	IDatasmithMaterialExpression* GlossinessExpression = nullptr;
	{
		TGuardValue< bool > SetIsMonoChannel( ConvertState.bIsMonoChannel, true );

		ConvertState.DefaultTextureMode = EDatasmithTextureMode::Specular;

		GlossinessExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.ReflectionGlossinessMap, TEXT("Reflection Glossiness"), TOptional< FLinearColor >(), CoronaMaterialProperties.ReflectionGlossiness );

		if ( GlossinessExpression )
		{
			GlossinessExpression->SetName( TEXT("Reflection Glossiness") );
		}
	}

	// Bump
	{
		ConvertState.DefaultTextureMode = EDatasmithTextureMode::Bump; // Will change to normal if we pass through a normal map texmap
		ConvertState.bCanBake = false; // Current baking fails to produce proper normal maps

		IDatasmithMaterialExpression* BumpExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.BumpMap, TEXT("Bump Map"), TOptional< FLinearColor >(), TOptional< float >() );

		if ( BumpExpression )
		{
			BumpExpression->ConnectExpression( PbrMaterialElement->GetNormal() );
		}

		if ( BumpExpression )
		{
			BumpExpression->SetName( TEXT("Bump Map") );
		}

		ConvertState.bCanBake = true;
	}
	
	// Displacement
	{
		ConvertState.DefaultTextureMode = EDatasmithTextureMode::Displace;

		IDatasmithMaterialExpression* DisplacementExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.DisplacementMap, TEXT("Displacement Map"), TOptional< FLinearColor >(), TOptional< float >() );

		if ( DisplacementExpression )
		{
			DisplacementExpression->ConnectExpression( PbrMaterialElement->GetWorldDisplacement() );
		}

		if ( DisplacementExpression )
		{
			DisplacementExpression->SetName( TEXT("Displacement Map") );
		}
	}

	ConvertState.DefaultTextureMode = EDatasmithTextureMode::Specular; // At this point, all maps are considered specular maps

	// Opacity
	IDatasmithMaterialExpression* OpacityExpression = nullptr;
	{
		TGuardValue< bool > SetIsMonoChannel( ConvertState.bIsMonoChannel, true );
		OpacityExpression = ConvertTexmap( CoronaMaterialProperties.OpacityMap );
	}

	// Refraction
	CoronaMaterialProperties.Refraction.Weight *= CoronaMaterialProperties.RefractionLevel;
	CoronaMaterialProperties.Refraction.Value *= CoronaMaterialProperties.Refraction.Weight;
	CoronaMaterialProperties.RefractionMap.Weight *= CoronaMaterialProperties.RefractionLevel;

	IDatasmithMaterialExpression* RefractionExpression = nullptr;
	{
		TOptional< FLinearColor > OptionalRefractionColor;

		if ( !CoronaMaterialProperties.Refraction.Value.IsAlmostBlack() )
		{
			OptionalRefractionColor = CoronaMaterialProperties.Refraction.Value;
		}

		RefractionExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.RefractionMap, TEXT("Refraction"), OptionalRefractionColor, TOptional< float >() );
	}

	// UE Roughness
	{
		IDatasmithMaterialExpressionGeneric* MultiplyGlossiness = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		MultiplyGlossiness->SetExpressionName( TEXT("Multiply") );

		GlossinessExpression->ConnectExpression( *MultiplyGlossiness->GetInput(0) );
		GlossinessExpression->ConnectExpression( *MultiplyGlossiness->GetInput(1) );

		IDatasmithMaterialExpression* RoughnessOutput = MultiplyGlossiness;

		IDatasmithMaterialExpressionGeneric* OneMinusRougnessExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		OneMinusRougnessExpression->SetExpressionName( TEXT("OneMinus") );

		MultiplyGlossiness->ConnectExpression( *OneMinusRougnessExpression->GetInput(0) );

		RoughnessOutput = OneMinusRougnessExpression;

		IDatasmithMaterialExpressionGeneric* PowRoughnessExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		PowRoughnessExpression->SetExpressionName( TEXT("Power") );

		TSharedRef< IDatasmithKeyValueProperty > PowRoughnessValue = FDatasmithSceneFactory::CreateKeyValueProperty( TEXT("ConstExponent") );
		PowRoughnessValue->SetPropertyType( EDatasmithKeyValuePropertyType::Float );
		PowRoughnessValue->SetValue( *LexToString( 1.5f ) );

		PowRoughnessExpression->AddProperty( PowRoughnessValue );

		RoughnessOutput->ConnectExpression( *PowRoughnessExpression->GetInput(0) );
		PowRoughnessExpression->ConnectExpression( PbrMaterialElement->GetRoughness() );
	}

	IDatasmithMaterialExpressionGeneric* ReflectionFresnelExpression = nullptr;

	IDatasmithMaterialExpressionGeneric* IORFactor = nullptr;

	{
		DiffuseExpression->ConnectExpression( PbrMaterialElement->GetBaseColor() );

		IDatasmithMaterialExpressionGeneric* DiffuseLerpExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		DiffuseLerpExpression->SetExpressionName( TEXT("LinearInterpolate") );

		DiffuseLerpExpression->ConnectExpression( PbrMaterialElement->GetBaseColor() );

		IDatasmithMaterialExpression* ReflectionIOR = nullptr;
		
		{
			TGuardValue< bool > SetIsMonoChannel( ConvertState.bIsMonoChannel, true );
			ReflectionIOR = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue( this, CoronaMaterialProperties.ReflectionIORMap, TEXT("Fresnel IOR"), TOptional< FLinearColor >(), CoronaMaterialProperties.ReflectionIOR );
		}

		ReflectionIOR->SetName( TEXT("Fresnel IOR") );

		IDatasmithMaterialExpressionScalar* MinusOneFresnelIOR = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		MinusOneFresnelIOR->GetScalar() = -1.f;

		IDatasmithMaterialExpressionGeneric* AddAdjustFresnelIOR = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		AddAdjustFresnelIOR->SetExpressionName( TEXT("Add") );

		ReflectionIOR->ConnectExpression( *AddAdjustFresnelIOR->GetInput(0) );
		MinusOneFresnelIOR->ConnectExpression( *AddAdjustFresnelIOR->GetInput(1) );

		IORFactor = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		IORFactor->SetExpressionName( TEXT("Multiply") );

		IDatasmithMaterialExpressionScalar* ScaleIORScalar = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		ScaleIORScalar->GetScalar() = 0.02f;

		AddAdjustFresnelIOR->ConnectExpression( *IORFactor->GetInput(0) );
		ScaleIORScalar->ConnectExpression( *IORFactor->GetInput(1) );

		IDatasmithMaterialExpressionGeneric* BaseColorIORPow = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		BaseColorIORPow->SetExpressionName( TEXT("Power") );

		IDatasmithMaterialExpressionScalar* BaseColorIORPowScalar = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		BaseColorIORPowScalar->GetScalar() = 0.5f;

		IORFactor->ConnectExpression( *BaseColorIORPow->GetInput(0) );
		BaseColorIORPowScalar->ConnectExpression( *BaseColorIORPow->GetInput(1) );

		IDatasmithMaterialExpressionGeneric* DiffuseIORLerpExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		DiffuseIORLerpExpression->SetExpressionName( TEXT("LinearInterpolate") );

		DiffuseExpression->ConnectExpression( *DiffuseIORLerpExpression->GetInput(0) );
		ReflectionExpression->ConnectExpression( *DiffuseIORLerpExpression->GetInput(1) );
		BaseColorIORPow->ConnectExpression( *DiffuseIORLerpExpression->GetInput(2) );

		DiffuseExpression->ConnectExpression( *DiffuseLerpExpression->GetInput(0) );
		DiffuseIORLerpExpression->ConnectExpression( *DiffuseLerpExpression->GetInput(1) );
		ReflectionIntensityExpression->ConnectExpression( *DiffuseLerpExpression->GetInput(2) );
	}

	// UE Metallic
	IDatasmithMaterialExpression* MetallicExpression = nullptr;
	{
		IDatasmithMaterialExpressionGeneric* MetallicIORPow = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		MetallicIORPow->SetExpressionName( TEXT("Power") );

		IDatasmithMaterialExpressionScalar* MetallicIORPowScalar = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
		MetallicIORPowScalar->GetScalar() = 0.2f;

		IORFactor->ConnectExpression( *MetallicIORPow->GetInput(0) );
		MetallicIORPowScalar->ConnectExpression( *MetallicIORPow->GetInput(1) );

		IDatasmithMaterialExpressionGeneric* MultiplyIORExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
		MultiplyIORExpression->SetExpressionName( TEXT("Multiply") );

		ReflectionIntensityExpression->ConnectExpression( *MultiplyIORExpression->GetInput(0) );
		MetallicIORPow->ConnectExpression( *MultiplyIORExpression->GetInput(1) );

		MetallicExpression = MultiplyIORExpression;
	}

	if ( MetallicExpression )
	{
		MetallicExpression->ConnectExpression( PbrMaterialElement->GetMetallic() );
	}
	
	// UE Specular
	if ( MetallicExpression )
	{
		MetallicExpression->ConnectExpression( PbrMaterialElement->GetSpecular() );
	}

	// UE Opacity & Refraction
	if ( !FMath::IsNearlyZero( CoronaMaterialProperties.RefractionLevel ) && ( OpacityExpression || RefractionExpression ) )
	{
		IDatasmithMaterialExpression* UEOpacityExpression = nullptr;

		if ( RefractionExpression )
		{
			IDatasmithMaterialExpressionGeneric* RefractionIntensity = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
			RefractionIntensity->SetExpressionName( TEXT("Desaturation") );

			RefractionExpression->ConnectExpression( *RefractionIntensity->GetInput(0) );

			IDatasmithMaterialExpressionGeneric* OneMinusRefraction = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
			OneMinusRefraction->SetExpressionName( TEXT("OneMinus") );

			RefractionIntensity->ConnectExpression( *OneMinusRefraction->GetInput(0) );

			if ( OpacityExpression )
			{
				IDatasmithMaterialExpressionGeneric* LerpOpacityRefraction = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
				LerpOpacityRefraction->SetExpressionName( TEXT("LinearInterpolate") );

				OpacityExpression->ConnectExpression( *LerpOpacityRefraction->GetInput(0) );
				OneMinusRefraction->ConnectExpression( *LerpOpacityRefraction->GetInput(1) );
				OpacityExpression->ConnectExpression( *LerpOpacityRefraction->GetInput(2) );

				UEOpacityExpression = LerpOpacityRefraction;
			}
			else
			{
				UEOpacityExpression = OneMinusRefraction;
			}
		}
		else
		{
			UEOpacityExpression = OpacityExpression;
		}

		if ( UEOpacityExpression )
		{
			UEOpacityExpression->ConnectExpression( PbrMaterialElement->GetOpacity() );
			PbrMaterialElement->SetShadingModel( EDatasmithShadingModel::ThinTranslucent );

			IDatasmithMaterialExpressionGeneric* ThinTranslucencyMaterialOutput = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionGeneric >();
			ThinTranslucencyMaterialOutput->SetExpressionName( TEXT("ThinTranslucentMaterialOutput") );

			// Transmittance color
			IDatasmithMaterialExpressionColor* TransmittanceExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionColor >();
			TransmittanceExpression->GetColor() = FLinearColor::White;
			TransmittanceExpression->ConnectExpression( *ThinTranslucencyMaterialOutput->GetInput(0) );
		}
	}

	MaterialElement = PbrMaterialElement;
}

bool FDatasmithMaxCoronaBlendMaterialToUEPbr::IsSupported( Mtl* Material )
{
	using namespace DatasmithMaxCoronaMaterialsToUEPbrImpl;

	if (!Material)
	{
		return false;
	}

	FMaxCoronaBlendMaterial CoronaBlendMaterialProperties = ParseCoronaBlendMaterialProperties(*Material);
	bool bAllMaterialsSupported = true;

	if (CoronaBlendMaterialProperties.BaseMaterial)
	{
		FDatasmithMaxMaterialsToUEPbr* MaterialConverter = FDatasmithMaxMaterialsToUEPbrManager::GetMaterialConverter(CoronaBlendMaterialProperties.BaseMaterial);
		bAllMaterialsSupported &= MaterialConverter && MaterialConverter->IsSupported(CoronaBlendMaterialProperties.BaseMaterial);
	}
	else
	{
		return false;
	}

	for (int CoatIndex = 0; bAllMaterialsSupported && CoatIndex < FMaxCoronaBlendMaterial::MaximumNumberOfCoat; ++CoatIndex)
	{
		const FMaxCoronaBlendMaterial::FCoronaCoatMaterialProperties& CoatedMaterial = CoronaBlendMaterialProperties.CoatedMaterials[CoatIndex];

		if (CoatedMaterial.Material != nullptr && CoatedMaterial.Mask.bEnabled)
		{
			//Only support if all the blended materials are UEPbr materials.
			FDatasmithMaxMaterialsToUEPbr* MaterialConverter = FDatasmithMaxMaterialsToUEPbrManager::GetMaterialConverter(CoatedMaterial.Material);
			bAllMaterialsSupported &= MaterialConverter && MaterialConverter->IsSupported(CoatedMaterial.Material);
		}
	}

	return bAllMaterialsSupported;
}

void FDatasmithMaxCoronaBlendMaterialToUEPbr::Convert( TSharedRef<IDatasmithScene> DatasmithScene, TSharedPtr<IDatasmithBaseMaterialElement>& MaterialElement, Mtl* Material, const TCHAR* AssetsPath )
{
	using namespace DatasmithMaxCoronaMaterialsToUEPbrImpl;

	TSharedRef< IDatasmithUEPbrMaterialElement > PbrMaterialElement = FDatasmithSceneFactory::CreateUEPbrMaterial(Material->GetName().data());
	FScopedConvertState ScopedConvertState(ConvertState);
	ConvertState.DatasmithScene = DatasmithScene;
	ConvertState.MaterialElement = PbrMaterialElement;
	ConvertState.AssetsPath = AssetsPath;

	FMaxCoronaBlendMaterial CoronaBlendMaterialProperties = ParseCoronaBlendMaterialProperties( *Material );

	//Exporting the base material.
	IDatasmithMaterialExpressionFunctionCall* BaseMaterialFunctionCall = PbrMaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
	if (TSharedPtr<IDatasmithBaseMaterialElement> ExportedMaterial = FDatasmithMaxMatExport::ExportUniqueMaterial(DatasmithScene, CoronaBlendMaterialProperties.BaseMaterial, AssetsPath))
	{
		BaseMaterialFunctionCall->SetFunctionPathName(ExportedMaterial->GetName());
	}

	//Exporting the blended materials.
	IDatasmithMaterialExpression* PreviousExpression = BaseMaterialFunctionCall;
	for (int CoatIndex = 0; CoatIndex < FMaxCoronaBlendMaterial::MaximumNumberOfCoat; ++CoatIndex)
	{
		const FMaxCoronaBlendMaterial::FCoronaCoatMaterialProperties& CoatedMaterial = CoronaBlendMaterialProperties.CoatedMaterials[CoatIndex];

		if (CoatedMaterial.Material != nullptr)
		{
			IDatasmithMaterialExpressionFunctionCall* BlendFunctionCall = PbrMaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
			BlendFunctionCall->SetFunctionPathName(TEXT("/Engine/Functions/MaterialLayerFunctions/MatLayerBlend_Standard.MatLayerBlend_Standard"));
			PreviousExpression->ConnectExpression(*BlendFunctionCall->GetInput(0));
			PreviousExpression = BlendFunctionCall;

			IDatasmithMaterialExpressionFunctionCall* LayerMaterialFunctionCall = PbrMaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionFunctionCall>();
			TSharedPtr<IDatasmithBaseMaterialElement> LayerMaterial = FDatasmithMaxMatExport::ExportUniqueMaterial(DatasmithScene, CoatedMaterial.Material, AssetsPath);

			if ( !LayerMaterial )
			{
				continue;
			}

			LayerMaterialFunctionCall->SetFunctionPathName(LayerMaterial->GetName());
			LayerMaterialFunctionCall->ConnectExpression(*BlendFunctionCall->GetInput(1));

			IDatasmithMaterialExpressionScalar* AmountExpression = PbrMaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionScalar>();
			AmountExpression->SetName( TEXT("Layer Amount") );
			AmountExpression->GetScalar() = CoatedMaterial.Amount;

			IDatasmithMaterialExpression* MaskExpression = FDatasmithMaxTexmapToUEPbrUtils::MapOrValue(this, CoatedMaterial.Mask, TEXT("MixAmount"),
				FLinearColor::White, TOptional< float >());
			
			IDatasmithMaterialExpressionGeneric* AlphaExpression = PbrMaterialElement->AddMaterialExpression<IDatasmithMaterialExpressionGeneric>();
			AlphaExpression->SetExpressionName(TEXT("Multiply"));

			//AlphaExpression is nullptr only when there is no mask and the mask weight is ~100% so we add scalar 0 instead.
			if (!MaskExpression) 
			{
				IDatasmithMaterialExpressionScalar* WeightExpression = PbrMaterialElement->AddMaterialExpression< IDatasmithMaterialExpressionScalar >();
				WeightExpression->GetScalar() = 0.f;
				MaskExpression = WeightExpression;
			}

			AmountExpression->ConnectExpression(*AlphaExpression->GetInput(0));
			MaskExpression->ConnectExpression(*AlphaExpression->GetInput(1));

			AlphaExpression->ConnectExpression(*BlendFunctionCall->GetInput(2));
		}
	}

	PbrMaterialElement->SetUseMaterialAttributes(true);
	PreviousExpression->ConnectExpression(PbrMaterialElement->GetMaterialAttributes());
	MaterialElement = PbrMaterialElement;
}