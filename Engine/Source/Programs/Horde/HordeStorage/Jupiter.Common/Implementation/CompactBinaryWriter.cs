// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Jupiter.Implementation
{
    public class CompactBinaryWriter
    {
        readonly List<Field> _fields = new List<Field>();
        private Field _topField;

        struct Field
        {
            public CompactBinaryFieldType FieldType;
            public byte[]? Name;
            public byte[]? Payload;
        }

        public byte[] Save()
        {
            uint bufferLength = 1; // field type;

            if (_topField.Name != null)
            {
                bufferLength += (uint)_topField.Name!.Length;
            }

            uint payloadBytesLength = 0;
            if (_topField.Payload != null)
            {
                payloadBytesLength = BitUtils.MeasureVarUInt((uint)_topField.Payload!.Length);
                bufferLength += payloadBytesLength;
                bufferLength += (uint)_topField.Payload!.Length;
            }
            byte[] buffer = new byte[bufferLength];
            buffer[0] = (byte) _topField.FieldType;
            int offset = 1;

            if (_topField.Name != null)
            {
                Array.Copy(_topField.Name!, 0, buffer, offset, _topField.Name!.Length);
                offset += _topField.Name.Length;
            }

            if (_topField.Payload != null)
            {
                // an array already contains the payload length
                if (!CompactBinaryFieldUtils.IsArray(_topField.FieldType))
                {
                    BitUtils.WriteVarUInt((uint) _topField.Payload!.Length, buffer, offset);
                    offset += (int)payloadBytesLength;
                }
                Array.Copy(_topField.Payload!, 0, buffer, offset, _topField.Payload!.Length);
                offset += _topField.Payload.Length;
            }

            return buffer;
        }

        public void BeginObject()
        {
            _topField = BeginField();
        }

        public void EndObject()
        {
            _topField.Payload = SerializePayload(_fields);
            
            EndField(ref this._topField, CompactBinaryFieldType.Object);
        }

        public void AsArray<T>(T[] members, CompactBinaryFieldType fieldType)
        {
            AddUniformArray<T>(members, fieldType);
            _topField = _fields.Last();
        }

        private static byte[] SerializePayload(List<Field> fields)
        {
            uint totalSizeFields = 0;
            foreach (Field field in fields)
            {
                totalSizeFields += 1; // the field type
                if (field.Payload != null)
                    totalSizeFields += (uint)field.Payload.Length;
                if (field.Name != null)
                    totalSizeFields += (uint)field.Name.Length;
            }

            byte[] buffer = new byte[totalSizeFields];
            long payloadOffset = 0;
            foreach (Field field in fields)
            {
                buffer[payloadOffset] = (byte) field.FieldType;
                payloadOffset += 1;

                if (CompactBinaryFieldUtils.HasFieldName(field.FieldType))
                {
                    Array.Copy(field.Name!, 0, buffer, payloadOffset, field.Name!.Length);
                    payloadOffset += field.Name.Length;
                }

                if ((field.Payload?.Length ?? 0) != 0)
                {
                    Array.Copy(field.Payload!, 0, buffer, payloadOffset, field.Payload!.Length);
                    payloadOffset += field.Payload.Length;
                }
            }

            return buffer;
        }

        public void AddString(string value, string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);

            uint length = (uint) Encoding.UTF8.GetByteCount(value);
            uint bytesCount = BitUtils.MeasureVarUInt(length);
            byte[] buffer = new byte[length + bytesCount];
            BitUtils.WriteVarUInt(length, buffer);
            int bytesWritten = Encoding.UTF8.GetBytes(value, 0, value.Length, buffer, (int)bytesCount);
            if (bytesWritten != length)
                throw new Exception("Failed to write string into buffer");

            field.Payload = buffer;
            EndField(ref field, CompactBinaryFieldType.String);
        }


        private Field BeginField()
        {
            // TODO: We could add checks similar to what we do in C++ to make sure the writer is used correctly
            return new Field();
        }

        private void EndField(ref Field field, CompactBinaryFieldType fieldType)
        {
            field.FieldType = fieldType;
            if (field.Name != null && field.Name.Length != 0)
            {
                field.FieldType |= CompactBinaryFieldType.HasFieldName;
            }
            // TODO: check if types are uniform and check the field type to be uniform thus removing redudant type info

            _fields.Add(field);
        }

        public void AddBinaryAttachment(BlobIdentifier hash, string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);

            if (hash.HashData.Length != 20)
                throw new Exception("Hash data is assumed to be 20 bytes");
            field.Payload = hash.HashData;

            EndField(ref field, CompactBinaryFieldType.BinaryAttachment);
        }

        public void AddCompactBinaryAttachment(BlobIdentifier hash, string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);

            if (hash.HashData.Length != 20)
                throw new Exception("Hash data is assumed to be 20 bytes");
            field.Payload = hash.HashData;

            EndField(ref field, CompactBinaryFieldType.CompactBinaryAttachment);
        }

        public void AddNull(string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);
            EndField(ref field, CompactBinaryFieldType.Null);
        }

        
        public void AddDateTime(DateTime dateTime, string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);
            long ticks = dateTime.Ticks;
            byte[] payload = BitConverter.GetBytes(ticks);
            if (BitConverter.IsLittleEndian)
            {
                Array.Reverse(payload, 0, payload.Length);
            }
            field.Payload = payload;

            EndField(ref field, CompactBinaryFieldType.DateTime);
        }

        
        public void AddBinary(byte[] data, string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);

            long bytesLength = BitUtils.MeasureVarUInt((uint)data.Length);
            byte[] payload = new byte[data.Length + bytesLength];
            BitUtils.WriteVarUInt((uint)data.Length, payload);
            Array.Copy(data, 0, payload, bytesLength, data.Length);
            field.Payload = payload;
            EndField(ref field, CompactBinaryFieldType.Binary);
        }

        public void AddUniformArray<T>(T[] members, CompactBinaryFieldType fieldType, string? fieldName = null)
        {
            Field field = BeginField();
            SetName(ref field, fieldName);

            uint bytesMembers = BitUtils.MeasureVarUInt((uint)members.Length);

            List<byte[]> payloads = new List<byte[]>();

            foreach (T member in members)
            {
                byte[] buffer = GetByteValue<T>(member, fieldType);
                payloads.Add(buffer);
            }

            long payloadsBytesLength = payloads.Sum(payload => (uint)payload.Length);
            uint payloadsLength = 1 + bytesMembers + (uint)payloadsBytesLength;
            uint bytesPayloads = BitUtils.MeasureVarUInt(payloadsLength);

            // Uniform array payload is a VarUInt byte count, followed by a VarUInt item count, followed by field type, followed by the fields without their field type.
            byte[] payloadBuffer = new byte[payloadsLength + bytesPayloads];
            BitUtils.WriteVarUInt(payloadsLength, payloadBuffer);
            int bufferOffset = (int)bytesPayloads;
            BitUtils.WriteVarUInt((uint)members.Length, payloadBuffer, bufferOffset);
            bufferOffset += (int)bytesMembers;
            payloadBuffer[bufferOffset] = (byte)fieldType;
            bufferOffset += 1;

            foreach (byte[] payload in payloads)
            {
                int payloadLength = payload.Length;
                Array.Copy(payload, 0, payloadBuffer, bufferOffset, payloadLength);
                bufferOffset += payloadLength;
            }

            field.Payload = payloadBuffer;
            EndField(ref field, CompactBinaryFieldType.UniformArray);
        }

        private byte[] GetByteValue<T>(T member, CompactBinaryFieldType fieldType)
        {
            switch (fieldType)
            {
                case CompactBinaryFieldType.None:
                case CompactBinaryFieldType.Null:
                    return Array.Empty<byte>();
                case CompactBinaryFieldType.Object:
                case CompactBinaryFieldType.UniformObject:
                case CompactBinaryFieldType.Array:
                case CompactBinaryFieldType.UniformArray:
                    throw new NotImplementedException();
                case CompactBinaryFieldType.Binary:
                    throw new NotImplementedException();
                case CompactBinaryFieldType.String:
                    if (member is string s)
                    {
                        return Encoding.UTF8.GetBytes(s);
                    }
                    break;
                case CompactBinaryFieldType.IntegerPositive:
                case CompactBinaryFieldType.IntegerNegative:
                    throw new NotImplementedException();
                case CompactBinaryFieldType.Float32:
                case CompactBinaryFieldType.Float64:
                    throw new NotImplementedException();
                case CompactBinaryFieldType.BoolFalse:
                case CompactBinaryFieldType.BoolTrue:
                    return Array.Empty<byte>();
                case CompactBinaryFieldType.CompactBinaryAttachment:
                case CompactBinaryFieldType.BinaryAttachment:
                case CompactBinaryFieldType.Hash:
                    if (member is BlobIdentifier blob)
                    {
                        return blob.HashData;
                    }

                    break;
                case CompactBinaryFieldType.Uuid:
                case CompactBinaryFieldType.DateTime:
                case CompactBinaryFieldType.TimeSpan:
                case CompactBinaryFieldType.ObjectId:
                case CompactBinaryFieldType.CustomById:
                case CompactBinaryFieldType.CustomByName:
                    throw new NotImplementedException();
                default:
                    throw new ArgumentOutOfRangeException(nameof(fieldType), fieldType, null);
            }
            throw new ArgumentException($"{member} of type {typeof(T).Name} not convert-able to field-type {fieldType}", nameof(member));
        }

        private void SetName(ref Field field, string? fieldName)
        {
            if (string.IsNullOrEmpty(fieldName))
                return;

            // TODO: We could add checks similar to what we do in C++ to make sure the writer is used correctly
            
            uint nameLength = (uint) fieldName.Length;
            uint nameBytesCount = BitUtils.MeasureVarUInt(nameLength);

            byte[] buffer = new byte[nameLength + nameBytesCount];
            BitUtils.WriteVarUInt(nameLength, buffer);
            int bytesWritten = Encoding.ASCII.GetBytes(fieldName, 0, fieldName.Length, buffer, (int)nameBytesCount);
            if (bytesWritten != nameLength)
                throw new Exception("Failed to write name into buffer");

            field.Name = buffer;
        }
    }
}
