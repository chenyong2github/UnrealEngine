// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "MetasoundGraphCoreModule.h"
#include "MetasoundDataReference.h"

/** Define which determines whether to check that the size of the audio buffer has not changed since initialization */
#define METASOUNDGRAPHCORE_CHECKAUDIONUM !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

namespace Metasound 
{
	/**  FAudioBuffer
	 *
	 * FAudioBuffer is the default buffer for passing audio data between nodes.  It should not be resized. 
	 * It is compatible with functions accepting const references to AlignedFloatBuffer (const AlignedFloatBuffer&) 
	 * arguments via an implicit conversion operator which exposes the underlying AlignedFloatBuffer container.
	 *
	 */
	class METASOUNDFRONTEND_API FAudioBuffer
	{
		public:
			/** Create an FAudioBuffer with a specific number of samples. 
			 *
			 * @param InNumSamples - Number of samples in buffer.
			 */
			explicit FAudioBuffer(int32 InNumSamples)
			{
				Buffer.AddUninitialized(InNumSamples);

#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				// InitialNum should not change during the life of an FAudioBuffer
				InitialNum = Buffer.Num();
#endif				
			}

			/**
			 * This is the constructor used by the frontend.
			 */
			FAudioBuffer(const FOperatorSettings& InSettings)
			{
				Buffer.AddUninitialized(InSettings.GetNumFramesPerBlock());

#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				// InitialNum should not change during the life of an FAudioBuffer
				InitialNum = Buffer.Num();
#endif	
			}

			FAudioBuffer()
			{
#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				// InitialNum should not change during the life of an FAudioBuffer
				InitialNum = Buffer.Num();
#endif				
			}

			/** Return a pointer to the audio float data. */
			FORCEINLINE const float* GetData() const
			{
#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				UE_CLOG(InitialNum != Buffer.Num(), LogMetasoundGraphCore, Error, TEXT("Metasound audio buffer size change detected.  Audio Buffers should not be resized."));
#endif				
				return Buffer.GetData();
			}

			/** Return a pointer to the audio float data. */
			FORCEINLINE float* GetData()
			{
#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				UE_CLOG(InitialNum != Buffer.Num(), LogMetasoundGraphCore, Error, TEXT("Metasound audio buffer size change detected.  Audio Buffers should not be resized."));
#endif				
				return Buffer.GetData();
			}

			/** Return the number of samples in the audio buffer. */
			FORCEINLINE int32 Num() const
			{
#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				UE_CLOG(InitialNum != Buffer.Num(), LogMetasoundGraphCore, Error, TEXT("Metasound audio buffer size change detected.  Audio Buffers should not be resized."));
#endif				
				return Buffer.Num();
			}

			/** Implicit conversion to Audio::AlignedFloatBuffer */
			FORCEINLINE operator const Audio::AlignedFloatBuffer& () const
			{
#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				UE_CLOG(InitialNum != Buffer.Num(), LogMetasoundGraphCore, Error, TEXT("Metasound audio buffer size change detected.  Audio Buffers should not be resized."));
#endif				
				return Buffer;
			}

			/** Implicit conversion to Audio::AlignedFloatBuffer 
			 *
			 * WARNING: if the buffer is resized, it will cause errors. 
			 */
			FORCEINLINE operator Audio::AlignedFloatBuffer& ()
			{
#if METASOUNDGRAPHCORE_CHECKAUDIONUM
				UE_CLOG(InitialNum != Buffer.Num(), LogMetasoundGraphCore, Error, TEXT("Metasound audio buffer size change detected.  Audio Buffers should not be resized."));
#endif				
				return Buffer;
			}

			FORCEINLINE void Zero()
			{
				if (Buffer.Num() > 0)
				{
					FMemory::Memset(Buffer.GetData(), 0, sizeof(float) * Buffer.Num());
				}
			}

		private:

			Audio::AlignedFloatBuffer Buffer;

#if METASOUNDGRAPHCORE_CHECKAUDIONUM
			int32 InitialNum;
#endif				
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FAudioBuffer, METASOUNDFRONTEND_API, FAudioBufferTypeInfo, FAudioBufferReadRef, FAudioBufferWriteRef);
};
