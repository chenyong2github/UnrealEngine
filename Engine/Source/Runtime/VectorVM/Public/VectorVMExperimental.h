// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

//only to be included by VectorVM.h

typedef void * (VectorVMReallocFn)              (void *Ptr, size_t NumBytes, const char *Filename, int LineNumber);
typedef void   (VectorVMFreeFn)                 (void *Ptr, const char *Filename, int LineNumber);
struct FDataSetMeta;

//prototypes for serialization that are required whether or not serialization is enabled
#if VECTORVM_SUPPORTS_EXPERIMENTAL || defined(VVM_INCLUDE_SERIALIZATION)
struct FVectorVMConstData
{
	void * RegisterData;
	uint32 NumDWords;
	uint32 StartingOffset;
};

typedef uint32 (VectorVMOptimizeErrorCallback)  (struct FVectorVMOptimizeContext *OptimizeContext, uint32 ErrorFlags);	//return new error flags
typedef uint32 (VectorVMSerializeErrorCallback) (struct FVectorVMSerializeState *SerializeState, uint32 ErrorFlags);

#endif // VECTORVM_SUPPORTS_EXPERIMENTAL || VVM_INCLUDE_SERIALIZATION

//Serialization
enum EVectorVMSerializeFlags
{
	VVMSer_SyncRandom        = 1 << 0,
	VVMSer_SyncExtFns        = 1 << 1,
	VVMSer_OptimizedBytecode = 1 << 2,
};

#ifdef VVM_INCLUDE_SERIALIZATION

enum EVectorVMSerializeError
{
	VVMSerErr_OutOfMemory    = 1 << 0,
	VVMSerErr_Init           = 1 << 1,
	VVMSerErr_InputDataSets  = 1 << 2,
	VVMSerErr_OutputDataSets = 1 << 3,
	VVMSerErr_Instruction    = 1 << 4,
	VVMSerErr_ConstData      = 1 << 5,

	VVMSerErr_Fatal          = 1 << 31
};

struct FVectorVMSerializeInstruction
{
	uint32   OpStart;
	uint32   NumOps;
	uint64   Dt;
	uint64   DtDecode;
	uint32 * TempRegisters;
	uint8 *  TempRegisterFlags;
};

struct FVectorVMSerializeExternalData
{
	wchar_t * Name;
	uint16    NameLen;
	uint16    NumInputs;
	uint16    NumOutputs;
};

struct FVectorVMSerializeDataSet
{
	uint32 * InputBuffers;
	uint32 * OutputBuffers;

	uint32  InputOffset[4];	    //float, int, half (half must be 0)
	uint32  OutputOffset[4];    //float, int, half (half must be 0)

	int32   InputInstanceOffset;
	int32   InputDataSetAccessIndex;
	int32   InputIDAcquireTag;

	int32   OutputInstanceOffset;
	int32   OutputDataSetAccessIndex;
	int32   OutputIDAcquireTag;

	int32 * InputIDTable;
	int32 * InputFreeIDTable;
	int32 * InputSpawnedIDTable;

	int32   InputIDTableNum;
	int32   InputFreeIDTableNum;
	int32   InputSpawnedIDTableNum;

	int32   InputNumFreeIDs;
	int32   InputMaxUsedIDs;
	int32   InputNumSpawnedIDs;

	int32 * OutputIDTable;
	int32 * OutputFreeIDTable;
	int32 * OutputSpawnedIDTable;

	int32   OutputIDTableNum;
	int32   OutputFreeIDTableNum;
	int32   OutputSpawnedIDTableNum;

	int32   OutputNumFreeIDs;
	int32   OutputMaxUsedIDs;
	int32   OutputNumSpawnedIDs;
};

struct FVectorVMSerializeChunk
{
	uint32 ChunkIdx;
	uint32 BatchIdx;
	uint32 ExecIdx;
	uint32 StartInstance;
	uint32 NumInstances;

	uint32 StartThreadID;
	uint32 EndThreadID;

	uint64 StartClock;
	uint64 EndClock;
	uint64 InsExecTime;
};

struct FVectorVMSerializeState
{
	uint32                              NumInstances;
	uint32                              NumTempRegisters;
	uint32                              NumTempRegFlags;  //max of NumTempRegisters and Num Input Registers in each dataset
	uint32                              NumConstBuffers;

	uint32                              Flags;

	FVectorVMSerializeInstruction *     Instructions;
	uint32                              NumInstructions;
	uint32                              NumInstructionsAllocated;

	uint32                              NumExternalData;
	FVectorVMSerializeExternalData *    ExternalData;
	uint32								MaxExtFnRegisters;
	uint32								MaxExtFnUsed;

	uint64                              ExecDt;
	uint64                              SerializeDt;

	uint8 *                             TempRegFlags;
	uint8 *                             Bytecode;
	uint32                              NumBytecodeBytes;

	FVectorVMSerializeDataSet *         DataSets;
	uint32                              NumDataSets;
	uint32 *                            PreExecConstData;
	uint32 *                            PostExecConstData;

	uint32                              NumChunks;
	FVectorVMSerializeChunk *           Chunks;

	const struct FVectorVMOptimizeContext *   OptimizeCtx;

	volatile int64                      ChunkComplete; //1 bit for each of the first 64 chunks

	VectorVMReallocFn *                  ReallocFn;
	VectorVMFreeFn *                     FreeFn;

	struct {
		uint32                           Flags;
		uint32                           Line;
		VectorVMSerializeErrorCallback * CallbackFn;
	} Error;
};
#else // VVM_INCLUDE_SERIALIZATION

struct FVectorVMSerializeState
{
	uint32              Flags;
	VectorVMReallocFn * ReallocFn;
	VectorVMFreeFn *    FreeFn;

};

#endif // VVM_INCLUDE_SERIALIZATION

#if VECTORVM_SUPPORTS_EXPERIMENTAL

union FVecReg {
	VectorRegister4f v;
	VectorRegister4i i;
};

struct FVectorVMExtFunctionData
{
	const FVMExternalFunction *Function;
	int32                      NumInputs;
	int32                      NumOutputs;
};

//Optimization
enum EVectorVMOptimizeFlags
{
	VVMOptFlag_SaveIntermediateState = 1 << 0,
	VVMOptFlag_OmitStats             = 1 << 1
};

struct FVectorVMOptimizeInstruction
{
	EVectorVMOp         OpCode;
	EVectorVMOpCategory OpCat;
	uint32              PtrOffsetInOrigBytecode;
	uint32              PtrOffsetInOptimizedBytecode;
	int                 Index; //initial index.  Instructions are moved around and removed and dependency chains are created based on index, so we need to store this.
	union
	{
		struct
		{
			uint16 DstRegPtrOffset;
			uint16 DataSetIdx;
			uint16 InputIdx;
			uint16 FuseCount;
			int    FirstInsInsertIdx;
		} Input;
		struct
		{
			uint16 RegPtrOffset;
			uint16 DataSetIdx;
			uint16 DstRegIdx;
			int    CopyFromInputInsIdx;
		} Output;
		struct
		{
			uint32 RegPtrOffset;
			uint16 NumInputs;
			uint16 NumOutputs;
			uint8  InputFuseBits;
		} Op;
		struct
		{
			uint32 RegPtrOffset;
			uint16 DataSetIdx;
		} IndexGen;
		struct
		{
			uint32 RegPtrOffset;
			uint16 ExtFnIdx;
			uint16 NumInputs;
			uint16 NumOutputs;
		} ExtFnCall;
		struct
		{
			uint32 RegPtrOffset;
		} ExecIndex;
		struct
		{
			uint32 RegPtrOffset;
			uint16 DataSetIdx;
		} RWBuffer;
		struct
		{
			uint16 ID;
		} Stat;
		struct
		{
		} Other;
	};
};

enum EVectorVMOptimizeError
{
	VVMOptErr_OutOfMemory           = 1 << 0,
	VVMOptErr_Overflow			    = 1 << 1,
	VVMOptErr_Bytecode              = 1 << 2,
	VVMOptErr_RegisterUsage         = 1 << 3,
	VVMOptErr_ConstRemap            = 1 << 4,
	VVMOptErr_Instructions          = 1 << 5,
	VVMOptErr_InputFuseBuffer       = 1 << 6,
	VVMOptErr_InstructionReOrder    = 1 << 7,
	VVMOptErr_SSARemap              = 1 << 8,
	VVMOptErr_OptimizedBytecode     = 1 << 9,
	VVMOptErr_ExternalFunction      = 1 << 10,
	VVMOptErr_RedundantInstruction  = 1 << 11,

	VVMOptErr_Fatal                 = 1 << 31
};

struct FVectorVMOptimizeContext
{
	uint8 *                               OutputBytecode;
	uint16 *                              ConstRemap[2];
	FVectorVMExtFunctionData *            ExtFnTable;
	uint32                                NumBytecodeBytes;
	uint32                                NumOutputDataSets;
	uint16                                NumConstsAlloced;    //upper bound to alloc
	uint16                                NumConstsRemapped;
	uint32                                NumTempRegisters;
	uint32                                NumExtFns;
	uint32                                MaxExtFnRegisters;
	uint32                                NumDummyRegsReq;     //External function "null" registers
	int32                                 MaxExtFnUsed;

	struct
	{
		VectorVMReallocFn *               ReallocFn;
		VectorVMFreeFn *                  FreeFn;
	} Init;                                                    //Set this stuff when calling Optimize()

	struct
	{
		uint32                            Flags;               //zero is good
		uint32                            Line;
		VectorVMOptimizeErrorCallback *   CallbackFn;          //set this to get a callback whenever there's an error
	} Error;

	struct
	{
		FVectorVMOptimizeInstruction *    Instructions;
		uint16 *                          RegisterUsageBuffer;
		uint16 *                          SSARegisterUsageBuffer;
		int32 *                           InputRegisterFuseBuffer;
		uint32                            NumBytecodeBytes;
		uint32                            NumInstructions;
		uint32                            NumRegistersUsed;
	} Intermediate;                                           //these are freed and NULL after optimize() unless SaveIntermediateData is true when calling OptimizeVectorVMScript
};

//VectorVMState
enum EVectorVMStateError
{
	VVMErr_InitOutOfMemory     = 1 << 0,
	VVMErr_InitMemMismatch     = 1 << 1,
	VVMErr_BatchMemory         = 1 << 2,
	VVMErr_AssignInstances     = 1 << 3,

	VVMErr_Fatal               = 1 << 31
};

struct FVectorVMExternalFnPerInstanceData
{
	class UNiagaraDataInterface *     DataInterface;
	void *                            UserData;
	uint16                            NumInputs;
	uint16                            NumOutputs;
};

struct FVectorVMInitData
{
	struct FVectorVMState *					ExistingVectorVMState;
	const FVectorVMOptimizeContext *        OptimizeContext;
	TArrayView<FDataSetMeta>                DataSets;
	TArrayView<const FVMExternalFunction *> ExtFunctionTable;
	
	int                                     NumInstances;

	int                                     NumConstData;
	FVectorVMConstData *                    ConstData;

	void **                                 UserPtrTable;
	int                                     NumUserPtrTable;

	VectorVMReallocFn *                     ReallocFn;
	VectorVMFreeFn *                        FreeFn;
};

struct FVectorVMBatchState
{
	MS_ALIGN(16) FVecReg *    RegisterData GCC_ALIGN(16);
	struct {
		uint32 *              StartingOutputIdxPerDataSet;
		uint32 *              NumOutputPerDataSet;
		struct {
			FVecReg **        RegData;
			uint32 *          RegInc;
			FVecReg *         DummyRegs;
		} ExtFnDecodedReg;
		int32 *               RandCounters; //used for external functions only.
	} ChunkLocalData;
	void *                    MallocedMemPtr; //needed for alignment purposes

	volatile int32			  CurrentlyExecuting;

	int                       StartInstance;
	int                       NumInstances;

	VectorRegister4i          RandState[5]; //xorwor state for random/randomi instructions.  DIs use RandomStream.
	VectorRegister4i          RandCounters;

	FRandomStream             RandStream;
	int                       UseCount;
};

static_assert((sizeof(FVectorVMBatchState) & 0xF) == 0, "FVectorVMBatchState must be 16 byte aligned");

struct FVectorVMState
{
	uint8 *                     Bytecode;
	FVectorVMExtFunctionData *  ExtFunctionTable;
	void **                     UserPtrTable;
	volatile int32 *            NumOutputPerDataSet;
	FVectorVMBatchState *       BatchStates;
	size_t                      NumBytesMalloced;
	
	TArrayView<FDataSetMeta>    DataSets;

	uint32                      NumTempRegisters;
	uint32                      NumConstBuffers;
	uint32                      NumBytecodeBytes;
	uint32                      NumOutputDataSets;   //computed in the optimizer
	uint32                      NumExtFunctions;
	uint32                      MaxExtFnRegisters;
	uint32                      NumDummyRegsReq;
	uint32                      NumUserPtrTable;
	int32                       TotalNumInstances;
	volatile int32              NumInstancesAssignedToBatches;
	volatile int32              NumInstancesCompleted;

	size_t                      NumBytesRequiredPerBatch;
	size_t                      PerBatchRegisterDataBytesRequired;
	size_t                      PerBatchChunkLocalDataOutputIdxBytesRequired;
	size_t                      PerBatchChunkLocalNumOutputBytesRequired;
	size_t                      PerBatchChunkLocalNumExtFnDecodeRegisterBytesRequired;

	int                         NumBatches;
	int                         MaxInstancesPerChunk;
	int                         MaxChunksPerBatch;

	VectorVMReallocFn *         ReallocFn;
	VectorVMFreeFn *            FreeFn;

	struct
	{
		int                     LineNum;
		uint32                  Flags;
	} Error;
};

class FVectorVMExternalFunctionContextExperimental
{
public:
	MS_ALIGN(16) uint32** RegisterData GCC_ALIGN(16);
	uint16* RawVecIndices; //undecoded, for compatbility with the previous VM
	uint32* RegInc;

	int                      RegReadCount;
	int                      NumRegisters;

	int                      StartInstance;
	int                      NumInstances;
	int                      NumLoops;
	int                      PerInstanceFnInstanceIdx;

	void** UserPtrTable;
	int                      NumUserPtrs;

	FRandomStream* RandStream;
	int32* RandCounters;
	TArrayView<FDataSetMeta> DataSets;

	FORCEINLINE int32                                  GetStartInstance() const { return StartInstance; }
	FORCEINLINE int32                                  GetNumInstances() const { return NumInstances; }
	FORCEINLINE int32* GetRandCounters() { return RandCounters; }
	FORCEINLINE FRandomStream& GetRandStream() { return *RandStream; }
	FORCEINLINE void* GetUserPtrTable(int32 UserPtrIdx) { check(UserPtrIdx < NumUserPtrs);  return UserPtrTable[UserPtrIdx]; }
	template<uint32 InstancesPerOp> FORCEINLINE int32  GetNumLoops() const { static_assert(InstancesPerOp == 4); return NumLoops; };

	FORCEINLINE float* GetNextRegister(int32* OutAdvanceOffset, int32* OutVecIndex)
	{
		check(RegReadCount < NumRegisters);
		*OutAdvanceOffset = RegInc[RegReadCount] & 1;
		*OutVecIndex = RawVecIndices[RegReadCount];
		return (float*)RegisterData[RegReadCount++];
	}
};

//API FUNCTIONS

//normal functions
VECTORVM_API FVectorVMState * InitVectorVMState     (FVectorVMInitData *InitData, FVectorVMExternalFnPerInstanceData **OutPerInstanceExtData, FVectorVMSerializeState *SerializeState);
VECTORVM_API void             FreeVectorVMState     (FVectorVMState *VectorVMState);
VECTORVM_API void             ExecVectorVMState     (FVectorVMState *VectorVMState, FVectorVMSerializeState *SerializeState, FVectorVMSerializeState *CmpSerializeState);
VECTORVM_API int              GetNumOutputInstances (FVectorVMState *VectorVMState, int DataSetIdx);

//optimize functions
VECTORVM_API uint32  OptimizeVectorVMScript                (const uint8 *Bytecode, int BytecodeLen, FVectorVMExtFunctionData *ExtFnIOData, int NumExtFns, FVectorVMOptimizeContext *OptContext, uint32 Flags); //OutContext must be zeroed except the Init struct
VECTORVM_API void    FreeVectorVMOptimizeContext           (FVectorVMOptimizeContext *Context);
VECTORVM_API void    FreezeVectorVMOptimizeContext         (const FVectorVMOptimizeContext& Context, TArray<uint8>& ContextData);
VECTORVM_API void    ReinterpretVectorVMOptimizeContextData(TConstArrayView<uint8> ContextData, FVectorVMOptimizeContext& Context);

//serialize functions
VECTORVM_API uint32  SerializeVectorVMInputDataSets  (FVectorVMSerializeState *SerializeState, TArrayView<FDataSetMeta> DataSets, FVectorVMConstData *ConstData, int NumConstData); //only use when not calling InitVectorVMState()
VECTORVM_API uint32  SerializeVectorVMOutputDataSets (FVectorVMSerializeState *SerializeState, TArrayView<FDataSetMeta> DataSets, FVectorVMConstData *ConstData, int NumConstData);
VECTORVM_API void    SerializeVectorVMWriteToFile    (FVectorVMSerializeState *SerializeState, uint8 WhichStateWritten, const wchar_t *Filename);
VECTORVM_API void    FreeVectorVMSerializeState      (FVectorVMSerializeState *SerializeState);

#else // VECTORVM_SUPPORTS_EXPERIMENTAL

struct FVectorVMState {

};

VECTORVM_API uint32 SerializeVectorVMInputDataSets  (FVectorVMSerializeState *SerializeState, TArrayView<FDataSetMeta> DataSets, struct FVectorVMConstData *ConstData, int NumConstData); //only use when not calling InitVectorVMState()
VECTORVM_API uint32 SerializeVectorVMOutputDataSets (FVectorVMSerializeState *SerializeState, TArrayView<FDataSetMeta> DataSets, struct FVectorVMConstData *ConstData, int NumConstData);
VECTORVM_API void   SerializeVectorVMWriteToFile    (FVectorVMSerializeState *SerializeState, uint8 WhichStateWritten, const wchar_t *Filename);
VECTORVM_API void   FreeVectorVMSerializeState      (FVectorVMSerializeState *SerializeState);

#endif // VECTORVM_SUPPORTS_EXPERIMENTAL
