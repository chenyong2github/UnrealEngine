// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceArray.h"
#include "NiagaraDataInterfaceArrayImpl.h"
#include "NiagaraDataInterfaceArrayInt.generated.h"

template<>
struct FNDIArrayImplHelper<int32> : public FNDIArrayImplHelperBase<int32>
{
	typedef int32 TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("int");
	static constexpr EPixelFormat ReadPixelFormat = PF_R32_SINT;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("int");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr EPixelFormat RWPixelFormat = PF_R32_SINT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("int");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index] = Value;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetIntDef(); }
	static const int32 GetDefaultValue() { return 0; }
};

template<>
struct FNDIArrayImplHelper<bool> : public FNDIArrayImplHelperBase<bool>
{
	typedef FNiagaraBool TVMArrayType;

	static constexpr TCHAR const* HLSLVariableType = TEXT("bool");
	static constexpr EPixelFormat ReadPixelFormat = PF_R8_UINT;
	static constexpr TCHAR const* ReadHLSLBufferType = TEXT("uint");
	static constexpr TCHAR const* ReadHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr EPixelFormat RWPixelFormat = PF_R8_UINT;
	static constexpr TCHAR const* RWHLSLBufferType = TEXT("uint");
	static constexpr TCHAR const* RWHLSLBufferRead = TEXT("Value = BUFFER_NAME[Index];");
	static constexpr TCHAR const* RWHLSLBufferWrite = TEXT("BUFFER_NAME[Index] = Value;");

	static const FNiagaraTypeDefinition& GetTypeDefinition() { return FNiagaraTypeDefinition::GetBoolDef(); }
	static const FNiagaraBool GetDefaultValue() { return FNiagaraBool::False; }

	static void CopyToGpuMemory(void* Dest, const bool* Src, int32 NumElements)
	{
		uint8* TypedDest = reinterpret_cast<uint8*>(Dest);
		while (NumElements--)
		{
			*TypedDest++ = *Src++ == false ? 0 : 0xff;
		}
	}

	static void CopyToCpuMemory(void* Dest, const void* Src, int32 NumElements)
	{
		FNiagaraBool* TypedDest = reinterpret_cast<FNiagaraBool*>(Dest);
		const uint8* TypedSrc = reinterpret_cast<const uint8*>(Src);
		while (NumElements--)
		{
			*TypedDest++ = *TypedSrc++ == 0 ? false : true;
		}
	}
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Int32 Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayInt32 : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<int32> IntData;

	TArray<int32>& GetArrayReference() { return IntData; }
};

UCLASS(EditInlineNew, Category = "Array", meta = (DisplayName = "Bool Array", Experimental), Blueprintable, BlueprintType)
class NIAGARA_API UNiagaraDataInterfaceArrayBool : public UNiagaraDataInterfaceArray
{
	GENERATED_UCLASS_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Array")
	TArray<bool> BoolData;

	TArray<bool>& GetArrayReference() { return BoolData; }
};
