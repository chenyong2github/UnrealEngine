// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;

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
		/// <param name="message"></param>
		public CbWriterException(string message)
			: base(message)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="message"></param>
		/// <param name="ex"></param>
		public CbWriterException(string message, Exception? ex)
			: base(message, ex)
		{
		}
	}

	/// <summary>
	/// Interface for compact binary writers
	/// </summary>
	public interface ICbWriter
	{
		/// <summary>
		/// Begin writing an object field
		/// </summary>
		/// <param name="name">Name of the field. May be empty for fields that are not part of another object.</param>
		void BeginObject(Utf8String name);

		/// <summary>
		/// End the current object
		/// </summary>
		void EndObject();

		/// <summary>
		/// Begin writing a named array field
		/// </summary>
		/// <param name="name">Name of the field, or an empty string.</param>
		/// <param name="elementType">Type of the field. May be <see cref="CbFieldType.None"/> for non-uniform arrays.</param>
		void BeginArray(Utf8String name, CbFieldType elementType);

		/// <summary>
		/// End the current array
		/// </summary>
		void EndArray();

		/// <summary>
		/// Writes the header for a named field
		/// </summary>
		/// <param name="type">Type of the field</param>
		/// <param name="name">Name of the field. May be empty for fields that are not part of another object.</param>
		/// <param name="length">Length of data for the field</param>
		Span<byte> WriteField(CbFieldType type, Utf8String name, int length);
	}

	/// <summary>
	/// Forward-only writer for compact binary objects
	/// </summary>
	public class CbWriter : ICbWriter
	{
		/// <summary>
		/// Stores information about an object or array scope within the written buffer which requires a header to be inserted containing
		/// the size or number of elements when copied to an output buffer
		/// </summary>
		class Scope
		{
			public CbFieldType _fieldType;
			public CbFieldType _uniformFieldType;
			public int _offset; // Offset to insert the length/count
			public int _length; // Excludes the size of this field's headers, and child fields' headers.
			public int _count;
			public List<Scope> _children = new List<Scope>();
			public int _sizeOfChildHeaders; // Sum of additional headers for child items, recursively.

			public Scope(CbFieldType fieldType, CbFieldType uniformFieldType, int offset)
			{
				Reset(fieldType, uniformFieldType, offset);
			}

			public void Reset(CbFieldType fieldType, CbFieldType uniformFieldType, int offset)
			{
				_fieldType = fieldType;
				_uniformFieldType = uniformFieldType;
				_offset = offset;
				_length = 0;
				_count = 0;
				_children.Clear();
				_sizeOfChildHeaders = 0;
			}
		}

		/// <summary>
		/// Chunk of written data. Chunks are allocated as needed and chained together with scope annotations to produce the output data.
		/// </summary>
		class Chunk
		{
			public int _offset;
			public int _length;
			public byte[] _data;
			public List<Scope> _scopes = new List<Scope>();

			public Chunk(int offset, int maxLength)
			{
				_data = new byte[maxLength];
				Reset(offset);
			}

			public void Reset(int offset)
			{
				_offset = offset;
				_length = 0;
				_scopes.Clear();
			}
		}

		/// <summary>
		/// Size of data to preallocate by default
		/// </summary>
		public const int DefaultChunkSize = 1024;

		readonly List<Chunk> _chunks = new List<Chunk>();
		readonly Stack<Scope> _openScopes = new Stack<Scope>();
		Chunk CurrentChunk => _chunks[^1];
		Scope CurrentScope => _openScopes.Peek();
		int _currentOffset;
		readonly List<Chunk> _freeChunks = new List<Chunk>();
		readonly List<Scope> _freeScopes = new List<Scope>();

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
		/// <param name="reserve">Amount of data to reserve for output</param>
		public CbWriter(int reserve)
		{
			_chunks.Add(new Chunk(0, reserve));
			_openScopes.Push(new Scope(CbFieldType.Array, CbFieldType.None, 0));
		}

		/// <summary>
		/// 
		/// </summary>
		public void Clear()
		{
			foreach (Chunk chunk in _chunks)
			{
				FreeChunk(chunk);
			}

			_currentOffset = 0;

			_chunks.Clear();
			_chunks.Add(AllocChunk(0, DefaultChunkSize));

			_openScopes.Clear();
			_openScopes.Push(AllocScope(CbFieldType.Array, CbFieldType.None, 0));
		}

		/// <summary>
		/// Allocate a new chunk object
		/// </summary>
		/// <param name="offset">Offset of the chunk</param>
		/// <param name="maxLength">Maximum length of the chunk</param>
		/// <returns>New chunk object</returns>
		Chunk AllocChunk(int offset, int maxLength)
		{
			for (int idx = _freeChunks.Count - 1; idx >= 0; idx--)
			{
				Chunk chunk = _freeChunks[idx];
				if (chunk._data.Length >= maxLength)
				{
					_freeChunks.RemoveAt(idx);
					chunk.Reset(offset);
					return chunk;
				}
			}
			return new Chunk(offset, maxLength);
		}

		/// <summary>
		/// Adds a chunk to the free list
		/// </summary>
		/// <param name="chunk"></param>
		void FreeChunk(Chunk chunk)
		{
			// Add the scopes to the free list
			_freeScopes.AddRange(chunk._scopes);
			chunk._scopes.Clear();

			// Insert it into the free list, sorted by descending size
			for (int idx = 0; ; idx++)
			{
				if (idx == _freeChunks.Count || chunk._data.Length >= _freeChunks[idx]._data.Length)
				{
					_freeChunks.Insert(idx, chunk);
					break;
				}
			}
		}

		/// <summary>
		/// Allocate a scope object
		/// </summary>
		/// <param name="fieldType"></param>
		/// <param name="uniformFieldType"></param>
		/// <param name="offset"></param>
		/// <returns></returns>
		Scope AllocScope(CbFieldType fieldType, CbFieldType uniformFieldType, int offset)
		{
			if (_freeScopes.Count > 0)
			{
				Scope scope = _freeScopes[^1];
				scope.Reset(fieldType, uniformFieldType, offset);
				_freeScopes.RemoveAt(_freeScopes.Count - 1);
				return scope;
			}
			return new Scope(fieldType, uniformFieldType, offset);
		}

		/// <summary>
		/// Ensure that a block of contiguous memory of the given length is available in the output buffer
		/// </summary>
		/// <param name="length"></param>
		/// <returns>The allocated memory</returns>
		public Span<byte> Allocate(int length)
		{
			Chunk lastChunk = CurrentChunk;
			if (lastChunk._length + length > lastChunk._data.Length)
			{
				int chunkSize = Math.Max(length, DefaultChunkSize);
				lastChunk = AllocChunk(_currentOffset, chunkSize);
				_chunks.Add(lastChunk);
			}

			Span<byte> buffer = lastChunk._data.AsSpan(lastChunk._length, length);
			lastChunk._length += length;
			_currentOffset += length;
			return buffer;
		}

		/// <summary>
		/// Insert a new scope
		/// </summary>
		/// <param name="fieldType"></param>
		/// <param name="uniformFieldType"></param>
		void PushScope(CbFieldType fieldType, CbFieldType uniformFieldType)
		{
			Scope newScope = AllocScope(fieldType, uniformFieldType, _currentOffset);
			CurrentScope._children.Add(newScope);
			_openScopes.Push(newScope);

			CurrentChunk._scopes.Add(newScope);
		}

		/// <summary>
		/// Pop a scope from the current open list
		/// </summary>
		void PopScope()
		{
			Scope scope = CurrentScope;
			scope._length = _currentOffset - scope._offset;
			scope._sizeOfChildHeaders = ComputeSizeOfChildHeaders(scope);
			_openScopes.Pop();
		}

		/// <summary>
		/// Writes the header for a named field
		/// </summary>
		/// <param name="type">Type of the field</param>
		/// <param name="name">Name of the field</param>
		/// <param name="size">Size of the field</param>
		public Span<byte> WriteField(CbFieldType type, Utf8String name, int size)
		{
			if (name.IsEmpty)
			{
				Scope scope = CurrentScope;
				if (!CbFieldUtils.IsArray(scope._fieldType))
				{
					throw new CbWriterException($"Anonymous fields are not allowed within fields of type {scope._fieldType}");
				}

				if (scope._uniformFieldType == CbFieldType.None)
				{
					Allocate(1)[0] = (byte)type;
				}
				else if (scope._uniformFieldType != type)
				{
					throw new CbWriterException($"Mismatched type for uniform array - expected {scope._uniformFieldType}, not {type}");
				}
				scope._count++;
			}
			else
			{
				Scope scope = CurrentScope;
				if (!CbFieldUtils.IsObject(scope._fieldType))
				{
					throw new CbWriterException($"Named fields are not allowed within fields of type {scope._fieldType}");
				}

				int nameVarIntLength = VarInt.MeasureUnsigned(name.Length);
				if (scope._uniformFieldType == CbFieldType.None)
				{
					Span<byte> buffer = Allocate(1 + nameVarIntLength + name.Length);
					buffer[0] = (byte)(type | CbFieldType.HasFieldName);
					WriteBinaryPayload(buffer[1..], name.Span);
				}
				else
				{
					if (scope._uniformFieldType != type)
					{
						throw new CbWriterException($"Mismatched type for uniform object - expected {scope._uniformFieldType}, not {type}");
					}
					Span<byte> buffer = Allocate(name.Length);
					WriteBinaryPayload(buffer, name.Span);
				}
				scope._count++;
			}
			return Allocate(size);
		}

		/// <summary>
		/// Writes the payload for a binary value
		/// </summary>
		/// <param name="output">Output buffer</param>
		/// <param name="value">Value to be written</param>
		static void WriteBinaryPayload(Span<byte> output, ReadOnlySpan<byte> value)
		{
			int varIntLength = VarInt.WriteUnsigned(output, value.Length);
			output = output[varIntLength..];

			value.CopyTo(output);
			CheckSize(output, value.Length);
		}

		/// <summary>
		/// Begin writing an object field
		/// </summary>
		/// <param name="name">Name of the field</param>
		public void BeginObject(Utf8String name)
		{
			WriteField(CbFieldType.Object, name, 0);
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
		/// Begin writing a named array field
		/// </summary>
		/// <param name="name"></param>
		/// <param name="elementType">Type of elements in the array</param>
		public void BeginArray(Utf8String name, CbFieldType elementType)
		{
			if (elementType == CbFieldType.None)
			{
				WriteField(CbFieldType.Array, name, 0);
				PushScope(CbFieldType.Array, CbFieldType.None);
			}
			else
			{
				WriteField(CbFieldType.UniformArray, name, 0);
				PushScope(CbFieldType.UniformArray, elementType);
				Allocate(1)[0] = (byte)elementType;
			}
		}

		/// <summary>
		/// End the current array
		/// </summary>
		public void EndArray()
		{
			PopScope();
		}

		/// <summary>
		/// Computes the hash for this object
		/// </summary>
		/// <returns>Hash for the object</returns>
		public IoHash ComputeHash()
		{
			using (Blake3.Hasher hasher = Blake3.Hasher.New())
			{
				foreach (ReadOnlyMemory<byte> segment in EnumerateSegments())
				{
					hasher.Update(segment.Span);
				}
				return IoHash.FromBlake3(hasher);
			}
		}

		/// <summary>
		/// Gets the size of the serialized data
		/// </summary>
		/// <returns></returns>
		public int GetSize()
		{
			if (_openScopes.Count != 1)
			{
				throw new CbWriterException("Unfinished scope in writer");
			}

			return _currentOffset + ComputeSizeOfChildHeaders(CurrentScope);
		}

		/// <summary>
		/// Gets the contents of this writer as a stream
		/// </summary>
		/// <returns>New stream for the contents of this object</returns>
		public Stream AsStream() => new ReadStream(EnumerateSegments().GetEnumerator(), GetSize());

		private IEnumerable<ReadOnlyMemory<byte>> EnumerateSegments()
		{
			byte[] scopeHeader = new byte[64];

			int sourceOffset = 0;
			foreach (Chunk chunk in _chunks)
			{
				foreach (Scope scope in chunk._scopes)
				{
					ReadOnlyMemory<byte> sourceData = chunk._data.AsMemory(sourceOffset - chunk._offset, scope._offset - sourceOffset);
					yield return sourceData;

					sourceOffset += sourceData.Length;

					int headerLength = WriteScopeHeader(scopeHeader, scope);
					yield return scopeHeader.AsMemory(0, headerLength);
				}

				ReadOnlyMemory<byte> lastSourceData = chunk._data.AsMemory(sourceOffset - chunk._offset, (chunk._offset + chunk._length) - sourceOffset);
				yield return lastSourceData;

				sourceOffset += lastSourceData.Length;
			}
		}

		/// <summary>
		/// Copy the data from this writer to a buffer
		/// </summary>
		/// <param name="buffer"></param>
		public void CopyTo(Span<byte> buffer)
		{
			int bufferOffset = 0;

			int sourceOffset = 0;
			foreach (Chunk chunk in _chunks)
			{
				foreach (Scope scope in chunk._scopes)
				{
					ReadOnlySpan<byte> sourceData = chunk._data.AsSpan(sourceOffset - chunk._offset, scope._offset - sourceOffset);
					sourceData.CopyTo(buffer.Slice(bufferOffset));

					bufferOffset += sourceData.Length;
					sourceOffset += sourceData.Length;

					bufferOffset += WriteScopeHeader(buffer.Slice(bufferOffset), scope);
				}

				ReadOnlySpan<byte> lastSourceData = chunk._data.AsSpan(sourceOffset - chunk._offset, (chunk._offset + chunk._length) - sourceOffset);
				lastSourceData.CopyTo(buffer.Slice(bufferOffset));
				bufferOffset += lastSourceData.Length;
				sourceOffset += lastSourceData.Length;
			}
		}

		class ReadStream : Stream
		{
			readonly IEnumerator<ReadOnlyMemory<byte>> _enumerator;
			ReadOnlyMemory<byte> _segment;
			long _positionInternal;

			public ReadStream(IEnumerator<ReadOnlyMemory<byte>> enumerator, long length)
			{
				_enumerator = enumerator;
				Length = length;
			}

			/// <inheritdoc/>
			public override bool CanRead => true;

			/// <inheritdoc/>
			public override bool CanSeek => false;

			/// <inheritdoc/>
			public override bool CanWrite => false;

			/// <inheritdoc/>
			public override long Length { get; }

			/// <inheritdoc/>
			public override long Position
			{
				get => _positionInternal;
				set => throw new NotSupportedException();
			}

			/// <inheritdoc/>
			public override void Flush() { }

			/// <inheritdoc/>
			public override int Read(Span<byte> buffer)
			{
				int readLength = 0;
				while (readLength < buffer.Length)
				{
					while (_segment.Length == 0)
					{
						if (!_enumerator.MoveNext())
						{
							return readLength;
						}
						_segment = _enumerator.Current;
					}

					int copyLength = Math.Min(_segment.Length, buffer.Length);

					_segment.Span.Slice(0, copyLength).CopyTo(buffer.Slice(readLength));
					_segment = _segment.Slice(copyLength);

					_positionInternal += copyLength;
					readLength += copyLength;
				}
				return readLength;
			}

			/// <inheritdoc/>
			public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));

			/// <inheritdoc/>
			public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();

			/// <inheritdoc/>
			public override void SetLength(long value) => throw new NotSupportedException();

			/// <inheritdoc/>
			public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
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
			byte[] buffer = new byte[GetSize()];
			CopyTo(buffer);
			return buffer;
		}

		/// <summary>
		/// Comptues the size of any child headers
		/// </summary>
		/// <param name="scope"></param>
		static int ComputeSizeOfChildHeaders(Scope scope)
		{
			int sizeOfChildHeaders = 0;
			foreach (Scope childScope in scope._children)
			{
				switch (childScope._fieldType)
				{
					case CbFieldType.Object:
					case CbFieldType.UniformObject:
						sizeOfChildHeaders += childScope._sizeOfChildHeaders + VarInt.MeasureUnsigned(childScope._length + childScope._sizeOfChildHeaders);
						break;
					case CbFieldType.Array:
					case CbFieldType.UniformArray:
						int arrayCountLength = VarInt.MeasureUnsigned(childScope._count);
						sizeOfChildHeaders += childScope._sizeOfChildHeaders + VarInt.MeasureUnsigned(childScope._length + childScope._sizeOfChildHeaders + arrayCountLength) + arrayCountLength;
						break;
					default:
						throw new InvalidOperationException();
				}
			}
			return sizeOfChildHeaders;
		}

		/// <summary>
		/// Writes the header for a particular scope
		/// </summary>
		/// <param name="span"></param>
		/// <param name="scope"></param>
		/// <returns></returns>
		static int WriteScopeHeader(Span<byte> span, Scope scope)
		{
			switch (scope._fieldType)
			{
				case CbFieldType.Object:
				case CbFieldType.UniformObject:
					return VarInt.WriteUnsigned(span, scope._length + scope._sizeOfChildHeaders);
				case CbFieldType.Array:
				case CbFieldType.UniformArray:
					int numItemsLength = VarInt.MeasureUnsigned(scope._count);
					int offset = VarInt.WriteUnsigned(span, scope._length + scope._sizeOfChildHeaders + numItemsLength);
					return offset + VarInt.WriteUnsigned(span.Slice(offset), scope._count);
				default:
					throw new InvalidOperationException();
			}
		}

		/// <summary>
		/// Check that the given span is the required size
		/// </summary>
		/// <param name="span"></param>
		/// <param name="expectedSize"></param>
		static void CheckSize(Span<byte> span, int expectedSize)
		{
			if (span.Length != expectedSize)
			{
				throw new Exception("Size of buffer is not correct");
			}
		}
	}

	/// <summary>
	/// Extension methods for <see cref="CbWriter"/>
	/// </summary>
	public static class CbWriterExtensions
	{
		static int MeasureFieldWithLength(int length) => length + VarInt.MeasureUnsigned(length);

		static Span<byte> WriteFieldWithLength(this ICbWriter writer, CbFieldType type, Utf8String name, int length)
		{
			int fullLength = MeasureFieldWithLength(length);
			Span<byte> buffer = writer.WriteField(type, name, fullLength);

			int lengthLength = VarInt.WriteUnsigned(buffer, length);
			return buffer.Slice(lengthLength);
		}

		/// <summary>
		/// Begin writing an object field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		public static void BeginObject(this ICbWriter writer) => writer.BeginObject(default);

		/// <summary>
		/// Begin writing an array field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		public static void BeginArray(this ICbWriter writer) => writer.BeginArray(default, CbFieldType.None);

		/// <summary>
		/// Begin writing a named array field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		public static void BeginArray(this ICbWriter writer, Utf8String name) => writer.BeginArray(name, CbFieldType.None);

		/// <summary>
		/// Begin writing a uniform array field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="fieldType">The field type for elements in the array</param>
		public static void BeginUniformArray(this ICbWriter writer, CbFieldType fieldType) => BeginUniformArray(writer, default, fieldType);

		/// <summary>
		/// Begin writing a named uniform array field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="fieldType">The field type for elements in the array</param>
		public static void BeginUniformArray(this ICbWriter writer, Utf8String name, CbFieldType fieldType) => writer.BeginArray(name, fieldType);

		/// <summary>
		/// End writing a uniform array field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		public static void EndUniformArray(this ICbWriter writer) => writer.EndArray();

		/// <summary>
		/// Copies an entire field value to the output
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="field"></param>
		public static void WriteFieldValue(this ICbWriter writer, CbField field) => WriteField(writer, default, field);

		/// <summary>
		/// Copies an entire field value to the output, using the name from the field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="field"></param>
		public static void WriteField(this ICbWriter writer, CbField field) => WriteField(writer, field.GetName(), field);

		/// <summary>
		/// Copies an entire field value to the output
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="field"></param>
		public static void WriteField(this ICbWriter writer, Utf8String name, CbField field)
		{
			ReadOnlySpan<byte> source = field.GetPayloadView().Span;
			Span<byte> target = writer.WriteField(field.GetType(), name, source.Length);
			source.CopyTo(target);
		}

		/// <summary>
		/// Write a null field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		public static void WriteNullValue(this ICbWriter writer) => WriteNull(writer, default);

		/// <summary>
		/// Write a named null field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		public static void WriteNull(this ICbWriter writer, Utf8String name) => writer.WriteField(CbFieldType.Null, name, 0);

		/// <summary>
		/// Writes a boolean value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBoolValue(this ICbWriter writer, bool value) => WriteBool(writer, default, value);

		/// <summary>
		/// Writes a boolean value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBool(this ICbWriter writer, Utf8String name, bool value) => writer.WriteField(value ? CbFieldType.BoolTrue : CbFieldType.BoolFalse, name, 0);

		/// <summary>
		/// Writes an unnamed integer field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteIntegerValue(this ICbWriter writer, int value) => WriteInteger(writer, default, value);

		/// <summary>
		/// Writes an unnamed integer field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteIntegerValue(this ICbWriter writer, long value) => WriteInteger(writer, default, value);

		/// <summary>
		/// Writes an named integer field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteInteger(this ICbWriter writer, Utf8String name, int value) => WriteInteger(writer, name, (long)value);

		/// <summary>
		/// Writes an named integer field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteInteger(this ICbWriter writer, Utf8String name, long value)
		{
			if (value >= 0)
			{
				int length = VarInt.MeasureUnsigned((ulong)value);
				Span<byte> data = writer.WriteField(CbFieldType.IntegerPositive, name, length);
				VarInt.WriteUnsigned(data, (ulong)value);
			}
			else
			{
				int length = VarInt.MeasureUnsigned((ulong)-value);
				Span<byte> data = writer.WriteField(CbFieldType.IntegerNegative, name, length);
				VarInt.WriteUnsigned(data, (ulong)-value);
			}
		}

		/// <summary>
		/// Writes an unnamed integer field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteIntegerValue(this ICbWriter writer, ulong value) => WriteInteger(writer, default, value);

		/// <summary>
		/// Writes a named integer field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteInteger(this ICbWriter writer, Utf8String name, ulong value)
		{
			int length = VarInt.MeasureUnsigned((ulong)value);
			Span<byte> data = writer.WriteField(CbFieldType.IntegerPositive, name, length);
			VarInt.WriteUnsigned(data, (ulong)value);
		}

		/// <summary>
		/// Writes an unnamed double field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteDoubleValue(this ICbWriter writer, double value) => WriteDouble(writer, default, value);

		/// <summary>
		/// Writes a named double field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteDouble(this ICbWriter writer, Utf8String name, double value)
		{
			Span<byte> buffer = writer.WriteField(CbFieldType.Float64, name, sizeof(double));
			BinaryPrimitives.WriteDoubleBigEndian(buffer, value);
		}

		/// <summary>
		/// Writes an unnamed <see cref="DateTime"/> field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteDateTimeValue(this ICbWriter writer, DateTime value) => WriteDateTime(writer, default, value);

		/// <summary>
		/// Writes a named <see cref="DateTime"/> field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteDateTime(this ICbWriter writer, Utf8String name, DateTime value)
		{
			Span<byte> buffer = writer.WriteField(CbFieldType.DateTime, name, sizeof(long));
			BinaryPrimitives.WriteInt64BigEndian(buffer, value.Ticks);
		}

		/// <summary>
		/// Writes an unnamed <see cref="IoHash"/> field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteHashValue(this ICbWriter writer, IoHash value) => WriteHash(writer, default, value);

		/// <summary>
		/// Writes a named <see cref="IoHash"/> field
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteHash(this ICbWriter writer, Utf8String name, IoHash value)
		{
			Span<byte> buffer = writer.WriteField(CbFieldType.Hash, name, IoHash.NumBytes);
			value.CopyTo(buffer);
		}

		/// <summary>
		/// Writes an unnamed reference to a binary attachment
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="hash">Hash of the attachment</param>
		public static void WriteBinaryAttachmentValue(this ICbWriter writer, IoHash hash) => WriteBinaryAttachment(writer, default, hash);

		/// <summary>
		/// Writes a named reference to a binary attachment
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="hash">Hash of the attachment</param>
		public static void WriteBinaryAttachment(this ICbWriter writer, Utf8String name, IoHash hash)
		{
			Span<byte> buffer = writer.WriteField(CbFieldType.BinaryAttachment, name, IoHash.NumBytes);
			hash.CopyTo(buffer);
		}

		/// <summary>
		/// Writes an object directly into the writer
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="obj">Object to write</param>
		public static void WriteObject(this ICbWriter writer, CbObject obj) => WriteObject(writer, default, obj);

		/// <summary>
		/// Writes an object directly into the writer
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the object</param>
		/// <param name="obj">Object to write</param>
		public static void WriteObject(this ICbWriter writer, Utf8String name, CbObject obj)
		{
			ReadOnlyMemory<byte> view = obj.AsField().Payload;
			Span<byte> buffer = writer.WriteField(CbFieldType.Object, name, view.Length);
			view.Span.CopyTo(buffer);
		}

		/// <summary>
		/// Writes an unnamed reference to an object attachment
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="hash">Hash of the attachment</param>
		public static void WriteObjectAttachmentValue(this ICbWriter writer, IoHash hash) => WriteObjectAttachment(writer, default, hash);

		/// <summary>
		/// Writes a named reference to an object attachment
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="hash">Hash of the attachment</param>
		public static void WriteObjectAttachment(this ICbWriter writer, Utf8String name, IoHash hash)
		{
			Span<byte> buffer = writer.WriteField(CbFieldType.ObjectAttachment, name, IoHash.NumBytes);
			hash.CopyTo(buffer);
		}

		/// <summary>
		/// Writes an unnamed string value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteStringValue(this ICbWriter writer, string value) => WriteUtf8StringValue(writer, value);

		/// <summary>
		/// Writes a named string value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteString(this ICbWriter writer, Utf8String name, string? value)
		{
			if(value != null)
			{
				writer.WriteUtf8String(name, value);
			}
		}

		/// <summary>
		/// Writes an unnamed string value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteUtf8StringValue(this ICbWriter writer, Utf8String value) => WriteUtf8String(writer, default, value);

		/// <summary>
		/// Writes a named string value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteUtf8String(this ICbWriter writer, Utf8String name, Utf8String value)
		{
			Span<byte> buffer = WriteFieldWithLength(writer, CbFieldType.String, name, value.Length);
			value.Span.CopyTo(buffer);
		}

		/// <summary>
		/// Writes an unnamed binary value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBinarySpanValue(this ICbWriter writer, ReadOnlySpan<byte> value) => WriteBinarySpan(writer, default, value);

		/// <summary>
		/// Writes a named binary value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBinarySpan(this ICbWriter writer, Utf8String name, ReadOnlySpan<byte> value)
		{
			Span<byte> buffer = WriteFieldWithLength(writer, CbFieldType.Binary, name, value.Length);
			value.CopyTo(buffer);
		}

		/// <summary>
		/// Writes an unnamed binary value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBinaryValue(this ICbWriter writer, ReadOnlyMemory<byte> value) => writer.WriteBinarySpanValue(value.Span);

		/// <summary>
		/// Writes a named binary value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBinary(this ICbWriter writer, Utf8String name, ReadOnlyMemory<byte> value) => writer.WriteBinarySpan(name, value.Span);

		/// <summary>
		/// Writes an unnamed binary value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBinaryArrayValue(this ICbWriter writer, byte[] value) => writer.WriteBinarySpanValue(value.AsSpan());

		/// <summary>
		/// Writes a named binary value
		/// </summary>
		/// <param name="writer">Writer for output data</param>
		/// <param name="name">Name of the field</param>
		/// <param name="value">Value to be written</param>
		public static void WriteBinaryArray(this ICbWriter writer, Utf8String name, byte[] value) => writer.WriteBinarySpan(name, value.AsSpan());
	}
}