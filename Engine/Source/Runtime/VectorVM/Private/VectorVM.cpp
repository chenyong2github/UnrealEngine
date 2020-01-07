// Copyright Epic Games, Inc. All Rights Reserved.

#include "VectorVM.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "VectorVMPrivate.h"
#include "Stats/Stats.h"
#include "HAL/ConsoleManager.h"
#include "Async/ParallelFor.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, VectorVM);


DECLARE_STATS_GROUP(TEXT("VectorVM"), STATGROUP_VectorVM, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("VVM Execution"), STAT_VVMExec, STATGROUP_VectorVM);
DECLARE_CYCLE_STAT(TEXT("VVM Chunk"), STAT_VVMExecChunk, STATGROUP_VectorVM);

DEFINE_LOG_CATEGORY_STATIC(LogVectorVM, All, All);

//#define FREE_TABLE_LOCK_CONTENTION_WARNINGS (!UE_BUILD_SHIPPING)
#define FREE_TABLE_LOCK_CONTENTION_WARNINGS (0)

//I don't expect us to ever be waiting long
#define FREE_TABLE_LOCK_CONTENTION_WARN_THRESHOLD_MS (0.01)

//#define VM_FORCEINLINE
#define VM_FORCEINLINE FORCEINLINE

#define OP_REGISTER (0)
#define OP0_CONST (1 << 0)
#define OP1_CONST (1 << 1)
#define OP2_CONST (1 << 2)

#define SRCOP_RRR (OP_REGISTER | OP_REGISTER | OP_REGISTER)
#define SRCOP_RRC (OP_REGISTER | OP_REGISTER | OP0_CONST)
#define SRCOP_RCR (OP_REGISTER | OP1_CONST | OP_REGISTER)
#define SRCOP_RCC (OP_REGISTER | OP1_CONST | OP0_CONST)
#define SRCOP_CRR (OP2_CONST | OP_REGISTER | OP_REGISTER)
#define SRCOP_CRC (OP2_CONST | OP_REGISTER | OP0_CONST)
#define SRCOP_CCR (OP2_CONST | OP1_CONST | OP_REGISTER)
#define SRCOP_CCC (OP2_CONST | OP1_CONST | OP0_CONST)

namespace VectorVMConstants
{
	static const VectorRegisterInt VectorStride = MakeVectorRegisterInt(VECTOR_WIDTH_FLOATS, VECTOR_WIDTH_FLOATS, VECTOR_WIDTH_FLOATS, VECTOR_WIDTH_FLOATS);

	// for generating shuffle masks given input {A, B, C, D}
	constexpr uint32 ShufMaskIgnore = 0xFFFFFFFF;
	constexpr uint32 ShufMaskA = 0x03020100;
	constexpr uint32 ShufMaskB = 0x07060504;
	constexpr uint32 ShufMaskC = 0x0B0A0908;
	constexpr uint32 ShufMaskD = 0x0F0E0D0C;

	static const VectorRegisterInt RegisterShuffleMask[] =
	{
		MakeVectorRegisterInt(ShufMaskIgnore, ShufMaskIgnore, ShufMaskIgnore, ShufMaskIgnore), // 0000
		MakeVectorRegisterInt(ShufMaskA, ShufMaskIgnore, ShufMaskIgnore, ShufMaskIgnore), // 0001
		MakeVectorRegisterInt(ShufMaskB, ShufMaskIgnore, ShufMaskIgnore, ShufMaskIgnore), // 0010
		MakeVectorRegisterInt(ShufMaskA, ShufMaskB, ShufMaskIgnore, ShufMaskIgnore), // 0011
		MakeVectorRegisterInt(ShufMaskC, ShufMaskIgnore, ShufMaskIgnore, ShufMaskIgnore), // 0100
		MakeVectorRegisterInt(ShufMaskA, ShufMaskC, ShufMaskIgnore, ShufMaskIgnore), // 0101
		MakeVectorRegisterInt(ShufMaskB, ShufMaskC, ShufMaskIgnore, ShufMaskIgnore), // 0110
		MakeVectorRegisterInt(ShufMaskA, ShufMaskB, ShufMaskC, ShufMaskIgnore), // 0111
		MakeVectorRegisterInt(ShufMaskD, ShufMaskIgnore, ShufMaskIgnore, ShufMaskIgnore), // 1000
		MakeVectorRegisterInt(ShufMaskA, ShufMaskD, ShufMaskIgnore, ShufMaskIgnore), // 1001
		MakeVectorRegisterInt(ShufMaskB, ShufMaskD, ShufMaskIgnore, ShufMaskIgnore), // 1010
		MakeVectorRegisterInt(ShufMaskA, ShufMaskB, ShufMaskD, ShufMaskIgnore), // 1011
		MakeVectorRegisterInt(ShufMaskC, ShufMaskD, ShufMaskIgnore, ShufMaskIgnore), // 1100
		MakeVectorRegisterInt(ShufMaskA, ShufMaskC, ShufMaskD, ShufMaskIgnore), // 1101
		MakeVectorRegisterInt(ShufMaskB, ShufMaskC, ShufMaskD, ShufMaskIgnore), // 1110
		MakeVectorRegisterInt(ShufMaskA, ShufMaskB, ShufMaskC, ShufMaskD), // 1111
	};

	constexpr uint32 cOne = 0xFFFFFFFFU;
	constexpr uint32 cZero = 0x00000000U;
	static const VectorRegister RemainderMask[] =
	{
		MakeVectorRegister(cZero, cZero, cZero, cZero), // 0 remaining
		MakeVectorRegister(cOne, cZero, cZero, cZero), // 1 remaining
		MakeVectorRegister(cOne, cOne, cZero, cZero), // 2 remaining
		MakeVectorRegister(cOne, cOne, cOne, cZero), // 3 remaining
		MakeVectorRegister(cOne, cOne, cOne, cOne), // 4 remaining
	};
};

// helper function wrapping the SSE3 shuffle operation.  Currently implemented for PS4/XB1/Neon, the
// rest will just use the FPU version so as to not push the requirements up to SSE3 (currently SSE2)
#if PLATFORM_ENABLE_VECTORINTRINSICS && (PLATFORM_PS4 || PLATFORM_XBOXONE)
#define VectorIntShuffle( Vec, Mask )	_mm_shuffle_epi8( (Vec), (Mask) )
#elif PLATFORM_ENABLE_VECTORINTRINSICS_NEON
/**
 * Shuffles a VectorInt using a provided shuffle mask
 *
 * @param Vec		Source vector
 * @param Mask		Shuffle vector
 */
FORCEINLINE VectorRegisterInt VectorIntShuffle(const VectorRegisterInt& Vec, const VectorRegisterInt& Mask)
{
	uint8x8x2_t VecSplit = { { vget_low_u8(Vec), vget_high_u8(Vec) } };
	return vcombine_u8(vtbl2_u8(VecSplit, vget_low_u8(Mask)), vtbl2_u8(VecSplit, vget_high_u8(Mask)));
	}
#else
FORCEINLINE VectorRegisterInt VectorIntShuffle(const VectorRegisterInt& Vec, const VectorRegisterInt& Mask)
{
	VectorRegisterInt Result;
	const int8* VecBytes = reinterpret_cast<const int8*>(&Vec);
	const int8* MaskBytes = reinterpret_cast<const int8*>(&Mask);
	int8* ResultBytes = reinterpret_cast<int8*>(&Result);

	for (int32 i = 0; i < sizeof(VectorRegisterInt); ++i)
	{
		ResultBytes[i] = (MaskBytes[i] < 0) ? 0 : VecBytes[MaskBytes[i] % 16];
	}

	return Result;
}
#endif

//Temporarily locking the free table until we can implement a lock free algorithm. UE-65856
FORCEINLINE void FDataSetMeta::LockFreeTable()
{
#if FREE_TABLE_LOCK_CONTENTION_WARNINGS
	uint64 StartCycles = FPlatformTime::Cycles64();
#endif
 		
	FreeTableLock.Lock();
 
#if FREE_TABLE_LOCK_CONTENTION_WARNINGS
	uint64 EndCylces = FPlatformTime::Cycles64();
	double DurationMs = FPlatformTime::ToMilliseconds64(EndCylces - StartCycles);
	if (DurationMs >= FREE_TABLE_LOCK_CONTENTION_WARN_THRESHOLD_MS)
	{
		UE_LOG(LogVectorVM, Warning, TEXT("VectorVM Stalled in LockFreeTable()! %g ms"), DurationMs);
	}
#endif
}

FORCEINLINE void FDataSetMeta::UnlockFreeTable()
{
 	FreeTableLock.Unlock();
}

static int32 GbParallelVVM = 1;
static FAutoConsoleVariableRef CVarbParallelVVM(
	TEXT("vm.Parallel"),
	GbParallelVVM,
	TEXT("If > 0 vector VM chunk level paralellism will be enabled. \n"),
	ECVF_Default
);

static int32 GParallelVVMChunksPerBatch = 4;
static FAutoConsoleVariableRef CVarParallelVVMChunksPerBatch(
	TEXT("vm.ParallelChunksPerBatch"),
	GParallelVVMChunksPerBatch,
	TEXT("Number of chunks to process per task when running in parallel. \n"),
	ECVF_Default
);

//These are possibly too granular to enable for everyone.
static int32 GbDetailedVMScriptStats = 0;
static FAutoConsoleVariableRef CVarDetailedVMScriptStats(
	TEXT("vm.DetailedVMScriptStats"),
	GbDetailedVMScriptStats,
	TEXT("If > 0 the vector VM will emit stats for it's internal module calls. \n"),
	ECVF_Default
);

static int32 GParallelVVMInstancesPerChunk = 128;
static FAutoConsoleVariableRef CVarParallelVVMInstancesPerChunk(
	TEXT("vm.InstancesPerChunk"),
	GParallelVVMInstancesPerChunk,
	TEXT("Number of instances per VM chunk. (default=128) \n"),
	ECVF_ReadOnly
);

static int32 GbOptimizeVMByteCode = 1;
static FAutoConsoleVariableRef CVarbOptimizeVMByteCode(
	TEXT("vm.OptimizeVMByteCode"),
	GbOptimizeVMByteCode,
	TEXT("If > 0 vector VM code optimization will be enabled at runtime.\n"),
	ECVF_Default
);

static int32 GbFreeUnoptimizedVMByteCode = 1;
static FAutoConsoleVariableRef CVarbFreeUnoptimizedVMByteCode(
	TEXT("vm.FreeUnoptimizedByteCode"),
	GbFreeUnoptimizedVMByteCode,
	TEXT("When we have optimized the VM byte code should we free the original unoptimized byte code?"),
	ECVF_Default
);

static int32 GbUseOptimizedVMByteCode = 1;
static FAutoConsoleVariableRef CVarbUseOptimizedVMByteCode(
	TEXT("vm.UseOptimizedVMByteCode"),
	GbUseOptimizedVMByteCode,
	TEXT("If > 0 optimized vector VM code will be excuted at runtime.\n"),
	ECVF_Default
);

static int32 GbSafeOptimizedKernels = 1;
static FAutoConsoleVariableRef CVarbSafeOptimizedKernels(
	TEXT("vm.SafeOptimizedKernels"),
	GbSafeOptimizedKernels,
	TEXT("If > 0 optimized vector VM byte code will use safe versions of the kernels.\n"),
	ECVF_Default
);

static int32 GbBatchPackVMOutput = 1;
static FAutoConsoleVariableRef CVarbBatchPackVMOutput(
	TEXT("vm.BatchPackedVMOutput"),
	GbBatchPackVMOutput,
	TEXT("If > 0 output elements will be packed and batched branch free.\n"),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////
//  VM Code Optimizer Context

typedef void(*FVectorVMExecFunction)(FVectorVMContext&);

struct FVectorVMCodeOptimizerContext
{
	typedef EVectorVMOp(*OptimizeVMFunction)(EVectorVMOp, FVectorVMCodeOptimizerContext&);

	explicit FVectorVMCodeOptimizerContext(FVectorVMContext& InBaseContext, const uint8* ByteCode, TArray<uint8>& InOptimizedCode, TArrayView<uint8> InExternalFunctionRegisterCounts)
		: BaseContext(InBaseContext)
		, OptimizedCode(InOptimizedCode)
		, ExternalFunctionRegisterCounts(InExternalFunctionRegisterCounts)
	{
		BaseContext.PrepareForExec(0, nullptr, nullptr, nullptr, TArrayView<FDataSetMeta>(), 0, false);
		BaseContext.PrepareForChunk(ByteCode, 0, 0);
	}
	FVectorVMCodeOptimizerContext(const FVectorVMCodeOptimizerContext&) = delete;
	FVectorVMCodeOptimizerContext(const FVectorVMCodeOptimizerContext&&) = delete;

	template<uint32 InstancesPerOp>
	int32 GetNumLoops() const { return 0; }

	FORCEINLINE uint8 DecodeU8() { return BaseContext.DecodeU8(); }
	FORCEINLINE uint16 DecodeU16() { return BaseContext.DecodeU16(); }
	FORCEINLINE uint32 DecodeU32() { return BaseContext.DecodeU32(); }
	FORCEINLINE uint64 DecodeU64() { return BaseContext.DecodeU64(); }

	//-TODO: Support unaligned writes
	template<typename T>
	void Write(const T& v)
	{
		reinterpret_cast<T&>(OptimizedCode[OptimizedCode.AddUninitialized(sizeof(T))]) = v;
	}

	struct FOptimizerCodeState
	{
		uint8 const* BaseContextCode;
		int32 OptimizedCodeLength;
	};

	FOptimizerCodeState CreateCodeState()
	{
		FOptimizerCodeState State;
		State.BaseContextCode = BaseContext.Code;
		State.OptimizedCodeLength = OptimizedCode.Num();

		return State;
	}

	void RollbackCodeState(const FOptimizerCodeState& State)
	{
		BaseContext.Code = State.BaseContextCode;
		OptimizedCode.SetNum(State.OptimizedCodeLength, false /* allowShrink */);
	}

	FVectorVMContext&		BaseContext;
	TArray<uint8>&			OptimizedCode;
	const TArrayView<uint8>	ExternalFunctionRegisterCounts;
	const int32				StartInstance = 0;
};

//////////////////////////////////////////////////////////////////////////
//  Constant Handlers

struct FConstantHandlerBase
{
	const uint16 ConstantIndex;
	FConstantHandlerBase(FVectorVMContext& Context)
		: ConstantIndex(Context.DecodeU16())
	{}

	FORCEINLINE void Advance() { }

	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write(Context.DecodeU16());
	}
};

template<typename T>
struct FConstantHandler : public FConstantHandlerBase
{
	const T Constant;
	FConstantHandler(FVectorVMContext& Context)
		: FConstantHandlerBase(Context)
		, Constant(*((T*)(Context.ConstantTable + ConstantIndex)))
	{}

	FORCEINLINE const T& Get() { return Constant; }
	FORCEINLINE const T& GetAndAdvance() { return Constant; }
};

template<>
struct FConstantHandler<VectorRegister> : public FConstantHandlerBase
{
	const VectorRegister Constant;
	FConstantHandler(FVectorVMContext& Context)
		: FConstantHandlerBase(Context)
		, Constant(VectorLoadFloat1(&Context.ConstantTable[ConstantIndex]))
	{}

	FORCEINLINE const VectorRegister Get() { return Constant; }
	FORCEINLINE const VectorRegister GetAndAdvance() { return Constant; }
};

template<>
struct FConstantHandler<VectorRegisterInt> : public FConstantHandlerBase
{
	const VectorRegisterInt Constant;
	FConstantHandler(FVectorVMContext& Context)
		: FConstantHandlerBase(Context)
		, Constant(VectorIntLoad1(&Context.ConstantTable[ConstantIndex]))
	{}

	FORCEINLINE const VectorRegisterInt Get() { return Constant; }
	FORCEINLINE const VectorRegisterInt GetAndAdvance() { return Constant; }
};


//////////////////////////////////////////////////////////////////////////
// Register handlers.
// Handle reading of a register, advancing the pointer with each read.

struct FRegisterHandlerBase
{
	const int32 RegisterIndex;
	FORCEINLINE FRegisterHandlerBase(FVectorVMContext& Context)
		: RegisterIndex(Context.DecodeU16())
	{}

	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write(Context.DecodeU16());
	}
};

template<typename T>
struct FRegisterHandler : public FRegisterHandlerBase
{
private:
	T * RESTRICT Register;
public:
	FORCEINLINE FRegisterHandler(FVectorVMContext& Context)
		: FRegisterHandlerBase(Context)
		, Register((T*)Context.GetTempRegister(RegisterIndex))
	{}

	FORCEINLINE const T Get() { return *Register; }
	FORCEINLINE T* GetDest() { return Register; }
	FORCEINLINE void Advance() { ++Register; }
	FORCEINLINE const T GetAndAdvance()
	{
		return *Register++;
	}
	FORCEINLINE T* GetDestAndAdvance()
	{
		return Register++;
	}
};

//////////////////////////////////////////////////////////////////////////

FVectorVMContext::FVectorVMContext()
	: Code(nullptr)
	, ConstantTable(nullptr)
	, ExternalFunctionTable(nullptr)
	, UserPtrTable(nullptr)
	, NumInstances(0)
	, StartInstance(0)
#if STATS
	, StatScopes(nullptr)
#endif
	, TempRegisterSize(0)
	, TempBufferSize(0)
{
	RandStream.GenerateNewSeed();
}

void FVectorVMContext::PrepareForExec(
	int32 InNumTempRegisters,
	const uint8* InConstantTable,
	FVMExternalFunction* InExternalFunctionTable,
	void** InUserPtrTable,
	TArrayView<FDataSetMeta> InDataSetMetaTable,
	int32 MaxNumInstances,
	bool bInParallelExecution
)
{
	NumTempRegisters = InNumTempRegisters;
	ConstantTable = InConstantTable;
	ExternalFunctionTable = InExternalFunctionTable;
	UserPtrTable = InUserPtrTable;

	TempRegisterSize = Align(MaxNumInstances * VectorVM::MaxInstanceSizeBytes, PLATFORM_CACHE_LINE_SIZE);
	TempBufferSize = TempRegisterSize * NumTempRegisters;
	TempRegTable.SetNumUninitialized(TempBufferSize, false);

	DataSetMetaTable = InDataSetMetaTable;

	for (auto& TLSTempData : ThreadLocalTempData)
	{
		TLSTempData.Reset();
	}
	ThreadLocalTempData.SetNum(DataSetMetaTable.Num());

	bIsParallelExecution = bInParallelExecution;
}

#if STATS
void FVectorVMContext::SetStatScopes(const TArray<TStatId>* InStatScopes)
{
	check(InStatScopes);
	StatScopes = InStatScopes;
	StatCounterStack.Reserve(StatScopes->Num());
}
#endif

void FVectorVMContext::FinishExec()
{
	//At the end of executing each chunk we can push any thread local temporary data out to the main storage with locks or atomics.

	check(ThreadLocalTempData.Num() == DataSetMetaTable.Num());
	for(int32 DataSetIndex=0; DataSetIndex < DataSetMetaTable.Num(); ++DataSetIndex)
	{
		FDataSetThreadLocalTempData&RESTRICT Data = ThreadLocalTempData[DataSetIndex];

		if (Data.IDsToFree.Num() > 0)
		{
			TArray<int32>&RESTRICT FreeIDTable = *DataSetMetaTable[DataSetIndex].FreeIDTable;
			int32&RESTRICT NumFreeIDs = *DataSetMetaTable[DataSetIndex].NumFreeIDs;
			check(FreeIDTable.Num() >= NumFreeIDs + Data.IDsToFree.Num());

			//Temporarily locking the free table until we can implement something lock-free
			DataSetMetaTable[DataSetIndex].LockFreeTable();
			for (int32 IDToFree : Data.IDsToFree)
			{
				//UE_LOG(LogVectorVM, Warning, TEXT("AddFreeID: ID:%d | FreeTableIdx:%d."), IDToFree, NumFreeIDs);
				FreeIDTable[NumFreeIDs++] = IDToFree;
			}
			//Unlock the free table.
			DataSetMetaTable[DataSetIndex].UnlockFreeTable();
			Data.IDsToFree.Reset();
		}

		//Also update the max ID seen. This should be the ONLY place in the VM we update this max value.
		if ( bIsParallelExecution )
		{
			volatile int32* MaxUsedID = DataSetMetaTable[DataSetIndex].MaxUsedID;
			int32 LocalMaxUsedID;
			do
			{
				LocalMaxUsedID = *MaxUsedID;
				if (LocalMaxUsedID >= Data.MaxID)
				{
					break;
				}
			} while (FPlatformAtomics::InterlockedCompareExchange(MaxUsedID, Data.MaxID, LocalMaxUsedID) != LocalMaxUsedID);

			*MaxUsedID = FMath::Max(*MaxUsedID, Data.MaxID);
		}
		else
		{
			int32* MaxUsedID = DataSetMetaTable[DataSetIndex].MaxUsedID;
			*MaxUsedID = FMath::Max(*MaxUsedID, Data.MaxID);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

uint8 VectorVM::CreateSrcOperandMask(EVectorVMOperandLocation Type0, EVectorVMOperandLocation Type1, EVectorVMOperandLocation Type2)
{
	return	(Type0 == EVectorVMOperandLocation::Constant ? OP0_CONST : OP_REGISTER) |
		(Type1 == EVectorVMOperandLocation::Constant ? OP1_CONST : OP_REGISTER) |
		(Type2 == EVectorVMOperandLocation::Constant ? OP2_CONST : OP_REGISTER);
}

//////////////////////////////////////////////////////////////////////////
// Kernels
template<typename Kernel, typename DstHandler, typename Arg0Handler, uint32 NumInstancesPerOp>
struct TUnaryKernelHandler
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Arg0Handler::Optimize(Context);
		DstHandler::Optimize(Context);
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		Arg0Handler Arg0(Context);
		DstHandler Dst(Context);

		const int32 Loops = Context.GetNumLoops<NumInstancesPerOp>();
		for (int32 i = 0; i < Loops; ++i)
		{
			Kernel::DoKernel(Context, Dst.GetDestAndAdvance(), Arg0.GetAndAdvance());
		}
	}
};

template<typename Kernel, typename DstHandler, typename Arg0Handler, typename Arg1Handler, int32 NumInstancesPerOp>
struct TBinaryKernelHandler
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Arg0Handler::Optimize(Context);
		Arg1Handler::Optimize(Context);
		DstHandler::Optimize(Context);
	}

	static void Exec(FVectorVMContext& Context)
	{
		Arg0Handler Arg0(Context); 
		Arg1Handler Arg1(Context);

		DstHandler Dst(Context);

		const int32 Loops = Context.GetNumLoops<NumInstancesPerOp>();
		for (int32 i = 0; i < Loops; ++i)
		{
			Kernel::DoKernel(Context, Dst.GetDestAndAdvance(), Arg0.GetAndAdvance(), Arg1.GetAndAdvance());
		}
	}
};

template<typename Kernel, typename DstHandler, typename Arg0Handler, typename Arg1Handler, typename Arg2Handler, int32 NumInstancesPerOp>
struct TTrinaryKernelHandler
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Arg0Handler::Optimize(Context);
		Arg1Handler::Optimize(Context);
		Arg2Handler::Optimize(Context);
		DstHandler::Optimize(Context);
	}

	static void Exec(FVectorVMContext& Context)
	{
		Arg0Handler Arg0(Context);
		Arg1Handler Arg1(Context);
		Arg2Handler Arg2(Context);

		DstHandler Dst(Context);

		const int32 Loops = Context.GetNumLoops<NumInstancesPerOp>();
		for (int32 i = 0; i < Loops; ++i)
		{
			Kernel::DoKernel(Context, Dst.GetDestAndAdvance(), Arg0.GetAndAdvance(), Arg1.GetAndAdvance(), Arg2.GetAndAdvance());
		}
	}
};


/** Base class of vector kernels with a single operand. */
template <typename Kernel, typename DstHandler, typename ConstHandler, typename RegisterHandler, int32 NumInstancesPerOp>
struct TUnaryKernel
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 SrcOpTypes = Context.BaseContext.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: TUnaryKernelHandler<Kernel, DstHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RRC:	TUnaryKernelHandler<Kernel, DstHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		default: check(0); break;
		};
	}

	static void Exec(FVectorVMContext& Context)
	{
		const uint32 SrcOpTypes = Context.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: TUnaryKernelHandler<Kernel, DstHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RRC:	TUnaryKernelHandler<Kernel, DstHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		default: check(0); break; 
		};
	}
};
template<typename Kernel>
struct TUnaryScalarKernel : public TUnaryKernel<Kernel, FRegisterHandler<float>, FConstantHandler<float>, FRegisterHandler<float>, 1> {};
template<typename Kernel>
struct TUnaryVectorKernel : public TUnaryKernel<Kernel, FRegisterHandler<VectorRegister>, FConstantHandler<VectorRegister>, FRegisterHandler<VectorRegister>, VECTOR_WIDTH_FLOATS> {};
template<typename Kernel>
struct TUnaryScalarIntKernel : public TUnaryKernel<Kernel, FRegisterHandler<int32>, FConstantHandler<int32>, FRegisterHandler<int32>, 1> {};
template<typename Kernel>
struct TUnaryVectorIntKernel : public TUnaryKernel<Kernel, FRegisterHandler<VectorRegisterInt>, FConstantHandler<VectorRegisterInt>, FRegisterHandler<VectorRegisterInt>, VECTOR_WIDTH_FLOATS> {};

/** Base class of Vector kernels with 2 operands. */
template <typename Kernel, typename DstHandler, typename ConstHandler, typename RegisterHandler, uint32 NumInstancesPerOp>
struct TBinaryKernel
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 SrcOpTypes = Context.BaseContext.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: TBinaryKernelHandler<Kernel, DstHandler, RegisterHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RRC:	TBinaryKernelHandler<Kernel, DstHandler, ConstHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RCR: TBinaryKernelHandler<Kernel, DstHandler, RegisterHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RCC:	TBinaryKernelHandler<Kernel, DstHandler, ConstHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		default: check(0); break;
		};
	}

	static void Exec(FVectorVMContext& Context)
	{
		const uint32 SrcOpTypes = Context.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: TBinaryKernelHandler<Kernel, DstHandler, RegisterHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RRC:	TBinaryKernelHandler<Kernel, DstHandler, ConstHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RCR: TBinaryKernelHandler<Kernel, DstHandler, RegisterHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RCC:	TBinaryKernelHandler<Kernel, DstHandler, ConstHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		default: check(0); break;
		};
	}
};
template<typename Kernel>
struct TBinaryScalarKernel : public TBinaryKernel<Kernel, FRegisterHandler<float>, FConstantHandler<float>, FRegisterHandler<float>, 1> {};
template<typename Kernel>
struct TBinaryVectorKernel : public TBinaryKernel<Kernel, FRegisterHandler<VectorRegister>, FConstantHandler<VectorRegister>, FRegisterHandler<VectorRegister>, VECTOR_WIDTH_FLOATS> {};
template<typename Kernel>
struct TBinaryVectorIntKernel : public TBinaryKernel<Kernel, FRegisterHandler<VectorRegisterInt>, FConstantHandler<VectorRegisterInt>, FRegisterHandler<VectorRegisterInt>, VECTOR_WIDTH_FLOATS> {};

/** Base class of Vector kernels with 3 operands. */
template <typename Kernel, typename DstHandler, typename ConstHandler, typename RegisterHandler, uint32 NumInstancesPerOp>
struct TTrinaryKernel
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 SrcOpTypes = Context.BaseContext.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, RegisterHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RRC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, RegisterHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RCR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, ConstHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_RCC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, ConstHandler, RegisterHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_CRR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, RegisterHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_CRC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, RegisterHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_CCR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, ConstHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		case SRCOP_CCC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, ConstHandler, ConstHandler, NumInstancesPerOp>::Optimize(Context); break;
		default: check(0); break;
		};
	}

	static void Exec(FVectorVMContext& Context)
	{
		const uint32 SrcOpTypes = Context.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, RegisterHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RRC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, RegisterHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RCR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, ConstHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_RCC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, ConstHandler, RegisterHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_CRR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, RegisterHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_CRC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, RegisterHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_CCR: TTrinaryKernelHandler<Kernel, DstHandler, RegisterHandler, ConstHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		case SRCOP_CCC:	TTrinaryKernelHandler<Kernel, DstHandler, ConstHandler, ConstHandler, ConstHandler, NumInstancesPerOp>::Exec(Context); break;
		default: check(0); break;
		};
	}
};

template<typename Kernel>
struct TTrinaryScalarKernel : public TTrinaryKernel<Kernel, FRegisterHandler<float>, FConstantHandler<float>, FRegisterHandler<float>, 1> {};
template<typename Kernel>
struct TTrinaryVectorKernel : public TTrinaryKernel<Kernel, FRegisterHandler<VectorRegister>, FConstantHandler<VectorRegister>, FRegisterHandler<VectorRegister>, VECTOR_WIDTH_FLOATS> {};
template<typename Kernel>
struct TTrinaryVectorIntKernel : public TTrinaryKernel<Kernel, FRegisterHandler<VectorRegisterInt>, FConstantHandler<VectorRegisterInt>, FRegisterHandler<VectorRegisterInt>, VECTOR_WIDTH_FLOATS> {};


/*------------------------------------------------------------------------------
	Implementation of all kernel operations.
------------------------------------------------------------------------------*/

struct FVectorKernelAdd : public TBinaryVectorKernel<FVectorKernelAdd>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1)
	{
		*Dst = VectorAdd(Src0, Src1);
	}
};

struct FVectorKernelSub : public TBinaryVectorKernel<FVectorKernelSub>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1)
	{
		*Dst = VectorSubtract(Src0, Src1);
	}
};

struct FVectorKernelMul : public TBinaryVectorKernel<FVectorKernelMul>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1)
	{
		*Dst = VectorMultiply(Src0, Src1);
	}
};

struct FVectorKernelDiv : public TBinaryVectorKernel<FVectorKernelDiv>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorDivide(Src0, Src1);
	}
};

struct FVectorKernelDivSafe : public TBinaryVectorKernel<FVectorKernelDivSafe>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		VectorRegister ValidMask = VectorCompareGT(VectorAbs(Src1), GlobalVectorConstants::SmallNumber);
		*Dst = VectorSelect(ValidMask, VectorDivide(Src0, Src1), GlobalVectorConstants::FloatZero);
	}
};

struct FVectorKernelMad : public TTrinaryVectorKernel<FVectorKernelMad>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1,VectorRegister Src2)
	{
		*Dst = VectorMultiplyAdd(Src0, Src1, Src2);
	}
};

struct FVectorKernelLerp : public TTrinaryVectorKernel<FVectorKernelLerp>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1,VectorRegister Src2)
	{
		const VectorRegister OneMinusAlpha = VectorSubtract(GlobalVectorConstants::FloatOne, Src2);
		const VectorRegister Tmp = VectorMultiply(Src0, OneMinusAlpha);
		*Dst = VectorMultiplyAdd(Src1, Src2, Tmp);
	}
};

struct FVectorKernelRcp : public TUnaryVectorKernel<FVectorKernelRcp>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0)
	{
		*Dst = VectorReciprocal(Src0);
	}
};

// if the magnitude of the value is too small, then the result will be 0 (not NaN/Inf)
struct FVectorKernelRcpSafe : public TUnaryVectorKernel<FVectorKernelRcpSafe>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		VectorRegister ValidMask = VectorCompareGT(VectorAbs(Src0), GlobalVectorConstants::SmallNumber);
		*Dst = VectorSelect(ValidMask, VectorReciprocal(Src0), GlobalVectorConstants::FloatZero);
	}
};

struct FVectorKernelRsq : public TUnaryVectorKernel<FVectorKernelRsq>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0)
	{
		*Dst = VectorReciprocalSqrt(Src0);
	}
};

// if the value is very small or negative, then the result will be 0 (not NaN/Inf/imaginary)
struct FVectorKernelRsqSafe : public TUnaryVectorKernel<FVectorKernelRsqSafe>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		VectorRegister ValidMask = VectorCompareGT(Src0, GlobalVectorConstants::SmallNumber);
		*Dst = VectorSelect(ValidMask, VectorReciprocalSqrt(Src0), GlobalVectorConstants::FloatZero);
	}
};

struct FVectorKernelSqrt : public TUnaryVectorKernel<FVectorKernelSqrt>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0)
	{
		// TODO: Need a SIMD sqrt!
		*Dst = VectorReciprocal(VectorReciprocalSqrt(Src0));
	}
};

struct FVectorKernelSqrtSafe : public TUnaryVectorKernel<FVectorKernelSqrtSafe>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		VectorRegister ValidMask = VectorCompareGT(Src0, GlobalVectorConstants::SmallNumber);
		*Dst = VectorSelect(ValidMask, VectorReciprocal(VectorReciprocalSqrt(Src0)), GlobalVectorConstants::FloatZero);
	}
};

struct FVectorKernelNeg : public TUnaryVectorKernel<FVectorKernelNeg>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0)
	{
		*Dst = VectorNegate(Src0);
	}
};

struct FVectorKernelAbs : public TUnaryVectorKernel<FVectorKernelAbs>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0)
	{
		*Dst = VectorAbs(Src0);
	}
};

struct FVectorKernelExp : public TUnaryVectorKernel<FVectorKernelExp>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorExp(Src0);
	}
}; 

struct FVectorKernelExp2 : public TUnaryVectorKernel<FVectorKernelExp2>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorExp2(Src0);
	}
};

struct FVectorKernelLog : public TUnaryVectorKernel<FVectorKernelLog>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorLog(Src0);
	}
};

struct FVectorKernelLogSafe : public TUnaryVectorKernel<FVectorKernelLogSafe>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		VectorRegister ValidMask = VectorCompareGT(Src0, GlobalVectorConstants::FloatZero);

		*Dst = VectorSelect(ValidMask, VectorLog(Src0), GlobalVectorConstants::FloatZero);
	}
};

struct FVectorKernelLog2 : public TUnaryVectorKernel<FVectorKernelLog2>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorLog2(Src0);
	}
};

struct FVectorKernelClamp : public TTrinaryVectorKernel<FVectorKernelClamp>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1,VectorRegister Src2)
	{
		const VectorRegister Tmp = VectorMax(Src0, Src1);
		*Dst = VectorMin(Tmp, Src2);
	}
};

struct FVectorKernelSin : public TUnaryVectorKernel<FVectorKernelSin>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorSin(Src0);
	}
};

struct FVectorKernelCos : public TUnaryVectorKernel<FVectorKernelCos>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
 	{
		*Dst = VectorCos(Src0);
	}
};

struct FVectorKernelTan : public TUnaryVectorKernel<FVectorKernelTan>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorTan(Src0);
	}
};

struct FVectorKernelASin : public TUnaryVectorKernel<FVectorKernelASin>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorASin(Src0);
	}
};

struct FVectorKernelACos : public TUnaryVectorKernel<FVectorKernelACos>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorACos(Src0);
	}
};

struct FVectorKernelATan : public TUnaryVectorKernel<FVectorKernelATan>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorATan(Src0);
	}
};

struct FVectorKernelATan2 : public TBinaryVectorKernel<FVectorKernelATan2>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorATan2(Src0, Src1);
	}
};

struct FVectorKernelCeil : public TUnaryVectorKernel<FVectorKernelCeil>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorCeil(Src0);
	}
};

struct FVectorKernelFloor : public TUnaryVectorKernel<FVectorKernelFloor>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorFloor(Src0);
	}
};

struct FVectorKernelRound : public TUnaryVectorKernel<FVectorKernelRound>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		//TODO: >SSE4 has direct ops for this.		
		VectorRegister Trunc = VectorTruncate(Src0);
		*Dst = VectorAdd(Trunc, VectorTruncate(VectorMultiply(VectorSubtract(Src0, Trunc), GlobalVectorConstants::FloatAlmostTwo)));
	}
};

struct FVectorKernelMod : public TBinaryVectorKernel<FVectorKernelMod>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorMod(Src0, Src1);
	}
};

struct FVectorKernelFrac : public TUnaryVectorKernel<FVectorKernelFrac>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorFractional(Src0);
	}
};

struct FVectorKernelTrunc : public TUnaryVectorKernel<FVectorKernelTrunc>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorTruncate(Src0);
	}
};

struct FVectorKernelCompareLT : public TBinaryVectorKernel<FVectorKernelCompareLT>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorCompareLT(Src0, Src1);
	}
};

struct FVectorKernelCompareLE : public TBinaryVectorKernel<FVectorKernelCompareLE>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorCompareLE(Src0, Src1);
	}
};

struct FVectorKernelCompareGT : public TBinaryVectorKernel<FVectorKernelCompareGT>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorCompareGT(Src0, Src1);
	}
};

struct FVectorKernelCompareGE : public TBinaryVectorKernel<FVectorKernelCompareGE>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorCompareGE(Src0, Src1);
	}
};

struct FVectorKernelCompareEQ : public TBinaryVectorKernel<FVectorKernelCompareEQ>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		*Dst = VectorCompareEQ(Src0, Src1);
	}
};

struct FVectorKernelCompareNEQ : public TBinaryVectorKernel<FVectorKernelCompareNEQ>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{		
		*Dst = VectorCompareNE(Src0, Src1);
	}
};

struct FVectorKernelSelect : public TTrinaryVectorKernel<FVectorKernelSelect>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Mask, VectorRegister A, VectorRegister B)
	{
		*Dst = VectorSelect(Mask, A, B);
	}
};

struct FVectorKernelExecutionIndex
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		FRegisterHandler<VectorRegisterInt>::Optimize(Context);
	}

	static void VM_FORCEINLINE Exec(FVectorVMContext& Context)
	{
		static_assert(VECTOR_WIDTH_FLOATS == 4, "Need to update this when upgrading the VM to support >SSE2");
		VectorRegisterInt VectorStride = MakeVectorRegisterInt(VECTOR_WIDTH_FLOATS, VECTOR_WIDTH_FLOATS, VECTOR_WIDTH_FLOATS, VECTOR_WIDTH_FLOATS);
		VectorRegisterInt Index = MakeVectorRegisterInt(Context.StartInstance, Context.StartInstance + 1, Context.StartInstance + 2, Context.StartInstance + 3);
		
		FRegisterHandler<VectorRegisterInt> Dest(Context);
		const int32 Loops = Context.GetNumLoops<VECTOR_WIDTH_FLOATS>();
		for (int32 i = 0; i < Loops; ++i)
		{
			*Dest.GetDestAndAdvance() = Index;
			Index = VectorIntAdd(Index, VectorStride);
		}
	}
};

struct FVectorKernelEnterStatScope
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
#if STATS
		Context.Write<FVectorVMExecFunction>(Exec);
		FConstantHandler<int32>::Optimize(Context);
#else
		// just skip the op if we don't have stats enabled
		FConstantHandler<int32>(Context.BaseContext);
#endif
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		FConstantHandler<int32> ScopeIdx(Context);
#if STATS
		if (GbDetailedVMScriptStats && Context.StatScopes)
		{
			int32 CounterIdx = Context.StatCounterStack.AddDefaulted(1);
			Context.StatCounterStack[CounterIdx].Start((*Context.StatScopes)[ScopeIdx.Get()]);
		}
#endif
	}
};

struct FVectorKernelExitStatScope
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
#if STATS
		Context.Write<FVectorVMExecFunction>(Exec);
#endif
	}
		
	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
#if STATS
		if (GbDetailedVMScriptStats)
		{
			Context.StatCounterStack.Last().Stop();
			Context.StatCounterStack.Pop(false);
		}
#endif
	}
};

struct FVectorKernelRandom : public TUnaryVectorKernel<FVectorKernelRandom>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		const float rm = RAND_MAX;
		//EEK!. Improve this. Implement GPU style seeded rand instead of this.
		VectorRegister Result = MakeVectorRegister(Context.RandStream.GetFraction(),
			Context.RandStream.GetFraction(),
			Context.RandStream.GetFraction(),
			Context.RandStream.GetFraction());
		*Dst = VectorMultiply(Result, Src0);
	}
};

/* gaussian distribution random number (not working yet) */
struct FVectorKernelRandomGauss : public TBinaryVectorKernel<FVectorKernelRandomGauss>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		const float rm = RAND_MAX;
		VectorRegister Result = MakeVectorRegister(Context.RandStream.GetFraction(),
			Context.RandStream.GetFraction(),
			Context.RandStream.GetFraction(),
			Context.RandStream.GetFraction());

		Result = VectorSubtract(Result, GlobalVectorConstants::FloatOneHalf);
		Result = VectorMultiply(MakeVectorRegister(3.0f, 3.0f, 3.0f, 3.0f), Result);

		// taylor series gaussian approximation
		const VectorRegister SPi2 = VectorReciprocal(VectorReciprocalSqrt(MakeVectorRegister(2 * PI, 2 * PI, 2 * PI, 2 * PI)));
		VectorRegister Gauss = VectorReciprocal(SPi2);
		VectorRegister Div = VectorMultiply(GlobalVectorConstants::FloatTwo, SPi2);
		Gauss = VectorSubtract(Gauss, VectorDivide(VectorMultiply(Result, Result), Div));
		Div = VectorMultiply(MakeVectorRegister(8.0f, 8.0f, 8.0f, 8.0f), SPi2);
		Gauss = VectorAdd(Gauss, VectorDivide(VectorPow(MakeVectorRegister(4.0f, 4.0f, 4.0f, 4.0f), Result), Div));
		Div = VectorMultiply(MakeVectorRegister(48.0f, 48.0f, 48.0f, 48.0f), SPi2);
		Gauss = VectorSubtract(Gauss, VectorDivide(VectorPow(MakeVectorRegister(6.0f, 6.0f, 6.0f, 6.0f), Result), Div));

		Gauss = VectorDivide(Gauss, MakeVectorRegister(0.4f, 0.4f, 0.4f, 0.4f));
		Gauss = VectorMultiply(Gauss, Src0);
		*Dst = Gauss;
	}
};

struct FVectorKernelMin : public TBinaryVectorKernel<FVectorKernelMin>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1)
	{
		*Dst = VectorMin(Src0, Src1);
	}
};

struct FVectorKernelMax : public TBinaryVectorKernel<FVectorKernelMax>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1)
	{
		*Dst = VectorMax(Src0, Src1);
	}
};

struct FVectorKernelPow : public TBinaryVectorKernel<FVectorKernelPow>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst,VectorRegister Src0,VectorRegister Src1)
	{
		*Dst = VectorPow(Src0, Src1);
	}
};

// if the base is small, then the result will be 0
struct FVectorKernelPowSafe : public TBinaryVectorKernel<FVectorKernelPowSafe>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0, VectorRegister Src1)
	{
		VectorRegister ValidMask = VectorCompareGT(Src0, GlobalVectorConstants::SmallNumber);
		*Dst = VectorSelect(ValidMask, VectorPow(Src0, Src1), GlobalVectorConstants::FloatZero);
	}
};

struct FVectorKernelSign : public TUnaryVectorKernel<FVectorKernelSign>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorSign(Src0);
	}
};

struct FVectorKernelStep : public TUnaryVectorKernel<FVectorKernelStep>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		*Dst = VectorStep(Src0);
	}
};

namespace VectorVMNoise
{
	int32 P[512] =
	{
		151,160,137,91,90,15,
		131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
		190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
		88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
		77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
		102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
		135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
		5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
		223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
		129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
		251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
		49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
		138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
		151,160,137,91,90,15,
		131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
		190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
		88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
		77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
		102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
		135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
		5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
		223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
		129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
		251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
		49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
		138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
	};

	static FORCEINLINE float Lerp(float X, float A, float B)
	{
		return A + X * (B - A);
	}

	static FORCEINLINE float Fade(float X)
	{
		return X * X * X * (X * (X * 6 - 15) + 10);
	}
	
	static FORCEINLINE float Grad(int32 hash, float x, float y, float z)
	{
		 hash &= 15;
		 float u = (hash < 8) ? x : y;
		 float v = (hash < 4) ? y : ((hash == 12 || hash == 14) ? x : z);
		 return ((hash & 1) == 0 ? u : -u) + ((hash & 2) == 0 ? v : -v);
	}

	struct FScalarKernelNoise3D_iNoise : TTrinaryScalarKernel<FScalarKernelNoise3D_iNoise>
	{
		static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, float* RESTRICT Dst, float X, float Y, float Z)
		{
			float Xfl = FMath::FloorToFloat(X);
			float Yfl = FMath::FloorToFloat(Y);
			float Zfl = FMath::FloorToFloat(Z);
			int32 Xi = (int32)(Xfl) & 255;
			int32 Yi = (int32)(Yfl) & 255;
			int32 Zi = (int32)(Zfl) & 255;
			X -= Xfl;
			Y -= Yfl;
			Z -= Zfl;
			float Xm1 = X - 1.0f;
			float Ym1 = Y - 1.0f;
			float Zm1 = Z - 1.0f;

			int32 A = P[Xi] + Yi;
			int32 AA = P[A] + Zi;	int32 AB = P[A + 1] + Zi;

			int32 B = P[Xi + 1] + Yi;
			int32 BA = P[B] + Zi;	int32 BB = P[B + 1] + Zi;

			float U = Fade(X);
			float V = Fade(Y);
			float W = Fade(Z);

			*Dst =
				Lerp(W,
					Lerp(V,
						Lerp(U,
							Grad(P[AA], X, Y, Z),
							Grad(P[BA], Xm1, Y, Z)),
						Lerp(U,
							Grad(P[AB], X, Ym1, Z),
							Grad(P[BB], Xm1, Ym1, Z))),
					Lerp(V,
						Lerp(U,
							Grad(P[AA + 1], X, Y, Zm1),
							Grad(P[BA + 1], Xm1, Y, Zm1)),
						Lerp(U,
							Grad(P[AB + 1], X, Ym1, Zm1),
							Grad(P[BB + 1], Xm1, Ym1, Zm1))));
		}
	};

	struct FScalarKernelNoise2D_iNoise : TBinaryScalarKernel<FScalarKernelNoise2D_iNoise>
	{
		static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, float* RESTRICT Dst, float X, float Y)
		{
			*Dst = 0.0f;//TODO
		}
	};

	struct FScalarKernelNoise1D_iNoise : TUnaryScalarKernel<FScalarKernelNoise1D_iNoise>
	{
		static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, float* RESTRICT Dst, float X)
		{
			*Dst = 0.0f;//TODO;
		}
	};

	static void Noise1D(FVectorVMContext& Context) { FScalarKernelNoise1D_iNoise::Exec(Context); }
	static void Noise2D(FVectorVMContext& Context) { FScalarKernelNoise2D_iNoise::Exec(Context); }
	static void Noise3D(FVectorVMContext& Context)
	{
		//Basic scalar implementation of perlin's improved noise until I can spend some quality time exploring vectorized implementations of Marc O's noise from Random.ush.
		//http://mrl.nyu.edu/~perlin/noise/
		FScalarKernelNoise3D_iNoise::Exec(Context);
	}

	static void Optimize_Noise1D(FVectorVMCodeOptimizerContext& Context) { FScalarKernelNoise1D_iNoise::Optimize(Context); }
	static void Optimize_Noise2D(FVectorVMCodeOptimizerContext& Context) { FScalarKernelNoise2D_iNoise::Optimize(Context); }
	static void Optimize_Noise3D(FVectorVMCodeOptimizerContext& Context) { FScalarKernelNoise3D_iNoise::Optimize(Context); }
};

//Olaf's orginal curl noise. Needs updating for the new scalar VM and possibly calling Curl Noise to avoid confusion with regular noise?
//Possibly needs to be a data interface as the VM can't output Vectors?
struct FVectorKernelNoise : public TUnaryVectorKernel<FVectorKernelNoise>
{
	static VectorRegister RandomTable[17][17][17];

	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* RESTRICT Dst, VectorRegister Src0)
	{
		const VectorRegister VecSize = MakeVectorRegister(16.0f, 16.0f, 16.0f, 16.0f);

		*Dst = GlobalVectorConstants::FloatZero;
		
		for (uint32 i = 1; i < 2; i++)
		{
			float Di = 0.2f * (1.0f/(1<<i));
			VectorRegister Div = MakeVectorRegister(Di, Di, Di, Di);
			VectorRegister Coords = VectorMod( VectorAbs( VectorMultiply(Src0, Div) ), VecSize );
			const float *CoordPtr = reinterpret_cast<float const*>(&Coords);
			const int32 Cx = CoordPtr[0];
			const int32 Cy = CoordPtr[1];
			const int32 Cz = CoordPtr[2];

			VectorRegister Frac = VectorFractional(Coords);
			VectorRegister Alpha = VectorReplicate(Frac, 0);
			VectorRegister OneMinusAlpha = VectorSubtract(GlobalVectorConstants::FloatOne, Alpha);
			
			VectorRegister XV1 = VectorMultiplyAdd(RandomTable[Cx][Cy][Cz], Alpha, VectorMultiply(RandomTable[Cx+1][Cy][Cz], OneMinusAlpha));
			VectorRegister XV2 = VectorMultiplyAdd(RandomTable[Cx][Cy+1][Cz], Alpha, VectorMultiply(RandomTable[Cx+1][Cy+1][Cz], OneMinusAlpha));
			VectorRegister XV3 = VectorMultiplyAdd(RandomTable[Cx][Cy][Cz+1], Alpha, VectorMultiply(RandomTable[Cx+1][Cy][Cz+1], OneMinusAlpha));
			VectorRegister XV4 = VectorMultiplyAdd(RandomTable[Cx][Cy+1][Cz+1], Alpha, VectorMultiply(RandomTable[Cx+1][Cy+1][Cz+1], OneMinusAlpha));

			Alpha = VectorReplicate(Frac, 1);
			OneMinusAlpha = VectorSubtract(GlobalVectorConstants::FloatOne, Alpha);
			VectorRegister YV1 = VectorMultiplyAdd(XV1, Alpha, VectorMultiply(XV2, OneMinusAlpha));
			VectorRegister YV2 = VectorMultiplyAdd(XV3, Alpha, VectorMultiply(XV4, OneMinusAlpha));

			Alpha = VectorReplicate(Frac, 2);
			OneMinusAlpha = VectorSubtract(GlobalVectorConstants::FloatOne, Alpha);
			VectorRegister ZV = VectorMultiplyAdd(YV1, Alpha, VectorMultiply(YV2, OneMinusAlpha));

			*Dst = VectorAdd(*Dst, ZV);
		}
	}
};

VectorRegister FVectorKernelNoise::RandomTable[17][17][17];

//////////////////////////////////////////////////////////////////////////
//Special Kernels.

/** Special kernel for acquiring a new ID. TODO. Can be written as general RWBuffer ops when we support that. */
struct FScalarKernelAcquireID
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Context.Write(Context.DecodeU16());		// DataSetIndex
		Context.Write(Context.DecodeU16());		// IDIndexReg
		Context.Write(Context.DecodeU16());		// IDTagReg
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		const int32 DataSetIndex = Context.DecodeU16();
		const TArrayView<FDataSetMeta> MetaTable = Context.DataSetMetaTable;
		TArray<int32>&RESTRICT FreeIDTable = *MetaTable[DataSetIndex].FreeIDTable;

		const int32 Tag = MetaTable[DataSetIndex].IDAcquireTag;

		const int32 IDIndexReg = Context.DecodeU16();
		int32*RESTRICT IDIndex = (int32*)(Context.GetTempRegister(IDIndexReg));

		const int32 IDTagReg = Context.DecodeU16();
		int32*RESTRICT IDTag = (int32*)(Context.GetTempRegister(IDTagReg));

		int32& NumFreeIDs = *MetaTable[DataSetIndex].NumFreeIDs;

		//Temporarily using a lock to ensure thread safety for accessing the FreeIDTable until a lock free solution can be implemented.
		MetaTable[DataSetIndex].LockFreeTable();
	
		check(FreeIDTable.Num() >= Context.NumInstances);
		check(NumFreeIDs >= Context.NumInstances);
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 FreeIDTableIndex = --NumFreeIDs;

			//Grab the value from the FreeIDTable.
			int32 AcquiredID = FreeIDTable[FreeIDTableIndex];
			checkSlow(AcquiredID != INDEX_NONE);

			//UE_LOG(LogVectorVM, Warning, TEXT("AcquireID: ID:%d | FreeTableIdx:%d."), AcquiredID, FreeIDTableIndex);
			//Mark this entry in the FreeIDTable as invalid.
			FreeIDTable[FreeIDTableIndex] = INDEX_NONE;

			*IDIndex = AcquiredID;
			*IDTag = Tag;
			++IDIndex;
			++IDTag;
		}

		MetaTable[DataSetIndex].UnlockFreeTable();
	}
};

/** Special kernel for updating a new ID. TODO. Can be written as general RWBuffer ops when we support that. */
struct FScalarKernelUpdateID
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Context.Write(Context.DecodeU16());		// DataSetIndex
		Context.Write(Context.DecodeU16());		// InstanceIDRegisterIndex
		Context.Write(Context.DecodeU16());		// InstanceIndexRegisterIndex
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		const int32 DataSetIndex = Context.DecodeU16();
		const int32 InstanceIDRegisterIndex = Context.DecodeU16();
		const int32 InstanceIndexRegisterIndex = Context.DecodeU16();

		const TArrayView<FDataSetMeta> MetaTable = Context.DataSetMetaTable;

		TArray<int32>&RESTRICT IDTable = *MetaTable[DataSetIndex].IDTable;
		const int32 InstanceOffset = MetaTable[DataSetIndex].InstanceOffset + Context.StartInstance;

		const int32*RESTRICT IDRegister = (int32*)(Context.GetTempRegister(InstanceIDRegisterIndex));
		const int32*RESTRICT IndexRegister = (int32*)(Context.GetTempRegister(InstanceIndexRegisterIndex));
		
		FDataSetThreadLocalTempData& DataSetTempData = Context.ThreadLocalTempData[DataSetIndex];

		TArray<int32>&RESTRICT IDsToFree = DataSetTempData.IDsToFree;
		check(IDTable.Num() >= InstanceOffset + Context.NumInstances);
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			int32 InstanceId = IDRegister[i];
			int32 Index = IndexRegister[i];

			if (Index == INDEX_NONE)
			{
				//Add the ID to a thread local list of IDs to free which are actually added to the list safely at the end of this chunk's execution.
				IDsToFree.Add(InstanceId);
				IDTable[InstanceId] = INDEX_NONE;
				//UE_LOG(LogVectorVM, Warning, TEXT("FreeingID: InstanceID:%d."), InstanceId);
			}
			else
			{
				//Update the actual index for this ID. No thread safety is needed as this ID slot can only ever be written by this instance and so a single thread.
				IDTable[InstanceId] = Index;

				//Update thread local max ID seen. We push this to the real value at the end of execution.
				DataSetTempData.MaxID = FMath::Max(DataSetTempData.MaxID, InstanceId);
				
				//UE_LOG(LogVectorVM, Warning, TEXT("UpdateID: RealIdx:%d | InstanceID:%d."), RealIdx, InstanceId);
			}
		}
	}
};

/** Special kernel for reading from the main input dataset. */
template<typename T>
struct FVectorKernelReadInput
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Context.Write(Context.DecodeU16());	// DataSetIndex
		Context.Write(Context.DecodeU16());	// InputRegisterIdx
		Context.Write(Context.DecodeU16());	// DestRegisterIdx
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		static const int32 InstancesPerVector = sizeof(VectorRegister) / sizeof(T);

		const int32 DataSetIndex = Context.DecodeU16();
		const int32 InputRegisterIdx = Context.DecodeU16();
		const int32 DestRegisterIdx = Context.DecodeU16();
		const int32 Loops = Context.GetNumLoops<InstancesPerVector>();

		VectorRegister* DestReg = (VectorRegister*)(Context.GetTempRegister(DestRegisterIdx));
		VectorRegister* InputReg = (VectorRegister*)(Context.GetInputRegister<T>(DataSetIndex, InputRegisterIdx) + Context.GetStartInstance());

		//TODO: We can actually do some scalar loads into the first and final vectors to get around alignment issues and then use the aligned load for all others.
		for (int32 i = 0; i < Loops; ++i)
		{
			*DestReg = VectorLoad(InputReg);
			++DestReg;
			++InputReg;
		}
	}
};



/** Special kernel for reading from an input dataset; non-advancing (reads same instance everytime). 
 *  this kernel splats the X component of the source register to all 4 dest components; it's meant to
 *	use scalar data sets as the source (e.g. events)
 */
template<typename T>
struct FVectorKernelReadInputNoAdvance
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write<FVectorVMExecFunction>(Exec);
		Context.Write(Context.DecodeU16());	// DataSetIndex
		Context.Write(Context.DecodeU16());	// InputRegisterIdx
		Context.Write(Context.DecodeU16());	// DestRegisterIdx
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		static const int32 InstancesPerVector = sizeof(VectorRegister) / sizeof(T);

		const int32 DataSetIndex = Context.DecodeU16();
		const int32 InputRegisterIdx = Context.DecodeU16();
		const int32 DestRegisterIdx = Context.DecodeU16();
		const int32 Loops = Context.GetNumLoops<InstancesPerVector>();

		VectorRegister* DestReg = (VectorRegister*)(Context.GetTempRegister(DestRegisterIdx));
		VectorRegister* InputReg = (VectorRegister*)(Context.GetInputRegister<T>(DataSetIndex, InputRegisterIdx));

		//TODO: We can actually do some scalar loads into the first and final vectors to get around alignment issues and then use the aligned load for all others.
		for (int32 i = 0; i < Loops; ++i)
		{
			*DestReg = VectorSwizzle(VectorLoad(InputReg), 0,0,0,0);
			++DestReg;
		}
	}
};





//TODO - Should be straight forwards to follow the input with a mix of the outputs direct indexing
/** Special kernel for reading an specific location in an input register. */
// template<typename T>
// struct FScalarKernelReadInputIndexed
// {
// 	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
// 	{
// 		int32* IndexReg = (int32*)(Context.RegisterTable[DecodeU16(Context)]);
// 		T* InputReg = (T*)(Context.RegisterTable[DecodeU16(Context)]);
// 		T* DestReg = (T*)(Context.RegisterTable[DecodeU16(Context)]);
// 
// 		//Has to be scalar as each instance can read from a different location in the input buffer.
// 		for (int32 i = 0; i < Context.NumInstances; ++i)
// 		{
// 			T* ReadPtr = (*InputReg) + (*IndexReg);
// 			*DestReg = (*ReadPtr);
// 			++IndexReg;
// 			++DestReg;
// 		}
// 	}
// };

/** Special kernel for writing to a specific output register. */
template<typename T>
struct FScalarKernelWriteOutputIndexed
{
	static VM_FORCEINLINE void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 SrcOpTypes = Context.BaseContext.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
			case SRCOP_RRR: Context.Write<FVectorVMExecFunction>(DoKernel<FRegisterHandler<T>>); break;
			case SRCOP_RRC:	Context.Write<FVectorVMExecFunction>(DoKernel<FConstantHandler<T>>); break;
			default: check(0); break;
		};

		Context.Write(Context.DecodeU16());		// DataSetIndex
		Context.Write(Context.DecodeU16());		// DestIndexRegisterIdx
		Context.Write(Context.DecodeU16());		// DataHandlerType
		Context.Write(Context.DecodeU16());		// DestRegisterIdx
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		const uint32 SrcOpTypes = Context.DecodeSrcOperandTypes();
		switch (SrcOpTypes)
		{
		case SRCOP_RRR: DoKernel<FRegisterHandler<T>>(Context); break;
		case SRCOP_RRC:	DoKernel<FConstantHandler<T>>(Context); break;
		default: check(0); break;
		};
	}

	template<typename DataHandlerType>
	static VM_FORCEINLINE void DoKernel(FVectorVMContext& Context)
	{
		const int32 DataSetIndex = Context.DecodeU16();

		const int32 DestIndexRegisterIdx = Context.DecodeU16();
		T* DestIndexReg = (T*)(Context.GetTempRegister(DestIndexRegisterIdx));

		DataHandlerType DataHandler(Context);

		const int32 DestRegisterIdx = Context.DecodeU16();
		T* DestReg = Context.GetOutputRegister<T>(DataSetIndex, DestRegisterIdx);

		for (int32 i = 0; i < Context.GetNumInstances(); ++i)
		{
			int32 DestIndex = *DestIndexReg;
			if (DestIndex != INDEX_NONE)
			{
				DestReg[DestIndex] = DataHandler.Get();
			}

			++DestIndexReg;
			DataHandler.Advance();
			//We don't increment the dest as we index into it directly.
		}
	}
};

struct FDataSetCounterHandler
{
	int32* Counter;
	FDataSetCounterHandler(FVectorVMContext& Context)
		: Counter(&Context.GetDataSetMeta(Context.DecodeU16()).DataSetAccessIndex)
	{}

	VM_FORCEINLINE void Advance() { }
	VM_FORCEINLINE int32* Get() { return Counter; }
	VM_FORCEINLINE int32* GetAndAdvance() { return Counter; }
	//VM_FORCEINLINE const int32* GetDest() { return Counter; }Should never use as a dest. All kernels with read and write to this.

	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		Context.Write(Context.DecodeU16());
	}
};

struct FScalarKernelAcquireCounterIndex
{
	template<bool bThreadsafe>
	struct InternalKernel
	{
		static VM_FORCEINLINE void DoKernel(FVectorVMContext& Context, int32* RESTRICT Dst, int32* Index, int32 Valid)
		{
			if (Valid != 0)
			{
				*Dst = bThreadsafe ? FPlatformAtomics::InterlockedIncrement(Index) : ++(*Index);
			}
			else
			{
				*Dst = INDEX_NONE;	// Subsequent DoKernal calls above will skip over INDEX_NONE register entries...
			}
		}

		static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
		{
			const uint32 SrcOpType = Context.DecodeSrcOperandTypes();
			switch (SrcOpType)
			{
				case SRCOP_RRR: TBinaryKernelHandler<InternalKernel<true>, FRegisterHandler<int32>, FDataSetCounterHandler, FRegisterHandler<int32>, 1>::Exec(Context); break;
				case SRCOP_RRC:	TBinaryKernelHandler<InternalKernel<true>, FRegisterHandler<int32>, FDataSetCounterHandler, FConstantHandler<int32>, 1>::Exec(Context); break;
				default: check(0); break;
			};
		}
	};

	template<uint32 SrcOpType>
	static void ExecOptimized(FVectorVMContext& Context)
	{
		if (Context.IsParallelExecution())
		{
			switch (SrcOpType)
			{
				case SRCOP_RRR: TBinaryKernelHandler<InternalKernel<true>, FRegisterHandler<int32>, FDataSetCounterHandler, FRegisterHandler<int32>, 1>::Exec(Context); break;
				case SRCOP_RRC:	TBinaryKernelHandler<InternalKernel<true>, FRegisterHandler<int32>, FDataSetCounterHandler, FConstantHandler<int32>, 1>::Exec(Context); break;
				default: check(0); break;
			}
		}
		else
		{
			switch (SrcOpType)
			{
				case SRCOP_RRR: TBinaryKernelHandler<InternalKernel<false>, FRegisterHandler<int32>, FDataSetCounterHandler, FRegisterHandler<int32>, 1>::Exec(Context); break;
				case SRCOP_RRC:	TBinaryKernelHandler<InternalKernel<false>, FRegisterHandler<int32>, FDataSetCounterHandler, FConstantHandler<int32>, 1>::Exec(Context); break;
				default: check(0); break;
			}
		}
	}

	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 SrcOpType = Context.BaseContext.DecodeSrcOperandTypes();
		switch (SrcOpType)
		{
			case SRCOP_RRR: Context.Write<FVectorVMExecFunction>(FScalarKernelAcquireCounterIndex::ExecOptimized<SRCOP_RRR>); break;
			case SRCOP_RRC: Context.Write<FVectorVMExecFunction>(FScalarKernelAcquireCounterIndex::ExecOptimized<SRCOP_RRC>); break;
			default: check(0); break;
		}

		// Three registers, note we don't call Optimize on the Kernel since that will write the Exec and we are selecting based upon thread safe or not
		Context.Write(Context.DecodeU16());
		Context.Write(Context.DecodeU16());
		Context.Write(Context.DecodeU16());
	}

	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
	{
		if ( Context.IsParallelExecution() )
		{
			InternalKernel<true>::Exec(Context);
		}
		else
		{
			InternalKernel<false>::Exec(Context);
		}
	}

};

//TODO: REWORK TO FUNCITON LIKE THE ABOVE.
// /** Special kernel for decrementing a dataset counter. */
// struct FScalarKernelReleaseCounterIndex
// {
// 	static VM_FORCEINLINE void Exec(FVectorVMContext& Context)
// 	{
// 		int32* CounterPtr = (int32*)(Context.ConstantTable[DecodeU16(Context)]);
// 		int32* DestReg = (int32*)(Context.RegisterTable[DecodeU16(Context)]);
// 
// 		for (int32 i = 0; i < Context.NumInstances; ++i)
// 		{
// 			int32 Counter = (*CounterPtr--);
// 			*DestReg = Counter >= 0 ? Counter : INDEX_NONE;
// 
// 			++DestReg;
// 		}
// 	}
// };

//////////////////////////////////////////////////////////////////////////
//external_func_call

struct FKernelExternalFunctionCall
{
	static void Optimize(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 ExternalFuncIdx = Context.DecodeU8();

		Context.Write<FVectorVMExecFunction>(Exec);
		Context.Write<uint8>(ExternalFuncIdx);

		const int32 NumRegisters = Context.ExternalFunctionRegisterCounts[ExternalFuncIdx];
		for ( int32 i=0; i < NumRegisters; ++i )
		{
			Context.Write(Context.DecodeU16());
		}
	}

	static void Exec(FVectorVMContext& Context)
	{
		const uint32 ExternalFuncIdx = Context.DecodeU8();
		Context.ExternalFunctionTable[ExternalFuncIdx].Execute(Context);
	}
};

//////////////////////////////////////////////////////////////////////////
//Integer operations

//addi,
struct FVectorIntKernelAdd : TBinaryVectorIntKernel<FVectorIntKernelAdd>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntAdd(Src0, Src1);
	}
};

//subi,
struct FVectorIntKernelSubtract : TBinaryVectorIntKernel<FVectorIntKernelSubtract>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntSubtract(Src0, Src1);
	}
};

//muli,
struct FVectorIntKernelMultiply : TBinaryVectorIntKernel<FVectorIntKernelMultiply>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntMultiply(Src0, Src1);
	}
};

//divi,
struct FVectorIntKernelDivide : TBinaryVectorIntKernel<FVectorIntKernelDivide>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		int32 TmpA[4];
		VectorIntStore(Src0, TmpA);

		int32 TmpB[4];
		VectorIntStore(Src1, TmpB);

		// No intrinsics exist for integer divide. Since div by zero causes crashes, we must be safe against that.

		int32 TmpDst[4];
		TmpDst[0] = TmpB[0] != 0 ? (TmpA[0] / TmpB[0]) : 0;
		TmpDst[1] = TmpB[1] != 0 ? (TmpA[1] / TmpB[1]) : 0;
		TmpDst[2] = TmpB[2] != 0 ? (TmpA[2] / TmpB[2]) : 0;
		TmpDst[3] = TmpB[3] != 0 ? (TmpA[3] / TmpB[3]) : 0;

		*Dst = MakeVectorRegisterInt(TmpDst[0], TmpDst[1], TmpDst[2], TmpDst[3]);
	}
};


//clampi,
struct FVectorIntKernelClamp : TTrinaryVectorIntKernel<FVectorIntKernelClamp>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1, VectorRegisterInt Src2)
	{
		*Dst = VectorIntMin(VectorIntMax(Src0, Src1), Src2);
	}
};

//mini,
struct FVectorIntKernelMin : TBinaryVectorIntKernel<FVectorIntKernelMin>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntMin(Src0, Src1);
	}
};

//maxi,
struct FVectorIntKernelMax : TBinaryVectorIntKernel<FVectorIntKernelMax>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntMax(Src0, Src1);
	}
};

//absi,
struct FVectorIntKernelAbs : TUnaryVectorIntKernel<FVectorIntKernelAbs>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntAbs(Src0);
	}
};

//negi,
struct FVectorIntKernelNegate : TUnaryVectorIntKernel<FVectorIntKernelNegate>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntNegate(Src0);
	}
};

//signi,
struct FVectorIntKernelSign : TUnaryVectorIntKernel<FVectorIntKernelSign>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntSign(Src0);
	}
};

//randomi,
//No good way to do this with SSE atm so just do it scalar.
struct FScalarIntKernelRandom : public TUnaryScalarIntKernel<FScalarIntKernelRandom>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, int32* RESTRICT Dst, int32 Src0)
	{
		const float rm = RAND_MAX;
		//EEK!. Improve this. Implement GPU style seeded rand instead of this.
		*Dst = static_cast<int32>(Context.RandStream.GetFraction() * Src0);
	}
};

//cmplti,
struct FVectorIntKernelCompareLT : TBinaryVectorIntKernel<FVectorIntKernelCompareLT>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntCompareLT(Src0, Src1);
	}
};

//cmplei,
struct FVectorIntKernelCompareLE : TBinaryVectorIntKernel<FVectorIntKernelCompareLE>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntCompareLE(Src0, Src1);
	}
};

//cmpgti,
struct FVectorIntKernelCompareGT : TBinaryVectorIntKernel<FVectorIntKernelCompareGT>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntCompareGT(Src0, Src1);
	}
};

//cmpgei,
struct FVectorIntKernelCompareGE : TBinaryVectorIntKernel<FVectorIntKernelCompareGE>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntCompareGE(Src0, Src1);
	}
};

//cmpeqi,
struct FVectorIntKernelCompareEQ : TBinaryVectorIntKernel<FVectorIntKernelCompareEQ>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntCompareEQ(Src0, Src1);
	}
};

//cmpneqi,
struct FVectorIntKernelCompareNEQ : TBinaryVectorIntKernel<FVectorIntKernelCompareNEQ>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntCompareNEQ(Src0, Src1);
	}
};

//bit_and,
struct FVectorIntKernelBitAnd : TBinaryVectorIntKernel<FVectorIntKernelBitAnd>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntAnd(Src0, Src1);
	}
};

//bit_or,
struct FVectorIntKernelBitOr : TBinaryVectorIntKernel<FVectorIntKernelBitOr>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntOr(Src0, Src1);
	}
};

//bit_xor,
struct FVectorIntKernelBitXor : TBinaryVectorIntKernel<FVectorIntKernelBitXor>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		*Dst = VectorIntXor(Src0, Src1);
	}
};

//bit_not,
struct FVectorIntKernelBitNot : TUnaryVectorIntKernel<FVectorIntKernelBitNot>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntNot(Src0);
	}
};

// bit_lshift
struct FVectorIntKernelBitLShift : TBinaryVectorIntKernel<FVectorIntKernelBitLShift>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0,  VectorRegisterInt Src1)
	{
		int32 TmpA[4];
		VectorIntStore(Src0, TmpA);

		int32 TmpB[4];
		VectorIntStore(Src1, TmpB);

		int32 TmpDst[4];
		TmpDst[0] = (TmpA[0] << TmpB[0]);
		TmpDst[1] = (TmpA[1] << TmpB[1]);
		TmpDst[2] = (TmpA[2] << TmpB[2]);
		TmpDst[3] = (TmpA[3] << TmpB[3]);
		*Dst = MakeVectorRegisterInt(TmpDst[0], TmpDst[1], TmpDst[2], TmpDst[3] );
	}
};

// bit_rshift
struct FVectorIntKernelBitRShift : TBinaryVectorIntKernel<FVectorIntKernelBitRShift>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		int32 TmpA[4];
		VectorIntStore(Src0, TmpA);

		int32 TmpB[4];
		VectorIntStore(Src1, TmpB);

		int32 TmpDst[4];
		TmpDst[0] = (TmpA[0] >> TmpB[0]);
		TmpDst[1] = (TmpA[1] >> TmpB[1]);
		TmpDst[2] = (TmpA[2] >> TmpB[2]);
		TmpDst[3] = (TmpA[3] >> TmpB[3]);
		*Dst = MakeVectorRegisterInt(TmpDst[0], TmpDst[1], TmpDst[2], TmpDst[3]);
	}
};

//"Boolean" ops. Currently handling bools as integers.
//logic_and,
struct FVectorIntKernelLogicAnd : TBinaryVectorIntKernel<FVectorIntKernelLogicAnd>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		//We need to assume a mask input and produce a mask output so just bitwise ops actually fine for these?
		*Dst = VectorIntAnd(Src0, Src1);
	}
};

//logic_or,
struct FVectorIntKernelLogicOr : TBinaryVectorIntKernel<FVectorIntKernelLogicOr>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		//We need to assume a mask input and produce a mask output so just bitwise ops actually fine for these?
		*Dst = VectorIntOr(Src0, Src1);
	}
};
//logic_xor,
struct FVectorIntKernelLogicXor : TBinaryVectorIntKernel<FVectorIntKernelLogicXor>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0, VectorRegisterInt Src1)
	{
		//We need to assume a mask input and produce a mask output so just bitwise ops actually fine for these?
		*Dst = VectorIntXor(Src0, Src1);
	}
};

//logic_not,
struct FVectorIntKernelLogicNot : TUnaryVectorIntKernel<FVectorIntKernelLogicNot>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		//We need to assume a mask input and produce a mask output so just bitwise ops actually fine for these?
		*Dst = VectorIntNot(Src0);
	}
};

//conversions
//f2i,
struct FVectorKernelFloatToInt : TUnaryKernel<FVectorKernelFloatToInt, FRegisterHandler<VectorRegisterInt>, FConstantHandler<VectorRegister>, FRegisterHandler<VectorRegister>, VECTOR_WIDTH_FLOATS>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegister Src0)
	{
		*Dst = VectorFloatToInt(Src0);
	}
};

//i2f,
struct FVectorKernelIntToFloat : TUnaryKernel<FVectorKernelIntToFloat, FRegisterHandler<VectorRegister>, FConstantHandler<VectorRegisterInt>, FRegisterHandler<VectorRegisterInt>, VECTOR_WIDTH_FLOATS>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntToFloat(Src0);
	}
};

//f2b,
struct FVectorKernelFloatToBool : TUnaryKernel<FVectorKernelFloatToBool, FRegisterHandler<VectorRegister>, FConstantHandler<VectorRegister>, FRegisterHandler<VectorRegister>, VECTOR_WIDTH_FLOATS>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* Dst, VectorRegister Src0)
	{		
		*Dst = VectorCompareGT(Src0, GlobalVectorConstants::FloatZero);
	}
};

//b2f,
struct FVectorKernelBoolToFloat : TUnaryKernel<FVectorKernelBoolToFloat, FRegisterHandler<VectorRegister>, FConstantHandler<VectorRegister>, FRegisterHandler<VectorRegister>, VECTOR_WIDTH_FLOATS>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegister* Dst, VectorRegister Src0)
	{
		*Dst = VectorSelect(Src0, GlobalVectorConstants::FloatOne, GlobalVectorConstants::FloatZero);
	}
};

//i2b,
struct FVectorKernelIntToBool : TUnaryKernel<FVectorKernelIntToBool, FRegisterHandler<VectorRegisterInt>, FConstantHandler<VectorRegisterInt>, FRegisterHandler<VectorRegisterInt>, VECTOR_WIDTH_FLOATS>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntCompareGT(Src0, GlobalVectorConstants::IntZero);
	}
};

//b2i,
struct FVectorKernelBoolToInt : TUnaryKernel<FVectorKernelBoolToInt, FRegisterHandler<VectorRegisterInt>, FConstantHandler<VectorRegisterInt>, FRegisterHandler<VectorRegisterInt>, VECTOR_WIDTH_FLOATS>
{
	static void VM_FORCEINLINE DoKernel(FVectorVMContext& Context, VectorRegisterInt* Dst, VectorRegisterInt Src0)
	{
		*Dst = VectorIntSelect(Src0, GlobalVectorConstants::IntOne, GlobalVectorConstants::IntZero);
	}
};

#if WITH_EDITOR
UEnum* g_VectorVMEnumStateObj = nullptr;
UEnum* g_VectorVMEnumOperandObj = nullptr;
#endif


void VectorVM::Init()
{
	static bool Inited = false;
	if (Inited == false)
	{
#if WITH_EDITOR
		g_VectorVMEnumStateObj = StaticEnum<EVectorVMOp>();
		g_VectorVMEnumOperandObj = StaticEnum<EVectorVMOperandLocation>();
#endif

		// random noise
		float TempTable[17][17][17];
		for (int z = 0; z < 17; z++)
		{
			for (int y = 0; y < 17; y++)
			{
				for (int x = 0; x < 17; x++)
				{
					float f1 = (float)FMath::FRandRange(-1.0f, 1.0f);
					TempTable[x][y][z] = f1;
				}
			}
		}

		// pad
		for (int i = 0; i < 17; i++)
		{
			for (int j = 0; j < 17; j++)
			{
				TempTable[i][j][16] = TempTable[i][j][0];
				TempTable[i][16][j] = TempTable[i][0][j];
				TempTable[16][j][i] = TempTable[0][j][i];
			}
		}

		// compute gradients
		FVector TempTable2[17][17][17];
		for (int z = 0; z < 16; z++)
		{
			for (int y = 0; y < 16; y++)
			{
				for (int x = 0; x < 16; x++)
				{
					FVector XGrad = FVector(1.0f, 0.0f, TempTable[x][y][z] - TempTable[x+1][y][z]);
					FVector YGrad = FVector(0.0f, 1.0f, TempTable[x][y][z] - TempTable[x][y + 1][z]);
					FVector ZGrad = FVector(0.0f, 1.0f, TempTable[x][y][z] - TempTable[x][y][z+1]);

					FVector Grad = FVector(XGrad.Z, YGrad.Z, ZGrad.Z);
					TempTable2[x][y][z] = Grad;
				}
			}
		}

		// pad
		for (int i = 0; i < 17; i++)
		{
			for (int j = 0; j < 17; j++)
			{
				TempTable2[i][j][16] = TempTable2[i][j][0];
				TempTable2[i][16][j] = TempTable2[i][0][j];
				TempTable2[16][j][i] = TempTable2[0][j][i];
			}
		}


		// compute curl of gradient field
		for (int z = 0; z < 16; z++)
		{
			for (int y = 0; y < 16; y++)
			{
				for (int x = 0; x < 16; x++)
				{
					FVector Dy = TempTable2[x][y][z] - TempTable2[x][y + 1][z];
					FVector Sy = TempTable2[x][y][z] + TempTable2[x][y + 1][z];
					FVector Dx = TempTable2[x][y][z] - TempTable2[x + 1][y][z];
					FVector Sx = TempTable2[x][y][z] + TempTable2[x + 1][y][z];
					FVector Dz = TempTable2[x][y][z] - TempTable2[x][y][z + 1];
					FVector Sz = TempTable2[x][y][z] + TempTable2[x][y][z + 1];
					FVector Dir = FVector(Dy.Z - Sz.Y, Dz.X - Sx.Z, Dx.Y - Sy.X);

					FVectorKernelNoise::RandomTable[x][y][z] = MakeVectorRegister(Dir.X, Dir.Y, Dir.Z, 0.0f);
				}
			}
		}

		Inited = true;
	}
}

void VectorVM::Exec(
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
	)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE("VMExec");
	SCOPE_CYCLE_COUNTER(STAT_VVMExec);

	const int32 MaxInstances = FMath::Min(GParallelVVMInstancesPerChunk, NumInstances);
	const int32 NumChunks = (NumInstances / GParallelVVMInstancesPerChunk) + 1;
	const int32 ChunksPerBatch = (GbParallelVVM != 0 && FApp::ShouldUseThreadingForPerformance()) ? GParallelVVMChunksPerBatch : NumChunks;
	const int32 NumBatches = FMath::DivideAndRoundUp(NumChunks, ChunksPerBatch);
	const bool bParallel = NumBatches > 1;
	const bool bUseOptimizedByteCode = (OptimizedByteCode != nullptr) && GbUseOptimizedVMByteCode;

	auto ExecChunkBatch = [&](int32 BatchIdx)
	{
		//SCOPE_CYCLE_COUNTER(STAT_VVMExecChunk);

		FVectorVMContext& Context = FVectorVMContext::Get();
		Context.PrepareForExec(NumTempRegisters, ConstantTable, ExternalFunctionTable, UserPtrTable, DataSetMetaTable, MaxInstances, bParallel);
#if STATS
		Context.SetStatScopes(&StatScopes);
#endif

		// Process one chunk at a time.
		int32 ChunkIdx = BatchIdx * ChunksPerBatch;
		const int32 FirstInstance = ChunkIdx * GParallelVVMInstancesPerChunk;
		const int32 FinalInstance = FMath::Min(NumInstances, FirstInstance + (ChunksPerBatch * GParallelVVMInstancesPerChunk));
		int32 InstancesLeft = FinalInstance - FirstInstance;
		while (InstancesLeft > 0)
		{
			int32 NumInstancesThisChunk = FMath::Min(InstancesLeft, (int32)GParallelVVMInstancesPerChunk);
			int32 StartInstance = GParallelVVMInstancesPerChunk * ChunkIdx;

			// Execute optimized byte code version
			if ( bUseOptimizedByteCode )
			{
				// Setup execution context.
				Context.PrepareForChunk(OptimizedByteCode, NumInstancesThisChunk, StartInstance);

				while (true)
				{
					FVectorVMExecFunction ExecFunction = reinterpret_cast<FVectorVMExecFunction>(Context.DecodePtr());
					if (ExecFunction == nullptr)
					{
						break;
					}
					ExecFunction(Context);
				}
			}
			else
			{
				// Setup execution context.
				Context.PrepareForChunk(ByteCode, NumInstancesThisChunk, StartInstance);

				// Execute VM on all vectors in this chunk.
				EVectorVMOp Op = EVectorVMOp::done;
				do
				{
					Op = Context.DecodeOp();
					switch (Op)
					{
						// Dispatch kernel ops.
						case EVectorVMOp::add: FVectorKernelAdd::Exec(Context); break;
						case EVectorVMOp::sub: FVectorKernelSub::Exec(Context); break;
						case EVectorVMOp::mul: FVectorKernelMul::Exec(Context); break;
						case EVectorVMOp::div: FVectorKernelDivSafe::Exec(Context); break;
						case EVectorVMOp::mad: FVectorKernelMad::Exec(Context); break;
						case EVectorVMOp::lerp: FVectorKernelLerp::Exec(Context); break;
						case EVectorVMOp::rcp: FVectorKernelRcpSafe::Exec(Context); break;
						case EVectorVMOp::rsq: FVectorKernelRsqSafe::Exec(Context); break;
						case EVectorVMOp::sqrt: FVectorKernelSqrtSafe::Exec(Context); break;
						case EVectorVMOp::neg: FVectorKernelNeg::Exec(Context); break;
						case EVectorVMOp::abs: FVectorKernelAbs::Exec(Context); break;
						case EVectorVMOp::exp: FVectorKernelExp::Exec(Context); break;
						case EVectorVMOp::exp2: FVectorKernelExp2::Exec(Context); break;
						case EVectorVMOp::log: FVectorKernelLogSafe::Exec(Context); break;
						case EVectorVMOp::log2: FVectorKernelLog2::Exec(Context); break;
						case EVectorVMOp::sin: FVectorKernelSin::Exec(Context); break;
						case EVectorVMOp::cos: FVectorKernelCos::Exec(Context); break;
						case EVectorVMOp::tan: FVectorKernelTan::Exec(Context); break;
						case EVectorVMOp::asin: FVectorKernelASin::Exec(Context); break;
						case EVectorVMOp::acos: FVectorKernelACos::Exec(Context); break;
						case EVectorVMOp::atan: FVectorKernelATan::Exec(Context); break;
						case EVectorVMOp::atan2: FVectorKernelATan2::Exec(Context); break;
						case EVectorVMOp::ceil: FVectorKernelCeil::Exec(Context); break;
						case EVectorVMOp::floor: FVectorKernelFloor::Exec(Context); break;
						case EVectorVMOp::round: FVectorKernelRound::Exec(Context); break;
						case EVectorVMOp::fmod: FVectorKernelMod::Exec(Context); break;
						case EVectorVMOp::frac: FVectorKernelFrac::Exec(Context); break;
						case EVectorVMOp::trunc: FVectorKernelTrunc::Exec(Context); break;
						case EVectorVMOp::clamp: FVectorKernelClamp::Exec(Context); break;
						case EVectorVMOp::min: FVectorKernelMin::Exec(Context); break;
						case EVectorVMOp::max: FVectorKernelMax::Exec(Context); break;
						case EVectorVMOp::pow: FVectorKernelPowSafe::Exec(Context); break;
						case EVectorVMOp::sign: FVectorKernelSign::Exec(Context); break;
						case EVectorVMOp::step: FVectorKernelStep::Exec(Context); break;
						case EVectorVMOp::random: FVectorKernelRandom::Exec(Context); break;
						case EVectorVMOp::noise: VectorVMNoise::Noise1D(Context); break;
						case EVectorVMOp::noise2D: VectorVMNoise::Noise2D(Context); break;
						case EVectorVMOp::noise3D: VectorVMNoise::Noise3D(Context); break;

						case EVectorVMOp::cmplt: FVectorKernelCompareLT::Exec(Context); break;
						case EVectorVMOp::cmple: FVectorKernelCompareLE::Exec(Context); break;
						case EVectorVMOp::cmpgt: FVectorKernelCompareGT::Exec(Context); break;
						case EVectorVMOp::cmpge: FVectorKernelCompareGE::Exec(Context); break;
						case EVectorVMOp::cmpeq: FVectorKernelCompareEQ::Exec(Context); break;
						case EVectorVMOp::cmpneq: FVectorKernelCompareNEQ::Exec(Context); break;
						case EVectorVMOp::select: FVectorKernelSelect::Exec(Context); break;

						case EVectorVMOp::addi: FVectorIntKernelAdd::Exec(Context); break;
						case EVectorVMOp::subi: FVectorIntKernelSubtract::Exec(Context); break;
						case EVectorVMOp::muli: FVectorIntKernelMultiply::Exec(Context); break;
						case EVectorVMOp::divi: FVectorIntKernelDivide::Exec(Context); break;
						case EVectorVMOp::clampi: FVectorIntKernelClamp::Exec(Context); break;
						case EVectorVMOp::mini: FVectorIntKernelMin::Exec(Context); break;
						case EVectorVMOp::maxi: FVectorIntKernelMax::Exec(Context); break;
						case EVectorVMOp::absi: FVectorIntKernelAbs::Exec(Context); break;
						case EVectorVMOp::negi: FVectorIntKernelNegate::Exec(Context); break;
						case EVectorVMOp::signi: FVectorIntKernelSign::Exec(Context); break;
						case EVectorVMOp::randomi: FScalarIntKernelRandom::Exec(Context); break;
						case EVectorVMOp::cmplti: FVectorIntKernelCompareLT::Exec(Context); break;
						case EVectorVMOp::cmplei: FVectorIntKernelCompareLE::Exec(Context); break;
						case EVectorVMOp::cmpgti: FVectorIntKernelCompareGT::Exec(Context); break;
						case EVectorVMOp::cmpgei: FVectorIntKernelCompareGE::Exec(Context); break;
						case EVectorVMOp::cmpeqi: FVectorIntKernelCompareEQ::Exec(Context); break;
						case EVectorVMOp::cmpneqi: FVectorIntKernelCompareNEQ::Exec(Context); break;
						case EVectorVMOp::bit_and: FVectorIntKernelBitAnd::Exec(Context); break;
						case EVectorVMOp::bit_or: FVectorIntKernelBitOr::Exec(Context); break;
						case EVectorVMOp::bit_xor: FVectorIntKernelBitXor::Exec(Context); break;
						case EVectorVMOp::bit_not: FVectorIntKernelBitNot::Exec(Context); break;
						case EVectorVMOp::bit_lshift: FVectorIntKernelBitLShift::Exec(Context); break;
						case EVectorVMOp::bit_rshift: FVectorIntKernelBitRShift::Exec(Context); break;
						case EVectorVMOp::logic_and: FVectorIntKernelLogicAnd::Exec(Context); break;
						case EVectorVMOp::logic_or: FVectorIntKernelLogicOr::Exec(Context); break;
						case EVectorVMOp::logic_xor: FVectorIntKernelLogicXor::Exec(Context); break;
						case EVectorVMOp::logic_not: FVectorIntKernelLogicNot::Exec(Context); break;
						case EVectorVMOp::f2i: FVectorKernelFloatToInt::Exec(Context); break;
						case EVectorVMOp::i2f: FVectorKernelIntToFloat::Exec(Context); break;
						case EVectorVMOp::f2b: FVectorKernelFloatToBool::Exec(Context); break;
						case EVectorVMOp::b2f: FVectorKernelBoolToFloat::Exec(Context); break;
						case EVectorVMOp::i2b: FVectorKernelIntToBool::Exec(Context); break;
						case EVectorVMOp::b2i: FVectorKernelBoolToInt::Exec(Context); break;

						case EVectorVMOp::outputdata_32bit:	FScalarKernelWriteOutputIndexed<int32>::Exec(Context);	break;
						case EVectorVMOp::inputdata_32bit: FVectorKernelReadInput<int32>::Exec(Context); break;
						case EVectorVMOp::inputdata_noadvance_32bit: FVectorKernelReadInputNoAdvance<int32>::Exec(Context); break;
						case EVectorVMOp::acquireindex:	FScalarKernelAcquireCounterIndex::Exec(Context); break;
						case EVectorVMOp::external_func_call: FKernelExternalFunctionCall::Exec(Context); break;

						case EVectorVMOp::exec_index: FVectorKernelExecutionIndex::Exec(Context); break;

						case EVectorVMOp::enter_stat_scope: FVectorKernelEnterStatScope::Exec(Context); break;
						case EVectorVMOp::exit_stat_scope: FVectorKernelExitStatScope::Exec(Context); break;

						//Special case ops to handle unique IDs but this can be written as generalized buffer operations. TODO!
						case EVectorVMOp::update_id:	FScalarKernelUpdateID::Exec(Context); break;
						case EVectorVMOp::acquire_id:	FScalarKernelAcquireID::Exec(Context); break;

						// Execution always terminates with a "done" opcode.
						case EVectorVMOp::done:
							break;

						// Opcode not recognized / implemented.
						default:
							UE_LOG(LogVectorVM, Fatal, TEXT("Unknown op code 0x%02x"), (uint32)Op);
							return;//BAIL
					}
				} while (Op != EVectorVMOp::done);
			}

			InstancesLeft -= GParallelVVMInstancesPerChunk;
			++ChunkIdx;
		}
		Context.FinishExec();
	};

	if ( NumBatches > 1 )
	{
		ParallelFor(NumBatches, ExecChunkBatch, GbParallelVVM == 0 || !bParallel);
	}
	else
	{
		ExecChunkBatch(0);
	}
}

uint8 VectorVM::GetNumOpCodes()
{
	return (uint8)EVectorVMOp::NumOpcodes;
}

#if WITH_EDITOR
FString VectorVM::GetOpName(EVectorVMOp Op)
{
	check(g_VectorVMEnumStateObj);

	FString OpStr = g_VectorVMEnumStateObj->GetNameByValue((uint8)Op).ToString();
	int32 LastIdx = 0;
	OpStr.FindLastChar(TEXT(':'),LastIdx);
	return OpStr.RightChop(LastIdx);
}

FString VectorVM::GetOperandLocationName(EVectorVMOperandLocation Location)
{
	check(g_VectorVMEnumOperandObj);

	FString LocStr = g_VectorVMEnumOperandObj->GetNameByValue((uint8)Location).ToString();
	int32 LastIdx = 0;
	LocStr.FindLastChar(TEXT(':'), LastIdx);
	return LocStr.RightChop(LastIdx);
}
#endif

// local implementation of VectorIntShuffle for neon/directx/


// Optimization managed by GbBatchPackVMOutput via PackedOutputOptimization()
// Looks for the common pattern of an acquireindex op followed by a number of associated outputdata_32bit ops.  The
// stock operation is to write an index into a temporary register, and then have the different outputs streams
// write into the indexed location.  This optimization does a number of things:
// -first we check if 'validity' is uniform or not, if it is we can have a fast path of both figuring out how many
// indices we need, as well as how to write the output (if we find that they are all invalid, then we don't need to do anything!)
// -if we need to evaluate the validity of each element we quickly count up the number (with vector intrinsics) and
// grab a block of the indices (rather than one at a time)
// -rather than storing the indices to use, we store a int8 mask which indicates a valid flag for each of the next 4 samples
// -outputs are then written to depending on their source and their frequency:
//		-uniform sources will be splatted to all valid entries
//		-variable sources will be packed into the available slots
struct FBatchedWriteIndexedOutput
{
	// functor for copying a source register to an output register
	struct FCopyOp
	{
		void VM_FORCEINLINE operator()(FVectorVMContext& Context, uint16 DataSetIndex)
		{
			FRegisterHandler<int32> SourceRegister(Context);
			const uint16 DestRegisterIdx = Context.DecodeU16();

			int32* DestReg = Context.GetOutputRegister<int32>(DataSetIndex, DestRegisterIdx) + Context.ValidInstanceIndexStart;

			FMemory::StreamingMemcpy(DestReg, SourceRegister.GetDest(), sizeof(int32) * Context.ValidInstanceCount);
		}
	};

	// functor for splatting a constant value to an output register
	template<typename InputHandler>
	struct FSplatOp
	{
		void VM_FORCEINLINE operator()(FVectorVMContext& Context, uint16 DataSetIndex)
		{
			InputHandler SourceRegister(Context);
			const uint16 DestRegisterIdx = Context.DecodeU16();

			int32* DestReg = Context.GetOutputRegister<int32>(DataSetIndex, DestRegisterIdx) + Context.ValidInstanceIndexStart;

			const int32 SourceValue = SourceRegister.Get();
			const int32 InstanceVectorCount = FMath::DivideAndRoundDown(Context.ValidInstanceCount, VECTOR_WIDTH_FLOATS);

			if (InstanceVectorCount)
			{
				const VectorRegisterInt SplatValue = MakeVectorRegisterInt(SourceValue, SourceValue, SourceValue, SourceValue);

				for (int32 VectorIt = 0; VectorIt < InstanceVectorCount; ++VectorIt)
				{
					VectorIntStore(SplatValue, DestReg + VectorIt * VECTOR_WIDTH_FLOATS);
				}
			}

			for (int32 InstanceIt = InstanceVectorCount * VECTOR_WIDTH_FLOATS; InstanceIt < Context.ValidInstanceCount; ++InstanceIt)
			{
				DestReg[InstanceIt] = SourceValue;
			}
		}
	};

	// performs the operation of copying data from a temporary register to an output register under the assumption
	// that the validity of each instance is uniform (valid or not).
	template<typename PopulateOp>
	static VM_FORCEINLINE void DoRegisterKernelFixedValid(FVectorVMContext& Context)
	{
		const uint16 DataSetIndex = Context.DecodeU16();
		Context.DecodeU16(); // DestIndexRegisterIdx
		const uint16 AccumulatedOpCount = Context.DecodeU16();

		// if none of the instances are valid, then don't bother writing anything
		if (!Context.ValidInstanceCount)
		{
			// todo we should early out of this case rather than keep parsing the code
			for (uint16 OpIt = 0; OpIt < AccumulatedOpCount; ++OpIt)
			{
				FRegisterHandler<int32> Dummy(Context);
				Context.DecodeU16(); // DestRegisterIdx
			}

			return;
		}

		// for each of our ops, copy the data from the working register to the output
		const int32 DataSize = sizeof(int32) * Context.ValidInstanceCount;

		for (uint16 OpIt = 0; OpIt < AccumulatedOpCount; ++OpIt)
		{
			PopulateOp()(Context, DataSetIndex);
		}
	}

	// performs the operation of copying data from a temporary register to an output register without foreknowledge
	// of the validity of individual instances
	static VM_FORCEINLINE void DoRegisterKernelVariableValid(FVectorVMContext& Context)
	{
		// if we found that all of the instances are valid, then just run the fixed version
		if (Context.ValidInstanceUniform)
		{
			DoRegisterKernelFixedValid<FCopyOp>(Context);
			return;
		}

		const uint16 DataSetIndex = Context.DecodeU16();
		const uint16 DestIndexRegisterIdx = Context.DecodeU16();
		const uint16 AccumulatedOpCount = Context.DecodeU16();

		FDataSetMeta& DataSetMeta = Context.GetDataSetMeta(DataSetIndex);
		
		const int8* DestIndexReg = reinterpret_cast<const int8*>(Context.GetTempRegister(DestIndexRegisterIdx));

		//
		// VectorIntStore(		- unaligned writes of 16 bytes to our Destination; note that this maneuver requires us to have
		//						our output buffers padded out to 16 bytes!
		//	VectorIntShuffle(	- swizzle our source register to pack the valid entries at the beginning, with 0s at the end
		//    Source,			- source data
		//    ShuffleMask),		- result of the VectorMaskBits done in the acquireindex, int8/VectorRegister of input
		//  Destination);
		for (uint16 OpIt = 0; OpIt < AccumulatedOpCount; ++OpIt)
		{
			const RegisterType* Source = FRegisterHandler<RegisterType>(Context).GetDest();
			int32* DestReg = Context.GetOutputRegister<int32>(DataSetIndex, Context.DecodeU16()) + Context.ValidInstanceIndexStart;

			// the number of instances that we're expecting to write.  it is important that we keep track of it because when we
			// get down to the end we need to switch from the shuffled approach to a scalar approach so that we don't
			// overwrite the indexed output that another parallel context might have written to
			int32 WritesRemaining = Context.ValidInstanceCount;
			int32 SourceIt = 0;

			// vector shuffle path writes 4 at a time (though the trailing elements may not be valid) until we have to move over
			// to the scalar version for fear of overwriting our neighbors
			while (WritesRemaining >= VECTOR_WIDTH_FLOATS)
			{
				check(SourceIt * VECTOR_WIDTH_FLOATS < Context.NumInstances);

				const int8 ShuffleMask = DestIndexReg[SourceIt];
				const int8 AdvanceCount = FMath::CountBits(ShuffleMask);

				VectorIntStore(VectorIntShuffle(Source[SourceIt], VectorVMConstants::RegisterShuffleMask[ShuffleMask]), DestReg);

				DestReg += AdvanceCount;
				WritesRemaining -= AdvanceCount;

				++SourceIt;
			}

			// scalar path that will read 4 values and write one at a time to the output based on the valid mask
			while (WritesRemaining)
			{
				const int8 ShuffleMask = DestIndexReg[SourceIt];
				const int8 AdvanceCount = FMath::CountBits(ShuffleMask);
				if (AdvanceCount)
				{
					int32 RawSourceData[VECTOR_WIDTH_FLOATS];

					VectorIntStore(Source[SourceIt], RawSourceData);

					for (int32 ScalarIt = 0; ScalarIt < 4; ++ScalarIt)
					{
						if (!!(ShuffleMask & (1 << ScalarIt)))
						{
							*DestReg = RawSourceData[ScalarIt];
							++DestReg;
						}
					}

					WritesRemaining -= AdvanceCount;
				}
				++SourceIt;
			}
		}
	}

	// acquires a batch of indices from the provided CounterHandler.  If we're running in parallel, then we'll need to use
	// atomics to guarantee our place in the list of indices.
	template<bool bParallel>
	static VM_FORCEINLINE void AcquireCounterIndex(FVectorVMContext& Context, FDataSetCounterHandler& CounterHandler, int32 AcquireCount)
	{
		if (AcquireCount)
		{
			int32* CounterHandlerIndex = CounterHandler.Get();
			int32 StartIndex = INDEX_NONE;

			if (bParallel)
			{
				StartIndex = FPlatformAtomics::InterlockedAdd(CounterHandlerIndex, AcquireCount);
			}
			else
			{
				StartIndex = *CounterHandlerIndex;
				*CounterHandlerIndex = StartIndex + AcquireCount;
			}

			// increment StartIndex, since CounterHandlerIndex starts at INDEX_NONE
			Context.ValidInstanceIndexStart = StartIndex + 1;
		}

		Context.ValidInstanceCount = AcquireCount;
		Context.ValidInstanceUniform = !AcquireCount || (Context.NumInstances == AcquireCount);
	}

	// evaluates a register to evaluate which instances are valid or not; will read 4 entries at a time and generate a
	// a mask for which entries are valid as well as an overall count
	template<bool bParallel>
	static void HandleRegisterValidIndices(FVectorVMContext& Context)
	{
		FDataSetCounterHandler CounterHandler(Context);
		FRegisterHandler<VectorRegister> ValidReader(Context);
		FRegisterHandler<int8> Dst(Context);

		int8* DestAddr = Dst.GetDest();

		// we can process VECTOR_WIDTH_FLOATS entries at a time, generating a int8 mask for each set of 4 indicating
		// which are valid
		const int32 LoopCount = FMath::DivideAndRoundUp(Context.NumInstances, VECTOR_WIDTH_FLOATS);

		int32 Remainder = Context.NumInstances;
		int32 ValidCount = 0;
		for (int32 LoopIt = 0; LoopIt < LoopCount; ++LoopIt)
		{
			// input register needs to be padded to allow for 16 byte reads; but mask out the ones beyond NumInstances
			const VectorRegister Mask = VectorVMConstants::RemainderMask[FMath::Min(VECTOR_WIDTH_FLOATS, Remainder)];

			const int8 ValidMask = static_cast<int8>(VectorMaskBits(VectorSelect(Mask, ValidReader.GetAndAdvance(), GlobalVectorConstants::FloatZero)));
			ValidCount += FMath::CountBits(ValidMask);

			DestAddr[LoopIt] = ValidMask;

			Remainder -= VECTOR_WIDTH_FLOATS;
		}

		// grab our batch of indices
		AcquireCounterIndex<bParallel>(Context, CounterHandler, ValidCount);
	}

	// evaluates the uniform check and grab the appropriate number of indices
	template<typename ValidReaderType, bool bParallel>
	static VM_FORCEINLINE void HandleUniformValidIndices(FVectorVMContext& Context)
	{
		FDataSetCounterHandler CounterHandler(Context);
		ValidReaderType ValidReader(Context);

		if (ValidReader.Get())
		{
			AcquireCounterIndex<bParallel>(Context, CounterHandler, Context.NumInstances);
		}
	}

	template<uint8 SrcOpType>
	static VM_FORCEINLINE void IndexExecOptimized(FVectorVMContext& Context)
	{
		if (Context.IsParallelExecution())
		{
			switch (SrcOpType)
			{
			case SRCOP_RRR: HandleRegisterValidIndices<true>(Context); break;
			case SRCOP_RRC:	HandleUniformValidIndices<FConstantHandler<int32>, true>(Context); break;
			default: check(0); break;
			}
		}
		else
		{
			switch (SrcOpType)
			{
			case SRCOP_RRR: HandleRegisterValidIndices<false>(Context); break;
			case SRCOP_RRC:	HandleUniformValidIndices<FConstantHandler<int32>, false>(Context); break;
			default: check(0); break;
			}
		}
	}

	void OptimizeAcquireIndex(FVectorVMCodeOptimizerContext& Context)
	{
		const uint32 SrcOpType = Context.BaseContext.DecodeSrcOperandTypes();

		AcquireIndexConstant = !!(SrcOpType & OP0_CONST);

		switch (SrcOpType)
		{
		case SRCOP_RRR: Context.Write<FVectorVMExecFunction>(IndexExecOptimized<SRCOP_RRR>); break;
		case SRCOP_RRC: Context.Write<FVectorVMExecFunction>(IndexExecOptimized<SRCOP_RRC>); break;
		default: check(0); break;
		}

		DataSetCounterIndex = Context.DecodeU16();
		ValidTestRegisterIndex = Context.DecodeU16();
		WorkingRegisterIndex = Context.DecodeU16();

		Context.Write(DataSetCounterIndex);
		Context.Write(ValidTestRegisterIndex);

		// we only need the working register if we've got non-uniform data
		if (SrcOpType == SRCOP_RRR)
		{
			Context.Write(WorkingRegisterIndex);
		}
	}

	bool OptimizeBatch(FVectorVMCodeOptimizerContext& Context)
	{
		const int32 BatchedOpCount = BatchedOps.Num();

		if (!BatchedOpCount)
			return false;

		for (const auto& BatchEntry : BatchedOps)
		{
			const uint16 AccumulatedOpCount = BatchEntry.Value.Num();

			if (!AccumulatedOpCount)
				continue;

			switch (BatchEntry.Key.SrcOpType)
			{
			case SRCOP_RRR:
				if (AcquireIndexConstant)
				{
					Context.Write<FVectorVMExecFunction>(DoRegisterKernelFixedValid<FCopyOp>);
				}
				else
				{
					Context.Write<FVectorVMExecFunction>(DoRegisterKernelVariableValid);
				}
				break;
			case SRCOP_RRC:	Context.Write<FVectorVMExecFunction>(DoRegisterKernelFixedValid<FSplatOp<FConstantHandler<int32>>>); break;
			default: check(0); break;
			}

			Context.Write(BatchEntry.Key.DataSetIndex);
			Context.Write(BatchEntry.Key.DestIndexRegisterIdx);
			Context.Write(AccumulatedOpCount);
			for (const FOpValue& OpValue : BatchEntry.Value)
			{
				Context.Write(OpValue.SourceRegisterIndex);
				Context.Write(OpValue.DestRegisterIdx);
			}
		}

		return true;
	}

	bool ExtractOp(FVectorVMCodeOptimizerContext& Context)
	{
		FOpKey Key;
		Key.SrcOpType = Context.BaseContext.DecodeSrcOperandTypes();
		Key.DataSetIndex = Context.DecodeU16();
		Key.DestIndexRegisterIdx = Context.DecodeU16();

		if (Key.DestIndexRegisterIdx != WorkingRegisterIndex)
		{
			// if we've found an output node that is not related to the acquire index op, then just exit
			return false;
		}

		FOpValue Value;
		Value.SourceRegisterIndex = Context.DecodeU16();
		Value.DestRegisterIdx = Context.DecodeU16();

		TArray<FOpValue>& ExistingOps = BatchedOps.FindOrAdd(Key);
		ExistingOps.Add(Value);

		return true;
	}

private:
	using RegisterType = VectorRegisterInt;
	using ScalarType = int32;

	uint16 DataSetCounterIndex = 0;
	uint16 ValidTestRegisterIndex = 0;
	uint16 WorkingRegisterIndex = 0;
	bool AcquireIndexConstant = false;

	struct FOpKey
	{
		uint16 DestIndexRegisterIdx;
		uint16 DataSetIndex;
		uint8 SrcOpType;
	};

	struct FOpValue
	{
		uint16 SourceRegisterIndex;
		uint16 DestRegisterIdx;
	};

	struct FOpKeyFuncs : public TDefaultMapKeyFuncs<FOpKey, TArray<FOpValue>, false>
	{
		static VM_FORCEINLINE bool Matches(const FOpKey& A, const FOpKey& B)
		{
			return A.DestIndexRegisterIdx == B.DestIndexRegisterIdx
				&& A.DataSetIndex == B.DataSetIndex
				&& A.SrcOpType == B.SrcOpType;
		}

		static VM_FORCEINLINE uint32 GetKeyHash(const FOpKey& Key)
		{
			return HashCombine(
				HashCombine(GetTypeHash(Key.DestIndexRegisterIdx), GetTypeHash(Key.DataSetIndex)),
				GetTypeHash(Key.SrcOpType));
		}
	};

	TMap<FOpKey, TArray<FOpValue>, FDefaultSetAllocator, FOpKeyFuncs> BatchedOps;
};

// look for the pattern of acquireindex followed by a bunch of outputs.
EVectorVMOp PackedOutputOptimization(EVectorVMOp Op, FVectorVMCodeOptimizerContext& Context)
{
	if (!GbBatchPackVMOutput)
	{
		return Op;
	}

	if (Op == EVectorVMOp::acquireindex)
	{
		const auto RollbackState = Context.CreateCodeState();

		FBatchedWriteIndexedOutput BatchedOutputOp;

		BatchedOutputOp.OptimizeAcquireIndex(Context);

		bool BatchValid = true;

		Op = Context.BaseContext.DecodeOp();

		while (BatchValid && Op == EVectorVMOp::outputdata_32bit)
		{
			BatchValid = BatchedOutputOp.ExtractOp(Context);
			Op = Context.BaseContext.DecodeOp();
		}

		// if there's nothing worth optimizing here, then just revert what we've parsed
		if (!BatchValid || !BatchedOutputOp.OptimizeBatch(Context))
		{
			Context.RollbackCodeState(RollbackState);
			return EVectorVMOp::acquireindex;
		}
	}

	return Op;
}

EVectorVMOp SafeMathOptimization(EVectorVMOp Op, FVectorVMCodeOptimizerContext& Context)
{
	if (!GbSafeOptimizedKernels)
	{
		return Op;
	}

	switch (Op)
	{
		case EVectorVMOp::div: FVectorKernelDivSafe::Optimize(Context); break;
		case EVectorVMOp::rcp: FVectorKernelRcpSafe::Optimize(Context); break;
		case EVectorVMOp::rsq: FVectorKernelRsqSafe::Optimize(Context); break;
		case EVectorVMOp::sqrt: FVectorKernelSqrtSafe::Optimize(Context); break;
		case EVectorVMOp::log: FVectorKernelLogSafe::Optimize(Context); break;
		case EVectorVMOp::pow: FVectorKernelPowSafe::Optimize(Context); break;
		default:
			return Op;
	}

	return Context.BaseContext.DecodeOp();
}

void VectorVM::OptimizeByteCode(const uint8* ByteCode, TArray<uint8>& OptimizedCode, TArrayView<uint8> ExternalFunctionRegisterCounts)
{
	OptimizedCode.Empty();

//-TODO: Support unaligned writes & little endian
#if PLATFORM_SUPPORTS_UNALIGNED_LOADS && PLATFORM_LITTLE_ENDIAN

	if ( !GbOptimizeVMByteCode || (ByteCode == nullptr) )
	{
		return;
	}

	FVectorVMCodeOptimizerContext Context(FVectorVMContext::Get(), ByteCode, OptimizedCode, ExternalFunctionRegisterCounts);

	// add any optimization filters in here, useful so what we can isolate optimizations with CVars
	FVectorVMCodeOptimizerContext::OptimizeVMFunction VMFilters[] =
	{
		PackedOutputOptimization,
		SafeMathOptimization,
	};

	EVectorVMOp Op = EVectorVMOp::done;
	do
	{
		Op = Context.BaseContext.DecodeOp();

		for (auto Filter : VMFilters)
			Op = Filter(Op, Context);

		switch (Op)
		{
			case EVectorVMOp::add: FVectorKernelAdd::Optimize(Context); break;
			case EVectorVMOp::sub: FVectorKernelSub::Optimize(Context); break;
			case EVectorVMOp::mul: FVectorKernelMul::Optimize(Context); break;
			case EVectorVMOp::div: FVectorKernelDiv::Optimize(Context); break;
			case EVectorVMOp::mad: FVectorKernelMad::Optimize(Context); break;
			case EVectorVMOp::lerp: FVectorKernelLerp::Optimize(Context); break;
			case EVectorVMOp::rcp: FVectorKernelRcp::Optimize(Context); break;
			case EVectorVMOp::rsq: FVectorKernelRsq::Optimize(Context); break;
			case EVectorVMOp::sqrt: FVectorKernelSqrt::Optimize(Context); break;
			case EVectorVMOp::neg: FVectorKernelNeg::Optimize(Context); break;
			case EVectorVMOp::abs: FVectorKernelAbs::Optimize(Context); break;
			case EVectorVMOp::exp: FVectorKernelExp::Optimize(Context); break;
			case EVectorVMOp::exp2: FVectorKernelExp2::Optimize(Context); break;
			case EVectorVMOp::log: FVectorKernelLog::Optimize(Context); break;
			case EVectorVMOp::log2: FVectorKernelLog2::Optimize(Context); break;
			case EVectorVMOp::sin: FVectorKernelSin::Optimize(Context); break;
			case EVectorVMOp::cos: FVectorKernelCos::Optimize(Context); break;
			case EVectorVMOp::tan: FVectorKernelTan::Optimize(Context); break;
			case EVectorVMOp::asin: FVectorKernelASin::Optimize(Context); break;
			case EVectorVMOp::acos: FVectorKernelACos::Optimize(Context); break;
			case EVectorVMOp::atan: FVectorKernelATan::Optimize(Context); break;
			case EVectorVMOp::atan2: FVectorKernelATan2::Optimize(Context); break;
			case EVectorVMOp::ceil: FVectorKernelCeil::Optimize(Context); break;
			case EVectorVMOp::floor: FVectorKernelFloor::Optimize(Context); break;
			case EVectorVMOp::round: FVectorKernelRound::Optimize(Context); break;
			case EVectorVMOp::fmod: FVectorKernelMod::Optimize(Context); break;
			case EVectorVMOp::frac: FVectorKernelFrac::Optimize(Context); break;
			case EVectorVMOp::trunc: FVectorKernelTrunc::Optimize(Context); break;
			case EVectorVMOp::clamp: FVectorKernelClamp::Optimize(Context); break;
			case EVectorVMOp::min: FVectorKernelMin::Optimize(Context); break;
			case EVectorVMOp::max: FVectorKernelMax::Optimize(Context); break;
			case EVectorVMOp::pow: FVectorKernelPow::Optimize(Context); break;
			case EVectorVMOp::sign: FVectorKernelSign::Optimize(Context); break;
			case EVectorVMOp::step: FVectorKernelStep::Optimize(Context); break;
			case EVectorVMOp::random: FVectorKernelRandom::Optimize(Context); break;
			case EVectorVMOp::noise: VectorVMNoise::Optimize_Noise1D(Context); break;
			case EVectorVMOp::noise2D: VectorVMNoise::Optimize_Noise2D(Context); break;
			case EVectorVMOp::noise3D: VectorVMNoise::Optimize_Noise3D(Context); break;

			case EVectorVMOp::cmplt: FVectorKernelCompareLT::Optimize(Context); break;
			case EVectorVMOp::cmple: FVectorKernelCompareLE::Optimize(Context); break;
			case EVectorVMOp::cmpgt: FVectorKernelCompareGT::Optimize(Context); break;
			case EVectorVMOp::cmpge: FVectorKernelCompareGE::Optimize(Context); break;
			case EVectorVMOp::cmpeq: FVectorKernelCompareEQ::Optimize(Context); break;
			case EVectorVMOp::cmpneq: FVectorKernelCompareNEQ::Optimize(Context); break;
			case EVectorVMOp::select: FVectorKernelSelect::Optimize(Context); break;

			case EVectorVMOp::addi: FVectorIntKernelAdd::Optimize(Context); break;
			case EVectorVMOp::subi: FVectorIntKernelSubtract::Optimize(Context); break;
			case EVectorVMOp::muli: FVectorIntKernelMultiply::Optimize(Context); break;
			case EVectorVMOp::divi: FVectorIntKernelDivide::Optimize(Context); break;
			case EVectorVMOp::clampi: FVectorIntKernelClamp::Optimize(Context); break;
			case EVectorVMOp::mini: FVectorIntKernelMin::Optimize(Context); break;
			case EVectorVMOp::maxi: FVectorIntKernelMax::Optimize(Context); break;
			case EVectorVMOp::absi: FVectorIntKernelAbs::Optimize(Context); break;
			case EVectorVMOp::negi: FVectorIntKernelNegate::Optimize(Context); break;
			case EVectorVMOp::signi: FVectorIntKernelSign::Optimize(Context); break;
			case EVectorVMOp::randomi: FScalarIntKernelRandom::Optimize(Context); break;
			case EVectorVMOp::cmplti: FVectorIntKernelCompareLT::Optimize(Context); break;
			case EVectorVMOp::cmplei: FVectorIntKernelCompareLE::Optimize(Context); break;
			case EVectorVMOp::cmpgti: FVectorIntKernelCompareGT::Optimize(Context); break;
			case EVectorVMOp::cmpgei: FVectorIntKernelCompareGE::Optimize(Context); break;
			case EVectorVMOp::cmpeqi: FVectorIntKernelCompareEQ::Optimize(Context); break;
			case EVectorVMOp::cmpneqi: FVectorIntKernelCompareNEQ::Optimize(Context); break;
			case EVectorVMOp::bit_and: FVectorIntKernelBitAnd::Optimize(Context); break;
			case EVectorVMOp::bit_or: FVectorIntKernelBitOr::Optimize(Context); break;
			case EVectorVMOp::bit_xor: FVectorIntKernelBitXor::Optimize(Context); break;
			case EVectorVMOp::bit_not: FVectorIntKernelBitNot::Optimize(Context); break;
			case EVectorVMOp::bit_lshift: FVectorIntKernelBitLShift::Optimize(Context); break;
			case EVectorVMOp::bit_rshift: FVectorIntKernelBitRShift::Optimize(Context); break;
			case EVectorVMOp::logic_and: FVectorIntKernelLogicAnd::Optimize(Context); break;
			case EVectorVMOp::logic_or: FVectorIntKernelLogicOr::Optimize(Context); break;
			case EVectorVMOp::logic_xor: FVectorIntKernelLogicXor::Optimize(Context); break;
			case EVectorVMOp::logic_not: FVectorIntKernelLogicNot::Optimize(Context); break;
			case EVectorVMOp::f2i: FVectorKernelFloatToInt::Optimize(Context); break;
			case EVectorVMOp::i2f: FVectorKernelIntToFloat::Optimize(Context); break;
			case EVectorVMOp::f2b: FVectorKernelFloatToBool::Optimize(Context); break;
			case EVectorVMOp::b2f: FVectorKernelBoolToFloat::Optimize(Context); break;
			case EVectorVMOp::i2b: FVectorKernelIntToBool::Optimize(Context); break;
			case EVectorVMOp::b2i: FVectorKernelBoolToInt::Optimize(Context); break;

			case EVectorVMOp::outputdata_32bit:	FScalarKernelWriteOutputIndexed<int32>::Optimize(Context);	break;
			case EVectorVMOp::inputdata_32bit: FVectorKernelReadInput<int32>::Optimize(Context); break;
			case EVectorVMOp::inputdata_noadvance_32bit: FVectorKernelReadInputNoAdvance<int32>::Optimize(Context); break;
			case EVectorVMOp::acquireindex: FScalarKernelAcquireCounterIndex::Optimize(Context); break;
			case EVectorVMOp::external_func_call: FKernelExternalFunctionCall::Optimize(Context); break;

			case EVectorVMOp::exec_index: FVectorKernelExecutionIndex::Optimize(Context); break;

			case EVectorVMOp::enter_stat_scope: FVectorKernelEnterStatScope::Optimize(Context); break;
			case EVectorVMOp::exit_stat_scope: FVectorKernelExitStatScope::Optimize(Context); break;

			//Special case ops to handle unique IDs but this can be written as generalized buffer operations. TODO!
			case EVectorVMOp::update_id:	FScalarKernelUpdateID::Optimize(Context); break;
			case EVectorVMOp::acquire_id:	FScalarKernelAcquireID::Optimize(Context); break;

			// Execution always terminates with a "done" opcode.
			case EVectorVMOp::done:
				break;

				// Opcode not recognized / implemented.
			default:
				UE_LOG(LogVectorVM, Fatal, TEXT("Unknown op code 0x%02x"), (uint32)Op);
				OptimizedCode.Empty();
				return;//BAIL
		}
	} while (Op != EVectorVMOp::done);
	Context.Write<FVectorVMExecFunction>(nullptr);
#endif //PLATFORM_SUPPORTS_UNALIGNED_LOADS && PLATFORM_LITTLE_ENDIAN
}

#undef VM_FORCEINLINE
