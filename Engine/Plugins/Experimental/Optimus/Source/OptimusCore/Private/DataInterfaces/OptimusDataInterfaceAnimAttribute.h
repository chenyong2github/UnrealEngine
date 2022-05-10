// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "OptimusDataType.h"
#include "ComputeFramework/ComputeDataProvider.h"

#include "OptimusDataInterfaceAnimAttribute.generated.h"

class USkeletalMeshComponent;
class UOptimusValueContainer;

USTRUCT()
struct FOptimusAnimAttributeDescription
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	FString Name;

	// Used to look for attributes associated with a specific bone. Default to use the root bone
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	FName BoneName;

	UPROPERTY(EditAnywhere, Category = "Data Interface", meta=(UseInAnimAttribute))
	FOptimusDataTypeRef DataType;

	UPROPERTY(EditAnywhere, Category = "Data Interface", meta=(EditInLine))
	TObjectPtr<UOptimusValueContainer> DefaultValue = nullptr;
	
	UPROPERTY()
	FString HlslId;

	UPROPERTY()
	FName PinName;
	
	void UpdatePinNameAndHlslId(bool bInIncludeBoneName = true, bool bInIncludeTypeName = true);
	
	// Helpers
	FOptimusAnimAttributeDescription& Init(class UOptimusAnimAttributeDataInterface* InOwner,const FString& InName, FName InBoneName,
	const FOptimusDataTypeRef& InDataType);
	
private:
	FString GetFormattedId(
		const FString& InDelimiter,
		bool bInIncludeBoneName,
		bool bInIncludeTypeName) const;
};

USTRUCT()
struct FOptimusAnimAttributeArray
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, DisplayName = "Animation Attributes" ,Category = "Data Interface")
	TArray<FOptimusAnimAttributeDescription> InnerArray;

	template <typename Predicate>
	const FOptimusAnimAttributeDescription* FindByPredicate(Predicate Pred) const
	{
		return InnerArray.FindByPredicate(Pred);
	}

	bool IsEmpty() const
	{
		return InnerArray.IsEmpty();
	}

	FOptimusAnimAttributeDescription& Last(int32 IndexFromTheEnd = 0)
	{
		return InnerArray.Last(IndexFromTheEnd);
	}

	const FOptimusAnimAttributeDescription& Last(int32 IndexFromTheEnd = 0) const
	{
		return InnerArray.Last(IndexFromTheEnd);
	}

	FOptimusAnimAttributeArray& operator=(const TArray<FOptimusAnimAttributeDescription>& Rhs)
	{
		InnerArray = Rhs;
		return *this;
	}
	
	int32 Num() const { return InnerArray.Num();}
	bool IsValidIndex(int32 Index) const { return Index < InnerArray.Num() && Index >= 0; }
	const FOptimusAnimAttributeDescription& operator[](int32 InIndex) const { return InnerArray[InIndex]; }
	FOptimusAnimAttributeDescription& operator[](int32 InIndex) { return InnerArray[InIndex]; }
	FORCEINLINE	TArray<FOptimusAnimAttributeDescription>::RangedForIteratorType      begin()       { return InnerArray.begin(); }
	FORCEINLINE	TArray<FOptimusAnimAttributeDescription>::RangedForConstIteratorType begin() const { return InnerArray.begin(); }
	FORCEINLINE	TArray<FOptimusAnimAttributeDescription>::RangedForIteratorType      end()         { return InnerArray.end();   }
	FORCEINLINE	TArray<FOptimusAnimAttributeDescription>::RangedForConstIteratorType end() const   { return InnerArray.end();   }
};


/** Compute Framework Data Interface for reading animation attributes on skeletal mesh. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusAnimAttributeDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:

	UOptimusAnimAttributeDataInterface();

#if WITH_EDITOR
	void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	virtual TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("AnimAttribute"); }
	virtual void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	void GetHLSL(FString& OutHLSL) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	const FOptimusAnimAttributeDescription& AddAnimAttribute(const FString& InName, FName InBoneName, const FOptimusDataTypeRef& InDataType);
	
	UPROPERTY(EditAnywhere, Category = "Animation Attribute", meta = (ShowOnlyInnerProperties))
	FOptimusAnimAttributeArray AttributeArray;

private:
	FString GetUnusedAttributeName(const FString& InName) const;
	void UpdateAttributePinNamesAndHlslIds();
};

// Runtime data with cached values baked out from AttributeDescription
struct FOptimusAnimAttributeRuntimeData
{
	FOptimusAnimAttributeRuntimeData() = default;
	
	FOptimusAnimAttributeRuntimeData(const FOptimusAnimAttributeDescription& InDescription);
	
	FName Name;

	FName BoneName;

	FOptimusDataTypeRef DataType;
	
	int32 Offset;
	
	int32 CachedBoneIndex;

	TArray<uint8> CachedDefaultValue;

};

/** Compute Framework Data Provider for reading animation attributes on skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusAnimAttributeDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UOptimusAnimAttributeDataProvider() = default;
	
	void Init(
		USkeletalMeshComponent* InSkeletalMesh,
		TArray<FOptimusAnimAttributeDescription> InAttributeArray
	);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh = nullptr;

	TArray<FOptimusAnimAttributeRuntimeData> AttributeRuntimeData;

	TArray<uint8> AttributeBuffer;
	
	int32 AttributeBufferSize;
	
	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FOptimusAnimAttributeDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusAnimAttributeDataProviderProxy(
		TArray<uint8> InAttributeBuffer,
		int32 InAttributeBufferSize
	);

	//~ Begin FComputeDataProviderRenderProxy Interface
	void GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	TArray<uint8> AttributeBuffer;

	int32 AttributeBufferSize;
};
