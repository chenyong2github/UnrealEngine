// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Math/RandomStream.h"
#include "Misc/ByteSwap.h"
#include "Templates/AlignmentTemplates.h"

//TODO: move to a per platform header and have VM scale vectorization according to vector width.
#define VECTOR_WIDTH (128)
#define VECTOR_WIDTH_BYTES (16)
#define VECTOR_WIDTH_FLOATS (4)

DECLARE_DELEGATE_OneParam(FVMExternalFunction, struct FVectorVMContext& /*Context*/);

UENUM()
enum class EVectorVMBaseTypes : uint8
{
	Float,
	Int,
	Bool,
	Num UMETA(Hidden),
};

UENUM()
enum class EVectorVMOperandLocation : uint8
{
	Register,
	Constant,
	Num
};

UENUM()
enum class EVectorVMOp : uint8
{
	done,
	add,
	sub,
	mul,
	div,
	mad,
	lerp,
	rcp,
	rsq,
	sqrt,
	neg,
	abs,
	exp,
	exp2,
	log,
	log2,
	sin,
	cos,
	tan,
	asin,
	acos,
	atan,
	atan2,
	ceil,
	floor,
	fmod,
	frac,
	trunc,
	clamp,
	min,
	max,
	pow,
	round,
	sign,
	step,
	random,
	noise,

	//Comparison ops.
	cmplt,
	cmple,
	cmpgt,
	cmpge,
	cmpeq,
	cmpneq,
	select,

// 	easein,  Pretty sure these can be replaced with just a single smoothstep implementation.
// 	easeinout,

	//Integer ops
	addi,
	subi,
	muli,
	divi,//SSE Integer division is not implemented as an intrinsic. Will have to do some manual implementation.
	clampi,
	mini,
	maxi,
	absi,
	negi,
	signi,
	randomi,
	cmplti,
	cmplei,
	cmpgti,
	cmpgei,
	cmpeqi,
	cmpneqi,
	bit_and,
	bit_or,
	bit_xor,
	bit_not,
	bit_lshift,
	bit_rshift,

	//"Boolean" ops. Currently handling bools as integers.
	logic_and,
	logic_or,
	logic_xor,
	logic_not,

	//conversions
	f2i,
	i2f,
	f2b,
	b2f,
	i2b,
	b2i,

	// data read/write
	inputdata_32bit,
	inputdata_noadvance_32bit,
	outputdata_32bit,
	acquireindex,

	external_func_call,

	/** Returns the index of each instance in the current execution context. */
	exec_index,

	noise2D,
	noise3D,

	/** Utility ops for hooking into the stats system for performance analysis. */
	enter_stat_scope,
	exit_stat_scope,

	//updates an ID in the ID table
	update_id,
	//acquires a new ID from the free list.
	acquire_id,

	NumOpcodes
};


//TODO: 
//All of this stuff can be handled by the VM compiler rather than dirtying the VM code.
//Some require RWBuffer like support.
struct FDataSetMeta
{
	uint8*RESTRICT*RESTRICT InputRegisters;
	uint8*RESTRICT*RESTRICT OutputRegisters;

	int32 DataSetAccessIndex;	// index for individual elements of this set

	int32 InstanceOffset;		// offset of the first instance processed 
	
	TArray<int32>*RESTRICT IDTable;
	TArray<int32>*RESTRICT FreeIDTable;

	/** Number of free IDs in the FreeIDTable */
	int32* NumFreeIDs;

	/** MaxID used in this execution. */
	int32* MaxUsedID;

	int32 IDAcquireTag;

	//Temporary lock we're using for thread safety when writing to the FreeIDTable.
	//TODO: A lock free algorithm is possible here. We can create a specialized lock free list and reuse the IDTable slots for FreeIndices as Next pointers for our LFL.
	//This would also work well on the GPU. 
	//UE-65856 for tracking this work.
	FCriticalSection FreeTableLock;

	FORCEINLINE void LockFreeTable();
	FORCEINLINE void UnlockFreeTable();

	FDataSetMeta()
		: InputRegisters(nullptr)
		, OutputRegisters(nullptr)
		, DataSetAccessIndex(INDEX_NONE)
		, InstanceOffset(INDEX_NONE)
		, IDTable(nullptr)
		, FreeIDTable(nullptr)
		, NumFreeIDs(nullptr)
		, MaxUsedID(nullptr)
		, IDAcquireTag(INDEX_NONE)
	{
	}	

	FORCEINLINE void Reset()
	{
		InputRegisters = nullptr;
		OutputRegisters = nullptr;
		DataSetAccessIndex = INDEX_NONE;
		InstanceOffset = INDEX_NONE;
		IDTable = nullptr;
		FreeIDTable = nullptr;
		NumFreeIDs = nullptr;
		MaxUsedID = nullptr;
		IDAcquireTag = INDEX_NONE;
	}

	FORCEINLINE void Init(uint8*RESTRICT *RESTRICT InInputRegisters, uint8*RESTRICT *RESTRICT InOutputRegisters, int32 InInstanceOffset, TArray<int32>* InIDTable, TArray<int32>* InFreeIDTable, int32* InNumFreeIDs, int32* InMaxUsedID, int32 InIDAcquireTag)
	{
		InputRegisters = InInputRegisters;
		OutputRegisters = InOutputRegisters;

		DataSetAccessIndex = INDEX_NONE;
		InstanceOffset = InInstanceOffset;
		IDTable = InIDTable;
		FreeIDTable = InFreeIDTable;
		NumFreeIDs = InNumFreeIDs;
		MaxUsedID = InMaxUsedID;
		IDAcquireTag = InIDAcquireTag;
	}

private:
	// Non-copyable and non-movable
	FDataSetMeta(FDataSetMeta&&) = delete;
	FDataSetMeta(const FDataSetMeta&) = delete;
	FDataSetMeta& operator=(FDataSetMeta&&) = delete;
	FDataSetMeta& operator=(const FDataSetMeta&) = delete;
};

//Data the VM will keep on each dataset locally per thread which is then thread safely pushed to it's destination at the end of execution.
struct FDataSetThreadLocalTempData
{
	FDataSetThreadLocalTempData()
	{
		Reset();
	}

	FORCEINLINE void Reset()
	{
		IDsToFree.Reset();
		MaxID = INDEX_NONE;
	}

	TArray<int32> IDsToFree;
	int32 MaxID;

	//TODO: Possibly store output data locally and memcpy to the real buffers. Could avoid false sharing in parallel execution and so improve perf.
	//using _mm_stream_ps on platforms that support could also work for this?
	//TArray<TArray<float>> OutputFloatData;
	//TArray<TArray<int32>> OutputIntData;
};

/**
* Context information passed around during VM execution.
*/
struct FVectorVMContext : TThreadSingleton<FVectorVMContext>
{
private:
	/** Pointer to the next element in the byte code. */
	uint8 const* RESTRICT Code;
public:
	/** Pointer to the constant table. */
	uint8 const* RESTRICT ConstantTable;
	/** Num temp registers required by this script. */
	int32 NumTempRegisters;

	/** Pointer to the shared data table. */
	FVMExternalFunction* RESTRICT ExternalFunctionTable;
	/** Table of user pointers.*/
	void** UserPtrTable;

	/** Number of instances to process. */
	int32 NumInstances;
	/** Number of instances to process when doing batches of VECTOR_WIDTH_FLOATS. */
	int32 NumInstancesVectorFloats;
	/** Start instance of current chunk. */
	int32 StartInstance;

	/** Array of meta data on data sets. TODO: This struct should be removed and all features it contains be handled by more general vm ops and the compiler's knowledge of offsets etc. */
	TArrayView<FDataSetMeta> DataSetMetaTable;

	TArray<FDataSetThreadLocalTempData> ThreadLocalTempData;

#if STATS
	TArray<FCycleCounter, TInlineAllocator<64>> StatCounterStack;
	const TArray<TStatId>* StatScopes;
#endif

	TArray<uint8, TAlignedHeapAllocator<VECTOR_WIDTH_BYTES>> TempRegTable;
	uint32 TempRegisterSize;
	uint32 TempBufferSize;

	/** Thread local random stream for use in external functions needing non-deterministic randoms. */
	FRandomStream RandStream;

	/** Thread local per instance random counters for use in external functions needing deterministic randoms. */
	TArray<int32> RandCounters;

	bool bIsParallelExecution;

	FVectorVMContext();

	void PrepareForExec(
		int32 InNumTempRegisters,
		const uint8* InConstantTable,
		FVMExternalFunction* InExternalFunctionTable,
		void** InUserPtrTable,
		TArrayView<FDataSetMeta> InDataSetMetaTable,
		int32 MaxNumInstances,
		bool bInParallelExecution
#if STATS
		, const TArray<TStatId>* InStatScopes
#endif
	);

	void FinishExec();

	void PrepareForChunk(const uint8* InCode, int32 InNumInstances, int32 InStartInstance)
	{
		Code = InCode;
		NumInstances = InNumInstances;
		NumInstancesVectorFloats = (NumInstances + VECTOR_WIDTH_FLOATS - 1) / VECTOR_WIDTH_FLOATS;
		StartInstance = InStartInstance;

		RandCounters.Reset();
		RandCounters.SetNumZeroed(InNumInstances);
	}

	FORCEINLINE FDataSetMeta& GetDataSetMeta(int32 DataSetIndex) { return DataSetMetaTable[DataSetIndex]; }
	FORCEINLINE uint8 * RESTRICT GetTempRegister(int32 RegisterIndex) { return TempRegTable.GetData() + TempRegisterSize * RegisterIndex; }
	template<typename T>
	FORCEINLINE T* RESTRICT GetInputRegister(int32 DataSetIndex, int32 RegisterIndex) 
	{
		FDataSetMeta& Meta = GetDataSetMeta(DataSetIndex);
		return ((T*)Meta.InputRegisters[RegisterIndex]) + Meta.InstanceOffset;
	}
	template<typename T>
	FORCEINLINE T* RESTRICT GetOutputRegister(int32 DataSetIndex, int32 RegisterIndex) 
	{ 
		FDataSetMeta& Meta = GetDataSetMeta(DataSetIndex);
		return  ((T*)Meta.OutputRegisters[RegisterIndex]) + Meta.InstanceOffset;
	}

	int32 GetNumInstances() const { return NumInstances; }
	int32 GetStartInstance() const { return StartInstance; }

	template<uint32 InstancesPerOp>
	int32 GetNumLoops() const { return (InstancesPerOp == VECTOR_WIDTH_FLOATS) ? NumInstancesVectorFloats : ((InstancesPerOp == 1) ? NumInstances : Align(NumInstances, InstancesPerOp));	}

	FORCEINLINE uint8 DecodeU8() { return *Code++; }
#if PLATFORM_SUPPORTS_UNALIGNED_LOADS
	FORCEINLINE uint16 DecodeU16() { uint16 v = *reinterpret_cast<const uint16*>(Code); Code += sizeof(uint16); return INTEL_ORDER16(v); }
	FORCEINLINE uint32 DecodeU32() { uint32 v = *reinterpret_cast<const uint32*>(Code); Code += sizeof(uint32); return INTEL_ORDER32(v); }
	FORCEINLINE uint64 DecodeU64() { uint64 v = *reinterpret_cast<const uint64*>(Code); Code += sizeof(uint64); return INTEL_ORDER64(v); }
#else
	FORCEINLINE uint16 DecodeU16() { uint16 v = Code[1]; v = v << 8 | Code[0]; Code += 2; return INTEL_ORDER16(v); }
	FORCEINLINE uint32 DecodeU32() { uint32 v = Code[3]; v = v << 8 | Code[2]; v = v << 8 | Code[1]; v = v << 8 | Code[0]; Code += 4; return INTEL_ORDER32(v); }
	FORCEINLINE uint64 DecodeU64() { uint64 v = Code[7]; v = v << 8 | Code[6]; v = v << 8 | Code[5]; v = v << 8 | Code[4]; v = v << 8 | Code[3]; v = v << 8 | Code[2]; v = v << 8 | Code[1]; v = v << 8 | Code[0]; Code += 8; return INTEL_ORDER64(v); }
#endif
	FORCEINLINE uintptr_t DecodePtr() { return (sizeof(uintptr_t) == 4) ? DecodeU32() : DecodeU64(); }

	/** Decode the next operation contained in the bytecode. */
	FORCEINLINE EVectorVMOp DecodeOp()
	{
		return static_cast<EVectorVMOp>(DecodeU8());
	}

	FORCEINLINE uint8 DecodeSrcOperandTypes()
	{
		return DecodeU8();
	}

	FORCEINLINE bool IsParallelExecution()
	{
		return bIsParallelExecution;
	}
};

namespace VectorVM
{
	/** Get total number of op-codes */
	VECTORVM_API uint8 GetNumOpCodes();

#if WITH_EDITOR
	VECTORVM_API FString GetOpName(EVectorVMOp Op);
	VECTORVM_API FString GetOperandLocationName(EVectorVMOperandLocation Location);
#endif

	VECTORVM_API uint8 CreateSrcOperandMask(EVectorVMOperandLocation Type0, EVectorVMOperandLocation Type1 = EVectorVMOperandLocation::Register, EVectorVMOperandLocation Type2 = EVectorVMOperandLocation::Register);

	/**
	 * Execute VectorVM bytecode.
	 */
	VECTORVM_API void Exec(
		uint8 const* ByteCode,
		uint8 const* OptimizedByteCode,
		int32 NumTempRegisters,
		uint8 const* ConstantTable,
		TArrayView<FDataSetMeta> DataSetMetaTable,
		FVMExternalFunction* ExternalFunctionTable,
		void** UserPtrTable,
		int32 NumInstances
#if STATS
		, const TArray<TStatId>& StatScopes
#endif
	);

	VECTORVM_API void OptimizeByteCode(const uint8* ByteCode, TArray<uint8>& OptimizedCode, TArrayView<uint8> ExternalFunctionRegisterCounts);

	VECTORVM_API void Init();

	#define VVM_EXT_FUNC_INPUT_LOC_BIT (unsigned short)(1<<15)
	#define VVM_EXT_FUNC_INPUT_LOC_MASK (unsigned short)~VVM_EXT_FUNC_INPUT_LOC_BIT

	template<typename T>
	struct FUserPtrHandler
	{
		int32 UserPtrIdx;
		T* Ptr;
		FUserPtrHandler(FVectorVMContext& Context)
			: UserPtrIdx(*(int32*)(Context.ConstantTable + (Context.DecodeU16())))
			, Ptr((T*)Context.UserPtrTable[UserPtrIdx])
		{
			check(UserPtrIdx != INDEX_NONE);
		}
		FORCEINLINE T* Get() { return Ptr; }
		FORCEINLINE T* operator->() { return Ptr; }
		FORCEINLINE operator T*() { return Ptr; }
	};

	// A flexible handler that can deal with either constant or register inputs.
	template<typename T>
	struct FExternalFuncInputHandler
	{
	private:
		/** Either byte offset into constant table or offset into register table deepening on VVM_INPUT_LOCATION_BIT */
		int32 InputOffset;
		T* RESTRICT InputPtr;
		int32 AdvanceOffset;

	public:
		FExternalFuncInputHandler()
			: InputOffset(INDEX_NONE)
			, InputPtr(nullptr)
			, AdvanceOffset(0)
		{}

		FORCEINLINE FExternalFuncInputHandler(FVectorVMContext& Context)
		{
			Init(Context);
		}

		void Init(FVectorVMContext& Context)
		{
			InputOffset = Context.DecodeU16();
			InputPtr = IsConstant() ? (T*)(Context.ConstantTable + GetOffset()) : (T*)Context.GetTempRegister(GetOffset());
			AdvanceOffset = IsConstant() ? 0 : 1;
		}

		FORCEINLINE bool IsConstant()const { return !IsRegister(); }
		FORCEINLINE bool IsRegister()const { return (InputOffset & VVM_EXT_FUNC_INPUT_LOC_BIT) != 0; }
		FORCEINLINE int32 GetOffset()const { return InputOffset & VVM_EXT_FUNC_INPUT_LOC_MASK; }

		FORCEINLINE const T Get() { return *InputPtr; }
		FORCEINLINE T* GetDest() { return InputPtr; }
		FORCEINLINE void Advance() { InputPtr += AdvanceOffset; }
		FORCEINLINE const T GetAndAdvance()
		{
			T* Ret = InputPtr;
			InputPtr += AdvanceOffset;
			return *Ret;
		}
		FORCEINLINE T* GetDestAndAdvance()
		{
			T* Ret = InputPtr;
			InputPtr += AdvanceOffset;
			return Ret;
		}
	};
	
	template<typename T>
	struct FExternalFuncRegisterHandler
	{
	private:
		int32 RegisterIndex;
		int32 AdvanceOffset;
		T Dummy;
		T* RESTRICT Register;
	public:
		FORCEINLINE FExternalFuncRegisterHandler(FVectorVMContext& Context)
			: RegisterIndex(Context.DecodeU16() & VVM_EXT_FUNC_INPUT_LOC_MASK)
			, AdvanceOffset(IsValid() ? 1 : 0)
		{
			if (IsValid())
			{
				checkSlow(RegisterIndex < Context.NumTempRegisters);
				Register = (T*)Context.GetTempRegister(RegisterIndex);
			}
			else
			{
				Register = &Dummy;
			}
		}

		FORCEINLINE bool IsValid() const { return RegisterIndex != (uint16)VVM_EXT_FUNC_INPUT_LOC_MASK; }

		FORCEINLINE const T Get() { return *Register; }
		FORCEINLINE T* GetDest() { return Register; }
		FORCEINLINE void Advance() { Register += AdvanceOffset; }
		FORCEINLINE const T GetAndAdvance()
		{
			T* Ret = Register;
			Register += AdvanceOffset;
			return *Ret;
		}
		FORCEINLINE T* GetDestAndAdvance()
		{
			T* Ret = Register;
			Register += AdvanceOffset;
			return Ret;
		}
	};

	template<typename T>
	struct FExternalFuncConstHandler
	{
		uint16 ConstantIndex;
		T Constant;
		FExternalFuncConstHandler(FVectorVMContext& Context)
			: ConstantIndex(Context.DecodeU16() & VVM_EXT_FUNC_INPUT_LOC_MASK)
			, Constant(*((T*)(Context.ConstantTable + ConstantIndex)))
		{}
		FORCEINLINE const T& Get() { return Constant; }
		FORCEINLINE const T& GetAndAdvance() { return Constant; }
		FORCEINLINE void Advance() { }
	};
} // namespace VectorVM



