// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSimCache.h"

#include "NiagaraClearCounts.h"
#include "NiagaraConstants.h"
#include "NiagaraComponent.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraDataInterfaceUtilities.h"
#include "NiagaraDataSetReadback.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"

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

	void BuildCacheLayout(FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraDataSetCompiledData& CompiledData, FName LayoutName, TArray<FName> InRebaseVariableNames) const
	{
		CacheLayout.LayoutName = LayoutName;
		CacheLayout.SimTarget = CompiledData.SimTarget;

		const int32 NumVariables = CompiledData.Variables.Num();
		CacheLayout.Variables.AddDefaulted(NumVariables);

		const int32 CacheTotalComponents = CompiledData.TotalFloatComponents + CompiledData.TotalFloatComponents + CompiledData.TotalInt32Components;
		CacheLayout.ComponentMappingsFromDataBuffer.Empty(CacheTotalComponents);
		CacheLayout.ComponentMappingsFromDataBuffer.AddDefaulted(CacheTotalComponents);
		CacheLayout.RebaseVariableNames = MoveTemp(InRebaseVariableNames);

		for ( int32 iVariable=0; iVariable < NumVariables; ++iVariable)
		{
			const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[iVariable];
			FNiagaraSimCacheVariable& CacheVariable = CacheLayout.Variables[iVariable];

			CacheVariable.Variable = CompiledData.Variables[iVariable];
			CacheVariable.FloatOffset = DataSetVariableLayout.GetNumFloatComponents() > 0 ? CacheLayout.FloatCount : INDEX_NONE;
			CacheVariable.FloatCount = DataSetVariableLayout.GetNumFloatComponents();
			CacheVariable.HalfOffset = DataSetVariableLayout.GetNumHalfComponents() > 0 ? CacheLayout.HalfCount : INDEX_NONE;
			CacheVariable.HalfCount = DataSetVariableLayout.GetNumHalfComponents();
			CacheVariable.Int32Offset = DataSetVariableLayout.GetNumInt32Components() > 0 ? CacheLayout.Int32Count : INDEX_NONE;
			CacheVariable.Int32Count = DataSetVariableLayout.GetNumInt32Components();

			CacheLayout.FloatCount += DataSetVariableLayout.GetNumFloatComponents();
			CacheLayout.HalfCount += DataSetVariableLayout.GetNumHalfComponents();
			CacheLayout.Int32Count += DataSetVariableLayout.GetNumInt32Components();
		}

		// Build write mappings we will build read mappings in a separate path
		int32 FloatOffset = 0;
		int32 HalfOffset = CacheLayout.FloatCount;
		int32 Int32Offset = HalfOffset + CacheLayout.HalfCount;
		for (int32 iVariable = 0; iVariable < NumVariables; ++iVariable)
		{
			const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData.VariableLayouts[iVariable];
			FNiagaraSimCacheVariable& CacheVariable = CacheLayout.Variables[iVariable];

			for (int32 iComponent=0; iComponent < CacheVariable.FloatCount; ++iComponent)
			{
				CacheLayout.ComponentMappingsFromDataBuffer[FloatOffset] = CacheVariable.FloatOffset + iComponent;
				++FloatOffset;
			}

			for (int32 iComponent=0; iComponent < CacheVariable.HalfCount; ++iComponent)
			{
				CacheLayout.ComponentMappingsFromDataBuffer[HalfOffset] = CacheVariable.HalfOffset + iComponent;
				++HalfOffset;
			}

			for (int32 iComponent=0; iComponent < CacheVariable.Int32Count; ++iComponent)
			{
				CacheLayout.ComponentMappingsFromDataBuffer[Int32Offset] = CacheVariable.Int32Offset + iComponent;
				++Int32Offset;
			}
		}

		// Slightly inefficient but we can share the code between the paths
		BuildCacheReadMappings(CacheLayout, CompiledData);
	}

	void BuildCacheLayoutForSystem(const FNiagaraSimCacheCreateParameters& CreateParmaeters, FNiagaraSimCacheDataBuffersLayout& CacheLayout)
	{
		const FNiagaraDataSetCompiledData& SystemCompileData = NiagaraSystem->GetSystemCompiledData().DataSetCompiledData;

		TArray<FName> RebaseVariableNames;
		if ( CreateParmaeters.bAllowRebasing )
		{
			TArray<FString, TInlineAllocator<8>> LocalSpaceEmitters;
			for ( int32 i=0; i < NiagaraSystem->GetNumEmitters(); ++i )
			{
				const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(i);
				if ( EmitterHandle.GetInstance().GetEmitterData()->bLocalSpace )
				{
					LocalSpaceEmitters.Add(EmitterHandle.GetUniqueInstanceName());
				}
			}

			for (const FNiagaraVariable& Variable : SystemCompileData.Variables)
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

					if ( bIsLocalSpace == false && CreateParmaeters.RebaseExcludeList.Contains(Variable.GetName()) == false )
					{
						RebaseVariableNames.AddUnique(Variable.GetName());
					}
				}
				else if ( CanRebaseVariable(Variable) && CreateParmaeters.RebaseIncludeList.Contains(Variable.GetName()) )
				{
					RebaseVariableNames.AddUnique(Variable.GetName());
				}
			}
		}

		BuildCacheLayout(CacheLayout, SystemCompileData, NiagaraSystem->GetFName(), MoveTemp(RebaseVariableNames));
	}

	void BuildCacheLayoutForEmitter(const FNiagaraSimCacheCreateParameters& CreateParmaeters, FNiagaraSimCacheDataBuffersLayout& CacheLayout, int EmitterIndex)
	{
		const FNiagaraEmitterHandle& EmitterHandle = NiagaraSystem->GetEmitterHandle(EmitterIndex);
		const FNiagaraEmitterCompiledData& EmitterCompiledData = NiagaraSystem->GetEmitterCompiledData()[EmitterIndex].Get();

		// Find potential candidates for re-basing
		TArray<FName> RebaseVariableNames;
		if ( CreateParmaeters.bAllowRebasing && EmitterHandle.GetInstance().GetEmitterData()->bLocalSpace == false )
		{
			// Build list of include / exclude names
			TArray<FName> ForceIncludeNames;
			TArray<FName> ForceExcludeNames;
			if ( CreateParmaeters.RebaseIncludeList.Num() > 0 || CreateParmaeters.RebaseExcludeList.Num() > 0 )
			{
				const FString EmitterName = EmitterHandle.GetUniqueInstanceName();
				for (FName RebaseName : CreateParmaeters.RebaseIncludeList)
				{
					FNiagaraVariableBase BaseVar(FNiagaraTypeDefinition::GetFloatDef(), RebaseName);
					if (BaseVar.RemoveRootNamespace(EmitterName))
					{
						ForceIncludeNames.Add(BaseVar.GetName());
					}
				}

				for (FName RebaseName : CreateParmaeters.RebaseExcludeList)
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
			EmitterHandle.GetInstance().GetEmitterData()->ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* RenderProperties)
				{
					for (FNiagaraVariableBase BoundAttribute : RenderProperties->GetBoundAttributes())
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
			for (const FNiagaraVariable& Variable : EmitterCompiledData.DataSetCompiledData.Variables)
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

		BuildCacheLayout(CacheLayout, EmitterCompiledData.DataSetCompiledData, EmitterHandle.GetName(), MoveTemp(RebaseVariableNames));
	}

	static bool BuildCacheReadMappings(FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraDataSetCompiledData& CompiledData)
	{
		const int32 CacheTotalComponents = CacheLayout.FloatCount + CacheLayout.HalfCount + CacheLayout.Int32Count;
		CacheLayout.ComponentMappingsToDataBuffer.Empty(CacheTotalComponents);
		CacheLayout.ComponentMappingsToDataBuffer.AddDefaulted(CacheTotalComponents);
		CacheLayout.VariableMappingsToDataBuffer.Empty(0);

		int32 FloatOffset = 0;
		int32 HalfOffset = CacheLayout.FloatCount;
		int32 Int32Offset = HalfOffset + CacheLayout.HalfCount;
		for (const FNiagaraSimCacheVariable& SourceVariable : CacheLayout.Variables)
		{
			// Find variable, if it doesn't exist that's ok as the cache contains more data than is required
			const int32 DataSetVariableIndex = CompiledData.Variables.IndexOfByPredicate(FNiagaraVariableMatch(SourceVariable.Variable.GetType(), SourceVariable.Variable.GetName()));
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

			// Is this a type that requires conversion / re-basing?
			if (DestVariableLayout != nullptr)
			{
				if ( CacheLayout.RebaseVariableNames.Contains(SourceVariable.Variable.GetName()) )
				{
					if (SourceVariable.Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef())
					{
						check(SourceVariable.FloatCount == 3);
						CacheLayout.VariableMappingsToDataBuffer.Emplace(FloatOffset, DestVariableLayout->FloatComponentStart, &FNiagaraSimCacheHelper::CopyPositions);
						DestVariableLayout = nullptr;
					}
					else if (SourceVariable.Variable.GetType() == FNiagaraTypeDefinition::GetQuatDef())
					{
						check(SourceVariable.FloatCount == 4);
						CacheLayout.VariableMappingsToDataBuffer.Emplace(FloatOffset, DestVariableLayout->FloatComponentStart, &FNiagaraSimCacheHelper::CopyQuaternions);
						DestVariableLayout = nullptr;
					}
					else if (SourceVariable.Variable.GetType() == FNiagaraTypeDefinition::GetMatrix4Def())
					{
						check(SourceVariable.FloatCount == 16);
						CacheLayout.VariableMappingsToDataBuffer.Emplace(FloatOffset, DestVariableLayout->FloatComponentStart, &FNiagaraSimCacheHelper::CopyMatrices);
						DestVariableLayout = nullptr;
					}
				}
			}

			for (int32 i = 0; i < SourceVariable.FloatCount; ++i)
			{
				CacheLayout.ComponentMappingsToDataBuffer[FloatOffset++] = DestVariableLayout ? DestVariableLayout->FloatComponentStart + i : INDEX_NONE;
			}

			for (int32 i = 0; i < SourceVariable.HalfCount; ++i)
			{
				CacheLayout.ComponentMappingsToDataBuffer[HalfOffset++] = DestVariableLayout ? DestVariableLayout->HalfComponentStart + i : INDEX_NONE;
			}

			for (int32 i = 0; i < SourceVariable.Int32Count; ++i)
			{
				CacheLayout.ComponentMappingsToDataBuffer[Int32Offset++] = DestVariableLayout ? DestVariableLayout->Int32ComponentStart + i : INDEX_NONE;
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

	void ReadDataBuffer(const FTransform& RebaseTransform, const FNiagaraSimCacheDataBuffersLayout& CacheLayout, const FNiagaraSimCacheDataBuffers& CacheBuffer, FNiagaraDataSet& DataSet)
	{
		FNiagaraDataBuffer& DataBuffer = DataSet.BeginSimulate();
		DataBuffer.Allocate(CacheBuffer.NumInstances);
		DataBuffer.SetNumInstances(CacheBuffer.NumInstances);
		if ( CacheBuffer.NumInstances > 0 )
		{
			int32 iComponent = 0;
			const int32 NumInstances = CacheBuffer.NumInstances;

			// Copy Float
			for (uint32 i=0; i < CacheLayout.FloatCount; ++i)
			{
				const uint32 Component = CacheLayout.ComponentMappingsToDataBuffer[iComponent++];
				if (Component == InvalidComponent)
				{
					continue;
				}
				const uint8* Source = CacheBuffer.FloatData.GetData() + (i * NumInstances * sizeof(float));
				uint8* Dest = DataBuffer.GetComponentPtrFloat(Component);
				CheckedMemcpy(DataBuffer.GetFloatBuffer(), Dest, CacheBuffer.FloatData, Source, sizeof(float) * NumInstances);
				//FMemory::Memcpy(Dest, Source, sizeof(float) * NumInstances);
			}

			// Copy Half
			for (uint32 i = 0; i < CacheLayout.HalfCount; ++i)
			{
				const uint32 Component = CacheLayout.ComponentMappingsToDataBuffer[iComponent++];
				if (Component == InvalidComponent)
				{
					continue;
				}
				const uint8* Source = CacheBuffer.HalfData.GetData() + (i * NumInstances * sizeof(FFloat16));
				uint8* Dest = DataBuffer.GetComponentPtrHalf(Component);
				CheckedMemcpy(DataBuffer.GetHalfBuffer(), Dest, CacheBuffer.HalfData, Source, sizeof(FFloat16) * NumInstances);
				//FMemory::Memcpy(Dest, Source, sizeof(FFloat16) * NumInstances);
			}

			// Copy Int32
			for (uint32 i = 0; i < CacheLayout.Int32Count; ++i)
			{
				const uint32 Component = CacheLayout.ComponentMappingsToDataBuffer[iComponent++];
				if (Component == InvalidComponent)
				{
					continue;
				}
				const uint8* Source = CacheBuffer.Int32Data.GetData() + (i * NumInstances * sizeof(int32));
				uint8* Dest = DataBuffer.GetComponentPtrInt32(Component);
				CheckedMemcpy(DataBuffer.GetInt32Buffer(), Dest, CacheBuffer.Int32Data, Source, sizeof(int32) * NumInstances);
				//FMemory::Memcpy(Dest, Source, sizeof(int32) * NumInstances);
			}

			// Copy variables that require processing
			for ( const FNiagaraSimCacheDataBuffersLayout::FVariableCopyInfo& VariableCopyInfo : CacheLayout.VariableMappingsToDataBuffer )
			{
				const uint32 SrcStride = uint32(NumInstances) * sizeof(float);
				const uint8* Src = CacheBuffer.FloatData.GetData() + (VariableCopyInfo.ComponentFrom * SrcStride);
				uint8* Dst = DataBuffer.GetComponentPtrFloat(VariableCopyInfo.ComponentTo);
				VariableCopyInfo.CopyFunc(Dst, DataBuffer.GetFloatStride(), Src, SrcStride, uint32(NumInstances), RebaseTransform);
			}
		}

		//-TODO:DestinationDataBuffer.SetIDTable(CacheBuffer.IDToIndexTable);
		DataBuffer.SetIDAcquireTag(CacheBuffer.IDAcquireTag);

		DataSet.EndSimulate();
	}

	void ReadDataBufferGPU(const FTransform& InRebaseTransform, FNiagaraEmitterInstance& EmitterInstance, const FNiagaraSimCacheDataBuffersLayout& InCacheLayout, const FNiagaraSimCacheDataBuffers& InCacheBuffer, FNiagaraDataSet& InDataSet, std::atomic<int32>& InPendingCommandsCounter)
	{
		if (EmitterInstance.IsDisabled())
		{
			return;
		}

		++InPendingCommandsCounter;

		check(EmitterInstance.GetGPUContext());

		FNiagaraGpuComputeDispatchInterface* DispathInterface = EmitterInstance.GetParentSystemInstance()->GetComputeDispatchInterface();
		ENQUEUE_RENDER_COMMAND(NiagaraSimCacheGpuReadFrame)(
			[DispathInterface, GPUExecContext=EmitterInstance.GetGPUContext(), RebaseTransform=InRebaseTransform, CacheLayout=&InCacheLayout, CacheBuffer=&InCacheBuffer, DataSet=&InDataSet, PendingCommandsCounter=&InPendingCommandsCounter](FRHICommandListImmediate& RHICmdList)
			{
				const int32 NumInstances = CacheBuffer->NumInstances;

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

				if (CacheBuffer->NumInstances > 0 )
				{
					int32 iComponent = 0;

					// Copy Float
					if ( CacheLayout->FloatCount > 0 )
					{
						FRWBuffer& RWBuffer = DataBuffer.GetGPUBufferFloat();
						const int32 RWComponentStride = DataBuffer.GetFloatStride();
						uint8* RWBufferMemory = reinterpret_cast<uint8*>(RHILockBuffer(RWBuffer.Buffer, 0, RWBuffer.NumBytes, RLM_WriteOnly));

						for (uint32 i=0; i < CacheLayout->FloatCount; ++i)
						{
							const uint32 Component = CacheLayout->ComponentMappingsToDataBuffer[iComponent++];
							if (Component == InvalidComponent)
							{
								continue;
							}
							const uint8* Source = CacheBuffer->FloatData.GetData() + (i * NumInstances * sizeof(float));
							uint8* Dest = RWBufferMemory + (uint32(Component) * RWComponentStride);
							CheckedMemcpy(MakeArrayView(RWBufferMemory, RWBuffer.NumBytes), Dest, CacheBuffer->FloatData, Source, sizeof(float) * NumInstances);
							//FMemory::Memcpy(Dest, Source, sizeof(float) * NumInstances);
						}

						// Copy variables that require processing
						for (const FNiagaraSimCacheDataBuffersLayout::FVariableCopyInfo& VariableCopyInfo : CacheLayout->VariableMappingsToDataBuffer)
						{
							const uint32 SrcStride = uint32(NumInstances) * sizeof(float);
							const uint8* Src = CacheBuffer->FloatData.GetData() + (VariableCopyInfo.ComponentFrom * SrcStride);
							uint8* Dst = RWBufferMemory + (uint32(VariableCopyInfo.ComponentTo) * RWComponentStride);
							VariableCopyInfo.CopyFunc(Dst, DataBuffer.GetFloatStride(), Src, SrcStride, uint32(NumInstances), RebaseTransform);
						}

						RHIUnlockBuffer(RWBuffer.Buffer);
					}

					// Copy Half
					if (CacheLayout->HalfCount > 0)
					{
						FRWBuffer& RWBuffer = DataBuffer.GetGPUBufferHalf();
						const int32 RWComponentStride = DataBuffer.GetHalfStride();
						uint8* RWBufferMemory = reinterpret_cast<uint8*>(RHILockBuffer(RWBuffer.Buffer, 0, RWBuffer.NumBytes, RLM_WriteOnly));

						for (uint32 i = 0; i < CacheLayout->HalfCount; ++i)
						{
							const uint32 Component = CacheLayout->ComponentMappingsToDataBuffer[iComponent++];
							if (Component == InvalidComponent)
							{
								continue;
							}
							const uint8* Source = CacheBuffer->HalfData.GetData() + (i * NumInstances * sizeof(FFloat16));
							uint8* Dest = RWBufferMemory + (uint32(Component) * RWComponentStride);
							CheckedMemcpy(MakeArrayView(RWBufferMemory, RWBuffer.NumBytes), Dest, CacheBuffer->HalfData, Source, sizeof(FFloat16) * NumInstances);
							//FMemory::Memcpy(Dest, Source, sizeof(FFloat16) * NumInstances);
						}

						RHIUnlockBuffer(RWBuffer.Buffer);
					}


					// Copy Int32
					if (CacheLayout->Int32Count > 0)
					{
						FRWBuffer& RWBuffer = DataBuffer.GetGPUBufferInt();
						const int32 RWComponentStride = DataBuffer.GetInt32Stride();
						uint8* RWBufferMemory = reinterpret_cast<uint8*>(RHILockBuffer(RWBuffer.Buffer, 0, RWBuffer.NumBytes, RLM_WriteOnly));

						for (uint32 i = 0; i < CacheLayout->Int32Count; ++i)
						{
							const uint32 Component = CacheLayout->ComponentMappingsToDataBuffer[iComponent++];
							if (Component == InvalidComponent)
							{
								continue;
							}
							const uint8* Source = CacheBuffer->Int32Data.GetData() + (i * NumInstances * sizeof(int32));
							uint8* Dest = RWBufferMemory + (uint32(Component) * RWComponentStride);
							CheckedMemcpy(MakeArrayView(RWBufferMemory, RWBuffer.NumBytes), Dest, CacheBuffer->Int32Data, Source, sizeof(int32) * NumInstances);
							//FMemory::Memcpy(Dest, Source, sizeof(int32) * NumInstances);
						}

						RHIUnlockBuffer(RWBuffer.Buffer);
					}
				}

				//-TODO:DestinationDataBuffer.SetIDTable(CacheBuffer.IDToIndexTable);
				DataBuffer.SetIDAcquireTag(CacheBuffer->IDAcquireTag);

				// Ensure we decrement our counter so the GameThread knows the state of things
				--(*PendingCommandsCounter);
			}
		);
	}

	static bool CanRebaseVariable(const FNiagaraVariableBase& Variable)
	{
		return	
			(Variable.GetType() == FNiagaraTypeDefinition::GetQuatDef()) ||
			(Variable.GetType() == FNiagaraTypeDefinition::GetMatrix4Def()) ||
			(Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef());
	}

	static void CopyPositions(uint8* Dst, uint32 DstStride, const uint8* Src, uint32 SrcStride, uint32 NumInstances, const FTransform& RebaseTransform)
	{
		float* DstFloats = reinterpret_cast<float*>(Dst);
		DstStride = DstStride >> 2;

		const float* SrcFloats = reinterpret_cast<const float*>(Src);
		SrcStride = SrcStride >> 2;

		for (uint32 i = 0; i < NumInstances; ++i)
		{
			const FVector CachePosition(
				SrcFloats[i + (SrcStride * 0)],
				SrcFloats[i + (SrcStride * 1)],
				SrcFloats[i + (SrcStride * 2)]
			);
			const FVector RebasedPosition = RebaseTransform.TransformPosition(CachePosition);
			DstFloats[i + (DstStride * 0)] = RebasedPosition.X;
			DstFloats[i + (DstStride * 1)] = RebasedPosition.Y;
			DstFloats[i + (DstStride * 2)] = RebasedPosition.Z;
		}
	}

	static void CopyQuaternions(uint8* Dst, uint32 DstStride, const uint8* Src, uint32 SrcStride, uint32 NumInstances, const FTransform& RebaseTransform)
	{
		float* DstFloats = reinterpret_cast<float*>(Dst);
		DstStride = DstStride >> 2;

		const float* SrcFloats = reinterpret_cast<const float*>(Src);
		SrcStride = SrcStride >> 2;

		for (uint32 i = 0; i < NumInstances; ++i)
		{
			const FQuat4f CacheRotation(
				SrcFloats[i + (SrcStride * 0)],
				SrcFloats[i + (SrcStride * 1)],
				SrcFloats[i + (SrcStride * 2)],
				SrcFloats[i + (SrcStride * 3)]
			);
			const FQuat4f RebasedQuat = CacheRotation * FQuat4f(RebaseTransform.GetRotation());
			DstFloats[i + (DstStride * 0)] = RebasedQuat.X;
			DstFloats[i + (DstStride * 1)] = RebasedQuat.Y;
			DstFloats[i + (DstStride * 2)] = RebasedQuat.Z;
			DstFloats[i + (DstStride * 3)] = RebasedQuat.W;
		}
	}

	static void CopyMatrices(uint8* Dst, uint32 DstStride, const uint8* Src, uint32 SrcStride, uint32 NumInstances, const FTransform& RebaseTransform)
	{
		float* DstFloats = reinterpret_cast<float*>(Dst);
		DstStride = DstStride >> 2;

		const float* SrcFloats = reinterpret_cast<const float*>(Src);
		SrcStride = SrcStride >> 2;

		const FMatrix44d RebaseMatrix = RebaseTransform.ToMatrixWithScale();
		for (uint32 i = 0; i < NumInstances; ++i)
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

//////////////////////////////////////////////////////////////////////////

UNiagaraSimCache::UNiagaraSimCache(const FObjectInitializer& ObjectInitializer)
{
}

bool UNiagaraSimCache::IsReadyForFinishDestroy()
{
	return PendingCommandsInFlight == 0;
}

void UNiagaraSimCache::BeginWrite(FNiagaraSimCacheCreateParameters InCreateParameters, UNiagaraComponent* NiagaraComponent)
{
	check(PendingCommandsInFlight == 0);

	FNiagaraSimCacheHelper Helper(NiagaraComponent);
	if (Helper.HasValidSimulation() == false)
	{
		return;
	}

	Modify();

	// Reset to defaults
	SoftNiagaraSystem = Helper.NiagaraSystem;
	CreateParameters = InCreateParameters;
	StartSeconds = 0.0f;
	DurationSeconds = 0.0f;
	CacheLayout = FNiagaraSimCacheLayout();
	CacheFrames.Empty();

	// Build new layout for system / emitters
	Helper.BuildCacheLayoutForSystem(CreateParameters, CacheLayout.SystemLayout);

	const int32 NumEmitters = Helper.NiagaraSystem->GetEmitterHandles().Num();
	CacheLayout.EmitterLayouts.AddDefaulted(NumEmitters);
	for ( int32 i=0; i < NumEmitters; ++i )
	{
		Helper.BuildCacheLayoutForEmitter(CreateParameters, CacheLayout.EmitterLayouts[i], i);
	}

	// Find data interfaces we may want to cache
	if ( CreateParameters.bAllowDataInterfaceCaching )
	{
		FNiagaraDataInterfaceUtilities::ForEachDataInterface(
			Helper.SystemInstance,
			[&](const FNiagaraVariableBase& Variable, UNiagaraDataInterface* DataInterface)
			{
				const void* PerInstanceData = Helper.SystemInstance->FindDataInterfaceInstanceData(DataInterface);
				if (UObject* DICacheStorage = DataInterface->SimCacheBeginWrite(this, Helper.SystemInstance, PerInstanceData))
				{
					DataInterfaceStorage.FindOrAdd(Variable) = DICacheStorage;
				}
				return true;
			}
		);
	}
}

void UNiagaraSimCache::WriteFrame(UNiagaraComponent* NiagaraComponent)
{
	FNiagaraSimCacheHelper Helper(NiagaraComponent);
	if (Helper.HasValidSimulationData() == false)
	{
		SoftNiagaraSystem.Reset();
		return;
	}

	if ( SoftNiagaraSystem.Get() != Helper.NiagaraSystem )
	{
		SoftNiagaraSystem.Reset();
		return;
	}

	// Simulation is complete nothing to cache
	if ( Helper.SystemInstance->IsComplete() )
	{
		return;
	}

	// Is the simulation running?  If not nothing to cache yet
	if ( Helper.SystemInstance->SystemInstanceState != ENiagaraSystemInstanceState::Running )
	{
		return;
	}

	// First frame we are about to cache?
	if ( CacheFrames.Num() == 0 )
	{
		StartSeconds = Helper.SystemInstance->GetAge();
	}

	// Invalid we have reset for some reason
	if ( Helper.SystemInstance->GetAge() < StartSeconds + DurationSeconds )
	{
		SoftNiagaraSystem.Reset();
		return;
	}

	DurationSeconds = Helper.SystemInstance->GetAge() - StartSeconds;

	// Cache frame
	FNiagaraSimCacheFrame& CacheFrame = CacheFrames.AddDefaulted_GetRef();
	CacheFrame.LocalToWorld = Helper.SystemInstance->GatheredInstanceParameters.ComponentTrans;

	CacheFrame.SystemData.LocalBounds = Helper.SystemInstance->GetLocalBounds();

	const int32 NumEmitters = CacheLayout.EmitterLayouts.Num();
	CacheFrame.EmitterData.AddDefaulted(NumEmitters);

	Helper.WriteDataBuffer(*Helper.SystemSimulationDataBuffer, CacheLayout.SystemLayout, CacheFrame.SystemData.SystemDataBuffers, Helper.SystemInstance->GetSystemInstanceIndex(), 1);

	for ( int32 i=0; i < NumEmitters; ++i )
	{
		FNiagaraSimCacheEmitterFrame& CacheEmitterFrame = CacheFrame.EmitterData[i];
		FNiagaraEmitterInstance& EmitterInstance = Helper.SystemInstance->GetEmitters()[i].Get();
		FNiagaraDataBuffer* EmitterCurrentData = EmitterInstance.GetData().GetCurrentData();
		if (EmitterInstance.IsComplete() || !EmitterCurrentData)
		{
			continue;
		}

		CacheEmitterFrame.LocalBounds = EmitterInstance.GetBounds();
		CacheEmitterFrame.TotalSpawnedParticles = EmitterInstance.GetTotalSpawnedParticles();
		if (CacheLayout.EmitterLayouts[i].SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			Helper.WriteDataBufferGPU(EmitterInstance, *EmitterCurrentData, CacheLayout.EmitterLayouts[i], CacheEmitterFrame.ParticleDataBuffers);
		}
		else
		{
			Helper.WriteDataBuffer(*EmitterCurrentData, CacheLayout.EmitterLayouts[i], CacheEmitterFrame.ParticleDataBuffers, 0, EmitterCurrentData->GetNumInstances());
		}
	}

	// Store data interface data
	//-OPT: We shouldn't need to search all the time here
	if (DataInterfaceStorage.IsEmpty() == false)
	{
		const int FrameIndex = CacheFrames.Num() - 1;
		bool bDataInterfacesSucess = true;

		FNiagaraDataInterfaceUtilities::ForEachDataInterface(
			Helper.SystemInstance,
			[&](const FNiagaraVariableBase& Variable, UNiagaraDataInterface* DataInterface)
			{
				if ( UObject* StorageObject = DataInterfaceStorage.FindRef(Variable) )
				{
					const void* PerInstanceData = Helper.SystemInstance->FindDataInterfaceInstanceData(DataInterface);
					bDataInterfacesSucess &= DataInterface->SimCacheWriteFrame(StorageObject, FrameIndex, Helper.SystemInstance, PerInstanceData);
				}
				return true;
			}
		);

		// A data interface failed to write information
		if (bDataInterfacesSucess == false)
		{
			SoftNiagaraSystem.Reset();
		}
	}
}

void UNiagaraSimCache::EndWrite()
{
	check(PendingCommandsInFlight == 0);
	if ( CacheFrames.Num() == 0 )
	{
		SoftNiagaraSystem.Reset();
	}

	if (DataInterfaceStorage.IsEmpty() == false)
	{
		bool bDataInterfacesSucess = true;
		for ( auto it=DataInterfaceStorage.CreateIterator(); it; ++it )
		{
			UClass* DataInterfaceClass = it.Key().GetType().GetClass();
			check(DataInterfaceClass != nullptr);
			UNiagaraDataInterface* DataInterface = CastChecked<UNiagaraDataInterface>(DataInterfaceClass->GetDefaultObject());
			bDataInterfacesSucess &= DataInterface->SimCacheEndWrite(it.Value());
		}

		if (bDataInterfacesSucess == false)
		{
			SoftNiagaraSystem.Reset();
		}
	}
}

bool UNiagaraSimCache::CanRead(UNiagaraSystem* NiagaraSystem)
{
	check(IsInGameThread());

	if ( NiagaraSystem != SoftNiagaraSystem.Get() )
	{
		return false;
	}

	if ( NiagaraSystem->IsReadyToRun() == false )
	{
		return false;
	}

	// Uncooked platforms can recompile the system so we need to detect if a recache is required
	//-OPT: This should use the changed notification delegate to avoid checks
#if WITH_EDITORONLY_DATA
	if ( !bNeedsReadComponentMappingRecache )
	{
		uint32 CacheVMIndex = 0;
		NiagaraSystem->ForEachScript(
			[&](UNiagaraScript* Script)
			{
				if (CachedScriptVMIds.IsValidIndex(CacheVMIndex))
				{
					bNeedsReadComponentMappingRecache |= CachedScriptVMIds[CacheVMIndex] != Script->GetVMExecutableDataCompilationId();
				}
				else
				{
					bNeedsReadComponentMappingRecache = true;
				}
			}
		);
	}
#endif

	if (bNeedsReadComponentMappingRecache)
	{
		const int32 NumEmitters = NiagaraSystem->GetEmitterHandles().Num();
		if (NumEmitters != CacheLayout.EmitterLayouts.Num())
		{
			return false;
		}

		bool bCacheValid = true;
		bCacheValid &= FNiagaraSimCacheHelper::BuildCacheReadMappings(CacheLayout.SystemLayout, NiagaraSystem->GetSystemCompiledData().DataSetCompiledData);

		for ( int32 i=0; i < NumEmitters; ++i )
		{
			const FNiagaraEmitterCompiledData& EmitterCompiledData = NiagaraSystem->GetEmitterCompiledData()[i].Get();
			bCacheValid &= FNiagaraSimCacheHelper::BuildCacheReadMappings(CacheLayout.EmitterLayouts[i], EmitterCompiledData.DataSetCompiledData);
		}

		if (bCacheValid == false)
		{
			return false;
		}

#if WITH_EDITORONLY_DATA
		// Gather all the CachedScriptVMIds
		CachedScriptVMIds.Empty();
		NiagaraSystem->ForEachScript(
			[&](UNiagaraScript* Script)
			{
				CachedScriptVMIds.Add(Script->GetVMExecutableDataCompilationId());
			}
		);
		CachedScriptVMIds.Shrink();
#endif
		bNeedsReadComponentMappingRecache = false;
	}

	return true;
}

bool UNiagaraSimCache::Read(float TimeSeconds, FNiagaraSystemInstance* SystemInstance) const
{
	const float RelativeTime = FMath::Max(TimeSeconds - StartSeconds, 0.0f);
	if ( RelativeTime > DurationSeconds )
	{
		// Complete
		return false;
	}

	const float FrameTime		= (RelativeTime / DurationSeconds) * float(CacheFrames.Num() - 1);
	const float FrameIndex		= FMath::Floor(FrameTime);
	const float FrameFraction	= FrameTime - float(FrameIndex);

	return ReadFrame(FrameIndex, FrameFraction, SystemInstance);
}

bool UNiagaraSimCache::ReadFrame(int32 FrameIndex, float FrameFraction, FNiagaraSystemInstance* SystemInstance) const
{
	FNiagaraSimCacheHelper Helper(SystemInstance);
	if ( !Helper.HasValidSimulation() )
	{
		return false;
	}

	const FNiagaraSimCacheFrame& CacheFrame = CacheFrames[FrameIndex];

	FTransform RebaseTransform = FTransform::Identity;
	if ( USceneComponent* AttachComponent = SystemInstance->GetAttachComponent() )
	{
		RebaseTransform = AttachComponent->GetComponentToWorld();
		RebaseTransform.AddToTranslation(FVector(SystemInstance->GetLWCTile()) * -FLargeWorldRenderScalar::GetTileSize());
		RebaseTransform = CacheFrame.LocalToWorld * RebaseTransform;
	}

	Helper.SystemInstance->LocalBounds = CacheFrame.SystemData.LocalBounds;
	Helper.ReadDataBuffer(RebaseTransform, CacheLayout.SystemLayout, CacheFrame.SystemData.SystemDataBuffers, Helper.GetSystemSimulationDataSet());

	const int32 NumEmitters = CacheLayout.EmitterLayouts.Num();
	for (int32 i=0; i < NumEmitters; ++i)
	{
		const FNiagaraSimCacheEmitterFrame& CacheEmitterFrame = CacheFrame.EmitterData[i];
		FNiagaraEmitterInstance& EmitterInstance = Helper.SystemInstance->GetEmitters()[i].Get();
		EmitterInstance.CachedBounds = CacheEmitterFrame.LocalBounds;
		EmitterInstance.TotalSpawnedParticles = CacheEmitterFrame.TotalSpawnedParticles;

		if (CacheLayout.EmitterLayouts[i].SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			Helper.ReadDataBufferGPU(RebaseTransform, EmitterInstance, CacheLayout.EmitterLayouts[i], CacheEmitterFrame.ParticleDataBuffers, EmitterInstance.GetData(), PendingCommandsInFlight);
		}
		else
		{
			Helper.ReadDataBuffer(RebaseTransform, CacheLayout.EmitterLayouts[i], CacheEmitterFrame.ParticleDataBuffers, EmitterInstance.GetData());
		}
	}

	// Store data interface data
	//-OPT: We shouldn't need to search all the time here
	if (DataInterfaceStorage.IsEmpty() == false)
	{
		const int NextFrameIndex = FMath::Min(FrameIndex + 1, CacheFrames.Num() - 1);
		bool bDataInterfacesSucess = true;

		FNiagaraDataInterfaceUtilities::ForEachDataInterface(
			Helper.SystemInstance,
			[&](const FNiagaraVariableBase& Variable, UNiagaraDataInterface* DataInterface)
			{
				if (UObject* StorageObject = DataInterfaceStorage.FindRef(Variable))
				{
					void* PerInstanceData = Helper.SystemInstance->FindDataInterfaceInstanceData(DataInterface);
					bDataInterfacesSucess &= DataInterface->SimCacheReadFrame(StorageObject, FrameIndex, NextFrameIndex, FrameFraction, Helper.SystemInstance, PerInstanceData);
				}
				return true;
			}
		);


		if (bDataInterfacesSucess == false)
		{
			return false;
		}
	}

	//-TODO: This should loop over all DataInterfaces that register not just ones with instance data
	for (TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>& DataInterfacePair : SystemInstance->DataInterfaceInstanceDataOffsets)
	{
		if (UNiagaraDataInterface* Interface = DataInterfacePair.Key.Get())
		{
			Interface->SimCachePostReadFrame(&SystemInstance->DataInterfaceInstanceData[DataInterfacePair.Value], SystemInstance);
		}
	}
	return true;
}
