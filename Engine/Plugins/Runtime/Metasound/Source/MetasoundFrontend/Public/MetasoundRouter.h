// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DSP/Dsp.h"
#include "DSP/MultithreadedPatching.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundLiteral.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundTrigger.h"
#include "Misc/Guid.h"
#include "Templates/TypeHash.h"
#include "UObject/NameTypes.h"

#include <atomic>
#include <type_traits>

/**
 * METASOUND TRANSMISSION SYSTEM
 * This allows for transmission of arbitrary datatypes using addresses set by FNames.
 * This system is used by Send and Receive nodes in the Metasound graph, but can also be used in C++ for arbitrary message passing.
 *
 * Typical use case:
 * TSenderPtr<float> FloatSender FDataTransmissionCenter::Get().RegisterNewSender<float>(ExampleAddress, InitSettings);
 * TReceiverPtr<float> FloatReceiver FDataTransmissionCenter::Get().RegisterNewReceiver<float>(ExampleAddress, InitSettings);
 *
 * //...
 * FloatSender->Push(4.5f);
 *
 * //... elsewhere on a different thread:
 * float ReceivedFloat = FloatReceiver->Pop();
 *
 * It's that easy! (hopefully!)
 *
 * In general, some performance considerations for this system:
 * 1) a circular buffer is allocated for each address a sender is set up for, and is sized to the max Delay of any connected sender.
 * 2) Senders and receivers are meant to have long lifecycles, and requesting them from the FDataTransmissionCenter may be expensive if done every tick.
 * 3) Most non-audio data types cannot be mixed, so multiple senders to the same address will cause only one of the senders to be effective.
 * 4) while the overall system is thread safe, individual TSender and TReceiver objects should only be used by a single thread.
 */


namespace Metasound
{
	class METASOUNDFRONTEND_API FSendAddress
	{
	public:
		FSendAddress() = default;

		// Create an address without a data type or instance ID
		FSendAddress(const FString& InChannelName);

		// Create an address with a channel name, data type and optionally an instance ID. 
		FSendAddress(const FName& InChannelName, const FName& InDataType, uint64 InInstanceID=INDEX_NONE);

		friend uint32 GetTypeHash(const FSendAddress& InAddress)
		{
			uint32 HashedChannel = HashCombineFast(::GetTypeHash(InAddress.DataType), ::GetTypeHash(InAddress.ChannelName));
			HashedChannel = HashCombineFast(HashedChannel, ::GetTypeHash(InAddress.InstanceID));
			return HashedChannel;
		}

		FORCEINLINE bool operator==(const FSendAddress& Other) const
		{
			return (InstanceID == Other.InstanceID) && (ChannelName == Other.ChannelName) && (DataType == Other.DataType); /*&& (Subsystem == Other.Subsystem);*/
		}

		FORCEINLINE bool operator!=(const FSendAddress& Other) const
		{
			return !(*this == Other);
		}

		const FName& GetDataType() const
		{
			return DataType;
		}

		const FName& GetChannelName() const
		{
			return ChannelName;
		}

		uint64 GetInstanceID() const
		{
			return InstanceID;
		}

		FString ToString() const
		{
			return FString::Format(TEXT("SendAddress {0}:{1}[Type={2}]"), {ChannelName.ToString(), InstanceID, DataType.ToString()});
		}

	private:
		FName ChannelName;
		FName DataType;

		// For instance-specific addresses, this uint64 is the instance of this metasound.
		uint64 InstanceID = INDEX_NONE;
	};

	// Base class that defines shared state and utilities for senders, receivers, and the shared channel state.
	class METASOUNDFRONTEND_API IDataTransmissionBase
	{
	public:
		IDataTransmissionBase() = delete;

		IDataTransmissionBase(FName InDataType)
			: DataType(InDataType)
		{
		}

		virtual ~IDataTransmissionBase() = default;
		
		template<typename TSenderImplementation>
		bool CheckType() const
		{
			FName DestinationTypeName = TSenderImplementation::GetDataTypeName();
			return ensureAlwaysMsgf(DataType == DestinationTypeName, TEXT("Tried to downcast type %s to %s!"), *DataType.ToString(), *DestinationTypeName.ToString());
		}

		template<typename TSenderImplementation>
		const TSenderImplementation& GetAs() const
		{
			check(CheckType<TSenderImplementation>());
			return static_cast<TSenderImplementation&>(*this);
		}

		template<typename TSenderImplementation>
		TSenderImplementation& GetAs()
		{
			check(CheckType<TSenderImplementation>());
			return static_cast<TSenderImplementation&>(*this);
		}

		FName GetDataType() const
		{
			return DataType;
		}

	protected:
		// The data type for this sender.
		FName DataType;
	};

	class IDataChannel;

	// This is a handle to something that will retrieve a datatype.
	// Intentionally opaque- to use, one will need to call IReceiver::GetAs<TReceiver<YourDataType>>().
	class METASOUNDFRONTEND_API IReceiver : public IDataTransmissionBase
	{
	public:
		IReceiver(TSharedPtr<IDataChannel, ESPMode::ThreadSafe> InDataChannel, FName DataType)
			: IDataTransmissionBase(DataType)
			, DataChannel(InDataChannel)
		{}


		virtual ~IReceiver();

	protected:
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel;
	};

	// This is a handle to something that will retrieve a datatype.
	// Intentionally opaque- to use, one will need to call ISender::GetAs<TSender<YourDataType>>().
	class METASOUNDFRONTEND_API ISender : public IDataTransmissionBase
	{
	public:
		ISender(TSharedPtr<IDataChannel, ESPMode::ThreadSafe> InDataChannel, FName DataType)
			: IDataTransmissionBase(DataType)
			, DataChannel(InDataChannel)
		{}

		virtual ~ISender();

		virtual int64 GetPayloadID() const = 0;

		// Constructs the value with a literal and pushes to data channel. 
		virtual bool PushLiteral(const FLiteral& InLiteral) = 0;

	protected:
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> DataChannel;
	};

	struct FSenderInitParams
	{
		FOperatorSettings OperatorSettings;
		float DelayTimeInSeconds;
	};

	struct FReceiverInitParams
	{
		FOperatorSettings OperatorSettings;
	};

	// This contains an intermediary buffer for use between the send and receive nodes.
	// Typically only used by ISender and IReceiver implementations.
	class METASOUNDFRONTEND_API IDataChannel : public IDataTransmissionBase, public TSharedFromThis<IDataChannel, ESPMode::ThreadSafe>
	{
	public:
		virtual ~IDataChannel() = default;

		IDataChannel(FName DataType)
			: IDataTransmissionBase(DataType)
		{}

		TUniquePtr<IReceiver> NewReceiver(const FReceiverInitParams& InitParams)
		{
			NumAliveReceivers.Increment();
			return ConstructNewReceiverImplementation(InitParams);
		}

		TUniquePtr<ISender> NewSender(const FSenderInitParams& InitParams)
		{
			NumAliveSenders.Increment();
			return ConstructNewSenderImplementation(InitParams);
		}

		uint32 GetNumActiveReceivers() const
		{
			return NumAliveReceivers.GetValue();
		}

		uint32 GetNumActiveSenders() const
		{
			return NumAliveSenders.GetValue();
		}

		virtual int64 GetPayloadID() const { return INDEX_NONE; }
		virtual int64 CreateNewPayloadID() = 0;
		virtual bool PushOpaque(ISender& InSender) { return true; };
		virtual void PopOpaque(IReceiver& InReceiver) {};
		virtual bool IsEmpty() const { return true; };

		virtual FName GetDataType() { return FName(); };

		virtual bool PushLiteral(const FLiteral& InParam) { return false; }

	protected:
		virtual TUniquePtr<IReceiver> ConstructNewReceiverImplementation(const FReceiverInitParams& InitParams) { return nullptr; };
		virtual TUniquePtr<ISender> ConstructNewSenderImplementation(const FSenderInitParams& InitParams) { return nullptr; };

	private:
		void OnSenderDestroyed()
		{
			check(NumAliveSenders.GetValue() > 0);
			NumAliveSenders.Decrement();
		}

		void OnReceiverDestroyed()
		{
			check(NumAliveReceivers.GetValue() > 0);
			NumAliveReceivers.Decrement();
		}

		friend class ISender;
		friend class IReceiver;

		FThreadSafeCounter NumAliveReceivers;
		FThreadSafeCounter NumAliveSenders;
	};

	// Generic templates:
	template<typename TDataType>
	class TNonOperationalSender : public ISender
	{
	public:
		bool Push(const TDataType& InValue) { return true; }
	};

	template<typename TDataType>
	class TNonOperationalReceiver : public IReceiver
	{
	public:
		bool Pop(TDataType& OutValue) { return true; }
	};

	template<typename TDataType>
	class TNonOperationalDataChannel : public IDataChannel
	{

	public:

		TNonOperationalDataChannel(const FOperatorSettings& InOperatorSettings)
			: IDataChannel(FName())
			, PayloadIDCounter(0)
		{}

		virtual bool PushOpaque(ISender& InSender) override 
		{
			return true;
		}

		virtual void PopOpaque(IReceiver& InReceiver) override
		{

		}

		virtual int64 CreateNewPayloadID() override
		{
			// Atomic return and post increment.
			return PayloadIDCounter++;
		}

		virtual bool IsEmpty() const override
		{
			return true;
		}

		virtual FName GetDataType() override
		{
			return GetDataTypeName();
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

	protected:
		virtual TUniquePtr<IReceiver> ConstructNewReceiverImplementation(const FReceiverInitParams& InitParams) override
		{
			return TUniquePtr<IReceiver>();
		}

		virtual TUniquePtr<ISender> ConstructNewSenderImplementation(const FSenderInitParams& InitParams) override
		{
			return TUniquePtr<ISender>();
		}
	private:
		std::atomic<int64> PayloadIDCounter;
	};

	// Specializations for trivially copyable types:
	template<typename TDataType>
	class TSender : public ISender
	{
		static_assert(std::is_copy_constructible<TDataType>::value, "The generic TDataChannel requires the DataType it is specialized with to be copy assignable.");

		// This can be changed to change when we reallocate the internal buffer for this sender.
		static constexpr float BufferResizeThreshold = 2.0f;

		bool bMessageDataTypeError = true;

	public:

		bool Push(const TDataType& InElement)
		{
			SenderBuffer.Push(InElement);

			if (SenderBuffer.Num() >= NumElementsToDelayBy)
			{
				return PushToDataChannel();
			}
			else
			{
				return true;
			}
		}

		bool PushLiteral(const FLiteral& InLiteral) override
		{
			if (!TLiteralTraits<TDataType>::IsParsable(InLiteral))
			{
				if (bMessageDataTypeError)
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to send data. Data type [TypeName:%s] cannot be parsed from literal [Literal:%s]."), *GetMetasoundDataTypeString<TDataType>(), *LexToString(InLiteral));
					bMessageDataTypeError = false;
				}
				return false;
			}
			else
			{
				return TSender<TDataType>::Push(TDataTypeLiteralFactory<TDataType>::CreateExplicitArgs(Params.OperatorSettings, InLiteral));
			}
		}

		// Resets the delay for this specific sender. if this goes beyond the threshold set by BufferResizeThreshold, we reallocate.
		bool SetDelay(float InSeconds)
		{
			Params.DelayTimeInSeconds = InSeconds;
			// TODO: LOL. I was wondering why the having a delay didn't change the behavior.
			// Probably for the best since it does not make much sense to delay
			// sent values. The FMath::Min() results in it being either 1 or 0. 
			//NumElementsToDelayBy = FMath::Min(InSeconds * Params.OperatorSettings.GetActualBlockRate(), 1.f);

			/*
			if (NumElementsToDelayBy >= SenderBuffer.GetCapacity())
			{
				SenderBuffer.SetCapacity(NumElementsToDelayBy * BufferResizeThreshold);
			}
			*/
			SenderBuffer.SetCapacity(2);
			return true;
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

		TSender(const FSenderInitParams& InitParams, TSharedPtr<IDataChannel, ESPMode::ThreadSafe> InDataChannel)
			: ISender(InDataChannel, GetDataTypeName())
			, Params(InitParams)
		{
			SetDelay(InitParams.DelayTimeInSeconds);
		}

		int64 GetPayloadID() const override
		{
			return PayloadID;
		}

		TDataType RetrievePayload()
		{
			check(Payload.IsValid());
			return MoveTempIfPossible(*Payload);
		}

		// Push from the SenderBuffer to the data channel.
		bool PushToDataChannel()
		{
			PayloadID = DataChannel->CreateNewPayloadID();
			if (!Payload.IsValid())
			{
				Payload = MakeUnique<TDataType>(SenderBuffer.Pop());
			}
			else
			{
				*Payload = SenderBuffer.Pop();
			}

			return DataChannel->PushOpaque(*this);
		}

	private:
		// This buffer acts as a delay line for this sender specifically.
		Audio::TCircularAudioBuffer<TDataType> SenderBuffer;

		int64 PayloadID = 0;

		// lazily initialized holder for payload, allocated on first call to Push.
		// Placed on the heap so that we don't have to assume we know how to construct TDataType.
		TUniquePtr<TDataType> Payload;

		FSenderInitParams Params;

		// The amount of pop operations before elements before this sender pushed appear.
		uint32 NumElementsToDelayBy = 0;
	};

	template<typename TDataType>
	class TReceiver : public IReceiver
	{
		static_assert(std::is_copy_constructible<TDataType>::value, "The generic TDataChannel requires the DataType it is specialized with to be copy constructible.");
	public:

		bool CanPop() const
		{
			if (DataChannel->GetPayloadID() == LastPayloadID)
			{
				return false;
			}

			if (DataChannel->IsEmpty())
			{
				return false;
			}

			return true;
		}

		// Pop the latest value from the data channel.
		bool Pop(TDataType& OutValue)
		{
			DataChannel->PopOpaque(*this);

			check(Payload.IsValid());
			OutValue = *Payload;
			return true;
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

		TReceiver(const FReceiverInitParams& InitParams, TSharedPtr<IDataChannel, ESPMode::ThreadSafe> InDataChannel)
			: IReceiver(InDataChannel, GetDataTypeName())
			, OperatorSettings(InitParams.OperatorSettings)
		{
		}

		void PushPayload(int64 InPayloadID, const TDataType& InDataPayload)
		{
			// Attempting to send the same payload again, so ignore.
			if (InPayloadID == LastPayloadID)
			{
				return;
			}

			LastPayloadID = InPayloadID;
			if (!Payload.IsValid())
			{
				Payload = MakeUnique<TDataType>(InDataPayload);
			}
			else
			{
				*Payload = InDataPayload;
			}
		}

	private:
		int64 LastPayloadID = INDEX_NONE;
		TUniquePtr<TDataType> Payload;
		FOperatorSettings OperatorSettings;
	};

	// Generic templated implementation of IDataChannel that can use for any copyable type.
	template<typename TDataType>
	class TCopyableDataChannel : public IDataChannel
	{
		static_assert(std::is_copy_constructible<TDataType>::value, "The generic TDataChannel requires the DataType it is specialized with to be copy constructible.");

	public:

		TCopyableDataChannel(const FOperatorSettings& InOperatorSettings)
			: IDataChannel(GetDataTypeName())
			, OperatorSettings(InOperatorSettings)
			, PayloadIDCounter(0)
		{}

		virtual int64 GetPayloadID() const override
		{
			return PayloadID;
		}

		virtual int64 CreateNewPayloadID() override
		{
			// Atomic return and post increment.
			return PayloadIDCounter++;
		}

		virtual bool PushOpaque(ISender& InSender) override
		{
			TSender<TDataType>& CastSender = InSender.GetAs<TSender<TDataType>>();

			{
				FScopeLock ScopeLock(&AtomicDataLock);
				PayloadID = InSender.GetPayloadID();
				if (!AtomicData.IsValid())
				{
					AtomicData = MakeUnique<TDataType>(CastSender.RetrievePayload());
				}
				else
				{
					*AtomicData = CastSender.RetrievePayload();
				}
			}

			return true;
		}

		virtual void PopOpaque(IReceiver& InReceiver) override
		{
			TReceiver<TDataType>& CastReceiver = InReceiver.GetAs<TReceiver<TDataType>>();

			FScopeLock ScopeLock(&AtomicDataLock);
			if (AtomicData.IsValid())
			{
				CastReceiver.PushPayload(PayloadID, *AtomicData);
			}
		}

		virtual bool IsEmpty() const override
		{
			return !AtomicData.IsValid();
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

		virtual FName GetDataType() override
		{
			return GetDataTypeName();
		}

		virtual bool PushLiteral(const FLiteral& InParam)
		{
			if (TLiteralTraits<TDataType>::IsParsable(InParam))
			{
				TDataType DataToPush = TDataTypeLiteralFactory<TDataType>::CreateExplicitArgs(OperatorSettings, InParam);

				FScopeLock ScopeLock(&AtomicDataLock);
				if (!AtomicData.IsValid())
				{
					AtomicData = MakeUnique<TDataType>(DataToPush);
				}
				else
				{
					*AtomicData = DataToPush;
				}

				return true;
			}
			else
			{
				return false;
			}
		}

	protected:
		virtual TUniquePtr<IReceiver> ConstructNewReceiverImplementation(const FReceiverInitParams& InitParams) override
		{
			return TUniquePtr<IReceiver>(new TReceiver<TDataType>(InitParams, this->AsShared()));
		}

		virtual TUniquePtr<ISender> ConstructNewSenderImplementation(const FSenderInitParams& InitParams) override
		{
			return TUniquePtr<ISender>(new TSender<TDataType>(InitParams, this->AsShared()));
		}

	private:
		// NOTE- In the future, this could be changed to use a TCircualrAudioBuffer or a TQueue for lockless operation.
		// The primary challenge with this is handling multiple senders and receivers.
		// In order to support multiple senders and receivers at scale, we'd need a good implementation of a bounded MPMC queue for arbitrary datatypes.
		TUniquePtr<TDataType> AtomicData;
		FCriticalSection AtomicDataLock;

		int64 PayloadID = INDEX_NONE;

		FOperatorSettings OperatorSettings;

		std::atomic<int64> PayloadIDCounter;
	};

	// AUDIO SPECIALIZATIONS:
	template<typename TDataType>
	class TAudioSender : public ISender
	{
		static constexpr float BufferResizeThreshold = 2.0f;
		static const int32 MaxChannels = 8;
	public:

		bool Push(const TDataType& InElement)
		{
			const int32 NumChannelsToPush = InElement.GetNumChannels();
			check(NumChannelsToPush <= MaxChannels);

			const TArrayView<const FAudioBufferReadRef> InputBuffers = InElement.GetBuffers();

			for (int32 ChannelIndex = 0; ChannelIndex < NumChannelsToPush; ChannelIndex++)
			{
				const FAudioBuffer& ChannelBuffer = *InputBuffers[ChannelIndex];
				DelayLines[ChannelIndex].Push(ChannelBuffer.GetData(), ChannelBuffer.Num());

				if (DelayLines[ChannelIndex].Num() > DelayTimeInFrames)
				{
					Audio::FAlignedFloatBuffer& IntermediateBuffer = IntermediaryBuffers[ChannelIndex];
					DelayLines[ChannelIndex].Pop(IntermediateBuffer.GetData(), IntermediateBuffer.Num());
					DataChannelInputs[ChannelIndex].PushAudio(IntermediateBuffer.GetData(), IntermediateBuffer.Num());
				}
			}

			return true;
		}

		int64 GetPayloadID() const override
		{
			return INDEX_NONE;
		}

		bool PushLiteral(const FLiteral& InLiteral) override
		{
			ensureMsgf(false, TEXT("Cannot push literal to audio format"));
			return false;
		}

		// Resets the delay for this sender.
		bool SetDelay(float InSeconds)
		{
			Params.DelayTimeInSeconds = InSeconds;
			DelayTimeInFrames = FMath::Min<int32>(Params.DelayTimeInSeconds * Params.OperatorSettings.GetSampleRate(), Params.OperatorSettings.GetNumFramesPerBlock());
			
			if (DelayTimeInFrames >= DelayLines[0].Num())
			{
				int32 NumSamplesToBuffer = DelayTimeInFrames * BufferResizeThreshold;
				DelayLines.Reset();
				IntermediaryBuffers.Reset();

				// For now, all senders/receivers/data channels will just allocate 8 delay lines, but this can be changed in the future.
				for (int32 Index = 0; Index < MaxChannels; Index++)
				{
					DelayLines.Emplace(NumSamplesToBuffer);
					Audio::FAlignedFloatBuffer BufferForChannel;
					BufferForChannel.AddZeroed(Params.OperatorSettings.GetNumFramesPerBlock());
					IntermediaryBuffers.Add(MoveTemp(BufferForChannel));
				}
			}

			return true;
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

		TAudioSender(const FSenderInitParams& InitParams, TSharedPtr<IDataChannel, ESPMode::ThreadSafe> InDataChannel, TArray<Audio::FPatchInput>&& Inputs)
			: ISender(InDataChannel, GetDataTypeName())
			, DataChannelInputs(MoveTemp(Inputs))
			, Params(InitParams)
		{
			SetDelay(InitParams.DelayTimeInSeconds);
		}

	private:
		// This buffer acts as a delay line for this sender specifically.
		TArray<Audio::TCircularAudioBuffer<float>> DelayLines;

		uint32 DelayTimeInFrames;

		TArray<Audio::FPatchInput> DataChannelInputs;

		TArray<Audio::FAlignedFloatBuffer> IntermediaryBuffers;

		FSenderInitParams Params;
	};

	template<typename TDataType>
	class TAudioReceiver : public IReceiver
	{
		static constexpr float BufferResizeThreshold = 2.0f;
		static const int32 MaxChannels = 8;
	public:

		bool CanPop() const
		{
			// Audio receivers can and should always pop audio,
			// because we zero out audio when a datachannel underruns.
			return true;
		}

		bool Pop(TDataType& OutElement)
		{
			const int32 NumChannelsToPop = OutElement.GetNumChannels();
			check(NumChannelsToPop <= MaxChannels);

			DataChannel->PopOpaque(*this);

			TArrayView<const FAudioBufferWriteRef> OutputBuffers = OutElement.GetBuffers();

			for (int32 ChannelIndex = 0; ChannelIndex < NumChannelsToPop; ChannelIndex++)
			{
				FAudioBuffer& OutBuffer = *OutputBuffers[ChannelIndex];
				DataChannelOutputs[ChannelIndex]->PopAudio(OutBuffer.GetData(), OutBuffer.Num(), false);
			}

			return true;
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

		TAudioReceiver(const FReceiverInitParams& InitParams, TSharedPtr<IDataChannel, ESPMode::ThreadSafe> InDataChannel, TArray<Audio::FPatchOutputStrongPtr>&& Outputs)
			: IReceiver(InDataChannel, GetDataTypeName())
			, DataChannelOutputs(MoveTemp(Outputs))
			, Params(InitParams)
		{
		}

	private:

		TArray<Audio::FPatchOutputStrongPtr> DataChannelOutputs;

		FReceiverInitParams Params;
	};

	template<typename TDataType>
	class TAudioDataChannel : public IDataChannel
	{
	private:
		static const int32 MaxChannels = 8;
	public:
		TAudioDataChannel(const FOperatorSettings& InOperatorSettings)
			: IDataChannel(GetDataTypeName())
			, PayloadIDCounter(0)
		{
			AudioBuses.AddDefaulted(MaxChannels);
		}

		virtual bool PushOpaque(ISender& InSender) override
		{
			return true;
		}

		virtual void PopOpaque(IReceiver& InReceiver) override
		{
			for (Audio::FPatchMixerSplitter& Bus : AudioBuses)
			{
				Bus.ProcessAudio();
			}
		}

		virtual int64 CreateNewPayloadID() override
		{
			// Atomic return and post increment.
			return PayloadIDCounter++;
		}

		virtual bool IsEmpty() const override
		{
			return false;
		}

		static FName GetDataTypeName()
		{
			return GetMetasoundDataTypeName<TDataType>();
		}

		virtual FName GetDataType() override
		{
			return GetDataTypeName();
		}

	protected:
		virtual TUniquePtr<IReceiver> ConstructNewReceiverImplementation(const FReceiverInitParams& InitParams) override
		{
			TArray<Audio::FPatchOutputStrongPtr> Outputs;
			for (Audio::FPatchMixerSplitter& Bus : AudioBuses)
			{
				Outputs.Add(Bus.AddNewOutput(8096, 1.0f));
			}

			return TUniquePtr<IReceiver>(new TAudioReceiver<TDataType>(InitParams, this->AsShared(), MoveTemp(Outputs)));
		}

		virtual TUniquePtr<ISender> ConstructNewSenderImplementation(const FSenderInitParams& InitParams) override
		{
			TArray<Audio::FPatchInput> Inputs;
			for (Audio::FPatchMixerSplitter& Bus : AudioBuses)
			{
				Inputs.Add(Bus.AddNewInput(8096, 1.0f));
			}

			return TUniquePtr<ISender>(new TAudioSender<TDataType>(InitParams, this->AsShared(), MoveTemp(Inputs)));
		}

	private:
		TArray<Audio::FPatchMixerSplitter> AudioBuses;
		std::atomic<int64> PayloadIDCounter;
	};


	// Coalesce both our standard copying Sender/Receivers and our audio-specific sender/receiver implementations into TSenderPtr and TReceiverPtr:

	template<typename TDataType, typename USenderType = typename std::conditional<TIsDerivedFrom<TDataType, IAudioDataType>::Value, TAudioSender<TDataType>, TSender<TDataType>>::type>
	using TSenderPtr = TUniquePtr<USenderType>;

	template<typename TDataType, typename UReceiverType = typename std::conditional<TIsDerivedFrom<TDataType, IAudioDataType>::Value, TAudioReceiver<TDataType>, TReceiver<TDataType>>::type>
	using TReceiverPtr = TUniquePtr<UReceiverType>;

	// SFINAE for creating the correct data channel type for the given datatype:

	template<typename TDataType, typename TEnableIf<std::is_copy_constructible<TDataType>::value && !TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TSharedRef<IDataChannel, ESPMode::ThreadSafe> MakeDataChannel(const FOperatorSettings& InSettings)
	{
		return MakeShareable(new TCopyableDataChannel<TDataType>(InSettings));
	}

	template<typename TDataType, typename TEnableIf<TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TSharedRef<IDataChannel, ESPMode::ThreadSafe> MakeDataChannel(const FOperatorSettings& InSettings)
	{
		return MakeShareable(new TAudioDataChannel<TDataType>(InSettings));
	}

	template<typename TDataType, typename TEnableIf<!std::is_copy_constructible<TDataType>::value && !TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TSharedRef<IDataChannel, ESPMode::ThreadSafe> MakeDataChannel(const FOperatorSettings& InSettings)
	{
		return MakeShareable(new TNonOperationalDataChannel<TDataType>(InSettings));
	}

	// Utility function for properly downcasting to the correct type based on whether TDataType is an audio stream:

	// For Audio Senders:
	template <typename TDataType, typename TEnableIf<TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TSenderPtr<TDataType> Downcast(TUniquePtr<ISender>&& InPtr)
	{
		checkf(InPtr->CheckType<TAudioSender<TDataType>>(), TEXT("Tried to downcast an ISender of type %s to a TSender of type %s!"), *(InPtr->GetDataType().ToString()), *(TAudioSender<TDataType>::GetDataTypeName().ToString()));
		return TSenderPtr<TDataType>(static_cast<TAudioSender<TDataType>*>(InPtr.Release()));
	}

	// For generic senders for copyable types:
	template <typename TDataType, typename TEnableIf<std::is_copy_constructible<TDataType>::value && !TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TSenderPtr<TDataType> Downcast(TUniquePtr<ISender>&& InPtr)
	{
		checkf(InPtr->CheckType<TSender<TDataType>>(), TEXT("Tried to downcast an ISender of type %s to a TSender of type %s!"), *(InPtr->GetDataType().ToString()), *(TSender<TDataType>::GetDataTypeName().ToString()));
		return TSenderPtr<TDataType>(static_cast<TSender<TDataType>*>(InPtr.Release()));
	}

	// For invalid types (non-copyable AND not an audio type):
	template <typename TDataType, typename TEnableIf<!std::is_copy_constructible<TDataType>::value && !TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TSenderPtr<TDataType> Downcast(TUniquePtr<ISender>&& InPtr)
	{
		return nullptr;
	}

	// For audio receivers:
	template <typename TDataType, typename TEnableIf<TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TReceiverPtr<TDataType> Downcast(TUniquePtr<IReceiver>&& InPtr)
	{
		checkf(InPtr->CheckType<TAudioReceiver<TDataType>>(), TEXT("Tried to downcast an IReceiver of type %s to a TSender of type %s!"), *(InPtr->GetDataType().ToString()), *(TAudioReceiver<TDataType>::GetDataTypeName().ToString()));
		return TReceiverPtr<TDataType>(static_cast<TAudioReceiver<TDataType>*>(InPtr.Release()));
	}

	// For generic receivers for copyable types:
	template <typename TDataType, typename TEnableIf<std::is_copy_constructible<TDataType>::value && !TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TReceiverPtr<TDataType> Downcast(TUniquePtr<IReceiver>&& InPtr)
	{
		checkf(InPtr->CheckType<TReceiver<TDataType>>(), TEXT("Tried to downcast an IReceiver of type %s to a TSender of type %s!"), *(InPtr->GetDataType().ToString()), *(TReceiver<TDataType>::GetDataTypeName().ToString()));
		return TReceiverPtr<TDataType>(static_cast<TReceiver<TDataType>*>(InPtr.Release()));
	}

	// For invalid types (non-copyable AND not an audio type):
	template <typename TDataType, typename TEnableIf<!std::is_copy_constructible<TDataType>::value && !TIsDerivedFrom<TDataType, IAudioDataType>::Value, bool>::Type = true>
	TReceiverPtr<TDataType> Downcast(TUniquePtr<IReceiver>&& InPtr)
	{
		return nullptr;
	}

	// Basic router that takes an FName address, 
	class METASOUNDFRONTEND_API FAddressRouter
	{

	public:

		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> FindDataChannel(const FSendAddress& InAddress);
		TSharedPtr<IDataChannel, ESPMode::ThreadSafe> GetDataChannel(const FSendAddress& InAddress, const FOperatorSettings& InOperatorSettings);

		TUniquePtr<ISender> RegisterNewSender(const FSendAddress& InAddress, const FSenderInitParams& InitParams);
		TUniquePtr<IReceiver> RegisterNewReceiver(const FSendAddress& InAddress, const FReceiverInitParams& InitParams);

		template<typename TDataType>
		TReceiverPtr<TDataType> RegisterNewReceiver(const FSendAddress& InAddress, const FReceiverInitParams& InitParams)
		{
			TReceiverPtr<TDataType> Receiver;

			const bool bIsMatchingDataType = GetMetasoundDataTypeName<TDataType>() == InAddress.GetDataType();
			if (ensure(bIsMatchingDataType))
			{
				TUniquePtr<IReceiver> ReceiverBase = RegisterNewReceiver(InAddress, InitParams);
				if (ReceiverBase.IsValid())
				{
					Receiver = Downcast<TDataType>(MoveTemp(ReceiverBase));
				}
			}

			return MoveTemp(Receiver);
		}

		template<typename TDataType>
		TSenderPtr<TDataType> RegisterNewSender(const FSendAddress& InAddress, const FSenderInitParams& InitParams)
		{
			TSenderPtr<TDataType> Sender;

			const bool bIsMatchingDataType = GetMetasoundDataTypeName<TDataType>() == InAddress.GetDataType();
			if (ensure(bIsMatchingDataType))
			{
				TUniquePtr<ISender> SenderBase = RegisterNewSender(InAddress, InitParams);
				if (SenderBase.IsValid())
				{
					Sender = Downcast<TDataType>(MoveTemp(SenderBase));
				}
			}

			return MoveTemp(Sender);
		}

		bool PushLiteral(const FName& InDataTypeName, const FName& InChannelName, const FLiteral& InParam)
		{
			FSendAddress Address{ InChannelName, InDataTypeName };

			TSharedPtr<IDataChannel, ESPMode::ThreadSafe> Channel = FindDataChannel(Address);
			if (Channel.IsValid())
			{
				return Channel->PushLiteral(InParam);
			}
			else
			{
				return false;
			}
		}

		bool UnregisterDataChannel(const FSendAddress& InAddress);
		bool UnregisterDataChannelIfUnconnected(const FSendAddress& InAddress);

		FAddressRouter(const FAddressRouter& Other)
			: DataChannelMap(Other.DataChannelMap)
		{
		}

		FAddressRouter()
		{}

	private:

		TMap<FSendAddress, TSharedRef<IDataChannel, ESPMode::ThreadSafe>> DataChannelMap;
		FCriticalSection DataChannelMapMutationLock;
	};


	// Main entry point for all sender/receiver registration.
	class METASOUNDFRONTEND_API FDataTransmissionCenter
	{

	public:

		// Returns the universal router.
		static FDataTransmissionCenter& Get();

		// Creates a new object to push data to an address.
		// Returns a new sender, or nullptr if registration failed.
		TUniquePtr<ISender> RegisterNewSender(const FSendAddress& InAddress, const FSenderInitParams& InitParams);

		// Creates a new object to push data to an address.
		// Returns a new sender, or nullptr if registration failed.
		template<typename TDataType>
		TSenderPtr<TDataType> RegisterNewSender(const FSendAddress& InAddress, const FSenderInitParams& InitParams)
		{
			TSenderPtr<TDataType> Sender;
			
			const bool bIsMatchingDataType = GetMetasoundDataTypeName<TDataType>() == InAddress.GetDataType();
			if (ensure(bIsMatchingDataType))
			{
				TUniquePtr<ISender> SenderBase = RegisterNewSender(InAddress, InitParams);

				if (SenderBase.IsValid())
				{
					Sender = Downcast<TDataType>(MoveTemp(SenderBase));
				}
			}

			return Sender;
		}

		// Registers a new object to poll data from an address.
		// Returns a new receiver, or nullptr if registration failed.
		TUniquePtr<IReceiver> RegisterNewReceiver(const FSendAddress& InAddress, const FReceiverInitParams& InitParams);

		// Registers a new object to poll data from an address.
		// Returns a new receiver, or nullptr if registration failed.
		template<typename TDataType>
		TReceiverPtr<TDataType> RegisterNewReceiver(const FSendAddress& InAddress, const FReceiverInitParams& InitParams)
		{
			TReceiverPtr<TDataType> Receiver;

			const bool bIsMatchingDataType = GetMetasoundDataTypeName<TDataType>() == InAddress.GetDataType();
			if (ensure(bIsMatchingDataType))
			{
				TUniquePtr<IReceiver> ReceiverBase = RegisterNewReceiver(InAddress, InitParams);

				if (ReceiverBase.IsValid())
				{
					Receiver = Downcast<TDataType>(MoveTemp(ReceiverBase));
				}
			}

			return Receiver;
		}

		// Unregisters DataChannel irrespective of number of receivers or senders still active.
		bool UnregisterDataChannel(const FSendAddress& InAddress);

		// Unregister a data channel if there are no senders or receivers
		bool UnregisterDataChannelIfUnconnected(const FSendAddress& InAddress);

		// Pushes a literal parameter to a specific data channel in the global router.
		// returns false if the literal type isn't supported.
		bool PushLiteral(FName DataTypeName, FName GlobalChannelName, const FLiteral& InParam);

	private:
		// Single map of FNames to IDataChannels
		FAddressRouter GlobalRouter;

		FDataTransmissionCenter() = default;
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FSendAddress, METASOUNDFRONTEND_API, FSendAddressTypeInfo, FSendAddressReadRef, FSendAddressWriteRef)
}
