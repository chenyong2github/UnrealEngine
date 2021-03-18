// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNodeMessages.h"

class UMirrorDataTable;

namespace UE { namespace Anim {

class FAnimSyncGroupScope;

// Scoped graph message used to synchronize mirroring 
class ENGINE_API FMirrorSyncScope : public IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(FMirrorSyncScope);
public:
	FMirrorSyncScope(const FAnimationBaseContext& InContext, const UMirrorDataTable* InMirrorDataTable);
	virtual ~FMirrorSyncScope();
	int32 GetMirrorScopeDepth() const;
	
private:
	const UMirrorDataTable* MirrorDataTable = nullptr;
	int32 MirrorScopeDepth = 1; 
	const UMirrorDataTable* OuterScopeMirrorDataTable = nullptr;
	FAnimSyncGroupScope* AnimSyncGroupScope = nullptr; 
};

}}	// namespace UE::Anim
