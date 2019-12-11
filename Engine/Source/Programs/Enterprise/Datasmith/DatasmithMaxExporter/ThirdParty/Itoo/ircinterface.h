/**********************************************************************
FILE: rcinterface.h
DESCRIPTION: RailClone interface for external renderers
CREATED BY: CQG
HISTORY:	03/092/2013 - First Version

	Copyright (c) 2013, iToo Software. All Rights Reserved.

************************************************************************

The following interface can be used by third party render engines to instanciate the RailClone items, in the following way:

1) Using the static RailClone interface, Register the current render engine as supported for instancing.
   Strictly, this funcion only need to be inkoked one time by Max session, but it's ok if you call it more times:
	
		IRCStaticInterface *isrc = GetRCStaticInterface();
		isrc->IRCRegisterEngine();

At the rendering loop, repeat for each RailClone object:

2) Get a pointer to the IRCInterface interface:

		IRCInterface *irc = GetRCInterface(node->GetObjectRef());
		if (irc)
			{
	    ...
			}

3) Call to irc->RenderBegin(t). It prepares the object for rendering. If you have some pre-rendering phase, make the call from there.

4) For each segment that can be instanced, RailClone keeps internally a copy of its mesh. Use the following function to get an array of pointers to these meshes:

	int numMeshes;
	Mesh **pmesh = (Mesh **) irc->IRCGetMeshes(numMeshes);
	if (pmesh && numMeshes)
		{
		for (int i = 0; i < numMeshes; i++, pmesh++)
			{
			if (*pmesh)
				{
				Mesh &mesh = *pmesh;
				...
				}
			}
		}

	In the above loop, you must prepare each of these meshes for rendering. Usually converting them to the native geometry format of your engine.
	The number of meshes will be stored in "numMeshes". If the function fails for some reason, or there is not renderable geometry, it will return NULL. 

5) Generate the array of instances (instances of the meshes generated in step 5), and get a pointer to it:

	int numInstances;
	TRCInstance *inst = (TRCInstance *) itrees->IRCGetInstances(numInstances);
	if (inst && numInstances)
		{
		for (int i = 0; i < numInstances; i++, inst++)
			{
			if(inst->mesh)
				{
				...
				}
			}
		}

	The number of instances will be stored in "numInstances". If the function fails for some reason, or there is not renderable geometry, it will return NULL. 
	
	Each "TRCInstance" stores full information about the instance, incluing the source mesh, transformation matrix and more. See the 
	class definition below in this header file. Note: In some cases TRCInstance->mesh would be NULL, you must handle this case and skip it.

	- The transformation matrix is on local coordinates of the RailClone object. Just multiply it by the INode TM to get the world coordinates of the instance.
	- RailClone doesn't apply separated materials to the instances, use the same material assigned to the RailClone object.
	- The first item stores the geometry of the RailClone object that is not instantiable. This item is unique, and uses the first mesh returned by irc->IRCGetMeshes.
	- In case that Display->Render->Use Geometry Shader is off, there will be an unique item holding the geometry of the full RC object.

6) Clear the arrays:

	irc->IRCClearInstances();
	irc->IRCClearMeshes();

7) At the render's end, call to irc->RenderEnd(t). This function builds the object for viewport, clearing the rendering data.


**********************************************************************/

#ifndef __IRCINTERFACE__H
#define __IRCINTERFACE__H

#include "ifnpub.h"

// Forest Class_ID
#define TRAIL_CLASS_ID	Class_ID(0x39712def, 0x10a72959)

///////////////////////////////////////////////////////////////////////////////////////////
// RailClone Interface

#define RC_MIX_INTERFACE Interface_ID(0x54617e51, 0x67454c0c)
#define GetRCInterface(obj) ((IRCInterface*) obj->GetInterface(RC_MIX_INTERFACE))

// function IDs
enum { rc_segments_updateall, rc_getmeshes, rc_clearmeshes, rc_getinstances, rc_clearinstances, rc_renderbegin, rc_renderend };

class TRCInstance 
	{
	public:
	Matrix3 tm;						// full transformation for the instance
	Mesh *mesh;						// source mesh
	};


class IRCInterface : public FPMixinInterface {
	BEGIN_FUNCTION_MAP
		VFN_2(rc_segments_updateall, IRCSegmentsUpdateAll, TYPE_INT, TYPE_INT);
		FN_1(rc_getmeshes, TYPE_INTPTR, IRCGetMeshes, TYPE_INT);
		VFN_0(rc_clearmeshes, IRCClearMeshes);
		FN_1(rc_getinstances, TYPE_INTPTR, IRCGetInstances, TYPE_INT);
		VFN_0(rc_clearinstances, IRCClearInstances);
		VFN_1(rc_renderbegin, IRCRenderBegin, TYPE_TIMEVALUE);
		VFN_1(rc_renderend, IRCRenderEnd, TYPE_TIMEVALUE);
	END_FUNCTION_MAP

	virtual void IRCSegmentsUpdateAll(int n1, int n2) = 0;
	virtual INT_PTR IRCGetMeshes(int &numNodes) = 0;
	virtual void IRCClearMeshes() = 0;
	virtual INT_PTR IRCGetInstances(int &numNodes) = 0;
	virtual void IRCClearInstances() = 0;
	virtual void IRCRenderBegin(TimeValue t) = 0;
	virtual void IRCRenderEnd(TimeValue t) = 0;

	FPInterfaceDesc* GetDesc();	
};


///////////////////////////////////////////////////////////////////////////////////////////
// RailClone Static Interface

#define RC_STATIC_INTERFACE Interface_ID(0x2bd6594f, 0x5e6509d6)

#define GetRCStaticInterface()	(IRCStaticInterface *) ::GetInterface(GEOMOBJECT_CLASS_ID, TRAIL_CLASS_ID, RC_STATIC_INTERFACE)

enum { rc_registerengine };

class IRCStaticInterface : public FPStaticInterface {
	public:
	virtual void IRCRegisterEngine() = 0;

	FPInterfaceDesc* GetDesc();	
};



#endif


