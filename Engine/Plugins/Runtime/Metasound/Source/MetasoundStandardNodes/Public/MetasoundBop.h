// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundOperatorSettings.h"

namespace Metasound
{
	/** FBop supports sample accurate triggering, sample accurate internal tracking,
	 * and a conveient interface for running bop-aligned audio signal processing
	 * routines on buffers.. 
	 *
	 * FBops are triggered using FBop::BopTime or FBop::BopFrame.
	 * FBops track time internally by calling FBop::Advance.
	 * Executing audio signal processing on buffers can be performed by calling
	 * FBop::ExecuteBlock or FBop::LookAhead.
	 */
	class METASOUNDSTANDARDNODES_API FBop
	{
		public:
			/** FBop constructor. 
			 *
			 * @param bShouldBop - If true, bops first sample.
			 * @param InSettings - Operator settings.
			 */
			explicit FBop(bool bShouldBop, const FOperatorSettings& InSettings);

			/** FBop constructor. 
			 *
			 * @param InFrameToBop - Set specific frame to bop.
			 * @param InSettings - Operator settings.
			 */
			explicit FBop(int32 InFrameToBop, const FOperatorSettings& InSettings);

			/** FBop constructor. By default it is not bopped.
			 *
			 * @param InSettings - Operator settings.
			 */
			FBop(const FOperatorSettings& InSettings);

			/** Bop a specific frame in the future.
			 *
			 * @param InFrameToBop - Index of frame to bop.
			 */
			void BopFrame(int32 InFrameToBop);

			/** Bop a specific time in the future.
			 *
			 * @param InTime - Time to set bop.
			 */
			template<typename TimeType>
			void BopTime(const TimeType& InTime)
			{
				BopFrame(InTime.GetNumSamples(SampleRate));
			}

			/** Advance internal frame counters by block size. */
			void AdvanceBlock();

			/** Advance internal frame counters by specific frame count. */
			void Advance(int32 InNumFrames);

			/** Number of bopped frames. */
			int32 Num() const;

			/** Returns frame index for a given bop index. 
			 *
			 * @param InIndex - Index of bop. Must be a value between 0 and Num().
			 *
			 * @return The frame of the bop.
			 */
			int32 operator[](int32 InBopIndex) const;

			/** Returns true if there are any bopped frames. */
			bool IsBopped() const;

			/** Returns true there is a bop in the current block of audio. */
			bool IsBoppedInBlock() const;

			/** Implicit conversion of FBop into bool by calling IsBoppedInBlock()
			 *
			 * @return Returns true if frame is bopped in current block.
			 */
			operator bool() const;

			/** Removes all bopped frames. */
			void Reset();

			/** Executes one block of frames and calls underlying InPreBop and InOnBop
			 * functions with frame indices.
			 *
			 * @param InPreBop - A function which handles frames before the first 
			 *                   bop in the current block. The function must accept 
			 *                   the arguments (int32 StartFrame, int32 EndFrame).
			 * @param InOnBop - A function which handles frames starting with the 
			 *                  bops index and ending the next bop index or the
			 *                  number of frames in a block.. The function must
			 *                  accept the arguments (int32 StartFrame, int32 EndFrame).
			 */
			template<typename PreBopType, typename OnBopType>
			void ExecuteBlock(PreBopType InPreBop, OnBopType InOnBop) const
			{
				ExecuteFrames(NumFramesPerBlock, LastBopIndexInBlock, InPreBop, InOnBop);
			}

			/** Executes a desired number of frames and calls underlying InPreBop 
			 * and InOnBop functions with frame indices.
			 *
			 * @param InNumFrames - The number of frames to look ahead.
			 * @param InPreBop - A function which handles frames before the first 
			 *                   bop in the range of frames. The function must accept 
			 *                   the arguments (int32 StartFrame, int32 EndFrame).
			 * @param InOnBop - A function which handles frames starting with the 
			 *                  bops index and ending the next bop index or InNumFrames
			 *             		The function must accept the arguments 
			 *             		(int32 StartFrame, int32 EndFrame).
			 */
			template<typename PreBopType, typename OnBopType>
			void LookAhead(int32 InNumFrames, PreBopType InPreBop, OnBopType InOnBop) const
			{
				if (InNumFrames > 0)
				{
					int32 LastBopIndexInLookAhead= Algo::LowerBound(BoppedFrames, InNumFrames);
					
					ExecuteFrames(InNumFrames, LastBopIndexInLookAhead, InPreBop, InOnBop);
				}
			}

		private:
			void UpdateLastBopIndexInBlock();

			template<typename PreBopType, typename OnBopType>
			void ExecuteFrames(int32 InNumFrames, int32 InLastBopIndex, PreBopType InPreBop, OnBopType InOnBop) const
			{
				if (InLastBopIndex <= 0)
				{
					InPreBop(0, InNumFrames);
					return;
				}

				const int32* BoppedFramesData = BoppedFrames.GetData();

				if (BoppedFramesData[0] > 0)
				{
					InPreBop(0, BoppedFramesData[0]);
				}

				const int32 NextToLastBopIndex = InLastBopIndex - 1;

				for (int32 i = 0; i < NextToLastBopIndex; i++)
				{
					InOnBop(BoppedFramesData[i], BoppedFramesData[i + 1]);
				}

				InOnBop(BoppedFramesData[NextToLastBopIndex], InNumFrames);
			}


			TArray<int32> BoppedFrames;

			bool bHasBop = false;
			int32 NumFramesPerBlock = 0;
			float SampleRate;
			int32 LastBopIndexInBlock = INDEX_NONE;
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FBop, METASOUNDSTANDARDNODES_API, FBopTypeInfo, FBopReadRef, FBopWriteRef);
}
