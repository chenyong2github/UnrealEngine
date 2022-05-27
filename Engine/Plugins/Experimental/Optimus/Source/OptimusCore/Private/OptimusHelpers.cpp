// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusHelpers.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ShaderParameterMetadataBuilder.h"

FName Optimus::GetUniqueNameForScope(UObject* InScopeObj, FName InName)
{
	// If there's already an object with this name, then attempt to make the name unique.
	// For some reason, MakeUniqueObjectName does not already do this check, hence this function.
	if (StaticFindObjectFast(UObject::StaticClass(), InScopeObj, InName) != nullptr)
	{
		InName = MakeUniqueObjectName(InScopeObj, UObject::StaticClass(), InName);
	}

	return InName;
}

FName Optimus::GetSanitizedNameForHlsl(FName InName)
{
	// Sanitize the name
	FString Name = InName.ToString();
	for (int32 i = 0; i < Name.Len(); ++i)
	{
		TCHAR& C = Name[i];

		const bool bGoodChar =
			FChar::IsAlpha(C) ||											// Any letter (upper and lowercase) anytime
			(C == '_') ||  													// _  
			((i > 0) && FChar::IsDigit(C));									// 0-9 after the first character

		if (!bGoodChar)
		{
			C = '_';
		}
	}

	return *Name;
}

template<typename T>
void ParametrizedAddParm(FShaderParametersMetadataBuilder& InOutBuilder, const TCHAR* InName)
{
	InOutBuilder.AddParam<T>(InName);
}

void Optimus::AddParamForType(FShaderParametersMetadataBuilder& InOutBuilder, TCHAR const* InName, FShaderValueTypeHandle const& InValueType)
{
	using AddParamFuncType = TFunction<void(FShaderParametersMetadataBuilder&, const TCHAR*)>;

	static TMap<FShaderValueType, AddParamFuncType> AddParamFuncs =
	{
		{*FShaderValueType::Get(EShaderFundamentalType::Bool), &ParametrizedAddParm<bool>},
		{*FShaderValueType::Get(EShaderFundamentalType::Int), &ParametrizedAddParm<int32>},
		{*FShaderValueType::Get(EShaderFundamentalType::Int, 2), &ParametrizedAddParm<FIntPoint>},
		{*FShaderValueType::Get(EShaderFundamentalType::Int, 3), &ParametrizedAddParm<FIntVector>},
		{*FShaderValueType::Get(EShaderFundamentalType::Int, 4), &ParametrizedAddParm<FIntVector4>},
		{*FShaderValueType::Get(EShaderFundamentalType::Uint), &ParametrizedAddParm<uint32>},
		{*FShaderValueType::Get(EShaderFundamentalType::Uint, 2), &ParametrizedAddParm<FUintVector2>},
		{*FShaderValueType::Get(EShaderFundamentalType::Uint, 4), &ParametrizedAddParm<FUintVector4>},
		{*FShaderValueType::Get(EShaderFundamentalType::Float), &ParametrizedAddParm<float>},
		{*FShaderValueType::Get(EShaderFundamentalType::Float, 2), &ParametrizedAddParm<FVector2f>},
		{*FShaderValueType::Get(EShaderFundamentalType::Float, 3), &ParametrizedAddParm<FVector3f>},
		{*FShaderValueType::Get(EShaderFundamentalType::Float, 4), &ParametrizedAddParm<FVector4f>},
		{*FShaderValueType::Get(EShaderFundamentalType::Float, 4, 4), &ParametrizedAddParm<FMatrix44f>},
	};

	if (const AddParamFuncType* Entry = AddParamFuncs.Find(*InValueType))
	{
		(*Entry)(InOutBuilder, InName);
	}
}

bool Optimus::RenameObject(UObject* InObjectToRename, const TCHAR* InNewName, UObject* InNewOuter)
{
	return InObjectToRename->Rename(InNewName, InNewOuter, REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
}

TArray<UClass*> Optimus::GetClassObjectsInPackage(UPackage* InPackage)
{
	TArray<UObject*> Objects;
	GetObjectsWithOuter(InPackage, Objects, false);

	TArray<UClass*> ClassObjects;
	for (UObject* Object : Objects)
	{
		if (UClass* Class = Cast<UClass>(Object))
		{
			ClassObjects.Add(Class);
		}
	}

	return ClassObjects;
}
