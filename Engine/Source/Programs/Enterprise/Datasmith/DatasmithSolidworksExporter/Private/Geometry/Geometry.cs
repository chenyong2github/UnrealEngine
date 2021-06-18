// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class Geometry
    {
        public Vec3[] Vertices { get; set; } = null;
        public Vec3[] Normals { get; set; } = null;
        public Vec2[] TexCoords { get; set; } = null;
        public Triangle[] Indices { get; set; } = null;

        public Geometry()
        {
        }
    }
}
