// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralEnumClasses.h"
#include "RenderGraphBuilder.h" // FRDGBuilder
#include "RenderGraphDefinitions.h" // FRDGBufferSRVRef, FRDGBufferUAVRef
#include "RenderGraphResources.h" // FRDGPooledBuffer
#include "RHIDefinitions.h" // EBufferUsageFlags
#include "Templates/RefCounting.h" // TRefCountPtr
#include <algorithm> // std::fill
#include "NeuralTensor.generated.h"

/**
 * For a general overview of NeuralNetworkInference (NNI), including documentation and code samples, @see UNeuralNetwork, the main class of NNI.
 *
 * FNeuralTensor is an auxiliary class of UNeuralNetwork which represents a tensor of the UNeuralNetwork model. It is Unreal Engine's equivalent of
 * torch.Tensor (PyTorch) or caffe::Blob.
 *
 * Most of its functions run on the CPU, so `ToGPU_RenderThread()` must be called before running on GPU and after running any FNeuralTensor function
 * that modifies the CPU memory. In addition, FNeuralTensor's CPU functions are very similar to those of TArray<T>.
 */
USTRUCT()
struct NEURALNETWORKINFERENCE_API FNeuralTensor
{
	GENERATED_BODY()

public:
	/**
	 * There are several constructors of FNeuralTensor, all of them similar to each other. They set and fill the main variables of this class:
	 *
	 * @param InDataType It sets DataType, which defines the underlying uint8 data type of the network (float, double, int32, etc).
	 * @param InSizes It sets Sizes and Volume. Sizes defines the dimensions of the tensor and Volume the total number of elements (mathematically
	 *  Prod(Sizes[Index]) for all Indexes). Set to empty (or omit this argument) if memory allocation is not required or the final tensor size is
	 *  unknown.
	 * @param InVolume Alternative to InSizes which also sets Sizes and Volume. Using InVolume is equivalent to using
	 *  InSizes = TArray<int64>({InVolume}). I.e., it behaves like InSizes, but assumes Sizes.Num() == 1 and sets Sizes[0] = Volume = InVolume. Set
	 *  to 0 or a negative value if memory allocation is not required or the final size is unknown.
	 * @param InName It sets Name, which is used for GPU debugging and the ToString() function.
	 * @param InTensorType It sets TensorType, which is used when moving memory to the GPU.
	 * @param InArray Alternative to InSizes/InVolume, and represents the input data to copy from. The templated constructor that uses InArray is a
	 *  simple (but not efficient) way of initializing a FNeuralTensor from the existing InArray TArray. It is equivalent to calling
	 *  FNeuralTensor(..., InSizes/InVolume, ...) and then SetFromArrayCopy(InArray). It is not efficient because it makes a deep copy of the data
	 *  of InArray. For maximum speed, rather than creating this intermediate InArray TArray, you can avoid the double copy by calling
	 *  FNeuralTensor(..., InSizes/InVolume, ...) and then filling the tensor memory with GetData()/GetDataCasted().
	 *
	 * In addition, the array containing the CPU data, UnderlyingUInt8ArrayData, is pre-allocated based on the combination of InDataType and
	 * InSizes/InVolume (if InSizes/InVolume not empty/zero): UnderlyingUInt8ArrayData.Num() == sizeof(data type) x Volume.
	 */
	FNeuralTensor(const ENeuralDataType InDataType = ENeuralDataType::None, const TArray<int64>& InSizes = TArray<int64>(),
		const FString& InName = TEXT("FNeuralTensor"), const ENeuralTensorType InTensorType = ENeuralTensorType::Generic);
	FNeuralTensor(const ENeuralDataType InDataType, const int64 InVolume, const FString& InName = TEXT("FNeuralTensor"),
		const ENeuralTensorType InTensorType = ENeuralTensorType::Generic);
	FNeuralTensor(const FString& InName, const ENeuralTensorType InTensorType = ENeuralTensorType::Generic);
	template <typename T>
	FNeuralTensor(const TArray<T>& InArray, const TArray<int64>& InSizes = TArray<int64>(), const FString& InName = TEXT("FNeuralTensor"),
		const ENeuralTensorType InTensorType = ENeuralTensorType::Generic);

	/**
	 * Copy and move constructors and assignment functions, respectively.
	 * - Copy constructor and assignment (input is a const FNeuralTensor): They perform a deep (slow but safe) copy of the current tensor. The
	 *   resulting tensor will not share any parameters with the current one and both can be safely used.
	 * - Move constructor and assignment (input is not const and will not be valid after calling this function): They move the members of the input
	 *   InTensor FNeuralTensor, thus InTensor is no longer safe to use.
	 *
	 * @param InTensor The input FNeuralTensor to copy/move.
	 */
	FNeuralTensor(const FNeuralTensor& InTensor);
	FNeuralTensor& operator=(const FNeuralTensor& InTensor);
	FNeuralTensor(FNeuralTensor&& InTensor);
	FNeuralTensor& operator=(FNeuralTensor&& InTensor);

	/**
	 * Comparison operators (equal and not equal). They compare whether the CPU variables match between the 2 FNeuralTensor's (i.e., DataType, Sizes,
	 * Volume and CPU data). They do not consider other FNeuralTensor variables (such as ENeuralTensorType).
	 * @return
	 * - Equal operator, operator==(): True if the CPU variables matched each other, false otherwise.
	 * - Not equal operator, operator!=() (opposite than the equal operator): False if the CPU variables matched each other, false otherwise.
	 */
	bool operator==(const FNeuralTensor& InTensor) const;
	FORCEINLINE bool operator!=(const FNeuralTensor& InTensor) const;

	/**
	 * @see At(), GetArrayCopy(), GetUnderlyingUInt8ArrayRef(), GetData(), GetDataCasted(), which represent all the functions that can access/modify
	 *  the CPU memory. These functions could result in undefined behavior in not used properly:
	 * - If FNeuralTensor goes out of scope, it is destructed, or its memory is reset (constructor, SetNum(), etc.), these reference/pointer returned
	 *   by any of these functions will no longer be valid (except GetUnderlyingUInt8ArrayRef()).
	 * - These functions only modify the CPU memory, they do not synchronize CPU and GPU memory automatically. The user must do this accordingly.
	 *
	 * At() returns a reference to the desired indexed element of the underlying CPU data (UnderlyingUInt8ArrayData). There are 2 variations of this
	 * function, one constant (it does not allow to modify the referenced value) and a non-const one (which allows modifying it).
	 *
	 * @return Reference to the indexed element (const or non-constant).
	 */
	template <typename T, typename TInput>
	T& At(const TInput InIndex);
	template <typename T, typename TInput>
	const T& At(const TInput InIndex) const;

	/**
	 * @see At(), GetArrayCopy(), GetUnderlyingUInt8ArrayRef(), GetData(), GetDataCasted(), which represent all the functions that can access/modify
	 *  the CPU memory. These functions could result in undefined behavior in not used properly:
	 * - If FNeuralTensor goes out of scope, it is destructed, or its memory is reset (constructor, SetNum(), etc.), these reference/pointer returned
	 *   by any of these functions will no longer be valid (except GetUnderlyingUInt8ArrayRef()).
	 * - These functions only modify the CPU memory, they do not synchronize CPU and GPU memory automatically. The user must do this accordingly.
	 *
	 * There are 2 functions to access the tensor as a TArray (but none of them can modify the tensor underlying memory):
	 * - GetArrayCopy<T> (slower but safer) returns a copy of the data as a TArray<T>. T has to be the same size than sizeof(DataType).
	 * - GetUnderlyingUInt8ArrayRef (faster but could go out of scope) returns a reference to the underlying TArray<uint8> that contains the results.
	 */
	template <typename T>
	TArray<T> GetArrayCopy() const;
	FORCEINLINE const TArray<uint8>& GetUnderlyingUInt8ArrayRef() const;

	/**
	 * @see At(), GetArrayCopy(), GetUnderlyingUInt8ArrayRef(), GetData(), GetDataCasted(), which represent all the functions that can access/modify
	 *  the CPU memory. These functions could result in undefined behavior in not used properly:
	 * - If FNeuralTensor goes out of scope, it is destructed, or its memory is reset (constructor, SetNum(), etc.), these reference/pointer returned
	 *   by any of these functions will no longer be valid (except GetUnderlyingUInt8ArrayRef()).
	 * - These functions only modify the CPU memory, they do not synchronize CPU and GPU memory automatically. The user must do this accordingly.
	 *
	 * Both GetData() and GetDataCasted() return a pointer of the underlying CPU data (UnderlyingUInt8ArrayData):
	 * - GetData() returns a void pointer.
	 * - GetDataCasted() returns a pointer casted to the desired type T.
	 * There are 2 variations of each of those 2 functions, one constant (it does not allow to modify the pointer values) and a non-const one (which
	 * allows modifying it).
	 *
	 * @return Void (GetData) or T (GetDataCasted) pointer to the FNeuralTensor data (const or non-constant).
	 */
	FORCEINLINE void* GetData();
	FORCEINLINE const void* const GetData() const;

	template<typename T>
	FORCEINLINE T* GetDataCasted();

	template<typename T>
	FORCEINLINE const T* const GetDataCasted() const;

	/**
	 * It returns a copy of the name string.
	 */
	FORCEINLINE const FString& GetName() const;

	/**
	 * It returns a TCHAR pointer of the name string.
	 */
	FORCEINLINE const TCHAR* GetNameData() const;

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
	 * It returns the ENeuralTensorType (Generic, Input, Intermediate(Not)Initialized, Output, Weight, etc.).
	 */
	FORCEINLINE ENeuralTensorType GetTensorTypeGPU() const;

	/**
	 * It sets the ENeuralTensorType (Generic, Input, Intermediate(Not)Initialized, Output, Weight, etc.).
	 * If the GPU memory was already initialized, it will also UE_LOG a warning.
	 */
	void SetTensorTypeGPU(const ENeuralTensorType InTensorType);

	FORCEINLINE void SetEnableGPU(const bool bInEnableGPU);

	/**
	 * It uploads/downloads the memory from/to the CPU to/from the GPU based on TensorType (which sets a preset of EBufferUsageFlags flags).
	 * @param InEBufferUsageFlags gives the user total control over the buffer flags (and ignores the TensorType flag). This is meant to be filled
	 * with a combination of EBufferUsageFlags values.
	 */
	void ToGPU_RenderThread(FRDGBuilder* InOutGraphBuilder);
	void ToGPU_RenderThread(FRDGBuilder* InOutGraphBuilder, const EBufferUsageFlags InEBufferUsageFlags, const bool bInShouldCopyFromCPU);
	void UpdateSRVAndOrUAV_RenderThread(FRDGBuilder* InOutGraphBuilder);
	void ToCPU_RenderThread(FRDGBuilder* InOutGraphBuilder);

	/**
	 * Allocate data for GPU pooled buffer.
	 * NativeResource is a pointer to ID3D12Resource obtained from PooledBuffer and that one can be shared by DirectML execution provider
	 */
	bool InitPooledBuffer(void** NativeResource = nullptr);

	TRefCountPtr<FRDGPooledBuffer>& GetPooledBuffer() const;
	const FRDGBufferSRVRef GetBufferSRVRef() const;
	FRDGBufferUAVRef GetBufferUAVRef();

	/**
	 * Resize the FNeuralTensor to the desired new size.
	 * Analog to TArray<T>::SetNumUninitialized().
	 * @param InTensor Set Sizes and DataType from the input InTensor.
	 * @param InVolume Set InVolume to 0 if memory allocation is not required or the final size is unknown,
	 * SetNumUninitialized(InVolume) == SetNumUninitialized({InVolume}).
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
	 * @return Whether the conversion was successful.
	 */
	bool SetFromTensorProto(const struct FTensorProto* const InTensorProto, const FString& InTensorName, const ENeuralTensorType InTensorType);

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
	 * @param bInReturnOnlyData If false (default), it will print all information.
	 * E.g., "FNeuralTensor: Int64, Generic, volume=3, sizes={3}, data=[1 2 3]".
	 * If false, it will simply print the data. I.e., "[1 2 3]" for the previous example.
	 */
	FString ToString(const int64 InMaxNumberElementsToDisplay = -1, const bool bInReturnOnlyData = false) const;

protected:
	/**
	 * General variables and properties set by the FNeuralTensor constructor and saved on the FNeuralTensor UAsset.
	 * @see FNeuralTensor::FNeuralTensor() for more details about each one of these members.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralDataType DataType;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	TArray<int64> Sizes;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	int64 Volume;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	FString Name;
	UPROPERTY(VisibleAnywhere, Category = "Neural Network Inference")
	ENeuralTensorType TensorType;

private:
	/** CPU-based variables */
	UPROPERTY()
	TArray<uint8> UnderlyingUInt8ArrayData;
	/**
	 * GPU-based variables that are transient (do not need to be serialized) and initialized on the fly (with ToGPU/ToCPU).
	 * - bEnableGPU: By default false, meaning all GPU memory will be disabled and those functions will not do anything. Enable to allow using the
	 *   GPU functions and variables of the tensor.
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Neural Network Inference")
	bool bEnableGPU;
	TSharedPtr<TRefCountPtr<FRDGPooledBuffer>> PooledBuffer;
	TSharedPtr<FRDGBufferSRVRef> BufferSRVRef;
	TSharedPtr<FRDGBufferUAVRef> BufferUAVRef;

	/**
	 * Auxiliary for the templated constructor FNeuralTensor(const ENeuralDataType InDataType, const TArray<T>& InArray, ...).
	 * InSizeOfT and sizeof(InDataType) should match, as well as the volume of InValueNum and InSizes.
	 */
	FNeuralTensor(const ENeuralDataType InDataType, const void* const InValues, const int64 InSizeOfT, const int64 InValueNum,
		const TArray<int64>& InSizes, const FString& InName, const ENeuralTensorType InTensorType);

	/**
	 * Auxiliary for the templated SetFromArrayCopy().
	 */
	void SetFromPointer(const void* const InData, const int64 InSizeOfT, const int64 InDataSize);

	/**
	 * Checks and warns whether the current data type T is incompatible with DataType.
	 */
	template<typename T>
	bool CheckTAndDataTypeEquivalent() const;
	bool CheckTAndDataTypeResult(const bool bInCheckTAndDataTypeResult, const int64 InSizeOfT) const;
};



/* FNeuralTensor inlined and templated functions
 *****************************************************************************/

template <typename T>
FNeuralTensor::FNeuralTensor(const TArray<T>& InArray, const TArray<int64>& InSizes, const FString& InName, const ENeuralTensorType InTensorType)
	: FNeuralTensor(FNeuralDataTypeUtils::GetDataType<T>(), InArray.GetData(), sizeof(T), InArray.Num(),
		(InSizes.Num() > 0 ? InSizes : TArray<int64>({ (int64)InArray.Num() })), InName, InTensorType)
{}

bool FNeuralTensor::operator!=(const FNeuralTensor& InTensor) const
{
	return !(*this == InTensor);
}

template <typename T, typename TInput>
T& FNeuralTensor::At(const TInput InIndex)
{
	checkf(CheckTAndDataTypeEquivalent<T>(), TEXT("FNeuralTensor::At(): CheckTAndDataType failed."));
	return ((T*)UnderlyingUInt8ArrayData.GetData())[InIndex];
}

template <typename T, typename TInput>
const T& FNeuralTensor::At(const TInput InIndex) const
{
	checkf(CheckTAndDataTypeEquivalent<T>(), TEXT("FNeuralTensor::At(): CheckTAndDataType failed."));
	return ((T*)UnderlyingUInt8ArrayData.GetData())[InIndex];
}

template <typename T>
TArray<T> FNeuralTensor::GetArrayCopy() const
{
	TArray<T> Array;
	if (CheckTAndDataTypeEquivalent<T>())
	{
		Array.SetNumUninitialized(Num());
		FMemory::Memcpy(Array.GetData(), UnderlyingUInt8ArrayData.GetData(), NumInBytes());
	}
	return Array;
}

const TArray<uint8>& FNeuralTensor::GetUnderlyingUInt8ArrayRef() const
{
	return UnderlyingUInt8ArrayData;
}

void* FNeuralTensor::GetData()
{
	return (void*)UnderlyingUInt8ArrayData.GetData();
}

const void* const FNeuralTensor::GetData() const
{
	return (void*)UnderlyingUInt8ArrayData.GetData();
}

template<typename T>
T* FNeuralTensor::GetDataCasted()
{
	checkf(CheckTAndDataTypeEquivalent<T>(), TEXT("FNeuralTensor::GetDataCasted(): CheckTAndDataType failed."));
	return (T*)UnderlyingUInt8ArrayData.GetData();
}

template<typename T>
const T* const FNeuralTensor::GetDataCasted() const
{
	checkf(CheckTAndDataTypeEquivalent<T>(), TEXT("FNeuralTensor::GetDataCasted(): CheckTAndDataType failed."));
	return (T*)UnderlyingUInt8ArrayData.GetData();
}

const FString& FNeuralTensor::GetName() const
{
	return Name;
}

const TCHAR* FNeuralTensor::GetNameData() const
{
	return Name.GetCharArray().GetData();
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
	return UnderlyingUInt8ArrayData.Num();
}

int64 FNeuralTensor::Num() const
{
	return Volume;
}

bool FNeuralTensor::IsEmpty() const
{
	return UnderlyingUInt8ArrayData.IsEmpty();
}

ENeuralTensorType FNeuralTensor::GetTensorTypeGPU() const
{
	return TensorType;
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
	if (CheckTAndDataTypeEquivalent<T>())
	{
		SetFromPointer(InArray.GetData(), sizeof(T), InArray.Num());
	}
}

template<typename T, typename TInput>
void FNeuralTensor::SetTo(const TInput InValue)
{
	if (CheckTAndDataTypeEquivalent<T>())
	{
		std::fill(GetDataCasted<T>(), GetDataCasted<T>() + Num(), (T)InValue); // There is no UE equivalent for std::fill
	}
}

template<typename T>
bool FNeuralTensor::CheckTAndDataTypeEquivalent() const
{
	return CheckTAndDataTypeResult(FNeuralDataTypeUtils::GetDataType<T>() == DataType, sizeof(T));
}
