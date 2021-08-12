// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralEnumClasses.h"
#include <algorithm>
#include "NeuralTensor.generated.h"

class FRDGBuilder;
using FRDGBufferSRVRef = class FRDGBufferSRV*;
using FRDGBufferUAVRef = class FRDGBufferUAV*;
template<typename T> class TRefCountPtr;
enum class EBufferUsageFlags : uint32;
struct FTensorProto;


/**
 * Although conceptually this could apply to both the CPU and GPU versions, in practice only the GPU performance is affected by this setting.
 * Input and Intermediate(Not)Initialized currently share the same attributes because input might become intermediate (e.g., if input tensor fed into a ReLU, which simply modifies
 * the input FNeuralTensor). However, Intermediate(Not)Initialized and Output do not copy the memory from CPU to GPU but rather simply allocates it.
 * Output might also become Intermediate(Not)Initialized (e.g., if Output -> ReLU -> Output), so it is kept as ReadWrite rather than written once to account for this.
 */
UENUM()
enum class ENeuralTensorTypeGPU : uint8
{
	Generic,					/** Generic tensor that works in every situation (ReadWrite), although it might not be the most efficient one. */
	Input,						/** Input tensor of the UNeuralNetworkLegacy. Copied from CPU and ReadWrite (but usually ReadOnly). */
	IntermediateNotInitialized,	/** Intermediate tensor of the UNeuralNetworkLegacy (output of at least a layer and input of at least some other layer). Not copied from CPU, ReadWrite, and transient. */
	IntermediateInitialized,	/** Intermediate tensor that is initialized with CPU data (e.g., XWithZeros in FConvTranpose). Copied from CPU. */
	Output,						/** Output tensor of the UNeuralNetworkLegacy. Not copied from CPU and ReadWrite. */
	Weight						/** Weights of a particular operator/layer. Copied from CPU, ReadOnly, and initialized from CPU memory. */
};



/**
 * This is an auxiliary class. See UNeuralNetworkLegacy for a high-level wrapper of the whole NeuralNetworkInference plugin. The UNeuralNetworkLegacy header
 * documentation also includes some code examples.
 *
 * FNeuralTensor represents a tensor of the UNeuralNetworkLegacy model.
 * Its CPU operations are very similar to those of TArray<T>. Most of its operations run on CPU, so `ToGPU_RenderThread()` must be called before running on
 * GPU and after running any FNeuralTensor function that modified the CPU memory.
 */
USTRUCT()
struct NEURALNETWORKINFERENCE_API FNeuralTensor
{
	GENERATED_BODY()

public:
	/**
	 * It allocates the desired memory.
	 * @param InVolume Set to 0 if memory allocation is not required or the final size is unknown. Values smaller than 0 will be clipped to 0.
	 * @param InSizes Set to empty (or omit argument) if memory allocation is not required or the final size is unknown.
	 * @param InName Used for GPU debugging and the ToString() function.
	*/
	explicit FNeuralTensor(const ENeuralDataType InDataType, const int64 InVolume, const FString& InName = TEXT("FNeuralTensor"), const ENeuralTensorTypeGPU InTensorTypeGPU = ENeuralTensorTypeGPU::Generic);
	explicit FNeuralTensor(const ENeuralDataType InDataType = ENeuralDataType::None, const TArray<int64>& InSizes = TArray<int64>(), const FString& InName = TEXT("FNeuralTensor"), const ENeuralTensorTypeGPU InTensorTypeGPU = ENeuralTensorTypeGPU::Generic);

	/**
	 * Performance-wise, this constructor makes a deep copy of the data (not optimal). For maximum speed, use the other constructors + GetData()/GetDataCasted().
	 * It allocates the desired memory, and fills it with the input data from InArray. Equivalent to FNeuralTensor(InVolumeOrSizes) + SetFromArrayCopy(...)
	 *
	 * @param InArray The input data to copy from.
	 * @param InSizes If empty (default), a 1-D tensor of Volume = InArray.Num() will be assumed. If not empty, used to fill the Sizes of this FNeuralTensor.
	 * @param InName Used for GPU debugging and the ToString() function.
	 */
	template <typename T>
	explicit FNeuralTensor(const TArray<T>& InArray, const TArray<int64>& InSizes = TArray<int64>(), const FString& InName = TEXT("FNeuralTensor"), const ENeuralTensorTypeGPU InTensorTypeGPU = ENeuralTensorTypeGPU::Generic);

	/**
	 * Copy constructor.
	 * It performs a deep (slow but safe) copy of the current FNeuralTensor. The resulting FNeuralTensor will not share any parameters with the current one.
	 */
	FNeuralTensor(const FNeuralTensor& InTensor);

	/**
	 * Copy assignment. Analog to FNeuralTensor(const FNeuralTensor& InTensor).
	 */
	FNeuralTensor& operator=(const FNeuralTensor& InTensor);

	/**
	 * Move constructor. It destroys the original FNeuralTensor (InTensor) to be moved for a fast Move construction.
	 */
	FNeuralTensor(FNeuralTensor&& InTensor);

	/**
	 * Move assignment. Analog to FNeuralTensor(FNeuralTensor&& InTensor).
	 */
	FNeuralTensor& operator=(FNeuralTensor&& InTensor);

	/**
	 * Comparison operator (equal). Returns true if the dimensions, sizes, scalar type, and data match with each other.
	 * It does not consider other properties of the FNeuralTensor (such as ENeuralTensorTypeGPU).
	 */
	bool operator==(const FNeuralTensor& InTensorToCopy) const;

	/**
	 * Comparison operator (not equal). Returns the opposite than the equal (==) operator.
	 * It does not consider other properties of the FNeuralTensor (such as ENeuralTensorTypeGPU).
	 */
	FORCEINLINE bool operator!=(const FNeuralTensor& InTensorToCopy) const;

	/**
	 * Returns the reference to an element at the given index.
	 * @returns Reference to the indexed element.
	 */
	template <typename T, typename TInput>
	T& At(const TInput InIndex);

	/**
	 * Const version of the non-const function At().
	 * @returns Reference to the indexed element.
	 */
	template <typename T, typename TInput>
	const T& At(const TInput InIndex) const;

	/**
	 * 2 different functions to get the output tensor results:
	 * - GetArrayCopy<T> (slower but safer): It returns a copy of the results as a TArray<T>. T has to be the same size than sizeof(DataType).
	 * - GetUnderlyingUInt8ArrayRef (faster): It returns a reference to the TArray<uint8> that contains the results. The returned reference will only be valid as long as
	 *   this class instance does not go out of scope and its internal TArray<uint8> is not re-initialized.
	 * @see GetArrayCopy(), GetUnderlyingUInt8ArrayRef()
	 */
	template <typename T>
	TArray<T> GetArrayCopy() const;
	FORCEINLINE const TArray<uint8>& GetUnderlyingUInt8ArrayRef() const;

	/**
	 * Dangerous: If FNeuralTensor goes out of scope, this returned pointer will no longer be valid.
	 * Analog to TArray::GetData().
	 * @returns Void pointer to the FNeuralTensor data.
	 */
	FORCEINLINE void* GetData();

	/**
	 * Const analog to TArray::GetData().
	 * @returns Const void pointer to the FNeuralTensor data.
	 */
	FORCEINLINE const void* const GetData() const;

	template<typename T>
	FORCEINLINE T* GetDataCasted();

	template<typename T>
	FORCEINLINE const T* const GetDataCasted() const;

	FORCEINLINE FString GetName() const;

	FORCEINLINE ENeuralDataType GetDataType() const;

	/**
	 * It returns the size of the current dimension, or 1 if InDimension > GetNumberDimensions().
	 */
	int64 GetSize(const int32 InDimension) const;

	FORCEINLINE const TArray<int64>& GetSizes() const;

	FORCEINLINE int32 GetNumberDimensions() const;

	/**
	 * NumInBytes() = Num() * sizeof(type used)
	 */
	FORCEINLINE int64 NumInBytes() const;

	/**
	 * Num() is analog to TArray<T>::Num().
	 */
	FORCEINLINE int64 Num() const;

	/**
	 * IsEmpty() is analog to TArray<T>::IsEmpty().
	 */
	FORCEINLINE bool IsEmpty() const;

	/**
	 * It returns the ENeuralTensorTypeGPU (Generic, Input, Intermediate(Not)Initialized, Output, Weight, etc.).
	 */
	FORCEINLINE ENeuralTensorTypeGPU GetTensorTypeGPU() const;

	/**
	 * It sets the ENeuralTensorTypeGPU (Generic, Input, Intermediate(Not)Initialized, Output, Weight, etc.).
	 * If the GPU memory was already initialized, it will also UE_LOG a warning.
	 */
	void SetTensorTypeGPU(const ENeuralTensorTypeGPU InTensorTypeGPU);

	FORCEINLINE void SetEnableGPU(const bool bInEnableGPU);

	/**
	 * It uploads/downloads the memory from/to the CPU to/from the GPU based on TensorTypeGPU (which sets a preset of EBufferUsageFlags flags).
	 * @param InEBufferUsageFlags gives the user total control over the buffer flags (and ignores the TensorTypeGPU flag). This is meant to be filled with a combination of EBufferUsageFlags values.
	 */
	void ToGPU_RenderThread(FRDGBuilder* InOutGraphBuilder);
	void ToGPU_RenderThread(FRDGBuilder* InOutGraphBuilder, const EBufferUsageFlags InEBufferUsageFlags, const bool bInShouldCopyFromCPU);
	void UpdateSRVAndOrUAV_RenderThread(FRDGBuilder* InOutGraphBuilder);
	void ToCPU_RenderThread(FRDGBuilder* InOutGraphBuilder);

	TRefCountPtr<class FRDGPooledBuffer>& GetPooledBuffer() const;
	const FRDGBufferSRVRef GetBufferSRVRef() const;
	FRDGBufferUAVRef GetBufferUAVRef();

	/**
	 * Resize the FNeuralTensor to the desired new size.
	 * Analog to TArray<T>::SetNumUninitialized().
	 * @param InTensor Set Sizes and DataType from the input InTensor.
	 * @param InVolume Set InVolume to 0 if memory allocation is not required or the final size is unknown, SetNumUninitialized(InVolume) == SetNumUninitialized({InVolume}).
	 * @param InSizes Set InSizes to empty if memory allocation is not required or the final size is unknown.
	 * @param InDataType set to None means that it will maintain the previous type.
	 */
	void SetNumUninitialized(const FNeuralTensor& InTensor, const bool bInAllowShrinking = true);
	FORCEINLINE void SetNumUninitialized(const int64 InVolume, const ENeuralDataType InDataType, const bool bInAllowShrinking = true);
	void SetNumUninitialized(const TArray<int64>& InSizes, const ENeuralDataType InDataType, const bool bInAllowShrinking = true);

	/**
	 * This will replace the TArray with the input one, by deeply copying the array (safer and easier to use).
	 * For maximum performance:
	 * - If you are given an input TArray (or a FNeuralTensor with different Sizes) that cannot be moved, use SetFromArrayCopy().
	 * - If you are initializing the FNeuralTensor iteratively (and to avoid a copy), fill the tensor with GetDataCasted().
	 * Modifying the array InArray after calling this function will not modify the internal FNeuralTensor array.
	 * The size of both (InArray and current FNeuralTensor) must match, i.e., Num() must match InArray.Num().
	 */
	void SetFromUnderlyingUInt8ArrayCopy(const TArray<uint8>& InArray);
	template<typename T>
	void SetFromArrayCopy(const TArray<T>& InArray);

	/**
	 * It will fill the current neural tensor with the input FTensorProto.
	 * @returns Whether the conversion was successful.
	 */
	bool SetFromTensorProto(const FTensorProto* const InTensorProto, const FString& InTensorName, const ENeuralTensorTypeGPU InTensorTypeGPU);

	/**
	 * It sets all the elements of the FNeuralTensor to InValue.
	 * It use a double typename to avoid the mistake of SetTo(0) for a double or float (because that 0 would be an int otherwise).
	 */
	template<typename T, typename TInput>
	void SetTo(const TInput InValue);

	/**
	 * It flips the InDimension dimension of the tensor.
	 * @return Whether the flip was successful.
	 */
	bool Flip(const int32 InDimension);

	/**
	 * It flips all the dimensions of the tensor in the range [InDimensionFirst, InDimensionLast). Needed for efficient convolution.
	 * @return Whether the flip was successful.
	 */
	bool Flip(const int32 InDimensionFirst, const int32 InDimensionLast);

	/**
	 * It transposes the matrix (if the tensor has up to 2 dimensions).
	 * @return Whether the transpose was successful.
	 */
	bool Transpose();

	/**
	 * If Num() is constant, it reshapes the current tensor. I.e., it just updates Sizes with InSizes.
	 * Reshape() copies and ReshapeMove() moves InSizes.
	 */
	bool Reshape(const TArray<int64>& InSizes);
	bool ReshapeMove(TArray<int64>& InSizes);

	/**
	 * It returns a FString with up to InMaxNumberElementsToDisplay elements displayed. If InMaxNumberElementsToDisplay <= 0, it displays them all.
	 * @param bInReturnOnlyData If false (default), it will print all information. E.g., "FNeuralTensor: Int64, Generic, volume=3, sizes={3}, data=[1 2 3]".
	 * If false, it will simply print the data. I.e., "[1 2 3]" for the previous example.
	 */
	FString ToString(const int64 InMaxNumberElementsToDisplay = -1, const bool bInReturnOnlyData = false) const;

protected:
	/** Used for GPU debugging and the ToString() function. */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString Name;
	/** General variables and properties */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralDataType DataType;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	int64 Volume;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<int64> Sizes;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralTensorTypeGPU TensorTypeGPU;

private:
	/** CPU-based variables */
	UPROPERTY()
	TArray<uint8> ArrayCPU;
	/**
	 * GPU-based variables that are transient (do not need to be serialized) and initialized on the fly (with ToGPU/ToCPU).
	 * - bEnableGPU: By default false, meaning all GPU memory will be disabled and those functions will not do anything. Enable to allow using the GPU functions and variables of the tensor.
	 * - ArrayCPUForGPUAs32Data: If ArrayCPU is meant for 64-byte data (i.e., int64, uint64, double).
	 */
	bool bEnableGPU;
	TSharedPtr<TRefCountPtr<class FRDGPooledBuffer>> PooledBuffer;
	TSharedPtr<FRDGBufferSRVRef> BufferSRVRef;
	TSharedPtr<FRDGBufferUAVRef> BufferUAVRef;
	TArray<uint8> ArrayCPUForGPUAs32Data;

	/**
	 * Auxiliary for the templated constructor FNeuralTensor(const ENeuralDataType InDataType, const TArray<T>& InArray, ...).
	 * InSizeOfT and sizeof(InDataType) should match, as well as the volume of InValueNum and InSizes.
	 */
	explicit FNeuralTensor(const ENeuralDataType InDataType, const void* const InValues, const int64 InSizeOfT, const int64 InValueNum, const TArray<int64>& InSizes, const FString& InName, const ENeuralTensorTypeGPU InTensorTypeGPU);

	/**
	 * Auxiliary for the templated SetFromArrayCopy().
	 */
	void SetFromPointer(const void* const InData, const int64 InSizeOfT, const int64 InDataSize);

	/**
	 * Checks and warns whether the current data type T is incompatible with DataType.
	 */
	template<typename T>
	bool CheckTAndDataType() const;
	bool CheckTAndDataTypeResult(const bool bInCheckTAndDataTypeResult, const int64 InSizeOfT) const;
};



/* FNeuralTensor inline functions
 *****************************************************************************/

template <typename T>
FNeuralTensor::FNeuralTensor(const TArray<T>& InArray, const TArray<int64>& InSizes, const FString& InName, const ENeuralTensorTypeGPU InTensorTypeGPU)
	: FNeuralTensor(FDataType::GetDataType<T>(), InArray.GetData(), sizeof(T), InArray.Num(), (InSizes.Num() > 0 ? InSizes : TArray<int64>({ (int64)InArray.Num() })), InName, InTensorTypeGPU)
{}

bool FNeuralTensor::operator!=(const FNeuralTensor& InTensorToCopy) const
{
	return !(*this == InTensorToCopy);
}

template <typename T, typename TInput>
T& FNeuralTensor::At(const TInput InIndex)
{
	checkf(CheckTAndDataType<T>(), TEXT("FNeuralTensor::At(): CheckTAndDataType failed."));
	return ((T*)ArrayCPU.GetData())[InIndex];
}

template <typename T, typename TInput>
const T& FNeuralTensor::At(const TInput InIndex) const
{
	checkf(CheckTAndDataType<T>(), TEXT("FNeuralTensor::At(): CheckTAndDataType failed."));
	return ((T*)ArrayCPU.GetData())[InIndex];
}

template <typename T>
TArray<T> FNeuralTensor::GetArrayCopy() const
{
	TArray<T> Array;
	if (CheckTAndDataType<T>())
	{
		Array.SetNumUninitialized(Num());
		FMemory::Memcpy(Array.GetData(), ArrayCPU.GetData(), NumInBytes());
	}
	return Array;
}

const TArray<uint8>& FNeuralTensor::GetUnderlyingUInt8ArrayRef() const
{
	return ArrayCPU;
}

void* FNeuralTensor::GetData()
{
	return (void*)ArrayCPU.GetData();
}

const void* const FNeuralTensor::GetData() const
{
	return (void*)ArrayCPU.GetData();
}

template<typename T>
T* FNeuralTensor::GetDataCasted()
{
	checkf(CheckTAndDataType<T>(), TEXT("FNeuralTensor::GetDataCasted(): CheckTAndDataType failed."));
	return (T*)ArrayCPU.GetData();
}

template<typename T>
const T* const FNeuralTensor::GetDataCasted() const
{
	checkf(CheckTAndDataType<T>(), TEXT("FNeuralTensor::GetDataCasted(): CheckTAndDataType failed."));
	return (T*)ArrayCPU.GetData();
}

FString FNeuralTensor::GetName() const
{
	return Name;
}

ENeuralDataType FNeuralTensor::GetDataType() const
{
	return DataType;
}

const TArray<int64>& FNeuralTensor::GetSizes() const
{
	return Sizes;
}

int32 FNeuralTensor::GetNumberDimensions() const
{
	return Sizes.Num();
}

int64 FNeuralTensor::NumInBytes() const
{
	return ArrayCPU.Num();
}

int64 FNeuralTensor::Num() const
{
	return Volume;
}

bool FNeuralTensor::IsEmpty() const
{
	return ArrayCPU.IsEmpty();
}

ENeuralTensorTypeGPU FNeuralTensor::GetTensorTypeGPU() const
{
	return TensorTypeGPU;
}

void FNeuralTensor::SetEnableGPU(const bool bInEnableGPU)
{
	bEnableGPU = bInEnableGPU;
}

void FNeuralTensor::SetNumUninitialized(const int64 InVolume, const ENeuralDataType InDataType, const bool bInAllowShrinking)
{
	SetNumUninitialized((InVolume > 0 ? TArray<int64>({ InVolume }) : TArray<int64>({})), InDataType, bInAllowShrinking);
}

template<typename T>
void FNeuralTensor::SetFromArrayCopy(const TArray<T>& InArray)
{
	if (CheckTAndDataType<T>())
	{
		SetFromPointer(InArray.GetData(), sizeof(T), InArray.Num());
	}
}

template<typename T, typename TInput>
void FNeuralTensor::SetTo(const TInput InValue)
{
	if (CheckTAndDataType<T>())
	{
		std::fill(GetDataCasted<T>(), GetDataCasted<T>() + Num(), (T)InValue); // There is no UE equivalent for std::fill
	}
}

template<typename T>
bool FNeuralTensor::CheckTAndDataType() const
{
	return CheckTAndDataTypeResult(FDataType::CheckTAndDataType<T>(DataType), sizeof(T));
}
