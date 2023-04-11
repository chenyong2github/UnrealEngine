// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"

#include "MuR/SerialisationPrivate.h"
#include "MuR/MutableMath.h"

namespace mu
{

	MUTABLE_IMPLEMENT_ENUM_SERIALISABLE( EBlendType );
	MUTABLE_IMPLEMENT_ENUM_SERIALISABLE( EMipmapFilterType );
	MUTABLE_IMPLEMENT_ENUM_SERIALISABLE( ECompositeImageMode );
	MUTABLE_IMPLEMENT_ENUM_SERIALISABLE( ESamplingMethod );
	MUTABLE_IMPLEMENT_ENUM_SERIALISABLE( EMinFilterMethod );
	MUTABLE_IMPLEMENT_ENUM_SERIALISABLE( EImageFormat );


	struct FMipmapGenerationSettings
	{
		float m_sharpenFactor = 0.0f;
		EMipmapFilterType m_filterType = EMipmapFilterType::MFT_SimpleAverage;
		EAddressMode m_addressMode = EAddressMode::AM_NONE;
		bool m_ditherMipmapAlpha = false;

		void Serialise( OutputArchive& arch ) const
		{
			uint32 ver = 0;
			arch << ver;

			arch << m_sharpenFactor;
			arch << m_filterType;
			arch << m_ditherMipmapAlpha;
		}

		void Unserialise( InputArchive& arch )
		{
			uint32 ver = 0;
			arch >> ver;
			check(ver == 0);

			arch >> m_sharpenFactor;
			arch >> m_filterType;
			arch >> m_ditherMipmapAlpha;
		}
	};

	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	MUTABLERUNTIME_API inline EImageFormat GetUncompressedFormat(EImageFormat f )
	{
		check(f < EImageFormat::IF_COUNT);

		EImageFormat r = f;

		switch ( r )
		{
		case EImageFormat::IF_L_UBIT_RLE: r = EImageFormat::IF_L_UBYTE; break;
		case EImageFormat::IF_L_UBYTE_RLE: r = EImageFormat::IF_L_UBYTE; break;
		case EImageFormat::IF_RGB_UBYTE_RLE: r = EImageFormat::IF_RGB_UBYTE; break;
        case EImageFormat::IF_RGBA_UBYTE_RLE: r = EImageFormat::IF_RGBA_UBYTE; break;
        case EImageFormat::IF_BC1: r = EImageFormat::IF_RGBA_UBYTE; break;
        case EImageFormat::IF_BC2: r = EImageFormat::IF_RGBA_UBYTE; break;
        case EImageFormat::IF_BC3: r = EImageFormat::IF_RGBA_UBYTE; break;
        case EImageFormat::IF_BC4: r = EImageFormat::IF_L_UBYTE; break;
        case EImageFormat::IF_BC5: r = EImageFormat::IF_RGB_UBYTE; break;
        case EImageFormat::IF_ASTC_4x4_RGB_LDR: r = EImageFormat::IF_RGB_UBYTE; break;
        case EImageFormat::IF_ASTC_4x4_RGBA_LDR: r = EImageFormat::IF_RGBA_UBYTE; break;
        case EImageFormat::IF_ASTC_4x4_RG_LDR: r = EImageFormat::IF_RGB_UBYTE; break;
        default: break;
		}

		return r;
	}

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
	inline EImageFormat GetMostGenericFormat(EImageFormat a, EImageFormat b)
    {
        if (a==b) return a;
        if ( GetImageFormatData(a).m_channels>GetImageFormatData(b).m_channels ) return a;
        if ( GetImageFormatData(b).m_channels>GetImageFormatData(a).m_channels ) return b;
        if (a== EImageFormat::IF_BC2 || a== EImageFormat::IF_BC3 || a== EImageFormat::IF_ASTC_4x4_RGBA_LDR) return a;
        if (b== EImageFormat::IF_BC2 || b== EImageFormat::IF_BC3 || b== EImageFormat::IF_ASTC_4x4_RGBA_LDR) return b;

        return a;
    }


    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
	inline EImageFormat GetRGBOrRGBAFormat(EImageFormat InFormat)
    {
		InFormat = GetUncompressedFormat(InFormat);

		if (InFormat == EImageFormat::IF_NONE)
		{
			return InFormat;
		}

		switch (InFormat)
		{
		case EImageFormat::IF_L_UBYTE: 
		{
			return EImageFormat::IF_RGB_UBYTE;
		}
		case EImageFormat::IF_RGB_UBYTE:
		case EImageFormat::IF_RGBA_UBYTE:
		case EImageFormat::IF_BGRA_UBYTE:
		{
			return InFormat;
		}
		default:
		{
			unimplemented();
		}
		}

		return EImageFormat::IF_NONE;
    }

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
    inline bool IsCompressedFormat(EImageFormat f )
    {
        return f!=GetUncompressedFormat(f);
    }


    //---------------------------------------------------------------------------------------------
    //! Use with care.
    //---------------------------------------------------------------------------------------------
    template<class T>
    Ptr<T> CloneOrTakeOver( const T* source )
    {
        Ptr<T> result;
        if (source->IsUnique())
        {
            result = const_cast<T*>(source);
        }
        else
        {
            result = source->Clone();
        }

        return result;
    }

}

