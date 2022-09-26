// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Mesh.h"
#include "Platform.h"
#include "MutableMath.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	//! Convert one channel element
	//---------------------------------------------------------------------------------------------
	inline void ConvertData
		(
			int channel,
			void* pResult, MESH_BUFFER_FORMAT resultFormat,
			const void* pSource, MESH_BUFFER_FORMAT sourceFormat
		)
	{
		switch ( resultFormat )
		{
		case MBF_FLOAT64:
		{
			double* pTypedResult = reinterpret_cast<double*>(pResult);
			uint8_t* pByteResult = reinterpret_cast<uint8_t*>(pResult);
			const uint8_t* pByteSource = reinterpret_cast<const uint8_t*>(pSource);

			switch (sourceFormat)
			{
			case MBF_FLOAT64:
			{
				// Just dereferencing is not safe in all architectures. some like ARM require the
				// floats to be 4-byte aligned and it may not be the case. We need memcpy.
				memcpy(pByteResult + 8 * channel, pByteSource + 8* channel, 8);
				break;
			}

			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>(pSource);
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>(pSource);
				pTypedResult[channel] = halfToFloat(pTypedSource[channel]);
				break;
			}

			case MBF_INT32:
			{
				const int32_t* pTypedSource = reinterpret_cast<const int32_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				break;
			}

			case MBF_UINT32:
			{
				const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				break;
			}

			case MBF_INT16:
			{
				const int16_t* pTypedSource = reinterpret_cast<const int16_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				break;
			}

			case MBF_UINT16:
			{
				const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				break;
			}

			case MBF_INT8:
			{
				const int8_t* pTypedSource = reinterpret_cast<const int8_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				break;
			}

			case MBF_UINT8:
			{
				const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				break;
			}

			case MBF_NINT32:
			{
				const int32_t* pTypedSource = reinterpret_cast<const int32_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 65536.0f * 65536.0f / 2.0f;
				break;
			}

			case MBF_NUINT32:
			{
				const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 65536.0f * 65536.0f - 1.0f;
				break;
			}

			case MBF_NINT16:
			{
				const int16_t* pTypedSource = reinterpret_cast<const int16_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 32768.0f;
				break;
			}

			case MBF_NUINT16:
			{
				const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 65535.0f;
				break;
			}

			case MBF_NINT8:
			{
				const int8_t* pTypedSource = reinterpret_cast<const int8_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 128.0f;
				break;
			}

			case MBF_NUINT8:
			{
				const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 255.0f;
				break;
			}

			case MBF_PACKEDDIR8:
			case MBF_PACKEDDIR8_W_TANGENTSIGN:
			{
				const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 127.5f;
				pTypedResult[channel] -= 1.0f;
				break;
			}

			case MBF_PACKEDDIRS8:
			case MBF_PACKEDDIRS8_W_TANGENTSIGN:
			{
				const int8_t* pTypedSource = reinterpret_cast<const int8_t*>(pSource);
				pTypedResult[channel] = (double)(pTypedSource[channel]);
				pTypedResult[channel] /= 127.5f;
				break;
			}

			default:
				checkf(false, TEXT("Conversion not implemented."));
				break;
			}
			break;
		}

		case MBF_FLOAT32:
		{
			float* pTypedResult = reinterpret_cast<float*>( pResult );
            uint8_t* pByteResult = reinterpret_cast<uint8_t*>( pResult );
            const uint8_t* pByteSource = reinterpret_cast<const uint8_t*>( pSource );

			switch ( sourceFormat )
			{
			case MBF_FLOAT64:
			{
				const double* pTypedSource = reinterpret_cast<const double*>(pSource);
				pTypedResult[channel] = float(pTypedSource[channel]);
				break;
			}

			case MBF_FLOAT32:
			{
				// Just dereferencing is not safe in all architectures. some like ARM require the
				// floats to be 4-byte aligned and it may not be the case. We need memcpy.
				memcpy(pByteResult + 4 * channel, pByteSource + 4 * channel, 4);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
				pTypedResult[channel] = halfToFloat(pTypedSource[channel]);
				break;
			}

			case MBF_INT32:
			{
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				break;
			}

			case MBF_UINT32:
			{
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				break;
			}

			case MBF_INT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				break;
			}

			case MBF_UINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				break;
			}

			case MBF_NINT32:
			{
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				pTypedResult[channel] /= 65536.0f*65536.0f/2.0f;
				break;
			}

			case MBF_NUINT32:
			{
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				pTypedResult[channel] /= 65536.0f*65536.0f-1.0f;
				break;
			}

			case MBF_NINT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				pTypedResult[channel] /= 32768.0f;
				break;
			}

			case MBF_NUINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				pTypedResult[channel] /= 65535.0f;
				break;
			}

			case MBF_NINT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				pTypedResult[channel] /= 128.0f;
				break;
			}

			case MBF_NUINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
				pTypedResult[channel] = (float)(pTypedSource[channel]);
				pTypedResult[channel] /= 255.0f;
				break;
			}

            case MBF_PACKEDDIR8:
            case MBF_PACKEDDIR8_W_TANGENTSIGN:
            {
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
                pTypedResult[channel] = (float)(pTypedSource[channel]);
                pTypedResult[channel] /= 127.5f;
                pTypedResult[channel] -= 1.0f;
                break;
            }

            case MBF_PACKEDDIRS8:
            case MBF_PACKEDDIRS8_W_TANGENTSIGN:
            {
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                pTypedResult[channel] = (float)(pTypedSource[channel]);
                pTypedResult[channel] /= 127.5f;
                break;
            }

			default:
				checkf( false, TEXT("Conversion not implemented.") );
				break;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case MBF_FLOAT16:
		{
			float16* pTypedResult = reinterpret_cast<float16*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
				pTypedResult[channel] = floatToHalf(pTypedSource[channel]);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_UINT32:
			{
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
				pTypedResult[channel] = floatToHalf((float)(pTypedSource[channel]));
				break;
			}

			case MBF_INT32:
			{
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
				pTypedResult[channel] = floatToHalf((float)(pTypedSource[channel]));
				break;
			}

			case MBF_UINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
				pTypedResult[channel] = floatToHalf((float)(pTypedSource[channel]));
				break;
			}

			case MBF_INT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
				pTypedResult[channel] = floatToHalf((float)(pTypedSource[channel]));
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
				pTypedResult[channel] = floatToHalf((float)(pTypedSource[channel]));
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
				pTypedResult[channel] = floatToHalf((float)(pTypedSource[channel]));
				break;
			}

			default:
				checkf( false, TEXT("Conversion not implemented.") );
				break;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case MBF_UINT8:
		{
            uint8_t* pTypedResult = reinterpret_cast<uint8_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (uint8_t)
                        std::min<uint32_t>(
							0xFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (uint8_t)
                        std::min<uint32_t>(
							0xFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)halfToFloat(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                pTypedResult[channel] =  (uint8_t)std::max<int8_t>( 0, pTypedSource[channel] );
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_INT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
                pTypedResult[channel] =  (uint8_t)
                        std::min<int16_t>(
							0xFF,
                            std::max<int16_t>(
								0,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_UINT16:
			{
				// Clamp
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
                pTypedResult[channel] = (uint8_t)
                        std::min<uint16_t>(
							0xFF,
                            std::max<uint16_t>(
								0,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_INT32:
			{
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
                pTypedResult[channel] =  (uint8_t)
                        std::min<int32_t>(
							0xFF,
                            std::max<int32_t>(
								0,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_UINT32:
			{
				// Clamp
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
                pTypedResult[channel] = (uint8_t)
                        std::min<uint32_t>(
							0xFF,
                            std::max<uint32_t>(
								0,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_NUINT8:
			case MBF_NUINT16:
			case MBF_NUINT32:
			case MBF_NINT8:
			case MBF_NINT16:
			case MBF_NINT32:
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_UINT16:
		{
            uint16_t* pTypedResult = reinterpret_cast<uint16_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (uint16_t)
                        std::min<uint32_t>(
							0xFFFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (uint16_t)
                        std::min<uint32_t>(
							0xFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)halfToFloat(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
				pTypedResult[channel] =  pTypedSource[channel];
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                pTypedResult[channel] =  (uint16_t)std::max<int8_t>( 0, pTypedSource[channel] );
				break;
			}

			case MBF_UINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
				pTypedResult[channel] =  pTypedSource[channel];
				break;
			}

			case MBF_INT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
                pTypedResult[channel] =  (uint16_t)std::max<int16_t>( 0, pTypedSource[channel] );
				break;
			}

			case MBF_UINT32:
			{
				// Clamp
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
                pTypedResult[channel] = (uint16_t)
                        std::min<uint32_t>(
							0xFFFF,
                            std::max<uint32_t>(
								0,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_INT32:
			{
				// Clamp
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
                pTypedResult[channel] = (uint16_t)
                        std::min<int32_t>(
							0xFFFF,
                            std::max<int32_t>(
								0,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_NUINT8:
			case MBF_NUINT16:
			case MBF_NUINT32:
			case MBF_NINT8:
			case MBF_NINT16:
			case MBF_NINT32:
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_UINT32:
		{
            uint32_t* pTypedResult = reinterpret_cast<uint32_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
				pTypedResult[channel] =
                        std::min<uint32_t>(
							0xFFFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
				pTypedResult[channel] =
                        std::min<uint32_t>(
							0xFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)halfToFloat(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
				pTypedResult[channel] =  pTypedSource[channel];
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                pTypedResult[channel] =  (uint16_t)std::max<int8_t>( 0, pTypedSource[channel] );
				break;
			}

			case MBF_UINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
				pTypedResult[channel] =  pTypedSource[channel];
				break;
			}

			case MBF_INT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
                pTypedResult[channel] =  (uint16_t)std::max<int16_t>( 0, pTypedSource[channel] );
				break;
			}

			case MBF_UINT32:
			{
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_INT32:
			{
				// Clamp
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
                pTypedResult[channel] = (uint32_t)std::max<int32_t>( 0, pTypedSource[channel] );
				break;
			}

			case MBF_NUINT8:
			case MBF_NUINT16:
			case MBF_NUINT32:
			case MBF_NINT8:
			case MBF_NINT16:
			case MBF_NINT32:
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_INT8:
		{
            int8_t* pTypedResult = reinterpret_cast<int8_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (int8_t)
                        std::min<int32_t>(
							127,
                            std::max<int32_t>(
								-128,
                                (int32_t)(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (int8_t)
                        std::min<int32_t>(
							127,
                            std::max<int32_t>(
								-128,
                                (int32_t)halfToFloat(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_NUINT8:
			case MBF_NUINT16:
			case MBF_NUINT32:
			case MBF_NINT8:
			case MBF_NINT16:
			case MBF_NINT32:
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_INT16:
		{
            int16_t* pTypedResult = reinterpret_cast<int16_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
							32767,
                            std::max<int32_t>(
								-32768,
                                (int32_t)(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
							32767,
                            std::max<int32_t>(
								-32768,
                                (int32_t)halfToFloat(pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                pTypedResult[channel] = (int16_t)pTypedSource[channel];
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
                pTypedResult[channel] = (int16_t)pTypedSource[channel];
				break;
			}

			case MBF_UINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
                            32767, (int32_t)pTypedSource[channel]
							);
				break;
			}

			case MBF_INT32:
			{
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
							32767,
                            std::max<int32_t>(
								-32768,
								pTypedSource[channel]
								)
							);
				break;
			}

			case MBF_UINT32:
			{
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
							32767, pTypedSource[channel]
							);
				break;
			}

			case MBF_NUINT8:
			case MBF_NUINT16:
			case MBF_NUINT32:
			case MBF_NINT8:
			case MBF_NINT16:
			case MBF_NINT32:
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_INT32:
		{
            int32_t* pTypedResult = reinterpret_cast<int32_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (int32_t)(pTypedSource[channel]);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (int32_t)halfToFloat(pTypedSource[channel]);
				break;
			}

			case MBF_INT8:
			{
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                pTypedResult[channel] = (int32_t)pTypedSource[channel];
				break;
			}

			case MBF_UINT8:
			{
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
                pTypedResult[channel] = (int32_t)pTypedSource[channel];
				break;
			}

			case MBF_INT16:
			{
                const int16_t* pTypedSource = reinterpret_cast<const int16_t*>( pSource );
                pTypedResult[channel] = (int32_t)pTypedSource[channel];
				break;
			}

			case MBF_UINT16:
			{
                const uint16_t* pTypedSource = reinterpret_cast<const uint16_t*>( pSource );
                pTypedResult[channel] = (int32_t)pTypedSource[channel];
				break;
			}

			case MBF_UINT32:
			{
                const uint32_t* pTypedSource = reinterpret_cast<const uint32_t*>( pSource );
                pTypedResult[channel] = (int32_t)pTypedSource[channel];
				break;
			}

			case MBF_INT32:
			{
                const int32_t* pTypedSource = reinterpret_cast<const int32_t*>( pSource );
                pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_NUINT8:
			case MBF_NUINT16:
			case MBF_NUINT32:
			case MBF_NINT8:
			case MBF_NINT16:
			case MBF_NINT32:
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		//-----------------------------------------------------------------------------------------
		case MBF_NUINT8:
		{
            uint8_t* pTypedResult = reinterpret_cast<uint8_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_NUINT8:
			{
				auto pTypedSource = reinterpret_cast<const uint8_t*>(pSource);
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>(pSource);
				pTypedResult[channel] = (uint8_t)
					std::min<uint32_t>(
						0xFF,
						std::max<uint32_t>(
							0,
							(uint32_t)(((float)0xFF)*pTypedSource[channel] + 0.5f)
							)
						);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (uint8_t)
                        std::min<uint32_t>(
							0xFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(((float)0xFF)*halfToFloat(pTypedSource[channel])+0.5f)
								)
							);
				break;
			}

			case MBF_UINT8:
			case MBF_UINT16:
			case MBF_UINT32:
			case MBF_INT8:
			case MBF_INT16:
			case MBF_INT32:
				// TODO: 1 or 0
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_NUINT16:
		{
            uint16_t* pTypedResult = reinterpret_cast<uint16_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_NUINT16:
			{
				auto pTypedSource = reinterpret_cast<const uint16_t*>(pSource);
				pTypedResult[channel] = pTypedSource[channel];
				break;
			}

			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (uint16_t)
                        std::min<uint32_t>(
							0xFFFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(((float)0xFFFF)*pTypedSource[channel]+0.5f)
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (uint16_t)
                        std::min<uint32_t>(
							0xFFFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(((float)0xFFFF)*halfToFloat(pTypedSource[channel])+0.5f)
								)
							);
				break;
			}

			case MBF_UINT8:
			case MBF_UINT16:
			case MBF_UINT32:
			case MBF_INT8:
			case MBF_INT16:
			case MBF_INT32:
				// TODO: 1 or 0
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_NUINT32:
		{
            uint32_t* pTypedResult = reinterpret_cast<uint32_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (uint32_t)
                        std::min<uint32_t>(
							0xFFFFFFFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(((float)0xFFFFFFFF)*pTypedSource[channel])
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (uint32_t)
                        std::min<uint32_t>(
							0xFFFFFFFF,
                            std::max<uint32_t>(
								0,
                                (uint32_t)(((float)0xFFFFFFFF)*halfToFloat(pTypedSource[channel])+0.5f)
								)
							);
				break;
			}

			case MBF_UINT8:
			case MBF_UINT16:
			case MBF_UINT32:
			case MBF_INT8:
			case MBF_INT16:
			case MBF_INT32:
				// TODO: 1 or 0
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_NINT8:
		{
            int8_t* pTypedResult = reinterpret_cast<int8_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (int8_t)
                        std::min<int32_t>(
							127,
                            std::max<int32_t>(
								-128,
                                (int32_t)(128.0f*pTypedSource[channel]+0.5f)
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (int8_t)
                        std::min<int32_t>(
							127,
                            std::max<int32_t>(
								-128,
                                (int32_t)(128.0f*halfToFloat(pTypedSource[channel])+0.5f)
								)
							);
				break;
			}

			case MBF_UINT8:
			case MBF_UINT16:
			case MBF_UINT32:
			case MBF_INT8:
			case MBF_INT16:
			case MBF_INT32:
				// TODO: -1, 1 or 0
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

		case MBF_NINT16:
		{
            int16_t* pTypedResult = reinterpret_cast<int16_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
							32767,
                            std::max<int32_t>(
								-32768,
                                (int32_t)(32768.0f*pTypedSource[channel]+0.5f)
								)
							);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (int16_t)
                        std::min<int32_t>(
							32767,
                            std::max<int32_t>(
								-32768,
                                (int32_t)(32768.0f*halfToFloat(pTypedSource[channel])+0.5f)
								)
							);
				break;
			}

			case MBF_UINT8:
			case MBF_UINT16:
			case MBF_UINT32:
			case MBF_INT8:
			case MBF_INT16:
			case MBF_INT32:
				// TODO: -1, 1 or 0
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}


		case MBF_NINT32:
		{
            int32_t* pTypedResult = reinterpret_cast<int32_t*>( pResult );

			switch ( sourceFormat )
			{
			case MBF_FLOAT32:
			{
				const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                pTypedResult[channel] = (int32_t)(2147483648.0f*pTypedSource[channel]+0.5f);
				break;
			}

			case MBF_FLOAT16:
			{
				const float16* pTypedSource = reinterpret_cast<const float16*>( pSource );
                pTypedResult[channel] = (int32_t)(2147483648.0f*halfToFloat(pTypedSource[channel])+0.5f);
				break;
			}

			case MBF_UINT8:
			case MBF_UINT16:
			case MBF_UINT32:
			case MBF_INT8:
			case MBF_INT16:
			case MBF_INT32:
				// TODO: -1, 1 or 0
				pTypedResult = 0;
				break;

			default:
				checkf( false, TEXT("Conversion not implemented." ) );
				break;
			}
			break;
		}

        case MBF_PACKEDDIR8:
        case MBF_PACKEDDIR8_W_TANGENTSIGN:
        {
            uint8_t* pTypedResult = reinterpret_cast<uint8_t*>( pResult );

            switch ( sourceFormat )
            {
            case MBF_PACKEDDIR8:
            case MBF_PACKEDDIR8_W_TANGENTSIGN:
            {
                const uint8_t* pTypedSource = reinterpret_cast<const uint8_t*>( pSource );
                uint8_t source = pTypedSource[channel];
                pTypedResult[channel] = source;
                break;
            }

            case MBF_FLOAT32:
            {
                const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                float source = pTypedSource[channel];
                source = (source*0.5f+0.5f)*255.0f;
                pTypedResult[channel] =
                        (uint8_t)std::min<float>( 255.0f, std::max<float>( 0.0f, source ) );
                break;
            }

            default:
                checkf( false, TEXT("Conversion not implemented." ) );
                break;
            }
            break;
        }

        case MBF_PACKEDDIRS8:
        case MBF_PACKEDDIRS8_W_TANGENTSIGN:
        {
            int8_t* pTypedResult = reinterpret_cast<int8_t*>( pResult );

            switch ( sourceFormat )
            {
            case MBF_PACKEDDIRS8:
            case MBF_PACKEDDIRS8_W_TANGENTSIGN:
            {
                const int8_t* pTypedSource = reinterpret_cast<const int8_t*>( pSource );
                int8_t source = pTypedSource[channel];
                pTypedResult[channel] = source;
                break;
            }

            case MBF_FLOAT32:
            {
                const float* pTypedSource = reinterpret_cast<const float*>( pSource );
                float source = pTypedSource[channel];
                source = source*0.5f*255.0f;
                pTypedResult[channel] =
                        (int8_t)std::min<float>( 127.0f, std::max<float>( -128.0f, source ) );
                break;
            }

            default:
                checkf( false, TEXT("Conversion not implemented." ) );
                break;
            }
            break;
        }

		default:
			checkf( false, TEXT("Conversion not implemented." ) );
			break;
		}

	}

}
