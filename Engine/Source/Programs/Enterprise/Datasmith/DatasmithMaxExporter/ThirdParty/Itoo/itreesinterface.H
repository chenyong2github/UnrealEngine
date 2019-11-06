/**********************************************************************
FILE: itreesinterface.h
DESCRIPTION: Tree interface
CREATED BY: DQG
HISTORY:	18/07/2009- First Version
			14/10/2010- Moved to idependent file
			06/11/2011- CQ - Added fullTM access
			07/12/2011- CQ - Added updateUI
			30/01/2012- CQ - Added forest_getrenderid
			12/04/2012- CQ - Added forest_getrendernode
			16/09/2012- CQ - Added forest_getrendernodes, forest_clearrendernodes, forest_registerengine
			16/12/2012- CQ - Added forest_renderbegin, forest_renderend

	Copyright (c) 2012, iToo Software. All Rights Reserved.

************************************************************************

The following interface can be used by third party render engines to instanciate the Forest items, in the following way:

1) Using the static ForestPack interface, Register the current render engine as supported for instancing.
   Strictly, this funcion only need to be inkoked one time by Max session, but it's ok if you call it more times:
	
		IForestPackInterface *fpi = GetForestPackInterface();
		fpi->IForestRegisterEngine();

At the rendering loop, repeat for each Forest object:

2) Get a pointer to the ITreesInterface interface:

		ITreesInterface *itrees = GetTreesInterface(node->GetObjectRef());
		if (itrees)
			{
	    ...
			}

3) Call to "RenderBegin". It prepares the object for rendering. If you have some pre-process rendering phase, make the call from there.

	 Alternatively you can call to "itrees->IForestRenderBegin(t)". This is a light version of RenderBegin, which doesn't generate Max notifications.
	 In some cases (as ActiveShade modes), Max notifications would cause infinite build loops.

4) Call to "GetRenderMesh". It rebuilds the object properly for rendering.
	 The returned mesh will hold the geometry for billboards, which are not instantiable. If you want provide support for billboards, render this mesh as a standard object.
	 Also in case that Display->Render->Mode is set to "Meshes", instead the "Automatic" default value, the mesh will hold the geometry of all Forest items.
	 (basically this parameter is used to disble the geometry shader of each render engine)

5) Generate the array of instances and get a pointer to it:

	int numInstances;
	TForestInstance *fpi = (TForestInstance *) itrees->IForestGetRenderNodes(numInstances);
	if (fpi && numInstances)
		{
		for (int i = 0; i < numInstances; i++, fpi++)
			{
			if(fpi->node)
				{
				...
				}
			}
		}

	The number of instances will be stored in "numInstances". If the functions fails for some reason, or the are not instantiable items
	it will return NULL. 
	
	Each "TForestInstance" stores full information about the instance, incluing the source INode, transformation matrix and more. See the 
	class definition below in this header file. Note: In some cases TForestInstance->node would be NULL, you must handle this case and skip it.
	The transformation matrix is in local coordinates of the Forest object. Just multiply it by the INode TM to get the world coordinates of the instance.

6) Clear the array:

	itrees->IForestClearRenderNodes();

7) At the render's end, call to "RenderEnd". This function sets the object for the viewport, clearing the rendering data.
	 If you used on step 3 IForestRenderBegin, use instead "itrees->IForestRenderEnd(t)" here.

**********************************************************************/

#ifndef __TREESINTERFACE__H
#define __TREESINTERFACE__H

#include "ifnpub.h"

// Forest Class_ID
#define TFOREST_CLASS_ID	Class_ID(0x79fe41c2, 0x1d7b4fb1)

///////////////////////////////////////////////////////////////////////////////////////////
// Forest Trees Interface

#define FOREST_TREE_INTERFACE Interface_ID(0x6afe7c5e, 0x15813e00)

#define GetTreesInterface(obj) ((ITreesInterface*)obj->GetInterface(FOREST_TREE_INTERFACE))

enum { forest_create = 0, forest_delete, forest_edit, forest_count, forest_move, forest_rotate, forest_getposition, forest_getwidth, forest_getheight, forest_getsize,
	forest_getrotation, forest_getspecid, forest_getseed, forest_setposition, forest_setrotation, forest_setwidth, forest_setheight, forest_setsize, forest_setspecid, 
	forest_setseed, forest_update, forest_getfulltm, forest_update_ui, forest_getrenderid, forest_getrendernode, forest_getrendernodes, forest_clearrendernodes,
	forest_renderbegin, forest_renderend };

class TForestInstance 
	{
	public:
	Matrix3 tm;						// full transformation for the instance
	INode *node;					// source INode 
	Mtl *mtl;							// material
	int seed;							// an unique seed value
	Color color;					// color for random tint
	float colorMult;			// multiplier for the random color (0 to 1)
	};

class ITreesInterface : public FPMixinInterface {
	BEGIN_FUNCTION_MAP
	VFN_4(forest_create, IForestCreate, TYPE_POINT3, TYPE_FLOAT, TYPE_FLOAT, TYPE_INT);
	VFN_1(forest_delete, IForestDelete, TYPE_INT);
	RO_PROP_FN(forest_count, IForestCount, TYPE_INT);
	VFN_5(forest_edit, IForestEdit, TYPE_INT, TYPE_FLOAT, TYPE_FLOAT, TYPE_INT, TYPE_INT);
	VFN_2(forest_move, IForestMove, TYPE_INT, TYPE_POINT3);
	VFN_2(forest_setposition, IForestMove, TYPE_INT, TYPE_POINT3);
	VFN_2(forest_setrotation, IForestRotate, TYPE_INT, TYPE_POINT3);
	VFN_2(forest_setwidth, IForestSetWidth, TYPE_INT, TYPE_FLOAT);
	VFN_2(forest_setheight, IForestSetHeight, TYPE_INT, TYPE_FLOAT);
	VFN_2(forest_setsize, IForestSetSize, TYPE_INT, TYPE_POINT3);
	VFN_2(forest_setspecid, IForestSetSpecID, TYPE_INT, TYPE_INT);
	VFN_2(forest_setseed, IForestSetSeed, TYPE_INT, TYPE_INT);
	FN_1(forest_getposition, TYPE_POINT3, IForestGetPosition, TYPE_INT);
	FN_1(forest_getwidth, TYPE_FLOAT, IForestGetWidth, TYPE_INT);
	FN_1(forest_getheight, TYPE_FLOAT, IForestGetHeight, TYPE_INT);
	FN_1(forest_getsize, TYPE_POINT3, IForestGetSize, TYPE_INT);
	FN_1(forest_getspecid, TYPE_INT, IForestGetSpecID, TYPE_INT);
	FN_1(forest_getrotation, TYPE_POINT3, IForestGetRotation, TYPE_INT);
	FN_1(forest_getseed, TYPE_INT, IForestGetSeed, TYPE_INT);
	FN_1(forest_getfulltm, TYPE_MATRIX3_BV, IForestGetFullTM, TYPE_INT);
	VFN_0(forest_update, IForestUpdate);
	VFN_0(forest_update_ui, IForestUpdateUI);
	FN_3(forest_getrenderid, TYPE_BOOL, IForestGetRenderID, TYPE_INTPTR, TYPE_FLOAT_BR, TYPE_FLOAT_BR);
	FN_1(forest_getrendernode, TYPE_INODE, IForestGetRenderNode, TYPE_INT);
	FN_1(forest_getrendernodes, TYPE_INTPTR, IForestGetRenderNodes, TYPE_INT);
	VFN_0(forest_clearrendernodes, IForestClearRenderNodes);
	VFN_1(forest_renderbegin, IForestRenderBegin, TYPE_TIMEVALUE);
	VFN_1(forest_renderend, IForestRenderEnd, TYPE_TIMEVALUE);
	END_FUNCTION_MAP

	public:
	virtual void IForestCreate(Point3 p, float width, float height, int specid) = 0;
	virtual void IForestDelete(int n) = 0;
	virtual int IForestCount() = 0;
	virtual void IForestEdit(int n, float width, float height, int specid, int seed) = 0;
	virtual void IForestMove(int n, Point3 p) = 0;
	virtual void IForestRotate(int n, Point3 r) = 0;
	virtual void IForestSetWidth(int n, float width) = 0;
	virtual void IForestSetHeight(int n, float height) = 0;
	virtual void IForestSetSize(int n, Point3 s) = 0;
	virtual void IForestSetSpecID(int, int specid) = 0;
	virtual void IForestSetSeed(int, int seed) = 0;
	virtual Point3 *IForestGetPosition(int n) = 0;
	virtual float IForestGetWidth(int n) = 0;
	virtual float IForestGetHeight(int n) = 0;
	virtual Point3 *IForestGetSize(int n) = 0;
	virtual Point3 *IForestGetRotation(int n) = 0;
	virtual int IForestGetSpecID(int n) = 0;
	virtual int IForestGetSeed(int n) = 0;
	virtual Matrix3 IForestGetFullTM(int n) = 0;
	virtual void IForestUpdate() = 0;
	virtual void IForestUpdateUI() = 0;
	virtual BOOL IForestGetRenderID(INT_PTR sc, float &itemid, float &elemid) = 0;
	virtual INode *IForestGetRenderNode(int n) = 0;
	virtual INT_PTR IForestGetRenderNodes(int &numNodes) = 0;
	virtual void IForestClearRenderNodes() = 0;
	virtual void IForestRenderBegin(TimeValue t) = 0;
	virtual void IForestRenderEnd(TimeValue t) = 0;

	FPInterfaceDesc* GetDesc();	
};


///////////////////////////////////////////////////////////////////////////////////////////
// ForestPack Static Interface

#define FORESTPACK_INTERFACE Interface_ID(0x5c920a09, 0x6ce40001)

#define GetForestPackInterface()	(IForestPackInterface *) ::GetInterface(GEOMOBJECT_CLASS_ID, TFOREST_CLASS_ID, FORESTPACK_INTERFACE)

enum { forest_registerengine };

class IForestPackInterface : public FPStaticInterface {

	public:
	virtual void IForestRegisterEngine() = 0;

	FPInterfaceDesc* GetDesc();	
};

#endif

