// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"


#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRW"


UNiagaraDataInterfaceRWBase::UNiagaraDataInterfaceRWBase(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

bool UNiagaraDataInterfaceRWBase::Equals(const UNiagaraDataInterface* Other) const
{
	const UNiagaraDataInterfaceRWBase* OtherTyped = CastChecked<const UNiagaraDataInterfaceRWBase>(Other);

	if (OtherTyped)
	{
		return OutputShaderStages.Difference(OtherTyped->OutputShaderStages).Num() == 0 && IterationShaderStages.Difference(OtherTyped->IterationShaderStages).Num() == 0;
	}

	return false;
}

bool UNiagaraDataInterfaceRWBase::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceRWBase* OtherTyped = CastChecked<UNiagaraDataInterfaceRWBase>(Destination);

	OtherTyped->OutputShaderStages = OutputShaderStages;
	OtherTyped->IterationShaderStages = IterationShaderStages;

	return true;
}

/*--------------------------------------------------------------------------------------------------------------------------*/

UNiagaraDataInterfaceGrid3D::UNiagaraDataInterfaceGrid3D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumVoxels(3, 3, 3)
	, VoxelSize(1.)
	, SetGridFromVoxelSize(false)	
	, WorldBBoxMin(0., 0., 0.)
	, WorldBBoxSize(100., 100., 100.)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxyRW, ESPMode::ThreadSafe>();
	RWProxy = (FNiagaraDataInterfaceProxyRW*) Proxy.Get();
	PushToRenderThread();
}


void UNiagaraDataInterfaceGrid3D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumVoxelsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WorldToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToLinearFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumVoxelsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = VoxelSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("VoxelSize")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}


void UNiagaraDataInterfaceGrid3D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == NumVoxelsFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }	
	else if (BindingInfo.Name == WorldToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == UnitToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == VoxelSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

bool UNiagaraDataInterfaceGrid3D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid3D* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid3D>(Other);

	return OtherTyped->NumVoxels == NumVoxels &&
		FMath::IsNearlyEqual(OtherTyped->VoxelSize, VoxelSize) &&		
		OtherTyped->WorldBBoxMin.Equals(WorldBBoxMin) &&
		OtherTyped->WorldBBoxSize.Equals(WorldBBoxSize);
}

void UNiagaraDataInterfaceGrid3D::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(R"(
		int3 {NumVoxels};
		float3 {VoxelSize};
		float3 {WorldBBoxMinName};
		float3 {WorldBBoxSizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("NumVoxels"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("VoxelSize"), VoxelSizeName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("WorldBBoxMinName"),  WorldBBoxMinName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid3D::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	if (DefinitionFunctionName == NumVoxelsFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_NumVoxelsX, out int Out_NumVoxelsY, out int Out_NumVoxelsZ)
			{
				Out_NumVoxelsX = {NumVoxelsName}.x;
				Out_NumVoxelsY = {NumVoxelsName}.y;
				Out_NumVoxelsZ = {NumVoxelsName}.z;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumVoxelsName"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == WorldToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_World, out float3 Out_Unit)
			{
				Out_Unit = (In_World - {WorldBBoxMinName}) / {WorldBBoxSizeName};
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("WorldBBoxMinName"), WorldBBoxMinName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("WorldBBoxSizeName"), WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == UnitToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, out int Out_IndexX, out int Out_IndexY, out int Out_IndexZ)
			{
				int3 Out_IndexTmp = round(In_Unit * {NumVoxelsName} - .5);
				Out_IndexX = Out_IndexTmp.x;
				Out_IndexY = Out_IndexTmp.y;
				Out_IndexZ = Out_IndexTmp.z;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumVoxelsName"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == IndexToLinearFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, out int Out_Linear)
			{
				Out_Linear = In_IndexX + In_IndexY * {NumVoxelsName}.x + In_IndexZ * {NumVoxelsName}.x * {NumVoxelsName}.y;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumVoxelsName"), NumVoxelsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == VoxelSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_VoxelSize)
			{
				Out_VoxelSize = {VoxelSizeName};
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("VoxelSizeName"), VoxelSizeName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	return false;
}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceGrid3D::ConstructComputeParameters()const
{
	return nullptr;
}



bool UNiagaraDataInterfaceGrid3D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid3D* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid3D>(Destination);


	OtherTyped->NumVoxels = NumVoxels;
	OtherTyped->VoxelSize = VoxelSize;
	OtherTyped->SetGridFromVoxelSize = SetGridFromVoxelSize;	
	OtherTyped->WorldBBoxMin = WorldBBoxMin;
	OtherTyped->WorldBBoxSize = WorldBBoxSize;

	return true;
}

void UNiagaraDataInterfaceGrid3D::PushToRenderThread()
{


}

/*--------------------------------------------------------------------------------------------------------------------------*/

UNiagaraDataInterfaceGrid2D::UNiagaraDataInterfaceGrid2D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumCellsX(3)
	, NumCellsY(3)
	, CellSize(1.)
	, SetGridFromCellSize(false)
	, WorldBBoxMin(0., 0., 0.)
	, WorldBBoxSize(100., 100.)
{
	Proxy = MakeShared<FNiagaraDataInterfaceProxyRW, ESPMode::ThreadSafe>();
	PushToRenderThread();
}


void UNiagaraDataInterfaceGrid2D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WorldToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("World")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToUnitStaggeredXFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToUnitStaggeredYFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = IndexToLinearFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = LinearToIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumVoxelsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = CellSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("CellSize")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}


void UNiagaraDataInterfaceGrid2D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == NumCellsFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == WorldToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == UnitToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToUnitStaggeredXFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToUnitStaggeredYFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == IndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == LinearToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == CellSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }	
}

bool UNiagaraDataInterfaceGrid2D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid2D* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid2D>(Other);

	return 
		OtherTyped->NumCellsX == NumCellsX &&
		OtherTyped->NumCellsY == NumCellsY &&
		FMath::IsNearlyEqual(OtherTyped->CellSize, CellSize) &&
		OtherTyped->WorldBBoxMin.Equals(WorldBBoxMin) &&
		OtherTyped->WorldBBoxSize.Equals(WorldBBoxSize);
}

void UNiagaraDataInterfaceGrid2D::GetParameterDefinitionHLSL(FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(R"(
		int2 {NumCells};
		float2 {CellSize};
		float3 {WorldBBoxMinName};
		float2 {WorldBBoxSizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("NumCells"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSize"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("WorldBBoxMinName"),  WorldBBoxMinName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid2D::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	if (DefinitionFunctionName == NumCellsFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_NumCellsX, out int Out_NumCellsY)
			{
				Out_NumCellsX = {NumCellsName}.x;
				Out_NumCellsY = {NumCellsName}.y;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == WorldToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_World, out float2 Out_Unit)
			{
				Out_Unit = (In_World - {WorldBBoxMinName}) / float3({WorldBBoxSizeName}, 1);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("WorldBBoxMinName"), WorldBBoxMinName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("WorldBBoxSizeName"), WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == UnitToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float2 In_Unit, out int Out_IndexX, out int Out_IndexY)
			{
				int2 Out_IndexTmp = round(In_Unit * {NumCellsName}  - .5);
				Out_IndexX = Out_IndexTmp.x;
				Out_IndexY = Out_IndexTmp.y;				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},			
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == IndexToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) + .5) / {NumCellsName}, 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == IndexToUnitStaggeredXFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) + float2(0.0, 0.5)) / {NumCellsName}, 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == IndexToUnitStaggeredYFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) +  + float2(0.5, 0.0)) / {NumCellsName}, 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == IndexToLinearFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, out int Out_Linear)
			{
				Out_Linear = In_IndexX + In_IndexY * {NumCellsName}.x;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == LinearToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_Linear, out int Out_IndexX, out int Out_IndexY)
			{
				Out_IndexX = In_Linear % {NumCells}.x;
				Out_IndexY = In_Linear / {NumCells}.x;				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("NumCells"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == CellSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float2 Out_CellSize)
			{
				Out_CellSize = {CellSizeName};
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), InstanceFunctionName},
			{TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	return false;
}

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceGrid2D::ConstructComputeParameters()const
{
	return nullptr;
}



bool UNiagaraDataInterfaceGrid2D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid2D* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid2D>(Destination);


	OtherTyped->NumCellsX = NumCellsX;
	OtherTyped->NumCellsY = NumCellsY;
	OtherTyped->CellSize = CellSize;
	OtherTyped->SetGridFromCellSize = SetGridFromCellSize;
	OtherTyped->WorldBBoxMin = WorldBBoxMin;
	OtherTyped->WorldBBoxSize = WorldBBoxSize;

	return true;
}

void UNiagaraDataInterfaceGrid2D::PushToRenderThread()
{


}


#undef LOCTEXT_NAMESPACE
