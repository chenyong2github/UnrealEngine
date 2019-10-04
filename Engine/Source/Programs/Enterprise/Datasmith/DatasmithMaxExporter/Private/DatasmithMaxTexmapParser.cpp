// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithMaxTexmapParser.h"

#include "DatasmithMaxExporterDefines.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "bitmap.h"
	#include "iparamb2.h"
	#include "max.h"
	#include "mtl.h"
MAX_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace DatasmithMaxTexmapParser
{
	FCompositeTexmapParameters ParseCompositeTexmap( Texmap* InTexmap )
	{
		FCompositeTexmapParameters CompositeParameters;
		TimeValue CurrentTime = GetCOREInterface()->GetTime();

		const int NumParamBlocks = InTexmap->NumParamBlocks();

		for ( int j = 0; j < NumParamBlocks; j++ )
		{
			IParamBlock2* ParamBlock2 = InTexmap->GetParamBlockByID((short)j);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();

			for ( int i = 0; i < ParamBlockDesc->count; i++ )
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[i];

				if (FCString::Stricmp(ParamDefinition.int_name, TEXT("mapEnabled")) == 0)
				{
					for (int s = 0; s < ParamBlock2->Count(ParamDefinition.ID); s++)
					{
						FCompositeTexmapParameters::FLayer& Layer = CompositeParameters.Layers.AddDefaulted_GetRef();

						Layer.Map.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime, s ) != 0 );
					}
				}
			}

			ParamBlock2->ReleaseDesc();
		}

		for ( int j = 0; j < NumParamBlocks; j++ )
		{
			IParamBlock2* ParamBlock2 = InTexmap->GetParamBlockByID((short)j);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();

			for ( int i = 0; i < ParamBlockDesc->count; i++ )
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[i];

				if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("opacity") ) == 0 )
				{
					for ( int s = 0; s < ParamBlock2->Count(ParamDefinition.ID); s++ )
					{
						FCompositeTexmapParameters::FLayer& Layer = CompositeParameters.Layers[s];
						Layer.Map.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, CurrentTime, s ) / 100.0f;
					}
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("mapList") ) == 0 )
				{
					for ( int s = 0; s < ParamBlock2->Count(ParamDefinition.ID); s++ )
					{
						FCompositeTexmapParameters::FLayer& Layer = CompositeParameters.Layers[s];
						Layer.Map.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, CurrentTime, s );
					}
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("Mask") ) == 0 )
				{
					for ( int s = 0; s < ParamBlock2->Count(ParamDefinition.ID); s++ )
					{
						FCompositeTexmapParameters::FLayer& Layer = CompositeParameters.Layers[s];

						Layer.Mask.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, CurrentTime, s );
						Layer.Mask.bEnabled = ( Layer.Mask.Map != nullptr );
						Layer.Mask.Weight = 1.f;
					}
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("blendmode") ) == 0 )
				{
					for ( int s = 0; s < ParamBlock2->Count(ParamDefinition.ID); s++ )
					{
						FCompositeTexmapParameters::FLayer& Layer = CompositeParameters.Layers[s];

						switch ( ParamBlock2->GetInt( ParamDefinition.ID, CurrentTime, s ) )
						{
						case 1:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Average;
							break;
						case 2:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Add;
							break;
						case 3:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Sub;
							break;
						case 4:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Darken;
							break;
						case 5:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Mult;
							break;
						case 6:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Burn;
							break;
						case 7:
							Layer.CompositeMode = EDatasmithCompositeCompMode::LinearBurn;
							break;
						case 8:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Lighten;
							break;
						case 9:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Screen;
							break;
						case 10:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Dodge;
							break;
						case 11:
							Layer.CompositeMode = EDatasmithCompositeCompMode::LinearDodge;
							break;
						case 14:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Overlay;
							break;
						case 15:
							Layer.CompositeMode = EDatasmithCompositeCompMode::SoftLight;
							break;
						case 16:
							Layer.CompositeMode = EDatasmithCompositeCompMode::HardLight;
							break;
						case 17:
							Layer.CompositeMode = EDatasmithCompositeCompMode::PinLight;
							break;
						case 19:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Difference;
							break;
						case 20:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Exclusion;
							break;
						case 21:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Hue;
							break;
						case 22:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Saturation;
							break;
						case 23:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Color;
							break;
						case 24:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Value;
							break;
						default:
							Layer.CompositeMode = EDatasmithCompositeCompMode::Alpha;
							break;
						}
					}
				}
			}

			ParamBlock2->ReleaseDesc();
		}

		return CompositeParameters;
	}

	FNormalMapParameters ParseNormalMap( Texmap* InTexmap )
	{
		FNormalMapParameters NormalMapParameters;

		for (int j = 0; j < InTexmap->NumParamBlocks(); j++)
		{
			IParamBlock2* ParamBlock2 = InTexmap->GetParamBlockByID((short)j);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();

			for (int i = 0; i < ParamBlockDesc->count; i++)
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[i];
				if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("flip_green") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("flipgreen") ) == 0 )
				{
					NormalMapParameters.bFlipGreen = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime() ) != 0 );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("flip_red") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("flipred") ) == 0 )
				{
					NormalMapParameters.bFlipRed = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime() ) != 0 );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("swap_red_and_green") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("swap_rg") ) == 0 )
				{
					NormalMapParameters.bSwapRedAndGreen = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime() ) != 0 );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("normal_map") ) == 0 )
				{
					NormalMapParameters.NormalMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime() );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("normal_map_on") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("map1on") ) == 0 )
				{
					NormalMapParameters.NormalMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime() ) != 0 );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("normal_map_multiplier") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("mult_spin") ) == 0 )
				{
					NormalMapParameters.NormalMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime() );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("bump_map") ) == 0 )
				{
					NormalMapParameters.BumpMap.Map = ParamBlock2->GetTexmap( ParamDefinition.ID, GetCOREInterface()->GetTime() );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("bump_map_on") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("map2on") ) == 0 )
				{
					NormalMapParameters.BumpMap.bEnabled = ( ParamBlock2->GetInt( ParamDefinition.ID, GetCOREInterface()->GetTime() ) != 0 );
				}
				else if ( FCString::Stricmp( ParamDefinition.int_name, TEXT("bump_map_multiplier") ) == 0 || FCString::Stricmp( ParamDefinition.int_name, TEXT("bump_spin") ) == 0 )
				{
					NormalMapParameters.BumpMap.Weight = ParamBlock2->GetFloat( ParamDefinition.ID, GetCOREInterface()->GetTime() );
				}
			}
		}

		return NormalMapParameters;
	}

	FAutodeskBitmapParameters ParseAutodeskBitmap(Texmap* InTexmap)
	{
		FAutodeskBitmapParameters AutodeskBitmapParameters;

		TimeValue CurrentTime = GetCOREInterface()->GetTime();

		const int NumParamBlocks = InTexmap->NumParamBlocks();
		for (int j = 0; j < NumParamBlocks; j++)
		{
			IParamBlock2* ParamBlock2 = InTexmap->GetParamBlockByID((short)j);
			ParamBlockDesc2* ParamBlockDesc = ParamBlock2->GetDesc();

			for (int i = 0; i < ParamBlockDesc->count; i++)
			{
				const ParamDef& ParamDefinition = ParamBlockDesc->paramdefs[i];

				if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Parameters_Source")) == 0)
				{
					AutodeskBitmapParameters.SourceFile = (ParamBlock2->GetBitmap(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Parameters_Brightness")) == 0)
				{
					AutodeskBitmapParameters.Brightness = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Parameters_Invert_Image")) == 0)
				{
					AutodeskBitmapParameters.InvertImage = (ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Position_X")) == 0)
				{
					AutodeskBitmapParameters.Position.X = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Position_Y")) == 0)
				{
					AutodeskBitmapParameters.Position.Y = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Position_Rotation")) == 0)
				{
					AutodeskBitmapParameters.Rotation = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Scale_Width")) == 0)
				{
					AutodeskBitmapParameters.Scale.X = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Scale_Height")) == 0)
				{
					AutodeskBitmapParameters.Scale.Y = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Repeat_Horizontal")) == 0)
				{
					AutodeskBitmapParameters.RepeatHorizontal = (ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Repeat_Vertical")) == 0)
				{
					AutodeskBitmapParameters.RepeatVertical = (ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Advanced_Parameters_Blur")) == 0)
				{
					AutodeskBitmapParameters.BlurValue = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Advanced_Parameters_Blur_Offset")) == 0)
				{
					AutodeskBitmapParameters.BlurOffset = (ParamBlock2->GetFloat(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Advanced_Parameters_Filtering")) == 0)
				{
					AutodeskBitmapParameters.FilteringValue = (ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime));
				}
				else if (FCString::Stricmp(ParamDefinition.int_name, TEXT("Advanced_Parameters_Map_Channel")) == 0)
				{
					AutodeskBitmapParameters.MapChannel = (ParamBlock2->GetInt(ParamDefinition.ID, CurrentTime));
				}
			}
		}

		return AutodeskBitmapParameters;
	}
}