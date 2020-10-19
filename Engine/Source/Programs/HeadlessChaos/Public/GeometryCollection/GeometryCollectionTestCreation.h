// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GeometryCollectionTest.h"

namespace GeometryCollectionTest
{	
	template<class T>
	void  CheckIncrementMask();

	template<class T>
	void  Creation();

	template<class T>
	void  Empty();

	template<class T>
	void  AppendTransformHierarchy();

	template<class T>
	void  ContiguousElementsTest();

	template<class T>
	void  DeleteFromEnd();

	template<class T>
	void  DeleteFromStart();

	template<class T>
	void  DeleteFromMiddle();

	template<class T>
	void  DeleteBranch();

	template<class T>
	void  DeleteRootLeafMiddle();

	template<class T>
	void  DeleteEverything();

	template<class T>
	void  ParentTransformTest();

	template<class T>
	void  ReindexMaterialsTest();

	template<class T>
	void  AttributeTransferTest();

	template<class T>
	void  AttributeDependencyTest();

}
