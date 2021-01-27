// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


// Forward declaration from <spirv_reflect.h>
struct SpvReflectInterfaceVariable;

// Cross compiler support/common functionality
namespace CrossCompiler
{
	class SHADERCOMPILERCOMMON_API FHlslccHeaderWriter
	{
	public:

		void WriteInputAttribute(const SpvReflectInterfaceVariable& Attribute);
		void WriteInputAttribute(const TCHAR* AttributeName, const TCHAR* TypeSpecifier, int32 Location, bool bLocationPrefix, bool bLocationSuffix);
		void WriteOutputAttribute(const SpvReflectInterfaceVariable& Attribute);
		void WriteOutputAttribute(const TCHAR* AttributeName, const TCHAR* TypeSpecifier, int32 Location, bool bLocationPrefix, bool bLocationSuffix);
		void WriteUniformBlock(const TCHAR* ResourceName, uint32 BindingIndex);
		void WritePackedGlobal(const TCHAR* ResourceName, const TCHAR* TypeSpecifier, uint32 ByteOffset, uint32 ByteSize);
		void WritePackedUB(uint32 BindingIndex);
		void WritePackedUBField(const TCHAR* ResourceName, uint32 ByteOffset, uint32 ByteSize);
		void WritePackedUBGlobalCopy(uint32 SourceCB, uint32 SourceOffset, uint32 DestCBIndex, uint32 DestCBPrecision, uint32 DestOffset, uint32 Size, bool bGroupFlattenedUBs = false);
		void WriteSRV(const TCHAR* ResourceName, uint32 BindingIndex, uint32 Count = 1);
		void WriteSRV(const TCHAR* ResourceName, uint32 BindingIndex, uint32 Count, const TArray<FString>& AssociatedResourceNames);
		void WriteUAV(const TCHAR* ResourceName, uint32 BindingIndex, uint32 Count = 1);
		void WriteSamplerState(const TCHAR* ResourceName, uint32 BindingIndex);
		void WriteNumThreads(uint32 NumThreadsX, uint32 NumThreadsY, uint32 NumThreadsZ);

		void WriteSideTable(const TCHAR* ResourceName, uint32 SideTableIndex);
		void WriteArgumentBuffers(uint32 BindingIndex, const TArray<uint32>& ResourceIndices);

		void WriteTessellationInputControlPoints(uint32 PatchSize);
		void WriteTessellationOutputControlPoints(uint32 PatchSize);
		void WriteTessellationMaxTessFactor(uint32 MaxTessFactor);

		void WriteTessellationDomainTri();
		void WriteTessellationDomainQuad();

		void WriteTessellationOutputWindingCW();
		void WriteTessellationOutputWindingCCW();

		void WriteTessellationPartitioningInteger();
		void WriteTessellationPartitioningFractionalOdd();
		void WriteTessellationPartitioningFractionalEven();

		void WriteTessellationPatchesPerThreadGroup(uint32 NumPatches);
		void WriteTessellationPatchCountBuffer(uint32 BindingIndex);
		void WriteTessellationIndexBuffer(uint32 BindingIndex);
		void WriteTessellationHSOutBuffer(uint32 BindingIndex);
		void WriteTessellationHSTFOutBuffer(uint32 BindingIndex);
		void WriteTessellationControlPointOutBuffer(uint32 BindingIndex);
		void WriteTessellationControlPointIndexBuffer(uint32 BindingIndex);

		/** Returns the finalized meta data. */
		FString ToString() const;

	private:
		void WriteIOAttribute(FString& OutMetaData, const TCHAR* AttributeName, const TCHAR* TypeSpecifier, int32 Location, bool bLocationPrefix, bool bLocationSuffix);
		void WriteIOAttribute(FString& OutMetaData, const SpvReflectInterfaceVariable& Attribute, bool bIsInput);

	private:
		struct FMetaDataStrings
		{
			FString InputAttributes;
			FString OutputAttributes;
			FString UniformBlocks;
			FString PackedGlobals;
			FString PackedUB;
			FString PackedUBFields;
			FString PackedUBGlobalCopies;
			FString SRVs; // Shader resource views (SRV) and samplers
			FString UAVs; // Unordered access views (UAV)
			FString SamplerStates;
			FString NumThreads;
			FString ExternalTextures; // External texture resources (Vulkan ES3.1 profile only)
			FString SideTable; // Side table for additional indices, e.,g. "spvBufferSizeConstants(31)" (Metal only)
			FString ArgumentBuffers; // Indirect argument buffers (Metal only)
			FString TessellationOutputControlPoints;
			FString TessellationDomain;
			FString TessellationInputControlPoints;
			FString TessellationMaxTessFactor;
			FString TessellationOutputWinding;
			FString TessellationPartitioning;
			FString TessellationPatchesPerThreadGroup;
			FString TessellationPatchCountBuffer;
			FString TessellationIndexBuffer;
			FString TessellationHSOutBuffer;
			FString TessellationControlPointOutBuffer;
			FString TessellationHSTFOutBuffer;
			FString TessellationControlPointIndexBuffer;
		};

	private:
		FMetaDataStrings Strings;
		
	};
}
