// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <atomic>

//TODO: move to a per platform header and have VM scale vectorization according to vector width.
#define VECTOR_WIDTH (128)
#define VECTOR_WIDTH_BYTES (16)
#define VECTOR_WIDTH_FLOATS (4)

DECLARE_DELEGATE_OneParam(FVMExternalFunction, class FVectorVMExternalFunctionContext& /*Context*/);

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
	inputdata_float,
	inputdata_int32,
	inputdata_half,
	inputdata_noadvance_float,
	inputdata_noadvance_int32,
	inputdata_noadvance_half,
	outputdata_float,
	outputdata_int32,
	outputdata_half,
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

	//experimental VM only
	fused_input1_1, /* 95    op has 1 input operand	   - binary 1          ie: register 0 is an input          (ORDER IS CRUCIAL) */
	fused_input2_1, /* 96    op has 2 input operands   - binary 01         ie: register 0 is an input          (ORDER IS CRUCIAL) */
	fused_input2_2, /* 97    op has 2 input operands   - binary 10         ie: register 1 is an input          (ORDER IS CRUCIAL) */
	fused_input2_3, /* 97    op has 2 input operands   - binary 11         ie: register 1 and 2 are inputs     (ORDER IS CRUCIAL) */
	fused_input3_1, /* 98    op has 3 input operands   - binary 001        ie: register 1 is an input          (ORDER IS CRUCIAL) */
	fused_input3_2, /* 99    op has 3 input operands   - binary 010        ie: register 2 is an input          (ORDER IS CRUCIAL) */
	fused_input3_4, /* 101   op has 3 input operands   - binary 100        ie: register 3 is an input          (ORDER IS CRUCIAL) */
	fused_input3_3, /* 100   op has 3 input operands   - binary 011        ie: register 1 and 2 are inputs     (ORDER IS CRUCIAL) */
	fused_input3_5, /* 102   op has 3 input operands   - binary 101        ie: register 3 and 1 are inputs     (ORDER IS CRUCIAL) */
	fused_input3_6, /* 103   op has 3 input operands   - binary 110        ie: register 2 and 2 are inputs     (ORDER IS CRUCIAL) */
	fused_input3_7, /* 104   op has 3 input operands   - binary 111        ie: register 1, 2 and 3 are inputs  (ORDER IS CRUCIAL) */
	copy_to_output, /* 105 */
	output_batch2,  /* 106 */
	output_batch3,  /* 107 */
	output_batch4,  /* 108 */
	output_batch7,  /* 108 */
	output_batch8,  /* 108 */

	NumOpcodes
};
	
enum class EVectorVMOpCategory : uint8 {
	Input,
	Output,
	Op,
	ExtFnCall,
	IndexGen,
	ExecIndex,
	RWBuffer,
	Stat,
	Fused,
	Other
};

#if STATS
struct FVMCycleCounter
{
	int32 ScopeIndex;
	uint64 ScopeEnterCycles;
};

struct FStatScopeData
{
	TStatId StatId;
	std::atomic<uint64> ExecutionCycleCount;

	FStatScopeData(TStatId InStatId) : StatId(InStatId)
	{
		ExecutionCycleCount.store(0);
	}
	
	FStatScopeData(const FStatScopeData& InObj)
	{
		StatId = InObj.StatId;
		ExecutionCycleCount.store(InObj.ExecutionCycleCount.load());
	}
};

struct FStatStackEntry
{
	FCycleCounter CycleCounter;
	FVMCycleCounter VmCycleCounter;
};
#endif

//TODO: 
//All of this stuff can be handled by the VM compiler rather than dirtying the VM code.
//Some require RWBuffer like support.
struct FDataSetMeta
{
	TArrayView<uint8 const* RESTRICT const> InputRegisters;
	TArrayView<uint8 const* RESTRICT const> OutputRegisters;

	uint32 InputRegisterTypeOffsets[3];
	uint32 OutputRegisterTypeOffsets[3];

	int32 DataSetAccessIndex;	// index for individual elements of this set

	int32 InstanceOffset;		// offset of the first instance processed 
	
	TArray<int32>*RESTRICT IDTable;
	TArray<int32>*RESTRICT FreeIDTable;
	TArray<int32>*RESTRICT SpawnedIDsTable;

	/** Number of free IDs in the FreeIDTable */
	int32* NumFreeIDs;

	/** MaxID used in this execution. */
	int32* MaxUsedID;

	int32 *NumSpawnedIDs;

	int32 IDAcquireTag;

	//Temporary lock we're using for thread safety when writing to the FreeIDTable.
	//TODO: A lock free algorithm is possible here. We can create a specialized lock free list and reuse the IDTable slots for FreeIndices as Next pointers for our LFL.
	//This would also work well on the GPU. 
	//UE-65856 for tracking this work.

	//@NOTE (smcgrath): the lock/unlock functions can go with the ne VM, we don't need them
#ifndef NIAGARA_EXP_VM
	FCriticalSection FreeTableLock;
#endif
	FORCEINLINE void LockFreeTable();
	FORCEINLINE void UnlockFreeTable();

	FDataSetMeta()
		: InputRegisterTypeOffsets{}
		, OutputRegisterTypeOffsets{}
		, DataSetAccessIndex(INDEX_NONE)
		, InstanceOffset(INDEX_NONE)
		, IDTable(nullptr)
		, FreeIDTable(nullptr)
		, SpawnedIDsTable(nullptr)
		, NumFreeIDs(nullptr)
		, MaxUsedID(nullptr)
		, IDAcquireTag(INDEX_NONE)
	{
	}	

	FORCEINLINE void Reset()
	{
		InputRegisters = TArrayView<uint8 const* RESTRICT const>();
		OutputRegisters = TArrayView<uint8 const* RESTRICT const>();
		DataSetAccessIndex = INDEX_NONE;
		InstanceOffset = INDEX_NONE;
		IDTable = nullptr;
		FreeIDTable = nullptr;
		SpawnedIDsTable = nullptr;
		NumFreeIDs = nullptr;
		MaxUsedID = nullptr;
		IDAcquireTag = INDEX_NONE;
	}

	FORCEINLINE void Init(const TArrayView<uint8 const* RESTRICT const>& InInputRegisters, const TArrayView<uint8 const* RESTRICT const>& InOutputRegisters, int32 InInstanceOffset, TArray<int32>* InIDTable, TArray<int32>* InFreeIDTable, int32* InNumFreeIDs, int32 *InNumSpawnedIDs, int32* InMaxUsedID, int32 InIDAcquireTag, TArray<int32>* InSpawnedIDsTable)
	{
		InputRegisters = InInputRegisters;
		OutputRegisters = InOutputRegisters;

		DataSetAccessIndex = INDEX_NONE;
		InstanceOffset = InInstanceOffset;
		IDTable = InIDTable;
		FreeIDTable = InFreeIDTable;
		NumFreeIDs = InNumFreeIDs;
		NumSpawnedIDs = InNumSpawnedIDs;
		MaxUsedID = InMaxUsedID;
		IDAcquireTag = InIDAcquireTag;
		SpawnedIDsTable = InSpawnedIDsTable;
	}

private:
	// Non-copyable and non-movable
	FDataSetMeta(FDataSetMeta&&) = delete;
	FDataSetMeta(const FDataSetMeta&) = delete;
	FDataSetMeta& operator=(FDataSetMeta&&) = delete;
	FDataSetMeta& operator=(const FDataSetMeta&) = delete;
};
