// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonMatrix.h"

const FGLTFJsonMatrix2 TGLTFJsonMatrix<FGLTFMatrix2>::Identity
({
	1, 0,
	0, 1
});

const FGLTFJsonMatrix3 TGLTFJsonMatrix<FGLTFMatrix3>::Identity
({
	1, 0, 0,
	0, 1, 0,
	0, 0, 1
});

const FGLTFJsonMatrix4 TGLTFJsonMatrix<FGLTFMatrix4>::Identity
({
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
});
