// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonVector.h"

template <> const FGLTFJsonVector2 FGLTFJsonVector2::Zero({ 0, 0 });
template <> const FGLTFJsonVector2 FGLTFJsonVector2::One ({ 1, 1 });

template <> const FGLTFJsonVector3 FGLTFJsonVector3::Zero({ 0, 0, 0 });
template <> const FGLTFJsonVector3 FGLTFJsonVector3::One ({ 1, 1, 1 });

template <> const FGLTFJsonVector4 FGLTFJsonVector4::Zero({ 0, 0, 0, 0 });
template <> const FGLTFJsonVector4 FGLTFJsonVector4::One ({ 1, 1, 1, 1 });
