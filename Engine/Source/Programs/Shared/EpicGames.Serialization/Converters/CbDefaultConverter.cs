// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	/// <summary>
	/// Default converter implementation. Calls delegates to perform read/write operations.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	class CbDefaultConverter<T> : CbConverter<T>
	{
		public delegate T ReadDelegate(CbField Field);
		public delegate void WriteDelegate(CbWriter Writer, T Value);
		public delegate void WriteNamedDelegate(CbWriter Writer, Utf8String Name, T Value);

		readonly ReadDelegate ReadFunc;
		readonly WriteDelegate WriteFunc;
		readonly WriteNamedDelegate WriteNamedFunc;

		public CbDefaultConverter(ReadDelegate ReadFunc, WriteDelegate WriteFunc, WriteNamedDelegate WriteNamedFunc)
		{
			this.ReadFunc = ReadFunc;
			this.WriteFunc = WriteFunc;
			this.WriteNamedFunc = WriteNamedFunc;
		}

		public CbDefaultConverter(Expression<Func<CbField, T>> Read, Expression<Action<CbWriter, T>> Write, Expression<Action<CbWriter, Utf8String, T>> WriteNamed)
		{
			Func<CbField, T> ReadFunc = Read.Compile();
			this.ReadFunc = f => ReadFunc(f);

			Action<CbWriter, T> WriteFunc = Write.Compile();
			this.WriteFunc = (w, t) => WriteFunc(w, t);

			Action<CbWriter, Utf8String, T> WriteNamedFunc = WriteNamed.Compile();
			this.WriteNamedFunc = (w, n, t) => WriteNamedFunc(w, n, t);
		}

		public CbDefaultConverter(DynamicMethod ReadMethod, DynamicMethod WriteMethod, DynamicMethod WriteNamedMethod)
		{
			this.ReadFunc = (ReadDelegate)ReadMethod.CreateDelegate(typeof(ReadDelegate));
			this.WriteFunc = (WriteDelegate)WriteMethod.CreateDelegate(typeof(WriteDelegate));
			this.WriteNamedFunc = (WriteNamedDelegate)WriteNamedMethod.CreateDelegate(typeof(WriteNamedDelegate));
		}

		public override T Read(CbField Field) => ReadFunc(Field);
		public override void Write(CbWriter Writer, T Value) => WriteFunc(Writer, Value);
		public override void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => WriteNamedFunc(Writer, Name, Value);
	}

	/// <summary>
	/// Converter which generates dynamic serialization methods for classes based on attribute markup
	/// </summary>
	class CbDefaultConverterFactory : CbConverterFactory
	{
		static object LockObject = new object();

		delegate void SkipDefaultDelegate(ILGenerator Generator, Label SkipLabel);

		class CbConverterInfo
		{
			public CbConverter Converter { get; }
			public MethodInfo ReadMethod { get; }
			public MethodInfo WriteMethod { get; }
			public MethodInfo WriteNamedMethod { get; }
			public SkipDefaultDelegate? SkipDefault { get; }

			public CbConverterInfo(CbConverter Converter, MethodInfo ReadMethod, MethodInfo WriteMethod, MethodInfo WriteNamedMethod, SkipDefaultDelegate? SkipDefault)
			{
				this.Converter = Converter;
				this.ReadMethod = ReadMethod;
				this.WriteMethod = WriteMethod;
				this.WriteNamedMethod = WriteNamedMethod;
				this.SkipDefault = SkipDefault;
			}
		}

		// Implementation of CbConverterInfo for types that already have a registered converter
		class CbCustomConverterInfo<T> : CbConverterInfo
		{
			static CbConverter<T> StaticConverter = CbSerializer.GetConverter<T>();

			static T Read(CbField Field) => StaticConverter.Read(Field);
			static void Write(CbWriter Writer, T Value) => StaticConverter.Write(Writer, Value);
			static void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => StaticConverter.WriteNamed(Writer, Name, Value);

			public CbCustomConverterInfo()
				: base(StaticConverter, GetMethodInfo<CbField>(x => Read(x)), GetMethodInfo(() => Write(default!, default!)), GetMethodInfo(() => WriteNamed(default!, default!, default!)), null)
			{
			}
		}

		static void WriteInt32Value(CbWriter Writer, int Value) => Writer.WriteIntegerValue(Value);
		static void WriteInt32(CbWriter Writer, Utf8String Name, int Value) => Writer.WriteInteger(Name, Value);
		static void WriteInt64Value(CbWriter Writer, long Value) => Writer.WriteIntegerValue(Value);
		static void WriteInt64(CbWriter Writer, Utf8String Name, long Value) => Writer.WriteInteger(Name, Value);

		static string ReadString(CbField Field) => Field.AsString().ToString();
		static void WriteString(CbWriter Writer, Utf8String Name, string Value) => Writer.WriteString(Name, Value);
		static void WriteStringValue(CbWriter Writer, string Value) => Writer.WriteStringValue(Value);

		static ReadOnlyMemory<byte> ReadMemory(CbField Field) => Field.AsBinary();
		static void WriteMemory(CbWriter Writer, Utf8String Name, ReadOnlyMemory<byte> Value) => Writer.WriteBinary(Name, Value.Span);
		static void WriteMemoryValue(CbWriter Writer, ReadOnlyMemory<byte> Value) => Writer.WriteBinaryValue(Value.Span);

		static byte[] ReadByteArray(CbField Field) => Field.AsBinary().ToArray();
		static void WriteByteArray(CbWriter Writer, Utf8String Name, byte[] Value) => Writer.WriteBinary(Name, Value);
		static void WriteByteArrayValue(CbWriter Writer, byte[] Value) => Writer.WriteBinaryValue(Value);

		static Dictionary<Type, CbConverterInfo> TypeToConverterInfo = new Dictionary<Type, CbConverterInfo>(new KeyValuePair<Type, CbConverterInfo>[]
		{
			GetPodConverterInfo(x => x.AsBool(), (w, v) => w.WriteBoolValue(v), (w, n, v) => w.WriteBool(n, v), SkipIfNullOrZero),
			GetPodConverterInfo(x => x.AsInt32(), (w, v) => WriteInt32Value(w, v), (w, n, v) => WriteInt32(w, n, v), SkipIfNullOrZero),
			GetPodConverterInfo(x => x.AsInt64(), (w, v) => WriteInt64Value(w, v), (w, n, v) => WriteInt64(w, n, v), SkipIfNullOrZero),
			GetPodConverterInfo(x => x.AsString(), (w, v) => w.WriteStringValue(v), (w, n, v) => w.WriteString(n, v), null),
			GetPodConverterInfo(x => x.AsHash(), (w, v) => w.WriteHashValue(v), (w, n, v) => w.WriteHash(n, v), null),
			GetPodConverterInfo(x => x.AsObjectAttachment(), (w, v) => w.WriteObjectAttachmentValue(v.Hash), (w, n, v) => w.WriteObjectAttachment(n, v.Hash), null),
			GetPodConverterInfo(x => x.AsBinaryAttachment(), (w, v) => w.WriteBinaryAttachmentValue(v.Hash), (w, n, v) => w.WriteBinaryAttachment(n, v.Hash), null),
			GetPodConverterInfo(x => x.AsDateTime(), (w, v) => w.WriteDateTimeValue(v), (w, n, v) => w.WriteDateTime(n, v), null),
			GetPodConverterInfo(x => ReadString(x), (w, v) => WriteStringValue(w, v), (w, n, v) => WriteString(w, n, v), SkipIfNullOrZero),
			GetPodConverterInfo(x => ReadMemory(x), (w, v) => WriteMemoryValue(w, v), (w, n, v) => WriteMemory(w, n, v), null),
			GetPodConverterInfo(x => ReadByteArray(x), (w, v) => WriteByteArrayValue(w, v), (w, n, v) => WriteByteArray(w, n, v), null),
		});

		static KeyValuePair<Type, CbConverterInfo> GetPodConverterInfo<T>(Expression<Func<CbField, T>> Read, Expression<Action<CbWriter, T>> Write, Expression<Action<CbWriter, Utf8String, T>> WriteNamed, SkipDefaultDelegate? SkipDefault)
		{
			CbDefaultConverter<T> Converter = new CbDefaultConverter<T>(Read, Write, WriteNamed);

			MethodInfo ReadMethod = ((MethodCallExpression)Read.Body).Method;
			MethodInfo WriteMethod = ((MethodCallExpression)Write.Body).Method;
			MethodInfo WriteNamedMethod = ((MethodCallExpression)WriteNamed.Body).Method;

			return new KeyValuePair<Type, CbConverterInfo>(typeof(T), new CbConverterInfo(Converter, ReadMethod, WriteMethod, WriteNamedMethod, SkipDefault));
		}

		static CbConverterInfo GetConverterInfo(Type Type)
		{
			lock (LockObject)
			{
				CbConverterInfo? ConverterInfo;
				if (!TypeToConverterInfo.TryGetValue(Type, out ConverterInfo))
				{
					CbConverter Converter = CbSerializer.GetConverter(Type);
					if (!TypeToConverterInfo.TryGetValue(Type, out ConverterInfo)) // CreateConverter above may have added it
					{
						Type GenericType = typeof(CbCustomConverterInfo<>).MakeGenericType(Type);
						ConverterInfo = (CbConverterInfo)Activator.CreateInstance(GenericType)!;
						TypeToConverterInfo.Add(Type, ConverterInfo);
					}
				}
				return ConverterInfo;
			}
		}

		public override CbConverter CreateConverter(Type Type)
		{
			lock (LockObject)
			{
				CbConverterInfo? ConverterInfo;
				if (!TypeToConverterInfo.TryGetValue(Type, out ConverterInfo))
				{
					if (Type.IsClass)
					{
						DynamicMethod ReadMethod = CreateObjectReader(Type);
						DynamicMethod WriteMethod = CreateObjectWriter(Type);
						DynamicMethod WriteNamedMethod = CreateNamedObjectWriter(Type);

						Type GenericType = typeof(CbDefaultConverter<>).MakeGenericType(Type);
						CbConverter Converter = (CbConverter)Activator.CreateInstance(GenericType, ReadMethod, WriteMethod, WriteNamedMethod)!;

						ConverterInfo = new CbConverterInfo(Converter, ReadMethod, WriteMethod, WriteNamedMethod, SkipIfNullOrZero);
					}
					else if (Type.IsEnum)
					{
						DynamicMethod ReadMethod = CreateEnumReader(Type);
						DynamicMethod WriteMethod = CreateEnumWriter(Type);
						DynamicMethod WriteNamedMethod = CreateNamedEnumWriter(Type);

						Type GenericType = typeof(CbDefaultConverter<>).MakeGenericType(Type);
						CbConverter Converter = (CbConverter)Activator.CreateInstance(GenericType, ReadMethod, WriteMethod, WriteNamedMethod)!;

						ConverterInfo = new CbConverterInfo(Converter, ReadMethod, WriteMethod, WriteNamedMethod, SkipIfNullOrZero);
					}
					else
					{
						throw new InvalidOperationException($"Unable to serialize type {Type}");
					}
					TypeToConverterInfo.Add(Type, ConverterInfo);
				}
				return ConverterInfo.Converter;
			}
		}

		static void SkipIfNullOrZero(ILGenerator Generator, Label SkipLabel)
		{
			Generator.Emit(OpCodes.Dup);
			Generator.Emit(OpCodes.Brfalse, SkipLabel);
		}

		class CbReflectedTypeInfo<T> where T : class
		{
			public static Utf8String[]? Names = null;
			public static PropertyInfo[]? Properties = null;

			public static bool MatchName(CbField Field, int Idx)
			{
				return Field.Name == Names![Idx];
			}
		}

		static DynamicMethod CreateEnumWriter(Type Type)
		{
			// Create the new method
			DynamicMethod DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), Type });

			// Implement the body
			ILGenerator Generator = DynamicMethod.GetILGenerator();

			Generator.Emit(OpCodes.Ldarg_1);

			Label SkipLabel = Generator.DefineLabel();
			Generator.Emit(OpCodes.Brfalse, SkipLabel);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_1);
			Generator.Emit(OpCodes.Conv_I8);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.WriteIntegerValue(0L)), null);

			Generator.MarkLabel(SkipLabel);
			Generator.Emit(OpCodes.Ret);

			return DynamicMethod;
		}

		static DynamicMethod CreateNamedEnumWriter(Type Type)
		{
			// Create the new method
			DynamicMethod DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), typeof(Utf8String), Type });

			// Implement the body
			ILGenerator Generator = DynamicMethod.GetILGenerator();
			Generator.Emit(OpCodes.Ldarg_2);

			Label SkipLabel = Generator.DefineLabel();
			Generator.Emit(OpCodes.Brfalse, SkipLabel);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_1);
			Generator.Emit(OpCodes.Ldarg_2);
			Generator.Emit(OpCodes.Conv_I8);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.WriteInteger(default, 0L)), null);

			Generator.MarkLabel(SkipLabel);
			Generator.Emit(OpCodes.Ret);

			return DynamicMethod;
		}

		static DynamicMethod CreateEnumReader(Type Type)
		{
			// Create the method
			DynamicMethod DynamicMethod = new DynamicMethod("_", Type, new Type[] { typeof(CbField) });

			// Generate the IL for it
			ILGenerator Generator = DynamicMethod.GetILGenerator();

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbField>(x => x.AsInt32()), null);
			Generator.Emit(OpCodes.Ret);

			return DynamicMethod;
		}

		static Dictionary<Type, DynamicMethod> WriteMethods = new Dictionary<Type, DynamicMethod>();

		static DynamicMethod CreateObjectWriter(Type Type)
		{
			DynamicMethod? DynamicMethod;
			if (!WriteMethods.TryGetValue(Type, out DynamicMethod))
			{
				// Create the new method
				DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), Type });
				WriteMethods.Add(Type, DynamicMethod);

				// Implement the body
				ILGenerator Generator = DynamicMethod.GetILGenerator();
				Generator.Emit(OpCodes.Ldarg_1);

				Label SkipLabel = Generator.DefineLabel();
				Generator.Emit(OpCodes.Brfalse, SkipLabel);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.BeginObject()), null);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.Emit(OpCodes.Ldarg_1);
				Generator.EmitCall(OpCodes.Call, CreateObjectContentsWriter(Type), null);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.EndObject()), null);

				Generator.MarkLabel(SkipLabel);
				Generator.Emit(OpCodes.Ret);
			}
			return DynamicMethod;
		}

		static Dictionary<Type, DynamicMethod> WriteNamedMethods = new Dictionary<Type, DynamicMethod>();

		static DynamicMethod CreateNamedObjectWriter(Type Type)
		{
			DynamicMethod? DynamicMethod;
			if (!WriteNamedMethods.TryGetValue(Type, out DynamicMethod))
			{
				// Create the new method
				DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), typeof(Utf8String), Type });
				WriteNamedMethods.Add(Type, DynamicMethod);

				// Implement the body
				ILGenerator Generator = DynamicMethod.GetILGenerator();
				Generator.Emit(OpCodes.Ldarg_2);

				Label SkipLabel = Generator.DefineLabel();
				Generator.Emit(OpCodes.Brfalse, SkipLabel);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.Emit(OpCodes.Ldarg_1);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.BeginObject(null!)), null);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.Emit(OpCodes.Ldarg_2);
				Generator.EmitCall(OpCodes.Call, CreateObjectContentsWriter(Type), null);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.EndObject()), null);

				Generator.MarkLabel(SkipLabel);
				Generator.Emit(OpCodes.Ret);
			}
			return DynamicMethod;
		}

		static Dictionary<Type, DynamicMethod> WriteObjectProperties = new Dictionary<Type, DynamicMethod>();

		static DynamicMethod CreateObjectContentsWriter(Type Type)
		{
			DynamicMethod? DynamicMethod;
			if (!WriteObjectProperties.TryGetValue(Type, out DynamicMethod))
			{
				// Create the method
				DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), Type });
				WriteObjectProperties.Add(Type, DynamicMethod);

				// Find the reflected properties from this type
				(Utf8String Name, PropertyInfo Property)[] Properties = GetProperties(Type);

				// Create a static type with the required reflection data
				Type ReflectedType = typeof(CbReflectedTypeInfo<>).MakeGenericType(Type);
				FieldInfo NamesField = ReflectedType.GetField(nameof(CbReflectedTypeInfo<object>.Names))!;
				NamesField.SetValue(null, Properties.Select(x => x.Name).ToArray());

				// Generate code for the method
				ILGenerator Generator = DynamicMethod.GetILGenerator();
				for (int Idx = 0; Idx < Properties.Length; Idx++)
				{
					PropertyInfo Property = Properties[Idx].Property;
					Type PropertyType = Property.PropertyType;

					// Get the field value
					Generator.Emit(OpCodes.Ldarg_1);
					Generator.EmitCall(OpCodes.Call, Property.GetMethod!, null);

					Label SkipLabel = Generator.DefineLabel();

					MethodInfo WriteMethod;
					if (WriteNamedMethods.TryGetValue(PropertyType, out DynamicMethod? DynamicWriteMethod))
					{
						Generator.Emit(OpCodes.Dup);
						Generator.Emit(OpCodes.Brfalse, SkipLabel);
						WriteMethod = DynamicWriteMethod;
					}
					else
					{
						CbConverterInfo ConverterInfo = GetConverterInfo(PropertyType);
						ConverterInfo.SkipDefault?.Invoke(Generator, SkipLabel);
						WriteMethod = ConverterInfo.WriteNamedMethod;
					}

					// Store the variable in a local
					LocalBuilder Local = Generator.DeclareLocal(PropertyType);
					Generator.Emit(OpCodes.Dup);
					Generator.Emit(OpCodes.Stloc, Local);

					// Call the writer
					Generator.Emit(OpCodes.Ldarg_0);

					Generator.Emit(OpCodes.Ldsfld, NamesField);
					Generator.Emit(OpCodes.Ldc_I4, Idx);
					Generator.Emit(OpCodes.Ldelem, typeof(Utf8String));

					Generator.Emit(OpCodes.Ldloc, Local);
					Generator.EmitCall(OpCodes.Call, WriteMethod, null);

					// Remove the duplicated value from the top of the stack
					Generator.MarkLabel(SkipLabel);
					Generator.Emit(OpCodes.Pop);
				}
				Generator.Emit(OpCodes.Ret);
			}
			return DynamicMethod;
		}

		static Dictionary<Type, DynamicMethod> ReadObjectMethods = new Dictionary<Type, DynamicMethod>();

		static DynamicMethod CreateObjectReader(Type Type)
		{
			DynamicMethod? DynamicMethod;
			if (!ReadObjectMethods.TryGetValue(Type, out DynamicMethod))
			{
				// Create the method
				DynamicMethod = new DynamicMethod("_", Type, new Type[] { typeof(CbField) });
				ReadObjectMethods.Add(Type, DynamicMethod);

				// Generate the IL for it
				ILGenerator Generator = DynamicMethod.GetILGenerator();

				// Construct the object
				ConstructorInfo? Constructor = Type.GetConstructor(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null, Type.EmptyTypes, null);
				if (Constructor == null)
				{
					throw new CbException($"Unable to find default constructor for {Type}");
				}

				// Find the reflected properties from this type
				(Utf8String Name, PropertyInfo Property)[] Properties = GetProperties(Type);

				// Create a static type with the required reflection data
				Type ReflectedType = typeof(CbReflectedTypeInfo<>).MakeGenericType(Type);
				FieldInfo NamesField = ReflectedType.GetField(nameof(CbReflectedTypeInfo<object>.Names))!;
				NamesField.SetValue(null, Properties.Select(x => x.Name).ToArray());
				MethodInfo MatchNameMethod = ReflectedType.GetMethod(nameof(CbReflectedTypeInfo<object>.MatchName))!;

				// NewObjectLocal = new Type()
				LocalBuilder NewObjectLocal = Generator.DeclareLocal(typeof(object));
				Generator.Emit(OpCodes.Newobj, Constructor);
				Generator.Emit(OpCodes.Stloc, NewObjectLocal);

				// Stack(0) = CbField.CreateIterator()
				Generator.Emit(OpCodes.Ldarg_0);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbField>(x => x.CreateIterator()), null);

				// CbFieldIterator IteratorLocal = Stack(0)
				LocalBuilder IteratorLocal = Generator.DeclareLocal(typeof(CbFieldIterator));
				Generator.Emit(OpCodes.Dup);
				Generator.Emit(OpCodes.Stloc, IteratorLocal);

				// if(!Stack.Pop().IsValid()) goto ReturnLabel
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbFieldIterator>(x => x.IsValid()), null);
				Label ReturnLabel = Generator.DefineLabel();
				Generator.Emit(OpCodes.Brfalse, ReturnLabel);

				// NamesLocal = CbReflectedTypeInfo<Type>.Names
				LocalBuilder NamesLocal = Generator.DeclareLocal(typeof(Utf8String[]));
				Generator.Emit(OpCodes.Ldsfld, NamesField);
				Generator.Emit(OpCodes.Stloc, NamesLocal);

				// IterationLoopLabel:
				Label IterationLoopLabel = Generator.DefineLabel();
				Generator.MarkLabel(IterationLoopLabel);

				// bool MatchLocal = false
				LocalBuilder MatchLocal = Generator.DeclareLocal(typeof(bool));
				Generator.Emit(OpCodes.Ldc_I4_0);
				Generator.Emit(OpCodes.Stloc, MatchLocal);

				// Stack(0) = IteratorLocal.GetCurrent()
				Generator.Emit(OpCodes.Ldloc, IteratorLocal);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbFieldIterator>(x => x.GetCurrent()), null);

				// Try to parse each of the properties in order. If fields are ordered correctly, we will parse the object in a single pass. Otherwise we can loop and start again.
				LocalBuilder FieldLocal = Generator.DeclareLocal(typeof(CbField));
				for (int Idx = 0; Idx < Properties.Length; Idx++)
				{
					PropertyInfo Property = Properties[Idx].Property;

					// Get the read method for this property type
					MethodInfo ReadMethod;
					if (ReadObjectMethods.TryGetValue(Property.PropertyType, out DynamicMethod? DynamicReadMethod))
					{
						ReadMethod = DynamicReadMethod;
					}
					else
					{
						ReadMethod = GetConverterInfo(Property.PropertyType).ReadMethod;
					}

					// if(!CbReflectedTypeInfo<Type>.MatchName(Stack(0), Idx)) goto SkipPropertyLabel
					Label SkipPropertyLabel = Generator.DefineLabel();
					Generator.Emit(OpCodes.Dup); // Current CbField
					Generator.Emit(OpCodes.Ldc_I4, Idx);
					Generator.Emit(OpCodes.Call, MatchNameMethod);
					Generator.Emit(OpCodes.Brfalse, SkipPropertyLabel);

					// FieldLocal = Stack.Pop()
					Generator.Emit(OpCodes.Stloc, FieldLocal);

					// Property.SetMethod(NewObjectLocal, ReadMethod(FieldLocal))
					Generator.Emit(OpCodes.Ldloc, NewObjectLocal);
					Generator.Emit(OpCodes.Ldloc, FieldLocal);
					Generator.EmitCall(OpCodes.Call, ReadMethod, null);
					Generator.EmitCall(OpCodes.Call, Property.SetMethod!, null);

					// if(!IteratorLocal.MoveNext()) goto ReturnLabel
					Generator.Emit(OpCodes.Ldloc, IteratorLocal);
					Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbFieldIterator>(x => x.MoveNext()), null);
					Generator.Emit(OpCodes.Brfalse, ReturnLabel);

					// MatchLocal = true
					Generator.Emit(OpCodes.Ldc_I4_1);
					Generator.Emit(OpCodes.Stloc, MatchLocal);

					// Stack(0) = IteratorLocal.GetCurrent()
					Generator.Emit(OpCodes.Ldloc, IteratorLocal);
					Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbFieldIterator>(x => x.GetCurrent()), null);

					// SkipPropertyLabel:
					Generator.MarkLabel(SkipPropertyLabel);
				}

				// Stack.Pop()
				Generator.Emit(OpCodes.Pop); // Current CbField

				// if(MatchLocal) goto IterationLoopLabel
				Generator.Emit(OpCodes.Ldloc, MatchLocal);
				Generator.Emit(OpCodes.Brtrue, IterationLoopLabel);

				// if(IteratorLocal.MoveNext()) goto IterationLoopLabel
				Generator.Emit(OpCodes.Ldloc, IteratorLocal);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbFieldIterator>(x => x.MoveNext()), null);
				Generator.Emit(OpCodes.Brtrue, IterationLoopLabel);

				// return NewObjectLocal
				Generator.MarkLabel(ReturnLabel);
				Generator.Emit(OpCodes.Ldloc, NewObjectLocal);
				Generator.Emit(OpCodes.Ret);
			}
			return DynamicMethod;
		}

		static MethodInfo GetMethodInfo(Expression<Action> Expr)
		{
			return ((MethodCallExpression)Expr.Body).Method;
		}

		static MethodInfo GetMethodInfo<T>(Expression<Action<T>> Expr)
		{
			return ((MethodCallExpression)Expr.Body).Method;
		}

		static (Utf8String, PropertyInfo)[] GetProperties(Type Type)
		{
			List<(Utf8String, PropertyInfo)> PropertyList = new List<(Utf8String, PropertyInfo)>();
			foreach (PropertyInfo Property in Type.GetProperties(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance))
			{
				CbFieldAttribute? Attribute = Property.GetCustomAttribute<CbFieldAttribute>();
				if (Attribute != null)
				{
					Utf8String Name = Attribute.Name ?? Property.Name;
					PropertyList.Add((Name, Property));
				}
			}
			if (PropertyList.Count == 0)
			{
				throw new CbException($"{Type.Name} does not have any properties marked with [CbField]");
			}
			return PropertyList.ToArray();
		}
	}
}
