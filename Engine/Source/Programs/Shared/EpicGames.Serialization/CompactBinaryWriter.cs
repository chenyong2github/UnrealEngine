// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.Serialization
{
	public class CbWriter
	{
		readonly List<Field> _fields = new List<Field>();
		private Field _topField;

		struct Field
		{
			public CbFieldType FieldType;
			public byte[]? Name;
			public byte[]? Payload;
		}

		public CbObject ToObject()
		{
			return new CbObject(Save());
		}

		public byte[] Save()
		{
			int bufferLength = 1; // field type;

			if (_topField.Name != null)
			{
				bufferLength += _topField.Name!.Length;
			}

			int payloadBytesLength = 0;
			if (_topField.Payload != null)
			{
				payloadBytesLength = BitUtils.MeasureVarUInt((uint)_topField.Payload!.Length);
				bufferLength += payloadBytesLength;
				bufferLength += _topField.Payload!.Length;
			}
			byte[] buffer = new byte[bufferLength];
			buffer[0] = (byte)_topField.FieldType;
			int offset = 1;

			if (_topField.Name != null)
			{
				Array.Copy(_topField.Name!, 0, buffer, offset, _topField.Name!.Length);
				offset += _topField.Name.Length;
			}

			if (_topField.Payload != null)
			{
				BitUtils.WriteVarUInt((uint)_topField.Payload!.Length, buffer, offset);
				offset += (int)payloadBytesLength;
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

			EndField(ref this._topField, CbFieldType.Object);
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
				buffer[payloadOffset] = (byte)field.FieldType;
				payloadOffset += 1;

				if (CbFieldUtils.HasFieldName(field.FieldType))
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

			int length = Encoding.UTF8.GetByteCount(value);
			int bytesCount = BitUtils.MeasureVarUInt(length);
			byte[] buffer = new byte[length + bytesCount];
			BitUtils.WriteVarUInt(length, buffer);
			int bytesWritten = Encoding.UTF8.GetBytes(value, 0, value.Length, buffer, (int)bytesCount);
			if (bytesWritten != length)
				throw new Exception("Failed to write string into buffer");

			field.Payload = buffer;
			EndField(ref field, CbFieldType.String);
		}


		private Field BeginField()
		{
			// TODO: We could add checks similar to what we do in C++ to make sure the writer is used correctly
			return new Field();
		}

		private void EndField(ref Field field, CbFieldType fieldType)
		{
			field.FieldType = fieldType;
			if (field.Name != null && field.Name.Length != 0)
			{
				field.FieldType |= CbFieldType.HasFieldName;
			}
			// TODO: check if types are uniform and check the field type to be uniform thus removing redudant type info

			_fields.Add(field);
		}

		public void AddBinaryAttachment(IoHash hash, string? fieldName = null)
		{
			Field field = BeginField();
			SetName(ref field, fieldName);

			if (hash.Memory.Length != 20)
				throw new Exception("Hash data is assumed to be 20 bytes");
			field.Payload = hash.Memory.ToArray();

			EndField(ref field, CbFieldType.BinaryAttachment);
		}

		public void AddCompactBinaryAttachment(IoHash hash, string? fieldName = null)
		{
			Field field = BeginField();
			SetName(ref field, fieldName);

			if (hash.Memory.Length != 20)
				throw new Exception("Hash data is assumed to be 20 bytes");
			field.Payload = hash.Memory.ToArray();

			EndField(ref field, CbFieldType.ObjectAttachment);
		}

		public void AddNull(string? fieldName = null)
		{
			Field field = BeginField();
			SetName(ref field, fieldName);
			EndField(ref field, CbFieldType.Null);
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

			EndField(ref field, CbFieldType.DateTime);
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
			EndField(ref field, CbFieldType.Binary);
		}

		public void AddUniformArray<T>(T[] members, CbFieldType fieldType, string? fieldName = null)
		{
			Field field = BeginField();
			SetName(ref field, fieldName);

			int bytesMembers = BitUtils.MeasureVarUInt((uint)members.Length);

			List<byte[]> payloads = new List<byte[]>();

			foreach (T member in members)
			{
				byte[] buffer = GetByteValue<T>(member, fieldType);
				payloads.Add(buffer);
			}

			int payloadsBytesLength = payloads.Sum(payload => payload.Length);
			int payloadsLength = 1 + bytesMembers + payloadsBytesLength;
			int bytesPayloads = BitUtils.MeasureVarUInt(payloadsLength);
			
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
			EndField(ref field, CbFieldType.UniformArray);
		}

		private byte[] GetByteValue<T>(T member, CbFieldType fieldType)
		{
			switch (fieldType)
			{
				case CbFieldType.None:
				case CbFieldType.Null:
					return Array.Empty<byte>();
				case CbFieldType.Object:
				case CbFieldType.UniformObject:
				case CbFieldType.Array:
				case CbFieldType.UniformArray:
					throw new NotImplementedException();
				case CbFieldType.Binary:
					throw new NotImplementedException();
				case CbFieldType.String:
					if (member is string s)
					{
						return Encoding.UTF8.GetBytes(s);
					}
					break;
				case CbFieldType.IntegerPositive:
				case CbFieldType.IntegerNegative:
					throw new NotImplementedException();
				case CbFieldType.Float32:
				case CbFieldType.Float64:
					throw new NotImplementedException();
				case CbFieldType.BoolFalse:
				case CbFieldType.BoolTrue:
					return Array.Empty<byte>();
				case CbFieldType.ObjectAttachment:
				case CbFieldType.BinaryAttachment:
				case CbFieldType.Hash:
					if (member is IoHash blob)
					{
						return blob.Memory.ToArray();
					}

					break;
				case CbFieldType.Uuid:
				case CbFieldType.DateTime:
				case CbFieldType.TimeSpan:
				case CbFieldType.ObjectId:
				case CbFieldType.CustomById:
				case CbFieldType.CustomByName:
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

			int nameLength = fieldName.Length;
			int nameBytesCount = BitUtils.MeasureVarUInt(nameLength);

			byte[] buffer = new byte[nameLength + nameBytesCount];
			BitUtils.WriteVarUInt(nameLength, buffer);
			int bytesWritten = Encoding.ASCII.GetBytes(fieldName, 0, fieldName.Length, buffer, (int)nameBytesCount);
			if (bytesWritten != nameLength)
				throw new Exception("Failed to write name into buffer");

			field.Name = buffer;
		}
	}
}