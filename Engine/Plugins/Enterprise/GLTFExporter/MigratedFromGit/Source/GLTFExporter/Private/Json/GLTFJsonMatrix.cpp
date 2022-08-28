// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonMatrix.h"

template <> const FGLTFJsonMatrix2 FGLTFJsonMatrix2::Identity
({
	1, 0,
	0, 1
});

template <> const FGLTFJsonMatrix3 FGLTFJsonMatrix3::Identity
({
	1, 0, 0,
	0, 1, 0,
	0, 0, 1
});

template <> const FGLTFJsonMatrix4 FGLTFJsonMatrix4::Identity
({
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
});
