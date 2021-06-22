// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
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
    public class ConstructionGeometry
    {
        public List<Vec3> CVertices;
        public List<Vec3> CNormals;
        public List<Vec2> CTexCoords;
        public List<Triangle> CTriangles;
        private GeometryChunk Original = null;

        public ConstructionGeometry(GeometryChunk chunk)
        {
            Original = chunk;
            CVertices = Original.Vertices.ToList();
            CNormals = Original.Normals.ToList();
            CTexCoords = Enumerable.Repeat(new Vec2(0f, 0f), CVertices.Count).ToList();
            CTriangles = new List<Triangle>();
        }

        // Remeshing

        public void AddTriangle(Vec3 v1, Vec3 v2, Vec3 v3, Vec3 n1, Vec3 n2, Vec3 n3, UVPlane plane, SwMaterial material)
        {
            int curIndex = CVertices.Count;
            CTriangles.Add(new Triangle(curIndex, curIndex + 1, curIndex + 2, material.ID));
            CVertices.Add(v1);
            CVertices.Add(v2);
            CVertices.Add(v3);
            CNormals.Add(n1);
            CNormals.Add(n2);
            CNormals.Add(n3);
            CTexCoords.Add(material.ComputeVertexUV(plane, v1));
            CTexCoords.Add(material.ComputeVertexUV(plane, v2));
            CTexCoords.Add(material.ComputeVertexUV(plane, v3));
        }

        public void AddTriangle(Triangle t, Vec2 uv1, Vec2 uv2, Vec2 uv3)
        {
            CTexCoords[t.Index1] = uv1;
            CTexCoords[t.Index2] = uv2;
            CTexCoords[t.Index3] = uv3;
            CTriangles.Add(t);
        }

        public void AddTriangle(Vec3 v1, Vec3 v2, Vec3 v3, Vec3 n1, Vec3 n2, Vec3 n3, Vec2 uv1, Vec2 uv2, Vec2 uv3, SwMaterial material)
        {
            int curIndex = CVertices.Count;
            CTriangles.Add(new Triangle(curIndex, curIndex + 1, curIndex + 2, material.ID));
            CVertices.Add(v1);
            CVertices.Add(v2);
            CVertices.Add(v3);
            CNormals.Add(n1);
            CNormals.Add(n2);
            CNormals.Add(n3);
            CTexCoords.Add(uv1);
            CTexCoords.Add(uv2);
            CTexCoords.Add(uv3);
        }

        private void SplitTriangle(Vec3 v1, Vec3 v2, Vec3 v3, Vec3 n1, Vec3 n2, Vec3 n3, UVPlane[] pplanes, SwMaterial material)
        {
            uint splits = 0;
            bool[] split_edge = new bool[3];
            float u = 0.5f, v = 0.5f, w = 0.5f;

            if (pplanes[0] != pplanes[1])
            {
                splits++;
                split_edge[0] = true;
                u = 0.5f;
            }

            if (pplanes[1] != pplanes[2])
            {
                splits++;
                split_edge[1] = true;
                v = 0.5f;
            }

            if (pplanes[2] != pplanes[0])
            {
                splits++;
                split_edge[2] = true;
                w = 0.5f;
            }

            float sum = (u + v + w);
            Vec3 barycentric_weights = new Vec3(u / sum, v / sum, w / sum);

            if (splits == 0)
            {
                AddTriangle(v1, v2, v3, n1, n2, n3, pplanes[0], material);
            }
            else if (splits == 2 || splits == 3)
            {
                Vec3[] splitVerts = new Vec3[3];
                Vec3[] splitNorms = new Vec3[3];
                Vec3 centerVertex = Utility.BarycentricToPoint(barycentric_weights, v1, v2, v3);
                Vec3 centerNormal = Utility.BarycentricToPoint(barycentric_weights, n1, n2, n3).Normalized();

                float a = barycentric_weights.x / (barycentric_weights.x + barycentric_weights.y);
                splitVerts[0] = (v1 * a) + (v2 * (1.0f - a));
                splitNorms[0] = ((n1 * a) + (n2 * (1.0f - a))).Normalized();

                a = barycentric_weights.y / (barycentric_weights.y + barycentric_weights.z);
                splitVerts[1] = (v2 * a) + (v3 * (1.0f - a));
                splitNorms[1] = ((n2 * a) + (n3 * (1.0f - a))).Normalized();

                a = barycentric_weights.z / (barycentric_weights.z + barycentric_weights.x);
                splitVerts[2] = (v3 * a) + (v1 * (1.0f - a));
                splitNorms[2] = ((n3 * a) + (n1 * (1.0f - a))).Normalized();

                AddTriangle(v1, splitVerts[0], centerVertex, n1, splitNorms[0], centerNormal, pplanes[0], material);
                AddTriangle(splitVerts[0], v2, centerVertex, splitNorms[0], n2, centerNormal, pplanes[1], material);
                AddTriangle(centerVertex, v2, splitVerts[1], centerNormal, n2, splitNorms[1], pplanes[1], material);
                AddTriangle(splitVerts[1], v3, centerVertex, splitNorms[1], n3, centerNormal, pplanes[2], material);
                AddTriangle(v3, splitVerts[2], centerVertex, n3, splitNorms[2], centerNormal, pplanes[2], material);
                AddTriangle(splitVerts[2], v1, centerVertex, splitNorms[2], n1, centerNormal, pplanes[0], material);
            }
        }

        private void AddCylindricalTriangle(Vec3[] vertices, Vec3[] normals, Vec2[] signs, SwMaterial material, Vec2 cylinderScale, Vec2 cylinderOffset, bool angleNotZero, Vec2 cylinderTextureRotate)
        {
            int curIndex = CVertices.Count;
            CTriangles.Add(new Triangle(curIndex, curIndex + 1, curIndex + 2, material.ID));
            CVertices.Add(vertices[0]);
            CVertices.Add(vertices[1]);
            CVertices.Add(vertices[2]);
            CNormals.Add(normals[0]);
            CNormals.Add(normals[1]);
            CNormals.Add(normals[2]);
            CTexCoords.Add(CapCylinderMapping(signs[0], cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate));
            CTexCoords.Add(CapCylinderMapping(signs[1], cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate));
            CTexCoords.Add(CapCylinderMapping(signs[2], cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate));
        }

        public void AddCylindricalTriangleWithSplit(Vec3[] vertices, Vec3[] normals, Vec2[] signs, SwMaterial material, Vec2 cylinderScale, Vec2 cylinderOffset, bool angleNotZero, Vec2 cylinderTextureRotate)
        {
            uint splits = 0;
            bool[] split_edge = new bool[3];
            List<Vec3> positiveVerts = new List<Vec3>();
            List<Vec3> positiveNormals = new List<Vec3>();
            List<Vec2> positiveUVs = new List<Vec2>();
            List<Vec3> negativeVerts = new List<Vec3>();
            List<Vec3> negativeNormals = new List<Vec3>();
            List<Vec2> negativeUVs = new List<Vec2>();

            if (Math.Sign(signs[0].x) != Math.Sign(signs[1].y))
            {
                if (Math.Abs(signs[0].x - signs[1].x) > 0.5f)
                {
                    splits++;
                    split_edge[0] = true;
                }
            }

            if (Math.Sign(signs[1].x) != Math.Sign(signs[2].x))
            {
                if (Math.Abs(signs[1].x - signs[2].x) > 0.5f)
                {
                    splits++;
                    split_edge[1] = true;
                }
            }

            if (Math.Sign(signs[2].x) != Math.Sign(signs[0].x))
            {
                if (Math.Abs(signs[2].x - signs[0].x) > 0.5f)
                {
                    splits++;
                    split_edge[2] = true;
                }
            }

            for (uint i = 0; i < 3; i++)
            {
                if (signs[i].x < 0.0f)
                {
                    negativeVerts.Add(vertices[i]);
                    negativeNormals.Add(normals[i]);
                    negativeUVs.Add(signs[i]);
                }
                else
                {
                    positiveVerts.Add(vertices[i]);
                    positiveNormals.Add(normals[i]);
                    positiveUVs.Add(signs[i]);
                }
            }

            if (splits == 0)
            {
                AddCylindricalTriangle(vertices, normals, signs, material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);
            }
            else if (splits == 2)
            {
                Vec3[] splitVerts = new Vec3[2];
                Vec3[] splitNormals = new Vec3[2];
                Vec2[] splitUVs = new Vec2[2];
                if (negativeVerts.Count > positiveVerts.Count)
                {
                    splitVerts[0] = (negativeVerts[0] + positiveVerts[0]) * 0.5f;
                    splitNormals[0] = (negativeNormals[0] + positiveNormals[0]) * 0.5f;
                    splitUVs[0] = (negativeUVs[0] + positiveUVs[0]) * 0.5f;

                    splitVerts[1] = (negativeVerts[1] + positiveVerts[0]) * 0.5f;
                    splitNormals[1] = (negativeNormals[1] + positiveNormals[0]) * 0.5f;
                    splitUVs[1] = (negativeUVs[1] + positiveUVs[0]) * 0.5f;

                    AddCylindricalTriangle(new Vec3[] { negativeVerts[0], splitVerts[1], splitVerts[0] },
                                            new Vec3[] { negativeNormals[0], splitNormals[1], splitNormals[0] },
                                            new Vec2[] { negativeUVs[0], new Vec2(-0.5f, splitUVs[1].y), new Vec2(-0.5f, splitUVs[0].y) },
                                            material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);

                    AddCylindricalTriangle(new Vec3[] { negativeVerts[0], negativeVerts[1], splitVerts[1] },
                                            new Vec3[] { negativeNormals[0], negativeNormals[1], splitNormals[1] },
                                            new Vec2[] { negativeUVs[0], negativeUVs[1], new Vec2(-0.5f, splitUVs[1].y) },
                                            material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);

                    AddCylindricalTriangle(new Vec3[] { positiveVerts[0], splitVerts[0], splitVerts[1] },
                                            new Vec3[] { positiveNormals[0], splitNormals[0], splitNormals[1] },
                                            new Vec2[] { positiveUVs[0], new Vec2(0.5f, splitUVs[0].y), new Vec2(0.5f, splitUVs[1].y) },
                                            material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);
                }
                else
                {
                    splitVerts[0] = (negativeVerts[0] + positiveVerts[0]) * 0.5f;
                    splitNormals[0] = (negativeNormals[0] + positiveNormals[0]) * 0.5f;
                    splitUVs[0] = (negativeUVs[0] + positiveUVs[0]) * 0.5f;
                    splitVerts[1] = (negativeVerts[0] + positiveVerts[1]) * 0.5f;
                    splitNormals[1] = (negativeNormals[0] + positiveNormals[1]) * 0.5f;
                    splitUVs[1] = (negativeUVs[0] + positiveUVs[1]) * 0.5f;

                    AddCylindricalTriangle(new Vec3[] { positiveVerts[0], splitVerts[0], splitVerts[1] },
                                            new Vec3[] { positiveNormals[0], splitNormals[0], splitNormals[1] },
                                            new Vec2[] { positiveUVs[0], new Vec2(0.5f, splitUVs[0].y), new Vec2(0.5f, splitUVs[1].y) },
                                            material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);

                    AddCylindricalTriangle(new Vec3[] { positiveVerts[0], splitVerts[1], positiveVerts[1] },
                                            new Vec3[] { positiveNormals[0], splitNormals[1], positiveNormals[1] },
                                            new Vec2[] { positiveUVs[0], new Vec2(0.5f, splitUVs[1].y), positiveUVs[1] },
                                            material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);

                    AddCylindricalTriangle(new Vec3[] { negativeVerts[0], splitVerts[1], splitVerts[0] },
                                            new Vec3[] { negativeNormals[0], splitNormals[1], splitNormals[0] },
                                            new Vec2[] { negativeUVs[0], new Vec2(-0.5f, splitUVs[1].y), new Vec2(-0.5f, splitUVs[0].y) },
                                            material, cylinderScale, cylinderOffset, angleNotZero, cylinderTextureRotate);
        }
        }
        }

        private static Vec2 CapCylinderMapping(Vec2 atan2, Vec2 cylinderScale, Vec2 cylinderOffset, bool angleNotZero, Vec2 cylinderTextureRotate)
        {
            atan2 = Vec2.Translate(atan2, new Vec2(0.5f, 0));
            atan2 = Vec2.Translate(Vec2.Scale(atan2, cylinderScale), cylinderOffset);
            if (angleNotZero)
                atan2 = Vec2.RotateOnPlane(cylinderTextureRotate, atan2);
            return atan2;
        }

        // UV generation

        public void ComputeSphericalUV(SwMaterial material)
        {
            Original.TexCoords = new Vec2[Original.Vertices.Length];
            float angle = (float)(-material.RotationAngle * Utility.Deg2Rad);
            float rotationCos = (float)Math.Cos(angle);
            float rotationSin = (float)Math.Sin(angle);
            float flipU = material.MirrorHorizontal ? -1f : 1f;
            float flipV = material.MirrorVertical ? -1f : 1f;
            bool isAngleNotZero = !Utility.IsSame(material.RotationAngle, 0.0);
            float scaleU = (float)(Original.ModelSize * flipU / material.Width);
            float scaleV = (float)(Original.ModelSize * flipV / material.Height);
            float offsetU = (float)((flipU / material.Width) * (isAngleNotZero ? material.XPos * rotationCos - material.YPos * rotationSin : material.XPos));
            float offsetV = (float)((flipV / material.Height) * (isAngleNotZero ? material.XPos * rotationSin + material.YPos * rotationCos : material.YPos));

            for (var i = 0; i < Original.Vertices.Length; i++)
            {
                Vec3 normal = new Vec3(Original.Vertices[i].x - Original.ModelCenter.x, Original.Vertices[i].y - Original.ModelCenter.y, Original.Vertices[i].z - Original.ModelCenter.z).Normalized();
                normal = material.RotateVectorByXY(normal, Original.ModelCenter);
                Original.TexCoords[i] = new Vec2((float)(0.5 + Math.Atan2(normal.x, -normal.z) / (Math.PI * 2)), (float)(0.5 - Math.Asin(normal.y) / Math.PI));
                if (isAngleNotZero)
                    Utility.RotateOnPlane(rotationCos, rotationSin, ref Original.TexCoords[i].x, ref Original.TexCoords[i].y);
                Original.TexCoords[i].x = Original.TexCoords[i].x * scaleU;
                Original.TexCoords[i].y = Original.TexCoords[i].y * scaleV;
                Original.TexCoords[i].x = Original.TexCoords[i].x + offsetU;
                Original.TexCoords[i].y = Original.TexCoords[i].y + offsetV;
            }
        }

        public void ComputePlanarUV(SwMaterial material)
        {
            UVPlane[] planes = new UVPlane[3];
            var matPlanes = material.ComputeUVPlanes();
            if (Original.Vertices.Length >= 3)
            {
                for (int tt = 0; tt < Original.Indices.Length; tt++)
                {
                    var t = Original.Indices[tt];
                    planes[0] = material.GetTexturePlane(matPlanes, Original.Normals[t.Index1]);
                    planes[1] = material.GetTexturePlane(matPlanes, Original.Normals[t.Index2]);
                    planes[2] = material.GetTexturePlane(matPlanes, Original.Normals[t.Index3]);
                    if ((planes[0] != planes[1]) || (planes[1] != planes[2]) || (planes[2] != planes[0]))
                        SplitTriangle(Original.Vertices[t.Index1], Original.Vertices[t.Index2], Original.Vertices[t.Index3], Original.Normals[t.Index1], Original.Normals[t.Index2], Original.Normals[t.Index3], planes, material);
                    else
                        AddTriangle(t,
                            material.ComputeVertexUV(planes[0], Original.Vertices[t.Index1]),
                            material.ComputeVertexUV(planes[0], Original.Vertices[t.Index2]),
                            material.ComputeVertexUV(planes[0], Original.Vertices[t.Index3]));
                }
            }
            Original.Vertices = CVertices.ToArray();
            Original.Normals = CNormals.ToArray();
            Original.TexCoords = CTexCoords.ToArray();
            Original.Indices = CTriangles.ToArray();
        }

        public void ComputeCylindricalUV(SwMaterial material)
        {
            float flipU = material.MirrorHorizontal ? -1f : 1f;
            float flipV = material.MirrorVertical ? -1f : 1f;
            float angle = (float)(-material.RotationAngle * Utility.Deg2Rad);
            Vec2 rotation = new Vec2((float)Math.Cos(angle), (float)Math.Sin(flipU * flipV * angle));
            bool angleNotZero = !Utility.IsSame(material.RotationAngle, 0.0);
            Vec2 scale = new Vec2((float)(Original.ModelSize * flipU / material.Width), (float)(flipV / material.Height));
            Vec2 offset = new Vec2((float)(material.XPos * flipU / material.Width), (float)(material.YPos * flipV / material.Height));

            if (Original.Vertices.Length >= 3)
            {
                for (int tt = 0; tt < Original.Indices.Length; tt++)
                {
                    var t = Original.Indices[tt];
                    Vec2[] signs = new Vec2[3] {
                        material.ComputeNormalAtan2(Original.Vertices[t.Index1], Original.ModelCenter),
                        material.ComputeNormalAtan2(Original.Vertices[t.Index2], Original.ModelCenter),
                        material.ComputeNormalAtan2(Original.Vertices[t.Index3], Original.ModelCenter)
                    };
                    if ((Math.Sign(signs[0].x) != Math.Sign(signs[1].x)) || (Math.Sign(signs[1].x) != Math.Sign(signs[2].x)) || (Math.Sign(signs[2].x) != Math.Sign(signs[0].x)))
                        AddCylindricalTriangleWithSplit(new Vec3[] { Original.Vertices[t.Index1], Original.Vertices[t.Index2], Original.Vertices[t.Index3] },
                            new Vec3[] { Original.Normals[t.Index1], Original.Normals[t.Index2], Original.Normals[t.Index3] },
                            signs, material, scale, offset, angleNotZero, rotation);
                    else
                        AddTriangle(t,
                            CapCylinderMapping(signs[0], scale, offset, angleNotZero, rotation),
                            CapCylinderMapping(signs[1], scale, offset, angleNotZero, rotation),
                            CapCylinderMapping(signs[2], scale, offset, angleNotZero, rotation));
                    }
                }
            Original.Vertices = CVertices.ToArray();
            Original.Normals = CNormals.ToArray();
            Original.TexCoords = CTexCoords.ToArray();
            Original.Indices = CTriangles.ToArray();
        }

    }
}
