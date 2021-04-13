// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/InterpolatedLinearPitchShifter.h"

namespace Audio
{
	void FLinearPitchShifter::Reset(int32 InNumChannels, float InInitialPitchShiftSemitones, int32 InInterpLengthFrames)
	{
		InterpLengthFrames = InInterpLengthFrames;
		InterpFramesRemaining = 0;
		PitchShiftRatio.SetValue(FMath::Pow(2.0f, InInitialPitchShiftSemitones / 12.0f), 0);
		NumChannels = InNumChannels;

		PreviousFrame.Reset();
		PreviousFrame.AddZeroed(NumChannels);

		bInterpolateBetweenBuffers = false;
	}

	int32 FLinearPitchShifter::ProcessAudio(const TArrayView<float> InputBuffer, Audio::TCircularAudioBuffer<float>& OutputBuffer)
	{
		const int32 NumInputFrames = InputBuffer.Num() / NumChannels;
		int32 OutputFramesRendered = 0; // return value

		// NOTE: CurrentIndex is a float in the range (-1.0f, (float)(NumInputFrames - 1.0f))
		// the fractional portion of CurrentIndex is used to interpolate between Floor(CurrentIndex) and Ceil(CurrentIndex)
		// if CurrentIndex < -1.0f then we missed an interpolation that should have occured in the previous buffer
		check(CurrentIndex > -1.0f);


		// Handle interpolations between index -1.0f and 0.0f
		// i.e. interpolating between our last buffer and the current buffer
		if (bInterpolateBetweenBuffers)
		{
			// Alpha is between (0.0 and 1.0]
			// 0.0 -> Final frame of the previous Input buffer
			// 1.0 -> 0th frame of the current Input buffer
			float Alpha = CurrentIndex + 1.0f;

			// Once Alpha reaches 1.x, we are on frame 0.x of the current buffer
			while (Alpha < 1.0f)
			{
				for (int32 Chan = 0; Chan < NumChannels; ++Chan)
				{
					const float A = PreviousFrame[Chan];
					const float B = InputBuffer[Chan];
					OutputBuffer.Push(FMath::Lerp(A, B, Alpha));
				}

				Alpha += GetNextIndexDelta();
				++OutputFramesRendered;
			}

			// Alpha == 1.0 means CurrentIndex == 0.0
			CurrentIndex = Alpha - 1.0f;
			bInterpolateBetweenBuffers = false;
		}

		// Early Exit: copy full input buffer if no work needs to be done
		// (i.e., not interpolating and pitch shift ratio is 1.0f)
		if (!InterpFramesRemaining && FMath::IsNearlyEqual(1.0f, PitchShiftRatio.GetTarget()))
		{
			OutputBuffer.Push(InputBuffer.GetData(), NumInputFrames * NumChannels);
			return (NumInputFrames + OutputFramesRendered);
		}

		// Normal case: linear interpolation across the input buffer
		const int32 LastFrame = NumInputFrames - 1;
		while (FMath::CeilToInt(CurrentIndex) < LastFrame)
		{
			const int32 i = NumChannels * (int32)FMath::Floor(CurrentIndex);
			float const* A = InputBuffer.GetData() + i;
			float const* B = A + NumChannels;
			const float Alpha = FMath::Frac(CurrentIndex);

			for (int32 Chan = 0; Chan < NumChannels; ++Chan)
			{
				OutputBuffer.Push(FMath::Lerp(*A++, *B++, Alpha));
			}

			++OutputFramesRendered;
			CurrentIndex += GetNextIndexDelta();
		}

		// wrap our fractional index by the buffer size
		CurrentIndex -= (float)(LastFrame);

		// If -1 < CurrentIndex < 0.f, we are going to need to interpolate between
		// the final frame of this buffer and the 0th frame of the next buffer.
		// So we cache the final frame and raise a flag to use on the next call
		if (CurrentIndex < 0.0f)
		{
			const int32 FinalFrameIndex = (NumInputFrames - 1) * NumChannels;

			PreviousFrame.Reset(NumChannels);
			for (int32 Chan = 0; Chan < NumChannels; ++Chan)
			{
				PreviousFrame.Add(InputBuffer[FinalFrameIndex + Chan]);
			}

			bInterpolateBetweenBuffers = true;
		}

		return OutputFramesRendered;
	}

	void FLinearPitchShifter::UpdatePitchShift(float InNewPitchSemitones)
	{
		InterpFramesRemaining = InterpLengthFrames;
		PitchShiftRatio.SetValue(FMath::Pow(2.0f, InNewPitchSemitones / 12.0f), InterpLengthFrames);

		if (FMath::IsNearlyEqual(PitchShiftRatio.GetValue(), PitchShiftRatio.GetTarget()))
		{
			InterpFramesRemaining = 0; // already at target
		}
	}

	float FLinearPitchShifter::GetNextIndexDelta()
	{
		if (InterpFramesRemaining)
		{
			--InterpFramesRemaining;
			return PitchShiftRatio.Update();
		}

		return PitchShiftRatio.GetTarget();
	}

} // namespace Audio