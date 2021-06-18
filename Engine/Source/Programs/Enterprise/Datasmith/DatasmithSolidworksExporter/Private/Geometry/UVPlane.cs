// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidworksDatasmith.Geometry;
using SolidworksDatasmith.SwObjects;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class UVPlane
    {
        public Vec3 UDirection { get; set; }
        public Vec3 VDirection { get; set; }
        public Vec3 Normal { get; set; }
        public Vec3 Offset { get; set; }
    }
}
