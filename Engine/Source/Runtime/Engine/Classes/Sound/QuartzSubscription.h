// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Containers/MpscQueue.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"
#include "Quartz/AudioMixerClock.h"
#include "Sound/QuartzQuantizationUtilities.h"


// forwards
class UQuartzClockHandle;
class UQuartzSubsystem;

namespace Audio
{
	class FQuartzClockProxy;

	// Struct used to communicate command state back to the game play thread
	struct ENGINE_API FQuartzQuantizedCommandDelegateData : public FQuartzCrossThreadMessage
	{
		EQuartzCommandType CommandType;
		EQuartzCommandDelegateSubType DelegateSubType;

		// ID so the clock handle knows which delegate to fire
		int32 DelegateID{ -1 };

	}; // struct FQuartzQuantizedCommandDelegateData

	// Struct used to communicate metronome events back to the game play thread
	struct ENGINE_API FQuartzMetronomeDelegateData : public FQuartzCrossThreadMessage
	{
		int32 Bar;
		int32 Beat;
		float BeatFraction;
		EQuartzCommandQuantization Quantization;
		FName ClockName;
	}; // struct FQuartzMetronomeDelegateData

	//Struct used to queue events to be sent to the Audio Render thread closer to their start time
	struct ENGINE_API FQuartzQueueCommandData : public FQuartzCrossThreadMessage
	{
		FAudioComponentCommandInfo AudioComponentCommandInfo;
		FName ClockName;

		FQuartzQueueCommandData(const FAudioComponentCommandInfo& InAudioComponentCommandInfo, FName InClockName);
	}; // struct FQuartzQueueCommandData


	// old non-template\ version of TQuartzShareableCommandQueue
	class UE_DEPRECATED(5.1, "Message") FShareableQuartzCommandQueue;
	class ENGINE_API FShareableQuartzCommandQueue
	{
	};

	/**
	*	Template class for mono-directional MPSC command queues 
	* 
	*   in order to enforce thread-safe access to the object executing the commands,
	*	"listener type" is the type of the object that is being accessed in the commands
	*	that object will have to provide a 'this' ptr (of type ListenerType) in order to 
	*   invoke the commands on itself. (The lambdas do NOT and should NOT cache a ptr or
	*	reference to the target).
	* 
	*	User-provided lambdas can take any (single) argument type T in PushEvent()
	*	but there must exist a ListenerType::ExecCommand(T) overload for any PushEvent(T) instantiated. 
	* 
	*	(see FQuartzTickableObject and FQuartzClock as examples)
	* 
	**/
	template <class ListenerType>
	class ENGINE_API TQuartzShareableCommandQueue
	{
	public:

		// ctor
		TQuartzShareableCommandQueue() {}

		// dtor
		~TQuartzShareableCommandQueue() {}

		// static helper to create a new sharable queue
		static TSharedPtr<TQuartzShareableCommandQueue<ListenerType>, ESPMode::ThreadSafe> Create()
		{
			return MakeShared<TQuartzShareableCommandQueue<ListenerType>, ESPMode::ThreadSafe>();
		}

		// note: ListenerType must have a ExecCommand() overload for each instantiation of this method
		template <typename T>
		void PushEvent(const T& Data)
		{
			CommandQueue.Enqueue([InData = Data](ListenerType* Listener) { Listener->ExecCommand(InData); });
		}

		void PushCommand(TFunction<void(ListenerType*)> InCommand)
		{
			CommandQueue.Enqueue([=](ListenerType* Listener) { InCommand(Listener); });
		}

		void PumpCommandQueue(ListenerType* InListener)
		{
			// gather move all the current commands into our temp container
			// (in case the commands themselves alter the container)
			TOptional<TFunction<void(ListenerType*)>> Function = CommandQueue.Dequeue();
			while (Function.IsSet())
			{
				TempCommandQueue.Emplace(Function.GetValue());
				Function = CommandQueue.Dequeue();
			}

			for (auto& Command : TempCommandQueue)
			{
				Command(InListener);
			}

			TempCommandQueue.Reset();
		}

	private:
		TMpscQueue<TFunction<void(ListenerType*)>> CommandQueue;
		TArray<TFunction<void(ListenerType*)>> TempCommandQueue;
	};

} // namespace Audio


	/**
	 *	FQuartzTickableObject
	 *
	 *		This is the base class for non-Audio Render Thread objects that want to receive
	 *		callbacks for Quartz events.
	 *
	 *		It is a wrapper around TQuartzShareableCommandQueue.
	 *		(see UQuartzClockHandle or UAudioComponent as implementation examples)
	 */
	class ENGINE_API FQuartzTickableObject
	{
		struct ENGINE_API FQuartzTickableObjectGCObjectMembers : public FGCObject
		{
			UQuartzSubsystem* QuartzSubsystem{ nullptr };

			UWorld* WorldPtr{ nullptr };

			// Begin FGCObject Interface
			virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
			virtual FString GetReferencerName() const override;
			// End FGCObject Interface

			bool IsValid() const { return QuartzSubsystem && WorldPtr; }
		}; // struct FQuartzTickableObjectGCObjectMembers

	public:
		// ctor
		FQuartzTickableObject() {}

		// dtor
		virtual ~FQuartzTickableObject();

		FQuartzTickableObject* Init(UWorld* InWorldPtr);

		// called by the associated QuartzSubsystem
		void QuartzTick(float DeltaTime);
		
		bool QuartzIsTickable() const;
		
		// maxtodo: deprecate, derived classes should have their own world
		UWorld* QuartzGetWorld() const { return GCObjectMembers.WorldPtr; }

		void AddMetronomeBpDelegate(EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

		bool IsInitialized() const { return GCObjectMembers.IsValid(); }

		TWeakPtr<Audio::TQuartzShareableCommandQueue<FQuartzTickableObject>, ESPMode::ThreadSafe> GetCommandQueue();

		int32 AddCommandDelegate(const FOnQuartzCommandEventBP& InDelegate, TArray<TSharedPtr<Audio::TQuartzShareableCommandQueue<FQuartzTickableObject>, ESPMode::ThreadSafe>>& TargetSubscriberArray);

		// maxtodo: deprecate (use the static getter on the subsystem class)
		UQuartzSubsystem* GetQuartzSubsystem() const;

		// required by TQuartzShareableCommandQueue template
		void ExecCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data);
		void ExecCommand(const Audio::FQuartzMetronomeDelegateData& Data);
		void ExecCommand(const Audio::FQuartzQueueCommandData& Data);

		// virtual interface (ExecCommand will forward the data to derived classes' ProcessCommand() call)
		virtual void ProcessCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data) {}
		virtual void ProcessCommand(const Audio::FQuartzMetronomeDelegateData& Data) {}
		virtual void ProcessCommand(const Audio::FQuartzQueueCommandData& Data) {}

	private:
		struct FMetronomeDelegateGameThreadData
		{
			FOnQuartzMetronomeEvent MulticastDelegate;
		};

		struct FCommandDelegateGameThreadData
		{
			FOnQuartzCommandEvent MulticastDelegate;
			FThreadSafeCounter RefCount;
		};

		// delegate containers
		FMetronomeDelegateGameThreadData MetronomeDelegates[static_cast<int32>(EQuartzCommandQuantization::Count)];
		TArray<FCommandDelegateGameThreadData> QuantizedCommandDelegates;

		// command queue
		TSharedPtr<Audio::TQuartzShareableCommandQueue<FQuartzTickableObject>, ESPMode::ThreadSafe> CommandQueue;
		TArray<TFunction<void(FQuartzTickableObject*)>> TempCommandQueue;

	public:
		// deprecate public access
		UE_DEPRECATED(5.1, "FQuartzTickableObject::CommandQueuePtr should no longer be accessed directly. (This TSharedPtr will always be invalid).  Use Use GetCommandQueue().Pin() instead")
		TSharedPtr<Audio::TQuartzShareableCommandQueue<FQuartzTickableObject>, ESPMode::ThreadSafe> CommandQueuePtr;

	private:
		// gc object members struct (USubsystem / world )
		FQuartzTickableObjectGCObjectMembers GCObjectMembers;

	}; // class FQuartzTickableObject

