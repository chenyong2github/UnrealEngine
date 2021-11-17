// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections;
using System.Collections.Generic;
using System.Dynamic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Text;
using System.Text.Json;
using BindingFlags = System.Reflection.BindingFlags;

namespace Jupiter.Implementation
{
    static class CompactBinaryFieldUtils
    {
        private const CompactBinaryFieldType SerializedTypeMask = (CompactBinaryFieldType)0b_1001_1111;
        private const CompactBinaryFieldType TypeMask              = (CompactBinaryFieldType)0b_0001_1111;

        private const CompactBinaryFieldType ObjectMask             = (CompactBinaryFieldType)0b_0001_1110;
        private const CompactBinaryFieldType ObjectBase             = (CompactBinaryFieldType)0b_0001_0010;
        
        private const CompactBinaryFieldType ArrayMask             = (CompactBinaryFieldType)0b_0001_1110;
        private const CompactBinaryFieldType ArrayBase             = (CompactBinaryFieldType)0b_0000_0100;

        private const CompactBinaryFieldType IntegerMask             = (CompactBinaryFieldType)0b_0001_1110;
        private const CompactBinaryFieldType IntegerBase             = (CompactBinaryFieldType)0b_0000_1000;

        private const CompactBinaryFieldType FloatMask             = (CompactBinaryFieldType)0b_0001_1100;
        private const CompactBinaryFieldType FloatBase             = (CompactBinaryFieldType)0b_0000_1000;

        private const CompactBinaryFieldType BoolMask             = (CompactBinaryFieldType)0b_0001_1110;
        private const CompactBinaryFieldType BoolBase             = (CompactBinaryFieldType)0b_0000_1100;

        private const CompactBinaryFieldType AttachmentMask             = (CompactBinaryFieldType)0b_0001_1110;
        private const CompactBinaryFieldType AttachmentBase             = (CompactBinaryFieldType)0b_0000_1110;


        public static bool IsArray(CompactBinaryFieldType type)
        {
            return (type & ArrayMask) == ArrayBase;
        }

        public static bool IsObject(CompactBinaryFieldType type)
        {
            return (type & ObjectMask) == ObjectBase;
        }

        public static bool IsString(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.String;
        }

        public static bool IsInteger(CompactBinaryFieldType type)
        {
            return (type & IntegerMask) == IntegerBase;
        }

        public static bool IsFloat(CompactBinaryFieldType type)
        {
            return (type & FloatMask) == FloatBase;
        }

        public static bool IsBool(CompactBinaryFieldType type)
        {
            return (type & BoolMask) == BoolBase;
        }
        
        public static bool IsAttachment(CompactBinaryFieldType type)
        {
            return (type & AttachmentMask) == AttachmentBase;
        }

        public static bool IsNull(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.Null;
        }

        public static bool IsCompactBinaryAttachment(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.CompactBinaryAttachment;
        }

        public static bool IsBinaryAttachment(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.BinaryAttachment;
        }

        public static bool IsHash(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.Hash || IsAttachment(type);
        }

        public static bool IsUuid(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.Uuid;
        }
        
        public static bool IsDateTime(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.DateTime;
        }

        public static bool IsBinary(CompactBinaryFieldType type)
        {
            return RemoveFlags(type) == CompactBinaryFieldType.Binary;
        }

        public static bool HasUniformFields(CompactBinaryFieldType type)
        {
            CompactBinaryFieldType localType = RemoveFlags(type);
            return localType == CompactBinaryFieldType.UniformObject || localType == CompactBinaryFieldType.UniformArray;
        }

        public static bool HasFields(CompactBinaryFieldType type)
        {
            CompactBinaryFieldType noFlags = RemoveFlags(type);
            return noFlags >= CompactBinaryFieldType.Object && noFlags <= CompactBinaryFieldType.UniformArray;
        }

        public static bool HasFieldType(CompactBinaryFieldType type)
        {
            return type.HasFlag(CompactBinaryFieldType.HasFieldType);
        }

        public static bool HasFieldName(CompactBinaryFieldType type)
        {
            return type.HasFlag(CompactBinaryFieldType.HasFieldName);
        }

        public static CompactBinaryFieldType RemoveFlags(CompactBinaryFieldType type)
        {
            return type & TypeMask;
        }

        public static ulong GetPayloadSize(CompactBinaryFieldType type, ReadOnlyMemory<byte> payload)
        {
            switch (RemoveFlags(type))
            {
                case CompactBinaryFieldType.None:
                case CompactBinaryFieldType.Null:
                    return 0;
                case CompactBinaryFieldType.Object:
                case CompactBinaryFieldType.UniformObject:
                case CompactBinaryFieldType.Array:
                case CompactBinaryFieldType.UniformArray:
                case CompactBinaryFieldType.Binary:
                case CompactBinaryFieldType.String:
                case CompactBinaryFieldType.CustomByName:
                case CompactBinaryFieldType.CustomById:
                {
                    ReadOnlyMemory<byte> localPayload = payload.Slice(0);
                    ulong payloadSize = BitUtils.ReadVarUInt(ref localPayload, out int bytesRead);
                    return payloadSize + (ulong)bytesRead;
                }

                case CompactBinaryFieldType.IntegerPositive:
                case CompactBinaryFieldType.IntegerNegative:
                {
                    ReadOnlyMemory<byte> localPayload = payload.Slice(0);
                    return BitUtils.MeasureVarUInt(ref localPayload);
                }
                case CompactBinaryFieldType.Float32:
                    return 4;
                case CompactBinaryFieldType.Float64:
                    return 8;
                case CompactBinaryFieldType.BoolFalse:
                case CompactBinaryFieldType.BoolTrue:
                    return 0;
                case CompactBinaryFieldType.CompactBinaryAttachment:
                case CompactBinaryFieldType.BinaryAttachment:
                case CompactBinaryFieldType.Hash:
                    return 20;
                case CompactBinaryFieldType.Uuid:
                    return 16;
                case CompactBinaryFieldType.DateTime:
                case CompactBinaryFieldType.TimeSpan:
                    return 8;
                case CompactBinaryFieldType.ObjectId:
                    return 12;
                default:
                    return 0;
            }
        }
    }
    [Flags]
    public enum CompactBinaryFieldType : byte
    {
        /** A field type that does not occur in a valid object. */
        None                             = 0x00,

        /** Null. Payload is empty. */
        Null                             = 0x01,

        /**
         * Object is an array of fields with unique non-empty names.
         *
         * Payload is a VarUInt byte count for the encoded fields followed by the fields.
         */
        Object                           = 0x02,
        /**
         * UniformObject is an array of fields with the same field types and unique non-empty names.
         *
         * Payload is a VarUInt byte count for the encoded fields followed by the fields.
         */
        UniformObject                    = 0x03,

        /**
         * Array is an array of fields with no name that may be of different types.
         *
         * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by the fields.
         */
        Array                            = 0x04,
        /**
         * UniformArray is an array of fields with no name and with the same field type.
         *
         * Payload is a VarUInt byte count, followed by a VarUInt item count, followed by field type,
         * followed by the fields without their field type.
         */
        UniformArray                     = 0x05,

        /** Binary. Payload is a VarUInt byte count followed by the data. */
        Binary                           = 0x06,

        /** String in UTF-8. Payload is a VarUInt byte count then an unterminated UTF-8 string. */
        String                           = 0x07,

        /**
         * Non-negative integer with the range of a 64-bit unsigned integer.
         *
         * Payload is the value encoded as a VarUInt.
         */
        IntegerPositive                  = 0x08,
        /**
         * Negative integer with the range of a 64-bit signed integer.
         *
         * Payload is the ones' complement of the value encoded as a VarUInt.
         */
        IntegerNegative                  = 0x09,

        /** Single precision float. Payload is one big endian IEEE 754 binary32 float. */
        Float32                          = 0x0a,
        /** Double precision float. Payload is one big endian IEEE 754 binary64 float. */
        Float64                          = 0x0b,

        /** Boolean false value. Payload is empty. */
        BoolFalse                        = 0x0c,
        /** Boolean true value. Payload is empty. */
        BoolTrue                         = 0x0d,

        /**
         * CompactBinaryAttachment is a reference to a compact binary attachment stored externally.
         *
         * Payload is a 160-bit hash digest of the referenced compact binary data.
         */
        CompactBinaryAttachment          = 0x0e,
        /**
         * BinaryAttachment is a reference to a binary attachment stored externally.
         *
         * Payload is a 160-bit hash digest of the referenced binary data.
         */
        BinaryAttachment                 = 0x0f,

        /** Hash. Payload is a 160-bit hash digest. */
        Hash                             = 0x10,
        /** UUID/GUID. Payload is a 128-bit UUID as defined by RFC 4122. */
        Uuid                             = 0x11,

        /**
         * Date and time between 0001-01-01 00:00:00.0000000 and 9999-12-31 23:59:59.9999999.
         *
         * Payload is a big endian int64 count of 100ns ticks since 0001-01-01 00:00:00.0000000.
         */
        DateTime                         = 0x12,
        /**
         * Difference between two date/time values.
         *
         * Payload is a big endian int64 count of 100ns ticks in the span, and may be negative.
         */
        TimeSpan                         = 0x13,

        /**
         * ObjectId is an opaque object identifier. See FCbObjectId.
         *
         * Payload is a 12-byte object identifier.
         */
        ObjectId                         = 0x14,

        /**
         * CustomById identifies the sub-type of its payload by an integer identifier.
         *
         * Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
         * by a VarUInt of the sub-type identifier then the payload of the sub-type.
         */
        CustomById                       = 0x1e,
        /**
         * CustomByType identifies the sub-type of its payload by a string identifier.
         *
         * Payload is a VarUInt byte count of the sub-type identifier and the sub-type payload, followed
         * by a VarUInt byte count of the unterminated sub-type identifier, then the sub-type identifier
         * without termination, then the payload of the sub-type.
         */
        CustomByName                     = 0x1f,

        /** Reserved for future use as a flag. Do not add types in this range. */
        Reserved                         = 0x20,

        /**
         * A transient flag which indicates that the object or array containing this field has stored
         * the field type before the payload and name. Non-uniform objects and fields will set this.
         *
         * Note: Since the flag must never be serialized, this bit may be repurposed in the future.
         */
        HasFieldType                     = 0x40,

        /** A persisted flag which indicates that the field has a name stored before the payload. */
        HasFieldName                     = 0x80,

    }


    public class CompactBinaryField
    {
        /*
        * These has to follow Engine\Source\Runtime\Core\Public\Serialization\CompactBinary.h
        *
        * DO NOT CHANGE THE VALUE OF ANY MEMBERS OF THIS ENUM!
        * BACKWARD COMPATIBILITY REQUIRES THAT THESE VALUES BE FIXED!
        * SERIALIZATION USES HARD-CODED CONSTANTS BASED ON THESE VALUES!
        */

        private readonly CompactBinaryFieldType _type;
        // the memory storing the name
        private readonly ReadOnlyMemory<byte> _nameMemory;
        // the memory area of the payload
        private readonly ReadOnlyMemory<byte> _payload;
        private readonly int _objectSize;

        public CompactBinaryField(ReadOnlyMemory<byte> data, CompactBinaryFieldType type = CompactBinaryFieldType.HasFieldType)
        {
            _objectSize = 0;
            CompactBinaryFieldType fieldType = type;
            if (CompactBinaryFieldUtils.HasFieldType(fieldType))
            {
                fieldType = (CompactBinaryFieldType)data.Span[0] | CompactBinaryFieldType.HasFieldType;
                data = data.Slice(1);
                _objectSize += 1;
            }
            _type = fieldType;

            _nameMemory = ReadOnlyMemory<byte>.Empty;
            if (CompactBinaryFieldUtils.HasFieldName(fieldType))
            {
                int nameLength = (int)BitUtils.ReadVarUInt(ref data, out int nameBytesRead);
                _nameMemory = data.Slice(nameBytesRead, nameLength);
                data = data.Slice(nameLength + nameBytesRead);
                _objectSize += nameLength + nameBytesRead;
            }

            int payloadSize = (int) CompactBinaryFieldUtils.GetPayloadSize(_type, data);
            // we reassign the memory range based on how long the payload is expected to be
            _payload = data.Slice(0, payloadSize);
            _objectSize += payloadSize;
        }

        
        public string Name 
        { 
            get
            {
                return _nameMemory.Length == 0 ? "" : Encoding.ASCII.GetString(_nameMemory.Span);
            }
        }

        public CompactBinaryFieldType Type
        {
            get
            {
                return CompactBinaryFieldUtils.RemoveFlags(_type);
            }
        }

        /** Value accessors **/
        public string AsString(string @default = "")
        {
            if (CompactBinaryFieldUtils.IsString(_type))
            {
                ReadOnlyMemory<byte> localPayload = _payload;
                ulong stringLength = BitUtils.ReadVarUInt(ref localPayload, out int bytesRead);
                ReadOnlyMemory<byte> stringMemory = _payload.Slice(bytesRead, (int)stringLength);
                return Encoding.ASCII.GetString(stringMemory.Span);
            }

            return @default;
        }

        public BlobIdentifier? AsCompactBinaryAttachment()
        {
            if (CompactBinaryFieldUtils.IsCompactBinaryAttachment(_type))
            {
                return new BlobIdentifier(_payload.ToArray());
            }

            return null;
        }

        public BlobIdentifier? AsBinaryAttachment()
        {
            if (CompactBinaryFieldUtils.IsBinaryAttachment(_type))
            {
                return new BlobIdentifier(_payload.ToArray());
            }

            return null;
        }

        public BlobIdentifier? AsAttachment()
        {
            if (CompactBinaryFieldUtils.IsAttachment(_type))
            {
                return new BlobIdentifier(_payload.ToArray());
            }

            return null;
        }

        public BlobIdentifier? AsHash()
        {
            if (CompactBinaryFieldUtils.IsHash(_type))
            {
                return new BlobIdentifier(_payload.ToArray());
            }

            return null;
        }

        public DateTime? AsDateTime()
        {
            if (CompactBinaryFieldUtils.IsDateTime(_type))
            {
                long? ticks = AsLongInteger();
                if (ticks == null)
                    return null;

                return DateTime.FromFileTime(ticks.Value);
            }

            return null;
        }

        public int? AsInteger()
        {
            if (CompactBinaryFieldUtils.IsInteger(_type))
            {
                return BitConverter.ToInt32(_payload.Span);
            }

            return null;
        }

        public long? AsLongInteger()
        {
            if (CompactBinaryFieldUtils.IsInteger(_type))
            {
                return BitConverter.ToInt64(_payload.Span);
            }

            return null;
        }

        public bool? AsBool()
        {
            if (CompactBinaryFieldUtils.IsBool(_type))
            {
                CompactBinaryFieldType type = CompactBinaryFieldUtils.RemoveFlags(_type);
                if (type == CompactBinaryFieldType.BoolFalse)
                    return false;
                else if (type == CompactBinaryFieldType.BoolTrue)
                    return true;

                throw new NotImplementedException("Unknown bool mapping, was not false or true");
            }

            return null;
        }

        public byte[]? AsBinary()
        {
            if (CompactBinaryFieldUtils.IsBinary(_type))
            {
                ReadOnlyMemory<byte> localPayload = _payload.Slice(0);
                ulong payloadSize = BitUtils.ReadVarUInt(ref localPayload, out int payloadBytesRead);
                localPayload = localPayload.Slice(payloadBytesRead);

                return localPayload.ToArray();
            }

            return null;
        }

        public IEnumerable<CompactBinaryField> AsArray()
        {
            return GetFields();
        }
        
        public CompactBinaryObject AsObject()
        {
            ReadOnlyMemory<byte> localMemory = _payload.Slice(0);
            return CompactBinaryObject.Load(ref localMemory);
        }

        public Guid AsUuid(Guid @default = default(Guid))
        {
            if (CompactBinaryFieldUtils.IsUuid(_type))
            {
                return new Guid(_payload.ToArray());
            }

            return @default;
        }

        public bool IsObject()
        {
            return CompactBinaryFieldUtils.IsObject(_type);
        }

        public bool IsArray()
        {
            return CompactBinaryFieldUtils.IsArray(_type);
        }

        public bool IsInteger()
        {
            return CompactBinaryFieldUtils.IsInteger(_type);
        }

        public bool IsDateTime()
        {
            return CompactBinaryFieldUtils.IsDateTime(_type);
        }

        public bool IsNull()
        {
            return CompactBinaryFieldUtils.IsNull(_type);
        }

        public bool IsBool()
        {
            return CompactBinaryFieldUtils.IsBool(_type);
        }

        public bool IsString()
        {
            return CompactBinaryFieldUtils.IsString(_type);
        }

        public bool IsHash()
        {
            return CompactBinaryFieldUtils.IsHash(_type);
        }

        public bool IsBinary()
        {
            return CompactBinaryFieldUtils.IsBinary(_type);
        }

        public bool IsBinaryAttachment()
        {
            return CompactBinaryFieldUtils.IsBinaryAttachment(_type);
        }

        public bool IsCompactBinaryAttachment()
        {
            return CompactBinaryFieldUtils.IsCompactBinaryAttachment(_type);
        }

        public bool IsAttachment()
        {
            return CompactBinaryFieldUtils.IsAttachment(_type);
        }

        public int GetObjectLength()
        {
            return _objectSize;
        }

        public IEnumerable<CompactBinaryField> GetFields()
        {
            if (CompactBinaryFieldUtils.HasFields(_type))
            {
                ReadOnlyMemory<byte> payloadBytes = _payload.Slice(0);
                ulong payloadSize = BitUtils.ReadVarUInt(ref payloadBytes, out int payloadBytesRead);
                payloadBytes = payloadBytes.Slice(payloadBytesRead);
                uint byteCount = CompactBinaryFieldUtils.IsArray(_type) ? BitUtils.MeasureVarUInt(ref payloadBytes) : 0;
                if (payloadSize > byteCount)
                {
                    payloadBytes = payloadBytes.Slice((int) byteCount, (int) (payloadSize - byteCount));
                    CompactBinaryFieldType uniformType = CompactBinaryFieldType.HasFieldType;
                    if (CompactBinaryFieldUtils.HasUniformFields(_type))
                    {
                        uniformType = (CompactBinaryFieldType) payloadBytes.Span[0];
                        payloadBytes = payloadBytes.Slice(1);
                    }

                    while (payloadBytes.Length != 0)
                    {
                        CompactBinaryField field = new CompactBinaryField(payloadBytes, uniformType);
                        payloadBytes = payloadBytes.Slice(field.GetObjectLength());

                        yield return field;
                    }
                }
            }
        }

        // Fetch a field by case sensitive name
        public CompactBinaryField? this[string name]
        {
            get { return GetFields().FirstOrDefault(field => field.Name == name); }
        }

        public CompactBinaryField? this[int i]
        {
            get { return GetFields().Skip(i).FirstOrDefault(); }
        }
    }

    public class CompactBinaryObject : CompactBinaryField
    {
        private CompactBinaryObject(byte[] buffer) : base(new ReadOnlyMemory<byte>(buffer))
        {
            Data = buffer;
        }

        public byte[] Data { get; }

        public static CompactBinaryObject Load(ref ReadOnlyMemory<byte> data)
        {
            ReadOnlyMemory<byte> localMemory = data.Slice(0);
            if (!TryMeasureCompactBinary(ref localMemory, out CompactBinaryFieldType _, out ulong size))
            {
                throw new Exception("Failed to load from invalid compact binary data.");
            }

            byte[] buffer = data.Slice(0, (int)size).ToArray();
            return new CompactBinaryObject(buffer);
        }

        public static CompactBinaryObject Load(byte[] data)
        {
            ReadOnlyMemory<byte> localMemory = new ReadOnlyMemory<byte>(data);
            if (!TryMeasureCompactBinary(ref localMemory, out CompactBinaryFieldType _, out ulong size))
            {
                throw new Exception("Failed to load from invalid compact binary data.");
            }

            byte[] buffer = new byte[size];
            Array.Copy(data, buffer, (long)size);
            return new CompactBinaryObject(buffer);
        }

        static bool TryMeasureCompactBinary(ref ReadOnlyMemory<byte> data, out CompactBinaryFieldType outFieldType, out ulong outSize, CompactBinaryFieldType assumeFieldType = CompactBinaryFieldType.HasFieldType)
        {
            CompactBinaryFieldType type = assumeFieldType;
            ulong size = 0;

            if (CompactBinaryFieldUtils.HasFieldType(type))
            {
                if (data.Length == 0)
                {
                    outFieldType = CompactBinaryFieldType.None;
                    outSize = 1;
                    return false;
                }

                type = (CompactBinaryFieldType) data.Span[0];
                data = data.Slice(1);
                size += 1;
            }

            bool dynamicSize = false;
            ulong fixedSize = 0;
            switch (CompactBinaryFieldUtils.RemoveFlags(type))
            {
            case CompactBinaryFieldType.Null:
                break;
            case CompactBinaryFieldType.Object:
            case CompactBinaryFieldType.UniformObject:
            case CompactBinaryFieldType.Array:
            case CompactBinaryFieldType.UniformArray:
            case CompactBinaryFieldType.Binary:
            case CompactBinaryFieldType.String:
            case CompactBinaryFieldType.IntegerPositive:
            case CompactBinaryFieldType.IntegerNegative:
            case CompactBinaryFieldType.CustomById:
            case CompactBinaryFieldType.CustomByName:
                dynamicSize = true;
                break;
            case CompactBinaryFieldType.Float32:
                fixedSize = 4;
                break;
            case CompactBinaryFieldType.Float64:
                fixedSize = 8;
                break;
            case CompactBinaryFieldType.BoolFalse:
            case CompactBinaryFieldType.BoolTrue:
                break;
            case CompactBinaryFieldType.CompactBinaryAttachment:
            case CompactBinaryFieldType.BinaryAttachment:
            case CompactBinaryFieldType.Hash:
                fixedSize = 20;
                break;
            case CompactBinaryFieldType.Uuid:
                fixedSize = 16;
                break;
            case CompactBinaryFieldType.DateTime:
            case CompactBinaryFieldType.TimeSpan:
                fixedSize = 8;
                break;
            case CompactBinaryFieldType.ObjectId:
                fixedSize = 12;
                break;
            case CompactBinaryFieldType.None:
            default:
                outFieldType = CompactBinaryFieldType.None;
                outSize = 0;
                return false;
            }

            outFieldType = type;

            if (CompactBinaryFieldUtils.HasFieldName(type))
            {
                if (data.Length == 0)
                {
                    outSize = size + 1;
                    return false;
                }

                uint nameLenByteCount = BitUtils.MeasureVarUInt(ref data);
                if (data.Length < nameLenByteCount)
                {
                    outSize = size + nameLenByteCount;
                    return false;
                }

                ulong nameLen = BitUtils.ReadVarUInt(ref data, out int bytesRead);
                ulong nameSize = nameLen + nameLenByteCount;

                if (dynamicSize && data.Length < (int)nameSize)
                {
                    outSize = size + nameSize;
                    return false;
                }

                data = data.Slice((int)nameSize);
                size += nameSize;
            }

            switch (CompactBinaryFieldUtils.RemoveFlags(type))
            {
            case CompactBinaryFieldType.Object:
            case CompactBinaryFieldType.UniformObject:
            case CompactBinaryFieldType.Array:
            case CompactBinaryFieldType.UniformArray:
            case CompactBinaryFieldType.Binary:
            case CompactBinaryFieldType.String:
            case CompactBinaryFieldType.CustomById:
            case CompactBinaryFieldType.CustomByName:
                if (data.Length == 0)
                {
                    outSize = size + 1;
                    return false;
                }
                else
                {
                    uint payloadSizeByteCount = BitUtils.MeasureVarUInt(ref data);
                    if (data.Length < payloadSizeByteCount)
                    {
                        outSize = size + payloadSizeByteCount;
                        return false;
                    }
                    ulong payloadSize = BitUtils.ReadVarUInt(ref data, out int _);
                    outSize = size + payloadSize + payloadSizeByteCount;
                    return true;
                }
            case CompactBinaryFieldType.IntegerPositive:
            case CompactBinaryFieldType.IntegerNegative:
                if (data.Length == 0)
                {
                    outSize = size + 1;
                    return false;
                }
                outSize = size + BitUtils.MeasureVarUInt(ref data);
                return true;
            default:
                outSize = size + fixedSize;
                return true;
            }
        }

        public IEnumerable<CompactBinaryField> GetAllFields()
        {
            Queue<CompactBinaryField> fieldQueue = new Queue<CompactBinaryField>(GetFields());

            while(fieldQueue.Count != 0)
            {
                CompactBinaryField? field = fieldQueue.Dequeue();
                yield return field;

                foreach (CompactBinaryField f in field.GetFields())
                {
                    fieldQueue.Enqueue(f);
                }
            }
        }

        public string ToJson()
        {
            var buffer = new ArrayBufferWriter<byte>();
            using Utf8JsonWriter jsonWriter = new Utf8JsonWriter(buffer);

            jsonWriter.WriteStartObject();
            foreach (CompactBinaryField field in GetFields())
            {
                WriteField(field, jsonWriter);
            }
            jsonWriter.WriteEndObject();
            jsonWriter.Flush();
            return Encoding.UTF8.GetString(buffer.WrittenMemory.Span);
        }

        private static void WriteField(CompactBinaryField field, Utf8JsonWriter jsonWriter)
        {
            if (field.IsObject())
            {
                jsonWriter.WriteStartObject();
                CompactBinaryObject o = field.AsObject();
                foreach (CompactBinaryField f in o.GetFields())
                {
                    WriteField(f, jsonWriter);
                }
                jsonWriter.WriteEndObject();
            }
            else if (field.IsArray())
            {
                jsonWriter.WriteStartArray();
                jsonWriter.WriteEndArray();
            }
            else if (field.IsInteger())
            {
                jsonWriter.WriteNumber(field.Name, field.AsInteger()!.Value);
            }
            else if (field.IsBool())
            {
                jsonWriter.WriteBoolean(field.Name, field.AsBool()!.Value);
            }
            else if (field.IsNull())
            {
                jsonWriter.WriteNullValue();
            }
            else if (field.IsDateTime())
            {
                jsonWriter.WriteString(field.Name, field.AsDateTime()!.Value);
            }
            else if (field.IsHash())
            {
                jsonWriter.WriteString(field.Name, field.AsHash()!.ToString()!);
            }
            else if (field.IsString())
            {
                jsonWriter.WriteString(field.Name, field.AsString());
            }
            else
            {
                throw new NotImplementedException($"Not handled type {field.Type} when attempting to convert to json");
            }

        }

        
        public object ToPoco(Type pocoType)
        {
            object? ToPocoField(CompactBinaryField field)
            {
                if (field.IsObject())
                {
                    ExpandoObject o = new ExpandoObject();
                    foreach (CompactBinaryField f in field.GetFields())
                    {
                        o.TryAdd(f.Name, ToPocoField(f));
                    }

                    return o;
                }
                else if (field.IsArray())
                {
                    List<object?> objs = new();
                    foreach (CompactBinaryField f in field.AsArray())
                    {
                        objs.Add(ToPocoField(f));
                    }

                    return objs.ToArray();
                }
                else if (field.IsInteger())
                {
                    return field.AsInteger()!.Value;
                }
                else if (field.IsBool())
                {
                    return field.AsBool()!.Value;
                }
                else if (field.IsNull())
                {
                    return null;
                }
                else if (field.IsDateTime())
                {
                    return field.AsDateTime()!.Value;
                }
                else if (field.IsHash())
                {
                    return field.AsHash()!;
                }
                else if (field.IsString())
                {
                    return field.AsString();
                }
                else
                {
                    throw new NotImplementedException($"Not handled type {field.Type} when attempting to convert to poco type");
                }
            }

            if (IsArray())
            {
                if (!pocoType.IsArray)
                    throw new Exception($"Trying to convert a compact binary array to a none array poco type: {pocoType}");

                Type arrayType = pocoType.GetElementType()!;
                List<object?> objs = new();
                foreach (CompactBinaryField f in GetFields())
                {
                    objs.Add(ToPocoField(f));
                }

                Array array = Array.CreateInstance(arrayType, objs.Count);
                for (int i = 0; i < objs.Count; i++)
                {
                    array.SetValue(objs[i], i);
                }

                return array;
            }

            ConstructorInfo? defaultConstructor = pocoType.GetConstructor(BindingFlags.CreateInstance, null, Array.Empty<Type>(), Array.Empty<ParameterModifier>());
            if (defaultConstructor == null)
                throw new Exception($"Failed to find a parameter-less constructor for type {pocoType} which is required to serialize from a compact binary");
            object o = defaultConstructor.Invoke(Array.Empty<object>());

            foreach (CompactBinaryField field in GetFields())
            {
                FieldInfo? fieldInfo = pocoType.GetField(field.Name);
                if (fieldInfo == null)
                    throw new Exception($"Failed to map compact binary field \"{field.Name}\" to a field in type {pocoType}");

                fieldInfo.SetValue(o, ToPocoField(field));
            }

            return o;
        }

    }

    static class BitUtils
    { 
        /**
             * Read a variable-length unsigned integer.
             *
             * @param InData A variable-length encoding of an unsigned integer.
             * @param OutByteCount The number of bytes consumed from the input.
             * @return An unsigned integer.
             */
        public static ulong ReadVarUInt(ref ReadOnlyMemory<byte> buffer, out int bytesRead)
        {
            bytesRead = (int)MeasureVarUInt(ref buffer);

            ulong value = (ulong) (buffer.Span[0] & (0xff >> bytesRead));

            for (int i = 1; i < bytesRead; i++)
            {
                value <<= 8;
                value |= buffer.Span[i];
            }

            return value;
        }

        /**
         * Measure the length in bytes (1-9) of an encoded variable-length integer.
         *
         * @param InData A variable-length encoding of an (signed or unsigned) integer.
         * @return The number of bytes used to encode the integer, in the range 1-9.
         */
        public static uint MeasureVarUInt(ref ReadOnlyMemory<byte> buffer)
        {
            byte b = buffer.Span[0];
            b = (byte)~b;
            return (uint)BitOperations.LeadingZeroCount (b) - 23;
        }

        /** Measure the number of bytes (1-5) required to encode the 32-bit input. */

        public static uint MeasureVarUInt(uint value)
        {
            //return uint32(int32(FPlatformMath::FloorLog2(InValue)) / 7 + 1);

            return (uint) (BitOperations.Log2(value) / 7 + 1);
        }

        /** Measure the number of bytes (1-9) required to encode the 64-bit input. */

        public static uint MeasureVarUInt(ulong value)
        {
            //return uint32(FPlatformMath::Min(int32(FPlatformMath::FloorLog2_64(InValue)) / 7 + 1, 9));

            return (uint) (Math.Min(BitOperations.Log2(value) / 7 + 1, 9));
        }

        /**
         * Write a variable-length unsigned integer.
         *
         * @param InValue An unsigned integer to encode.
         * @param OutData A buffer of at least 9 bytes to write the output to.
         * @return The number of bytes used in the output.
         */

        public static uint WriteVarUInt(ulong value, byte[] buffer, int bufferOffset = 0)
        {
            uint byteCount = MeasureVarUInt(value);

            for (int i = 1; i < byteCount; i++)
            {
                buffer[bufferOffset + byteCount - i] = (byte)value;
                value >>= 8;
            }
            buffer[bufferOffset] = (byte)((0xff << (9 - (int)byteCount)) | (byte)value);
            return byteCount;
        }
    }
}
