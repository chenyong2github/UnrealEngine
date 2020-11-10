// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceRW.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"


#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceRW"

// Global HLSL variable base names, used by HLSL.
NIAGARA_API extern const FString NumAttributesName(TEXT("NumAttributes_"));
NIAGARA_API extern const FString NumCellsName(TEXT("NumCells_"));
NIAGARA_API extern const FString CellSizeName(TEXT("CellSize_"));
NIAGARA_API extern const FString WorldBBoxSizeName(TEXT("WorldBBoxSize_"));

// Global VM function names, also used by the shaders code generation methods.
NIAGARA_API extern const FName NumCellsFunctionName("GetNumCells");
NIAGARA_API extern const FName CellSizeFunctionName("GetCellSize");

NIAGARA_API extern const FName WorldBBoxSizeFunctionName("GetWorldBBoxSize");

NIAGARA_API extern const FName SimulationToUnitFunctionName("SimulationToUnit");
NIAGARA_API extern const FName UnitToSimulationFunctionName("UnitToSimulation");
NIAGARA_API extern const FName UnitToIndexFunctionName("UnitToIndex");
NIAGARA_API extern const FName UnitToFloatIndexFunctionName("UnitToFloatIndex");
NIAGARA_API extern const FName IndexToUnitFunctionName("IndexToUnit");
NIAGARA_API extern const FName IndexToUnitStaggeredXFunctionName("IndexToUnitStaggeredX");
NIAGARA_API extern const FName IndexToUnitStaggeredYFunctionName("IndexToUnitStaggeredY");

NIAGARA_API extern const FName IndexToLinearFunctionName("IndexToLinear");
NIAGARA_API extern const FName LinearToIndexFunctionName("LinearToIndex");

NIAGARA_API extern const FName ExecutionIndexToUnitFunctionName("ExecutionIndexToUnit");
NIAGARA_API extern const FName ExecutionIndexToGridIndexFunctionName("ExecutionIndexToGridIndex");

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
	, NumCells(3, 3, 3)
	, CellSize(1.)
	, NumCellsMaxAxis(10)
	, SetResolutionMethod(ESetResolutionMethod::Independent)	
	, WorldBBoxSize(100., 100., 100.)
{
}

void UNiagaraDataInterfaceGrid3D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WorldBBoxSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("WorldBBoxSize")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SimulationToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("SimulationToUnitTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToSimulationFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("UnitToSimulationTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));

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
		Sig.Name = UnitToFloatIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Index")));	

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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("IndexZ")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
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
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ExecutionIndexToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ExecutionIndexToGridIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NumCellsFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));		
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsY")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumCellsZ")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = CellSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("CellSize")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceGrid3D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	//if (BindingInfo.Name == WorldBBoxSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == NumCellsFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }	
	//else if (BindingInfo.Name == SimulationToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == UnitToSimulationFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == UnitToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == UnitToFloatIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == IndexToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == IndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == LinearToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == ExecutionIndexToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == ExecutionIndexToGridIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == CellSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

bool UNiagaraDataInterfaceGrid3D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid3D* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid3D>(Other);

	return OtherTyped->NumCells == NumCells &&
		FMath::IsNearlyEqual(OtherTyped->CellSize, CellSize) &&		
		OtherTyped->WorldBBoxSize.Equals(WorldBBoxSize) && 
		OtherTyped->SetResolutionMethod == SetResolutionMethod && 
		OtherTyped->NumCellsMaxAxis == NumCellsMaxAxis;
}

void UNiagaraDataInterfaceGrid3D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(R"(
		int3 {NumCellsName};
		float3 {CellSizeName};		
		float3 {WorldBBoxSizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid3D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("FunctionName"), FunctionInfo.InstanceName},
		{ TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};

	if (FunctionInfo.DefinitionName == WorldBBoxSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_WorldBBox)
			{
				Out_WorldBBox = {WorldBBoxSizeName};				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NumCellsFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_NumCellsX, out int Out_NumCellsY, out int Out_NumCellsZ)
			{
				Out_NumCellsX = {NumCellsName}.x;
				Out_NumCellsY = {NumCellsName}.y;
				Out_NumCellsZ = {NumCellsName}.z;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SimulationToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Simulation, float4x4 In_SimulationToUnitTransform, out float3 Out_Unit)
			{
				Out_Unit = mul(float4(In_Simulation, 1.0), In_SimulationToUnitTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToSimulationFunctionName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, float4x4 In_UnitToSimulationTransform, out float3 Out_Simulation)
			{
				Out_Simulation = mul(float4(In_Unit, 1.0), In_UnitToSimulationTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, out int Out_IndexX, out int Out_IndexY, out int Out_IndexZ)
			{
				int3 Out_IndexTmp = round(In_Unit * {NumCellsName} - .5);
				Out_IndexX = Out_IndexTmp.x;
				Out_IndexY = Out_IndexTmp.y;
				Out_IndexZ = Out_IndexTmp.z;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToFloatIndexFunctionName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, out float3 Out_Index)
			{
				Out_Index = In_Unit * {NumCellsName} - .5;				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitFunctionName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, float In_IndexZ, out float3 Out_Unit)
			{
				Out_Unit = (float3(In_IndexX, In_IndexY, In_IndexZ) + .5) / {NumCellsName};
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToLinearFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, out int Out_Linear)
			{
				Out_Linear = In_IndexX + In_IndexY * {NumCellsName}.x + In_IndexZ * {NumCellsName}.x * {NumCellsName}.y;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == LinearToIndexFunctionName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(int In_Linear, out int Out_IndexX, out int Out_IndexY, out int Out_IndexZ)
			{
				Out_IndexX = In_Linear % {NumCellsName}.x;
				Out_IndexY = (In_Linear / {NumCellsName}.x) % {NumCellsName}.y;
				Out_IndexZ = In_Linear / ({NumCellsName}.x * {NumCellsName}.y);
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ExecutionIndexToUnitFunctionName)
	{
	static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_Unit)
			{
				const uint Linear = GLinearThreadId;
				const uint IndexX = Linear % {NumCellsName}.x;
				const uint IndexY = (Linear / {NumCellsName}.x) % {NumCellsName}.y;
				const uint IndexZ = Linear / ({NumCellsName}.x * {NumCellsName}.y);				

				Out_Unit = (float3(IndexX, IndexY, IndexZ) + .5) / {NumCellsName};				
			}
		)");

	OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
	return true;
	}
	else if (FunctionInfo.DefinitionName == ExecutionIndexToGridIndexFunctionName)
	{
	static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_IndexX, out int Out_IndexY, out int Out_IndexZ)
			{
				const uint Linear = GLinearThreadId;
				Out_IndexX = Linear % {NumCellsName}.x;
				Out_IndexY = (Linear / {NumCellsName}.x) % {NumCellsName}.y;
				Out_IndexZ = Linear / ({NumCellsName}.x * {NumCellsName}.y);
			}
		)");

	OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
	return true;
	}
	else if (FunctionInfo.DefinitionName == CellSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_CellSize)
			{
				Out_CellSize = {CellSizeName};
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}

	return false;
}



bool UNiagaraDataInterfaceGrid3D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid3D* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid3D>(Destination);


	OtherTyped->NumCells = NumCells;
	OtherTyped->CellSize = CellSize;
	OtherTyped->SetResolutionMethod = SetResolutionMethod;
	OtherTyped->WorldBBoxSize = WorldBBoxSize;
	OtherTyped->NumCellsMaxAxis = NumCellsMaxAxis;

	return true;
}

/*--------------------------------------------------------------------------------------------------------------------------*/

UNiagaraDataInterfaceGrid2D::UNiagaraDataInterfaceGrid2D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumCellsX(3)
	, NumCellsY(3)
	, NumCellsMaxAxis(3)
	, NumAttributes(1)
	, SetGridFromMaxAxis(false)	
	, WorldBBoxSize(100., 100.)
{
}

#if WITH_EDITOR
void UNiagaraDataInterfaceGrid2D::ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors)
{
	TArray<FNiagaraFunctionSignature> DIFuncs;
	GetFunctions(DIFuncs);
	
	// All the deprecated grid2d functions
	TSet<FName> DeprecatedFunctionNames;
	DeprecatedFunctionNames.Add(WorldBBoxSizeFunctionName);
	DeprecatedFunctionNames.Add(CellSizeFunctionName);

	if (DIFuncs.Contains(Function) && DeprecatedFunctionNames.Contains(FName(Function.GetName())))
	{
		// #TODO(dmp): add validation warnings that aren't as strict as these errors
		// OutValidationErrors.Add(FText::Format(LOCTEXT("Grid2DDeprecationMsgFmt", "Grid2D DI Function {0} has been deprecated. Specify grid size on your emitter.\n"), FText::FromString(Function.GetName())));	
	}
	Super::ValidateFunction(Function, OutValidationErrors);
}

#endif

void UNiagaraDataInterfaceGrid2D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{	
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = WorldBBoxSizeFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("WorldBBoxSize")));		

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

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
		Sig.Name = SimulationToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("SimulationToUnitTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UnitToSimulationFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Unit")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("UnitToSimulationTransform")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Simulation")));

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
		Sig.Name = UnitToFloatIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Index")));		

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
		Sig.Name = ExecutionIndexToUnitFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Unit")));

		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ExecutionIndexToGridIndexFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));

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
	//if (BindingInfo.Name == WorldBBoxSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == NumCellsFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == SimulationToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == UnitToSimulationFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == UnitToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == UnitToFloatIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == IndexToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == IndexToUnitStaggeredXFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == IndexToUnitStaggeredYFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == IndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == ExecutionIndexToUnitFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == ExecutionIndexToGridIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == LinearToIndexFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	//else if (BindingInfo.Name == CellSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
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
		OtherTyped->NumAttributes == NumAttributes &&
		OtherTyped->NumCellsMaxAxis == NumCellsMaxAxis &&		
		OtherTyped->WorldBBoxSize.Equals(WorldBBoxSize);
}

void UNiagaraDataInterfaceGrid2D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	static const TCHAR *FormatDeclarations = TEXT(R"(
		int2 {NumCellsName};
		float2 {CellSizeName};		
		float2 {WorldBBoxSizeName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid2D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("FunctionName"), FunctionInfo.InstanceName},
		{ TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("CellSizeName"), CellSizeName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("WorldBBoxSizeName"),    WorldBBoxSizeName + ParamInfo.DataInterfaceHLSLSymbol },
	};

	if (FunctionInfo.DefinitionName == WorldBBoxSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float2 Out_WorldBBox)
			{
				Out_WorldBBox = {WorldBBoxSizeName};				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NumCellsFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_NumCellsX, out int Out_NumCellsY)
			{
				Out_NumCellsX = {NumCellsName}.x;
				Out_NumCellsY = {NumCellsName}.y;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SimulationToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Simulation, float4x4 In_SimulationToUnitTransform, out float3 Out_Unit)
			{
				Out_Unit = mul(float4(In_Simulation, 1.0), In_SimulationToUnitTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToSimulationFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float3 In_Unit, float4x4 In_UnitToSimulationTransform, out float3 Out_Simulation)
			{
				Out_Simulation = mul(float4(In_Unit, 1.0), In_UnitToSimulationTransform).xyz;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float2 In_Unit, out int Out_IndexX, out int Out_IndexY)
			{
				int2 Out_IndexTmp = round(In_Unit * float2({NumCellsName})  - .5);
				Out_IndexX = Out_IndexTmp.x;
				Out_IndexY = Out_IndexTmp.y;				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UnitToFloatIndexFunctionName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(float2 In_Unit, out float2 Out_Index)
			{
				Out_Index = In_Unit * float2({NumCellsName})  - .5;							
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) + .5) / float2({NumCellsName}), 0);
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitStaggeredXFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) + float2(0.0, 0.5)) / float2({NumCellsName}), 0);
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToUnitStaggeredYFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(float In_IndexX, float In_IndexY, out float3 Out_Unit)
			{
				Out_Unit = float3((float2(In_IndexX, In_IndexY) +  + float2(0.5, 0.0)) / float2({NumCellsName}), 0);
			}
		)");
		
		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == IndexToLinearFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, out int Out_Linear)
			{
				Out_Linear = In_IndexX + In_IndexY * {NumCellsName}.x;
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == LinearToIndexFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(int In_Linear, out int Out_IndexX, out int Out_IndexY)
			{
				Out_IndexX = In_Linear % {NumCellsName}.x;
				Out_IndexY = In_Linear / {NumCellsName}.x;				
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ExecutionIndexToUnitFunctionName)
	{
	static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(out float2 Out_Unit)
			{
				const uint Linear = GLinearThreadId;
				const uint IndexX = Linear % {NumCellsName}.x;
				const uint IndexY = Linear / {NumCellsName}.x;				

				Out_Unit = (float2(IndexX, IndexY) + .5) / float2({NumCellsName});			
			}
		)");

	OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
	return true;
	}
	else if (FunctionInfo.DefinitionName == ExecutionIndexToGridIndexFunctionName)
	{
	static const TCHAR* FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_IndexX, out int Out_IndexY)
			{
				const uint Linear = GLinearThreadId;
				Out_IndexX = Linear % {NumCellsName}.x;
				Out_IndexY = Linear / {NumCellsName}.x;				
			}
		)");

	OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
	return true;
	}
	else if (FunctionInfo.DefinitionName == CellSizeFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float2 Out_CellSize)
			{
				Out_CellSize = {CellSizeName};
			}
		)");

		OutHLSL += FString::Format(FormatSample, ArgsDeclarations);
		return true;
	}

	return false;
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
	OtherTyped->NumAttributes = NumAttributes;
	OtherTyped->NumCellsMaxAxis = NumCellsMaxAxis;
	OtherTyped->SetGridFromMaxAxis = SetGridFromMaxAxis;	
	OtherTyped->WorldBBoxSize = WorldBBoxSize;

	return true;
}

#undef LOCTEXT_NAMESPACE
