// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"

#include "MuR/SerialisationPrivate.h"
#include "MuR/MutableMath.h"

namespace mu
{

	MUTABLE_DEFINE_ENUM_SERIALISABLE( EBlendType );
	MUTABLE_DEFINE_ENUM_SERIALISABLE( EMipmapFilterType );
	MUTABLE_DEFINE_ENUM_SERIALISABLE( ECompositeImageMode );
	MUTABLE_DEFINE_ENUM_SERIALISABLE( ESamplingMethod );
	MUTABLE_DEFINE_ENUM_SERIALISABLE( EMinFilterMethod );
	MUTABLE_DEFINE_ENUM_SERIALISABLE( EImageFormat );

    struct FImageFormatData
	{
		static constexpr SIZE_T MAX_BYTES_PER_BLOCK = 16;

		FImageFormatData
			(
				unsigned pixelsPerBlockX = 0,
				unsigned pixelsPerBlockY = 0,
				unsigned bytesPerBlock = 0,
				unsigned channels = 0
			)
		{
			m_pixelsPerBlockX = (uint8)pixelsPerBlockX;
			m_pixelsPerBlockY = (uint8)pixelsPerBlockY;
			m_bytesPerBlock = (uint16)bytesPerBlock;
			m_channels = (uint16)channels;	
		}

		FImageFormatData
			(
				unsigned pixelsPerBlockX,
				unsigned pixelsPerBlockY,
				unsigned bytesPerBlock,
				unsigned channels,
				std::initializer_list<uint8> BlackBlockInit
			)
			: FImageFormatData(pixelsPerBlockX, pixelsPerBlockY, bytesPerBlock, channels)
		{
			check(MAX_BYTES_PER_BLOCK >= BlackBlockInit.size());

			const SIZE_T SanitizedBlockSize = FMath::Min<SIZE_T>(MAX_BYTES_PER_BLOCK, BlackBlockInit.size());
			FMemory::Memcpy(BlackBlock, BlackBlockInit.begin(), SanitizedBlockSize);
		}

		//! For block based formats, size of the block size. For uncompressed formats it will
		//! always be 1,1. For non-block-based compressed formats, it will be 0,0.
        uint8 m_pixelsPerBlockX, m_pixelsPerBlockY;

		//! Number of bytes used by every pixel block, if uncompressed or block-compressed format.
		//! For non-block-compressed formats, it returns 0.
        uint16 m_bytesPerBlock;

		//! Channels in every pixel of the image.
        uint16 m_channels;

		//! Representation of a black block of the image.
		uint8 BlackBlock[MAX_BYTES_PER_BLOCK] = { 0 };
	};


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
	MUTABLERUNTIME_API const FImageFormatData& GetImageFormatData(EImageFormat format );


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

