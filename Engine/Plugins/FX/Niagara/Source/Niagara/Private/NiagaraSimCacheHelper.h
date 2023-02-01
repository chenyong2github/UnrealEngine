// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "NiagaraClearCounts.h"
#include "NiagaraComponent.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraConstants.h"
#include "NiagaraDataSetReadback.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraSimCache.h"
#include "NiagaraSystemInstanceController.h"

struct FNiagaraSimCacheHelper
{
	explicit FNiagaraSimCacheHelper(FNiagaraSystemInstance* InSystemInstance)
	{
		SystemInstance = InSystemInstance;
		SystemSimulation = SystemInstance->GetSystemSimulation();
		check(SystemSimulation);
		SystemSimulationDataBuffer = SystemSimulation->MainDataSet.GetCurrentData();
		NiagaraSystem = SystemSimulation->GetSystem();
	}

	explicit FNiagaraSimCacheHelper(UNiagaraComponent* NiagaraComponent)
	{
		if ( NiagaraComponent == nullptr )
		{
			return;
		}

		NiagaraSystem = NiagaraComponent->GetAsset();
		if ( NiagaraSystem == nullptr )
		{
			return;
		}

		FNiagaraSystemInstanceControllerPtr SystemInstanceController = NiagaraComponent->GetSystemInstanceController();
		if (SystemInstanceController.IsValid() == false)
		{
			return;
		}

		SystemInstance = SystemInstanceController->GetSystemInstance_Unsafe();
		if (SystemInstance == nullptr)
		{
			return;
		}

		SystemSimulation = SystemInstance->GetSystemSimulation();
		if (SystemSimulation == nullptr)
		{
			return;
		}

		SystemSimulationDataBuffer = SystemSimulation->MainDataSet.GetCurrentData();
		if (SystemSimulationDataBuffer == nullptr)
		{
			return;
		}
	}

	FNiagaraDataSet& GetSystemSimulationDataSet() { return SystemSimulation->MainDataSet; }

	bool HasValidSimulation() const { return SystemSimulation != nullptr; }
	bool HasValidSimulationData() const { return SystemSimulationDataBuffer != nullptr; }

	void BuildCacheLayout(const FNiagaraSimCacheCreateParameters& CreateParameters, FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraDataSetCompiledData& CompiledData, FName LayoutName, TArray<FName> InRebaseVariableNames, TArray<FName> InInterpVariableNames, TConstArrayView<FName> ExplicitCaptureAttributes) const
	{
		CacheLayout.LayoutName = LayoutName;
		CacheLayout.SimTarget = CompiledData.SimTarget;

		// Determine the components to cache
		int32 TotalCacheComponents = 0;
		TArray<int32> CacheToDataSetVariables;

		CacheToDataSetVariables.Reserve(CompiledData.Variables.Num());
		for (int32 i = 0; i < CompiledData.Variables.Num(); ++i)
		{
			const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[i];
			const FNiagaraVariable& DataSetVariable = CompiledData.Variables[i];
			if (ExplicitCaptureAttributes.Num() == 0 || ExplicitCaptureAttributes.Contains(DataSetVariable.GetName()))
			{
				CacheToDataSetVariables.Add(i);
				TotalCacheComponents += DataSetVariableLayout.GetNumFloatComponents() + DataSetVariableLayout.GetNumHalfComponents() + DataSetVariableLayout.GetNumInt32Components();
			}
		}

		// We need to preserve the velocity attribute if we want to use velocity based extrapolation of positions
		if (CreateParameters.bAllowVelocityExtrapolation)
		{
			const FNiagaraVariableBase VelocityVariable(FNiagaraTypeDefinition::GetVec3Def(), "Velocity");
			const int32 VelocityAttributeIndex = CompiledData.Variables.IndexOfByKey(VelocityVariable);
			if (VelocityAttributeIndex != INDEX_NONE)
			{
				CacheLayout.bAllowVelocityExtrapolation = true;
				if (!CacheToDataSetVariables.Contains(VelocityAttributeIndex))
				{
					CacheToDataSetVariables.AddUnique(VelocityAttributeIndex);
					const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[VelocityAttributeIndex];
					TotalCacheComponents += DataSetVariableLayout.GetNumFloatComponents() + DataSetVariableLayout.GetNumHalfComponents() + DataSetVariableLayout.GetNumInt32Components();
				}
			}
		}

		const int32 NumCacheVariables = CacheToDataSetVariables.Num();

		CacheLayout.Variables.AddDefaulted(NumCacheVariables);

		CacheLayout.ComponentMappingsFromDataBuffer.Empty(TotalCacheComponents);
		CacheLayout.ComponentMappingsFromDataBuffer.AddDefaulted(TotalCacheComponents);
		CacheLayout.RebaseVariableNames = MoveTemp(InRebaseVariableNames);
		CacheLayout.InterpVariableNames = MoveTemp(InInterpVariableNames);

		for ( int32 iCacheVariable=0; iCacheVariable < NumCacheVariables; ++iCacheVariable)
		{
			const int32 iDataSetVariable = CacheToDataSetVariables[iCacheVariable];
			const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[iDataSetVariable];
			FNiagaraSimCacheVariable& CacheVariable = CacheLayout.Variables[iCacheVariable];

			CacheVariable.Variable = CompiledData.Variables[iDataSetVariable];
			CacheVariable.FloatOffset = DataSetVariableLayout.GetNumFloatComponents() > 0 ? CacheLayout.FloatCount : INDEX_NONE;
			CacheVariable.FloatCount = uint16(DataSetVariableLayout.GetNumFloatComponents());
			CacheVariable.HalfOffset = DataSetVariableLayout.GetNumHalfComponents() > 0 ? CacheLayout.HalfCount : INDEX_NONE;
			CacheVariable.HalfCount = uint16(DataSetVariableLayout.GetNumHalfComponents());
			CacheVariable.Int32Offset = DataSetVariableLayout.GetNumInt32Components() > 0 ? CacheLayout.Int32Count : INDEX_NONE;
			CacheVariable.Int32Count = uint16(DataSetVariableLayout.GetNumInt32Components());

			CacheLayout.FloatCount += uint16(DataSetVariableLayout.GetNumFloatComponents());
			CacheLayout.HalfCount += uint16(DataSetVariableLayout.GetNumHalfComponents());
			CacheLayout.Int32Count += uint16(DataSetVariableLayout.GetNumInt32Components());
		}

		if (CreateParameters.bAllowInterpolation)
		{
			const FNiagaraVariableBase UniqueIDVariable(FNiagaraTypeDefinition::GetIntDef(), "UniqueID");
			CacheLayout.ComponentUniqueID = CompiledData.Variables.IndexOfByKey(UniqueIDVariable);
			if (CacheLayout.ComponentUniqueID != uint16(INDEX_NONE))
			{
				const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[CacheLayout.ComponentUniqueID];
				check(DataSetVariableLayout.GetNumInt32Components() == 1);
				CacheLayout.ComponentUniqueID = DataSetVariableLayout.Int32ComponentStart;
				CacheLayout.bAllowInterpolation = true;
			}
		}

		// Build write mappings we will build read mappings in a separate path
		int32 FloatOffset = 0;
		int32 HalfOffset = CacheLayout.FloatCount;
		int32 Int32Offset = HalfOffset + CacheLayout.HalfCount;
		for (int32 iCacheVariable = 0; iCacheVariable < NumCacheVariables; ++iCacheVariable)
		{
			const int32 iDataSetVariable = CacheToDataSetVariables[iCacheVariable];
			const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[iDataSetVariable];
			FNiagaraSimCacheVariable& CacheVariable = CacheLayout.Variables[iCacheVariable];

			for (int32 iComponent=0; iComponent < CacheVariable.FloatCount; ++iComponent)
			{
				CacheLayout.ComponentMappingsFromDataBuffer[FloatOffset] = uint16(DataSetVariableLayout.FloatComponentStart + iComponent);
				++FloatOffset;
			}

			for (int32 iComponent=0; iComponent < CacheVariable.HalfCount; ++iComponent)
			{
				CacheLayout.ComponentMappingsFromDataBuffer[HalfOffset] = uint16(DataSetVariableLayout.HalfComponentStart + iComponent);
				++HalfOffset;
			}

			for (int32 iComponent=0; iComponent < CacheVariable.Int32Count; ++iComponent)
			{
				CacheLayout.ComponentMappingsFromDataBuffer[Int32Offset] = uint16(DataSetVariableLayout.Int32ComponentStart + iComponent);
				++Int32Offset;
			}
		}

		// Slightly inefficient but we can share the code between the paths
		BuildCacheReadMappings(CacheLayout, CompiledData);
	}

	void BuildCacheLayoutForSystem(const FNiagaraSimCacheCreateParameters& CreateParameters, FNiagaraSimCacheDataBuffersLayout& CacheLayout)
	{
		const FNiagaraDataSetCompiledData& SystemCompileData = NiagaraSystem->GetSystemCompiledData().DataSetCompiledData;

		TArray<FName> RebaseVariableNames;
		if ( CreateParameters.bAllowRebasing )
		{
			TArray<FString, TInlineAllocator<8>> LocalSpaceEmitters;
			for ( int32 i=0; i < NiagaraSystem->GetNumEmitters(); ++i )
			{
				const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(i);
				if (EmitterHandle.GetIsEnabled() )
				{
					const FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetInstance().GetEmitterData();
					if (EmitterData && EmitterData->bLocalSpace)
					{
						LocalSpaceEmitters.Add(EmitterHandle.GetUniqueInstanceName());
					}
				}
			}

			for (const FNiagaraVariableBase& Variable : SystemCompileData.Variables)
			{
				if (Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef())
				{
					// If this is an emitter variable we need to check if it's local space or not
					bool bIsLocalSpace = false;
					for ( const FString& LocalSpaceEmitter : LocalSpaceEmitters )
					{
						if ( Variable.IsInNameSpace(LocalSpaceEmitter) )
						{
							bIsLocalSpace = true;
							break;
						}
					}

					if ( bIsLocalSpace == false && CreateParameters.RebaseExcludeAttributes.Contains(Variable.GetName()) == false )
					{
						RebaseVariableNames.AddUnique(Variable.GetName());
					}
				}
				else if ( CanRebaseVariable(Variable) && CreateParameters.RebaseIncludeAttributes.Contains(Variable.GetName()) )
				{
					RebaseVariableNames.AddUnique(Variable.GetName());
				}
			}
		}

		TArray<FName> InterpVariableNames;
		if (CreateParameters.bAllowInterpolation)
		{
			for (const FNiagaraVariableBase& Variable : SystemCompileData.Variables)
			{
				if (!CanInterpolateVariable(Variable) || CreateParameters.RebaseExcludeAttributes.Contains(Variable.GetName()))
				{
					continue;
				}

				if (CreateParameters.InterpolationIncludeAttributes.Num() == 0 || CreateParameters.InterpolationIncludeAttributes.Contains(Variable.GetName()))
				{
					InterpVariableNames.Add(Variable.GetName());
				}
			}
		}

		BuildCacheLayout(CreateParameters, CacheLayout, SystemCompileData, NiagaraSystem->GetFName(), MoveTemp(RebaseVariableNames), MoveTemp(InterpVariableNames), CreateParameters.ExplicitCaptureAttributes);
	}

	void BuildCacheLayoutForEmitter(const FNiagaraSimCacheCreateParameters& CreateParameters, FNiagaraSimCacheDataBuffersLayout& CacheLayout, int EmitterIndex)
	{
		const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(EmitterIndex);
		const FNiagaraEmitterCompiledData& EmitterCompiledData = NiagaraSystem->GetEmitterCompiledData()[EmitterIndex].Get();
		const FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetInstance().GetEmitterData();
		if (EmitterHandle.GetIsEnabled() == false || EmitterData == nullptr)
		{
			return;
		}

		// Find potential candidates for re-basing
		CacheLayout.bLocalSpace = EmitterData->bLocalSpace;

		TArray<FName> RebaseVariableNames;
		if ( CreateParameters.bAllowRebasing && CacheLayout.bLocalSpace == false )
		{
			// Build list of include / exclude names
			TArray<FName> ForceIncludeNames;
			TArray<FName> ForceExcludeNames;
			if ( CreateParameters.RebaseIncludeAttributes.Num() > 0 || CreateParameters.RebaseExcludeAttributes.Num() > 0 )
			{
				const FString EmitterName = EmitterHandle.GetUniqueInstanceName();
				for (FName RebaseName : CreateParameters.RebaseIncludeAttributes)
				{
					FNiagaraVariableBase BaseVar(FNiagaraTypeDefinition::GetFloatDef(), RebaseName);
					if (BaseVar.RemoveRootNamespace(EmitterName))
					{
						ForceIncludeNames.Add(BaseVar.GetName());
					}
				}

				for (FName RebaseName : CreateParameters.RebaseExcludeAttributes)
				{
					FNiagaraVariableBase BaseVar(FNiagaraTypeDefinition::GetFloatDef(), RebaseName);
					if (BaseVar.RemoveRootNamespace(EmitterName))
					{
						ForceExcludeNames.Add(BaseVar.GetName());
					}
				}
			}

		#if WITH_EDITORONLY_DATA
			// Look for renderer attributes bound to Quat / Matrix types are we will want to rebase those
			// We will add all Position types after this so no need to add them here
			EmitterData->ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* RenderProperties)
				{
					for (FNiagaraVariable BoundAttribute : RenderProperties->GetBoundAttributes())
					{
						if ( (BoundAttribute.GetType() == FNiagaraTypeDefinition::GetQuatDef()) ||
							 (BoundAttribute.GetType() == FNiagaraTypeDefinition::GetMatrix4Def()) )
						{
							if (BoundAttribute.RemoveRootNamespace(FNiagaraConstants::ParticleAttributeNamespaceString))
							{
								if (EmitterCompiledData.DataSetCompiledData.Variables.Contains(BoundAttribute) && ForceExcludeNames.Contains(BoundAttribute.GetName()) == false )
								{
									RebaseVariableNames.AddUnique(BoundAttribute.GetName());
								}
							}
						}
					}
				}
			);
		#endif

			// Look for regular attributes that we are forcing to rebase or can rebase like positions
			for (const FNiagaraVariableBase& Variable : EmitterCompiledData.DataSetCompiledData.Variables)
			{
				if ( Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef() )
				{
					if ( ForceExcludeNames.Contains(Variable.GetName()) == false )
					{
						RebaseVariableNames.AddUnique(Variable.GetName());
					}
				}
				else if ( ForceIncludeNames.Contains(Variable.GetName()) && CanRebaseVariable(Variable) )
				{
					RebaseVariableNames.AddUnique(Variable.GetName());
				}
			}
		}

		TArray<FName> InterpVariableNames;
		if (CreateParameters.bAllowInterpolation)
		{
			for (const FNiagaraVariableBase& Variable : EmitterCompiledData.DataSetCompiledData.Variables)
			{
				if (!CanInterpolateVariable(Variable) || CreateParameters.RebaseExcludeAttributes.Contains(Variable.GetName()))
				{
					continue;
				}

				if (CreateParameters.InterpolationIncludeAttributes.Num() == 0 || CreateParameters.InterpolationIncludeAttributes.Contains(Variable.GetName()))
				{
					InterpVariableNames.Add(Variable.GetName());
				}
			}
		}

		TArray<FName> ExplicitCaptureAttributes;
		if ( CreateParameters.ExplicitCaptureAttributes.Num() > 0 )
		{
			const FString EmitterName = EmitterHandle.GetUniqueInstanceName();
			for ( FName AttributeName : CreateParameters.ExplicitCaptureAttributes)
			{
				FNiagaraVariableBase AttributeVar(FNiagaraTypeDefinition::GetFloatDef(), AttributeName);
				if (AttributeVar.RemoveRootNamespace(EmitterName))
				{
					if (AttributeVar.RemoveRootNamespace(FNiagaraConstants::ParticleAttributeNamespaceString))
					{
						ExplicitCaptureAttributes.Add(AttributeVar.GetName());
					}
				}
			}
		}

		BuildCacheLayout(CreateParameters, CacheLayout, EmitterCompiledData.DataSetCompiledData, EmitterHandle.GetName(), MoveTemp(RebaseVariableNames), MoveTemp(InterpVariableNames), ExplicitCaptureAttributes);
	}

	static bool BuildCacheReadMappings(FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraDataSetCompiledData& CompiledData)
	{
		const int32 CacheTotalComponents = CacheLayout.FloatCount + CacheLayout.HalfCount + CacheLayout.Int32Count;
		CacheLayout.ComponentMappingsToDataBuffer.Empty(CacheTotalComponents);
		CacheLayout.ComponentMappingsToDataBuffer.AddDefaulted(CacheTotalComponents);
		CacheLayout.VariableCopyMappingsToDataBuffer.Empty(0);
		CacheLayout.ComponentVelocity = INDEX_NONE;

		int32 FloatOffset = 0;
		int32 HalfOffset = CacheLayout.FloatCount;
		int32 Int32Offset = HalfOffset + CacheLayout.HalfCount;
		for (const FNiagaraSimCacheVariable& SourceVariable : CacheLayout.Variables)
		{
			// Find variable, if it doesn't exist that's ok as the cache contains more data than is required
			const int32 DataSetVariableIndex = CompiledData.Variables.IndexOfByPredicate([&](const FNiagaraVariableBase& DataSetVariable) { return DataSetVariable == SourceVariable.Variable; });
			const FNiagaraVariableLayoutInfo* DestVariableLayout = nullptr;
			if (DataSetVariableIndex != INDEX_NONE)
			{
				DestVariableLayout = &CompiledData.VariableLayouts[DataSetVariableIndex];

				// If the variable exists but types not match the cache is invalid
				if ((DestVariableLayout->GetNumFloatComponents() != SourceVariable.FloatCount) ||
					(DestVariableLayout->GetNumHalfComponents() != SourceVariable.HalfCount) ||
					(DestVariableLayout->GetNumInt32Components() != SourceVariable.Int32Count))
				{
					return false;
				}
			}

			// If this is our velocity component track it
			//-OPT: Don't need to do this each time
			if (CacheLayout.bAllowVelocityExtrapolation)
			{
				const FNiagaraVariableBase VelocityVariable(FNiagaraTypeDefinition::GetVec3Def(), "Velocity");

				if (SourceVariable.Variable == VelocityVariable)
				{
					CacheLayout.ComponentVelocity = uint16(FloatOffset);
				}
			}

			// Is this a type that requires conversion / re-basing?
			if (DestVariableLayout != nullptr)
			{
				const bool bInterpVariable = CacheLayout.InterpVariableNames.Contains(SourceVariable.Variable.GetName());
				const bool bRebaseVariable = CacheLayout.RebaseVariableNames.Contains(SourceVariable.Variable.GetName());

				if (bInterpVariable || bRebaseVariable)
				{
					if (SourceVariable.Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef())
					{
						check(SourceVariable.FloatCount == 3);
						if (bInterpVariable)
						{
							CacheLayout.VariableCopyMappingsToDataBuffer.Emplace(uint16(FloatOffset), uint16(DestVariableLayout->FloatComponentStart), bRebaseVariable ? &FNiagaraSimCacheHelper::InterpPositions<true> : &FNiagaraSimCacheHelper::InterpPositions<false>);
						}
						else if ( CacheLayout.bAllowVelocityExtrapolation )
						{
							CacheLayout.VariableCopyMappingsToDataBuffer.Emplace(uint16(FloatOffset), uint16(DestVariableLayout->FloatComponentStart), bRebaseVariable ? &FNiagaraSimCacheHelper::ExtrapolatePositions<true> : &FNiagaraSimCacheHelper::ExtrapolatePositions<false>);
						}
						else
						{
							CacheLayout.VariableCopyMappingsToDataBuffer.Emplace(uint16(FloatOffset), uint16(DestVariableLayout->FloatComponentStart), &FNiagaraSimCacheHelper::CopyPositions);
						}
						DestVariableLayout = nullptr;
					}
					else if (SourceVariable.Variable.GetType() == FNiagaraTypeDefinition::GetQuatDef())
					{
						check(SourceVariable.FloatCount == 4);
						if (bInterpVariable)
						{
							CacheLayout.VariableCopyMappingsToDataBuffer.Emplace(uint16(FloatOffset), uint16(DestVariableLayout->FloatComponentStart), bInterpVariable  ? &FNiagaraSimCacheHelper::InterpQuaternions<true> : &FNiagaraSimCacheHelper::InterpQuaternions<false>);
						}
						else
						{
							CacheLayout.VariableCopyMappingsToDataBuffer.Emplace(uint16(FloatOffset), uint16(DestVariableLayout->FloatComponentStart), &FNiagaraSimCacheHelper::CopyQuaternions);
						}
						DestVariableLayout = nullptr;
					}
					else if (SourceVariable.Variable.GetType() == FNiagaraTypeDefinition::GetMatrix4Def())
					{
						check(SourceVariable.FloatCount == 16);
						CacheLayout.VariableCopyMappingsToDataBuffer.Emplace(uint16(FloatOffset), uint16(DestVariableLayout->FloatComponentStart), &FNiagaraSimCacheHelper::CopyMatrices);
						DestVariableLayout = nullptr;
					}
				}
			}

			for (int32 i = 0; i < SourceVariable.FloatCount; ++i)
			{
				CacheLayout.ComponentMappingsToDataBuffer[FloatOffset++] = uint16(DestVariableLayout ? DestVariableLayout->FloatComponentStart + i : INDEX_NONE);
			}

			for (int32 i = 0; i < SourceVariable.HalfCount; ++i)
			{
				CacheLayout.ComponentMappingsToDataBuffer[HalfOffset++] = uint16(DestVariableLayout ? DestVariableLayout->HalfComponentStart + i : INDEX_NONE);
			}

			for (int32 i = 0; i < SourceVariable.Int32Count; ++i)
			{
				CacheLayout.ComponentMappingsToDataBuffer[Int32Offset++] = uint16(DestVariableLayout ? DestVariableLayout->Int32ComponentStart + i : INDEX_NONE);
			}
		}

		return true;
	}

	static void CheckedMemcpy(TConstArrayView<uint8> DstArray, uint8* Dst, TConstArrayView<uint8> SrcArray, const uint8* Src, uint32 Size)
	{
		checkf(Src >= SrcArray.GetData() && Src + Size <= SrcArray.GetData() + SrcArray.Num(), TEXT("Source %p-%p is out of bounds, start %p end %p"), Src, Src + Size, SrcArray.GetData(), SrcArray.GetData() + SrcArray.Num());
		checkf(Dst >= DstArray.GetData() && Dst + Size <= DstArray.GetData() + DstArray.Num(), TEXT("Dest %p-%p is out of bounds, start %p end %p"), Dst, Dst + Size, DstArray.GetData(), DstArray.GetData() + DstArray.Num());
		FMemory::Memcpy(Dst, Src, Size);
	}

	void WriteDataBuffer(const FNiagaraDataBuffer& DataBuffer, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, FNiagaraSimCacheDataBuffers& CacheBuffer, int32 FirstInstance, int32 NumInstances)
	{
		if ( NumInstances == 0 )
		{
			return;
		}

		CacheBuffer.NumInstances = NumInstances;

		int32 iComponent = 0;

		// Copy Float
		CacheBuffer.FloatData.AddDefaulted(CacheLayout.FloatCount * NumInstances * sizeof(float));
		for ( uint32 i=0; i < CacheLayout.FloatCount; ++i )
		{
			const uint32 Component = CacheLayout.ComponentMappingsFromDataBuffer[iComponent++];
			const uint8* Source = DataBuffer.GetComponentPtrFloat(Component) + (FirstInstance * sizeof(float));
			uint8* Dest = CacheBuffer.FloatData.GetData() + (i * NumInstances * sizeof(float));
			CheckedMemcpy(CacheBuffer.FloatData, Dest, DataBuffer.GetFloatBuffer(), Source, sizeof(float) * NumInstances);
			//FMemory::Memcpy(Dest, Source, sizeof(float) * NumInstances);
		}
		
		// Copy Half
		CacheBuffer.HalfData.AddDefaulted(CacheLayout.HalfCount * NumInstances * sizeof(FFloat16));
		for (uint32 i = 0; i < CacheLayout.HalfCount; ++i)
		{
			const uint32 Component = CacheLayout.ComponentMappingsFromDataBuffer[iComponent++];
			const uint8* Source = DataBuffer.GetComponentPtrHalf(Component) + (FirstInstance * sizeof(FFloat16));
			uint8* Dest = CacheBuffer.HalfData.GetData() + (i * NumInstances * sizeof(FFloat16));
			CheckedMemcpy(CacheBuffer.HalfData, Dest, DataBuffer.GetHalfBuffer(), Source, sizeof(FFloat16) * NumInstances);
			//FMemory::Memcpy(Dest, Source, sizeof(FFloat16) * NumInstances);
		}

		// Copy Int32
		CacheBuffer.Int32Data.AddDefaulted(CacheLayout.Int32Count * NumInstances * sizeof(int32));
		for (uint32 i = 0; i < CacheLayout.Int32Count; ++i)
		{
			const uint32 Component = CacheLayout.ComponentMappingsFromDataBuffer[iComponent++];
			const uint8* Source = DataBuffer.GetComponentPtrInt32(Component) + (FirstInstance * sizeof(int32));
			uint8* Dest = CacheBuffer.Int32Data.GetData() + (i * NumInstances * sizeof(int32));
			CheckedMemcpy(CacheBuffer.Int32Data, Dest, DataBuffer.GetInt32Buffer(), Source, sizeof(int32) * NumInstances);
			//FMemory::Memcpy(Dest, Source, sizeof(int32) * NumInstances);
		}

		// Copy ID to Index Table
		CacheBuffer.IDToIndexTable = DataBuffer.GetIDTable();
		CacheBuffer.IDAcquireTag = DataBuffer.GetIDAcquireTag();

		// Generate a interp mapping (if we have enabled it)
		if (CacheLayout.bAllowInterpolation)
		{
			//-TODO: Persistent ID mapping
			CacheBuffer.InterpMapping.SetNumUninitialized(DataBuffer.GetNumInstances());
			const uint8* UniqueIDs = DataBuffer.GetComponentPtrInt32(CacheLayout.ComponentUniqueID);
			FMemory::Memcpy(CacheBuffer.InterpMapping.GetData(), UniqueIDs, DataBuffer.GetNumInstances() * sizeof(int32));
		}
	}

	void WriteDataBufferGPU(FNiagaraEmitterInstance& EmitterInstance, const FNiagaraDataBuffer& DataBuffer, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, FNiagaraSimCacheDataBuffers& CacheBuffer)
	{
		//-TODO: Make async
		TSharedRef<FNiagaraDataSetReadback> ReadbackRequest = MakeShared<FNiagaraDataSetReadback>();
		ReadbackRequest->ImmediateReadback(&EmitterInstance);
		if ( FNiagaraDataBuffer* CurrentData = ReadbackRequest->GetDataSet().GetCurrentData() )
		{
			WriteDataBuffer(*CurrentData, CacheLayout, CacheBuffer, 0, CurrentData->GetNumInstances());
		}
	}

	static void ReadFloatBuffers(int32& iComponent, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraSimCacheDataBuffers& CacheBuffer, TArrayView<uint8> DestBuffer, uint32 DestStride)
	{
		const int32 NumInstances = CacheBuffer.NumInstances;
		for (uint32 i = 0; i < CacheLayout.FloatCount; ++i)
		{
			const uint32 Component = CacheLayout.ComponentMappingsToDataBuffer[iComponent++];
			if (Component == InvalidComponent)
			{
				continue;
			}
			const uint8* SourceFloats = CacheBuffer.FloatData.GetData() + (i * NumInstances * sizeof(float));
			uint8* DestFloats = DestBuffer.GetData() + (DestStride * Component);
			CheckedMemcpy(DestBuffer, DestFloats, CacheBuffer.FloatData, SourceFloats, sizeof(float) * NumInstances);
		}
	}

	static void ReadHalfBuffers(int32& iComponent, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraSimCacheDataBuffers& CacheBuffer, TArrayView<uint8> DestBuffer, uint32 DestStride)
	{
		const int32 NumInstances = CacheBuffer.NumInstances;
		for (uint32 i = 0; i < CacheLayout.HalfCount; ++i)
		{
			const uint32 Component = CacheLayout.ComponentMappingsToDataBuffer[iComponent++];
			if (Component == InvalidComponent)
			{
				continue;
			}
			const uint8* SourceHalfs = CacheBuffer.HalfData.GetData() + (i * NumInstances * sizeof(FFloat16));
			uint8* DestHalfs = DestBuffer.GetData() + (DestStride * Component);
			CheckedMemcpy(DestBuffer, DestHalfs, CacheBuffer.HalfData, SourceHalfs, sizeof(FFloat16) * NumInstances);
		}
	}

	static void ReadInt32Buffers(int32& iComponent, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraSimCacheDataBuffers& CacheBuffer, TArrayView<uint8> DestBuffer, uint32 DestStride)
	{
		const int32 NumInstances = CacheBuffer.NumInstances;
		for (uint32 i = 0; i < CacheLayout.Int32Count; ++i)
		{
			const uint32 Component = CacheLayout.ComponentMappingsToDataBuffer[iComponent++];
			if (Component == InvalidComponent)
			{
				continue;
			}
			const uint8* SourceInt32s = CacheBuffer.Int32Data.GetData() + (i * NumInstances * sizeof(int32));
			uint8* DestInt32s = DestBuffer.GetData() + (DestStride * Component);
			CheckedMemcpy(DestBuffer, DestInt32s, CacheBuffer.Int32Data, SourceInt32s, sizeof(int32) * NumInstances);
		}
	}

	static void ReadCustomBuffers(float FrameFraction, float FrameDeltaSeconds, const FTransform& RebaseTransform, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraSimCacheDataBuffers& CacheBufferA, const FNiagaraSimCacheDataBuffers& CacheBufferB, uint8* DestBuffer, uint32 DestStride)
	{
		if (CacheLayout.VariableCopyMappingsToDataBuffer.Num() == 0)
		{
			return;
		}

		FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext VariableCopyDataContext;
		VariableCopyDataContext.FrameFraction	= FrameFraction;
		VariableCopyDataContext.RecipDt			= FrameDeltaSeconds;
		VariableCopyDataContext.NumInstances	= CacheBufferA.NumInstances;
		VariableCopyDataContext.RebaseTransform	= RebaseTransform;
		VariableCopyDataContext.InterpMappings	= CacheBufferA.InterpMapping;
		VariableCopyDataContext.DestStride		= DestStride;
		VariableCopyDataContext.SourceAStride	= CacheBufferA.NumInstances * sizeof(float);
		VariableCopyDataContext.SourceBStride	= CacheBufferB.NumInstances * sizeof(float);
		if (CacheLayout.ComponentVelocity != uint16(INDEX_NONE))
		{
			VariableCopyDataContext.VelocityComponent = CacheBufferA.FloatData.GetData() + (CacheLayout.ComponentVelocity * VariableCopyDataContext.SourceAStride);
		}
		else
		{
			VariableCopyDataContext.VelocityComponent = nullptr;
		}

		for (const FNiagaraSimCacheDataBuffersLayout::FVariableCopyMapping& VariableCopyMapping : CacheLayout.VariableCopyMappingsToDataBuffer)
		{
			VariableCopyDataContext.Dest				= DestBuffer + (uint32(VariableCopyMapping.ComponentTo) * DestStride);
			VariableCopyDataContext.SourceAComponent	= CacheBufferA.FloatData.GetData() + (VariableCopyMapping.ComponentFrom * VariableCopyDataContext.SourceAStride);
			VariableCopyDataContext.SourceBComponent	= CacheBufferB.FloatData.GetData() + (VariableCopyMapping.ComponentFrom * VariableCopyDataContext.SourceBStride);
			VariableCopyMapping.CopyFunc(VariableCopyDataContext);
		}
	}

	void ReadDataBuffer(float FrameFraction, float FrameDeltaSeconds, const FTransform& RebaseTransform, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraSimCacheDataBuffers& CacheBufferA, const FNiagaraSimCacheDataBuffers& CacheBufferB, FNiagaraDataSet& DataSet)
	{
		FNiagaraDataBuffer& DataBuffer = DataSet.BeginSimulate();
		DataBuffer.Allocate(CacheBufferA.NumInstances);
		DataBuffer.SetNumInstances(CacheBufferA.NumInstances);
		if (CacheBufferA.NumInstances > 0 )
		{
			int32 iComponent = 0;
			ReadFloatBuffers(iComponent, CacheLayout, CacheBufferA, MakeArrayView(DataBuffer.GetComponentPtrFloat(0), DataBuffer.GetFloatBuffer().Num()), DataBuffer.GetFloatStride());
			ReadHalfBuffers(iComponent, CacheLayout, CacheBufferA, MakeArrayView(DataBuffer.GetComponentPtrHalf(0), DataBuffer.GetHalfBuffer().Num()), DataBuffer.GetHalfStride());
			ReadInt32Buffers(iComponent, CacheLayout, CacheBufferA, MakeArrayView(DataBuffer.GetComponentPtrInt32(0), DataBuffer.GetInt32Buffer().Num()), DataBuffer.GetInt32Stride());
			ReadCustomBuffers(FrameFraction, FrameDeltaSeconds, RebaseTransform, CacheLayout, CacheBufferA, CacheBufferB, DataBuffer.GetComponentPtrFloat(0), DataBuffer.GetFloatStride());
		}

		//-TODO:DestinationDataBuffer.SetIDTable(CacheBufferA.IDToIndexTable);
		DataBuffer.SetIDAcquireTag(CacheBufferA.IDAcquireTag);

		DataSet.EndSimulate();
	}

	void ReadDataBufferGPU(float InFrameFraction, float InFrameDeltaSeconds, const FTransform& InRebaseTransform, FNiagaraEmitterInstance& EmitterInstance, const FNiagaraSimCacheDataBuffersLayout& InCacheLayout, const FNiagaraSimCacheDataBuffers& InCacheBufferA, const FNiagaraSimCacheDataBuffers& InCacheBufferB, FNiagaraDataSet& InDataSet, std::atomic<int32>& InPendingCommandsCounter)
	{
		if (EmitterInstance.IsDisabled())
		{
			return;
		}

		++InPendingCommandsCounter;

		check(EmitterInstance.GetGPUContext());

		FNiagaraGpuComputeDispatchInterface* DispathInterface = EmitterInstance.GetParentSystemInstance()->GetComputeDispatchInterface();
		ENQUEUE_RENDER_COMMAND(NiagaraSimCacheGpuReadFrame)(
			[DispathInterface, GPUExecContext=EmitterInstance.GetGPUContext(), FrameFraction=InFrameFraction, FrameDeltaSeconds=InFrameDeltaSeconds, RebaseTransform=InRebaseTransform, CacheLayout=&InCacheLayout, CacheBufferA=&InCacheBufferA, CacheBufferB=&InCacheBufferB, DataSet=&InDataSet, PendingCommandsCounter=&InPendingCommandsCounter](FRHICommandListImmediate& RHICmdList)
			{
				const int32 NumInstances = CacheBufferA->NumInstances;

				// Set Instance Count
				{
					FNiagaraGPUInstanceCountManager& CountManager = DispathInterface->GetGPUInstanceCounterManager();
					if (GPUExecContext->CountOffset_RT == INDEX_NONE)
					{
						GPUExecContext->CountOffset_RT = CountManager.AcquireOrAllocateEntry(RHICmdList);
					}

					const FRWBuffer& CountBuffer = CountManager.GetInstanceCountBuffer();
					const TPair<uint32, int32> DataToSet(GPUExecContext->CountOffset_RT, NumInstances);
					RHICmdList.Transition(FRHITransitionInfo(CountBuffer.UAV, FNiagaraGPUInstanceCountManager::kCountBufferDefaultState, ERHIAccess::UAVCompute));
					NiagaraClearCounts::ClearCountsInt(RHICmdList, CountBuffer.UAV, MakeArrayView(&DataToSet, 1));
					RHICmdList.Transition(FRHITransitionInfo(CountBuffer.UAV, ERHIAccess::UAVCompute, FNiagaraGPUInstanceCountManager::kCountBufferDefaultState));
				}

				// Copy instance counts
				FNiagaraDataBuffer& DataBuffer = DataSet->GetCurrentDataChecked();
				DataBuffer.AllocateGPU(RHICmdList, NumInstances, DispathInterface->GetFeatureLevel(), TEXT("NiagaraSimCache"));
				DataBuffer.SetNumInstances(NumInstances);
				DataBuffer.SetGPUDataReadyStage(ENiagaraGpuComputeTickStage::PreInitViews);
				GPUExecContext->SetDataToRender(&DataBuffer);

				if (NumInstances > 0)
				{
					int32 iComponent = 0;

					// Copy Float
					if ( CacheLayout->FloatCount > 0 )
					{
						FRWBuffer& RWBuffer = DataBuffer.GetGPUBufferFloat();
						uint8* RWBufferMemory = reinterpret_cast<uint8*>(RHILockBuffer(RWBuffer.Buffer, 0, RWBuffer.NumBytes, RLM_WriteOnly));
						ReadFloatBuffers(iComponent, *CacheLayout, *CacheBufferA, MakeArrayView(RWBufferMemory, RWBuffer.NumBytes), DataBuffer.GetFloatStride());
						ReadCustomBuffers(FrameFraction, FrameDeltaSeconds, RebaseTransform, *CacheLayout, *CacheBufferA, *CacheBufferB, RWBufferMemory, DataBuffer.GetFloatStride());
						RHIUnlockBuffer(RWBuffer.Buffer);
					}

					// Copy Half
					if (CacheLayout->HalfCount > 0)
					{
						FRWBuffer& RWBuffer = DataBuffer.GetGPUBufferHalf();
						uint8* RWBufferMemory = reinterpret_cast<uint8*>(RHILockBuffer(RWBuffer.Buffer, 0, RWBuffer.NumBytes, RLM_WriteOnly));
						ReadHalfBuffers(iComponent, *CacheLayout, *CacheBufferA, MakeArrayView(RWBufferMemory, RWBuffer.NumBytes), DataBuffer.GetHalfStride());
						RHIUnlockBuffer(RWBuffer.Buffer);
					}

					// Copy Int32
					if (CacheLayout->Int32Count > 0)
					{
						FRWBuffer& RWBuffer = DataBuffer.GetGPUBufferInt();
						uint8* RWBufferMemory = reinterpret_cast<uint8*>(RHILockBuffer(RWBuffer.Buffer, 0, RWBuffer.NumBytes, RLM_WriteOnly));
						ReadInt32Buffers(iComponent, *CacheLayout, *CacheBufferA, MakeArrayView(RWBufferMemory, RWBuffer.NumBytes), DataBuffer.GetInt32Stride());
						RHIUnlockBuffer(RWBuffer.Buffer);
					}
				}

				//-TODO:DestinationDataBuffer.SetIDTable(CacheBuffer.IDToIndexTable);
				DataBuffer.SetIDAcquireTag(CacheBufferA->IDAcquireTag);

				// Ensure we decrement our counter so the GameThread knows the state of things
				--(*PendingCommandsCounter);
			}
		);
	}

	static bool CanInterpolateVariable(const FNiagaraVariableBase& Variable)
	{
		return
			(Variable.GetType() == FNiagaraTypeDefinition::GetQuatDef()) ||
			(Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef());
	}

	static bool CanRebaseVariable(const FNiagaraVariableBase& Variable)
	{
		return	
			(Variable.GetType() == FNiagaraTypeDefinition::GetQuatDef()) ||
			(Variable.GetType() == FNiagaraTypeDefinition::GetMatrix4Def()) ||
			(Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef());
	}

	template<bool bWithRebase>
	static void ExtrapolatePositions(const FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext& CopyDataContext)
	{
		float* DstPositions[3] = { reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 0)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 1)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 2))};
		const float* SrcPositions[3] = { reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 2)) };
		const float* SrcVelocities[3] = { reinterpret_cast<const float*>(CopyDataContext.VelocityComponent + (CopyDataContext.SourceAStride * 0)), reinterpret_cast<const float*>(CopyDataContext.VelocityComponent + (CopyDataContext.SourceAStride * 1)), reinterpret_cast<const float*>(CopyDataContext.VelocityComponent + (CopyDataContext.SourceAStride * 2)) };

		for (uint32 i = 0; i < CopyDataContext.NumInstances; ++i)
		{
			const FVector3f CachePosition(SrcPositions[0][i], SrcPositions[1][i], SrcPositions[2][i]);
			const FVector3f CacheVelocity(SrcVelocities[0][i], SrcVelocities[1][i], SrcVelocities[2][i]);
			const FVector3f Position = CachePosition + (CacheVelocity * CopyDataContext.FrameFraction * CopyDataContext.RecipDt);
			const FVector3f RebasedPosition = bWithRebase ? FVector3f(CopyDataContext.RebaseTransform.TransformPosition(FVector(Position))) : Position;
			DstPositions[0][i] = RebasedPosition.X;
			DstPositions[1][i] = RebasedPosition.Y;
			DstPositions[2][i] = RebasedPosition.Z;
		}
	}

	template<bool bWithRebase>
	static void InterpPositions(const FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext& CopyDataContext)
	{
		float* DstPositions[3] = { reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 0)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 1)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 2)) };
		const float* SrcAPositions[3] = { reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 2)) };
		const float* SrcBPositions[3] = { reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 2)) };

		for (uint32 iInstanceA=0; iInstanceA < CopyDataContext.NumInstances; ++iInstanceA)
		{
			const uint32 iInstanceB = CopyDataContext.InterpMappings[iInstanceA];

			const FVector3f CachePositionA(SrcAPositions[0][iInstanceA], SrcAPositions[1][iInstanceA], SrcAPositions[2][iInstanceA]);
			const FVector3f CachePositionB = iInstanceB == INDEX_NONE ? CachePositionA : FVector3f(SrcBPositions[0][iInstanceB], SrcBPositions[1][iInstanceB], SrcBPositions[2][iInstanceB]);
			const FVector3f Position(FMath::Lerp(CachePositionA, CachePositionB, CopyDataContext.FrameFraction));

			const FVector3f RebasedPosition = bWithRebase ? FVector3f(CopyDataContext.RebaseTransform.TransformPosition(FVector(Position))) : Position;
			DstPositions[0][iInstanceA] = float(RebasedPosition.X);
			DstPositions[1][iInstanceA] = float(RebasedPosition.Y);
			DstPositions[2][iInstanceA] = float(RebasedPosition.Z);
		}
	}

	static void CopyPositions(const FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext& CopyDataContext)
	{
		float* DstPositions[3] = { reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 0)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 1)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 2)) };
		const float* SrcPositions[3] = { reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 2)) };

		for (uint32 i = 0; i < CopyDataContext.NumInstances; ++i)
		{
			const FVector3f CachePosition(SrcPositions[0][i], SrcPositions[1][i], SrcPositions[2][i]);
			const FVector3f RebasedPosition = FVector3f(CopyDataContext.RebaseTransform.TransformPosition(FVector(CachePosition)));
			DstPositions[0][i] = RebasedPosition.X;
			DstPositions[1][i] = RebasedPosition.Y;
			DstPositions[2][i] = RebasedPosition.Z;
		}
	}

	template<bool bWithRebase>
	static void InterpQuaternions(const FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext& CopyDataContext)
	{
		float* DstQuats[4] = { reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 0)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 1)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 2)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 3)) };
		const float* SrcAQuats[4] = { reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 2)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 3)) };
		const float* SrcBQuats[4] = { reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 2)), reinterpret_cast<const float*>(CopyDataContext.SourceBComponent + (CopyDataContext.SourceBStride * 3)) };

		for (uint32 iInstanceA=0; iInstanceA < CopyDataContext.NumInstances; ++iInstanceA)
		{
			const uint32 iInstanceB = CopyDataContext.InterpMappings[iInstanceA];
			const FQuat4f CacheRotationA(SrcAQuats[0][iInstanceA], SrcAQuats[1][iInstanceA], SrcAQuats[2][iInstanceA], SrcAQuats[3][iInstanceA]);
			const FQuat4f CacheRotationB = iInstanceB == INDEX_NONE ? CacheRotationA : FQuat4f(SrcBQuats[0][iInstanceB], SrcBQuats[1][iInstanceB], SrcBQuats[2][iInstanceB], SrcBQuats[3][iInstanceB]);
			const FQuat4f CacheRotation(FQuat4f::Slerp(CacheRotationA, CacheRotationB, CopyDataContext.FrameFraction));
			const FQuat4f RebasedQuat = bWithRebase ? CacheRotation * FQuat4f(CopyDataContext.RebaseTransform.GetRotation()) : CacheRotation;
			DstQuats[0][iInstanceA] = RebasedQuat.X;
			DstQuats[1][iInstanceA] = RebasedQuat.Y;
			DstQuats[2][iInstanceA] = RebasedQuat.Z;
			DstQuats[3][iInstanceA] = RebasedQuat.W;
		}
	}

	static void CopyQuaternions(const FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext& CopyDataContext)
	{
		float* DstQuats[4] = { reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 0)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 1)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 2)), reinterpret_cast<float*>(CopyDataContext.Dest + (CopyDataContext.DestStride * 3)) };
		const float* SrcQuats[4] = { reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 0)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 1)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 2)), reinterpret_cast<const float*>(CopyDataContext.SourceAComponent + (CopyDataContext.SourceAStride * 3)) };

		for (uint32 i = 0; i < CopyDataContext.NumInstances; ++i)
		{
			const FQuat4f CacheRotation(SrcQuats[0][i], SrcQuats[1][i], SrcQuats[2][i], SrcQuats[3][i]);
			const FQuat4f RebasedQuat = CacheRotation * FQuat4f(CopyDataContext.RebaseTransform.GetRotation());
			DstQuats[0][i] = RebasedQuat.X;
			DstQuats[1][i] = RebasedQuat.Y;
			DstQuats[2][i] = RebasedQuat.Z;
			DstQuats[3][i] = RebasedQuat.W;
		}
	}

	static void CopyMatrices(const FNiagaraSimCacheDataBuffersLayout::FVariableCopyContext& CopyDataContext)
	{
		float* DstFloats = reinterpret_cast<float*>(CopyDataContext.Dest);
		const uint32 DstStride = CopyDataContext.DestStride >> 2;

		const float* SrcFloats = reinterpret_cast<const float*>(CopyDataContext.SourceAComponent);
		const uint32 SrcStride = CopyDataContext.SourceAStride >> 2;

		const FMatrix44d RebaseMatrix = CopyDataContext.RebaseTransform.ToMatrixWithScale();
		for (uint32 i = 0; i < CopyDataContext.NumInstances; ++i)
		{
			FMatrix44d CacheMatrix;
			for (int32 j = 0; j < 16; ++j)
			{
				CacheMatrix.M[j >> 2][j & 0x3] = double(SrcFloats[i + (SrcStride * j)]);
			}

			CacheMatrix = CacheMatrix * RebaseMatrix;

			for (int32 j = 0; j < 16; ++j)
			{
				DstFloats[i + (DstStride * j)] = float(CacheMatrix.M[j >> 2][j & 0x3]);
			}
		}
	}

	UNiagaraSystem*						NiagaraSystem = nullptr;
	FNiagaraSystemInstance*				SystemInstance = nullptr;
	FNiagaraSystemSimulationPtr			SystemSimulation = nullptr;
	FNiagaraDataBuffer*					SystemSimulationDataBuffer = nullptr;

	static constexpr uint16				InvalidComponent = INDEX_NONE;
};
