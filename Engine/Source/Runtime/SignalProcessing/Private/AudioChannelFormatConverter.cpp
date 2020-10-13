// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/AudioChannelFormatConverter.h"

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/FloatArrayMath.h"
#include "SignalProcessingModule.h"

namespace Audio
{
	const IChannelFormatConverter::FInputFormat& FBaseChannelFormatConverter::GetInputFormat() const
	{
		return InputFormat;
	}

	const IChannelFormatConverter::FOutputFormat& FBaseChannelFormatConverter::GetOutputFormat() const
	{
		return OutputFormat;
	}

	void FBaseChannelFormatConverter::SetOutputGain(float InOutputGain, bool bFadeToGain)
	{
		OutputGainState.bFadeToNextGain = bFadeToGain;

		if (bFadeToGain)
		{
			// If fading, set as the next gain to fade to.
			OutputGainState.NextGain = InOutputGain;
		}
		else
		{
			OutputGainState.Gain = InOutputGain;
		}
	}

	void FBaseChannelFormatConverter::SetMixGain(const FChannelMixEntry& InEntry, bool bFadeToGain)
	{
		SetMixGain(InEntry.InputChannelIndex, InEntry.OutputChannelIndex, InEntry.Gain, bFadeToGain);
	}

	void FBaseChannelFormatConverter::SetMixGain(int32 InInputChannelIndex, int32 InOutputChannelIndex, float InGain, bool bFadeToGain)
	{
		FChannelMixKey Key(InInputChannelIndex, InOutputChannelIndex);

		if ((InInputChannelIndex >= InputFormat.NumChannels) || (InInputChannelIndex < 0))
		{
			UE_LOG(LogSignalProcessing, Warning, TEXT("Skipping mix entry. Input channel (%d) does not exist for input format with %d channels."), InInputChannelIndex, InputFormat.NumChannels);
			return;
		}

		if ((InOutputChannelIndex >= OutputFormat.NumChannels) || (InOutputChannelIndex < 0))
		{
			UE_LOG(LogSignalProcessing, Warning, TEXT("Skipping mix entry. Output channel (%d) does not exist for output format with %d channels."), InOutputChannelIndex, OutputFormat.NumChannels);
			return;
		}

		if ((InGain == 0.f) && !bFadeToGain)
		{
			// Remove a mix state if it has zero gain in order to avoid extra
			// processing of gain entries. 
			ChannelMixStates.Remove(Key);
		}
		else
		{
			FChannelMixState* State = ChannelMixStates.Find(Key);

			if (nullptr == State)
			{
				// No existing gain state for the input/output chnannel pair.
				// One is created here. 
				FChannelMixState& NewState = ChannelMixStates.Add(Key);
				
				NewState.InputChannelIndex = InInputChannelIndex;
				NewState.OutputChannelIndex = InOutputChannelIndex;
				NewState.Gain = 0.f;

				State = &NewState;
			}
			
			check(nullptr != State);

			State->bFadeToNextGain = bFadeToGain;

			if (bFadeToGain)
			{
				// Setup next gain if fading.
				State->NextGain = InGain;
			}
			else
			{
				// Set current gain if not fading.
				State->Gain = InGain;
			}
		}
	}

	float FBaseChannelFormatConverter::GetTargetMixGain(int32 InInputChannelIndex, int32 InOutputChannelIndex) const
	{
		FChannelMixKey Key(InInputChannelIndex, InOutputChannelIndex);

		if (ChannelMixStates.Contains(Key))
		{
			const FChannelMixState& State = ChannelMixStates.FindChecked(Key);

			// Check whether to return next gain or current gain.
			// "NextGain" is meaningless if bFadeToNextGain == false.
			return State.bFadeToNextGain ? State.NextGain : State.Gain;
		}

		return 0.f;
	}

	float FBaseChannelFormatConverter::GetTargetOutputGain() const
	{
		// Check whether to return next gain or current gain.
		// "NextGain" is meaningless if bFadeToNextGain == false.
		return OutputGainState.bFadeToNextGain ? OutputGainState.NextGain : OutputGainState.Gain;
	}

	void FBaseChannelFormatConverter::ProcessAudio(const TArray<AlignedFloatBuffer>& InInputBuffers, TArray<AlignedFloatBuffer>& OutOutputBuffers)
	{
		using FMixElement = TSortedMap<FChannelMixKey, FChannelMixState>::ElementType;

		check(InInputBuffers.Num() == InputFormat.NumChannels);

		// Ensure output buffers exist in output array.
		while (OutOutputBuffers.Num() < OutputFormat.NumChannels)
		{
			OutOutputBuffers.Emplace();
		}

		for (int32 i = 0; i < OutputFormat.NumChannels; i++)
		{
			// Allocate output buffers to the correct size. 
			OutOutputBuffers[i].Reset();
			OutOutputBuffers[i].AddUninitialized(NumFramesPerCall);

			// Zero out data in output buffers. 
			FMemory::Memset(OutOutputBuffers[i].GetData(), 0, sizeof(float) * NumFramesPerCall);
		}

		// Cache current input buffer information to minimize duplicate processing
		// and improve cache performance.  MixEntries are sorted by input buffer.
		// All mix entries for a single input index will be processed sequentially
		// before the next input index is processed. The `CurrentInputChannelIndex`
		// is used to determine whether the input channel in a mix entry differs
		// from the previous. If so, the "Current" input channel is updated.
		int32 CurrentInputChannelIndex = INDEX_NONE;
		const AlignedFloatBuffer* CurrentInputChannel = nullptr;

		TArray<FChannelMixKey> EntriesToRemove;

		// Process all mix entries.
		for (FMixElement& Element : ChannelMixStates)
		{
			FChannelMixState& MixState = Element.Value;

			// Check if the input index differs from the previous. If so, update
			// "Current" input index.
			if (MixState.InputChannelIndex != CurrentInputChannelIndex)
			{
				// Check validity of input channel.
				const AlignedFloatBuffer& InputBuffer = InInputBuffers[MixState.InputChannelIndex];

				if (ensure(InputBuffer.Num() == NumFramesPerCall))
				{
					CurrentInputChannelIndex = MixState.InputChannelIndex;
					CurrentInputChannel = &InputBuffer;
				}
				else
				{
					UE_LOG(LogSignalProcessing, Warning, TEXT("Input buffer frame count (%d) does not match expected frame count (%d)"), InputBuffer.Num(), NumFramesPerCall);

					CurrentInputChannelIndex = INDEX_NONE;
					CurrentInputChannel = nullptr;

					continue;
				}
			}

			check(nullptr != CurrentInputChannel);

			// Get gain values for mix entry. Include the output gain.
			const float InitialGain = MixState.Gain * OutputGainState.Gain;
			const float FinalMixGain = MixState.bFadeToNextGain ? MixState.NextGain : MixState.Gain;
			const float FinalOutputGain = OutputGainState.bFadeToNextGain ? OutputGainState.NextGain : OutputGainState.Gain;
			const float FinalGain = FinalMixGain * FinalOutputGain;

			if (FMath::IsNearlyEqual(InitialGain, FinalGain, 0.000001f))
			{
				// No fade is needed because gain is constant. 
				ArrayMultiplyAddInPlace(*CurrentInputChannel, FinalGain, OutOutputBuffers[MixState.OutputChannelIndex]);
			}
			else 
			{
				// Fade required because gain changes. 
				ArrayLerpAddInPlace(*CurrentInputChannel, InitialGain, FinalGain, OutOutputBuffers[MixState.OutputChannelIndex]);
			}

			if (MixState.bFadeToNextGain)
			{
				// Update gain state if fade has been processed. 
				MixState.bFadeToNextGain = false;
				MixState.Gain = MixState.NextGain;

				if (0.f == MixState.Gain)
				{
					// Remove mix states with zero gain.
					EntriesToRemove.Emplace(MixState);
				}
			}
		}

		if (OutputGainState.bFadeToNextGain)
		{
			// Update gain state if fade has been processed. 
			OutputGainState.Gain = OutputGainState.NextGain;
			OutputGainState.bFadeToNextGain = false;
		}

		for (const FChannelMixKey& Key : EntriesToRemove)
		{
			// Remove mix states with zero gain.
			ChannelMixStates.Remove(Key);
		}
	}

	TUniquePtr<FBaseChannelFormatConverter> FBaseChannelFormatConverter::CreateBaseFormatConverter(const FInputFormat& InInputFormat, const FOutputFormat& InOutputFormat, TArrayView<const FChannelMixEntry> InMixEntries, int32 InNumFramesPerCall)
	{
		if (InInputFormat.NumChannels < 1)
		{
			UE_LOG(LogSignalProcessing, Error, TEXT("Invalid input format channel count (%d). Must be greater than zero"), InInputFormat.NumChannels);
			return TUniquePtr<FBaseChannelFormatConverter>(nullptr);
		}

		if (InOutputFormat.NumChannels < 1) 
		{
			UE_LOG(LogSignalProcessing, Error, TEXT("Invalid output format channel count (%d). Must be greater than zero"), InOutputFormat.NumChannels);
			return TUniquePtr<FBaseChannelFormatConverter>(nullptr);
		}

		if (InNumFramesPerCall < 1)
		{
			UE_LOG(LogSignalProcessing, Error, TEXT("Invalid num frames per call (%d). Must be greater than zero"), InNumFramesPerCall);
			return TUniquePtr<FBaseChannelFormatConverter>(nullptr);
		}

		return TUniquePtr<FBaseChannelFormatConverter>(new FBaseChannelFormatConverter(InInputFormat, InOutputFormat, InMixEntries, InNumFramesPerCall));
	}

	FBaseChannelFormatConverter::FBaseChannelFormatConverter(const FInputFormat& InInputFormat, const FOutputFormat& InOutputFormat, TArrayView<const FChannelMixEntry> InMixEntries, int32 InNumFramesPerCall)
	:	InputFormat(InInputFormat)
	,	OutputFormat(InOutputFormat)
	,	NumFramesPerCall(InNumFramesPerCall)
	{
		check(InputFormat.NumChannels > 0);
		check(OutputFormat.NumChannels > 0);
		check(InNumFramesPerCall > 0);

		const bool bFadeToGain = false;

		for (const FChannelMixEntry& Entry : InMixEntries)
		{
			SetMixGain(Entry, bFadeToGain);
		}
	}


	void FSimpleUpmixer::SetRearChannelBleed(float InGain, bool bFadeToGain)
	{
		for (int32 InputChannelIndex : FrontChannelIndices)
		{
			TArray<int32> RearChannelIndices;	
			GetPairedRearChannelIndices(InputChannelIndex, RearChannelIndices);

			for (int32 RearChannelIndex : RearChannelIndices)
			{
				SetMixGain(InputChannelIndex, RearChannelIndex, InGain, bFadeToGain);
			}
		}

		// Output gain needs to be updated so things don't get too loud.
		UpdateOutputGain(bFadeToGain);
	}

	void FSimpleUpmixer::SetRearChannelFlip(bool bInDoRearChannelFlip, bool bFadeFlip)
	{
		// Only process on change in value. 
		if (bDoRearChannelFlip != bInDoRearChannelFlip)
		{
			// Setting the rear channel flip will update the `GetPairedRearChannelIndices` 
			// results.  Cache the existing rear channel gains so they can be used after 
			// the rear channel swap.

			TArray<TArray<float>> ExistingGains;

			for (int32 InputChannelIndex : FrontChannelIndices)
			{
				TArray<int32> RearChannelIndices; 

				// Get rear channel indices before toggling `bDoRearChannelFlip`
				GetPairedRearChannelIndices(InputChannelIndex, RearChannelIndices);

				TArray<float> Gains;

				for (int32 RearChannelIndex : RearChannelIndices)
				{
					// Cache existing gain.
					Gains.Add(GetTargetMixGain(InputChannelIndex, RearChannelIndex));

					// Clear out the existing gain.
					SetMixGain(InputChannelIndex, RearChannelIndex, 0.f, bFadeFlip);
				}

				ExistingGains.Add(Gains);
			}

			// Toggling bDoRearChannelFlip will alter the behavior of `GetPairedRearChannelIndices`.
			bDoRearChannelFlip = bInDoRearChannelFlip;

			for (int32 i = 0; i < ExistingGains.Num(); i++)
			{
				TArray<int32> RearChannelIndices; 
				int32 InputChannelIndex = FrontChannelIndices[i];

				// Get rear channel indices after toggling `bDoRearChannelFlip`
				GetPairedRearChannelIndices(InputChannelIndex, RearChannelIndices);

				for (int32 j = 0; j < RearChannelIndices.Num(); j++)
				{
					int32 RearChannelIndex = RearChannelIndices[j];
					float RearChannelGain = ExistingGains[i][j];

					// Set new mix gain after toggling bDoRearChannelFlip
					SetMixGain(InputChannelIndex, RearChannelIndex, RearChannelGain, bFadeFlip);
				}
			}

			// Output gain changes to account for correlation between signals
			// when downmixed. 
			UpdateOutputGain(bFadeFlip);
		}
	}

	bool FSimpleUpmixer::GetRearChannelFlip() const
	{
		return bDoRearChannelFlip;
	}


	bool FSimpleUpmixer::GetSimpleUpmixerStaticMixEntries(const FInputFormat& InInputFormat, const FOutputFormat& InOutputFormat, TArray<FChannelMixEntry>& OutEntries)
	{
		OutEntries.Reset();

		if ((1 == InInputFormat.NumChannels) && (InOutputFormat.NumChannels >= 2))
		{
			// If there is mono input and at least 2 output channels, upmix
			// to stereo. 
			FChannelMixEntry& MonoToFrontLeft = OutEntries.AddDefaulted_GetRef();

			MonoToFrontLeft.InputChannelIndex = 0;
			MonoToFrontLeft.OutputChannelIndex = 0;
			MonoToFrontLeft.Gain = 0.707f; // Equal power

			FChannelMixEntry& MonoToFrontRight = OutEntries.AddDefaulted_GetRef();

			MonoToFrontRight.InputChannelIndex = 0;
			MonoToFrontRight.OutputChannelIndex = 1;
			MonoToFrontRight.Gain = 0.707f; // Equal power
		}
		else
		{
			int32 NumChannels = FMath::Min(InInputFormat.NumChannels, InOutputFormat.NumChannels);

			for (int32 i = 0; i < NumChannels; i++)
			{
				FChannelMixEntry& Entry = OutEntries.AddDefaulted_GetRef();

				Entry.InputChannelIndex = i;
				Entry.OutputChannelIndex = i;
				Entry.Gain = 1.f;
			}
		}

		return true;
	}

	TUniquePtr<FSimpleUpmixer> FSimpleUpmixer::CreateSimpleUpmixer(const FInputFormat& InInputFormat, const FOutputFormat& InOutputFormat, int32 InNumFramesPerCall)
	{
		if (InInputFormat.NumChannels < 1)
		{
			UE_LOG(LogSignalProcessing, Error, TEXT("Invalid input format channel count (%d). Must be greater than zero"), InInputFormat.NumChannels);
			return TUniquePtr<FSimpleUpmixer>(nullptr);
		}

		if (InOutputFormat.NumChannels < 1) 
		{
			UE_LOG(LogSignalProcessing, Error, TEXT("Invalid output format channel count (%d). Must be greater than zero"), InOutputFormat.NumChannels);
			return TUniquePtr<FSimpleUpmixer>(nullptr);
		}

		if (InNumFramesPerCall < 1)
		{
			UE_LOG(LogSignalProcessing, Error, TEXT("Invalid num frames per call (%d). Must be greater than zero"), InNumFramesPerCall);
			return TUniquePtr<FSimpleUpmixer>(nullptr);
		}

		TArray<FChannelMixEntry> ChannelMixEntries;

		bool bSuccess = GetSimpleUpmixerStaticMixEntries(InInputFormat, InOutputFormat, ChannelMixEntries);

		if (!bSuccess)
		{
			return TUniquePtr<FSimpleUpmixer>(nullptr);
		}

		return TUniquePtr<FSimpleUpmixer>(new FSimpleUpmixer(InInputFormat, InOutputFormat, ChannelMixEntries, InNumFramesPerCall));
	}

	FSimpleUpmixer::FSimpleUpmixer(const FInputFormat& InInputFormat, const FOutputFormat& InOutputFormat, TArrayView<const FChannelMixEntry> InMixEntries, int32 InNumFramesPerCall)
	:	FBaseChannelFormatConverter(InInputFormat, InOutputFormat, InMixEntries, InNumFramesPerCall)
	{
		// Cache front channel indices. These do not change for lifetime of FSimpleUpmixer.
		GetFrontChannelIndices(FrontChannelIndices);
	}

	void FSimpleUpmixer::UpdateOutputGain(bool bFadeToGain)
	{
		// Update output gain to keep overall loudness constant if later mixed down.

		int32 NumRearChannelBleed = 0;
		float SumRearChannelBleed = 0.f;

		for (int32 InputChannelIndex : FrontChannelIndices)
		{
			TArray<int32> RearChannelIndices;	
			GetPairedRearChannelIndices(InputChannelIndex, RearChannelIndices);

			for (int32 RearChannelIndex : RearChannelIndices)
			{
				float MixGain = GetTargetMixGain(InputChannelIndex, RearChannelIndex);
				if (bDoRearChannelFlip)
				{
					// Assume channels are uncorrelated. There shouldn't be 
					// any phase cancellation so take absolute value of gain. It
					// does not matter if the rear channel's phase is flipped.
					SumRearChannelBleed += FMath::Abs(MixGain);
				}
				else
				{
					// Channels are correlated, if mix gain is negative it will
					// cancel out existing signal when mixed down. Do not take
					// absolute value of gain.
					SumRearChannelBleed += MixGain;
				}

				NumRearChannelBleed++;
			}
		}

		if (0 == NumRearChannelBleed)
		{
			SetOutputGain(1.f, bFadeToGain);
		}
		else
		{
			float AverageRearGain = SumRearChannelBleed / NumRearChannelBleed;
			float OutputGain = 1.f;

			if (bDoRearChannelFlip)
			{
				// If rear channel flip, can assume that signals are uncorrelated if they are mixed back down,
				// so use equal power normalization.
				OutputGain = 1.f / FMath::Sqrt(1.f + (AverageRearGain * AverageRearGain));
			}
			else
			{
				// Rear channels not flipped, so assume signals are correlated if they are mixed back down,
				// so use equal amplitude normalization. 
				OutputGain = 1.f / FMath::Max(1.f, (1.f + AverageRearGain));
			}

			SetOutputGain(OutputGain, bFadeToGain);
		}
	}

	void FSimpleUpmixer::GetFrontChannelIndices(TArray<int32>& OutFrontChannelIndices) const
	{
		OutFrontChannelIndices.Reset();

		const int32 EndInputChannelIndex = FMath::Min(2, GetInputFormat().NumChannels);

		for (int32 i = 0; i < EndInputChannelIndex; i++)
		{
			OutFrontChannelIndices.Add(i);
		}
	}

	void FSimpleUpmixer::GetPairedRearChannelIndices(int32 InInputChannelIndex, TArray<int32>& OutRearChannelIndices) const
	{
		OutRearChannelIndices.Reset();

		if ((InInputChannelIndex >= 2) || (InInputChannelIndex < 0))
		{
			// Only front channels have paired rear channels.
			return;
		}

		int32 NumOutputChannels = GetOutputFormat().NumChannels;

		const bool bIsOutputSurround = NumOutputChannels >= 4;

		if (!bIsOutputSurround)
		{
			// output must be surround sound to have paired rear channels.
			return;
		}

		static const int32 FrontLeftIndex = 0;
		static const int32 FrontRightIndex = 1;

		const int32 RearLeftIndex = NumOutputChannels - 2;
		const int32 RearRightIndex = NumOutputChannels - 1;

		if (GetInputFormat().NumChannels == 1)
		{
			// Special case for mono.
			OutRearChannelIndices.Append({RearLeftIndex, RearRightIndex});
			return;
		}

		if (bDoRearChannelFlip)
		{
			// Rear channels are flipped
			switch (InInputChannelIndex)
			{
				case FrontLeftIndex:

					OutRearChannelIndices.Add(RearRightIndex);
					break;

				case FrontRightIndex:

					OutRearChannelIndices.Add(RearLeftIndex);
					break;

				default:

					checkNoEntry();
					break;
			}
		}
		else
		{
			// Rear channels are _not_ flipped. 
			switch (InInputChannelIndex)
			{
				case FrontLeftIndex:

					OutRearChannelIndices.Add(RearLeftIndex);
					break;

				case FrontRightIndex:

					OutRearChannelIndices.Add(RearRightIndex);
					break;

				default:

					checkNoEntry();
					break;
			}
		}
	}
}
