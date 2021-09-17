// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12RootSignatureDefinitions.h: D3D12 utilities for Root signatures.
=============================================================================*/

#pragma once

namespace D3D12ShaderUtils
{
	namespace StaticRootSignatureConstants
	{
		// Assume descriptors are volatile because we don't initialize all the descriptors in a table, just the ones used by the current shaders.
		const D3D12_DESCRIPTOR_RANGE_FLAGS SRVDescriptorRangeFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		const D3D12_DESCRIPTOR_RANGE_FLAGS CBVDescriptorRangeFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		const D3D12_DESCRIPTOR_RANGE_FLAGS UAVDescriptorRangeFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		const D3D12_DESCRIPTOR_RANGE_FLAGS SamplerDescriptorRangeFlags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
	}

	enum class ERootSignatureType
	{
		CBV,
		SRV,
		UAV,
		Sampler,
	};

	// Simple base class to help write out a root signature (subclass to generate either to a binary struct or a #define)
	struct FRootSignatureCreator
	{
		virtual ~FRootSignatureCreator() { }

		virtual FRootSignatureCreator& Reset() = 0;
		virtual FRootSignatureCreator& AddRootFlag(D3D12_ROOT_SIGNATURE_FLAGS Flag) = 0;
		virtual FRootSignatureCreator& AddTable(EShaderFrequency Stage, ERootSignatureType Type, int32 NumDescriptors/*, bool bAppend*/) = 0;

		inline D3D12_DESCRIPTOR_RANGE_TYPE GetD3D12DescriptorRangeType(ERootSignatureType Type)
		{
			switch (Type)
			{
			case ERootSignatureType::SRV:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			case ERootSignatureType::UAV:
				return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			case ERootSignatureType::Sampler:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			default:
				return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			}
		}

		inline D3D12_DESCRIPTOR_RANGE_FLAGS GetD3D12DescriptorRangeFlags(ERootSignatureType Type)
		{
			switch (Type)
			{
			case ERootSignatureType::SRV:
				return D3D12ShaderUtils::StaticRootSignatureConstants::SRVDescriptorRangeFlags;
			case ERootSignatureType::CBV:
				return D3D12ShaderUtils::StaticRootSignatureConstants::CBVDescriptorRangeFlags;
			case ERootSignatureType::UAV:
				return D3D12ShaderUtils::StaticRootSignatureConstants::UAVDescriptorRangeFlags;
			case ERootSignatureType::Sampler:
				return D3D12ShaderUtils::StaticRootSignatureConstants::SamplerDescriptorRangeFlags;
			default:
				return D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			}
		}

		inline D3D12_SHADER_VISIBILITY GetD3D12ShaderVisibility(EShaderFrequency Stage)
		{
			switch (Stage)
			{
			case SF_Vertex:
				return D3D12_SHADER_VISIBILITY_VERTEX;
			case SF_Pixel:
				return D3D12_SHADER_VISIBILITY_PIXEL;
			case SF_Geometry:
				return D3D12_SHADER_VISIBILITY_GEOMETRY;
			default:
				return D3D12_SHADER_VISIBILITY_ALL;
			}
		}

		inline const TCHAR* GetVisibilityFlag(EShaderFrequency Stage)
		{
			switch (Stage)
			{
			case SF_Vertex:
				return TEXT("SHADER_VISIBILITY_VERTEX");
			case SF_Geometry:
				return TEXT("SHADER_VISIBILITY_GEOMETRY");
			case SF_Pixel:
				return TEXT("SHADER_VISIBILITY_PIXEL");
			case SF_Mesh:
				return TEXT("SHADER_VISIBILITY_MESH");
			case SF_Amplification:
				return TEXT("SHADER_VISIBILITY_AMPLIFICATION");
			default:
				return TEXT("SHADER_VISIBILITY_ALL");
			}
		};

		inline const TCHAR* GetTypePrefix(ERootSignatureType Type)
		{
			switch (Type)
			{
			case ERootSignatureType::SRV:
				return TEXT("SRV(t");
			case ERootSignatureType::UAV:
				return TEXT("UAV(u");
			case ERootSignatureType::Sampler:
				return TEXT("Sampler(s");
			default:
				return TEXT("CBV(b");
			}
		}

		inline const TCHAR* GetFlagName(D3D12_ROOT_SIGNATURE_FLAGS Flag)
		{
			switch (Flag)
			{
			case D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT:
				return TEXT("ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT");
			case D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS:
				return TEXT("DENY_VERTEX_SHADER_ROOT_ACCESS");
			case D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS:
				return TEXT("DENY_GEOMETRY_SHADER_ROOT_ACCESS");
			case D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS:
				return TEXT("DENY_PIXEL_SHADER_ROOT_ACCESS");
			case D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT:
				return TEXT("ALLOW_STREAM_OUTPUT");

#if !defined(D3D12RHI_TOOLS_MESH_SHADERS_UNSUPPORTED)
			case D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS:
				return TEXT("DENY_AMPLIFICATION_SHADER_ROOT_ACCESS");
			case D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS:
				return TEXT("DENY_MESH_SHADER_ROOT_ACCESS");
#endif

			case D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED:
				return TEXT("CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED");
			case D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED:
				return TEXT("SAMPLER_HEAP_DIRECTLY_INDEXED");

			default:
				break;
			}

			return TEXT("0");
		};
	};

	// Fat/Static Gfx Root Signature
	inline void CreateGfxRootSignature(FRootSignatureCreator& Creator, bool bAllowMeshShaders)
	{
		// Ensure the creator starts in a clean state (in cases of creator reuse, etc.).
		Creator
			.Reset()
			.AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)
			.AddTable(SF_Pixel, ERootSignatureType::SRV, MAX_SRVS)
			.AddTable(SF_Pixel, ERootSignatureType::CBV, MAX_CBS)
			.AddTable(SF_Pixel, ERootSignatureType::Sampler, MAX_SAMPLERS)
			.AddTable(SF_Vertex, ERootSignatureType::SRV, MAX_SRVS)
			.AddTable(SF_Vertex, ERootSignatureType::CBV, MAX_CBS)
			.AddTable(SF_Vertex, ERootSignatureType::Sampler, MAX_SAMPLERS)
			.AddTable(SF_Geometry, ERootSignatureType::SRV, MAX_SRVS)
			.AddTable(SF_Geometry, ERootSignatureType::CBV, MAX_CBS)
			.AddTable(SF_Geometry, ERootSignatureType::Sampler, MAX_SAMPLERS);
		if (bAllowMeshShaders)
		{
			Creator
				.AddTable(SF_Mesh, ERootSignatureType::SRV, MAX_SRVS)
				.AddTable(SF_Mesh, ERootSignatureType::CBV, MAX_CBS)
				.AddTable(SF_Mesh, ERootSignatureType::Sampler, MAX_SAMPLERS)
				.AddTable(SF_Amplification, ERootSignatureType::SRV, MAX_SRVS)
				.AddTable(SF_Amplification, ERootSignatureType::CBV, MAX_CBS)
				.AddTable(SF_Amplification, ERootSignatureType::Sampler, MAX_SAMPLERS);
		}
		Creator.AddTable(SF_NumFrequencies, ERootSignatureType::UAV, MAX_UAVS);
	}

	// Fat/Static Compute Root Signature
	inline void CreateComputeRootSignature(FRootSignatureCreator& Creator)
	{
		// Ensure the creator starts in a clean state (in cases of creator reuse, etc.).
		Creator
			.Reset()
			.AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_NONE)
			.AddTable(SF_NumFrequencies, ERootSignatureType::SRV, MAX_SRVS)
			.AddTable(SF_NumFrequencies, ERootSignatureType::CBV, MAX_CBS)
			.AddTable(SF_NumFrequencies, ERootSignatureType::Sampler, MAX_SAMPLERS)
			.AddTable(SF_NumFrequencies, ERootSignatureType::UAV, MAX_UAVS);
	}

	struct FBinaryRootSignatureCreator : public FRootSignatureCreator
	{
		TArray<D3D12_DESCRIPTOR_RANGE1> DescriptorRanges;
		TArray<D3D12_ROOT_PARAMETER1> Parameters;
		TMap<uint32, uint32> ParameterToRangeMap;

		D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		FRootSignatureCreator& Reset() override
		{
			DescriptorRanges.Empty();
			Parameters.Empty();
			ParameterToRangeMap.Empty();
			Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
			return *this;
		}

		FRootSignatureCreator& AddRootFlag(D3D12_ROOT_SIGNATURE_FLAGS Flag) override
		{
			Flags |= Flag;
			return *this;
		}

		FRootSignatureCreator& AddTable(EShaderFrequency Stage, ERootSignatureType Type, int32 NumDescriptors) override
		{
			int32 ParameterIndex = Parameters.AddZeroed();
			int32 RangeIndex = DescriptorRanges.AddZeroed();

			D3D12_ROOT_PARAMETER1& Parameter = Parameters[ParameterIndex];
			D3D12_DESCRIPTOR_RANGE1& Range = DescriptorRanges[ParameterIndex];

			Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			Parameter.DescriptorTable.NumDescriptorRanges = 1;

			// Pointer will be filled in last
			//Parameter.DescriptorTable.pDescriptorRanges = &DescriptorRanges[0];
			ParameterToRangeMap.Add(ParameterIndex, RangeIndex);

			Range.RangeType = GetD3D12DescriptorRangeType(Type);
			Range.NumDescriptors = NumDescriptors;
			Range.Flags = GetD3D12DescriptorRangeFlags(Type);
			//Range.OffsetInDescriptorsFromTableStart = UINT32_MAX;
			Parameter.ShaderVisibility =  GetD3D12ShaderVisibility(Stage);

			return *this;
		}

		void Compile(EShaderFrequency Freq)
		{
			if (Freq < SF_NumGraphicsFrequencies)
			{
				bool bAllowMeshShaders = false;
#if !defined(D3D12RHI_TOOLS_MESH_SHADERS_UNSUPPORTED)
				bAllowMeshShaders = true;
#endif
				D3D12ShaderUtils::CreateGfxRootSignature(*this, bAllowMeshShaders);
			}
			else //if (Freq == SF_Compute)
			{
				D3D12ShaderUtils::CreateComputeRootSignature(*this);
			}

			// Patch pointers
			for (auto& Pair : ParameterToRangeMap)
			{
				Parameters[Pair.Key].DescriptorTable.pDescriptorRanges = &DescriptorRanges[Pair.Value];
			}
		}
	};

	/* Root signature generator for DXC */
	struct FTextRootSignatureCreator : public FRootSignatureCreator
	{
		FRootSignatureCreator& AddRootFlag(D3D12_ROOT_SIGNATURE_FLAGS InFlag) override
		{
			if (Flags.Len() > 0)
			{
				Flags += "|";
			}
			Flags += GetFlagName(InFlag);
			return *this;
		}

		FRootSignatureCreator& AddTable(EShaderFrequency InStage, ERootSignatureType Type, int32 NumDescriptors) override
		{
			FString Line = FString::Printf(TEXT("DescriptorTable(visibility=%s, %s0, numDescriptors=%d%s"/*, flags = DESCRIPTORS_VOLATILE*/"))"),
				GetVisibilityFlag(InStage), GetTypePrefix(Type), NumDescriptors,
				TEXT("")
			);
			if (Table.Len() > 0)
			{
				Table += ",";
			}
			Table += Line;
			return *this;
		}

		FRootSignatureCreator& Reset() override
		{
			Flags.Empty();
			Table.Empty();
			return *this;
		}

		FString CreateAndGenerateString(EShaderFrequency Freq)
		{
			if (Freq < SF_NumGraphicsFrequencies)
			{
				bool bAllowMeshShaders = false;
#if !defined(D3D12RHI_TOOLS_MESH_SHADERS_UNSUPPORTED)
				bAllowMeshShaders = true;
#endif
				D3D12ShaderUtils::CreateGfxRootSignature(*this, bAllowMeshShaders);
			}
			else //if (Freq == SF_Compute)
			{
				D3D12ShaderUtils::CreateComputeRootSignature(*this);
			}

			//FString String = FString::Printf(TEXT("#define TheRootSignature \"RootFlags(%s),%s\"\n[RootSignature(TheRootSignature)\n"),
			FString String = FString::Printf(TEXT("\"RootFlags(%s),%s\""),
				Flags.Len() == 0 ? TEXT("0") : *Flags,
				*Table);
			return String;
		}

		FString Flags;
		FString Table;
	};

	inline FString GenerateRootSignatureString(EShaderFrequency InFrequency)
	{
		if (InFrequency < SF_NumStandardFrequencies)
		{
			FTextRootSignatureCreator Creator;
			return Creator.CreateAndGenerateString(InFrequency);
		}

		return TEXT("");
	}
}
