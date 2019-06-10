// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace Chaos
{
	enum class EMultiBufferMode : uint8
	{
		Undefined = 0,
		Single,
		Double,
		Triple
	};

	template <typename ResourceType>
	class IBufferResource
	{
	public:

		virtual ~IBufferResource() {}

		virtual EMultiBufferMode GetBufferMode() = 0;
		virtual ResourceType* AccessProducerBuffer() = 0;
		virtual const ResourceType* GetProducerBuffer() const = 0;
		virtual const ResourceType* GetConsumerBuffer() const = 0;
		virtual void FlipProducer() = 0;

		// Required because we have multiple game side accessors vs a free running physics thread
		// In the case of events it's not good enough to just supply the latest data as we would
		// miss events if the physics thread happens to tick multiple times before a game system
		// has time to read it
		virtual const ResourceType* GetSyncConsumerBuffer() const = 0;
		virtual void SyncGameThread() = 0;
	};

	//////////////////////////////////////////////////////////////////////////
	/**
	 * Single Buffer Implementation
	 */
	template <typename ResourceType>
	class FSingleBuffer : public IBufferResource<ResourceType>
	{
	public:

		FSingleBuffer() {}
		virtual ~FSingleBuffer() {}

		virtual EMultiBufferMode GetBufferMode() override { return EMultiBufferMode::Single; }
		virtual ResourceType* AccessProducerBuffer() override { return &Data; }
		virtual const ResourceType* GetProducerBuffer() const override { return &Data; }
		virtual const ResourceType* GetConsumerBuffer() const override { return &Data; }

		void FlipProducer() override { /* NOP */ }

		virtual const ResourceType* GetSyncConsumerBuffer() const override { return &Data; }
		virtual void SyncGameThread() override { /* NOP */ }

	private:

		ResourceType Data;
	};
	//////////////////////////////////////////////////////////////////////////
	/*
	* Double Buffer Implementation - Not thread-safe requires external locks
	*/
	template <typename ResourceType>
	class FDoubleBuffer : public IBufferResource<ResourceType>
	{
	public:

		FDoubleBuffer() 
			: Data_Producer(&Data1)
			, Data_Consumer(&Data2)
		{}
		virtual ~FDoubleBuffer() {}

		virtual EMultiBufferMode GetBufferMode() override { return EMultiBufferMode::Double; }
		virtual ResourceType* AccessProducerBuffer() override { return Data_Producer; }
		virtual const ResourceType* GetProducerBuffer() const override { return Data_Producer; }
		virtual const ResourceType* GetConsumerBuffer() const override { return Data_Consumer; }

		void FlipProducer()
		{
			if (Data_Producer == &Data1)
			{
				Data_Producer = &Data2;
				Data_Consumer = &Data1;
			}
			else
			{
				Data_Producer = &Data1;
				Data_Consumer = &Data2;
			}

			Data_Producer->Reset();
		}

		virtual const ResourceType* GetSyncConsumerBuffer() const override { return &GameSyncData; }
		virtual void SyncGameThread() override { GameSyncData = *Data_Consumer; }

	private:

		ResourceType Data1;
		ResourceType Data2;
		ResourceType* Data_Producer;
		ResourceType* Data_Consumer;

		ResourceType GameSyncData;
	};
	//////////////////////////////////////////////////////////////////////////
	/*
	* Triple Buffer Implementation - Not thread-safe requires external locks
	*/
	template <typename ResourceType>
	class FTripleBuffer : public IBufferResource<ResourceType>
	{
	public:

		FTripleBuffer()
			: WriteIndex(1)
			, ReadIndex(0)
			, NextReadIndex(0)
		{
		}
		virtual ~FTripleBuffer() {}

		virtual EMultiBufferMode GetBufferMode() override { return EMultiBufferMode::Triple; }
		virtual ResourceType* AccessProducerBuffer() override { return &Data[GetWriteIndex()]; }
		virtual const ResourceType* GetProducerBuffer() const override { return &Data[GetWriteIndex()]; }
		virtual const ResourceType* GetConsumerBuffer() const override { return &Data[GetReadIndex()]; }

		virtual void FlipProducer() override
		{
			int32 CurrentReadIdx = GetReadIndex();
			int32 CurrentWriteIdx = GetWriteIndex();
			int32 FreeIdx = 3 - (CurrentReadIdx + CurrentWriteIdx);

			NextReadIndex.Store(CurrentWriteIdx);
			WriteIndex.Store(FreeIdx);

			AccessProducerBuffer()->Reset();

			checkSlow(GetReadIndex() != GetWriteIndex());
		}

		virtual const ResourceType* GetSyncConsumerBuffer() const override { return &GameSyncData; }
		virtual void SyncGameThread() override 
		{ 
			checkSlow(GetReadIndex() != GetWriteIndex());

			ReadIndex.Store(NextReadIndex.Load());
			GameSyncData = Data[GetReadIndex()];
		}

	private:
		int32 GetWriteIndex() const { return WriteIndex.Load(); }
		int32 GetReadIndex() const { return ReadIndex.Load(); }

		ResourceType Data[3];
		TAtomic<int32> WriteIndex;
		TAtomic<int32> ReadIndex;
		TAtomic<int32> NextReadIndex;

		ResourceType GameSyncData;
	};	
	//////////////////////////////////////////////////////////////////////////


	template<typename ResourceType>
	class FMultiBufferFactory
	{
	public:
		static TUniquePtr<IBufferResource<ResourceType>> CreateBuffer(const EMultiBufferMode BufferMode)
		{
			switch (BufferMode)
			{
			case EMultiBufferMode::Single:
			{
				return MakeUnique<FSingleBuffer<ResourceType>>();
			}
			break;

			case EMultiBufferMode::Double:
			{
				return MakeUnique<FDoubleBuffer<ResourceType>>();
			}
			break;

			case EMultiBufferMode::Triple:
			{
				return MakeUnique<FTripleBuffer<ResourceType>>();
			}
			break;

			default:
				checkf(false, TEXT("FMultiBufferFactory Unexpected buffer mode"));
			break;

			}

			return nullptr;
		}
	};

} // namespace Chaos
