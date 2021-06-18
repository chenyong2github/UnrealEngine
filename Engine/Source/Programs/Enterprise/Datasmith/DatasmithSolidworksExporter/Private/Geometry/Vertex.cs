// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class Vertex
    {
        public Vec3 v { get; set; }
        public Vec3 n { get; set; }
        public Vec2 uv { get; set; }
        public int index { get; set; }

        public Vertex()
        {
        }

        public Vertex(Vec3 vertex, Vec3 normal, Vec2 uvc, int index)
        {
            v = vertex;
            n = normal;
            uv = uvc;
            this.index = index;
        }

        public override int GetHashCode()
        {
            int hash = index;
            hash = hash * 17 + v.GetHashCode();
            hash = hash * 17 + n.GetHashCode();
            hash = hash * 17 + uv.GetHashCode();
            return hash;
        }

        public static bool operator != (Vertex a, Vertex b)
        {
            return !(a == b);
        }

        public static bool operator == (Vertex v1, Vertex v2)
        {
            return (v1.v == v2.v) && (v1.n == v2.n);
        }

        public static bool operator < (Vertex v1, Vertex v2)
        {
            bool res = false;
            if ((v1.v < v2.v) || ((v1.v == v2.v) && (v1.n < v2.n)))
                res = true;
            return res;
        }

        public static bool operator > (Vertex v1, Vertex v2)
        {
            bool res = false;
            if ((v1.v > v2.v) || ((v1.v == v2.v) && (v1.n > v2.n)))
                res = true;
            return res;
        }

        public override bool Equals(object obj)
        {
            // STEP 1: Check for null
            if (obj == null)
            {
                return false;
            }

            // STEP 3: equivalent data types
            if (this.GetType() != obj.GetType())
            {
                return false;
            }
            return Equals((Vertex)obj);
        }

        public bool Equals(Vertex obj)
        {
            if (obj == null)
                return false;
            if (ReferenceEquals(this, obj))
                return true;
            if (this.GetHashCode() != obj.GetHashCode())
                return false;
            System.Diagnostics.Debug.Assert(base.GetType() != typeof(object));
            if (!base.Equals(obj))
                return false;
            return (this == obj);
        }
    }
}
