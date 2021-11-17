// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Jupiter.Implementation;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EuropaUnit
{
    [TestClass]
    public class CompactBinaryTests
    {

        [TestMethod]
        public void BuildObject()
        {
            byte[] bytes = File.ReadAllBytes("CompactBinaryObjects/build");

            ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(bytes);
            CompactBinaryObject o = CompactBinaryObject.Load(ref memory);

            Assert.AreEqual("BuildAction", o.Name);
            List<CompactBinaryField> buildActionFields = o.GetFields().ToList();
            Assert.AreEqual(3, buildActionFields.Count);
            Assert.AreEqual("Function", buildActionFields[0].Name);
            Assert.AreEqual("Constants", buildActionFields[1].Name);
            Assert.AreEqual("Inputs", buildActionFields[2].Name);

            List<CompactBinaryField>  constantsFields = buildActionFields[1].GetFields().ToList();
            Assert.AreEqual(3, constantsFields.Count);
            Assert.AreEqual("TextureBuildSettings", constantsFields[0].Name);
            Assert.AreEqual("TextureOutputSettings", constantsFields[1].Name);
            Assert.AreEqual("TextureSource", constantsFields[2].Name);

            List<CompactBinaryField>  inputsFields = buildActionFields[2].GetFields().ToList();
            Assert.AreEqual(1, inputsFields.Count);
            Assert.AreEqual("7587B323422942733DDD048A91709FDE", inputsFields[0].Name);
            Assert.IsTrue(inputsFields[0].IsBinaryAttachment());
            Assert.IsTrue(inputsFields[0].IsAttachment());
            Assert.IsFalse(inputsFields[0].IsCompactBinaryAttachment());
            Assert.AreEqual(new BlobIdentifier("f855382171a0b1e5a1c653aa6c5121a05cbf4ba0"), inputsFields[0].AsHash());
        }

        
        [TestMethod]
        public void ReferenceOutput()
        {
            byte[] bytes = File.ReadAllBytes("CompactBinaryObjects/ReferenceOutput");

            ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(bytes);
            CompactBinaryObject o = CompactBinaryObject.Load(ref memory);

            Assert.AreEqual("BuildOutput", o.Name);
            List<CompactBinaryField> buildActionFields = o.GetFields().ToList();
            Assert.AreEqual(1, buildActionFields.Count);
            CompactBinaryField payloads = buildActionFields[0];
            List<CompactBinaryField> payloadFields = payloads.GetFields().ToList();
            Assert.AreEqual(3, payloadFields.Count);

            Assert.AreEqual(new BlobIdentifier("5d8a6dc277c968f0d027c98f879c955c1905c293"), payloadFields[0]["RawHash"]!.AsHash());
            Assert.AreEqual(new BlobIdentifier("313f0d0d334100d83aeb1ee2c42794fd087cb0ae"), payloadFields[1]["RawHash"]!.AsHash());
            Assert.AreEqual(new BlobIdentifier("c7a03f83c08cdca882110ecf2b5654ee3b09b11e"), payloadFields[2]["RawHash"]!.AsHash());
        }


        [TestMethod]
        public void WriteArray()
        {
            var hash1 = new BlobIdentifier("5d8a6dc277c968f0d027c98f879c955c1905c293");
            var hash2 = new BlobIdentifier("313f0d0d334100d83aeb1ee2c42794fd087cb0ae");

            CompactBinaryWriter writer = new CompactBinaryWriter();
            writer.BeginObject();
            writer.AddUniformArray(new BlobIdentifier[] { hash1, hash2}, CompactBinaryFieldType.Hash, "needs");
            writer.EndObject();

            byte[] objectData = writer.Save();
            ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(objectData);
            CompactBinaryObject o = CompactBinaryObject.Load(ref memory);

            // the top object has no name
            Assert.AreEqual("", o.Name);
            List<CompactBinaryField> fields = o.GetFields().ToList();
            Assert.AreEqual(1, fields.Count);
            CompactBinaryField? needs = o["needs"];
            List<CompactBinaryField> blobList = needs!.AsArray().ToList();
            BlobIdentifier?[] blobs = blobList.Select(field => field!.AsHash()).ToArray();
            CollectionAssert.AreEqual(new BlobIdentifier[] {hash1, hash2}, blobs);
        }


        [TestMethod]
        public void WriteObject()
        {
            var hash1 = new BlobIdentifier("5d8a6dc277c968f0d027c98f879c955c1905c293");

            CompactBinaryWriter writer = new CompactBinaryWriter();
            writer.BeginObject();
            writer.AddString("test", "string");
            writer.AddBinaryAttachment(hash1, "hash");
            writer.EndObject();

            byte[] objectData = writer.Save();
            ReadOnlyMemory<byte> memory = new ReadOnlyMemory<byte>(objectData);
            CompactBinaryObject o = CompactBinaryObject.Load(ref memory);

            // the object has no name and 2 fields
            Assert.AreEqual("", o.Name);
            List<CompactBinaryField> fields = o.GetFields().ToList();
            Assert.AreEqual(2, fields.Count);

            CompactBinaryField? stringField = o["string"];
            Assert.AreEqual("test", stringField!.AsString());

            CompactBinaryField? hashField = o["hash"];
            Assert.AreEqual(hash1, hashField!.AsAttachment());
        }
    }

}
