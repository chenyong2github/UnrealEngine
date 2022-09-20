// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDataInterfaceJiggleSpring.h"

#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeKernelPermutationVector.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OptimusDataDomain.h"
#include "OptimusDataTypeRegistry.h"
#include "ShaderParameterMetadataBuilder.h"
#include "RenderGraphBuilder.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshDeformerHelpers.h"
#include "SkeletalRenderPublic.h"

//
// FOptimusJiggleSpringParameters
//

bool 
FOptimusJiggleSpringParameters::ReadWeightsFile(
	const FFilePath& FilePath, 
	//TArray<TTuple<FString,TArray<float>>>& SectionValues) const
	TArray<FString>& SectionNames,
	TArray<TArray<float>>& SectionValues) const
{
#if WITH_EDITOR
	if (FilePath.FilePath.IsEmpty())
	{
		return false;
	}

	FPlatformFileManager& FileManager = FPlatformFileManager::Get();
	IPlatformFile& PlatformFile = FileManager.GetPlatformFile();
	if (!PlatformFile.FileExists(*FilePath.FilePath))
	{
		return false;
	}
	TSharedPtr<IFileHandle> File(PlatformFile.OpenRead(*FilePath.FilePath, false));
	if (!File.IsValid())
	{
		return false;
	}
		
	if (FilePath.FilePath.EndsWith(".bin"))
	{
		size_t NumValues = 0;
		if (!File->Read(reinterpret_cast<uint8*>(&NumValues), sizeof(size_t)))
			return false;
		TArray<float> Values;
		Values.SetNum(NumValues);

		int64 BytesRemaining = File->Size() - File->Tell();
		uint8 DataSize = BytesRemaining / NumValues;
		if (DataSize == sizeof(float))
		{
			if (!File->Read(reinterpret_cast<uint8*>(&Values[0]), NumValues * DataSize))
				return false;
		}
		else if (DataSize == sizeof(double))
		{
			TArray<double> Tmp;
			Tmp.SetNum(NumValues);
			if (!File->Read(reinterpret_cast<uint8*>(&Tmp[0]), NumValues * DataSize))
				return false;
			for (int32 i = 0; i < NumValues; i++)
			Values[i] = Tmp[i];
		}
		else
		{
			return false;
		}

		if (Values.Num() == 14185) // ryan - slrpmnstr hack!
		{
			SectionNames.SetNum(2);
			SectionValues.SetNum(2);
			SectionValues[0].SetNum(13343);
			for (int32 i = 0; i < 13343; i++)
				SectionValues[0][i] = Values[i];
			SectionValues[1].SetNum(842);
			for (int32 i = 13343, j=0; i < 14185; i++, j++)
				SectionValues[1][j] = Values[i];
		}
		else
		{
			SectionNames.Add(FilePath.FilePath);
			SectionValues.Add(MoveTemp(Values));
		}
	}
	else if (FilePath.FilePath.EndsWith(".jiggleweights"))
	{
		uint8 FileVersion = 0;
		if (!File->Read(reinterpret_cast<uint8*>(&FileVersion), sizeof(uint8)))
			return false;
		uint64 NumSections = 0;
		if (!File->Read(reinterpret_cast<uint8*>(&NumSections), sizeof(uint64)))
			return false;
		SectionNames.Reserve(NumSections);
		SectionValues.Reserve(NumSections);
		for (uint64 Section = 0; Section < NumSections; Section++)
		{
			uint64 NumChar = 0;
			if (!File->Read(reinterpret_cast<uint8*>(&NumChar), sizeof(uint64)))
				return false;
			FString Name;
			for (uint64 i = 0; i < NumChar; i++)
			{
				char Ch;
				if (!File->Read(reinterpret_cast<uint8*>(&Ch), sizeof(char)))
					return false;
				Name.AppendChar(Ch);
			}
			uint8 DataTypeSize = 0;
			if (!File->Read(reinterpret_cast<uint8*>(&DataTypeSize), sizeof(uint8)))
				return false;
			uint64 NumValues = 0;
			if (!File->Read(reinterpret_cast<uint8*>(&NumValues), sizeof(uint64)))
				return false;
			TArray<float> Values; Values.AddUninitialized(NumValues);
			if (DataTypeSize == sizeof(float))
			{
				if (!File->Read(reinterpret_cast<uint8*>(&Values[0]), DataTypeSize * NumValues))
					return false;
			}
			else if (DataTypeSize == sizeof(double))
			{
				for (uint64 i = 0; i < NumValues; i++)
				{
					double Value;
					if (!File->Read(reinterpret_cast<uint8*>(&Value), DataTypeSize))
						return false;
					Values[i] = Value;
				}
			}
			else
			{
				return false;
			}
			SectionNames.Add(Name);
			SectionValues.Add(MoveTemp(Values));
		}
	}
#endif
	return true;
}

bool
FOptimusJiggleSpringParameters::ReadWeightsFile(const FFilePath& FilePath, TArray<FVector3f>& Positions, TArray<float>& Values) const
{
#if WITH_EDITOR
	if (!FilePath.FilePath.IsEmpty())
	{
		FPlatformFileManager& FileManager = FPlatformFileManager::Get();
		IPlatformFile& PlatformFile = FileManager.GetPlatformFile();
		if (!PlatformFile.FileExists(*FilePath.FilePath))
		{
			return false;
		}
		TSharedPtr<IFileHandle> File(PlatformFile.OpenRead(*FilePath.FilePath, false));
		if (File.IsValid())
		{
			if (FilePath.FilePath.EndsWith(".bin"))
			{
				// NumValues here is NOT the number of [x, y, z, v] records, rather it is the total number of individual values.
				size_t NumValues = 0;
				if (!File->Read(reinterpret_cast<uint8*>(&NumValues), sizeof(size_t)))
					return false;
				NumValues /= 4; // Make NumValues reflect the number of [x, y, z, v] records.

				int64 BytesRemaining = File->Size() - File->Tell();
				uint8 DataSize = BytesRemaining / (NumValues * 4);

				if (DataSize == sizeof(float))
				{
					Positions.SetNum(NumValues);
					Values.SetNum(NumValues);
					for (int32 i = 0; i < NumValues; i++)
					{
						if (!File->Read(reinterpret_cast<uint8*>(&Positions[i]), 3 * DataSize))
							return false;
						if (!File->Read(reinterpret_cast<uint8*>(&Values[i]), DataSize))
							return false;
					}
				}
				else if (DataSize == sizeof(double))
				{
					Positions.SetNum(NumValues);
					Values.SetNum(NumValues);
					TArray<double> Tmp;
					Tmp.SetNum(4);
					for (int32 i = 0; i < NumValues; i++)
					{
						if (!File->Read(reinterpret_cast<uint8*>(&Tmp[0]), Tmp.Num() * DataSize))
							return false;
						Positions[i][0] = Tmp[0];
						Positions[i][1] = Tmp[1];
						Positions[i][2] = Tmp[2];
						Values[i] = Tmp[3];
					}
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
#endif
	return true;
}

//
// Interface
//

FString
UOptimusJiggleSpringDataInterface::GetDisplayName() const
{
	return TEXT("Jiggle Spring");
}

void
UOptimusJiggleSpringDataInterface::RegisterTypes()
{
	FOptimusDataTypeRegistry::Get().RegisterType(
		FName("FAnalyticSpring"),
		FText::FromString(TEXT("FAnalyticSpring")),
		FShaderValueType::Get(
			FName("FAnalyticSpring"), 
			// Initializer list for declaring members; Optimus doesn't allow empty structs, so we must have at least 1.
			{ FShaderValueType::FStructElement(FName("Dt"), FShaderValueType::Get(EShaderFundamentalType::Float)) }), 
		FName("FAnalyticSpring"),
		nullptr,
		FLinearColor(0.3f, 0.7f, 0.4f, 1.0f),
		EOptimusDataTypeUsageFlags::None);
}

TArray<FOptimusCDIPinDefinition> 
UOptimusJiggleSpringDataInterface::GetPinDefinitions() const
{
	TArray<FOptimusCDIPinDefinition> Defs;
	Defs.Add({ "AnalyticSpring", "ReadAnalyticSpring" });
	return Defs;
}

TSubclassOf<UActorComponent> 
UOptimusJiggleSpringDataInterface::GetRequiredComponentClass() const
{
	return USkinnedMeshComponent::StaticClass();
}

void 
UOptimusJiggleSpringDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	// Note: Removing these declarations breaks existing assets, even if they don't use them.

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumVertices"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadStiffness"))
		.AddReturnType(EShaderFundamentalType::Float)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadDamping"))
		.AddReturnType(EShaderFundamentalType::Float)
		.AddParam(EShaderFundamentalType::Uint);

	// DEPRECATED
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadBufferIndex"))
		.AddReturnType(EShaderFundamentalType::Uint)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadMaxStretch"))
		.AddReturnType(EShaderFundamentalType::Float)
		.AddParam(EShaderFundamentalType::Uint);

	FShaderValueTypeHandle AnalyticSpringType = FOptimusDataTypeRegistry::Get().FindType(FName("FAnalyticSpring"))->ShaderValueType;
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadAnalyticSpring"))
		.AddReturnType(AnalyticSpringType);	
}

BEGIN_SHADER_PARAMETER_STRUCT(FJiggleSpringDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, NumVertexMap)
	SHADER_PARAMETER(uint32, NumVertices)
	SHADER_PARAMETER(uint32, BaseVertexIndex)
	SHADER_PARAMETER(uint32, NumStiffnessWeights)
	SHADER_PARAMETER(uint32, NumDampingWeights)
	SHADER_PARAMETER(uint32, NumMaxStretchWeights)
	SHADER_PARAMETER(float, Stiffness)
	SHADER_PARAMETER(float, Damping)
	SHADER_PARAMETER(float, MaxStretch)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint32>, VertexMapBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, StiffnessWeightsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, DampingWeightsBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, MaxStretchWeightsBuffer)
END_SHADER_PARAMETER_STRUCT()

void 
UOptimusJiggleSpringDataInterface::GetShaderParameters(
	TCHAR const* UID, 
	FShaderParametersMetadataBuilder& InOutBuilder, 
	FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FJiggleSpringDataInterfaceParameters>(UID);
}

void 
UOptimusJiggleSpringDataInterface::GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const
{
	OutPermutationVector.AddPermutation(TEXT("ENABLE_DEFORMER_JIGGLE_SPRING"), 2);
}

void 
UOptimusJiggleSpringDataInterface::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(
		TEXT("/Plugin/Optimus/Private/DataInterfaceJiggleSpring.ush"), 
		EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void 
UOptimusJiggleSpringDataInterface::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
	};

	FString TemplateFile;
	LoadShaderSourceFile(TEXT("/Plugin/Optimus/Private/DataInterfaceJiggleSpring.ush"), EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

template <class T>
void
GetStats(const TArray<T>& Values, T& Min, double& Avg, T& Max, const bool SkipZero=false)
{
	Min = TNumericLimits<T>::Max();
	Max = -TNumericLimits<T>::Max();
	Avg = 0.0;
	for (int32 i = 0; i < Values.Num(); i++)
	{
		if (SkipZero && Values[i] == 0.0) continue;
		if (Values[i] < Min) Min = Values[i];
		if (Values[i] > Max) Max = Values[i];
		Avg += Values[i] / Values.Num();
	}
}

UComputeDataProvider*
UOptimusJiggleSpringDataInterface::CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const
{
	// We're missing a transport mechanism that'll feed us TArray<float> via Blueprints, 
	// so we need to read these files here for now.
	// 
	// Read files if weights are empty.
	UOptimusJiggleSpringDataInterface* NCThis = const_cast<UOptimusJiggleSpringDataInterface*>(this);
	if (!JiggleSpringParameters.StiffnessWeights.Num() && !JiggleSpringParameters.StiffnessWeightsFile.FilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("UOptimusJiggleSpringDataInterface::CreateDataProvider() - Reading stiffness file: '%s'"), *JiggleSpringParameters.StiffnessWeightsFile.FilePath);
		if (!JiggleSpringParameters.ReadWeightsFile(JiggleSpringParameters.StiffnessWeightsFile, NCThis->JiggleSpringParameters.StiffnessWeightsNames, NCThis->JiggleSpringParameters.StiffnessWeights))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read stiffness file: '%s'"), *JiggleSpringParameters.StiffnessWeightsFile.FilePath);
		}
		//float Min, Max; double Avg;
		//GetStats(JiggleSpringParameters.StiffnessWeights, Min, Avg, Max, true);
		//UE_LOG(LogTemp, Log, TEXT("UOptimusJiggleSpringDataInterface::CreateDataProvider() - Stiffness stats (min, avg, max): %g, %g, %g"), Min, Avg, Max);
	}
	if (!JiggleSpringParameters.DampingWeights.Num() && !JiggleSpringParameters.DampingWeightsFile.FilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("UOptimusJiggleSpringDataInterface::CreateDataProvider() - Reading damping file: '%s'"), *JiggleSpringParameters.DampingWeightsFile.FilePath);
		if (!JiggleSpringParameters.ReadWeightsFile(JiggleSpringParameters.DampingWeightsFile, NCThis->JiggleSpringParameters.DampingWeightsNames, NCThis->JiggleSpringParameters.DampingWeights))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to read damping file: '%s'"), *JiggleSpringParameters.DampingWeightsFile.FilePath);
		}
		//float Min, Max; double Avg;
		//GetStats(JiggleSpringParameters.DampingWeights, Min, Avg, Max, true);
		//UE_LOG(LogTemp, Log, TEXT("UOptimusJiggleSpringDataInterface::CreateDataProvider() - Stiffness stats (min, avg, max): %g, %g, %g"), Min, Avg, Max);
	}

	// Copy JiggleSpringParameters for data provider.
	UOptimusJiggleSpringDataProvider* Provider = NewObject<UOptimusJiggleSpringDataProvider>();
	Provider->SkinnedMesh = Cast<USkinnedMeshComponent>(InBinding);
	Provider->JiggleSpringParameters = JiggleSpringParameters;

	if (JiggleSpringParameters.StiffnessWeights.Num() || JiggleSpringParameters.DampingWeights.Num())
	{
#if WITH_EDITORONLY_DATA
		// TODO Ryan - Topology is only available with the editor.  Crazy talk, I know...  Save what we need in parameters?
		const USkeletalMesh* SkeletalMesh = Provider->SkinnedMesh ? Cast<USkeletalMesh>(Provider->SkinnedMesh->GetSkinnedAsset()) : nullptr;
		if (const FSkeletalMeshModel* ImportedModel = SkeletalMesh ? SkeletalMesh->GetImportedModel() : nullptr)
		{
			// We (may) need to reorder stiffness & damping values from the import geometry to the render geometry.
			// Send along the map that does that translation to the shader.
			Provider->JiggleSpringParameters.VertexMap.SetNum(ImportedModel->LODModels[0].MeshToImportVertexMap.Num());
			for (int32 i = 0; i < ImportedModel->LODModels[0].MeshToImportVertexMap.Num(); i++)
			{
				Provider->JiggleSpringParameters.VertexMap[i] = static_cast<uint32>(ImportedModel->LODModels[0].MeshToImportVertexMap[i]);
			}

			if (JiggleSpringParameters.bUseAvgEdgeLengthForMaxStretchMap)
			{
				// Pry the mesh topology out of the LOD model.
				TArray<FIntVector3> Triangles;
				for (int32 i = 0; i < ImportedModel->LODModels[0].Sections.Num(); i++)
				{
					const auto& Section = ImportedModel->LODModels[0].Sections[i];
					Triangles.Reserve(Triangles.Num() + Section.NumTriangles);
					for (uint32 j = 0; j < Section.NumTriangles; j++)
					{
						FIntVector3 Tri;
						for (int32 k = 0; k < 3; k++)
						{
							Tri[k] = ImportedModel->LODModels[0].IndexBuffer[Section.BaseIndex + ((j * 3 + k))];
						}
						Triangles.Add(Tri);
					}
				}

				// Get the set of unique edges (don't care about winding order).
				TSet<TPair<int32, int32>> Edges;
				for (int i = 0; i < Triangles.Num(); i++)
				{
					const FIntVector3& Tri = Triangles[i];
					for (int j = 0; j < 3; j++)
					{
						int32 a = Tri[j];
						int32 b = Tri[(j + 1) % 3];
						Edges.Add(TPair<int32, int32>(FMath::Min(a, b), FMath::Max(a, b)));
					}
				}

				// Compute the per vertex average edge length.
				TArray<float> AvgEdgeLength; AvgEdgeLength.SetNum(ImportedModel->LODModels[0].MeshToImportVertexMap.Num());
				TArray<int32> NumEdges; NumEdges.SetNum(ImportedModel->LODModels[0].MeshToImportVertexMap.Num());
				TArray<FSoftSkinVertex> Vertices;
				ImportedModel->LODModels[0].GetVertices(Vertices);
				for (auto it = Edges.CreateConstIterator(); !!it; ++it)
				{
					const TPair<int32, int32>& Edge = *it;
					float EdgeLength = (Vertices[Edge.Key].Position - Vertices[Edge.Value].Position).Length();
					AvgEdgeLength[Edge.Key] += EdgeLength;
					AvgEdgeLength[Edge.Value] += EdgeLength;
					NumEdges[Edge.Key]++;
					NumEdges[Edge.Value]++;
				}
				for (int32 i = 0; i < NumEdges.Num(); i++)
					if (NumEdges[i] > 0)
						AvgEdgeLength[i] /= NumEdges[i];

				Provider->JiggleSpringParameters.MaxStretchWeights = AvgEdgeLength;
			}
		}
		else
		{
			// Despair?
		}
#endif
	}
	return Provider;
}

//
// DataProvider
//

bool 
UOptimusJiggleSpringDataProvider::IsValid() const
{
	return 
		SkinnedMesh != nullptr &&
		SkinnedMesh->MeshObject != nullptr;
}

FComputeDataProviderRenderProxy* 
UOptimusJiggleSpringDataProvider::GetRenderProxy()
{
	return new FOptimusJiggleSpringDataProviderProxy(SkinnedMesh, JiggleSpringParameters);
}

//
// Proxy
//

FOptimusJiggleSpringDataProviderProxy::FOptimusJiggleSpringDataProviderProxy(
	USkinnedMeshComponent* SkinnedMeshComponent, 
	FOptimusJiggleSpringParameters const& InJiggleSpringParameters)
	: SkinnedMeshComponent(SkinnedMeshComponent)
{

	SkeletalMeshObject = SkinnedMeshComponent->MeshObject;
	JiggleSpringParameters = InJiggleSpringParameters;
}

void
FOptimusJiggleSpringDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder)
{
	// Find the total number of vertices for this skeletal mesh
	uint32 TotalNumVertices = 0;
	const int32 LodIndex = SkeletalMeshObject->GetLOD();
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = &SkeletalMeshRenderData.LODRenderData[LodIndex];
	for (int32 i = 0; i < LodRenderData->RenderSections.Num(); i++)
	{
		TotalNumVertices += LodRenderData->RenderSections[i].GetNumVertices();
	}

	// Find the material property index that matches this skeletal mesh's name or num vertices.
	SectionIndex = INDEX_NONE;
	for (int32 i = 0; i < JiggleSpringParameters.StiffnessWeights.Num(); i++)
	{
		const FString& Name = JiggleSpringParameters.StiffnessWeightsNames.Num() > i ? JiggleSpringParameters.StiffnessWeightsNames[i] : FString();
		if (SkeletalMeshObject->GetDebugName().ToString() == Name ||
			TotalNumVertices == JiggleSpringParameters.StiffnessWeights[i].Num())
		{
			SectionIndex = i;
		}
	}

	// VertexMapBuffer - maps from render vertex index to import vertex index.
	// We only need this if the material property files were solved for on the import geometry,
	// which likely differs from the render geometry.
	if (SectionIndex != INDEX_NONE &&
		JiggleSpringParameters.VertexMap.Num() &&
		(JiggleSpringParameters.StiffnessWeights[SectionIndex].Num() != TotalNumVertices ||
		 JiggleSpringParameters.DampingWeights[SectionIndex].Num() != TotalNumVertices))
	{
		VertexMapBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), JiggleSpringParameters.VertexMap.Num()),
				TEXT("JiggleSpring.VertexMap"));
		VertexMapBufferSRV =
			GraphBuilder.CreateSRV(VertexMapBuffer);
		GraphBuilder.QueueBufferUpload(
			VertexMapBuffer,
			JiggleSpringParameters.VertexMap.GetData(),
			JiggleSpringParameters.VertexMap.Num() * sizeof(uint32),
			ERDGInitialDataFlags::None);
	}
	else
	{
		VertexMapBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1),
				TEXT("JiggleSpring.VertexMap"));
		VertexMapBufferSRV =
			GraphBuilder.CreateSRV(VertexMapBuffer);
		GraphBuilder.QueueBufferUpload(
			VertexMapBuffer,
			&NullIntBuffer,
			1 * sizeof(uint32),
			ERDGInitialDataFlags::None);
	}
	// Stiffness weights
	if (SectionIndex != INDEX_NONE &&
		JiggleSpringParameters.StiffnessWeights[SectionIndex].Num())
	{
		StiffnessWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), JiggleSpringParameters.StiffnessWeights[SectionIndex].Num()),
				TEXT("JiggleSpring.StiffnessWeights"));
		StiffnessWeightsBufferSRV =
			GraphBuilder.CreateSRV(StiffnessWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			StiffnessWeightsBuffer,
			JiggleSpringParameters.StiffnessWeights[SectionIndex].GetData(),
			JiggleSpringParameters.StiffnessWeights[SectionIndex].Num() * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	else
	{
		StiffnessWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 1),
				TEXT("JiggleSpring.StiffnessWeights"));
		StiffnessWeightsBufferSRV =
			GraphBuilder.CreateSRV(StiffnessWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			StiffnessWeightsBuffer,
			&NullFloatBuffer,
			1 * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	// Damping weights
	if (SectionIndex != INDEX_NONE &&
		JiggleSpringParameters.DampingWeights[SectionIndex].Num())
	{
		DampingWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), JiggleSpringParameters.DampingWeights[SectionIndex].Num()),
				TEXT("JiggleSpring.DampingWeights"));
		DampingWeightsBufferSRV =
			GraphBuilder.CreateSRV(DampingWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			DampingWeightsBuffer,
			JiggleSpringParameters.DampingWeights[SectionIndex].GetData(),
			JiggleSpringParameters.DampingWeights[SectionIndex].Num() * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	else
	{
		DampingWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 1),
				TEXT("JiggleSpring.DampingWeights"));
		DampingWeightsBufferSRV =
			GraphBuilder.CreateSRV(DampingWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			DampingWeightsBuffer,
			&NullFloatBuffer,
			1 * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	// Max stretch weights
	if (JiggleSpringParameters.MaxStretchWeights.Num())
	{
		MaxStretchWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), JiggleSpringParameters.MaxStretchWeights.Num()),
				TEXT("JiggleSpring.MaxStretchWeights"));
		MaxStretchWeightsBufferSRV =
			GraphBuilder.CreateSRV(MaxStretchWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			MaxStretchWeightsBuffer,
			JiggleSpringParameters.MaxStretchWeights.GetData(),
			JiggleSpringParameters.MaxStretchWeights.Num() * sizeof(float),
			ERDGInitialDataFlags::None);
	}
	else
	{
		MaxStretchWeightsBuffer =
			GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 1),
				TEXT("JiggleSpring.MaxStretchWeights"));
		MaxStretchWeightsBufferSRV =
			GraphBuilder.CreateSRV(MaxStretchWeightsBuffer);
		GraphBuilder.QueueBufferUpload(
			MaxStretchWeightsBuffer,
			&NullFloatBuffer,
			1 * sizeof(float),
			ERDGInitialDataFlags::None);
	}
}

struct FJiggleSpringDataInterfacePermutationIds
{
	uint32 EnableDeformerJiggleSpring = 0;

	FJiggleSpringDataInterfacePermutationIds(FComputeKernelPermutationVector const& PermutationVector)
	{
		{
			static FString Name(TEXT("ENABLE_DEFORMER_JIGGLE_SPRING"));
			static uint32 Hash = GetTypeHash(Name);
			EnableDeformerJiggleSpring = PermutationVector.GetPermutationBits(Name, Hash, 1);
		}
	}
};

void FOptimusJiggleSpringDataProviderProxy::GatherDispatchData(FDispatchSetup const& InDispatchSetup, FCollectedDispatchData& InOutDispatchData)
{
	if (!ensure(InDispatchSetup.ParameterStructSizeForValidation == sizeof(FJiggleSpringDataInterfaceParameters)))
	{
		return;
	}

	const int32 LodIndex = SkeletalMeshObject->GetLOD();
	FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshObject->GetSkeletalMeshRenderData();
	FSkeletalMeshLODRenderData const* LodRenderData = &SkeletalMeshRenderData.LODRenderData[LodIndex];
	if (!ensure(LodRenderData->RenderSections.Num() == InDispatchSetup.NumInvocations))
	{
		return;
	}

	FJiggleSpringDataInterfacePermutationIds PermutationIds(InDispatchSetup.PermutationVector);

	for (int32 InvocationIndex = 0; InvocationIndex < InDispatchSetup.NumInvocations; ++InvocationIndex)
	{
		FSkelMeshRenderSection const& RenderSection = LodRenderData->RenderSections[InvocationIndex];

		FJiggleSpringDataInterfaceParameters* Parameters = 
			(FJiggleSpringDataInterfaceParameters*)(InOutDispatchData.ParameterBuffer + InDispatchSetup.ParameterBufferOffset + InDispatchSetup.ParameterBufferStride * InvocationIndex);
		Parameters->NumVertices = RenderSection.GetNumVertices();
		Parameters->BaseVertexIndex = RenderSection.BaseVertexIndex;
		Parameters->NumVertexMap = JiggleSpringParameters.VertexMap.Num();

		Parameters->NumStiffnessWeights = SectionIndex != INDEX_NONE ? JiggleSpringParameters.StiffnessWeights[SectionIndex].Num() : 0;
		Parameters->NumDampingWeights = SectionIndex != INDEX_NONE ? JiggleSpringParameters.DampingWeights[SectionIndex].Num() : 0;

		Parameters->VertexMapBuffer = VertexMapBufferSRV;
		Parameters->StiffnessWeightsBuffer = StiffnessWeightsBufferSRV;
		Parameters->DampingWeightsBuffer = DampingWeightsBufferSRV;

		Parameters->Stiffness = JiggleSpringParameters.BaselineStiffness;
		Parameters->Damping = JiggleSpringParameters.BaselineDamping;

		Parameters->MaxStretch = JiggleSpringParameters.MaxStretchMultiplier;
		Parameters->MaxStretchWeightsBuffer = MaxStretchWeightsBufferSRV;

		InOutDispatchData.PermutationId[InvocationIndex] |= PermutationIds.EnableDeformerJiggleSpring;
	}
}
