// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigDispatchFactory.h"
#include "RigUnit_Metadata.generated.h"

USTRUCT(meta=(Abstract, Category="Hierarchy", NodeColor="0.462745, 1,0, 0.329412", Varying))
struct CONTROLRIG_API FRigDispatch_MetadataBase : public FRigDispatchFactory
{
	GENERATED_BODY()

#if WITH_EDITOR
	virtual FString GetNodeTitle(const FRigVMTemplateTypeMap& InTypes) const override;;
#endif
	virtual TArray<FRigVMTemplateArgument> GetArguments() const override;
	virtual bool IsSetMetadata() const { return false; }

#if WITH_EDITOR
	virtual FText GetArgumentTooltip(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex) const override;
#endif

protected:

	const TArray<TRigVMTypeIndex>& GetValueTypes() const;

	mutable TArray<FRigVMTemplateArgument> Arguments;
	
	mutable int32 ExecuteArgIndex = INDEX_NONE;
	mutable int32 ItemArgIndex = INDEX_NONE;
	mutable int32 NameArgIndex = INDEX_NONE;
	mutable int32 CacheArgIndex = INDEX_NONE;
	mutable int32 DefaultArgIndex = INDEX_NONE;
	mutable int32 ValueArgIndex = INDEX_NONE;
	mutable int32 FoundArgIndex = INDEX_NONE;
	mutable int32 SuccessArgIndex = INDEX_NONE;
	static FName ItemArgName;
	static FName NameArgName;
	static FName CacheArgName;
	static FName DefaultArgName;
	static FName ValueArgName;
	static FName FoundArgName;
	static FName SuccessArgName;
};

/*
 * Sets some metadata for the provided item
 */
USTRUCT(meta=(DisplayName="Get Metadata"))
struct CONTROLRIG_API FRigDispatch_GetMetadata : public FRigDispatch_MetadataBase
{
	GENERATED_BODY()

	virtual TArray<FRigVMTemplateArgument> GetArguments() const override;

protected:

	static FRigBaseMetadata* FindMetadata(const FRigVMExtendedExecuteContext& InContext, const FRigElementKey& InKey, const FName& InName, ERigMetadataType InType, FCachedRigElement& Cache);
	virtual FRigVMFunctionPtr GetDispatchFunctionImpl(const FRigVMTemplateTypeMap& InTypes) const override;

#if WITH_EDITOR
	template<typename ValueType>
	FORCEINLINE_DEBUGGABLE bool CheckArgumentTypes(FRigVMMemoryHandleArray Handles) const
	{
		return CheckArgumentType(Handles[ItemArgIndex].IsType<FRigElementKey>(), ItemArgName) &&
			CheckArgumentType(Handles[NameArgIndex].IsType<FName>(), NameArgName) &&
			CheckArgumentType(Handles[CacheArgIndex].IsType<FCachedRigElement>(true), CacheArgName) &&
			CheckArgumentType(Handles[DefaultArgIndex].IsType<ValueType>(), DefaultArgName) &&
			CheckArgumentType(Handles[ValueArgIndex].IsType<ValueType>(), ValueArgName) &&
			CheckArgumentType(Handles[FoundArgIndex].IsType<bool>(), FoundArgName);
	}
#endif

	template<typename ValueType, typename MetadataType, ERigMetadataType EnumValue>
	FORCEINLINE_DEBUGGABLE static void GetMetadataDispatch(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles)
	{
		const FRigDispatch_GetMetadata* Factory = static_cast<const FRigDispatch_GetMetadata*>(InContext.Factory);

#if WITH_EDITOR
		if(!Factory->CheckArgumentTypes<ValueType>(Handles))
		{
			return;
		}
#endif

		// unpack the memory
		const FRigElementKey& Item = *(const FRigElementKey*)Handles[Factory->ItemArgIndex].GetData();
		const FName& Name = *(const FName*)Handles[Factory->NameArgIndex].GetData();
		FCachedRigElement& Cache = *(FCachedRigElement*)Handles[Factory->CacheArgIndex].GetData(false, InContext.GetSlice().GetIndex());
		const ValueType& Default = *(const ValueType*)Handles[Factory->DefaultArgIndex].GetData();
		ValueType& Value = *(ValueType*)Handles[Factory->ValueArgIndex].GetData();
		bool& Found = *(bool*)Handles[Factory->FoundArgIndex].GetData();

		// extract the metadata
		if (MetadataType* Md = Cast<MetadataType>(FindMetadata(InContext, Item, Name, EnumValue, Cache)))
		{
			Value = Md->GetValue();
			Found = true;
		}
		else
		{
			Value = Default;
			Found = false;
		}
	}
};

/*
 * Returns some metadata for the provided item
 */
USTRUCT(meta=(DisplayName="Set Metadata"))
struct CONTROLRIG_API FRigDispatch_SetMetadata : public FRigDispatch_MetadataBase
{
	GENERATED_BODY()

	virtual TArray<FRigVMTemplateArgument> GetArguments() const override;
	virtual bool IsSetMetadata() const override { return true; }

protected:

	static FRigBaseMetadata* FindOrAddMetadata(FControlRigExecuteContext& InContext, const FRigElementKey& InKey, const FName& InName, ERigMetadataType InType, FCachedRigElement& Cache);
	virtual FRigVMFunctionPtr GetDispatchFunctionImpl(const FRigVMTemplateTypeMap& InTypes) const override;

#if WITH_EDITOR
	template<typename ValueType>
	FORCEINLINE_DEBUGGABLE bool CheckArgumentTypes(FRigVMMemoryHandleArray Handles) const
	{
		return CheckArgumentType(Handles[ExecuteArgIndex].IsType<FControlRigExecuteContext>(), ItemArgName) &&
			CheckArgumentType(Handles[ItemArgIndex].IsType<FRigElementKey>(), ItemArgName) &&
			CheckArgumentType(Handles[NameArgIndex].IsType<FName>(), NameArgName) &&
			CheckArgumentType(Handles[CacheArgIndex].IsType<FCachedRigElement>(true), CacheArgName) &&
			CheckArgumentType(Handles[ValueArgIndex].IsType<ValueType>(), ValueArgName) &&
			CheckArgumentType(Handles[SuccessArgIndex].IsType<bool>(), SuccessArgName);
	}
#endif
	
	template<typename ValueType, typename MetadataType, ERigMetadataType EnumValue>
	FORCEINLINE_DEBUGGABLE static void SetMetadataDispatch(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles)
	{
		const FRigDispatch_SetMetadata* Factory = static_cast<const FRigDispatch_SetMetadata*>(InContext.Factory);

#if WITH_EDITOR
		if(!Factory->CheckArgumentTypes<ValueType>(Handles))
		{
			return;
		}
#endif

		// unpack the memory
		FControlRigExecuteContext& ExecuteContext = *(FControlRigExecuteContext*)Handles[Factory->ExecuteArgIndex].GetData();
		const FRigElementKey& Item = *(const FRigElementKey*)Handles[Factory->ItemArgIndex].GetData();
		const FName& Name = *(const FName*)Handles[Factory->NameArgIndex].GetData();
		FCachedRigElement& Cache = *(FCachedRigElement*)Handles[Factory->CacheArgIndex].GetData(false, InContext.GetSlice().GetIndex());
		ValueType& Value = *(ValueType*)Handles[Factory->ValueArgIndex].GetData();
		bool& Success = *(bool*)Handles[Factory->SuccessArgIndex].GetData();

		// extract the metadata
		if (MetadataType* Md = Cast<MetadataType>(FindOrAddMetadata(ExecuteContext, Item, Name, EnumValue, Cache)))
		{
			Md->GetValue() = Value;
			Success = true;
		}
		else
		{
			Success = false;
		}
	}
};

/**
 * Returns true if a given item in the hierarchy has a specific set of metadata
 */
USTRUCT(meta=(DisplayName="Has Metadata", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="MetadataExists,HasKey,SupportsMetadata", NodeColor="0.462745, 1,0, 0.329412", Varying))
struct CONTROLRIG_API FRigUnit_HasMetadata : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_HasMetadata()
		: Item(NAME_None, ERigElementType::Bone)
		, Name(NAME_None)
		, Type(ERigMetadataType::Float)
		, bFound(false)
		, CachedIndex()
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The item to check the metadata for
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * The name of the metadata to check
	 */ 
	UPROPERTY(meta = (Input))
	FName Name;

	/**
	 * The type of metadata to check for
	 */ 
	UPROPERTY(meta = (Input))
	ERigMetadataType Type;

	// True if the item has the metadata
	UPROPERTY(meta=(Output))
	bool bFound;

	// Used to cache the internally
	UPROPERTY()
	FCachedRigElement CachedIndex;
};

/**
 * Returns all items containing a specific set of metadata
 */
USTRUCT(meta=(DisplayName="Find Items with Metadata", Category="Hierarchy", DocumentationPolicy = "Strict", Keywords="MetadataExists,HasKey,SupportsMetadata", NodeColor="0.462745, 1,0, 0.329412", Varying))
struct CONTROLRIG_API FRigUnit_FindItemsWithMetadata : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_FindItemsWithMetadata()
		: Name(NAME_None)
		, Type(ERigMetadataType::Float)
		, Items()
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the metadata to find
	 */ 
	UPROPERTY(meta = (Input))
	FName Name;

	/**
	 * The type of metadata to find
	 */ 
	UPROPERTY(meta = (Input))
	ERigMetadataType Type;

	// The items containing the metadata
	UPROPERTY(meta=(Output))
	TArray<FRigElementKey> Items;
};