// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonVector.h"

const FGLTFJsonVector2 TGLTFJsonVector<FGLTFVector2>::Zero({ 0, 0 });
const FGLTFJsonVector2 TGLTFJsonVector<FGLTFVector2>::One ({ 1, 1 });

const FGLTFJsonVector3 TGLTFJsonVector<FGLTFVector3>::Zero({ 0, 0, 0 });
const FGLTFJsonVector3 TGLTFJsonVector<FGLTFVector3>::One ({ 1, 1, 1 });

const FGLTFJsonVector4 TGLTFJsonVector<FGLTFVector4>::Zero({ 0, 0, 0, 0 });
const FGLTFJsonVector4 TGLTFJsonVector<FGLTFVector4>::One ({ 1, 1, 1, 1 });
