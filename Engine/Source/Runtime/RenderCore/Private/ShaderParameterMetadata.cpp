// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderParameterMetadata.cpp: Shader parameter metadata implementations.
=============================================================================*/

#include "ShaderParameterMetadata.h"
#include "RenderCore.h"
#include "ShaderCore.h"

FUniformBufferStaticSlotRegistrar::FUniformBufferStaticSlotRegistrar(const TCHAR* InName)
{
	FUniformBufferStaticSlotRegistry::Get().RegisterSlot(InName);
}

FUniformBufferStaticSlotRegistry& FUniformBufferStaticSlotRegistry::Get()
{
	static FUniformBufferStaticSlotRegistry Registry;
	return Registry;
}

void FUniformBufferStaticSlotRegistry::RegisterSlot(FName SlotName)
{
	// Multiple definitions with the same name resolve to the same slot.
	const FUniformBufferStaticSlot Slot = FindSlotByName(SlotName);

	if (!IsUniformBufferStaticSlotValid(Slot))
	{
		SlotNames.Emplace(SlotName);
	}
}

#define VALIDATE_UNIFORM_BUFFER_UNIQUE_NAME (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

#if VALIDATE_UNIFORM_BUFFER_UNIQUE_NAME
static TMap<FName, FName> GlobalShaderVariableToStructMap;
#endif

static TLinkedList<FShaderParametersMetadata*>* GUniformStructList = nullptr;
static TMap<uint32, FShaderParametersMetadata*> GLayoutHashStructMap;

TLinkedList<FShaderParametersMetadata*>*& FShaderParametersMetadata::GetStructList()
{
	return GUniformStructList;
}

TMap<FName, FShaderParametersMetadata*>& FShaderParametersMetadata::GetNameStructMap()
{
	static TMap<FName, FShaderParametersMetadata*> NameStructMap;
	return NameStructMap;
}

FShaderParametersMetadata* FindUniformBufferStructByName(const TCHAR* StructName)
{
	return FindUniformBufferStructByFName(FName(StructName, FNAME_Find));
}

FShaderParametersMetadata* FindUniformBufferStructByFName(FName StructName)
{
	return FShaderParametersMetadata::GetNameStructMap().FindRef(StructName);
}

FShaderParametersMetadata* FindUniformBufferStructByLayoutHash(uint32 Hash)
{
	return GLayoutHashStructMap.FindRef(Hash);
}

class FUniformBufferMemberAndOffset
{
public:
	FUniformBufferMemberAndOffset(const FShaderParametersMetadata& InContainingStruct, const FShaderParametersMetadata::FMember& InMember, int32 InStructOffset) :
		ContainingStruct(InContainingStruct),
		Member(InMember),
		StructOffset(InStructOffset)
	{}

	const FShaderParametersMetadata& ContainingStruct;
	const FShaderParametersMetadata::FMember& Member;
	int32 StructOffset;
};

FShaderParametersMetadata::FShaderParametersMetadata(
	EUseCase InUseCase,
	const FName& InLayoutName,
	const TCHAR* InStructTypeName,
	const TCHAR* InShaderVariableName,
	const TCHAR* InStaticSlotName,
	uint32 InSize,
	const TArray<FMember>& InMembers)
	: StructTypeName(InStructTypeName)
	, ShaderVariableName(InShaderVariableName)
	, StaticSlotName(InStaticSlotName)
	, Size(InSize)
	, UseCase(InUseCase)
	, Layout(InLayoutName)
	, Members(InMembers)
	, GlobalListLink(this)
	, bLayoutInitialized(false)
{
	check(StructTypeName);
	if (UseCase == EUseCase::ShaderParameterStruct)
	{
		checkf(!StaticSlotName, TEXT("Only uniform buffers can be tagged with a static slot."));

		check(ShaderVariableName == nullptr);
	}
	else
	{
		check(ShaderVariableName);
	}

	if (UseCase == EUseCase::UniformBuffer)
	{
		// Register this uniform buffer struct in global list.
		GlobalListLink.LinkHead(GetStructList());

		FName StructTypeFName(StructTypeName);
		// Verify that during FName creation there's no case conversion
		checkSlow(FCString::Strcmp(StructTypeName, *StructTypeFName.GetPlainNameString()) == 0);
		GetNameStructMap().Add(FName(StructTypeFName), this);

#if VALIDATE_UNIFORM_BUFFER_UNIQUE_NAME
		FName ShaderVariableFName(ShaderVariableName);

		// Verify that the global variable name is unique so that we can disambiguate when reflecting from shader source.
		if (FName* StructFName = GlobalShaderVariableToStructMap.Find(ShaderVariableFName))
		{
			checkf(
				false,
				TEXT("Found duplicate Uniform Buffer shader variable name %s defined by struct %s. Previous definition ")
				TEXT("found on struct %s. Uniform buffer shader names must be unique to support name-based reflection of ")
				TEXT("shader source files."),
				ShaderVariableName,
				StructTypeName,
				*StructFName->GetPlainNameString());
		}

		GlobalShaderVariableToStructMap.Add(ShaderVariableFName, StructTypeFName);
#endif
	}
	else
	{
		// We cannot initialize the layout during global initialization, since we have to walk nested struct members.
		// Structs created during global initialization will have bRegisterForAutoBinding==false, and are initialized during startup.
		// Structs created at runtime with bRegisterForAutoBinding==true can be initialized now.
		InitializeLayout();
	}
}

FShaderParametersMetadata::~FShaderParametersMetadata()
{
	if (UseCase == EUseCase::UniformBuffer)
	{
		GlobalListLink.Unlink();
		GetNameStructMap().Remove(FName(StructTypeName, FNAME_Find));

#if VALIDATE_UNIFORM_BUFFER_UNIQUE_NAME
		GlobalShaderVariableToStructMap.Remove(FName(ShaderVariableName, FNAME_Find));
#endif

		if (bLayoutInitialized)
		{
			GLayoutHashStructMap.Remove(GetLayout().GetHash());
		}
	}
}


void FShaderParametersMetadata::InitializeAllUniformBufferStructs()
{
	for (TLinkedList<FShaderParametersMetadata*>::TIterator StructIt(FShaderParametersMetadata::GetStructList()); StructIt; StructIt.Next())
	{
		if (!StructIt->bLayoutInitialized)
		{
			StructIt->InitializeLayout();
		}
	}
}

void FShaderParametersMetadata::InitializeLayout()
{
	check(!bLayoutInitialized);
	Layout.ConstantBufferSize = Size;

	if (StaticSlotName)
	{
		checkf(UseCase == EUseCase::UniformBuffer,
			TEXT("Attempted to assign static slot %s to uniform buffer %s. Static slots are only supported for compile-time uniform buffers."),
			ShaderVariableName, StaticSlotName);

		const FUniformBufferStaticSlot StaticSlot = FUniformBufferStaticSlotRegistry::Get().FindSlotByName(StaticSlotName);

		checkf(IsUniformBufferStaticSlotValid(StaticSlot),
			TEXT("Uniform buffer of type '%s' and shader name '%s' attempted to reference static slot '%s', but the slot could not be found in the registry."),
			StructTypeName, ShaderVariableName, StaticSlotName);

		Layout.StaticSlot = StaticSlot;
	}

	TArray<FUniformBufferMemberAndOffset> MemberStack;
	MemberStack.Reserve(Members.Num());
	for (int32 MemberIndex = 0; MemberIndex < Members.Num(); MemberIndex++)
	{
		MemberStack.Push(FUniformBufferMemberAndOffset(*this, Members[MemberIndex], 0));
	}

	/** The point of RDG is to track resources that have deferred allocation. Could deffer the creation of uniform buffer,
	 * but there is a risk where it create more resource dependency than necessary on passes that reference this deferred
	 * uniform buffers. Therefore only allow graph resources in shader parameter structures.
	 */
	const bool bAllowGraphResources = UseCase == EUseCase::ShaderParameterStruct;

	/** Uniform buffer references are only allowed in shader parameter structures that may be used as a root shader parameter
	 * structure.
	 */
	const bool bAllowUniformBufferReferences = UseCase == EUseCase::ShaderParameterStruct;

	/** Resource array are currently only supported for shader parameter structures. */
	const bool bAllowResourceArrays = UseCase == EUseCase::ShaderParameterStruct;

	/** White list all use cases that inline a structure within another. Data driven are not known to inline structures. */
	const bool bAllowStructureInlining = UseCase == EUseCase::ShaderParameterStruct || UseCase == EUseCase::UniformBuffer;

	for (int32 i = 0; i < MemberStack.Num(); ++i)
	{
		const FShaderParametersMetadata& CurrentStruct = MemberStack[i].ContainingStruct;
		const FMember& CurrentMember = MemberStack[i].Member;

		EUniformBufferBaseType BaseType = CurrentMember.GetBaseType();
		const uint32 ArraySize = CurrentMember.GetNumElements();
		const FShaderParametersMetadata* ChildStruct = CurrentMember.GetStructMetadata();

		const bool bIsArray = ArraySize > 0;
		const bool bIsRHIResource = (
			BaseType == UBMT_TEXTURE ||
			BaseType == UBMT_SRV ||
			BaseType == UBMT_SAMPLER);
		const bool bIsRDGResource = IsRDGResourceReferenceShaderParameterType(BaseType);
		const bool bIsVariableNativeType = (
			BaseType == UBMT_INT32 ||
			BaseType == UBMT_UINT32 ||
			BaseType == UBMT_FLOAT32);

		if (DO_CHECK)
		{
			const FString CppName = FString::Printf(TEXT("%s::%s"), CurrentStruct.GetStructTypeName(), CurrentMember.GetName());

			if (BaseType == UBMT_BOOL)
			{
				UE_LOG(LogRendererCore, Fatal,
					TEXT("Shader parameter %s error: bool are actually illegal in shader parameter structure, ")
					TEXT("because bool type in HLSL means using scalar register to store binary information. ")
					TEXT("Boolean information should always be packed explicitly in bitfield to reduce memory footprint, ")
					TEXT("and use HLSL comparison operators to translate into clean SGPR, to have minimal VGPR footprint."), *CppName);
			}

			if (IsRDGResourceReferenceShaderParameterType(BaseType) || BaseType == UBMT_RENDER_TARGET_BINDING_SLOTS)
			{
				if (!bAllowGraphResources)
				{
					UE_LOG(LogRendererCore, Fatal, TEXT("Shader parameter %s error: Graph resources are only allowed in shader parameter structs."), *CppName);
				}
			}
			else if (BaseType == UBMT_REFERENCED_STRUCT)
			{
				if (!bAllowUniformBufferReferences)
				{
					UE_LOG(LogRendererCore, Fatal, TEXT("Shader parameter %s error: Shader parameter struct reference can only be done in shader parameter structs."), *CppName);
				}
			}
			else if (BaseType == UBMT_NESTED_STRUCT || BaseType == UBMT_INCLUDED_STRUCT)
			{
				check(ChildStruct);

				if (!bAllowStructureInlining)
				{
					UE_LOG(LogRendererCore, Fatal, TEXT("Shader parameter %s error: Shader parameter struct is not known inline other structures."), *CppName);
				}
				else if (ChildStruct->GetUseCase() != EUseCase::ShaderParameterStruct && UseCase == EUseCase::ShaderParameterStruct)
				{
					UE_LOG(LogRendererCore, Fatal, TEXT("Shader parameter %s error: can only nests or include shader parameter struct define with BEGIN_SHADER_PARAMETER_STRUCT(), but %s is not."), *CppName, ChildStruct->GetStructTypeName());
				}
			}

			const bool bTypeCanBeArray = (bAllowResourceArrays && (bIsRHIResource || bIsRDGResource)) || bIsVariableNativeType || BaseType == UBMT_NESTED_STRUCT;
			if (bIsArray && !bTypeCanBeArray)
			{
				UE_LOG(LogRendererCore, Fatal, TEXT("Shader parameter %s error: Not allowed to be an array."), *CppName);
			}
		}

		if (IsShaderParameterTypeForUniformBufferLayout(BaseType))
		{
			for (uint32 ArrayElementId = 0; ArrayElementId < (bIsArray ? ArraySize : 1u); ArrayElementId++)
			{
				const uint32 AbsoluteMemberOffset = CurrentMember.GetOffset() + MemberStack[i].StructOffset + ArrayElementId * SHADER_PARAMETER_POINTER_ALIGNMENT;
				check(AbsoluteMemberOffset < (1u << (sizeof(FRHIUniformBufferLayout::FResourceParameter::MemberOffset) * 8)));
				Layout.Resources.Add(FRHIUniformBufferLayout::FResourceParameter{ uint16(AbsoluteMemberOffset), BaseType });
			}
		}

		if (ChildStruct && BaseType != UBMT_REFERENCED_STRUCT)
		{
			for (uint32 ArrayElementId = 0; ArrayElementId < (bIsArray ? ArraySize : 1u); ArrayElementId++)
			{
				int32 AbsoluteStructOffset = CurrentMember.GetOffset() + MemberStack[i].StructOffset + ArrayElementId * ChildStruct->GetSize();

				for (int32 StructMemberIndex = 0; StructMemberIndex < ChildStruct->Members.Num(); StructMemberIndex++)
				{
					const FMember& StructMember = ChildStruct->Members[StructMemberIndex];
					MemberStack.Insert(FUniformBufferMemberAndOffset(*ChildStruct, StructMember, AbsoluteStructOffset), i + 1 + StructMemberIndex);
				}
			}
		}
	} // for (int32 i = 0; i < MemberStack.Num(); ++i)

	Layout.Resources.Sort([](
		const FRHIUniformBufferLayout::FResourceParameter& A,
		const FRHIUniformBufferLayout::FResourceParameter& B)
	{
#if 0 // TODO(RDG)
		/** Sort the resource on MemberType first to avoid CPU miss predictions when iterating over the resources. Then based on ascending offset
		 * to still allow O(N) complexity on offset cross referencing such as done in ClearUnusedGraphResourcesImpl().
		 */
		if (A.MemberType == B.MemberType)
		{
			return A.MemberOffset < B.MemberOffset;
		}
		return A.MemberType < B.MemberType;
#else
		// Sorts the resource based on MemberOffset to allow O(N) complexity on offset cross referencing such as done in ClearUnusedGraphResourcesImpl().
		return A.MemberOffset < B.MemberOffset;
#endif
	});

	// Compute the hash of the RHI layout.
	Layout.ComputeHash();
	
	// Compute the hash about the entire layout of the structure.
	{
		uint32 RootStructureHash = 0;
		RootStructureHash = HashCombine(RootStructureHash, GetTypeHash(int32(GetSize())));

		for (const FMember& CurrentMember : Members)
		{
			EUniformBufferBaseType BaseType = CurrentMember.GetBaseType();
			const FShaderParametersMetadata* ChildStruct = CurrentMember.GetStructMetadata();

			uint32 MemberHash = 0;
			MemberHash = HashCombine(MemberHash, GetTypeHash(int32(CurrentMember.GetOffset())));
			MemberHash = HashCombine(MemberHash, GetTypeHash(uint8(BaseType)));
			static_assert(EUniformBufferBaseType_NumBits <= 8, "Invalid EUniformBufferBaseType_NumBits");
			MemberHash = HashCombine(MemberHash, GetTypeHash(CurrentMember.GetName()));
			MemberHash = HashCombine(MemberHash, GetTypeHash(int32(CurrentMember.GetNumElements())));

			if (BaseType == UBMT_INT32 ||
				BaseType == UBMT_UINT32 ||
				BaseType == UBMT_FLOAT32)
			{
				MemberHash = HashCombine(MemberHash, GetTypeHash(uint8(CurrentMember.GetNumRows())));
				MemberHash = HashCombine(MemberHash, GetTypeHash(uint8(CurrentMember.GetNumColumns())));
			}
			else if (BaseType == UBMT_INCLUDED_STRUCT || BaseType == UBMT_NESTED_STRUCT)
			{
				if (!ChildStruct->bLayoutInitialized)
				{
					const_cast<FShaderParametersMetadata*>(ChildStruct)->InitializeLayout();
				}

				MemberHash = HashCombine(MemberHash, ChildStruct->GetLayoutHash());
			}

			RootStructureHash = HashCombine(RootStructureHash, MemberHash);
		}

		LayoutHash = RootStructureHash;
	}

	if (UseCase == EUseCase::UniformBuffer)
	{
		GLayoutHashStructMap.Emplace(Layout.GetHash(), this);
	}

	bLayoutInitialized = true;
}

void FShaderParametersMetadata::GetNestedStructs(TArray<const FShaderParametersMetadata*>& OutNestedStructs) const
{
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const FMember& CurrentMember = Members[i];

		const FShaderParametersMetadata* MemberStruct = CurrentMember.GetStructMetadata();

		if (MemberStruct)
		{
			OutNestedStructs.Add(MemberStruct);
			MemberStruct->GetNestedStructs(OutNestedStructs);
		}
	}
}

void FShaderParametersMetadata::AddResourceTableEntries(TMap<FString, FResourceTableEntry>& ResourceTableMap, TMap<FString, uint32>& ResourceTableLayoutHashes, TMap<FString, FString>& UniformBufferStaticSlots) const
{
	uint16 ResourceIndex = 0;
	FString Prefix = FString::Printf(TEXT("%s_"), ShaderVariableName);
	AddResourceTableEntriesRecursive(ShaderVariableName, *Prefix, ResourceIndex, ResourceTableMap);
	ResourceTableLayoutHashes.Add(ShaderVariableName, Layout.GetHash());

	if (StaticSlotName)
	{
		UniformBufferStaticSlots.Add(ShaderVariableName, StaticSlotName);
	}
}

void FShaderParametersMetadata::AddResourceTableEntriesRecursive(const TCHAR* UniformBufferName, const TCHAR* Prefix, uint16& ResourceIndex, TMap<FString, FResourceTableEntry>& ResourceTableMap) const
{
	for (int32 MemberIndex = 0; MemberIndex < Members.Num(); ++MemberIndex)
	{
		const FMember& Member = Members[MemberIndex];
		uint32 NumElements = Member.GetNumElements();

		if (IsShaderParameterTypeForUniformBufferLayout(Member.GetBaseType()))
		{
			FResourceTableEntry& Entry = ResourceTableMap.FindOrAdd(FString::Printf(TEXT("%s%s"), Prefix, Member.GetName()));
			if (Entry.UniformBufferName.IsEmpty())
			{
				Entry.UniformBufferName = UniformBufferName;
				Entry.Type = Member.GetBaseType();
				Entry.ResourceIndex = ResourceIndex++;
			}
		}
		else if (Member.GetBaseType() == UBMT_NESTED_STRUCT && NumElements == 0)
		{
			check(Member.GetStructMetadata());
			FString MemberPrefix = FString::Printf(TEXT("%s%s_"), Prefix, Member.GetName());
			Member.GetStructMetadata()->AddResourceTableEntriesRecursive(UniformBufferName, *MemberPrefix, ResourceIndex, ResourceTableMap);
		}
		else if (Member.GetBaseType() == UBMT_NESTED_STRUCT && NumElements > 0)
		{
			for (uint32 ArrayElementId = 0; ArrayElementId < NumElements; ArrayElementId++)
			{
				check(Member.GetStructMetadata());
				FString MemberPrefix = FString::Printf(TEXT("%s%s_%u_"), Prefix, Member.GetName(), ArrayElementId);
				Member.GetStructMetadata()->AddResourceTableEntriesRecursive(UniformBufferName, *MemberPrefix, ResourceIndex, ResourceTableMap);
			}
		}
		else if (Member.GetBaseType() == UBMT_INCLUDED_STRUCT)
		{
			check(Member.GetStructMetadata());
			check(NumElements == 0);
			Member.GetStructMetadata()->AddResourceTableEntriesRecursive(UniformBufferName, Prefix, ResourceIndex, ResourceTableMap);
		}
	}
}

void FShaderParametersMetadata::FindMemberFromOffset(uint16 MemberOffset, const FShaderParametersMetadata** OutContainingStruct, const FShaderParametersMetadata::FMember** OutMember, int32* ArrayElementId, FString* NamePrefix) const
{
	check(MemberOffset < GetSize());

	for (const FMember& Member : Members)
	{
		EUniformBufferBaseType BaseType = Member.GetBaseType();
		uint32 NumElements = Member.GetNumElements();

		if ((BaseType == UBMT_NESTED_STRUCT && NumElements == 0) || BaseType == UBMT_INCLUDED_STRUCT)
		{
			const FShaderParametersMetadata* SubStruct = Member.GetStructMetadata();
			if (MemberOffset < (Member.GetOffset() + SubStruct->GetSize()))
			{
				if (NamePrefix)
				{
					*NamePrefix = FString::Printf(TEXT("%s%s::"), **NamePrefix, Member.GetName());
				}

				return SubStruct->FindMemberFromOffset(MemberOffset - Member.GetOffset(), OutContainingStruct, OutMember, ArrayElementId, NamePrefix);
			}
		}
		else if (BaseType == UBMT_NESTED_STRUCT && NumElements > 0)
		{
			const FShaderParametersMetadata* SubStruct = Member.GetStructMetadata();
			uint32 StructSize = SubStruct->GetSize();
			
			uint16 ArrayStartOffset = Member.GetOffset();
			uint16 ArrayEndOffset = ArrayStartOffset + SubStruct->GetSize() * NumElements;
			
			if (MemberOffset >= ArrayStartOffset && MemberOffset < ArrayEndOffset)
			{
				uint32 MemberOffsetInArray = MemberOffset - ArrayStartOffset;
				check((MemberOffsetInArray % StructSize) == 0);

				uint32 MemberPosInStructArray = MemberOffsetInArray / StructSize;
				uint32 MemberOffsetInStructElement = MemberOffsetInArray - MemberPosInStructArray * StructSize;

				if (NamePrefix)
				{
					*NamePrefix = FString::Printf(TEXT("%s%s[%u]::"), **NamePrefix, Member.GetName(), MemberPosInStructArray);
				}

				return SubStruct->FindMemberFromOffset(MemberOffsetInStructElement, OutContainingStruct, OutMember, ArrayElementId, NamePrefix);
			}
		}
		else if (NumElements > 0 && (
			BaseType == UBMT_TEXTURE ||
			BaseType == UBMT_SRV ||
			BaseType == UBMT_SAMPLER ||
			IsRDGResourceReferenceShaderParameterType(BaseType)))
		{
			uint16 ArrayStartOffset = Member.GetOffset();
			uint16 ArrayEndOffset = ArrayStartOffset + SHADER_PARAMETER_POINTER_ALIGNMENT * NumElements;

			if (MemberOffset >= ArrayStartOffset && MemberOffset < ArrayEndOffset)
			{
				check((MemberOffset % SHADER_PARAMETER_POINTER_ALIGNMENT) == 0);
				*OutContainingStruct = this;
				*OutMember = &Member;
				*ArrayElementId = (MemberOffset - ArrayStartOffset) / SHADER_PARAMETER_POINTER_ALIGNMENT;
				return;
			}
		}
		else if (Member.GetOffset() == MemberOffset)
		{
			*OutContainingStruct = this;
			*OutMember = &Member;
			*ArrayElementId = 0;
			return;
		}
	}

	checkf(0, TEXT("Looks like this offset is invalid."));
}

FString FShaderParametersMetadata::GetFullMemberCodeName(uint16 MemberOffset) const
{
	const FShaderParametersMetadata* MemberContainingStruct = nullptr;
	const FShaderParametersMetadata::FMember* Member = nullptr;
	int32 ArrayElementId = 0;
	FString NamePrefix;
	FindMemberFromOffset(MemberOffset, &MemberContainingStruct, &Member, &ArrayElementId, &NamePrefix);

	FString MemberName = FString::Printf(TEXT("%s%s"), *NamePrefix, Member->GetName());
	if (Member->GetNumElements() > 0)
	{
		MemberName = FString::Printf(TEXT("%s%s[%d]"), *NamePrefix, Member->GetName(), ArrayElementId);
	}

	return MemberName;
}
