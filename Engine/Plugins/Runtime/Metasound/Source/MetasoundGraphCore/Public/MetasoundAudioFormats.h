// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundAudioBuffer.h"

namespace Metasound
{
	// TODO:
	// The 3 flavors of audio
	//
	// FVariableChannelAudio - Number of channels can change whenever! Format identifier included.
	// FMultiChannelAudio - Number of channels set on constructor!
	// TStaticChannelAudio - Number of channels set on declaration. Used for static channel configurations "Stereo", "FiveDotOne".

	// FAudio
	// 	- Union of all constant sized 
	//
	//
	// So lets say we want to write our node with generic audio.
	//
	//   GenericAudioIn -> Node -> GenericAudioOut -> CastToSpecificAudio.
	//   	We would have to have a failure mode and/or a specific mapping for each input type. 
	//   	Not too crazy. This would be an upmix/downmix node that would have to have a basic
	//   	approach for up/down mixing. 
	//
	//
	//   	Notes from email to self: Type erasure to define a union subtype interface with sharedptr passing semantics.  
	//
	//   	Audioref can support many types. All types must support format name and downcasting to self.  And monochannel forwarding. And channel count. And duplicating.  Operator implementations can use multimono convenience template, or specialize on audio subtype. Can also fail, but prolly shouldnt take audio union then. 
	//
	
	class FAudioUnion
	{
		class FAudioBase
		{
			public:
				virtual ~FAudioBase() {}

				virtual TArray<const FAudioBufferReadRef>& GetStorage() const = 0;

				virtual TArray<const FAudioBufferWriteRef>& GetStorage() = 0;

				virtual int32 GetMaxNumChannels() const = 0;

		};

		template<typename AudioType>
		class FAudioWrapper : public FAudioBase
		{
			public:
				FAudioWrapper(AudioType&& InType)
				:	Audio(MoveTemp(InType))
				{}

				virtual TArray<const FAudioBufferReadRef>& GetStorage() const override
				{
					return Audio.GetStorage();
				}


				virtual TArray<const FAudioBufferWriteRef>& GetStorage() override
				{
					return Audio.GetStorage();
				}

				virtual int32 GetMaxNumChannels() const override
				{
					return Audio.GetMaxNumChannels();
				}

			private:
				AudioType Audio;
		};

		public:
			template<typename AudioType>
			FAudioUnion(AudioType&& InType)
			:	AudioPtr(MoveTemp(InType))
			{
			}

			TArray<const FAudioBufferReadRef>& GetStorage() const
			{
				return AudioPtr->GetStorage();
			}

			TArray<const FAudioBufferWriteRef>& GetStorage()
			{
				return AudioPtr->GetStorage();
			}

			int32 GetMaxNumChannels() const
			{
				return AudioPTr->GetMaxNumChannels();
			}

			FName GetDataTypeName() const
			{
				// TODO: this should just be a call to the owning reference type's existing type information.
				return AudioPtr->GetDataTypeName();
			}

			template<typename AudioReferenceType>
			TDataReadReference<AudioReferenceType> GetReadableRefChecked() const
			{
				// TODO: check typename matches audioptr
				// TODO: think about how this cast actually works. Should it be a clone?
				// Where's that actual object stored? not clear yet from this implementation.
				return TDataReadReference<AudioReferenceType>(ParameterReferenceCast<AudioReferenceType>());
			}

		private:
			
			// TODO: This is holding onto the actual object, when it needs to hold on to the reference to the object. 
			TUniquePtr<FAudioBase> AudioPrt;
	};



	// If we are using this format, that we expect the channel count to change during runtime.
	// TODO: create another one that is sized on construction but cannot change channel count.
	class FVariableChannelAudio
	{
		public:
			FVariableChannelAudio(int32 InNumFrames, int32 InMaxNumChannels, int32 InNumChannels)
			:	NumFrames(InNumFrames)
			,	MaxNumChannels(InMaxNumChannels)
			,	NumChannels(0)
			{
				NumFrames = FMath::Max(NumFrames, 1);
				MaxNumChannels = FMath::Max(MaxNumChannels, 0);

				for (int32 i = 0; i < MaxNumChannels; i++)
				{
					FAudioBufferWriteRef Audio(NumFrames);
					// TODO: SetZeroed() should be a member func
					FMemory::Memset(Audio.GetData(), 0, sizeof(float) * NumFrames);

					WritableBufferStorage.Add(Audio);
					ReadableBufferStorage.Add(Audio);
				}

				SetNumChannels(InNumChannels)
			}


			FVariableChannelAudio(const TArray<FAudioBufferWriteRef>& InStorage)
			{
				// TODO:
			}

			int32 SetNumChannels(int32 InNumChannels)
			{
				// TODO: should we just add more channels if they exist? 
				NumChannels = FMath::Max(0, FMath::Min(InNumChannels, MaxNumChannels));

				ReadableBuffers = TArrayView<const FReadableAudioBuffer>(ReadableBufferStorage.GetData(), NumChannels);
				WritableBuffers = TArrayView<const FWritableAudioBuffer>(WritableBufferStorage.GetData(), NumChannels);
			}

			int32 GetNumChannels() const
			{
				return NumChannels;
			}

			// TODO: where do we store formats?
			//EFormat EFormat;
			//

			TArrayView<const FAudioBufferReadRef>& GetBuffers() const
			{
				return ReadableBuffers;
			}

			TArrayView<const FAudioBufferWriteRef>& GetBuffers() 
			{
				return WritableBuffers;
			}

			const TArray<const FAudioBufferReadRef>& GetStorage() const
			{
				return ReadableBufferStorage;
			}

			const TArray<const FAudioBufferWriteRef>& GetStorage()
			{
				return WritableBufferStorage;
			}
		protected:
			friend class TDataReadReference<FVariableChannelAudio>;

			FVariableChannelAudio(const TArray<FAudioBufferReadRef>& InStorage)
			{
				// TODO: What do we do about write storage in this scenario? We cannot return write data because
				// we have only been given a read reference. Maybe we could have a DataType specialization
				// which allows this constructor to exist for readable data types, and not for writable data types. 
				// Maybe a protected thing with a friend class? Maybe readable parameter has to do a WritableCast<>
				//
				// The static sized n channel templates should do something like that as well.
				//
				// FVariableChannelAudioReadRef ReadRef(OtherReadRef)
				// 		
				//
				// FVariableChannelAudioWriteRef WriteRef(OtherReadRef) // <- Not allowed!  
				//
				// 
			}
			
		private:


			int32 NumChannels;

			TArrayView<const FAudioBufferReadRef> ReadableBuffers;
			TArrayView<const FAudioBufferReadRef> WritableBuffers;

			// The reference storage is const so that the mono audio buffer 
			// references can be passed forward when the format is changed (as
			// opposed to making a copy of the reference). This requires that 
			// this object always be referencing the same underlying audio
			// buffer objects, hence the const protection to ensure that it 
			// never gets changed.
			TArray<const FAudioBufferReadRef> ReadableBufferStorage;
			TArray<const FAudioBufferWriteRef> WritableBufferStorage;
	};


	
	template<int32 NumChannels>
	class TStaticChannelAudio
	{
		public:
			TStaticChannelAudio(int32 InNumFrames)
			:	NumFrames(InNumFrames)
			{
				static_assert(NumChannels > 0, "NumChannels must be greater than zero");

				NumFrames = FMath::Max(NumFrames, 1);

				for (int32 i = 0; i < NumChannels; i++)
				{
					FAudioBufferWriteRef Audio(NumFrames);
					// TODO: SetZeroed() should be a member func
					FMemory::Memset(Audio.GetData(), 0, sizeof(float) * NumFrames);

					WritableBufferStorage.Add(Audio);
					ReadableBufferStorage.Add(Audio);
				}

				WritableBuffers = WritableBufferStorage;
				ReadableBuffers = ReadableBufferStorage;
			}

			/* TODO:
			// Make a constructor that can take individual arguments of buffers.
			// TStaticChannelAudio<2>(FAudioWriteRef Val1, FAudioWriteRef Val2)
			template<
				typename... ArgTypes,
				typename = typename TEnableIf<
					TAndValue<
						sizeof...(ArgTypes) == NumChannels,
						TAnd< TIsSameType<FAudioWriteRef, typename TDecay<ArgTypes>... >
					>
				>
			>
			//TStaticChannelAudio(ArgType&&....)
			*/

			int32 GetNumChannels() const
			{
				return NumChannels;
			}

			template<int32 Index>
			FAudioBufferReadRef GetBuffer() const
			{
				static_assert(Index >= 0, "Index must be within range of channels");
				static_assert(Index < NumChannels, "Index must be within range of channels");

				return ReadableBuffers.GetData()[Index];
			}

			template<int32 Index>
			FAudioBufferWriteRef GetBuffer() 
			{
				static_assert(Index >= 0, "Index must be within range of channels");
				static_assert(Index < NumChannels, "Index must be within range of channels");

				return WritableBuffers.GetData()[Index];
			}

			TArrayView<const FAudioBufferReadRef> GetBuffers() const
			{
				return ReadableBuffers;
			}

			TArrayView<const FAudioBufferWriteRef> GetBuffers() 
			{
				return WritableBuffers;
			}

			TArrayView<const FAudioBufferReadRef> GetStorage() const
			{
				return ReadableBufferStorage;
			}

			TArrayView<const FAudioBufferWriteRef> GetStorage()
			{
				return WritableBufferStorage;
			}

		private:

			TArray<const FAudioBufferWriteRef> WritableBufferStorage;
			TArray<const FAudioBufferReadRef> ReadableBufferStorage;

	};

	class FMonoAudio : public TStaticAudio<1>
	{
		public:
			using TStaticAudio<1>::TStaticAudio<1>;
			using Center = TStaticAudio<1>::GetBuffer<0>;
	};

	class FStereoAudio : public TStaticAudio<2>
	{
		public:
			using TStaticAudio<2>::TStaticAudio<2>;
			using Left = TStaticAudio<2>::GetBuffer<0>;
			using Right = TStaticAudio<2>::GetBuffer<1>;
	};

}
