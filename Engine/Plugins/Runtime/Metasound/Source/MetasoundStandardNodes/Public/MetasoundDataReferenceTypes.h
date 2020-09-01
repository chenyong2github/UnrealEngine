// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundDataTypeRegistrationMacro.h"

namespace Metasound
{
	class FBop
	{
		public:
			// Constructor that, if called with true, bops the beginning of the callback.
			FBop(bool bShouldBop)
			{
				if (bShouldBop)
				{
					BopFrame(0);
				}
			}

			// Constructor that bops a specific audio frame within the callback.
			FBop(uint32 InFrameToBop)
			{
				BopFrame(InFrameToBop);
			}

			// The default Bop Constructor 
			FBop()
				: FBop(false)
			{}

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

			bool IsBopped() const
			{
				return Frames.Num() > 0;
			}

			operator bool() const 
			{
				return IsBopped();
			}

			void Reset()
			{
				Frames.Reset();
			}

		private:
			TArray<uint32> Frames;
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FBop, METASOUNDSTANDARDNODES_API, FBopTypeInfo, FBopReadRef, FBopWriteRef);
}
