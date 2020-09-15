// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/Dsp.h"
#include "MetasoundBop.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundPrimitives.h"
#include "Sound/SoundGenerator.h"

namespace Metasound
{
	struct FMetasoundGeneratorInitParams
	{
		TUniquePtr<Metasound::IOperator> GraphOperator;
		FAudioBufferReadRef GraphOutputAudioRef;
		FBopReadRef BopOnFinishRef;
	};

	/** FMetasoundGenerator generates audio from a given metasound IOperator
	 * which produces a multichannel audio output.
	 */
	class METASOUNDGENERATOR_API FMetasoundGenerator : public ISoundGenerator
	{
	public:
		using FOperatorUniquePtr = TUniquePtr<Metasound::IOperator>;
		using FAudioBufferReadRef = Metasound::FAudioBufferReadRef;

		/** Create the generator with a graph operator and an output audio reference.
		 *
		 * @param InGraphOperator - Unique pointer to the IOperator which executes the entire graph.
		 * @param InGraphOutputAudioRef - Read reference to the audio buffer filled by the InGraphOperator.
		 */
		FMetasoundGenerator(FMetasoundGeneratorInitParams&& InParams);

		virtual ~FMetasoundGenerator();

		/** Set the value of a graph's input data using the assignment operator.
		 *
		 * @param InName - The name of the graph's input data reference.
		 * @param InData - The value to assign.
		 */
		template<typename DataType>
		void SetInputValue(const FString& InName, DataType InData)
		{
			typedef TDataWriteReference< typename TDecay<DataType>::Type > FDataWriteRef;

			const FDataReferenceCollection& InputCollection = RootExecuter.GetInputs();

			// Check if an input data reference with the given name and type exist in the graph.
			bool bContainsWriteReference = InputCollection.ContainsDataWriteReference< typename TDecay<DataType>::Type >(InName);
			if (ensureMsgf(bContainsWriteReference, TEXT("Operator does not contain write reference name \"%s\" of type \"%s\""), *InName, *GetMetasoundDataTypeName<DataType>().ToString()))
			{
				FDataWriteRef WriteRef = InputCollection.GetDataWriteReference<DataType>(InName);

				// call assignment operator of DataType.
				*WriteRef = InData;
			}
		}

		/** Apply a function to the graph's input data.
		 *
		 * @param InName - The name of the graph's input data reference.
		 * @param InFunc - A function which takes the DataType as an input.
		 */ 
		template<typename DataType>
		void ApplyToInputValue(const FString& InName, TUniqueFunction<void(DataType&)> InFunc)
		{
			// Get decayed type as InFunc could take a const qualified type.
			typedef TDataWriteReference< typename TDecay<DataType>::Type > FDataWriteRef;

			const FDataReferenceCollection& InputCollection = RootExecuter.GetInputs();

			// Check if an input data reference with the given name and type exists in the graph.
			bool bContainsWriteReference = InputCollection.ContainsDataWriteReference< typename TDecay<DataType>::Type >(InName);
			if (ensureMsgf(bContainsWriteReference, TEXT("Operator does not contain write reference name \"%s\" of type \"%s\""), *InName, *GetMetasoundDataTypeName<DataType>().ToString()))
			{
				FDataWriteRef WriteRef = InputCollection.GetDataWriteReference<DataType>(InName);

				// Apply function to DataType
				InFunc(*WriteRef);
			}
		}

		/** Update the current graph operator with a new graph operator. The number of channels
		 * of InGraphOutputAudioRef must match the existing number of channels reported by
		 * GetNumChannels() in order for this function to successfully replace the graph operator.
		 *
		 * @param InGraphOperator - Unique pointer to the IOperator which executes the entire graph.
		 * @param InGraphOutputAudioRef - Read reference to the audio buffer filled by the InGraphOperator.
		 *
		 * @return Returns true if the graph operator was successfully swapped. False otherwise.
		 */
		bool UpdateGraphOperator(FMetasoundGeneratorInitParams&& InParams);

		/** Return the number of audio channels. */
		int32 GetNumChannels() const;

		//~ Begin FSoundGenerator
		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
		int32 GetDesiredNumSamplesToRenderPerCallback() const override;
		bool IsFinished() const override;
		//~ End FSoundGenerator

	private:

		// Internal set graph after checking compatibility.
		void SetGraph(FMetasoundGeneratorInitParams&& InParams);

		// Fill OutAudio with data in InBuffer, up to maximum number of samples.
		// Returns the number of samples used.
		int32 FillWithBuffer(const Audio::AlignedFloatBuffer& InBuffer, float* OutAudio, int32 MaxNumOutputSamples); 

		// Metasound creates deinterleaved audio while sound generator requires interleaved audio.
		void InterleaveGeneratedAudio();
		
		FExecuter RootExecuter;

		int32 NumChannels;
		int32 NumFramesPerExecute;
		int32 NumSamplesPerExecute;

		FAudioBufferReadRef GraphOutputAudioRef;

		// This will be bopped when this metasound is finished playing.
		FBopReadRef OnFinishedBopRef;

		Audio::AlignedFloatBuffer InterleavedAudioBuffer;

		Audio::AlignedFloatBuffer OverflowBuffer;
	};
}

