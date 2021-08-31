// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Serialization.Tests
{
	[TestClass]
	public class EngineTests
	{
		class CbFieldAccessors
		{
			public object Default;
			public Func<CbField, bool> IsType;
			public Func<CbField, object> AsType;
			public Func<CbField, object, object> AsTypeWithDefault;
			public Func<object, object, bool> Comparer = (x, y) => x.Equals(y);

			public CbFieldAccessors(object Default, Func<CbField, bool> IsType, Func<CbField, object> AsType, Func<CbField, object, object> AsTypeWithDefault)
			{
				this.Default = Default;
				this.IsType = IsType;
				this.AsType = AsType;
				this.AsTypeWithDefault = AsTypeWithDefault;
			}

			public static CbFieldAccessors FromStruct<T>(Func<CbField, bool> IsType, Func<CbField, T, object> AsTypeWithDefault) where T : struct
			{
				return new CbFieldAccessors(new T(), IsType, x => AsTypeWithDefault(x, new T()), (x, y) => AsTypeWithDefault(x, (T)y));
			}

			public static CbFieldAccessors FromStruct<T>(T Default, Func<CbField, bool> IsType, Func<CbField, T, object> AsTypeWithDefault) where T : struct
			{
				return new CbFieldAccessors(Default, IsType, x => AsTypeWithDefault(x, new T()), (x, y) => AsTypeWithDefault(x, (T)y));
			}

			public static CbFieldAccessors FromStruct<T>(Func<CbField, bool> IsType, Func<CbField, T, object> AsTypeWithDefault, Func<T, T, bool> Comparer) where T : struct
			{
				return new CbFieldAccessors(new T(), IsType, x => AsTypeWithDefault(x, new T()), (x, y) => AsTypeWithDefault(x, (T)y)) { Comparer = (x, y) => Comparer((T)x, (T)y) };
			}
		}

		Dictionary<CbFieldType, CbFieldAccessors> TypeAccessors = new Dictionary<CbFieldType, CbFieldAccessors>
		{
			[CbFieldType.Object] = new CbFieldAccessors(CbObject.Empty, x => x.IsObject(), x => x.AsObject(), (x, y) => x.AsObject()),
			[CbFieldType.UniformObject] = new CbFieldAccessors(CbObject.Empty, x => x.IsObject(), x => x.AsObject(), (x, y) => x.AsObject()),
			[CbFieldType.Array] = new CbFieldAccessors(CbArray.Empty, x => x.IsArray(), x => x.AsArray(), (x, y) => x.AsArray()),
			[CbFieldType.UniformArray] = new CbFieldAccessors(CbArray.Empty, x => x.IsArray(), x => x.AsArray(), (x, y) => x.AsArray()),
			[CbFieldType.Binary] = CbFieldAccessors.FromStruct<ReadOnlyMemory<byte>>(x => x.IsBinary(), (x, y) => x.AsBinary(y), (x, y) => x.Span.SequenceEqual(y.Span)),
			[CbFieldType.String] = new CbFieldAccessors(Utf8String.Empty, x => x.IsString(), x => x.AsString(), (x, Default) => x.AsString((Utf8String)Default)),
			[CbFieldType.IntegerPositive] = CbFieldAccessors.FromStruct<ulong>(x => x.IsInteger(), (x, y) => x.AsUInt64(y)),
			[CbFieldType.IntegerNegative] = CbFieldAccessors.FromStruct<long>(x => x.IsInteger(), (x, y) => x.AsInt64(y)),
			[CbFieldType.Float32] = CbFieldAccessors.FromStruct<float>(x => x.IsFloat(), (x, y) => x.AsFloat(y)),
			[CbFieldType.Float64] = CbFieldAccessors.FromStruct<double>(x => x.IsFloat(), (x, y) => x.AsDouble(y)),
			[CbFieldType.BoolTrue] = CbFieldAccessors.FromStruct<bool>(x => x.IsBool(), (x, y) => x.AsBool(y)),
			[CbFieldType.BoolFalse] = CbFieldAccessors.FromStruct<bool>(x => x.IsBool(), (x, y) => x.AsBool(y)),
			[CbFieldType.ObjectAttachment] = new CbFieldAccessors(IoHash.Zero, x => x.IsObjectAttachment(), x => x.AsObjectAttachment(), (x, Default) => x.AsObjectAttachment((IoHash)Default)),
			[CbFieldType.BinaryAttachment] = new CbFieldAccessors(IoHash.Zero, x => x.IsBinaryAttachment(), x => x.AsBinaryAttachment(), (x, Default) => x.AsBinaryAttachment((IoHash)Default)),
			[CbFieldType.Hash] = new CbFieldAccessors(IoHash.Zero, x => x.IsHash(), x => x.AsHash(), (x, Default) => x.AsHash((IoHash)Default)),
			[CbFieldType.Uuid] = CbFieldAccessors.FromStruct<Guid>(x => x.IsUuid(), (x, y) => x.AsUuid(y)),
			[CbFieldType.DateTime] = CbFieldAccessors.FromStruct<DateTime>(new DateTime(0, DateTimeKind.Utc), x => x.IsDateTime(), (x, y) => x.AsDateTime(y)),
			[CbFieldType.TimeSpan] = CbFieldAccessors.FromStruct<TimeSpan>(x => x.IsTimeSpan(), (x, y) => x.AsTimeSpan(y)),
		};

		void TestField(CbFieldType FieldType, CbField Field, object? ExpectedValue = null, object? DefaultValue = null, CbFieldError ExpectedError = CbFieldError.None, CbFieldAccessors? Accessors = null)
		{
			Accessors ??= TypeAccessors[FieldType];
			ExpectedValue ??= Accessors.Default;
			DefaultValue ??= Accessors.Default;

			Assert.AreEqual(Accessors.IsType(Field), ExpectedError != CbFieldError.TypeError);
			if (ExpectedError == CbFieldError.None && !Field.IsBool())
			{
				Assert.IsFalse(Field.AsBool());
				Assert.IsTrue(Field.HasError());
				Assert.AreEqual(Field.GetError(), CbFieldError.TypeError);
			}

			object Value = Accessors.AsTypeWithDefault(Field, DefaultValue);
			Assert.IsTrue(Accessors.Comparer(Accessors.AsTypeWithDefault(Field, DefaultValue), ExpectedValue));
			Assert.AreEqual(Field.HasError(), ExpectedError != CbFieldError.None);
			Assert.AreEqual(Field.GetError(), ExpectedError);
		}

		void TestField(CbFieldType FieldType, byte[] Payload, object? ExpectedValue = null, object? DefaultValue = null, CbFieldError ExpectedError = CbFieldError.None, CbFieldAccessors? Accessors = null)
		{
			CbField Field = new CbField(Payload, FieldType);
			Assert.AreEqual(Field.GetSize(), Payload.Length + (CbFieldUtils.HasFieldType(FieldType) ? 0 : 1));
			Assert.IsTrue(Field.HasValue());
			Assert.IsFalse(Field.HasError());
			Assert.AreEqual(Field.GetError(), CbFieldError.None);
			TestField(FieldType, Field, ExpectedValue, DefaultValue, ExpectedError, Accessors);
		}

		void TestFieldError(CbFieldType FieldType, CbField Field, CbFieldError ExpectedError, object? ExpectedValue = null, CbFieldAccessors? Accessors = null)
		{
			TestField(FieldType, Field, ExpectedValue, ExpectedValue, ExpectedError, Accessors);
		}

		void TestFieldError(CbFieldType FieldType, ReadOnlyMemory<byte> Payload, CbFieldError ExpectedError, object? ExpectedValue = null, CbFieldAccessors? Accessors = null)
		{
			CbField Field = new CbField(Payload, FieldType);
			TestFieldError(FieldType, Field, ExpectedError, ExpectedValue, Accessors);
		}

		[TestMethod]
		public void CbFieldNoneTest()
		{
			// Test CbField()
			{
				CbField DefaultField = new CbField();
				Assert.IsFalse(DefaultField.HasName());
				Assert.IsFalse(DefaultField.HasValue());
				Assert.IsFalse(DefaultField.HasError());
				Assert.IsTrue(DefaultField.GetError() == CbFieldError.None);
				Assert.AreEqual(DefaultField.GetSize(), 1);
				Assert.AreEqual(DefaultField.GetName().Length, 0);
				Assert.IsFalse(DefaultField.HasName());
				Assert.IsFalse(DefaultField.HasValue());
				Assert.IsFalse(DefaultField.HasError());
				Assert.AreEqual(DefaultField.GetError(), CbFieldError.None);
				Assert.AreEqual(DefaultField.GetHash(), Blake3Hash.Compute(new byte[] { (byte)CbFieldType.None }));
				ReadOnlyMemory<byte> View;
				Assert.IsFalse(DefaultField.TryGetView(out View));
			}

			// Test CbField(None)
			{
				CbField NoneField = new CbField(ReadOnlyMemory<byte>.Empty, CbFieldType.None);
				Assert.AreEqual(NoneField.GetSize(), 1);
				Assert.AreEqual(NoneField.GetName().Length, 0);
				Assert.IsFalse(NoneField.HasName());
				Assert.IsFalse(NoneField.HasValue());
				Assert.IsFalse(NoneField.HasError());
				Assert.AreEqual(NoneField.GetError(), CbFieldError.None);
				Assert.AreEqual(NoneField.GetHash(), new CbField().GetHash());
				ReadOnlyMemory<byte> View;
				Assert.IsFalse(NoneField.TryGetView(out View));
			}

			// Test CbField(None|Type|Name)
			{
				CbFieldType FieldType = CbFieldType.None | CbFieldType.HasFieldName;
				byte[] NoneBytes = { (byte)FieldType, 4, (byte)'N', (byte)'a', (byte)'m', (byte)'e' };
				CbField NoneField = new CbField(NoneBytes);
				Assert.AreEqual(NoneField.GetSize(), NoneBytes.Length);
				Assert.AreEqual(NoneField.GetName(), "Name");
				Assert.IsTrue(NoneField.HasName());
				Assert.IsFalse(NoneField.HasValue());
				Assert.AreEqual(NoneField.GetHash(), Blake3Hash.Compute(NoneBytes));
				ReadOnlyMemory<byte> View;
				Assert.IsTrue(NoneField.TryGetView(out View) && View.Span.SequenceEqual(NoneBytes));

				byte[] CopyBytes = new byte[NoneBytes.Length];
				NoneField.CopyTo(CopyBytes);
				Assert.IsTrue(NoneBytes.AsSpan().SequenceEqual(CopyBytes));
			}

			// Test CbField(None|Type)
			{
				CbFieldType FieldType = CbFieldType.None;
				byte[] NoneBytes = { (byte)FieldType };
				CbField NoneField = new CbField(NoneBytes);
				Assert.AreEqual(NoneField.GetSize(), NoneBytes.Length);
				Assert.AreEqual(NoneField.GetName().Length, 0);
				Assert.IsFalse(NoneField.HasName());
				Assert.IsFalse(NoneField.HasValue());
				Assert.AreEqual(NoneField.GetHash(), new CbField().GetHash());
				ReadOnlyMemory<byte> View;
				Assert.IsTrue(NoneField.TryGetView(out View) && View.Span.SequenceEqual(NoneBytes));
			}

			// Test CbField(None|Name)
			{
				CbFieldType FieldType = CbFieldType.None | CbFieldType.HasFieldName;
				byte[] NoneBytes = { (byte)FieldType, 4, (byte)'N', (byte)'a', (byte)'m', (byte)'e' };
				CbField NoneField = new CbField(NoneBytes.AsMemory(1), FieldType);
				Assert.AreEqual(NoneField.GetSize(), NoneBytes.Length);
				Assert.AreEqual(NoneField.GetName(), "Name");
				Assert.IsTrue(NoneField.HasName());
				Assert.IsFalse(NoneField.HasValue());
				Assert.AreEqual(NoneField.GetHash(), Blake3Hash.Compute(NoneBytes));
				ReadOnlyMemory<byte> View;
				Assert.IsFalse(NoneField.TryGetView(out View));

				byte[] CopyBytes = new byte[NoneBytes.Length];
				NoneField.CopyTo(CopyBytes);
				Assert.IsTrue(NoneBytes.AsSpan().SequenceEqual(CopyBytes));
			}

			// Test CbField(None|EmptyName)
			{
				CbFieldType FieldType = CbFieldType.None | CbFieldType.HasFieldName;
				byte[] NoneBytes = { (byte)FieldType, 0 };
				CbField NoneField = new CbField(NoneBytes.AsMemory(1), FieldType);
				Assert.AreEqual(NoneField.GetSize(), NoneBytes.Length);
				Assert.AreEqual(NoneField.GetName(), "");
				Assert.IsTrue(NoneField.HasName());
				Assert.IsFalse(NoneField.HasValue());
				Assert.AreEqual(NoneField.GetHash(), Blake3Hash.Compute(NoneBytes));
				ReadOnlyMemory<byte> View;
				Assert.IsFalse(NoneField.TryGetView(out View));
			}
		}

		[TestMethod]
		public void CbFieldNullTest()
		{
			// Test CbField(Null)
			{
				CbField NullField = new CbField(ReadOnlyMemory<byte>.Empty, CbFieldType.Null);
				Assert.AreEqual(NullField.GetSize(), 1);
				Assert.IsTrue(NullField.IsNull());
				Assert.IsTrue(NullField.HasValue());
				Assert.IsFalse(NullField.HasError());
				Assert.AreEqual(NullField.GetError(), CbFieldError.None);
				Assert.AreEqual(NullField.GetHash(), Blake3Hash.Compute(new byte[] { (byte)CbFieldType.Null }));
			}

			// Test CbField(None) as Null
			{
				CbField Field = new CbField();
				Assert.IsFalse(Field.IsNull());
			}
		}

		[TestMethod]
		public void CbFieldObjectTest()
		{
			Action<CbObject, int, int> TestIntObject = (CbObject Object, int ExpectedNum, int ExpectedPayloadSize) =>
			{
				Assert.AreEqual(Object.GetSize(), ExpectedPayloadSize + sizeof(CbFieldType));

				int ActualNum = 0;
				foreach (CbField Field in Object)
				{
					++ActualNum;
					Assert.AreNotEqual(Field.GetName().Length, 0);
					Assert.AreEqual(Field.AsInt32(), ActualNum);
				}
				Assert.AreEqual(ActualNum, ExpectedNum);
			};

			// Test CbField(Object, Empty)
			TestField(CbFieldType.Object, new byte[1]);

			// Test CbField(Object, Empty)
			{
				CbObject Object = CbObject.Empty;
				TestIntObject(Object, 0, 1);

				// Find fields that do not exist.
				Assert.IsFalse(Object.Find("Field").HasValue());
				Assert.IsFalse(Object.FindIgnoreCase("Field").HasValue());
				Assert.IsFalse(Object["Field"].HasValue());

				// Advance an iterator past the last field.
				CbFieldIterator It = Object.CreateIterator();
				Assert.IsFalse((bool)It);
				Assert.IsTrue(!It);
				for (int Count = 16; Count > 0; --Count)
				{
					++It;
					It.Current.AsInt32();
				}
				Assert.IsFalse((bool)It);
				Assert.IsTrue(!It);
			}

			// Test CbField(Object, NotEmpty)
			{
				byte IntType = (byte)(CbFieldType.HasFieldName | CbFieldType.IntegerPositive);
				byte[] Payload = { 12, IntType, 1, (byte)'A', 1, IntType, 1, (byte)'B', 2, IntType, 1, (byte)'C', 3 };
				CbField Field = new CbField(Payload, CbFieldType.Object);
				TestField(CbFieldType.Object, Field, new CbObject(Payload, CbFieldType.Object));
				CbObject Object = CbObject.Clone(Field.AsObject());
				TestIntObject(Object, 3, Payload.Length);
				TestIntObject(Field.AsObject(), 3, Payload.Length);
				Assert.IsTrue(Object.Equals(Field.AsObject()));
				Assert.AreEqual(Object.Find("B").AsInt32(), 2);
				Assert.AreEqual(Object.Find("b").AsInt32(4), 4);
				Assert.AreEqual(Object.FindIgnoreCase("B").AsInt32(), 2);
				Assert.AreEqual(Object.FindIgnoreCase("b").AsInt32(), 2);
				Assert.AreEqual(Object["B"].AsInt32(), 2);
				Assert.AreEqual(Object["b"].AsInt32(4), 4);
			}

			// Test CbField(UniformObject, NotEmpty)
			{
				byte IntType = (byte)(CbFieldType.HasFieldName | CbFieldType.IntegerPositive);
				byte[] Payload = { 10, IntType, 1, (byte)'A', 1, 1, (byte)'B', 2, 1, (byte)'C', 3 };
				CbField Field = new CbField(Payload, CbFieldType.UniformObject);
				TestField(CbFieldType.UniformObject, Field, new CbObject(Payload, CbFieldType.UniformObject));
				CbObject Object = CbObject.Clone(Field.AsObject());
				TestIntObject(Object, 3, Payload.Length);
				TestIntObject(Field.AsObject(), 3, Payload.Length);
				Assert.IsTrue(Object.Equals(Field.AsObject()));
				Assert.AreEqual(Object.Find("B").AsInt32(), 2);
				Assert.AreEqual(Object.Find("B").AsInt32(), 2);
				Assert.AreEqual(Object.Find("b").AsInt32(4), 4);
				Assert.AreEqual(Object.Find("b").AsInt32(4), 4);
				Assert.AreEqual(Object.FindIgnoreCase("B").AsInt32(), 2);
				Assert.AreEqual(Object.FindIgnoreCase("B").AsInt32(), 2);
				Assert.AreEqual(Object.FindIgnoreCase("b").AsInt32(), 2);
				Assert.AreEqual(Object.FindIgnoreCase("b").AsInt32(), 2);
				Assert.AreEqual(Object["B"].AsInt32(), 2);
				Assert.AreEqual(Object["b"].AsInt32(4), 4);

				// Equals
				byte[] NamedPayload = { 1, (byte)'O', 10, IntType, 1, (byte)'A', 1, 1, (byte)'B', 2, 1, (byte)'C', 3 };
				CbField NamedField = new CbField(NamedPayload, CbFieldType.UniformObject | CbFieldType.HasFieldName);
				Assert.IsTrue(Field.AsObject().Equals(NamedField.AsObject()));

				// CopyTo
				byte[] CopyBytes = new byte[Payload.Length + 1];
				Field.AsObject().CopyTo(CopyBytes);
				Assert.IsTrue(Payload.AsSpan().SequenceEqual(CopyBytes.AsSpan(1)));
				NamedField.AsObject().CopyTo(CopyBytes);
				Assert.IsTrue(Payload.AsSpan().SequenceEqual(CopyBytes.AsSpan(1)));

				// TryGetView
				ReadOnlyMemory<byte> View;
				Assert.IsFalse(Field.AsObject().TryGetView(out View));
				Assert.IsFalse(NamedField.AsObject().TryGetView(out View));
			}

			// Test CbField(None) as Object
			{
				CbField Field = CbField.Empty;
				TestFieldError(CbFieldType.Object, Field, CbFieldError.TypeError);
				CbField.MakeView(Field).AsObject();
			}

			// Test FCbObjectView(ObjectWithName) and CreateIterator
			{
				byte ObjectType = (byte)(CbFieldType.Object | CbFieldType.HasFieldName);
				byte[] Buffer = { ObjectType, 3, (byte)'K', (byte)'e', (byte)'y', 4, (byte)(CbFieldType.HasFieldName | CbFieldType.IntegerPositive), 1, (byte)'F', 8 };
				CbObject Object = new CbObject(Buffer);
				Assert.AreEqual(Object.GetSize(), 6);
				CbObject ObjectClone = CbObject.Clone(Object);
				Assert.AreEqual(ObjectClone.GetSize(), 6);
				Assert.IsTrue(Object.Equals(ObjectClone));
				Assert.AreEqual(ObjectClone.GetHash(), Object.GetHash());
				for (CbFieldIterator It = ObjectClone.CreateIterator(); It; ++It)
				{
					CbField Field = It.Current;
					Assert.AreEqual(Field.GetName(), "F");
					Assert.AreEqual(Field.AsInt32(), 8);
				}
				for (CbFieldIterator It = ObjectClone.CreateIterator(), End = new CbFieldIterator(); It != End; ++It)
				{
				}
				foreach (CbField Field in ObjectClone)
				{
				}
			}

			// Test FCbObjectView as CbFieldIterator
			{
				int Count = 0;
				CbObject Object = CbObject.Empty;
				for (CbFieldIterator It = CbFieldIterator.MakeSingle(Object.AsField()); It; ++It)
				{
					CbField Field = It.Current;
					Assert.IsTrue(Field.IsObject());
					++Count;
				}
				Assert.AreEqual(Count, 1);
			}
		}

		public void CbFieldArrayTest()
		{
			Action<CbArray, int, int> TestIntArray = (CbArray Array, int ExpectedNum, int ExpectedPayloadSize) =>
			{
				Assert.AreEqual(Array.GetSize(), ExpectedPayloadSize + sizeof(CbFieldType));
				Assert.AreEqual(Array.Count, ExpectedNum);

				int ActualNum = 0;
				for (CbFieldIterator It = Array.CreateIterator(); It; ++It)
				{
					++ActualNum;
					Assert.AreEqual(It.Current.AsInt32(), ActualNum);
				}
				Assert.AreEqual(ActualNum, ExpectedNum);

				ActualNum = 0;
				foreach (CbField Field in Array)
				{
					++ActualNum;
					Assert.AreEqual(Field.AsInt32(), ActualNum);
				}
				Assert.AreEqual(ActualNum, ExpectedNum);

				ActualNum = 0;
				foreach (CbField Field in Array.AsField())
				{
					++ActualNum;
					Assert.AreEqual(Field.AsInt32(), ActualNum);
				}
				Assert.AreEqual(ActualNum, ExpectedNum);
			};

			// Test CbField(Array, Empty)
			TestField(CbFieldType.Array, new byte[]{1, 0});

			// Test CbField(Array, Empty)
			{
				CbArray Array = new CbArray();
				TestIntArray(Array, 0, 2);

				// Advance an iterator past the last field.
				CbFieldIterator It = Array.CreateIterator();
				Assert.IsFalse((bool)It);
				Assert.IsTrue(!It);
				for (int Count = 16; Count > 0; --Count)
				{
					++It;
					It.Current.AsInt32();
				}
				Assert.IsFalse((bool)It);
				Assert.IsTrue(!It);
			}

			// Test CbField(Array, NotEmpty)
			{
				byte IntType = (byte)CbFieldType.IntegerPositive;
				byte[] Payload = new byte[]{ 7, 3, IntType, 1, IntType, 2, IntType, 3 };
				CbField Field = new CbField(Payload, CbFieldType.Array);
				TestField(CbFieldType.Array, Field, new CbArray(Payload, CbFieldType.Array));
				CbArray Array = CbArray.Clone(Field.AsArray());
				TestIntArray(Array, 3, Payload.Length);
				TestIntArray(Field.AsArray(), 3, Payload.Length);
				Assert.IsTrue(Array.Equals(Field.AsArray()));
			}

			// Test CbField(UniformArray)
			{
				byte IntType = (byte)(CbFieldType.IntegerPositive);
				byte[] Payload = new byte[]{ 5, 3, IntType, 1, 2, 3 };
				CbField Field = new CbField(Payload, CbFieldType.UniformArray);
				TestField(CbFieldType.UniformArray, Field, new CbArray(Payload, CbFieldType.UniformArray));
				CbArray Array = CbArray.Clone(Field.AsArray());
				TestIntArray(Array, 3, Payload.Length);
				TestIntArray(Field.AsArray(), 3, Payload.Length);
				Assert.IsTrue(Array.Equals(Field.AsArray()));

//				Assert.IsTrue(Array.GetOuterBuffer() == Array.AsField().AsArray().GetOuterBuffer());

				// Equals
				byte[] NamedPayload = new byte[]{ 1, (byte)'A', 5, 3, IntType, 1, 2, 3 };
				CbField NamedField = new CbField(NamedPayload, CbFieldType.UniformArray | CbFieldType.HasFieldName);
				Assert.IsTrue(Field.AsArray().Equals(NamedField.AsArray()));
				Assert.IsTrue(Field.Equals(Field.AsArray().AsField()));
				Assert.IsTrue(NamedField.Equals(NamedField.AsArray().AsField()));

				// CopyTo
				byte[] CopyBytes = new byte[Payload.Length + 1];
				Field.AsArray().CopyTo(CopyBytes);
				Assert.IsTrue(Payload.AsSpan().SequenceEqual(CopyBytes.AsSpan(1)));
				NamedField.AsArray().CopyTo(CopyBytes);
				Assert.IsTrue(Payload.AsSpan().SequenceEqual(CopyBytes.AsSpan(1)));

				// TryGetView
				ReadOnlyMemory<byte> View;
//				Assert.IsTrue(Array.TryGetView(out View) && View == Array.GetOuterBuffer().GetView());
				Assert.IsFalse(Field.AsArray().TryGetView(out View));
				Assert.IsFalse(NamedField.AsArray().TryGetView(out View));

//				// GetBuffer
//				Assert.IsTrue(Array.GetBuffer().Flatten().GetView() == Array.GetOuterBuffer().GetView());
//				Assert.IsTrue(CbField.MakeView(Field).AsArray().GetBuffer().Flatten().GetView().Span.SequenceEqual(Array.GetOuterBuffer().GetView()));
//				Assert.IsTrue(CbField.MakeView(NamedField).AsArray().GetBuffer().Flatten().GetView().Span.SequenceEqual(Array.GetOuterBuffer().GetView()));
			}

			// Test CbField(None) as Array
			{
				CbField Field = new CbField();
//				TestFieldError(CbFieldType.Array, Field, CbFieldError.TypeError);
				CbField.MakeView(Field).AsArray();
			}

			// Test CbArray(ArrayWithName) and CreateIterator
			{
				byte ArrayType = (byte)(CbFieldType.Array | CbFieldType.HasFieldName);
				byte[] Buffer = new byte[] { ArrayType, 3, (byte)'K', (byte)'e', (byte)'y', 3, 1, (byte)(CbFieldType.IntegerPositive), 8 };
				CbArray Array = new CbArray(Buffer);
				Assert.AreEqual(Array.GetSize(), 5);
				CbArray ArrayClone = CbArray.Clone(Array);
				Assert.AreEqual(ArrayClone.GetSize(), 5);
				Assert.IsTrue(Array.Equals(ArrayClone));
				Assert.AreEqual(ArrayClone.GetHash(), Array.GetHash());
				for (CbFieldIterator It = ArrayClone.CreateIterator(); It; ++It)
				{
					CbField Field = It.Current;
					Assert.AreEqual(Field.AsInt32(), 8);
//					Assert.IsTrue(Field.IsOwned());
				}
				for (CbFieldIterator It = ArrayClone.CreateIterator(), End = new CbFieldIterator(); It != End; ++It)
				{
				}
				foreach (CbField Field in ArrayClone)
				{
				}

				// CopyTo
				byte[] CopyBytes = new byte[5];
				Array.CopyTo(CopyBytes);
//				Assert.IsTrue(ArrayClone.GetOuterBuffer().GetView().Span.SequenceEqual(CopyBytes));
				ArrayClone.CopyTo(CopyBytes);
//				Assert.IsTrue(ArrayClone.GetOuterBuffer().GetView().Span.SequenceEqual(CopyBytes));

//				// GetBuffer
//				Assert.IsTrue(CbField(FSharedBuffer.MakeView(Buffer)).GetBuffer().Flatten().GetView().Span.SequenceEqual(Buffer));
//				Assert.IsTrue(TEXT("CbField(ArrayWithNameNoType).GetBuffer()"),
//					CbField(CbField(Buffer + 1, CbFieldType(ArrayType)), FSharedBuffer.MakeView(Buffer)).GetBuffer().Flatten().GetView().Span.SequenceEqual(Buffer));
			}

			// Test CbArray as CbFieldIterator
			{
				uint Count = 0;
				CbArray Array = new CbArray();
				for (CbFieldIterator Iter = CbFieldIterator.MakeSingle(Array.AsField()); Iter; ++Iter)
				{
					CbField Field = Iter.Current;
					Assert.IsTrue(Field.IsArray());
					++Count;
				}
				Assert.AreEqual(Count, 1u);
			}

			// Test CbArray as CbFieldIterator
			{
				uint Count = 0;
				CbArray Array = new CbArray();
//				Array.MakeOwned();
				for(CbFieldIterator Iter = CbFieldIterator.MakeSingle(Array.AsField()); Iter; ++Iter)
				{
					CbField Field = Iter.Current;
					Assert.IsTrue(Field.IsArray());
					++Count;
				}
				Assert.AreEqual(Count, 1u);
			}
		}

		[TestMethod]
		public void CbFieldBinaryTest()
		{
			// Test CbField(Binary, Empty)
			TestField(CbFieldType.Binary, new byte[]{0});

			// Test CbField(Binary, Value)
			{
				byte[] Payload = { 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
				CbField FieldView = new CbField(Payload, CbFieldType.Binary);
				TestField(CbFieldType.Binary, FieldView, (ReadOnlyMemory<byte>)Payload.AsMemory(1, 3));

				CbField Field = CbField.Clone(FieldView);
				Field.AsBinary();
//				Assert.IsFalse(Field.GetOuterBuffer().IsNull());
//				MoveTemp(Field).AsBinary();
//				Assert.IsTrue(Field.GetOuterBuffer().IsNull());
			}

			// Test CbField(None) as Binary
			{
				CbField FieldView = new CbField();
				byte[] Default = { 1, 2, 3 };
				TestFieldError(CbFieldType.Binary, FieldView, CbFieldError.TypeError, (ReadOnlyMemory<byte>)Default);

//				CbField Field = CbField.Clone(FieldView);
//				TestFieldError(CbFieldType.Binary, FSharedBuffer, Field, CbFieldError.TypeError, FSharedBuffer.MakeView(Default), FCbBinaryAccessors());
			}
		}

		[TestMethod]
		public void CbFieldStringTest()
		{
			// Test CbField(String, Empty)
			TestField(CbFieldType.String, new byte[]{0});

			// Test CbField(String, Value)
			{
				byte[] Payload = { 3, (byte)'A', (byte)'B', (byte)'C' }; // Size: 3, Data: ABC
				TestField(CbFieldType.String, Payload, new Utf8String(Payload.AsMemory(1, 3)));
			}

			// Test CbField(String, OutOfRangeSize)
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, (ulong)(1) << 31);
				TestFieldError(CbFieldType.String, Payload, CbFieldError.RangeError, new Utf8String("ABC"));
			}

			// Test CbField(None) as String
			{
				CbField Field = new CbField();
				TestFieldError(CbFieldType.String, Field, CbFieldError.TypeError, new Utf8String("ABC"));
			}
		}

		[Flags]
		enum EIntType : byte
		{
			None   = 0x00,
			Int8   = 0x01,
			Int16  = 0x02,
			Int32  = 0x04,
			Int64  = 0x08,
			UInt8  = 0x10,
			UInt16 = 0x20,
			UInt32 = 0x40,
			UInt64 = 0x80,
			// Masks for positive values requiring the specified number of bits.
			Pos64 = UInt64,
			Pos63 = Pos64 |  Int64,
			Pos32 = Pos63 | UInt32,
			Pos31 = Pos32 |  Int32,
			Pos16 = Pos31 | UInt16,
			Pos15 = Pos16 |  Int16,
			Pos8  = Pos15 | UInt8,
			Pos7  = Pos8  |  Int8,
			// Masks for negative values requiring the specified number of bits.
			Neg63 = Int64,
			Neg31 = Neg63 | Int32,
			Neg15 = Neg31 | Int16,
			Neg7  = Neg15 | Int8,
		};

		void TestIntegerField(CbFieldType FieldType, EIntType ExpectedMask, ulong Magnitude)
		{
			byte[] Payload = new byte[9];
			ulong Negative = (ulong)((byte)FieldType & 1);
			VarInt.Write(Payload, Magnitude - Negative);
			ulong DefaultValue = 8;
			ulong ExpectedValue = (Negative != 0)? (ulong)(-(long)(Magnitude)) : Magnitude;
			CbField Field = new CbField(Payload, FieldType);

			TestField(CbFieldType.IntegerNegative, Field, (sbyte)(((ExpectedMask & EIntType.Int8) != 0) ? ExpectedValue : DefaultValue),
				(sbyte)(DefaultValue), ((ExpectedMask & EIntType.Int8) != 0)? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<sbyte>(x => x.IsInteger(), (x, y) => x.AsInt8(y)));

			TestField(CbFieldType.IntegerNegative, Field, (short)(((ExpectedMask & EIntType.Int16) != 0)? ExpectedValue : DefaultValue),
				(short)(DefaultValue), ((ExpectedMask & EIntType.Int16) != 0)? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<short>(x => x.IsInteger(), (x, y) => x.AsInt16(y)));

			TestField(CbFieldType.IntegerNegative, Field, (int)(((ExpectedMask & EIntType.Int32) != 0)? ExpectedValue : DefaultValue),
				(int)(DefaultValue), ((ExpectedMask & EIntType.Int32) != 0)? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<int>(x => x.IsInteger(), (x, y) => x.AsInt32(y)));

			TestField(CbFieldType.IntegerNegative, Field, (long)(((ExpectedMask & EIntType.Int64) != 0)? ExpectedValue : DefaultValue),
				(long)(DefaultValue), ((ExpectedMask & EIntType.Int64) != 0)? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<long>(x => x.IsInteger(), (x, y) => x.AsInt64(y)));

			TestField(CbFieldType.IntegerPositive, Field, (byte)(((ExpectedMask & EIntType.UInt8) != 0) ? ExpectedValue : DefaultValue),
				(byte)(DefaultValue), ((ExpectedMask & EIntType.UInt8) != 0) ? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<byte>(x => x.IsInteger(), (x, y) => x.AsUInt8(y)));

			TestField(CbFieldType.IntegerPositive, Field, (ushort)(((ExpectedMask & EIntType.UInt16) != 0)? ExpectedValue : DefaultValue),
				(ushort)(DefaultValue), ((ExpectedMask & EIntType.UInt16) != 0) ? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<ushort>(x => x.IsInteger(), (x, y) => x.AsUInt16(y)));

			TestField(CbFieldType.IntegerPositive, Field, (uint)(((ExpectedMask & EIntType.UInt32) != 0) ? ExpectedValue : DefaultValue),
				(uint)(DefaultValue), ((ExpectedMask & EIntType.UInt32) != 0) ? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<uint>(x => x.IsInteger(), (x, y) => x.AsUInt32(y)));
			TestField(CbFieldType.IntegerPositive, Field, (ulong)(((ExpectedMask & EIntType.UInt64) != 0) ? ExpectedValue : DefaultValue),
				(ulong)(DefaultValue), ((ExpectedMask & EIntType.UInt64) != 0)? CbFieldError.None : CbFieldError.RangeError, CbFieldAccessors.FromStruct<ulong>(x => x.IsInteger(), (x, y) => x.AsUInt64(y)));
		}

		void CbFieldIntegerTest()
		{
			// Test CbField(IntegerPositive)
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos7,  0x00);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos7,  0x7f);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos8,  0x80);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos8,  0xff);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos15, 0x0100);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos15, 0x7fff);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos16, 0x8000);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos16, 0xffff);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos31, 0x0001_0000);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos31, 0x7fff_ffff);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos32, 0x8000_0000);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos32, 0xffff_ffff);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos63, 0x0000_0001_0000_0000);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos63, 0x7fff_ffff_ffff_ffff);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos64, 0x8000_0000_0000_0000);
			TestIntegerField(CbFieldType.IntegerPositive, EIntType.Pos64, 0xffff_ffff_ffff_ffff);

			// Test CbField(IntegerNegative)
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg7,  0x01);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg7,  0x80);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg15, 0x81);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg15, 0x8000);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg31, 0x8001);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg31, 0x8000_0000);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg63, 0x8000_0001);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.Neg63, 0x8000_0000_0000_0000);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.None,  0x8000_0000_0000_0001);
			TestIntegerField(CbFieldType.IntegerNegative, EIntType.None,  0xffff_ffff_ffff_ffff);

			// Test CbField(None) as Integer
			{
				CbField Field = new CbField();
				TestFieldError(CbFieldType.IntegerPositive, Field, CbFieldError.TypeError, (ulong)(8));
				TestFieldError(CbFieldType.IntegerNegative, Field, CbFieldError.TypeError, (long)(8));
			}
		}

		[TestMethod]
		public void CbFieldFloatTest()
		{
			// Test CbField(Float, 32-bit)
			{
				byte[] Payload = new byte[]{ 0xc0, 0x12, 0x34, 0x56 }; // -2.28444433f
				TestField(CbFieldType.Float32, Payload, -2.28444433f);

				CbField Field = new CbField(Payload, CbFieldType.Float32);
				TestField(CbFieldType.Float64, Field, (double)-2.28444433f);
			}

			// Test CbField(Float, 64-bit)
			{
				byte[] Payload = new byte[]{ 0xc1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }; // -631475.76888888876
				TestField(CbFieldType.Float64, Payload, -631475.76888888876);

				CbField Field = new CbField(Payload, CbFieldType.Float64);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
			}

			// Test CbField(Integer+, MaxBinary32) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ((ulong)(1) << 24) - 1); // 16,777,215
				CbField Field = new CbField(Payload, CbFieldType.IntegerPositive);
				TestField(CbFieldType.Float32, Field, 16_777_215.0f);
				TestField(CbFieldType.Float64, Field, 16_777_215.0);
			}

			// Test CbField(Integer+, MaxBinary32+1) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, (ulong)(1) << 24); // 16,777,216
				CbField Field = new CbField(Payload, CbFieldType.IntegerPositive);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestField(CbFieldType.Float64, Field, 16_777_216.0);
			}

			// Test CbField(Integer+, MaxBinary64) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ((ulong)(1) << 53) - 1); // 9,007,199,254,740,991
				CbField Field = new CbField(Payload, CbFieldType.IntegerPositive);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestField(CbFieldType.Float64, Field, 9_007_199_254_740_991.0);
			}

			// Test CbField(Integer+, MaxBinary64+1) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, (ulong)(1) << 53); // 9,007,199,254,740,992
				CbField Field = new CbField(Payload, CbFieldType.IntegerPositive);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestFieldError(CbFieldType.Float64, Field, CbFieldError.RangeError, 8.0);
			}

			// Test CbField(Integer+, MaxUInt64) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ~(ulong)0); // Max uint64
				CbField Field = new CbField(Payload, CbFieldType.IntegerPositive);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestFieldError(CbFieldType.Float64, Field, CbFieldError.RangeError, 8.0);
			}

			// Test CbField(Integer-, MaxBinary32) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ((ulong)(1) << 24) - 2); // -16,777,215
				CbField Field = new CbField(Payload, CbFieldType.IntegerNegative);
				TestField(CbFieldType.Float32, Field, -16_777_215.0f);
				TestField(CbFieldType.Float64, Field, -16_777_215.0);
			}

			// Test CbField(Integer-, MaxBinary32+1) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ((ulong)(1) << 24) - 1); // -16,777,216
				CbField Field = new CbField(Payload, CbFieldType.IntegerNegative);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestField(CbFieldType.Float64, Field, -16_777_216.0);
			}

			// Test CbField(Integer-, MaxBinary64) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ((ulong)(1) << 53) - 2); // -9,007,199,254,740,991
				CbField Field = new CbField(Payload, CbFieldType.IntegerNegative);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestField(CbFieldType.Float64, Field, -9_007_199_254_740_991.0);
			}

			// Test CbField(Integer-, MaxBinary64+1) as Float
			{
				byte[] Payload = new byte[9];
				VarInt.Write(Payload, ((ulong)(1) << 53) - 1); // -9,007,199,254,740,992
				CbField Field = new CbField(Payload, CbFieldType.IntegerNegative);
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.RangeError, 8.0f);
				TestFieldError(CbFieldType.Float64, Field, CbFieldError.RangeError, 8.0);
			}

			// Test CbField(None) as Float
			{
				CbField Field = new CbField();
				TestFieldError(CbFieldType.Float32, Field, CbFieldError.TypeError, 8.0f);
				TestFieldError(CbFieldType.Float64, Field, CbFieldError.TypeError, 8.0);
			}
		}

		[TestMethod]
		public void CbFieldBoolTest()
		{
			// Test CbField(Bool, False)
			TestField(CbFieldType.BoolFalse, Array.Empty<byte>(), false, true);

			// Test CbField(Bool, True)
			TestField(CbFieldType.BoolTrue, Array.Empty<byte>(), true, false);

			// Test CbField(None) as Bool
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.BoolFalse, DefaultField, CbFieldError.TypeError, false);
				TestFieldError(CbFieldType.BoolTrue, DefaultField, CbFieldError.TypeError, true);
			}
		}

		[TestMethod]
		public void CbFieldObjectAttachmentTest()
		{
			byte[] ZeroBytes = new byte[20];
			byte[] SequentialBytes = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

			// Test CbField(ObjectAttachment, Zero)
			TestField(CbFieldType.ObjectAttachment, ZeroBytes);

			// Test CbField(ObjectAttachment, NonZero)
			TestField(CbFieldType.ObjectAttachment, SequentialBytes, new IoHash(SequentialBytes));

			// Test CbField(ObjectAttachment, NonZero) AsAttachment
			{
				CbField Field = new CbField(SequentialBytes, CbFieldType.ObjectAttachment);
				TestField(CbFieldType.ObjectAttachment, Field, new IoHash(SequentialBytes), new IoHash(), CbFieldError.None);
			}

			// Test CbField(None) as ObjectAttachment
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.ObjectAttachment, DefaultField, CbFieldError.TypeError, new IoHash(SequentialBytes));
			}
		}

		[TestMethod]
		public void CbFieldBinaryAttachmentTest()
		{
			byte[] ZeroBytes = new byte[20];
			byte[] SequentialBytes = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

			// Test CbField(BinaryAttachment, Zero)
			TestField(CbFieldType.BinaryAttachment, ZeroBytes);

			// Test CbField(BinaryAttachment, NonZero)
			TestField(CbFieldType.BinaryAttachment, SequentialBytes, new IoHash(SequentialBytes));

			// Test CbField(BinaryAttachment, NonZero) AsAttachment
			{
				CbField Field = new CbField(SequentialBytes, CbFieldType.BinaryAttachment);
				TestField(CbFieldType.BinaryAttachment, Field, new IoHash(SequentialBytes), new IoHash(), CbFieldError.None);
			}

			// Test CbField(None) as BinaryAttachment
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.BinaryAttachment, DefaultField, CbFieldError.TypeError, new IoHash(SequentialBytes));
			}
		}

		[TestMethod]
		public void CbFieldHashTest()
		{
			byte[] ZeroBytes = new byte[20];
			byte[] SequentialBytes = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

			// Test CbField(Hash, Zero)
			TestField(CbFieldType.Hash, ZeroBytes);

			// Test CbField(Hash, NonZero)
			TestField(CbFieldType.Hash, SequentialBytes, new IoHash(SequentialBytes));

			// Test CbField(None) as Hash
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.Hash, DefaultField, CbFieldError.TypeError, new IoHash(SequentialBytes));
			}

			// Test CbField(ObjectAttachment) as Hash
			{
				CbField Field = new CbField(SequentialBytes, CbFieldType.ObjectAttachment);
				TestField(CbFieldType.Hash, Field, new IoHash(SequentialBytes));
			}

			// Test CbField(BinaryAttachment) as Hash
			{
				CbField Field = new CbField(SequentialBytes, CbFieldType.BinaryAttachment);
				TestField(CbFieldType.Hash, Field, new IoHash(SequentialBytes));
			}
		}

		[TestMethod]
		public void CbFieldUuidTest()
		{
			byte[] ZeroBytes = new byte[]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
			byte[] SequentialBytes = new byte[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
			Guid SequentialGuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d0e0f");

			// Test CbField(Uuid, Zero)
			TestField(CbFieldType.Uuid, ZeroBytes, new Guid(), SequentialGuid);

			// Test CbField(Uuid, NonZero)
			TestField(CbFieldType.Uuid, SequentialBytes, SequentialGuid, new Guid());

			// Test CbField(None) as Uuid
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.Uuid, DefaultField, CbFieldError.TypeError, Guid.NewGuid());
			}
		}

		[TestMethod]
		public void CbFieldDateTimeTest()
		{
			// Test CbField(DateTime, Zero)
			TestField(CbFieldType.DateTime, new byte[]{0, 0, 0, 0, 0, 0, 0, 0});

			// Test CbField(DateTime, 0x1020_3040_5060_7080)
			TestField(CbFieldType.DateTime, new byte[]{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}, new DateTime(0x1020_3040_5060_7080L, DateTimeKind.Utc));

			// Test CbField(DateTime, Zero) as FDateTime
			{
				byte[] Payload = new byte[]{0, 0, 0, 0, 0, 0, 0, 0};
				CbField Field = new CbField(Payload, CbFieldType.DateTime);
				Assert.AreEqual(Field.AsDateTime(), new DateTime(0));
			}

			// Test CbField(None) as DateTime
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.DateTime, DefaultField, CbFieldError.TypeError);
				DateTime DefaultValue = new DateTime(0x1020_3040_5060_7080L, DateTimeKind.Utc);
				Assert.AreEqual(DefaultField.AsDateTime(DefaultValue), DefaultValue);
			}
		}

		[TestMethod]
		public void CbFieldTimeSpanTest()
		{
			// Test CbField(TimeSpan, Zero)
			TestField(CbFieldType.TimeSpan, new byte[] {0, 0, 0, 0, 0, 0, 0, 0});

			// Test CbField(TimeSpan, 0x1020_3040_5060_7080)
			TestField(CbFieldType.TimeSpan, new byte[] {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80}, new TimeSpan(0x1020_3040_5060_7080L));

			// Test CbField(TimeSpan, Zero) as FTimeSpan
			{
				byte[] Payload = new byte[]{0, 0, 0, 0, 0, 0, 0, 0};
				CbField Field = new CbField(Payload, CbFieldType.TimeSpan);
				Assert.AreEqual(Field.AsTimeSpan(), new TimeSpan(0));
			}

			// Test CbField(None) as TimeSpan
			{
				CbField DefaultField = new CbField();
				TestFieldError(CbFieldType.TimeSpan, DefaultField, CbFieldError.TypeError);
				TimeSpan DefaultValue = new TimeSpan(0x1020_3040_5060_7080L);
				Assert.AreEqual(DefaultField.AsTimeSpan(DefaultValue), DefaultValue);
			}
		}

#if false
		[TestMethod]
		public void CbFieldObjectIdTest()
		{
			// Test CbField(ObjectId, Zero)
			TestField(CbFieldType.ObjectId, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

			// Test CbField(ObjectId, 0x102030405060708090A0B0C0)
			TestField(CbFieldType.ObjectId, {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0},
				FCbObjectId(MakeMemoryView<byte>({0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0})));

			// Test CbField(ObjectId, Zero) as FCbObjectId
			{
				byte[] Payload = new byte[]{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
				CbField Field = new CbField(Payload, CbFieldType.ObjectId);
				Assert.AreEqual(Field.AsObjectId(), FCbObjectId());
			}

			// Test CbField(None) as ObjectId
			{
				CbField DefaultField;
				TestFieldError(CbFieldType.ObjectId, DefaultField, CbFieldError.TypeError);
				FCbObjectId DefaultValue(MakeMemoryView<byte>({0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0}));
				Assert.AreEqual(DefaultField.AsObjectId(DefaultValue), DefaultValue);
			}
		}

		[TestMethod]
		public void CbFieldCustomByIdTest()
		{
			struct FCustomByIdAccessor
			{
				explicit FCustomByIdAccessor(uint64 Id)
					: AsType([Id](CbField& Field, FMemoryView Default) { return Field.AsCustom(Id, Default); })
				{
				}

				bool (CbField.*IsType)() = &CbField.IsCustomById;
				TUniqueFunction<FMemoryView (CbField& Field, FMemoryView Default)> AsType;
			};

			// Test CbField(CustomById, MinId, Empty)
			{
				byte[] Payload = new byte[]{1, 0};
				TestField(CbFieldType.CustomById, Payload, FCbCustomById{0});
				TestField(CbFieldType.CustomById, Payload, FMemoryView(), MakeMemoryView<byte>({1, 2, 3}), CbFieldError.None, FCustomByIdAccessor(0));
				TestFieldError(CbFieldType.CustomById, Payload, CbFieldError.RangeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByIdAccessor(MAX_uint64));
			}

			// Test CbField(CustomById, MinId, Value)
			{
				byte[] Payload = new byte[]{5, 0, 1, 2, 3, 4};
				TestFieldNoClone(CbFieldType.CustomById, Payload, FCbCustomById{0, Payload.Right(4)});
				TestFieldNoClone(CbFieldType.CustomById, Payload, Payload.Right(4), FMemoryView(), CbFieldError.None, FCustomByIdAccessor(0));
				TestFieldError(CbFieldType.CustomById, Payload, CbFieldError.RangeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByIdAccessor(MAX_uint64));
			}

			// Test CbField(CustomById, MaxId, Empty)
			{
				byte[] Payload = new byte[]{9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
				TestField(CbFieldType.CustomById, Payload, FCbCustomById{MAX_uint64});
				TestField(CbFieldType.CustomById, Payload, FMemoryView(), MakeMemoryView<byte>({1, 2, 3}), CbFieldError.None, FCustomByIdAccessor(MAX_uint64));
				TestFieldError(CbFieldType.CustomById, Payload, CbFieldError.RangeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByIdAccessor(0));
			}

			// Test CbField(CustomById, MaxId, Value)
			{
				byte[] Payload = new byte[]{13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1, 2, 3, 4};
				TestFieldNoClone(CbFieldType.CustomById, Payload, FCbCustomById{MAX_uint64, Payload.Right(4)});
				TestFieldNoClone(CbFieldType.CustomById, Payload, Payload.Right(4), FMemoryView(), CbFieldError.None, FCustomByIdAccessor(MAX_uint64));
				TestFieldError(CbFieldType.CustomById, Payload, CbFieldError.RangeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByIdAccessor(0));
			}

			// Test CbField(None) as CustomById
			{
				CbField DefaultField;
				TestFieldError(CbFieldType.CustomById, DefaultField, CbFieldError.TypeError, FCbCustomById{4, MakeMemoryView<byte>({1, 2, 3})});
				TestFieldError(CbFieldType.CustomById, DefaultField, CbFieldError.TypeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByIdAccessor(0));
				byte[] DefaultValue = new byte[]{1, 2, 3};
				Assert.AreEqual(DefaultField.AsCustom(0, DefaultValue), DefaultValue);
			}

			return true;
		}

		[TestMethod]
		void CbFieldCustomByNameTest()
		{
			struct FCustomByNameAccessor
			{
				explicit FCustomByNameAccessor(FAnsiStringView Name)
					: AsType([Name = FString(Name)](CbField& Field, FMemoryView Default) { return Field.AsCustom(TCHAR_TO_ANSI(*Name), Default); })
				{
				}

				bool (CbField.*IsType)() = &CbField.IsCustomByName;
				TUniqueFunction<FMemoryView (CbField& Field, FMemoryView Default)> AsType;
			};

			// Test CbField(CustomByName, ABC, Empty)
			{
				byte[] Payload = new byte[]{4, 3, 'A', 'B', 'C'};
				TestField(CbFieldType.CustomByName, Payload, FCbCustomByName{"ABC"});
				TestField(CbFieldType.CustomByName, Payload, FMemoryView(), MakeMemoryView<byte>({1, 2, 3}), CbFieldError.None, FCustomByNameAccessor("ABC"));
				TestFieldError(CbFieldType.CustomByName, Payload, CbFieldError.RangeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByNameAccessor("abc"));
			}

			// Test CbField(CustomByName, ABC, Value)
			{
				byte[] Payload = new byte[]{8, 3, 'A', 'B', 'C', 1, 2, 3, 4};
				TestFieldNoClone(CbFieldType.CustomByName, Payload, FCbCustomByName{"ABC", Payload.Right(4)});
				TestFieldNoClone(CbFieldType.CustomByName, Payload, Payload.Right(4), FMemoryView(), CbFieldError.None, FCustomByNameAccessor("ABC"));
				TestFieldError(CbFieldType.CustomByName, Payload, CbFieldError.RangeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByNameAccessor("abc"));
			}

			// Test CbField(None) as CustomByName
			{
				CbField DefaultField;
				TestFieldError(CbFieldType.CustomByName, DefaultField, CbFieldError.TypeError, FCbCustomByName{"ABC", MakeMemoryView<byte>({1, 2, 3})});
				TestFieldError(CbFieldType.CustomByName, DefaultField, CbFieldError.TypeError, MakeMemoryView<byte>({1, 2, 3}), FCustomByNameAccessor("ABC"));
				byte[] DefaultValue = new byte[]{1, 2, 3};
				Assert.AreEqual(DefaultField.AsCustom("ABC", DefaultValue), DefaultValue);
			}
		}

		[TestMethod]
		public void CbFieldIterateAttachmentsTest()
		{
			Func<uint, IoHash> MakeTestHash = (uint Index) =>
			{
				byte[] Data = new byte[4];
				BinaryPrimitives.WriteUInt32LittleEndian(Data, Index);
				return IoHash.Compute(Data);
			};

			CbFieldIterator Fields;
			{
				CbWriter Writer = new CbWriter();

				Writer.SetName("IgnoredTypeInRoot").AddHash(MakeTestHash(100));
				Writer.AddObjectAttachment(MakeTestHash(0));
				Writer.AddBinaryAttachment(MakeTestHash(1));
				Writer.SetName("ObjAttachmentInRoot").AddObjectAttachment(MakeTestHash(2));
				Writer.SetName("BinAttachmentInRoot").AddBinaryAttachment(MakeTestHash(3));

				// Uniform array of type to ignore.
				Writer.BeginArray();
				{
					Writer << 1;
					Writer << 2;
				}
				Writer.EndArray();
				// Uniform array of binary attachments.
				Writer.BeginArray();
				{
					Writer.AddBinaryAttachment(MakeTestHash(4));
					Writer.AddBinaryAttachment(MakeTestHash(5));
				}
				Writer.EndArray();
				// Uniform array of uniform arrays.
				Writer.BeginArray();
				{
					Writer.BeginArray();
					Writer.AddBinaryAttachment(MakeTestHash(6));
					Writer.AddBinaryAttachment(MakeTestHash(7));
					Writer.EndArray();
					Writer.BeginArray();
					Writer.AddBinaryAttachment(MakeTestHash(8));
					Writer.AddBinaryAttachment(MakeTestHash(9));
					Writer.EndArray();
				}
				Writer.EndArray();
				// Uniform array of non-uniform arrays.
				Writer.BeginArray();
				{
					Writer.BeginArray();
					Writer << 0;
					Writer << false;
					Writer.EndArray();
					Writer.BeginArray();
					Writer.AddObjectAttachment(MakeTestHash(10));
					Writer << false;
					Writer.EndArray();
				}
				Writer.EndArray();
				// Uniform array of uniform objects.
				Writer.BeginArray();
				{
					Writer.BeginObject();
					Writer.SetName("ObjAttachmentInUniObjInUniObj1").AddObjectAttachment(MakeTestHash(11));
					Writer.SetName("ObjAttachmentInUniObjInUniObj2").AddObjectAttachment(MakeTestHash(12));
					Writer.EndObject();
					Writer.BeginObject();
					Writer.SetName("ObjAttachmentInUniObjInUniObj3").AddObjectAttachment(MakeTestHash(13));
					Writer.SetName("ObjAttachmentInUniObjInUniObj4").AddObjectAttachment(MakeTestHash(14));
					Writer.EndObject();
				}
				Writer.EndArray();
				// Uniform array of non-uniform objects.
				Writer.BeginArray();
				{
					Writer.BeginObject();
					Writer << "Int" << 0;
					Writer << "Bool" << false;
					Writer.EndObject();
					Writer.BeginObject();
					Writer.SetName("ObjAttachmentInNonUniObjInUniObj").AddObjectAttachment(MakeTestHash(15));
					Writer << "Bool" << false;
					Writer.EndObject();
				}
				Writer.EndArray();

				// Uniform object of type to ignore.
				Writer.BeginObject();
				{
					Writer << "Int1" << 1;
					Writer << "Int2" << 2;
				}
				Writer.EndObject();
				// Uniform object of binary attachments.
				Writer.BeginObject();
				{
					Writer.SetName("BinAttachmentInUniObj1").AddBinaryAttachment(MakeTestHash(16));
					Writer.SetName("BinAttachmentInUniObj2").AddBinaryAttachment(MakeTestHash(17));
				}
				Writer.EndObject();
				// Uniform object of uniform arrays.
				Writer.BeginObject();
				{
					Writer.SetName("Array1");
					Writer.BeginArray();
					Writer.AddBinaryAttachment(MakeTestHash(18));
					Writer.AddBinaryAttachment(MakeTestHash(19));
					Writer.EndArray();
					Writer.SetName("Array2");
					Writer.BeginArray();
					Writer.AddBinaryAttachment(MakeTestHash(20));
					Writer.AddBinaryAttachment(MakeTestHash(21));
					Writer.EndArray();
				}
				Writer.EndObject();
				// Uniform object of non-uniform arrays.
				Writer.BeginObject();
				{
					Writer.SetName("Array1");
					Writer.BeginArray();
					Writer << 0;
					Writer << false;
					Writer.EndArray();
					Writer.SetName("Array2");
					Writer.BeginArray();
					Writer.AddObjectAttachment(MakeTestHash(22));
					Writer << false;
					Writer.EndArray();
				}
				Writer.EndObject();
				// Uniform object of uniform objects.
				Writer.BeginObject();
				{
					Writer.SetName("Object1");
					Writer.BeginObject();
					Writer.SetName("ObjAttachmentInUniObjInUniObj1").AddObjectAttachment(MakeTestHash(23));
					Writer.SetName("ObjAttachmentInUniObjInUniObj2").AddObjectAttachment(MakeTestHash(24));
					Writer.EndObject();
					Writer.SetName("Object2");
					Writer.BeginObject();
					Writer.SetName("ObjAttachmentInUniObjInUniObj3").AddObjectAttachment(MakeTestHash(25));
					Writer.SetName("ObjAttachmentInUniObjInUniObj4").AddObjectAttachment(MakeTestHash(26));
					Writer.EndObject();
				}
				Writer.EndObject();
				// Uniform object of non-uniform objects.
				Writer.BeginObject();
				{
					Writer.SetName("Object1");
					Writer.BeginObject();
					Writer << "Int" << 0;
					Writer << "Bool" << false;
					Writer.EndObject();
					Writer.SetName("Object2");
					Writer.BeginObject();
					Writer.SetName("ObjAttachmentInNonUniObjInUniObj").AddObjectAttachment(MakeTestHash(27));
					Writer << "Bool" << false;
					Writer.EndObject();
				}
				Writer.EndObject();

				Fields = Writer.Save();
			}

			Assert.AreEqual(ValidateCompactBinaryRange(Fields.GetOuterBuffer(), ECbValidateMode.All), ECbValidateError.None);

			uint AttachmentIndex = 0;
			Fields.IterateRangeAttachments([this, &AttachmentIndex, &MakeTestHash](CbField Field)
				{
					Assert.IsTrue(FString.Printf(AttachmentIndex), Field.IsAttachment());
					Assert.AreEqual(FString.Printf(AttachmentIndex), Field.AsAttachment(), MakeTestHash(AttachmentIndex));
					++AttachmentIndex;
				});
			Assert.AreEqual(AttachmentIndex, 28);
		}

		[TestMethod]
		void CbFieldBufferTest()
		{
			static_assert(std.is_constructible<CbField>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&&>.value, "Missing constructor for CbField");

			static_assert(std.is_constructible<CbField, FSharedBuffer&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, FSharedBuffer&&>.value, "Missing constructor for CbField");

			static_assert(std.is_constructible<CbField, CbField&, FSharedBuffer&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, CbFieldIterator&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, CbField&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, CbArray&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, FCbObject&>.value, "Missing constructor for CbField");

			static_assert(std.is_constructible<CbField, CbField&, FSharedBuffer&&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, CbFieldIterator&&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, CbField&&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, CbArray&&>.value, "Missing constructor for CbField");
			static_assert(std.is_constructible<CbField, CbField&, FCbObject&&>.value, "Missing constructor for CbField");

			// Test CbField()
			{
				CbField DefaultField;
				Assert.IsFalse(DefaultField.HasValue());
				Assert.IsFalse(DefaultField.IsOwned());
				DefaultField.MakeOwned();
				Assert.IsTrue(DefaultField.IsOwned());
			}

			// Test Field w/ Type from Shared Buffer
			{
				byte[] Payload = new byte[]{ (byte)(CbFieldType.Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
				FSharedBuffer ViewBuffer = FSharedBuffer.MakeView(Payload);
				FSharedBuffer OwnedBuffer = FSharedBuffer.Clone(ViewBuffer);

				CbField View(ViewBuffer);
				CbField ViewMove{FSharedBuffer(ViewBuffer)};
				CbField ViewOuterField(ImplicitConv<CbField>(View), ViewBuffer);
				CbField ViewOuterBuffer(ImplicitConv<CbField>(View), View);
				CbField Owned(OwnedBuffer);
				CbField OwnedMove{FSharedBuffer(OwnedBuffer)};
				CbField OwnedOuterField(ImplicitConv<CbField>(Owned), OwnedBuffer);
				CbField OwnedOuterBuffer(ImplicitConv<CbField>(Owned), Owned);

				// These lines are expected to assert when uncommented.
				//CbField InvalidOuterBuffer(ImplicitConv<CbField>(Owned), ViewBuffer);
				//CbField InvalidOuterBufferMove(ImplicitConv<CbField>(Owned), FSharedBuffer(ViewBuffer));

				Assert.AreEqual(View.AsBinaryView(), ViewBuffer.GetView().Right(3));
				Assert.AreEqual(ViewMove.AsBinaryView(), View.AsBinaryView());
				Assert.AreEqual(ViewOuterField.AsBinaryView(), View.AsBinaryView());
				Assert.AreEqual(ViewOuterBuffer.AsBinaryView(), View.AsBinaryView());
				Assert.AreEqual(Owned.AsBinaryView(), OwnedBuffer.GetView().Right(3));
				Assert.AreEqual(OwnedMove.AsBinaryView(), Owned.AsBinaryView());
				Assert.AreEqual(OwnedOuterField.AsBinaryView(), Owned.AsBinaryView());
				Assert.AreEqual(OwnedOuterBuffer.AsBinaryView(), Owned.AsBinaryView());

				Assert.IsFalse(View.IsOwned());
				Assert.IsFalse(ViewMove.IsOwned());
				Assert.IsFalse(ViewOuterField.IsOwned());
				Assert.IsFalse(ViewOuterBuffer.IsOwned());
				Assert.IsTrue(Owned.IsOwned());
				Assert.IsTrue(OwnedMove.IsOwned());
				Assert.IsTrue(OwnedOuterField.IsOwned());
				Assert.IsTrue(OwnedOuterBuffer.IsOwned());

				View.MakeOwned();
				Owned.MakeOwned();
				Assert.AreNotEqual(View.AsBinaryView(), ViewBuffer.GetView().Right(3));
				Assert.IsTrue(View.IsOwned());
				Assert.AreEqual(Owned.AsBinaryView(), OwnedBuffer.GetView().Right(3));
				Assert.IsTrue(Owned.IsOwned());
			}

			// Test Field w/ Type
			{
				byte[] Payload = new byte[]{ (byte)(CbFieldType.Binary), 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
				CbField Field = new CbField(Payload);

				CbField VoidView = CbField.MakeView(Payload);
				CbField VoidClone = CbField.Clone(Payload);
				CbField FieldView = CbField.MakeView(Field);
				CbField FieldClone = CbField.Clone(Field);
				CbField FieldViewClone = CbField.Clone(FieldView);

				Assert.AreEqual(VoidView.AsBinaryView(), Payload.Right(3));
				Assert.AreNotEqual(VoidClone.AsBinaryView(), Payload.Right(3));
				Assert.IsTrue(VoidClone.AsBinaryView().Span.SequenceEqual(VoidView.AsBinaryView()));
				Assert.AreEqual(FieldView.AsBinaryView(), Payload.Right(3));
				Assert.AreNotEqual(FieldClone.AsBinaryView(), Payload.Right(3));
				Assert.IsTrue(FieldClone.AsBinaryView().Span.SequenceEqual(VoidView.AsBinaryView()));
				Assert.AreNotEqual(FieldViewClone.AsBinaryView(), FieldView.AsBinaryView());
				Assert.IsTrue(FieldViewClone.AsBinaryView().Span.SequenceEqual(VoidView.AsBinaryView()));

				Assert.IsFalse(VoidView.IsOwned());
				Assert.IsTrue(VoidClone.IsOwned());
				Assert.IsFalse(FieldView.IsOwned());
				Assert.IsTrue(FieldClone.IsOwned());
				Assert.IsTrue(FieldViewClone.IsOwned());
			}

			// Test Field w/o Type
			{
				byte[] Payload = new byte[]{ 3, 4, 5, 6 }; // Size: 3, Data: 4/5/6
				CbField Field = new CbField(Payload, CbFieldType.Binary);

				CbField FieldView = CbField.MakeView(Field);
				CbField FieldClone = CbField.Clone(Field);
				CbField FieldViewClone = CbField.Clone(FieldView);

				Assert.AreEqual(FieldView.AsBinaryView(), Payload.Right(3));
				Assert.IsTrue(FieldClone.AsBinaryView().Span.SequenceEqual(FieldView.AsBinaryView()));
				Assert.IsTrue(FieldViewClone.AsBinaryView().Span.SequenceEqual(FieldView.AsBinaryView()));

				Assert.IsFalse(FieldView.IsOwned());
				Assert.IsTrue(FieldClone.IsOwned());
				Assert.IsTrue(FieldViewClone.IsOwned());

				FieldView.MakeOwned();
				Assert.IsTrue(FieldView.AsBinaryView().Span.SequenceEqual(Payload.Right(3)));
				Assert.IsTrue(FieldView.IsOwned());
			}

			return true;
		}

		[TestMethod]
		void CbArrayBufferTest()
		{
			static_assert(std.is_constructible<CbArray>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&&>.value, "Missing constructor for CbArray");

			static_assert(std.is_constructible<CbArray, CbArray&, FSharedBuffer&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, CbFieldIterator&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, CbField&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, CbArray&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, FCbObject&>.value, "Missing constructor for CbArray");

			static_assert(std.is_constructible<CbArray, CbArray&, FSharedBuffer&&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, CbFieldIterator&&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, CbField&&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, CbArray&&>.value, "Missing constructor for CbArray");
			static_assert(std.is_constructible<CbArray, CbArray&, FCbObject&&>.value, "Missing constructor for CbArray");

			// Test CbArray()
			{
				CbArray DefaultArray;
				Assert.IsFalse(DefaultArray.IsOwned());
				DefaultArray.MakeOwned();
				Assert.IsTrue(DefaultArray.IsOwned());
			}
		}

		[TestMethod]
		public void CbObjectBufferTest()
		{
			static_assert(std.is_constructible<FCbObject>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObject&&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObject&&>.value, "Missing constructor for FCbObject");

			static_assert(std.is_constructible<FCbObject, FCbObjectView&, FSharedBuffer&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, CbFieldIterator&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, CbField&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, CbArray&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, FCbObject&>.value, "Missing constructor for FCbObject");

			static_assert(std.is_constructible<FCbObject, FCbObjectView&, FSharedBuffer&&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, CbFieldIterator&&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, CbField&&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, CbArray&&>.value, "Missing constructor for FCbObject");
			static_assert(std.is_constructible<FCbObject, FCbObjectView&, FCbObject&&>.value, "Missing constructor for FCbObject");

			// Test FCbObject()
			{
				FCbObject DefaultObject;
				Assert.IsFalse(DefaultObject.IsOwned());
				DefaultObject.MakeOwned();
				Assert.IsTrue(DefaultObject.IsOwned());
			}

			return true;
		}

		[TestMethod]
		public void CbFieldBufferIterator()
		{
			static_assert(std.is_constructible<CbFieldIterator, CbFieldIterator&>.value, "Missing constructor for CbFieldIterator");
			static_assert(std.is_constructible<CbFieldIterator, CbFieldIterator&&>.value, "Missing constructor for CbFieldIterator");

			static_assert(std.is_constructible<CbFieldIterator, CbFieldIterator&>.value, "Missing constructor for CbFieldIterator");
			static_assert(std.is_constructible<CbFieldIterator, CbFieldIterator&&>.value, "Missing constructor for CbFieldIterator");

			auto GetCount = [](auto It) -> uint
			{
				uint Count = 0;
				for (; It; ++It)
				{
					++Count;
				}
				return Count;
			};

			// Test CbField[View]Iterator()
			{
				Assert.AreEqual(GetCount(CbFieldIterator()), 0);
				Assert.AreEqual(GetCount(CbFieldIterator()), 0);
			}

			// Test CbField[View]Iterator(Range)
			{
				byte T = (byte)(CbFieldType.IntegerPositive);
				byte[] Payload = new byte[]{ T, 0, T, 1, T, 2, T, 3 };

				FSharedBuffer View = FSharedBuffer.MakeView(Payload);
				FSharedBuffer Clone = FSharedBuffer.Clone(View);

				FMemoryView EmptyView;
				FSharedBuffer NullBuffer;

				CbFieldIterator FieldViewIt = CbFieldIterator.MakeRange(View);
				CbFieldIterator FieldIt = CbFieldIterator.MakeRange(View);

				Assert.AreEqual(FieldViewIt.GetRangeHash(), FBlake3.HashBuffer(View));
				Assert.AreEqual(FieldIt.GetRangeHash(), FBlake3.HashBuffer(View));

				FMemoryView RangeView;
				Assert.IsTrue(FieldViewIt.TryGetRangeView(RangeView) && RangeView == Payload);
				Assert.IsTrue(FieldIt.TryGetRangeView(RangeView) && RangeView == Payload);

				Assert.AreEqual(GetCount(CbFieldIterator.CloneRange(CbFieldIterator())), 0);
				Assert.AreEqual(GetCount(CbFieldIterator.CloneRange(CbFieldIterator())), 0);
				CbFieldIterator FieldViewItClone = CbFieldIterator.CloneRange(FieldViewIt);
				CbFieldIterator FieldItClone = CbFieldIterator.CloneRange(FieldIt);
				Assert.AreEqual(GetCount(FieldViewItClone), 4);
				Assert.AreEqual(GetCount(FieldItClone), 4);
				Assert.AreNotEqual(FieldViewItClone, FieldIt);
				Assert.AreNotEqual(FieldItClone, FieldIt);

				Assert.AreEqual(GetCount(CbFieldIterator.MakeRange(EmptyView)), 0);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRange(NullBuffer)), 0);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRange(FSharedBuffer(NullBuffer))), 0);

				Assert.AreEqual(GetCount(CbFieldIterator.MakeRange(Payload)), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRange(Clone)), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRange(FSharedBuffer(Clone))), 4);

				Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(View), NullBuffer)), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(View), FSharedBuffer(NullBuffer))), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(View), View)), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(View), FSharedBuffer(View))), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(Clone), Clone)), 4);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(Clone), FSharedBuffer(Clone))), 4);

				Assert.AreEqual(GetCount(CbFieldIterator(FieldIt)), 4);
				Assert.AreEqual(GetCount(CbFieldIterator(CbFieldIterator(FieldIt))), 4);

				// Uniform
				byte[] UniformPayload = new byte[]{ 0, 1, 2, 3 };
				CbFieldIterator UniformFieldViewIt = CbFieldIterator.MakeRange(UniformPayload, CbFieldType.IntegerPositive);
				Assert.AreEqual(UniformFieldViewIt.GetRangeHash(), FieldViewIt.GetRangeHash());
				Assert.IsFalse(UniformFieldViewIt.TryGetRangeView(RangeView));
				FSharedBuffer UniformView = FSharedBuffer.MakeView(UniformPayload);
				CbFieldIterator UniformFieldIt = CbFieldIterator.MakeRange(UniformView, CbFieldType.IntegerPositive);
				Assert.AreEqual(UniformFieldIt.GetRangeHash(), FieldViewIt.GetRangeHash());
				Assert.IsFalse(UniformFieldIt.TryGetRangeView(RangeView));

				// Equals
				Assert.IsTrue(FieldViewIt.Equals(FieldViewIt));
				Assert.IsTrue(FieldViewIt.Equals(FieldIt));
				Assert.IsTrue(FieldIt.Equals(FieldIt));
				Assert.IsTrue(FieldIt.Equals(FieldViewIt));
				Assert.IsFalse(FieldViewIt.Equals(FieldViewItClone));
				Assert.IsFalse(FieldIt.Equals(FieldItClone));
				Assert.IsTrue(UniformFieldViewIt.Equals(UniformFieldViewIt));
				Assert.IsTrue(UniformFieldViewIt.Equals(UniformFieldIt));
				Assert.IsTrue(UniformFieldIt.Equals(UniformFieldIt));
				Assert.IsTrue(UniformFieldIt.Equals(UniformFieldViewIt));
				Assert.IsFalse(TEXT("CbFieldIterator.Equals(SamePayload, DifferentEnd)"),
					CbFieldIterator.MakeRange(UniformPayload, CbFieldType.IntegerPositive)
						.Equals(CbFieldIterator.MakeRange(UniformPayload.LeftChop(1), CbFieldType.IntegerPositive)));
				Assert.IsFalse(TEXT("CbFieldIterator.Equals(DifferentPayload, SameEnd)"),
					CbFieldIterator.MakeRange(UniformPayload, CbFieldType.IntegerPositive)
						.Equals(CbFieldIterator.MakeRange(UniformPayload.RightChop(1), CbFieldType.IntegerPositive)));

				// CopyRangeTo
				byte[] CopyBytes = new byte[Payload.Length];
				FieldViewIt.CopyRangeTo(CopyBytes);
				Assert.IsTrue(CopyBytes.Span.SequenceEqual(Payload));
				FieldIt.CopyRangeTo(CopyBytes);
				Assert.IsTrue(CopyBytes.Span.SequenceEqual(Payload));
				UniformFieldViewIt.CopyRangeTo(CopyBytes);
				Assert.IsTrue(CopyBytes.Span.SequenceEqual(Payload));

				// MakeRangeOwned
				CbFieldIterator OwnedFromView = UniformFieldIt;
				OwnedFromView.MakeRangeOwned();
				Assert.IsTrue(OwnedFromView.TryGetRangeView(RangeView) && RangeView.Span.SequenceEqual(Payload));
				CbFieldIterator OwnedFromOwned = OwnedFromView;
				OwnedFromOwned.MakeRangeOwned();
				Assert.AreEqual(OwnedFromOwned, OwnedFromView);

				// These lines are expected to assert when uncommented.
				//FSharedBuffer ShortView = FSharedBuffer.MakeView(Payload.LeftChop(2));
				//Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(*View), ShortView)), 4);
				//Assert.AreEqual(GetCount(CbFieldIterator.MakeRangeView(CbFieldIterator.MakeRange(*View), FSharedBuffer(ShortView))), 4);
			}

			// Test CbField[View]Iterator(Scalar)
			{
				byte T = (byte)(CbFieldType.IntegerPositive);
				byte[] Payload = new byte[]{ T, 0 };

				FSharedBuffer View = FSharedBuffer.MakeView(Payload);
				FSharedBuffer Clone = FSharedBuffer.Clone(View);

				CbField FieldView(Payload);
				CbField Field = new CbField(View);

				Assert.AreEqual(GetCount(CbFieldIterator.MakeSingle(FieldView)), 1);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeSingle(CbField(FieldView))), 1);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeSingle(Field)), 1);
				Assert.AreEqual(GetCount(CbFieldIterator.MakeSingle(CbField(Field))), 1);
			}

			return true;
		}
#endif
		delegate void ParseObjectType(CbObject Object, ref uint A, ref uint B, ref uint C, ref uint D);

		[TestMethod]
		public void CbFieldParseTest()
		{
			// Test the optimal object parsing loop because it is expected to be required for high performance.
			// Under ideal conditions, when the fields are in the expected order and there are no extra fields,
			// the loop will execute once and only one comparison will be performed for each field name. Either
			// way, each field will only be visited once even if the loop needs to execute several times.
			ParseObjectType ParseObject = (CbObject Object, ref uint A, ref uint B, ref uint C, ref uint D) =>
			{
				for (CbFieldIterator It = Object.CreateIterator(); It;)
				{
					CbFieldIterator Last = It;
					if (It.Current.GetName().Equals("A"))
					{
						A = It.Current.AsUInt32();
						++It;
					}
					if (It.Current.GetName().Equals("B"))
					{
						B = It.Current.AsUInt32();
						++It;
					}
					if (It.Current.GetName().Equals("C"))
					{
						C = It.Current.AsUInt32();
						++It;
					}
					if (It.Current.GetName().Equals("D"))
					{
						D = It.Current.AsUInt32();
						++It;
					}
					if (Last == It)
					{
						++It;
					}
				}
			};

			Func<byte[], uint, uint, uint, uint, bool> TestParseObject = (byte[] Data, uint A, uint B, uint C, uint D) =>
			{
				uint ParsedA = 0, ParsedB = 0, ParsedC = 0, ParsedD = 0;
				ParseObject(new CbObject(Data, CbFieldType.Object), ref ParsedA, ref ParsedB, ref ParsedC, ref ParsedD);
				return A == ParsedA && B == ParsedB && C == ParsedC && D == ParsedD;
			};

			byte T = (byte)(CbFieldType.IntegerPositive | CbFieldType.HasFieldName);
			Assert.IsTrue(TestParseObject(new byte[]{0}, 0, 0, 0, 0));
			Assert.IsTrue(TestParseObject(new byte[] { 16, T, 1, (byte)'A', 1, T, 1, (byte)'B', 2, T, 1, (byte)'C', 3, T, 1, (byte)'D', 4}, 1, 2, 3, 4));
			Assert.IsTrue(TestParseObject(new byte[]{16, T, 1, (byte)'B', 2, T, 1, (byte)'C', 3, T, 1, (byte)'D', 4, T, 1, (byte)'A', 1}, 1, 2, 3, 4));
			Assert.IsTrue(TestParseObject(new byte[]{12, T, 1, (byte)'B', 2, T, 1, (byte)'C', 3, T, 1, (byte)'D', 4}, 0, 2, 3, 4));
			Assert.IsTrue(TestParseObject(new byte[]{8, T, 1, (byte)'B', 2, T, 1, (byte)'C', 3}, 0, 2, 3, 0));
			Assert.IsTrue(TestParseObject(new byte[]{20, T, 1, (byte)'A', 1, T, 1, (byte)'B', 2, T, 1, (byte)'C', 3, T, 1, (byte)'D', 4, T, 1, (byte)'E', 5}, 1, 2, 3, 4));
			Assert.IsTrue(TestParseObject(new byte[]{20, T, 1, (byte)'E', 5, T, 1, (byte)'A', 1, T, 1, (byte)'B', 2, T, 1, (byte)'C', 3, T, 1, (byte)'D', 4}, 1, 2, 3, 4));
			Assert.IsTrue(TestParseObject(new byte[] { 16, T, 1, (byte)'D', 4, T, 1, (byte)'C', 3, T, 1, (byte)'B', 2, T, 1, (byte)'A', 1}, 1, 2, 3, 4));
		}
	}
}
