// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace EpicGames.Serialization
{
	/// <summary>
	/// Exception for <see cref="CbWriter"/>
	/// </summary>
	public class CbWriterException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message"></param>
		public CbWriterException(string Message)
			: base(Message)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="Ex"></param>
		public CbWriterException(string Message, Exception? Ex)
			: base(Message, Ex)
		{
		}
	}

	/// <summary>
	/// Forward-only writer for compact binary objects
	/// </summary>
	public class CbWriter
	{
		/// <summary>
		/// Stores information about an object or array scope within the written buffer which requires a header to be inserted containing
		/// the size or number of elements when copied to an output buffer
		/// </summary>
		class Scope
		{
			public CbFieldType FieldType;
			public CbFieldType UniformFieldType;
			public int Offset; // Offset to insert the length/count
			public int Length; // Excludes the size of this field's headers, and child fields' headers.
			public int Count;
			public List<Scope>? Children;
			public int SizeOfChildHeaders; // Sum of additional headers for child items, recursively.

			public Scope(CbFieldType FieldType, CbFieldType UniformFieldType, int Offset)
			{
				this.FieldType = FieldType;
				this.UniformFieldType = UniformFieldType;
				this.Offset = Offset;
			}
		}

		/// <summary>
		/// Chunk of written data. Chunks are allocated as needed and chained together with scope annotations to produce the output data.
		/// </summary>
		class Chunk
		{
			public int Offset;
			public int Length;
			public byte[] Data;
			public List<Scope> Scopes = new List<Scope>();

			public Chunk(int Offset, int MaxLength)
			{
				this.Offset = Offset;
				this.Data = new byte[MaxLength];
			}
		}

		const int DefaultChunkSize = 1024;

		List<Chunk> Chunks = new List<Chunk>();
		Stack<Scope> OpenScopes = new Stack<Scope>();
		Chunk CurrentChunk => Chunks[Chunks.Count - 1];
		Scope CurrentScope => OpenScopes.Peek();
		int CurrentOffset;

		/// <summary>
		/// Constructor
		/// </summary>
		public CbWriter()
			: this(DefaultChunkSize)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Reserve">Amount of data to reserve for output</param>
		public CbWriter(int Reserve)
		{
			Chunks.Add(new Chunk(0, Reserve));
			OpenScopes.Push(new Scope(CbFieldType.Array, CbFieldType.None, 0));
		}

		/// <summary>
		/// Ensure that a block of contiguous memory of the given length is available in the output buffer
		/// </summary>
		/// <param name="Length"></param>
		/// <returns>The allocated memory</returns>
		Memory<byte> Allocate(int Length)
		{
			Chunk LastChunk = CurrentChunk;
			if (LastChunk.Length + Length > LastChunk.Data.Length)
			{
				int ChunkSize = Math.Max(Length, DefaultChunkSize);
				LastChunk = new Chunk(CurrentOffset, ChunkSize);
				Chunks.Add(LastChunk);
			}

			Memory<byte> Buffer = LastChunk.Data.AsMemory(LastChunk.Length, Length);
			LastChunk.Length += Length;
			CurrentOffset += Length;
			return Buffer;
		}

		/// <summary>
		/// Insert a new scope
		/// </summary>
		/// <param name="FieldType"></param>
		/// <param name="UniformFieldType"></param>
		void PushScope(CbFieldType FieldType, CbFieldType UniformFieldType)
		{
			Scope NewScope = new Scope(FieldType, UniformFieldType, CurrentOffset);
			CurrentScope.Children ??= new List<Scope>();
			CurrentScope.Children.Add(NewScope);
			OpenScopes.Push(NewScope);

			CurrentChunk.Scopes.Add(NewScope);
		}

		/// <summary>
		/// Pop a scope from the current open list
		/// </summary>
		void PopScope()
		{
			Scope Scope = CurrentScope;
			Scope.Length = CurrentOffset - Scope.Offset;
			Scope.SizeOfChildHeaders = ComputeSizeOfChildHeaders(Scope);
			OpenScopes.Pop();
		}

		/// <summary>
		/// Writes the header for an unnamed field
		/// </summary>
		/// <param name="Type"></param>
		void WriteField(CbFieldType Type)
		{
			Scope Scope = CurrentScope;
			if (!CbFieldUtils.IsArray(Scope.FieldType))
			{
				throw new CbWriterException($"Anonymous fields are not allowed within fields of type {Scope.FieldType}");
			}
			
			if (Scope.UniformFieldType == CbFieldType.None)
			{
				Allocate(1).Span[0] = (byte)Type;
			}
			else if (Scope.UniformFieldType != Type)
			{
				throw new CbWriterException($"Mismatched type for uniform array - expected {Scope.UniformFieldType}, not {Type}");
			}
			Scope.Count++;
		}

		/// <summary>
		/// Writes the header for a named field
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Name"></param>
		void WriteField(CbFieldType Type, Utf8String Name)
		{
			Scope Scope = CurrentScope;
			if (!CbFieldUtils.IsObject(Scope.FieldType))
			{
				throw new CbWriterException($"Named fields are not allowed within fields of type {Scope.FieldType}");
			}

			int NameVarIntLength = VarInt.Measure(Name.Length);
			if (Scope.UniformFieldType == CbFieldType.None)
			{
				Span<byte> Buffer = Allocate(1 + NameVarIntLength + Name.Length).Span;
				Buffer[0] = (byte)(Type | CbFieldType.HasFieldName);
				WriteBinaryPayload(Buffer[1..], Name.Span);
			}
			else
			{
				if (Scope.UniformFieldType != Type)
				{
					throw new CbWriterException($"Mismatched type for uniform object - expected {Scope.UniformFieldType}, not {Type}");
				}
				WriteBinaryPayload(Name.Span);
			}
			Scope.Count++;
		}

		/// <summary>
		/// Begin writing an object field
		/// </summary>
		public void BeginObject()
		{
			WriteField(CbFieldType.Object);
			PushScope(CbFieldType.Object, CbFieldType.None);
		}

		/// <summary>
		/// Begin writing an object field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		public void BeginObject(Utf8String Name)
		{
			WriteField(CbFieldType.Object, Name);
			PushScope(CbFieldType.Object, CbFieldType.None);
		}

		/// <summary>
		/// End the current object
		/// </summary>
		public void EndObject()
		{
			PopScope();
		}

		/// <summary>
		/// Begin writing an array field
		/// </summary>
		public void BeginArray()
		{
			WriteField(CbFieldType.Array);
			PushScope(CbFieldType.Array, CbFieldType.None);
		}

		/// <summary>
		/// Begin writing a named array field
		/// </summary>
		/// <param name="Name"></param>
		public void BeginArray(Utf8String Name)
		{
			WriteField(CbFieldType.Array, Name);
			PushScope(CbFieldType.Array, CbFieldType.None);
		}

		/// <summary>
		/// End the current array
		/// </summary>
		public void EndArray()
		{
			PopScope();
		}

		/// <summary>
		/// Begin writing a uniform array field
		/// </summary>
		/// <param name="FieldType">The field type for elements in the array</param>
		public void BeginUniformArray(CbFieldType FieldType)
		{
			WriteField(CbFieldType.UniformArray);
			PushScope(CbFieldType.UniformArray, FieldType);
			Allocate(1).Span[0] = (byte)FieldType;
		}

		/// <summary>
		/// Begin writing a named uniform array field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="FieldType">The field type for elements in the array</param>
		public void BeginUniformArray(Utf8String Name, CbFieldType FieldType)
		{
			WriteField(CbFieldType.UniformArray, Name);
			PushScope(CbFieldType.UniformArray, FieldType);
			Allocate(1).Span[0] = (byte)FieldType;
		}

		/// <summary>
		/// End the current array
		/// </summary>
		public void EndUniformArray()
		{
			PopScope();
		}

		/// <summary>
		/// Write a null field
		/// </summary>
		public void WriteNullValue()
		{
			WriteField(CbFieldType.Null);
		}

		/// <summary>
		/// Write a named null field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		public void WriteNull(Utf8String Name)
		{
			WriteField(CbFieldType.Null, Name);
		}

		/// <summary>
		/// Writes a boolean value
		/// </summary>
		/// <param name="Value"></param>
		public void WriteBoolValue(bool Value)
		{
			WriteField(Value? CbFieldType.BoolTrue : CbFieldType.BoolFalse);
		}

		/// <summary>
		/// Writes a boolean value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value"></param>
		public void WriteBool(Utf8String Name, bool Value)
		{
			WriteField(Value ? CbFieldType.BoolTrue : CbFieldType.BoolFalse, Name);
		}

		/// <summary>
		/// Writes the payload for an integer
		/// </summary>
		/// <param name="Value">Value to write</param>
		void WriteIntegerPayload(ulong Value)
		{
			int Length = VarInt.Measure(Value);
			Span<byte> Buffer = Allocate(Length).Span;
			VarInt.Write(Buffer, Value);
		}

		/// <summary>
		/// Writes an unnamed integer field
		/// </summary>
		/// <param name="Value">Value to be written</param>
		public void WriteIntegerValue(long Value)
		{
			if (Value >= 0)
			{
				WriteField(CbFieldType.IntegerPositive);
				WriteIntegerPayload((ulong)Value);
			}
			else
			{
				WriteField(CbFieldType.IntegerNegative);
				WriteIntegerPayload((ulong)-Value);
			}
		}

		/// <summary>
		/// Writes an named integer field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value to be written</param>
		public void WriteInteger(Utf8String Name, long Value)
		{
			if (Value >= 0)
			{
				WriteField(CbFieldType.IntegerPositive, Name);
				WriteIntegerPayload((ulong)Value);
			}
			else
			{
				WriteField(CbFieldType.IntegerNegative, Name);
				WriteIntegerPayload((ulong)-Value);
			}
		}

		/// <summary>
		/// Writes an unnamed integer field
		/// </summary>
		/// <param name="Value">Value to be written</param>
		public void WriteIntegerValue(ulong Value)
		{
			WriteField(CbFieldType.IntegerPositive);
			WriteIntegerPayload(Value);
		}

		/// <summary>
		/// Writes a named integer field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value to be written</param>
		public void WriteInteger(Utf8String Name, ulong Value)
		{
			WriteField(CbFieldType.IntegerPositive, Name);
			WriteIntegerPayload(Value);
		}

		/// <summary>
		/// Writes the payload for a <see cref="DateTime"/> value
		/// </summary>
		/// <param name="DateTime">The value to write</param>
		void WriteDateTimePayload(DateTime DateTime)
		{
			Span<byte> Buffer = Allocate(sizeof(long)).Span;
			BinaryPrimitives.WriteInt64BigEndian(Buffer, DateTime.Ticks);
		}

		/// <summary>
		/// Writes an unnamed <see cref="DateTime"/> field
		/// </summary>
		/// <param name="Value">Value to be written</param>
		public void WriteDateTimeValue(DateTime Value)
		{
			WriteField(CbFieldType.DateTime);
			WriteDateTimePayload(Value);
		}

		/// <summary>
		/// Writes a named <see cref="DateTime"/> field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value to be written</param>
		public void WriteDateTime(Utf8String Name, DateTime Value)
		{
			WriteField(CbFieldType.DateTime, Name);
			WriteDateTimePayload(Value);
		}

		/// <summary>
		/// Writes the payload for a hash
		/// </summary>
		/// <param name="Hash"></param>
		void WriteHashPayload(IoHash Hash)
		{
			Span<byte> Buffer = Allocate(IoHash.NumBytes).Span;
			Hash.Span.CopyTo(Buffer);
		}

		/// <summary>
		/// Writes an unnamed <see cref="IoHash"/> field
		/// </summary>
		/// <param name="Hash"></param>
		public void WriteHashValue(IoHash Hash)
		{
			WriteField(CbFieldType.Hash);
			WriteHashPayload(Hash);
		}

		/// <summary>
		/// Writes a named <see cref="IoHash"/> field
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value to be written</param>
		public void WriteHash(Utf8String Name, IoHash Value)
		{
			WriteField(CbFieldType.Hash, Name);
			WriteHashPayload(Value);
		}

		/// <summary>
		/// Writes an unnamed reference to a binary attachment
		/// </summary>
		/// <param name="Hash">Hash of the attachment</param>
		public void WriteBinaryAttachmentValue(IoHash Hash)
		{
			WriteField(CbFieldType.BinaryAttachment);
			WriteHashPayload(Hash);
		}

		/// <summary>
		/// Writes a named reference to a binary attachment
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Hash">Hash of the attachment</param>
		public void WriteBinaryAttachment(Utf8String Name, IoHash Hash)
		{
			WriteField(CbFieldType.BinaryAttachment, Name);
			WriteHashPayload(Hash);
		}

		/// <summary>
		/// Writes the payload for an object to the buffer
		/// </summary>
		/// <param name="Object"></param>
		void WriteObjectPayload(CbObject Object)
		{
			CbField Field = Object.AsField();
			Memory<byte> Buffer = Allocate(Field.Payload.Length);
			Field.Payload.CopyTo(Buffer);
		}

		/// <summary>
		/// Writes an object directly into the writer
		/// </summary>
		/// <param name="Object">Object to write</param>
		public void WriteObject(CbObject Object)
		{
			WriteField(CbFieldType.Object);
			WriteObjectPayload(Object);
		}

		/// <summary>
		/// Writes an object directly into the writer
		/// </summary>
		/// <param name="Name">Name of the object</param>
		/// <param name="Object">Object to write</param>
		public void WriteObject(Utf8String Name, CbObject Object)
		{
			WriteField(CbFieldType.Object, Name);
			WriteObjectPayload(Object);
		}

		/// <summary>
		/// Writes an unnamed reference to an object attachment
		/// </summary>
		/// <param name="Hash">Hash of the attachment</param>
		public void WriteObjectAttachmentValue(IoHash Hash)
		{
			WriteField(CbFieldType.ObjectAttachment);
			WriteHashPayload(Hash);
		}

		/// <summary>
		/// Writes a named reference to an object attachment
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Hash">Hash of the attachment</param>
		public void WriteObjectAttachment(Utf8String Name, IoHash Hash)
		{
			WriteField(CbFieldType.ObjectAttachment, Name);
			WriteHashPayload(Hash);
		}

		/// <summary>
		/// Writes the payload for a binary value
		/// </summary>
		/// <param name="Output">Output buffer</param>
		/// <param name="Value">Value to be written</param>
		static void WriteBinaryPayload(Span<byte> Output, ReadOnlySpan<byte> Value)
		{
			int VarIntLength = VarInt.Write(Output, Value.Length);
			Output = Output[VarIntLength..];

			Value.CopyTo(Output);
			CheckSize(Output, Value.Length);
		}

		/// <summary>
		/// Writes the payload for a binary value
		/// </summary>
		/// <param name="Value">Value to be written</param>
		void WriteBinaryPayload(ReadOnlySpan<byte> Value)
		{
			int ValueVarIntLength = VarInt.Measure(Value.Length);
			Span<byte> Buffer = Allocate(ValueVarIntLength + Value.Length).Span;
			WriteBinaryPayload(Buffer, Value);
		}

		/// <summary>
		/// Writes an unnamed string value
		/// </summary>
		/// <param name="Value">Value to be written</param>
		public void WriteStringValue(Utf8String Value)
		{
			WriteField(CbFieldType.String);
			WriteBinaryPayload(Value.Span);
		}

		/// <summary>
		/// Writes a named string value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value to be written</param>
		public void WriteString(Utf8String Name, Utf8String Value)
		{
			WriteField(CbFieldType.String, Name);
			WriteBinaryPayload(Value.Span);
		}

		/// <summary>
		/// Writes an unnamed binary value
		/// </summary>
		/// <param name="Value">Value to be written</param>
		public void WriteBinaryValue(ReadOnlySpan<byte> Value)
		{
			WriteField(CbFieldType.Binary);
			WriteBinaryPayload(Value);
		}

		/// <summary>
		/// Writes a named binary value
		/// </summary>
		/// <param name="Name">Name of the field</param>
		/// <param name="Value">Value to be written</param>
		public void WriteBinary(Utf8String Name, ReadOnlySpan<byte> Value)
		{
			WriteField(CbFieldType.Binary, Name);
			WriteBinaryPayload(Value);
		}

		/// <summary>
		/// Check that the given span is the required size
		/// </summary>
		/// <param name="Span"></param>
		/// <param name="ExpectedSize"></param>
		static void CheckSize(Span<byte> Span, int ExpectedSize)
		{
			if (Span.Length != ExpectedSize)
			{
				throw new Exception("Size of buffer is not correct");
			}
		}

		/// <summary>
		/// Gets the size of the serialized data
		/// </summary>
		/// <returns></returns>
		public int GetSize()
		{
			if (OpenScopes.Count != 1)
			{
				throw new CbWriterException("Unfinished scope in writer");
			}

			return CurrentOffset + ComputeSizeOfChildHeaders(CurrentScope);
		}

		/// <summary>
		/// Copy the data from this writer to a buffer
		/// </summary>
		/// <param name="Buffer"></param>
		public void CopyTo(Span<byte> Buffer)
		{
			int BufferOffset = 0;

			int SourceOffset = 0;
			foreach (Chunk Chunk in Chunks)
			{
				foreach (Scope Scope in Chunk.Scopes)
				{
					ReadOnlySpan<byte> SourceData = Chunk.Data.AsSpan(SourceOffset - Chunk.Offset, Scope.Offset - SourceOffset);
					SourceData.CopyTo(Buffer.Slice(BufferOffset));

					BufferOffset += SourceData.Length;
					SourceOffset += SourceData.Length;

					BufferOffset += WriteScopeHeader(Buffer.Slice(BufferOffset), Scope);
				}

				ReadOnlySpan<byte> LastSourceData = Chunk.Data.AsSpan(SourceOffset - Chunk.Offset, (Chunk.Offset + Chunk.Length) - SourceOffset);
				LastSourceData.CopyTo(Buffer.Slice(BufferOffset));
				BufferOffset += LastSourceData.Length;
				SourceOffset += LastSourceData.Length;
			}
		}

		/// <summary>
		/// Convert the data into a compact binary object
		/// </summary>
		/// <returns></returns>
		public CbObject ToObject()
		{
			return new CbObject(ToByteArray());
		}

		/// <summary>
		/// Convert the data into a flat array
		/// </summary>
		/// <returns></returns>
		public byte[] ToByteArray()
		{
			byte[] Buffer = new byte[GetSize()];
			CopyTo(Buffer);
			return Buffer;
		}

		/// <summary>
		/// Comptues the size of any child headers
		/// </summary>
		/// <param name="Scope"></param>
		static int ComputeSizeOfChildHeaders(Scope Scope)
		{
			int SizeOfChildHeaders = 0;
			if (Scope.Children != null)
			{
				foreach (Scope ChildScope in Scope.Children)
				{
					switch (ChildScope.FieldType)
					{
						case CbFieldType.Object:
						case CbFieldType.UniformObject:
							SizeOfChildHeaders += ChildScope.SizeOfChildHeaders + VarInt.Measure(ChildScope.Length + ChildScope.SizeOfChildHeaders);
							break;
						case CbFieldType.Array:
						case CbFieldType.UniformArray:
							int ArrayCountLength = VarInt.Measure(ChildScope.Count);
							SizeOfChildHeaders += ChildScope.SizeOfChildHeaders + VarInt.Measure(ChildScope.Length + ChildScope.SizeOfChildHeaders + ArrayCountLength) + ArrayCountLength;
							break;
						default:
							throw new InvalidOperationException();
					}
				}
			}
			return SizeOfChildHeaders;
		}

		/// <summary>
		/// Writes the header for a particular scope
		/// </summary>
		/// <param name="Span"></param>
		/// <param name="Scope"></param>
		/// <returns></returns>
		static int WriteScopeHeader(Span<byte> Span, Scope Scope)
		{
			switch (Scope.FieldType)
			{
				case CbFieldType.Object:
				case CbFieldType.UniformObject:
					return VarInt.Write(Span, Scope.Length + Scope.SizeOfChildHeaders);
				case CbFieldType.Array:
				case CbFieldType.UniformArray:
					int NumItemsLength = VarInt.Measure(Scope.Count);
					int Offset = VarInt.Write(Span, Scope.Length + Scope.SizeOfChildHeaders + NumItemsLength);
					return Offset + VarInt.Write(Span.Slice(Offset), Scope.Count);
				default:
					throw new InvalidOperationException();
			}
		}
	}
}