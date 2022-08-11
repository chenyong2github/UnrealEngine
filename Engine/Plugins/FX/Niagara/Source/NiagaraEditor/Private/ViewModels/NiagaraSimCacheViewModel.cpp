// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraSimCacheViewModel.h"
#include "NiagaraSimCache.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraDebuggerCommon.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#define LOCTEXT_NAMESPACE "NiagaraSimCacheViewModel"

struct FSimCacheBufferReader
{

	explicit FSimCacheBufferReader(TWeakObjectPtr<UNiagaraSimCache> SimCache, uint32 InFrameIndex = 0, uint32 InEmitterIndex = INDEX_NONE)
		: WeakSimCache(SimCache)
	{
		UpdateLayout(InFrameIndex, InEmitterIndex);
	}

	void UpdateLayout(const uint32 FrameIndex, const uint32 EmitterIndex)
	{
		ComponentInfos.Empty();
		FoundFloats = 0;
		FoundHalfs = 0;
		FoundInt32s = 0;

		if (const FNiagaraSimCacheDataBuffersLayout* SimCacheLayout = GetSimCacheBufferLayout(FrameIndex, EmitterIndex))
		{
			for (const FNiagaraSimCacheVariable& Variable : SimCacheLayout->Variables)
			{
				const FNiagaraTypeDefinition& TypeDef = Variable.Variable.GetType();
				if (TypeDef.IsEnum())
				{
					FNiagaraSimCacheComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
					ComponentInfo.Name = Variable.Variable.GetName();
					ComponentInfo.ComponentOffset = FoundInt32s++;
					ComponentInfo.bIsInt32 = true;
					ComponentInfo.Enum = TypeDef.GetEnum();
				}
				else
				{
					Build(Variable.Variable.GetName(), TypeDef.GetScriptStruct());
				}
			}

			if ((FoundFloats != SimCacheLayout->FloatCount) ||
				(FoundHalfs != SimCacheLayout->HalfCount) ||
				(FoundInt32s != SimCacheLayout->Int32Count))
			{
				UE_LOG(LogNiagaraEditor, Warning, TEXT("SimCache Layout doesn't appear to match iterating the variables"));
				ComponentInfos.Empty();
			}
		}
	}

	TConstArrayView<FNiagaraSimCacheComponentInfo> GetComponentInfos() const
	{
		return MakeArrayView(ComponentInfos);
	}
	
	UNiagaraSimCache* GetSimCache() const
	{
		return WeakSimCache.Get();
	}

	const FNiagaraSimCacheDataBuffersLayout* GetSimCacheBufferLayout(const uint32 FrameIndex, const uint32 EmitterIndex) const
	{
		if (UNiagaraSimCache* const SimCache = WeakSimCache.Get())
		{
			if (SimCache->CacheFrames.IsValidIndex(FrameIndex))
			{
				if (EmitterIndex == INDEX_NONE)
				{
					return &SimCache->CacheLayout.SystemLayout;
				}
				else if (SimCache->CacheLayout.EmitterLayouts.IsValidIndex(EmitterIndex))
				{
					return &SimCache->CacheLayout.EmitterLayouts[EmitterIndex];
				}
			}
		}
		return nullptr;
	}

	const FNiagaraSimCacheDataBuffers* GetSimCacheDataBuffers(const uint32 FrameIndex, const uint32 EmitterIndex) const
	{
		if (UNiagaraSimCache* SimCache = WeakSimCache.Get())
		{
			if (SimCache->CacheFrames.IsValidIndex(FrameIndex))
			{
				if (EmitterIndex == INDEX_NONE)
				{
					return &SimCache->CacheFrames[FrameIndex].SystemData.SystemDataBuffers;
				}
				else if (SimCache->CacheFrames[FrameIndex].EmitterData.IsValidIndex(EmitterIndex))
				{
					return &SimCache->CacheFrames[FrameIndex].EmitterData[EmitterIndex].ParticleDataBuffers;
				}
			}
		}
		return nullptr;
	}

	FText GetComponentText(FName ComponentName, int32 InstanceIndex, uint32 FrameIndex, uint32 EmitterIndex) const
	{
		const FNiagaraSimCacheComponentInfo* ComponentInfo = ComponentInfos.FindByPredicate([ComponentName](const FNiagaraSimCacheComponentInfo& Info) { return Info.Name == ComponentName; });
		const FNiagaraSimCacheDataBuffers* SimCacheDataBuffers = GetSimCacheDataBuffers(FrameIndex, EmitterIndex);
	
		if (ComponentInfo && SimCacheDataBuffers)
		{
			const int32 NumInstances = SimCacheDataBuffers->NumInstances;
			if (InstanceIndex >= 0 && InstanceIndex < NumInstances)
			{
				if (ComponentInfo->bIsFloat)
				{
					const float* Value = reinterpret_cast<const float*>(SimCacheDataBuffers->FloatData.GetData()) + (ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex;
					return FText::AsNumber(*Value);
				}
				else if (ComponentInfo->bIsHalf)
				{
					const FFloat16* Value = reinterpret_cast<const FFloat16*>(SimCacheDataBuffers->HalfData.GetData()) + (ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex;
					return FText::AsNumber(Value->GetFloat());
				}
				else if (ComponentInfo->bIsInt32)
				{
					const int32* Value = reinterpret_cast<const int32*>(SimCacheDataBuffers->Int32Data.GetData()) + (ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex;
					if (ComponentInfo->bShowAsBool)
					{
						return *Value == 0 ? LOCTEXT("False", "False") : LOCTEXT("True", "True");
					}
					else if (ComponentInfo->Enum != nullptr)
					{
						return ComponentInfo->Enum->GetDisplayNameTextByValue(*Value);
					}
					else
					{
						return FText::AsNumber(*Value);
					}
				}
			}
		}
		return LOCTEXT("Error", "Error");
	}

protected:
	TWeakObjectPtr<UNiagaraSimCache>	WeakSimCache;

	TArray<FNiagaraSimCacheComponentInfo>				ComponentInfos;
	uint32								FoundFloats = 0;
	uint32								FoundHalfs = 0;
	uint32								FoundInt32s = 0;

	void Build(const FName Name, const UScriptStruct* Struct)
	{
		int32 NumProperties = 0;
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			++NumProperties;
		}

		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			const FName PropertyName = NumProperties > 1 ? FName(*FString::Printf(TEXT("%s.%s"), *Name.ToString(), *Property->GetName())) : Name;
			if (Property->IsA(FFloatProperty::StaticClass()))
			{
				FNiagaraSimCacheComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = PropertyName;
				ComponentInfo.ComponentOffset = FoundFloats++;
				ComponentInfo.bIsFloat = true;
			}
			else if (Property->IsA(FUInt16Property::StaticClass()))
			{
				FNiagaraSimCacheComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = PropertyName;
				ComponentInfo.ComponentOffset = FoundHalfs++;
				ComponentInfo.bIsHalf = true;
			}
			else if (Property->IsA(FIntProperty::StaticClass()))
			{
				FNiagaraSimCacheComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = PropertyName;
				ComponentInfo.ComponentOffset = FoundInt32s++;
				ComponentInfo.bIsInt32 = true;
				ComponentInfo.bShowAsBool = (NumProperties == 1) && (Struct == FNiagaraTypeDefinition::GetBoolStruct());
			}
			else if (Property->IsA(FBoolProperty::StaticClass()))
			{
				FNiagaraSimCacheComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = PropertyName;
				ComponentInfo.ComponentOffset = FoundInt32s++;
				ComponentInfo.bIsInt32 = true;
				ComponentInfo.bShowAsBool = true;
			}
			else if (Property->IsA(FEnumProperty::StaticClass()))
			{
				FNiagaraSimCacheComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = PropertyName;
				ComponentInfo.ComponentOffset = FoundInt32s++;
				ComponentInfo.bIsInt32 = true;
				ComponentInfo.Enum = CastFieldChecked<FEnumProperty>(Property)->GetEnum();
			}
			else if (Property->IsA(FStructProperty::StaticClass()))
			{
				const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Property);
				Build(PropertyName, FNiagaraTypeHelper::FindNiagaraFriendlyTopLevelStruct(StructProperty->Struct, ENiagaraStructConversion::Simulation));
			}
			else
			{
				// Fail
			}
		}
	}
};

TSharedPtr<FSimCacheBufferReader> BufferReader;

FNiagaraSimCacheViewModel::FNiagaraSimCacheViewModel()
{
	
}

FNiagaraSimCacheViewModel::~FNiagaraSimCacheViewModel()
{

}

void FNiagaraSimCacheViewModel::Initialize(TWeakObjectPtr<UNiagaraSimCache> SimCache) 
{
	BufferReader = MakeShared<FSimCacheBufferReader>(SimCache);
	BufferReader->UpdateLayout(FrameIndex, EmitterIndex);
}

void FNiagaraSimCacheViewModel::UpdateSimCache(const FNiagaraSystemSimCacheCaptureReply& Reply)
{
	UNiagaraSimCache* SimCache = nullptr;
	
	if (Reply.SimCacheData.Num() > 0)
	{
		SimCache = NewObject<UNiagaraSimCache>();

		FMemoryReader ArReader(Reply.SimCacheData);
		FObjectAndNameAsStringProxyArchive ProxyArReader(ArReader, false);
		SimCache->Serialize(ProxyArReader);
	}
	else
	{
		UE_LOG(LogNiagaraEditor, Warning, TEXT("Debug Spreadsheet recieved empty sim cache data."));
	}
	Initialize(SimCache);
}

FText FNiagaraSimCacheViewModel::GetComponentText(const FName ComponentName, const int32 InstanceIndex) const
{
	return BufferReader ? BufferReader->GetComponentText(ComponentName, InstanceIndex, FrameIndex, EmitterIndex) : FText();
}

TConstArrayView<FNiagaraSimCacheComponentInfo> FNiagaraSimCacheViewModel::GetComponentInfos() const
{
	return BufferReader ? BufferReader->GetComponentInfos() : TConstArrayView<FNiagaraSimCacheComponentInfo>();
}

int32 FNiagaraSimCacheViewModel::GetNumInstances() const
{
	const FNiagaraSimCacheDataBuffers* DataBuffer = BufferReader ? BufferReader->GetSimCacheDataBuffers(FrameIndex, EmitterIndex) : nullptr;
	return DataBuffer ? DataBuffer->NumInstances : 0;
}

int32 FNiagaraSimCacheViewModel::GetNumFrames() const
{
	const UNiagaraSimCache* SimCache = BufferReader ? BufferReader->GetSimCache() : nullptr;
	return SimCache ? SimCache->CacheFrames.Num() : 0;
}


void FNiagaraSimCacheViewModel::SetEmitterIndex(const int32 InEmitterIndex)
{
	EmitterIndex = InEmitterIndex;
	BufferReader->UpdateLayout(FrameIndex, EmitterIndex);
}

const FNiagaraSimCacheDataBuffersLayout* FNiagaraSimCacheViewModel::GetSimCacheBufferLayout() const
{
	return BufferReader ? BufferReader->GetSimCacheBufferLayout(FrameIndex, EmitterIndex) : nullptr;
}

bool FNiagaraSimCacheViewModel::IsCacheValid()
{
	return BufferReader ? BufferReader->GetSimCache()->IsCacheValid() : false;
}

int32 FNiagaraSimCacheViewModel::GetNumEmitterLayouts()
{
	const UNiagaraSimCache* SimCache = BufferReader ? BufferReader->GetSimCache() : nullptr;
	return SimCache ? SimCache->CacheLayout.EmitterLayouts.Num() : 0;
}

FName FNiagaraSimCacheViewModel::GetEmitterLayoutName(const int32 Index)
{
	const UNiagaraSimCache* SimCache = BufferReader ? BufferReader->GetSimCache() : nullptr;
	return SimCache ? SimCache->CacheLayout.EmitterLayouts[Index].LayoutName : NAME_None;
}

#undef LOCTEXT_NAMESPACE