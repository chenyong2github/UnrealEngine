// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/System.h"

#include "MuR/Settings.h"
#include "MuR/Operations.h"
#include "MuR/ImagePrivate.h"
#include "MuR/MutableString.h"
#include "MuR/ModelPrivate.h"
#include "MuR/MeshPrivate.h"
#include "MuR/InstancePrivate.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/MutableTrace.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "HAL/Thread.h"
#include "HAL/PlatformTLS.h"
#endif

#define UE_MUTABLE_CACHE_COUNT_LIMIT	3000000

namespace mu
{
	class ExtensionDataStreamer;

	// Call the tick of the LLM system (we do this to simulate a frame since the LLM system is not entirelly designed to run over a program)
	inline void UpdateLLMStats()
	{
		// This code will only be compiled (and ran) if the global definition to enable LLM tracking is set to 1 for the host program
		// Ex : 			GlobalDefinitions.Add("LLM_ENABLED_IN_CONFIG=1");
#if ENABLE_LOW_LEVEL_MEM_TRACKER && IS_PROGRAM
		FLowLevelMemTracker& MemTracker = FLowLevelMemTracker::Get();
		if (MemTracker.IsEnabled())
		{
			MemTracker.UpdateStatsPerFrame();
		}
#endif
	}

	constexpr uint64 AllParametersMask = TNumericLimits<uint64> ::Max();


	//! Reference-counted colour to be stored in the cache
	class Colour : public Resource
	{
	public:
		Colour(FVector4f v = FVector4f()) : m_colour(v) {}
		FVector4f m_colour;
		int32 GetDataSize() const override { return sizeof(Colour); }
	};
	typedef Ptr<Colour> ColourPtr;


	//! Reference-counted bool to be stored in the cache
	class Bool : public Resource
	{
	public:
		Bool(bool v = false) : m_value(v) {}
		bool m_value;
		int32 GetDataSize() const override { return sizeof(Bool); }
	};
	typedef Ptr<Bool> BoolPtr;


	//! Reference-counted scalar to be stored in the cache
	class Scalar : public Resource
	{
	public:
		Scalar(float v = 0.0f) : m_value(v) {}
		float m_value;
		int32 GetDataSize() const override { return sizeof(Scalar); }
	};
	typedef Ptr<Scalar> ScalarPtr;


	//! Reference-counted scalar to be stored in the cache
	class Int : public Resource
	{
	public:
		Int(int32 v = 0) : m_value(v) {}
		int32 m_value;
		int32 GetDataSize() const override { return sizeof(Int); }
	};
	typedef Ptr<Int> IntPtr;


	//! Reference-counted scalar to be stored in the cache
	class Projector : public Resource
	{
	public:
		FProjector m_value;
		int32 GetDataSize() const override { return sizeof(Projector); }
	};
	typedef Ptr<Projector> ProjectorPtr;


	/** ExecutinIndex stores the location inside all ranges of the execution of a specific
	* operation. The first integer on each pair is the dimension/range index in the program
	* array of ranges, and the second integer is the value inside that range.
	* The vector order is undefined.
	*/
	class ExecutionIndex : public  TArray<TPair<int32, int32>>
	{
	public:
		//! Set or add a value to the index
		void SetFromModelRangeIndex(uint16 rangeIndex, int rangeValue)
		{
			auto Index = IndexOfByPredicate([=](const ElementType& v) { return v.Key >= rangeIndex; });
			if (Index != INDEX_NONE && (*this)[Index].Key == rangeIndex)
			{
				// Update
				(*this)[Index].Value = rangeValue;
			}
			else
			{
				// Add new
				Push(ElementType(rangeIndex, rangeValue));
			}
		}

		//! Get the value of the index from the range index in the model.
		int GetFromModelRangeIndex(int modelRangeIndex) const
		{
			for (const auto& e : *this)
			{
				if (e.Key == modelRangeIndex)
				{
					return e.Value;
				}
			}
			return 0;
		}
	};


	/** This structure stores the data about an ongoing mutable operation that needs to be executed. */
	struct FScheduledOp
	{
		inline FScheduledOp()
		{
			Stage = 0;
			Type = EType::Full;
		}

		inline FScheduledOp(OP::ADDRESS InAt, const FScheduledOp& InOpTemplate, uint8 InStage = 0, uint32 InCustomState = 0)
		{
			check(InStage < 120);
			At = InAt;
			ExecutionOptions = InOpTemplate.ExecutionOptions;
			ExecutionIndex = InOpTemplate.ExecutionIndex;
			Stage = InStage;
			CustomState = InCustomState;
			Type = InOpTemplate.Type;
		}

		static inline FScheduledOp FromOpAndOptions(OP::ADDRESS InAt, const FScheduledOp& InOpTemplate, uint8 InExecutionOptions)
		{
			FScheduledOp r;
			r.At = InAt;
			r.ExecutionOptions = InExecutionOptions;
			r.ExecutionIndex = InOpTemplate.ExecutionIndex;
			r.Stage = 0;
			r.CustomState = InOpTemplate.CustomState;
			r.Type = InOpTemplate.Type;
			return r;
		}

		//! Address of the operation
		OP::ADDRESS At = 0;

		//! Additional custom state data that the operation can store. This is usually used to pass information
		//! between execution stages of an operation.
		uint32 CustomState = 0;

		//! Index of the operation execution: This is used for iteration of different ranges.
		//! It is an index into the CodeRunner::GetMemory()::m_rangeIndex vector.
		//! executionIndex 0 is always used for empty ExecutionIndex, which is the most common
		//! one.
		uint16 ExecutionIndex = 0;

		//! Additional execution options. Set externally to this op, it usually alters the result.
		//! For example, this is used to keep track of the mipmaps to skip in image operations.
		uint8 ExecutionOptions = 0;

		//! Internal stage of the operation.
		//! Stage 0 is usually scheduling of children, and 1 is execution. Some instructions
		//! may have more steges to schedule children that are optional for execution, etc.
		uint8 Stage : 7;

		//! Type of calculation we are requesting for this operation.
		enum class EType : uint8
		{
			//! Execute the operation to calculate the full result
			Full,

			//! Execute the operation to obtain the descriptor of an image.
			ImageDesc
		};
		EType Type : 1;
	};

	inline uint32 GetTypeHash(const FScheduledOp& Op)
	{
		return HashCombine(::GetTypeHash(Op.At), HashCombine(::GetTypeHash(Op.Stage), ::GetTypeHash(Op.ExecutionIndex)));
	}


	/** A cache address is the operation plus the context of execution(iteration indices, etc...). */
	struct FCacheAddress
	{
		/** The meaning of all these fields is the same than the FScheduledOp struct. */
		OP::ADDRESS At = 0;
		uint16 ExecutionIndex = 0;
		uint8 ExecutionOptions = 0;
		FScheduledOp::EType Type = FScheduledOp::EType::Full;

		FCacheAddress() {}

		FCacheAddress(OP::ADDRESS InAt, uint16 InExecutionIndex, uint8 InExecutionOptions)
		{
			At = InAt;
			ExecutionIndex = InExecutionIndex;
			ExecutionOptions = InExecutionOptions;
		}

		FCacheAddress(OP::ADDRESS InAt, const FScheduledOp& Item)
		{
			At = InAt;
			ExecutionIndex = Item.ExecutionIndex;
			ExecutionOptions = Item.ExecutionOptions;
			Type = Item.Type;
		}

		FCacheAddress(const FScheduledOp& Item)
		{
			At = Item.At;
			ExecutionIndex = Item.ExecutionIndex;
			ExecutionOptions = Item.ExecutionOptions;
			Type = Item.Type;
		}
	};


	inline uint32 GetTypeHash(const FCacheAddress& a)
	{
		return HashCombine(::GetTypeHash(a.At), a.ExecutionIndex);
	}


	inline uint32 GetTypeHash(const Ptr<const Resource>& V)
	{
		return ::GetTypeHash(V.get());
	}


	//! Container that stores data per executable code operation (indexed by address and execution
	//! index).
	template<class DATA>
	class CodeContainer
	{
	public:

		void resize(size_t s)
		{
			m_index0.SetNum(s);
		}

		uint32 size_code() const
		{
			return uint32(m_index0.Num());
		}

		void clear()
		{
			m_index0.Empty();
			m_otherIndex.Empty();
		}

		inline void erase(const FCacheAddress& at)
		{
			if (at.ExecutionIndex == 0 && at.ExecutionOptions == 0)
			{
				m_index0[at.At] = nullptr;
			}
			else
			{
				m_otherIndex.Remove(at);
			}
		}

		inline DATA get(const FCacheAddress& at) const
		{
			if (at.ExecutionIndex == 0 && at.ExecutionOptions == 0)
			{
				if (at.At < uint32(m_index0.Num()))
				{
					return m_index0[at.At];
				}
				else
				{
					return 0;
				}
			}
			else
			{
				const DATA* it = m_otherIndex.Find(at);
				if (it)
				{
					return *it;
				}
			}
			return 0;
		}

		inline const DATA* get_ptr(const FCacheAddress& at) const
		{
			if (at.ExecutionIndex == 0 && at.ExecutionOptions == 0)
			{
				if (at.At < uint32(m_index0.Num()))
				{
					return &m_index0[at.At];
				}
				else
				{
					return nullptr;
				}
			}
			else
			{
				const DATA* it = m_otherIndex.Find(at);
				if (it)
				{
					return it;
				}
			}
			return nullptr;
		}

		inline DATA& operator[](const FCacheAddress& at)
		{
			if (at.ExecutionIndex == 0 && at.ExecutionOptions == 0)
			{
				return m_index0[at.At];
			}
			else
			{
				return m_otherIndex.FindOrAdd(at);
			}
		}

		struct iterator
		{
		private:
			friend class CodeContainer<DATA>;
			const CodeContainer<DATA>* container;
			typename TArray<DATA>::TIterator it0;
			typename TMap<FCacheAddress, DATA>::TIterator it1;

			iterator(CodeContainer<DATA>* InContainer)
				: container(InContainer)
				, it0(InContainer->m_index0.CreateIterator())
				, it1(InContainer->m_otherIndex.CreateIterator())
			{
			}

		public:

			inline void operator++(int)
			{
				if (it0)
				{
					++it0;
				}
				else
				{
					++it1;
				}
			}

			iterator operator++()
			{
				if (it0)
				{
					++it0;
				}
				else
				{
					++it1;
				}
				return *this;
			}

			inline bool operator!=(const iterator& o) const
			{
				return it0 != o.it0 || it1 != o.it1;
			}

			inline DATA& operator*()
			{
				if (it0)
				{
					return *it0;
				}
				else
				{
					return it1->Value;
				}
			}

			inline FCacheAddress get_address() const
			{
				if (it0)
				{
					return { uint32_t(it0.GetIndex()), 0, 0};
				}
				else
				{
					return it1->Key;
				}
			}

			inline bool IsValid() const
			{
				return bool(it0) || bool(it1);
			}
		};

		inline iterator begin()
		{
			iterator it(this);
			return it;
		}

		inline int32 GetAllocatedSize() const
		{
			return m_index0.GetAllocatedSize()
				+ m_otherIndex.GetAllocatedSize();
		}

	private:
		// For index 0
		TArray<DATA> m_index0;

		// For index>0
		TMap<FCacheAddress, DATA> m_otherIndex;
	};


	/** Interface for storage of data while Mutable code is being executed. */
	class FProgramCache
	{
	public:

		TArray< ExecutionIndex, TInlineAllocator<4> > m_usedRangeIndices;

		/** Cached resources while the program is executing. 
		* first value of the pair:
		* 0 : value not valid (not set).
		* 1 : valid, not worth freeing for memory
		* 2 : valid, worth freeing
		*/
		CodeContainer< TPair<int32, Ptr<const Resource>> > m_resources;

		/** Addressed with OP::ADDRESS.It is true if value for an image desc is valid. */
		TArray<bool> m_descCache;

		/** The number operation stages waiting for the output of a specific operation.
		 * \todo: this could be optimised by merging in other CodeContainers here.
		 */
		CodeContainer<int32> m_opHitCount;


		inline const ExecutionIndex& GetRangeIndex(uint32_t i)
		{
			// Make sure we have the default element.
			if (m_usedRangeIndices.IsEmpty())
			{
				m_usedRangeIndices.Push(ExecutionIndex());
			}

			check(i < uint32_t(m_usedRangeIndices.Num()));
			return m_usedRangeIndices[i];
		}

		//!
		inline uint32_t GetRangeIndexIndex(const ExecutionIndex& rangeIndex)
		{
			if (rangeIndex.IsEmpty())
			{
				return 0;
			}

			// Make sure we have the default element.
			if (m_usedRangeIndices.IsEmpty())
			{
				m_usedRangeIndices.Push(ExecutionIndex());
			}

			// Look for or add the new element.
			auto ElemIndex = m_usedRangeIndices.Find(rangeIndex);
			if (ElemIndex != INDEX_NONE)
			{
				return ElemIndex;
			}

			m_usedRangeIndices.Push(rangeIndex);
			return uint32_t(m_usedRangeIndices.Num()) - 1;
		}

		void Init(size_t size)
		{
			// This prevents live update cache reusal
			//m_resources.clear();
			m_resources.resize(size);
			m_opHitCount.resize(size);
		}

		void SetUnused(FCacheAddress at)
		{
			//UE_LOG(LogMutableCore, Log, TEXT("memory SetUnused : %5d "), at.At);
			if (m_resources[at].Key >= 2)
			{
				// Keep the result anyway if it doesn't use any memory.
				if (m_resources[at].Value)
				{
					m_resources[at].Value = nullptr;
					m_resources[at].Key = 0;
				}
			}
		}


		bool IsValid(FCacheAddress at) const
		{
			if (at.At == 0) return false;

			// Is it a desc data query?
			if (at.Type == FScheduledOp::EType::ImageDesc)
			{
				if (uint32(m_descCache.Num()) > at.At)
				{
					return m_descCache[at.At];
				}
				else
				{
					return false;
				}
			}

			// It's a full data query.
			auto d = m_resources.get_ptr(at);
			return d && d->Key != 0;
		}


		/** Remove all intermediate data (big and small) from the memory except for the one that has been explicitely
		* marked as state cache.
		*/
		void CheckHitCountsCleared()
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			//MUTABLE_CPUPROFILER_SCOPE(CheckHitCountsCleared);

			//int32 IncorrectCount = 0;
			//CodeContainer<int>::iterator it = m_opHitCount.begin();
			//for (; it.IsValid(); ++it)
			//{
			//	int32 Count = *it;
			//	if (Count>0 && Count < UE_MUTABLE_CACHE_COUNT_LIMIT)
			//	{
			//		// We don't manage the hitcounts of small types that don't use much memory
			//		if (m_resources[it.get_address()].Key==2)
			//		{
			//			// Op hitcount should have reached 0, otherwise, it means we requested an operation but never read 
			//			// the result.
			//			++IncorrectCount;
			//		}
			//	}
			//}

			//if (IncorrectCount > 0)
			//{
			//	UE_LOG(LogMutableCore, Log, TEXT("The op-hit-count didn't hit 0 for %5d operations. This may mean that too much memory is cached."), IncorrectCount);
			//	//check(false);
			//}
#endif
		}


		void Clear()
		{
			MUTABLE_CPUPROFILER_SCOPE(ProgramCacheClear);

			size_t codeSize = m_resources.size_code();
			m_resources.clear();
			m_resources.resize(codeSize);
			m_descCache.SetNum(0);
			m_opHitCount.clear();
			m_opHitCount.resize(codeSize);
		}


		bool GetBool(FCacheAddress at)
		{
			if (!at.At) return false;
			auto d = m_resources.get_ptr(at);
			if (!d) return false;

			Ptr<const Bool> pResult;
			if (at.At)
			{
				pResult = (const Bool*)d->Value.get();
			}
			return pResult ? pResult->m_value : false;
		}

		float GetScalar(FCacheAddress at)
		{
			if (!at.At) return 0.0f;
			auto d = m_resources.get_ptr(at);
			if (!d) return 0.0f;

			Ptr<const Scalar> pResult;
			if (at.At)
			{
				pResult = (const Scalar*)d->Value.get();
			}
			return pResult ? pResult->m_value : 0.0f;
		}

		int GetInt(FCacheAddress at)
		{
			if (!at.At) return 0;
			auto d = m_resources.get_ptr(at);
			if (!d) return 0;

			Ptr<const Int> pResult;
			if (at.At)
			{
				pResult = (const Int*)d->Value.get();
			}
			return pResult ? pResult->m_value : 0;
		}

		FVector4f GetColour(FCacheAddress at)
		{
			if (!at.At) return FVector4f();
			auto d = m_resources.get_ptr(at);
			if (!d) return FVector4f();

			Ptr<const Colour> pResult;
			if (at.At)
			{
				pResult = (const Colour*)d->Value.get();
			}
			return pResult ? pResult->m_colour : FVector4f();
		}

		Ptr<const Projector> GetProjector(FCacheAddress at)
		{
			if (!at.At) return nullptr;
			auto d = m_resources.get_ptr(at);
			if (!d) return nullptr;

			Ptr<const Projector> pResult;
			if (at.At)
			{
				pResult = (const Projector*)d->Value.get();
			}
			return pResult;
		}

		Ptr<const Instance> GetInstance(FCacheAddress at)
		{
			if (!at.At) return nullptr;
			auto d = m_resources.get_ptr(at);
			if (!d) return nullptr;

			Ptr<const Instance> pResult = (const Instance*)d->Value.get();

			// We need to decrease the hitcount even if the result is null.
			// Lower hit counts means we shouldn't clear the value
			if (m_opHitCount[at] > 0)
			{
				--m_opHitCount[at];

				if (m_opHitCount[at] <= 0)
				{
					SetUnused(at);
				}
			}

			return pResult;
		}


		Ptr<const Layout> GetLayout(FCacheAddress at)
		{
			if (!at.At)
			{
				return nullptr;
			}
			auto d = m_resources.get_ptr(at);
			if (!d) return nullptr;

			Ptr<const Layout> pResult;
			if (at.At)
			{
				pResult = (const Layout*)d->Value.get();
			}
			return pResult;
		}

		Ptr<const String> GetString(FCacheAddress at)
		{
			if (!at.At)
			{
				return nullptr;
			}
			auto d = m_resources.get_ptr(at);
			if (!d)
				return nullptr;

			Ptr<const String> pResult;
			if (at.At)
			{
				pResult = (const String*)d->Value.get();
			}
			return pResult;
		}

		Ptr<const ExtensionData> GetExtensionData(FCacheAddress at)
		{
			if (!at.At)
			{
				return nullptr;
			}
			
			const TPair<int, Ptr<const Resource>>* d = m_resources.get_ptr(at);
			if (!d)
			{
				return nullptr;
			}

			Ptr<const ExtensionData> pResult;
			if (at.At)
			{
				pResult = (const ExtensionData*)d->Value.get();
			}
			return pResult;
		}

		void SetBool(FCacheAddress at, bool v)
		{
			check(at.At < m_resources.size_code());

			Ptr<Bool> pResult = new Bool;
			pResult->m_value = v;
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, pResult);
		}

		void SetValidDesc(FCacheAddress at)
		{
			check(at.Type == FScheduledOp::EType::ImageDesc);
			check(at.At < uint32(m_descCache.Num()));

			m_descCache[at.At] = true;
		}

		void SetScalar(FCacheAddress at, float v)
		{
			check(at.At < m_resources.size_code());

			Ptr<Scalar> pResult = new Scalar;
			pResult->m_value = v;
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, pResult);
		}

		void SetInt(FCacheAddress at, int v)
		{
			check(at.At < m_resources.size_code());

			Ptr<Int> pResult = new Int;
			pResult->m_value = v;
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, pResult);
		}

		void SetColour(FCacheAddress at, const FVector4f& v)
		{
			check(at.At < m_resources.size_code());

			Ptr<Colour> pResult = new Colour;
			pResult->m_colour = v;
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, pResult);
		}

		void SetProjector(FCacheAddress at, Ptr<const Projector> v)
		{
			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, v);
		}

		void SetInstance(FCacheAddress at, Ptr<const Instance> v)
		{
			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(2, v);
		}

		void SetExtensionData(FCacheAddress at, Ptr<const ExtensionData> v)
		{
			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, v);
		}

		void SetImage(FCacheAddress at, Ptr<const Image> v)
		{
			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(2, v);

			mu::UpdateLLMStats();
		}

		void SetMesh(FCacheAddress at, Ptr<const Mesh> v)
		{
			// debug
//            if (v)
//            {
//                v->GetPrivate()->CheckIntegrity();
//            }

			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(2, v);

			mu::UpdateLLMStats();
		}

		void SetLayout(FCacheAddress at, Ptr<const Layout> v)
		{
			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, v);

			mu::UpdateLLMStats();
		}

		void SetString(FCacheAddress at, Ptr<const String> v)
		{
			check(at.At < m_resources.size_code());
			m_resources[at] = TPair<int, Ptr<const Resource>>(1, v);
		}


		inline void IncreaseHitCount(FCacheAddress at)
		{
			// Don't count hits for instruction 0, which is always null. It is usually already
			// check that At is not 0, and then it is not requested, generating a stray non-zero count
			// at its position.
			if (at.At)
			{
				m_opHitCount[at] += 1;
			}
		}

		inline void SetForceCached(OP::ADDRESS at)
		{
			// \TODO: Review the ,0,0
			m_opHitCount[{at, 0, 0}] = 0xffffff;
		}


		Ptr<const Image> GetImage(FCacheAddress at, bool& bIsLastReference)
		{
			bIsLastReference = false;

			if (!at.At) return nullptr;
			if (size_t(at.At) >= m_resources.size_code()) return nullptr;
			auto d = m_resources.get_ptr(at);
			if (!d) return nullptr;

			Ptr<const Image> Result = (const Image*)d->Value.get();

			// We need to decrease the hit count (even if the result is null).
			// Lower hit counts means we shouldn't clear the value
			if ( m_opHitCount[at] > 0)
			{
				--m_opHitCount[at];
				if (m_opHitCount[at] <= 0)
				{
					SetUnused(at);
					bIsLastReference = true;
				}
			}

			return Result;
		}

		Ptr<const Mesh> GetMesh(FCacheAddress at)
		{
			if (!at.At) return nullptr;
			if (size_t(at.At) >= m_resources.size_code()) return nullptr;
			auto d = m_resources.get_ptr(at);
			if (!d) return nullptr;

			Ptr<const Mesh> pResult = (const Mesh*)d->Value.get();

			// We need to decrease the hitcount even if the result is null.
			// Lower hit counts means we shouldn't clear the value
			if ( m_opHitCount[at] > 0)
			{
				--m_opHitCount[at];

				if (m_opHitCount[at] <= 0)
				{
					SetUnused(at);
				}
			}

			return pResult;
		}
	};


	inline bool operator==(const FCacheAddress& a, const FCacheAddress& b)
	{
		return a.At == b.At
			&&
			a.ExecutionIndex == b.ExecutionIndex
			&&
			a.ExecutionOptions == b.ExecutionOptions
			&&
			a.Type == b.Type;
	}

	inline bool operator<(const FCacheAddress& a, const FCacheAddress& b)
	{
		if (a.At < b.At) return true;
		if (a.At > b.At) return false;
		if (a.ExecutionIndex < b.ExecutionIndex) return true;
		if (a.ExecutionIndex > b.ExecutionIndex) return false;
		if (a.ExecutionOptions < b.ExecutionOptions) return true;
		if (a.ExecutionOptions > b.ExecutionOptions) return false;
		return a.Type < b.Type;
	}

	/** Data for an instance that is currently being processed in the mutable system. This means it is
	* between a BeginUpdate and EndUpdate, or during an "atomic" operation (like generate a single resource).
	*/
	struct FLiveInstance
	{
		Instance::ID InstanceID;
		int32 State = 0;
		Ptr<const Instance> Instance;
		TSharedPtr<const Model> Model;

		Ptr<Parameters> OldParameters;

		/** Mask of the parameters that have changed since the last update.
		* Every bit represents a state parameter.
		*/
		uint64 UpdatedParameters = 0;

		/** Cached data for the generation of this instance. */
		TSharedPtr<FProgramCache> Cache;

		~FLiveInstance()
		{
			// Manually done to trace mem deallocations
			MUTABLE_CPUPROFILER_SCOPE(LiveInstanceDestructor);
			Cache = nullptr;
			OldParameters = nullptr;
			Instance = nullptr;
			Model = nullptr;
		}
	};


    /** Struct to manage all the memory allocated for resources used during mutable operation. */
    struct FWorkingMemoryManager
    {
		/** Cached traking for streamed model data for one model. */
        struct FModelCacheEntry
        {
            /** Model who's data is being tracked. */
            TWeakPtr<const Model> Model;

            /** For each model rom, the last time its streamed data was used. */
            TArray< TPair<uint64,uint64> > RomWeights;

			/** Count of pending operations for every rom index. */
			TArray<uint16> PendingOpsPerRom;
        };

		/** Maximum working memory that mutable should be using. */
		uint64 BudgetBytes = 0;

		/** Maximum excess memory reached suring the current operation. */
		uint64 BudgetExcessBytes = 0;

		/** This value is used to track the order of loading of roms. */
        uint64 RomTick = 0;

		/** Control info for the per-model cache of streamed data. */
        TArray< FModelCacheEntry > CachePerModel;

		/** Data for each mutable instance that is being updated. */
		TArray<FLiveInstance> LiveInstances;

		/** Temporary reference to the memory of the current instance being updated. Only valid during a mutable "atomic" operation, like a BeginUpdate or a GetImage. */
		TSharedPtr<FProgramCache> CurrentInstanceCache;

		/** Resources that have been used in the past, but haven't been deallocated because they still fitted the memory budget and they could be reused. */
		TArray<Ptr<Image>> PooledImages;

		/** List of intermediate resources that are not soterd anywhere yet. They are still locally referenced by code. */
		TArray<Ptr<const Image>> TempImages;

		/** */
		TMap<Ptr<const Resource>, int32> CacheResources;

		/** Given a mutable model, find or create its rom cache. */
		FModelCacheEntry& GetModelCache(const TSharedPtr<const Model>&);

        /** Make sure the working memory is below the internal budget, even counting with the passed additional memory. 
		* An optional function can be passed to "block" the unload of certain roms of data.
		* Return true if it succeeded, false otherwise.
		*/
		bool EnsureBudgetBelow(uint64 AdditionalMemory);

        /** Register that a specific rom has been requested and update the heuristics to keep it in memory. */
        void MarkRomUsed( int32 RomIndex, const TSharedPtr<const Model>& );

		/** */
		Ptr<Image> CreateImage(uint32 SizeX, uint32 SizeY, uint32 Lods, EImageFormat Format, EInitializationType Init = EInitializationType::Black)
		{
			CheckRunnerThread();

			uint32 DataSize = Image::CalculateDataSize(SizeX, SizeY, Lods, Format);

			// Look for an unused image in the pool that can be reused
			int32 PooledImageCount = PooledImages.Num();
			for (int Index = 0; DataSize>0 && Index<PooledImageCount; ++Index)
			{
				Ptr<Image>& Candidate = PooledImages[Index];
				if (Candidate->GetFormat() == Format
					&& Candidate->GetSizeX() == SizeX
					&& Candidate->GetSizeY() == SizeY
					&& Candidate->GetLODCount() == Lods
					)
				{
					Ptr<Image> Result = Candidate;
					PooledImages.RemoveAtSwap(Index);
					if (Init == EInitializationType::Black)
					{
						Result->InitToBlack();
					}
					return Result;
				}
			}

			// Make room in the budget
			EnsureBudgetBelow(DataSize);

			// Create it
			Ptr<Image> Result = new Image(SizeX, SizeY, Lods, Format, Init);

			TempImages.Add(Result);
			return Result;
		}

		/** Ref will be nulled and relesed in any case. */
		Ptr<Image> CloneOrTakeOver(Ptr<const Image>& Resource)
		{
			CheckRunnerThread();

			TempImages.RemoveSingle(Resource);
			check(!TempImages.Contains(Resource));
			check(!PooledImages.Contains(Resource));

			Ptr<Image> Result;
			if (!Resource->IsUnique())
			{
				// TODO: try to grab from the pool

				uint32 DataSize = Resource->GetDataSize();
				EnsureBudgetBelow(DataSize);

				Result = Resource->Clone();
				Release(Resource);
			}
			else
			{
				Result = const_cast<Image*>(Resource.get());
				Resource = nullptr;
			}

			return Result;
		}

		/** */
		void Release(Ptr<const Image>& Resource)
		{
			CheckRunnerThread();

			if (!Resource)
			{
				return;
			}

			TempImages.RemoveSingle(Resource);
			check(!TempImages.Contains(Resource));
			check(!PooledImages.Contains(Resource));

			if (IsBudgetTemp(Resource))
			{
				// Check if we are exceeding the budget
				bool bInBudget = EnsureBudgetBelow(Resource->GetDataSize());
				if (bInBudget)
				{
					PooledImages.Add(const_cast<Image*>(Resource.get()));
				}
			}
			else
			{
				// Check if we are exceeding the budget
				bool bInBudget = EnsureBudgetBelow(0);
			}

			Resource = nullptr;
		}


		/** */
		void Release(Ptr<Image>& Resource)
		{
			CheckRunnerThread();

			if (!Resource)
			{
				return;
			}

			TempImages.RemoveSingle(Resource);
			check(!TempImages.Contains(Resource));
			check(!PooledImages.Contains(Resource));

			if (IsBudgetTemp(Resource))
			{
				// Check if we are exceeding the budget
				bool bInBudget = EnsureBudgetBelow(Resource->GetDataSize());
				if (bInBudget)
				{
					PooledImages.Add(Resource.get());
				}
			}
			else
			{
				// Check if we are exceeding the budget
				bool bInBudget = EnsureBudgetBelow(0);
			}

			Resource = nullptr;
		}

		/** */
		Ptr<const Image> LoadImage(const FCacheAddress& From, bool bTakeOwnership=false)
		{
			bool bIsLastReference = false;
			Ptr<const Image> Result = CurrentInstanceCache->GetImage(From, bIsLastReference);
			if (!Result)
			{
				return nullptr;
			}

			// If we retrieved the last reference to this resource in "From" cache position (it could still be in other cache positions as well)
			if (bIsLastReference)
			{
				int32* CountPtr = CacheResources.Find(Result);
				check(CountPtr);
				*CountPtr = (*CountPtr) - 1;
				if (!*CountPtr)
				{
					CacheResources.FindAndRemoveChecked(Result);
				}
			}


			if (!bTakeOwnership && Result->IsUnique())
			{
				TempImages.Add(Result);
			}

			return Result;
		}

		/** */
		void StoreImage(const FCacheAddress& To, Ptr<const Image> Resource)
		{
			if (Resource)
			{
				TempImages.RemoveSingle(Resource);
				check(!TempImages.Contains(Resource));

				int32& Count = CacheResources.FindOrAdd(Resource, 0);
				++Count;
			}

			CurrentInstanceCache->SetImage(To, Resource);
		}


		/** Return true if the resource is not in any cache (0,1,rom). */
		bool IsBudgetTemp(const Ptr<const Image>& Resource)
		{
			if (!Resource)
			{
				return false;
			}
			bool bIsTemp = Resource->IsUnique();
			return bIsTemp;
		}


		int32 GetPooledBytes() const
		{
			int32 Result = 0;
			for (const Ptr<Image>& Value : PooledImages)
			{
				Result += Value->GetDataSize();
			}

			return Result;
		}


		int32 GetTempBytes() const
		{
			int32 Result = 0;
			for (const Ptr<const Image>& Value : TempImages)
			{
				Result += Value->GetDataSize();
			}

			return Result;
		}


		int32 GetRomBytes() const
		{
			int32 Result = 0;

			TArray<const Model*> Models;
			for (const FLiveInstance& Instance : LiveInstances)
			{
				Models.AddUnique(Instance.Model.Get());
			}

			// Data stored per-model, but related to instance construction
			for (const Model* Model : Models)
			{
				// Count streamable and currently-loaded resources
				const FProgram& Program = Model->GetPrivate()->m_program;
				for (const TPair<int32, Ptr<const Image>>& Rom : Program.m_constantImageLODs)
				{
					if (Rom.Value && Rom.Key >= 0)
					{
						Result += Rom.Value->GetDataSize();
					}
				}
				for (const TPair<int32, Ptr<const Mesh>>& Rom : Program.m_constantMeshes)
				{
					if (Rom.Value && Rom.Key >= 0)
					{
						Result += Rom.Value->GetDataSize();
					}
				}
			}

			return Result;
		}


		int32 GetTrackedCacheBytes() const
		{
			int32 Result = 0;
			for (const TTuple<Ptr<const Resource>,int32>& It : CacheResources)
			{
				Result += It.Key->GetDataSize();
			}

			return Result;
		}


		int32 GetCache0Bytes() const
		{
			if (!CurrentInstanceCache)
			{
				return 0;
			}

			int32 Result = 0;
			TSet<const Resource*> Cache0Unique;

			CodeContainer<int>::iterator it = CurrentInstanceCache->m_opHitCount.begin();
			for (; it.IsValid(); ++it)
			{
				const Resource* Value = CurrentInstanceCache->m_resources[it.get_address()].Value.get();
				if (!Value)
				{
					continue;
				}

				int32 Count = *it;
				if (Count < UE_MUTABLE_CACHE_COUNT_LIMIT)
				{
					Cache0Unique.Add(Value);
				}
			}

			for (const Resource* Value : Cache0Unique)
			{
				Result += Value->GetDataSize();
			}

			return Result;
		}

		int32 GetCache1Bytes() const
		{
			int32 Result = 0;
			TSet<const Resource*> Cache1Unique;

			for (const FLiveInstance& Instance : LiveInstances)
			{
				CodeContainer<int>::iterator it = Instance.Cache->m_opHitCount.begin();
				for (; it.IsValid(); ++it)
				{
					const Resource* Value = Instance.Cache->m_resources[it.get_address()].Value.get();
					if (!Value)
					{
						continue;
					}

					int32 Count = *it;
					if (Count >= UE_MUTABLE_CACHE_COUNT_LIMIT)
					{
						Cache1Unique.Add(Value);
					}
				}
			}

			for (const Resource* Value : Cache1Unique)
			{
				Result += Value->GetDataSize();
			}

			return Result;
		}


		/** Remove all intermediate data (big and small) from the memory except for the one that has been explicitely
		* marked as state cache.
		*/
		void ClearCacheLayer0()
		{
			check(CurrentInstanceCache);

			MUTABLE_CPUPROFILER_SCOPE(ClearLayer0);

			CodeContainer<int>::iterator it = CurrentInstanceCache->m_opHitCount.begin();
			for (; it.IsValid(); ++it)
			{
				int32 Count = *it;
				if (Count < UE_MUTABLE_CACHE_COUNT_LIMIT)
				{
					TPair<int32, Ptr<const Resource>>& PairRef = CurrentInstanceCache->m_resources[it.get_address()];

					//CacheResources.FindAndRemoveChecked(PairRef.Value);
					CacheResources.Remove(PairRef.Value);

					// SetUnused only clears if the data size is relevant (mesh or image) but we need to 
					// clear it in all cases here, because it may have become invalid because of parameter changes.
					// SetUnused(it.get_address());
					PairRef.Value = nullptr;
					PairRef.Key = 0;
					*it = 0;
				}
			}
		}


		/** Remove all intermediate data (big and small) from the memory including the one that has been explicitely
		* marked as state cache.
		*/
		void ClearCacheLayer1()
		{
			MUTABLE_CPUPROFILER_SCOPE(ClearLayer1);

			CodeContainer< TPair<int32, Ptr<const Resource>> >::iterator it = CurrentInstanceCache->m_resources.begin();
			for (; it.IsValid(); ++it)
			{
				//CacheResources.FindAndRemoveChecked((*it).Value);
				CacheResources.Remove((*it).Value);

				(*it).Value = nullptr;
				(*it).Key = 0;
			}

			CurrentInstanceCache->m_descCache.SetNum(0);
		}


		void LogWorkingMemory(const class CodeRunner* CurrentRunner) const;


#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		/** Temp variable with the ID of the thread running code, for debugging. */
		uint32 DebugRunnerThreadID = FThread::InvalidThreadId;
#endif

		/** This is a development-only check to make sure calls to resource management happen in the correct thread. */
		inline void BeginRunnerThread()
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// If this check fails it means have not set up correctly all the paths to debug threading for resource management.
			check(DebugRunnerThreadID == FThread::InvalidThreadId);

			DebugRunnerThreadID = FPlatformTLS::GetCurrentThreadId();
#endif
		}


		/** This is a development-only check to make sure calls to resource management happen in the correct thread. */
		inline void CheckRunnerThread()
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// If this check fails it means have not set up correctly all the paths to debug threading for resource management.
			check(DebugRunnerThreadID != FThread::InvalidThreadId);

			// If this check fails it means we are doing resource management from a thread that is not the CodeRunner::RunCode
			// thread, and this is not allowed.
			check(DebugRunnerThreadID== FPlatformTLS::GetCurrentThreadId());
#endif
		}


		/** This is a development-only check to make sure calls to resource management happen in the correct thread. */
		inline void EndRunnerThread()
		{
			CurrentInstanceCache->CheckHitCountsCleared();

			// If this check fails it means some operation is not correctly handling resource management and didn't release
			// a resource it created.
			// This should be reported and reviewed, but it is not fatal. Some unnecessary memory may be used temporarily.
			ensure(TempImages.Num()==0);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// If this check fails it means have not set up correctly all the paths to debug threading for resource management.
			check(DebugRunnerThreadID != FThread::InvalidThreadId);

			DebugRunnerThreadID = FThread::InvalidThreadId;
#endif
		}

	};


	/** */
    class System::Private : public Base
    {
    public:

        Private( Ptr<Settings>, const TSharedPtr<ExtensionDataStreamer>& );
        virtual ~Private();

        //-----------------------------------------------------------------------------------------
        //! Own interface
        //-----------------------------------------------------------------------------------------

		/** This method can be used to internally prepare for code execution. */
		MUTABLERUNTIME_API void BeginBuild(const TSharedPtr<const Model>&);
		MUTABLERUNTIME_API void EndBuild();

		MUTABLERUNTIME_API bool BuildBool(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
		MUTABLERUNTIME_API int32 BuildInt(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
		MUTABLERUNTIME_API float BuildScalar(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
		MUTABLERUNTIME_API FVector4f BuildColour(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
		MUTABLERUNTIME_API Ptr<const String> BuildString(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
		MUTABLERUNTIME_API Ptr<const Image> BuildImage(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS, int32 MipsToSkip, int32 LOD) ;
		MUTABLERUNTIME_API Ptr<const Mesh> BuildMesh(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
		MUTABLERUNTIME_API Ptr<const Layout> BuildLayout(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;
    	MUTABLERUNTIME_API Ptr<const Projector> BuildProjector(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS) ;

		ExtensionDataStreamer* GetExtensionDataStreamer() const { return ExtensionDataStreamer.Get(); }

        //!
        Ptr<const Settings> Settings;

        //! Data streaming interface, if any.
		TSharedPtr<ModelStreamer> StreamInterface;

		TSharedPtr<ImageParameterGenerator> ImageParameterGenerator;

		/** */
		FWorkingMemoryManager WorkingMemoryManager;

		/** Counter used to generate unique IDs for every new instance created in the system. */
        Instance::ID LastInstanceID = 0;

		/** The pointer returned by this function is only valid for the duration of the current mutable operation. */
		inline FLiveInstance* FindLiveInstance(Instance::ID id);

        //!
        bool CheckUpdatedParameters( const FLiveInstance*, const Ptr<const Parameters>&, uint64& OutUpdatedParameters );


		void RunCode(const TSharedPtr<const Model>&, const Parameters*, OP::ADDRESS at, uint32 LODs = System::AllLODs, uint8 executionOptions = 0, int32 LOD = 0);

		//!
		void PrepareCache(const Model*, int32 State);

	private:

		/** Owned by this system. */
		TSharedPtr<ExtensionDataStreamer> ExtensionDataStreamer = nullptr;

		/** This flag is turned on when a streaming error or similar happens. Results are not usable.
		* This should only happen in-editor.
		*/
		bool bUnrecoverableError = false;

    };

}
