// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraSimCacheViewModel.h"
#include "NiagaraComponent.h"
#include "NiagaraSimCache.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraDebuggerCommon.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#define LOCTEXT_NAMESPACE "NiagaraSimCacheViewModel"

FNiagaraSimCacheViewModel::FNiagaraSimCacheViewModel()
{
}

FNiagaraSimCacheViewModel::~FNiagaraSimCacheViewModel()
{
	UNiagaraSimCache::OnCacheEndWrite.RemoveAll(this);
	bDelegatesAdded = false;
}

void FNiagaraSimCacheViewModel::Initialize(TWeakObjectPtr<UNiagaraSimCache> SimCache) 
{
	if (bDelegatesAdded == false)
	{
		bDelegatesAdded = true;
		UNiagaraSimCache::OnCacheEndWrite.AddSP(this, &FNiagaraSimCacheViewModel::OnCacheModified);
	}

	WeakSimCache = SimCache;
	UpdateCachedFrame();
	SetupPreviewComponentAndInstance();
	OnViewDataChangedDelegate.Broadcast(true);
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

void FNiagaraSimCacheViewModel::SetupPreviewComponentAndInstance()
{
	UNiagaraSimCache* SimCache = WeakSimCache.Get();
	UNiagaraSystem* System = SimCache ? SimCache->GetSystem(true) : nullptr;

	if(SimCache && System)
	{
		PreviewComponent = NewObject<UNiagaraComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		PreviewComponent->CastShadow = 1;
		PreviewComponent->bCastDynamicShadow = 1;
		PreviewComponent->SetAllowScalability(false);
		PreviewComponent->SetAsset(System);
		PreviewComponent->SetForceSolo(true);
		PreviewComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
		PreviewComponent->SetCanRenderWhileSeeking(false);
		PreviewComponent->Activate(true);
		PreviewComponent->SetSimCache(SimCache);
		PreviewComponent->SetRelativeLocation(FVector::ZeroVector);
		PreviewComponent->SetDesiredAge(SimCache->GetStartSeconds());
	}

	
}

FText FNiagaraSimCacheViewModel::GetComponentText(const FName ComponentName, const int32 InstanceIndex) const
{
	const FComponentInfo* ComponentInfo = ComponentInfos.FindByPredicate([ComponentName](const FComponentInfo& FoundInfo) { return FoundInfo.Name == ComponentName; });

	if (ComponentInfo)
	{
		if (InstanceIndex >= 0 && InstanceIndex < NumInstances)
		{
			if (ComponentInfo->bIsFloat)
			{
				const float Value = FloatComponents[(ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex];
				return FText::AsNumber(Value);
			}
			else if (ComponentInfo->bIsHalf)
			{
				const FFloat16 Value = HalfComponents[(ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex];
				return FText::AsNumber(Value.GetFloat());
			}
			else if (ComponentInfo->bIsInt32)
			{
				const int32 Value = Int32Components[(ComponentInfo->ComponentOffset * NumInstances) + InstanceIndex];
				if (ComponentInfo->bShowAsBool)
				{
					return Value == 0 ? LOCTEXT("False", "False") : LOCTEXT("True", "True");
				}
				else if (ComponentInfo->Enum != nullptr)
				{
					return ComponentInfo->Enum->GetDisplayNameTextByValue(Value);
				}
				else
				{
					return FText::AsNumber(Value);
				}
			}
		}
	}
	return LOCTEXT("Error", "Error");
}

int32 FNiagaraSimCacheViewModel::GetNumFrames() const
{
	UNiagaraSimCache* SimCache = WeakSimCache.Get();
	return SimCache ? SimCache->GetNumFrames() : 0;
}

void FNiagaraSimCacheViewModel::SetFrameIndex(const int32 InFrameIndex)
{
	FrameIndex = InFrameIndex;
	UpdateCachedFrame();
	const UNiagaraSimCache* SimCache = WeakSimCache.Get();
	if(PreviewComponent && SimCache)
	{
		const float Duration = SimCache->GetDurationSeconds();
		const int NumFrames = SimCache->GetNumFrames();

		const float NormalizedFrame = NumFrames == 0 ? 0.0f : float(InFrameIndex) / float(NumFrames - 1);
		
		PreviewComponent->SetDesiredAge(SimCache->GetStartSeconds() + (Duration * NormalizedFrame));
	}
	OnViewDataChangedDelegate.Broadcast(false);
}

void FNiagaraSimCacheViewModel::SetEmitterIndex(const int32 InEmitterIndex)
{
	EmitterIndex = InEmitterIndex;
	UpdateCachedFrame();
	OnViewDataChangedDelegate.Broadcast(false);
}

bool FNiagaraSimCacheViewModel::IsCacheValid()
{
	UNiagaraSimCache* SimCache = WeakSimCache.Get();
	return SimCache ? SimCache->IsCacheValid() : false;
}

int32 FNiagaraSimCacheViewModel::GetNumEmitterLayouts()
{
	UNiagaraSimCache* SimCache = WeakSimCache.Get();
	return SimCache ? SimCache->GetNumEmitters() : 0;
}

FName FNiagaraSimCacheViewModel::GetEmitterLayoutName(const int32 Index)
{
	UNiagaraSimCache* SimCache = WeakSimCache.Get();
	return SimCache ? SimCache->GetEmitterName(Index) : NAME_None;
}

FNiagaraSimCacheViewModel::FOnViewDataChanged& FNiagaraSimCacheViewModel::OnViewDataChanged()
{
	return OnViewDataChangedDelegate;
}

void FNiagaraSimCacheViewModel::OnCacheModified(UNiagaraSimCache* SimCache)
{
	if ( UNiagaraSimCache* ThisSimCache = WeakSimCache.Get() )
	{
		if ( ThisSimCache == SimCache )
		{
			UpdateCachedFrame();
			OnViewDataChangedDelegate.Broadcast(true);
		}
	}
}

void FNiagaraSimCacheViewModel::UpdateCachedFrame()
{
	NumInstances = 0;
	ComponentInfos.Empty();
	FloatComponents.Empty();
	HalfComponents.Empty();
	Int32Components.Empty();

	UNiagaraSimCache* SimCache = WeakSimCache.Get();
	if (SimCache == nullptr)
	{
		return;
	}

	if ( FrameIndex < 0 || FrameIndex >= SimCache->GetNumFrames() )
	{
		return;
	}

	if ( EmitterIndex != INDEX_NONE && EmitterIndex >= SimCache->GetNumEmitters() )
	{
		return;
	}

	FoundFloatComponents = 0;
	FoundHalfComponents = 0;
	FoundInt32Components = 0;

	NumInstances = EmitterIndex == INDEX_NONE ? 1 : SimCache->GetEmitterNumInstances(EmitterIndex, FrameIndex);
	const FName EmitterName = EmitterIndex == INDEX_NONE ? NAME_None : SimCache->GetEmitterName(EmitterIndex);

	SimCache->ForEachEmitterAttribute(EmitterIndex,
		[&](const FNiagaraSimCacheVariable& Variable)
		{
			// Build component info
			const FNiagaraTypeDefinition& TypeDef = Variable.Variable.GetType();
			if (TypeDef.IsEnum())
			{
				FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
				ComponentInfo.Name = Variable.Variable.GetName();
				ComponentInfo.ComponentOffset = FoundInt32Components++;
				ComponentInfo.bIsInt32 = true;
				ComponentInfo.Enum = TypeDef.GetEnum();
			}
			else
			{
				BuildComponentInfos(Variable.Variable.GetName(), TypeDef.GetScriptStruct());
			}

			// Pull in data
			SimCache->ReadAttribute(FloatComponents, HalfComponents, Int32Components, Variable.Variable.GetName(), EmitterName, FrameIndex);

			return true;
		}
	);
}

void FNiagaraSimCacheViewModel::BuildComponentInfos(const FName Name, const UScriptStruct* Struct)
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
			FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundFloatComponents++;
			ComponentInfo.bIsFloat = true;
		}
		else if (Property->IsA(FUInt16Property::StaticClass()))
		{
			FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundHalfComponents++;
			ComponentInfo.bIsHalf = true;
		}
		else if (Property->IsA(FIntProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundInt32Components++;
			ComponentInfo.bIsInt32 = true;
			ComponentInfo.bShowAsBool = (NumProperties == 1) && (Struct == FNiagaraTypeDefinition::GetBoolStruct());
		}
		else if (Property->IsA(FBoolProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundInt32Components++;
			ComponentInfo.bIsInt32 = true;
			ComponentInfo.bShowAsBool = true;
		}
		else if (Property->IsA(FEnumProperty::StaticClass()))
		{
			FComponentInfo& ComponentInfo = ComponentInfos.AddDefaulted_GetRef();
			ComponentInfo.Name = PropertyName;
			ComponentInfo.ComponentOffset = FoundInt32Components++;
			ComponentInfo.bIsInt32 = true;
			ComponentInfo.Enum = CastFieldChecked<FEnumProperty>(Property)->GetEnum();
		}
		else if (Property->IsA(FStructProperty::StaticClass()))
		{
			const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Property);
			BuildComponentInfos(PropertyName, FNiagaraTypeHelper::FindNiagaraFriendlyTopLevelStruct(StructProperty->Struct, ENiagaraStructConversion::Simulation));
		}
		else
		{
			// Fail
		}
	}
}

#undef LOCTEXT_NAMESPACE
