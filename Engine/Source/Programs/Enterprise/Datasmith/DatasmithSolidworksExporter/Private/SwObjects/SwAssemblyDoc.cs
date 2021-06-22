// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.SwObjects
{
	[ComVisible(false)]
	public class SwAssemblyDoc
	{
		public AssemblyDoc Doc { get; set; } = null;
		public string PathName { get; set; } = "";

		public SwAssemblyDoc(AssemblyDoc doc)
		{
			Doc = doc;
			PathName = (doc as ModelDoc2).GetPathName();
			//SwSingleton.CurrentScene.EvaluateSceneForDocument(doc as ModelDoc2);
		}
	}
}
