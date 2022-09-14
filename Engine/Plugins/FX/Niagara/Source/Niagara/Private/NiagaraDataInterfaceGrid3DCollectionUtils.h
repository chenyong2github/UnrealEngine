// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"

struct FGrid3DCollectionAttributeHelper
{

	// only support rgba textures when we have a single attribute that contains up to 4 channels
	static bool ShouldUseRGBAGrid(const int TotalChannels, const int TotalNumAttributes);

	static TArray<FString> Channels;

	struct FAttributeInfo
	{
		FName					Name;
		FNiagaraTypeDefinition	TypeDef;
		int32					NumChannels = 0;
		int32					ChannelOffset = 0;
		int32					AttributeIndex = 0;
	};

	enum AttributeRetrievalMode
	{
		RGBAGrid = 0,
		Indirection,
		NoIndirection
	};

	explicit FGrid3DCollectionAttributeHelper(const FNiagaraDataInterfaceGPUParamInfo& InParamInfo, TArray<FText>* OutWarnings = nullptr);

	bool UseRGBAGrid();

	const FAttributeInfo* FindAttributeInfo(FName AttributeName) const;

#if WITH_EDITORONLY_DATA
	// Returns pixel offset for the channel
	static FString GetPerAttributePixelOffset(const TCHAR* DataInterfaceHLSLSymbol);
	FString GetPerAttributePixelOffset() const;
	// Returns UVW offset for the channel
	static FString GetPerAttributeUVWOffset(const TCHAR* DataInterfaceHLSLSymbol);
	FString GetPerAttributeUVWOffset() const;

	// Translates named attribute into actual attribute index
	FString GetAttributeIndex(bool bUseAttributeIndirection, const FAttributeInfo* AttributeInfo, int Channel = 0) const;
	FString GetGridChannelString();
	void GetChannelStrings(int AttributeIndex, int AttributeNumChannels, FString& NumChannelsString, FString& AttrGridChannels);
	void GetChannelStrings(const FAttributeInfo* AttributeInfo, FString& NumChannelsString, FString& AttrGridChannels);

	bool WriteGetHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL);
	bool WriteGetAtIndexHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int NumChannels, FString& OutHLSL);
	bool WriteSetAtIndexHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int NumChannels, FString& OutHLSL);
	bool WriteSampleAtIndexHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int NumChannels, bool IsCubic, FString& OutHLSL);
	bool WriteSetHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL);
	bool WriteSampleHLSL(AttributeRetrievalMode AttributeStorage, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, bool IsCubic, FString& OutHLSL);
	bool WriteAttributeGetIndexHLSL(bool bUseAttributeIndirection, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL);
#endif //WITH_EDITORONLY_DATA

	const FNiagaraDataInterfaceGPUParamInfo& ParamInfo;
	TArray<FAttributeInfo>						AttributeInfos;
	int32										TotalChannels = 0;
};
