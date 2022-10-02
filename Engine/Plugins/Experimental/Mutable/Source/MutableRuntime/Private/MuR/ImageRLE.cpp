// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/ImageRLE.h"

#include "HAL/UnrealMemory.h"
#include "Misc/AssertionMacros.h"
#include "MuR/MemoryPrivate.h"

#include <memory>
#include <utility>

namespace mu
{

    //---------------------------------------------------------------------------------------------
    uint32_t CompressRLE_L( int32 width, int32 rows, const uint8_t* pBaseData, uint8_t* destData, uint32_t destDataSize )
    {
		const uint8_t* InitialBaseData = pBaseData;

        uint8_t* rle = destData;

        size_t maxSize = destDataSize;

        // The first uint32_t will be the total mip size.
        // Then there is an offset from the initial data pointer for each line.
        uint32_t offset = sizeof( uint32_t ) * ( rows + 1 );

        // Could happen in an image of 1x100, for example.
        if ( offset >= maxSize )
        {
            return 0;
        }

        for ( int r = 0; r < rows; ++r )
        {
            uint32_t* pOffset = (uint32_t*)&rle[sizeof( uint32_t ) * ( r + 1 )];
            FMemory::Memmove( pOffset, &offset, sizeof( uint32_t ) );

            const uint8_t* pBaseRowEnd = pBaseData + width;
            while ( pBaseData != pBaseRowEnd )
            {
                // Count equal pixels
                uint8_t equalPixel = *pBaseData;

                uint16_t equal = 0;
                while ( pBaseData != pBaseRowEnd && equal < 65535 && pBaseData[0] == equalPixel )
                {
                    pBaseData++;
                    equal++;
                }

                // Count different pixels
                uint8_t different = 0;
                const uint8_t* pDifferentPixels = pBaseData;
                while ( pBaseData < pBaseRowEnd - 1 && different < 255 &&
                        // Last in the row, or different from next
                        ( pBaseData == pBaseRowEnd || pBaseData[0] != pBaseData[1] ) )
                {
                    pBaseData++;
                    different++;
                }

                // Copy header
                if ( maxSize < offset + 4 )
                {
                    return 0;
                }
                FMemory::Memmove( &rle[offset], &equal, sizeof( uint16_t ) );
                offset += 2;
                rle[offset] = different;
                ++offset;

                // Copy the equal pixel
                rle[offset] = equalPixel;
                ++offset;

                // Copy the different pixels
                if ( different )
                {
                    if ( maxSize < offset + different )
                    {
                        return 0;
                    }
                    FMemory::Memmove( &rle[offset], pDifferentPixels, different );
                    offset += different;
                }
            }
        }

        uint32_t* pTotalSize = (uint32_t*)rle;
        *pTotalSize = offset;

#ifdef MUTABLE_DEBUG_RLE		
		{
			TArray<uint8> Temp;
			Temp.SetNum(width*rows);
			UncompressRLE_L(width,rows,destData,Temp.GetData());
			int Difference = FMemory::Memcmp(Temp.GetData(), InitialBaseData, width * rows);
			if (Difference)
			{
				// Different pos.
				size_t Delta = 0;
				for ( ; Delta<width*rows; ++Delta )
				{
					if (Temp[Delta] != InitialBaseData[Delta])
					{
						break;
					}
				}

				UncompressRLE_L(width, rows, destData, Temp.GetData());
				CompressRLE_L( width, rows, InitialBaseData, destData, destDataSize);
			}
		}
#endif

        // succeded
        return offset;
    }


    //---------------------------------------------------------------------------------------------
    uint32_t UncompressRLE_L( int32 width, int32 rows, const uint8* pStartBaseData, uint8* pStartDestData )
    {
		const uint8* pBaseData = pStartBaseData;
		uint8* pDestData = pStartDestData;
		pBaseData += sizeof(uint32); // Total mip size
        pBaseData += rows*sizeof(uint32); // Size of each row.

        for ( int r=0; r<rows; ++r )
        {
            const uint8* pDestRowEnd = pDestData + width;
            while ( pDestData!=pDestRowEnd )
            {
                // Decode header
                uint16 equal = 0;
                FMemory::Memmove(&equal, pBaseData, sizeof(uint16));
                pBaseData += 2;

                uint8_t different = *pBaseData;
                ++pBaseData;

                uint8_t equalPixel = *pBaseData;
                ++pBaseData;

                if (equal)
                {
					check(pDestData + equal <= pStartDestData + width * rows);
					FMemory::Memset(pDestData, equalPixel, equal);
                    pDestData += equal;
                }

                if (different)
                {
					check(pDestData + different <= pStartDestData + width * rows);
					FMemory::Memmove( pDestData, pBaseData, different );
                    pDestData += different;
                    pBaseData += different;
                }
            }
        }

        size_t totalSize = pBaseData-pStartBaseData;
        check( totalSize==*(uint32*)pStartBaseData );

        return (uint32)totalSize;
    }

    //---------------------------------------------------------------------------------------------
    uint32_t CompressRLE_L1( int32 width, int32 rows,
                             const uint8_t* pBaseData,
                             uint8_t* destData,
                             uint32_t destDataSize )
    {
        vector<int8_t> rle;
        rle.reserve(  (width * rows) / 2 );

        uint32_t offset = sizeof(uint32_t)*(rows+1);
        rle.resize( offset );

        for ( int r=0; r<rows; ++r )
        {
            uint32_t* pOffset = (uint32_t*) &rle[ sizeof(uint32_t)*(r+1) ];
            *pOffset = offset;

            const uint8_t* pBaseRowEnd = pBaseData + width;
            while ( pBaseData!=pBaseRowEnd )
            {
                // Count 0 pixels
                uint16_t zeroPixels = 0;
                while ( pBaseData!=pBaseRowEnd
                        && !*pBaseData )
                {
                    pBaseData++;
                    zeroPixels++;
                }

                // Count 1 pixels
                uint16_t onePixels = 0;
                while ( pBaseData!=pBaseRowEnd
                        && *pBaseData )
                {
                    pBaseData++;
                    onePixels++;
                }

                // Copy block
                rle.resize( rle.size()+4 );
                FMemory::Memmove(&rle[ offset ], &zeroPixels, sizeof(uint16_t));
                offset += 2;

                FMemory::Memmove(&rle[ offset ], &onePixels, sizeof(uint16_t));
                offset += 2;
            }
        }

        if (destDataSize<rle.size())
        {
            // Failed
            return 0;
        }

        uint32_t* pTotalSize = (uint32_t*) &rle[ 0 ];
        *pTotalSize = offset;

        FMemory::Memmove( destData, &rle[0], offset );

        // succeded
        return offset;
    }


    //---------------------------------------------------------------------------------------------
    uint32_t UncompressRLE_L1( int width, int rows, const uint8_t* pStartBaseData, uint8_t* pDestData )
    {
        const uint8_t* pBaseData = pStartBaseData;
        pBaseData += sizeof(uint32_t); // Total mip size
        pBaseData += rows*sizeof(uint32_t);

        for ( int r=0; r<rows; ++r )
        {
            const uint8_t* pDestRowEnd = pDestData + width;
            while ( pDestData!=pDestRowEnd )
            {
                // Decode header
                uint16_t zeroPixels = 0;
                FMemory::Memmove(&zeroPixels, pBaseData, sizeof(uint16_t));
                pBaseData += 2;

                uint16_t onePixels = 0;
                FMemory::Memmove(&onePixels, pBaseData, sizeof(uint16_t));
                pBaseData += 2;

                if (zeroPixels)
                {
                    FMemory::Memzero( pDestData, zeroPixels );
                    pDestData += zeroPixels;
                }

                if (onePixels)
                {
                    FMemory::Memset( pDestData, 255, onePixels );
                    pDestData += onePixels;
                }
            }
        }

        size_t totalSize = pBaseData-pStartBaseData;
        check( totalSize==*(uint32_t*)pStartBaseData );

        return (uint32_t)totalSize;
    }


    //---------------------------------------------------------------------------------------------
    void CompressRLE_RGBA( int width, int rows,
                           const uint8_t* pBaseDataByte,
                           TArray<uint8>& destData )
    {
        // TODO: Support for compression from compressed data size, like L_RLE formats.
        vector<int8_t> rle;
        rle.reserve(  (width*rows) );

        const uint32_t* pBaseData = (const uint32_t*)pBaseDataByte;
        rle.resize( rows*4 );
        uint32_t offset = sizeof(uint32_t)*rows;
        for ( int r=0; r<rows; ++r )
        {
            uint32_t* pOffset = (uint32_t*) &rle[ sizeof(uint32_t)*r ];
            FMemory::Memmove(pOffset, &offset, sizeof(uint32_t));

            const uint32_t* pBaseRowEnd = pBaseData + width;
            while ( pBaseData!=pBaseRowEnd )
            {
                // Count equal pixels
                uint32_t equalPixel = *pBaseData;

                uint16_t equal = 0;
                while ( pBaseData<pBaseRowEnd-3 && equal<65535
                        && pBaseData[0]==equalPixel && pBaseData[1]==equalPixel
                        && pBaseData[2]==equalPixel && pBaseData[3]==equalPixel )
                {
                    pBaseData+=4;
                    equal++;
                }

                // Count different pixels
                uint16_t different = 0;
                const uint32_t* pDifferentPixels = pBaseData;
                while ( pBaseData!=pBaseRowEnd
                        &&
                        different<65535
                        &&
                        // Last in the row, or different from next
                        ( pBaseData>pBaseRowEnd-4
                          || pBaseData[0]!=pBaseData[1]
                          || pBaseData[0]!=pBaseData[2]
                          || pBaseData[0]!=pBaseData[3]
                          )
                        )
                {
					pBaseData += FMath::Min(int64(4), int64(pBaseRowEnd - pBaseData));
                    different++;
                }

                // Copy header
                rle.resize( rle.size()+8 );
                FMemory::Memmove(&rle[ offset ], &equal, sizeof(uint16_t));
                offset += 2;
                FMemory::Memmove(&rle[ offset ], &different, sizeof(uint16_t));
                offset += 2;
                FMemory::Memmove(&rle[ offset ], &equalPixel, sizeof(uint32_t));
                offset += 4;

                // Copy the different pixels
				if (different)
				{
					// If we are at the end of a row, maybe there isn't a block of 4 pixels
					uint16_t BytesToCopy = FMath::Min(different * 4 * 4, uint16_t(pBaseRowEnd - pDifferentPixels) * 4);

					rle.resize( rle.size()+ BytesToCopy);
                    FMemory::Memmove( &rle[offset], pDifferentPixels, BytesToCopy);
					offset += BytesToCopy;
				}
            }
        }

        destData.SetNum( offset );
        if ( offset )
        {
            FMemory::Memmove( &destData[0], &rle[0], offset );
        }
    }


    //---------------------------------------------------------------------------------------------
    void UncompressRLE_RGBA( int width, int rows, const uint8_t* pBaseData, uint8_t* pDestDataB )
    {
        uint32_t* pDestData = reinterpret_cast<uint32_t*>( pDestDataB );

        pBaseData += rows*sizeof(uint32_t);

        int pendingPixels = width*rows;

        for ( int r=0; r<rows; ++r )
        {
            const uint32_t* pDestRowEnd = pDestData + width;
            while ( pDestData!=pDestRowEnd )
            {
                // Decode header
                uint16_t equal = 0;
                FMemory::Memmove(&equal, pBaseData, sizeof(uint16_t));
                pBaseData += 2;

                uint16_t different = 0;
                FMemory::Memmove(&different, pBaseData, sizeof(uint16_t));
                pBaseData += 2;

                uint32_t equalPixel = 0;
                FMemory::Memmove(&equalPixel, pBaseData, sizeof(uint32_t));
                pBaseData += 4;

                check((equal+different)*4<=pendingPixels);

                for ( int e=0; e<equal*4; ++e )
                {
                    FMemory::Memmove( pDestData, &equalPixel, 4 );
                    ++pDestData;
                    pendingPixels--;
                }

				if (different)
				{
					// If we are at the end of a row, maybe there isn't a block of 4 pixels
					uint16_t PixelsToCopy = FMath::Min(uint16_t(different * 4), uint16_t(pDestRowEnd - pDestData));

					FMemory::Memmove( pDestData, pBaseData, PixelsToCopy *4 );
					pDestData += PixelsToCopy;
					pBaseData += PixelsToCopy *4;
                    pendingPixels-= PixelsToCopy;
                }
            }
        }

        check(pendingPixels==0);
    }


    //---------------------------------------------------------------------------------------------
    struct UINT24
    {
        uint8_t d[3];

        bool operator==( const UINT24& o ) const
        {
            return d[0]==o.d[0] && d[1]==o.d[1] && d[2]==o.d[2];
        }

        bool operator!=( const UINT24& o ) const
        {
            return d[0]!=o.d[0] || d[1]!=o.d[1] || d[2]!=o.d[2];
        }
    };
    static_assert( sizeof(UINT24)==3, "Uint24SizeCheck" );


    //---------------------------------------------------------------------------------------------
    void CompressRLE_RGB( int width, int rows,
                          const uint8_t* pBaseDataByte,
                          TArray<uint8>& destData )
    {
        vector<int8_t> rle;
        rle.reserve(  (width*rows) );

        const UINT24* pBaseData = (const UINT24*)pBaseDataByte;
        rle.resize( rows*4 );
        uint32_t offset = sizeof(uint32_t)*rows;
        for ( int r=0; r<rows; ++r )
        {
            uint32_t* pOffset = (uint32_t*) &rle[ sizeof(uint32_t)*r ];
            *pOffset = offset;

            const UINT24* pBaseRowEnd = pBaseData + width;
            while ( pBaseData!=pBaseRowEnd )
            {
                // Count equal pixels
                UINT24 equalPixel = *pBaseData;

                uint16_t equal = 0;
                while ( pBaseData<pBaseRowEnd-3 && equal<65535
                        && pBaseData[0]==equalPixel && pBaseData[1]==equalPixel
                        && pBaseData[2]==equalPixel && pBaseData[3]==equalPixel )
                {
                    pBaseData+=4;
                    equal++;
                }

                // Count different pixels
                uint16_t different = 0;
                const UINT24* pDifferentPixels = pBaseData;
                while ( pBaseData!=pBaseRowEnd
                        &&
                        different<65535
                        &&
                        // Last pixels in the row, or different from next
                        ( pBaseData>pBaseRowEnd-4
                          || pBaseData[0]!=pBaseData[1]
                          || pBaseData[0]!=pBaseData[2]
                          || pBaseData[0]!=pBaseData[3]
                          )
                        )
                {
                    pBaseData+=FMath::Min(int64(4), int64(pBaseRowEnd-pBaseData));
                    different++;
                }

                // Copy header
                rle.resize( rle.size()+8 );
                FMemory::Memmove( &rle[offset], &equal, sizeof(uint16_t) );
                offset += 2;
                FMemory::Memmove( &rle[offset], &different, sizeof(uint16_t) );
                offset += 2;
                FMemory::Memmove( &rle[offset], &equalPixel, sizeof(UINT24) );
                offset += 4;

                // Copy the different pixels
				if (different)
				{
					// If we are at the end of a row, maybe there isn't a block of 4 pixels
					uint16_t BytesToCopy = FMath::Min(different * 4 * 3, uint16_t(pBaseRowEnd- pDifferentPixels)*3 );

					rle.resize( rle.size()+BytesToCopy );
                    FMemory::Memmove( &rle[offset], pDifferentPixels, BytesToCopy );
					offset += BytesToCopy;
				}
            }
        }

        destData.SetNum( offset );
        if ( offset )
        {
            FMemory::Memmove( &destData[0], &rle[0], offset );
        }
    }


    //---------------------------------------------------------------------------------------------
    void UncompressRLE_RGB( int width, int rows, const uint8_t* pBaseData, uint8_t* pDestDataB )
    {
        UINT24* pDestData = reinterpret_cast<UINT24*>( pDestDataB );

        pBaseData += rows*sizeof(uint32_t);

        for ( int r=0; r<rows; ++r )
        {
            const UINT24* pDestRowEnd = pDestData + width;
            while ( pDestData!=pDestRowEnd )
            {
                // Decode header
                uint16_t equal = *(const uint16_t*)pBaseData;
                pBaseData += 2;

                uint16_t different = *(const uint16_t*)pBaseData;
                pBaseData += 2;

                UINT24 equalPixel = *(const UINT24*)pBaseData;
                pBaseData += 4;

                for ( int e=0; e<equal*4; ++e )
                {
                    FMemory::Memmove( pDestData, &equalPixel, 3 );
                    ++pDestData;
                }

				if (different)
				{
					// If we are at the end of a row, maybe there isn't a block of 4 pixels
					uint16_t PixelsToCopy = FMath::Min(uint16_t(different * 4), uint16_t(pDestRowEnd - pDestData));

					FMemory::Memmove( pDestData, pBaseData, PixelsToCopy*3);
					pDestData += PixelsToCopy;
					pBaseData += PixelsToCopy*3;
				}
            }
        }
    }



}

