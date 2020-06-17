// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundAudioBuffer.h"

namespace Metasound
{
	class FBop
	{
		public:
			void BopFrame(uint32 InFrameToBop)
			{
				// TODO: need to maintain a sorted array. 
				// Can likely implement a faster version.
				bool bNeedsSort = false;

				if (Frames.Num() > 0)
				{
					bNeedsSort = InFrameToBop < Frames.Last();
				}

				Frames.Add(InFrameToBop);
				Frames.Sort();
			}

			void Advance(uint32 InNumFrames)
			{
				Frames.RemoveAllSwap([InNumFrames](uint32 Frame) { return Frame < InNumFrames; }, false /* bAllowShrinking */);

				for (uint32& Frame : Frames)
				{
					Frame -= InNumFrames;
				}
			}

			int32 Num() const
			{
				return Frames.Num();
			}

			uint32 operator[](int32 Index) const
			{
				return Frames[Index];
			}

			operator bool() const 
			{
				return Frames.Num() >= 0;
			}

			void Reset()
			{
				Frames.Reset();
			}

		private:
			TArray<uint32> Frames;
	};

	struct FTimeSeconds
	{
		FTimeSeconds()
		:	Seconds(0.f)
		{
		}

		FTimeSeconds(float InSeconds)
		:	Seconds(InSeconds)
		{
		}

		float Seconds;
	};

	struct FTimeMilliseconds
	{
		FTimeMilliseconds()
		:	Milliseconds(0.f)
		{
		}

		FTimeMilliseconds(float InMilliseconds)
		:	Milliseconds(InMilliseconds)
		{
		}

		float Milliseconds;
	};

	typedef float FFloat;

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FBop, "Bop", 2, FBopTypeInfo, FBopReadRef, FBopWriteRef);
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FTimeSeconds, "Time:Seconds", 3, FTimeSecondsTypeInfo, FTimeSecondsReadRef, FTimeSecondsWriteRef);
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FTimeMilliseconds, "Time:Milliseconds", 4, FTimeMillisecondsTypeInfo, FTimeMillisecondsReadRef, FTimeMillisecondsWriteRef);
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FFloat, "Primitive:Float", 5, FFloatTypeInfo, FFloatReadRef, FFloatWriteRef);
}
