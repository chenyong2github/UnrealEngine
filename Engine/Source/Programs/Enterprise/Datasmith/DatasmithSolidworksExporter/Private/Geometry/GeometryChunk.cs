// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidworksDatasmith.SwObjects;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class GeometryChunk
    {
        public Vec3[] Vertices { get; set; } = null;
        public Vec3[] Normals { get; set; } = null;
        public Vec2[] TexCoords { get; set; } = null;
        public Triangle[] Indices = null;

        private float _modelSize = 0f;
        public float ModelSize { get { return _modelSize; } }
        private Vec3 _modelCenter = new Vec3();
        public Vec3 ModelCenter { get { return _modelCenter; } }

        public GeometryChunk()
        {
        }

        public GeometryChunk(int baseOffset, float[] vertices, float[] normals, float[] texcoords, int[] indices, BoundingBox bounds, SwMaterial material)
        {
            int vertexCount = (vertices.Length - baseOffset) / 3;
            int numFaces = indices.Length / 3;
            Vertices = new Vec3[vertexCount];
            Normals = new Vec3[vertexCount];
            Indices = new Triangle[numFaces];

            _modelSize = bounds.Size;
            _modelCenter = bounds.Center;

            int j = 0;
            for (int i = baseOffset; i < vertices.Length; i += 3)
            {
                Vertices[j] = new Vec3(vertices[i], vertices[i + 1], vertices[i + 2]);
                Normals[j] = new Vec3(normals[i], normals[i + 1], normals[i + 2]);
                j++;
            }

            int matID = -1;
            if (material != null)
			{
                matID = material.ID;
			}

            j = 0;
            for (int i = 0; i < indices.Length; i += 3)
            {
                Indices[j] = new Triangle(indices[i + 0], indices[i + 1], indices[i + 2], matID);
                j++;
            }

            //////////////////////////////////////////////////////////////////
            // UV MAPPING
            //////////////////////////////////////////////////////////////////

            if (material != null)
            {
                ConstructionGeometry c = new ConstructionGeometry(this);

                switch (material.UVMappingType)
                {
                    case SwMaterial.MappingType.TYPE_SPHERICAL:
                        {
                            c.ComputeSphericalUV(material);
                        }
                        break;

                    case SwMaterial.MappingType.TYPE_PROJECTION:
                    case SwMaterial.MappingType.TYPE_AUTOMATIC:
                        {
                            c.ComputePlanarUV(material);
                        }
                        break;

                    case SwMaterial.MappingType.TYPE_CYLINDRICAL:
                        {
                            c.ComputeCylindricalUV(material);
                        }
                        break;
                        
                    /*
                    case SwMaterial.MappingType.TYPE_SURFACE:
                        {
                            //GetSurfaceUVMapping(material, triStripInfo, swFace);
                        }
                        break;
                    */
                }
            }
        }
    }
}
