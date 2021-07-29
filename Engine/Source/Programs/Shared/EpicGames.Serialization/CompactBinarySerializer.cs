// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;

namespace EpicGames.Serialization
{
	/// <summary>
	/// Attribute used to mark a property that should be serialized to compact binary
	/// </summary>
	public class CbFieldAttribute : Attribute
	{
		/// <summary>
		/// Name of the serialized field
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public CbFieldAttribute()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		public CbFieldAttribute(string Name)
		{
			this.Name = Name;
		}
	}

	/// <summary>
	/// Exception thrown when serializing cb objects
	/// </summary>
	public class CbException : Exception
	{
		/// <inheritdoc cref="Exception(string?)"/>
		public CbException(string Message) : base(Message)
		{
		}

		/// <inheritdoc cref="Exception(string?, Exception)"/>
		public CbException(string Message, Exception Inner) : base(Message, Inner)
		{
		}
	}

	/// <summary>
	/// Attribute-driven compact binary serializer
	/// </summary>
	public static class CbSerializer
	{
		delegate object ReadObjectDelegate(CbField Value);
		delegate void WriteObjectDelegate(CbWriter Writer, object Value);

		class CbReflectedTypeInfo<T>
		{
			public static Utf8String[]? Names = null;
			public static PropertyInfo[]? Properties = null;

			public static bool MatchName(CbField Field, int Idx)
			{
				return Field.Name == Names![Idx];
			}
		}

		static Dictionary<Type, MethodInfo> ReadPropertyMethods = new Dictionary<Type, MethodInfo>
		{
			[typeof(int)] = GetMethodInfo<CbField>(x => x.AsInt32()),
			[typeof(string)] = GetMethodInfo<CbField>(x => GetString(x))
		};

		static string GetString(CbField Field)
		{
			return Field.AsString().ToString();
		}

		static Dictionary<Type, MethodInfo> WritePropertyMethods = new Dictionary<Type, MethodInfo>
		{
			[typeof(int)] = GetMethodInfo(() => WriteInt32(default!, default, default)),
			[typeof(string)] = GetMethodInfo(() => WriteString(default!, default, default)),
		};

		static ConcurrentDictionary<Type, MethodInfo> ReadObjectContentsMethods = new ConcurrentDictionary<Type, MethodInfo>();

		static ConcurrentDictionary<Type, MethodInfo> WriteObjectPropertyMethods = new ConcurrentDictionary<Type, MethodInfo>();
		static ConcurrentDictionary<Type, MethodInfo> WriteObjectContentsMethods = new ConcurrentDictionary<Type, MethodInfo>();

		static ConcurrentDictionary<Type, ReadObjectDelegate> ReadObjectDelegates = new ConcurrentDictionary<Type, ReadObjectDelegate>();
		static ConcurrentDictionary<Type, WriteObjectDelegate> WriteObjectDelegates = new ConcurrentDictionary<Type, WriteObjectDelegate>();

		/// <summary>
		/// Serialize an object
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Object"></param>
		/// <returns></returns>
		public static CbObject Serialize<T>(T Object) where T : notnull
		{
			CbWriter Writer = new CbWriter();
			Writer.BeginObject();

			WriteObjectDelegate? WriteObject;
			if (!WriteObjectDelegates.TryGetValue(typeof(T), out WriteObject))
			{
				CreateObjectContentsWriter(typeof(T));
				WriteObject = WriteObjectDelegates[typeof(T)];
			}
			WriteObject(Writer, Object);

			Writer.EndObject();
			return Writer.ToObject();
		}

		/// <summary>
		/// Deserialize an object from a <see cref="CbObject"/>
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="Object"></param>
		/// <returns></returns>
		public static T Deserialize<T>(CbObject Object)
		{
			ReadObjectDelegate? ReadObject;
			if (!ReadObjectDelegates.TryGetValue(typeof(T), out ReadObject))
			{
				CreateObjectReader(typeof(T));
				ReadObject = ReadObjectDelegates[typeof(T)];
			}
			return (T)ReadObject(Object.AsField());
		}

		static MethodInfo CreateObjectPropertyWriter(Type Type, Dictionary<Type, MethodInfo> NewWritePropertyMethods, Dictionary<Type, MethodInfo> NewWriteContentsMethods)
		{
			MethodInfo? Method;
			if (!WriteObjectPropertyMethods.TryGetValue(Type, out Method) && !NewWritePropertyMethods.TryGetValue(Type, out Method))
			{
				// Create the new method
				DynamicMethod DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), typeof(Utf8String), Type });
				Method = DynamicMethod;
				NewWritePropertyMethods.Add(Type, Method);

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
				Generator.EmitCall(OpCodes.Call, CreateObjectContentsWriter(Type, NewWritePropertyMethods, NewWriteContentsMethods), null);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.EndObject()), null);

				Generator.MarkLabel(SkipLabel);
				Generator.Emit(OpCodes.Ret);

				// Add it into the map
				WriteObjectPropertyMethods.TryAdd(Type, Method);
			}
			return Method;
		}

		static MethodInfo CreateObjectContentsWriter(Type Type)
		{
			MethodInfo? Method;
			if (!WriteObjectContentsMethods.TryGetValue(Type, out Method))
			{
				Dictionary<Type, MethodInfo> NewWritePropertyMethods = new Dictionary<Type, MethodInfo>();
				Dictionary<Type, MethodInfo> NewWriteContentsMethods = new Dictionary<Type, MethodInfo>();
				Method = CreateObjectContentsWriter(Type, NewWritePropertyMethods, NewWriteContentsMethods);
			}
			return Method;
		}

		static MethodInfo CreateObjectContentsWriter(Type Type, Dictionary<Type, MethodInfo> NewWritePropertyMethods, Dictionary<Type, MethodInfo> NewWriteContentsMethods)
		{
			MethodInfo? Method;
			if (!WriteObjectContentsMethods.TryGetValue(Type, out Method) && !NewWriteContentsMethods.TryGetValue(Type, out Method))
			{
				// Create the method
				DynamicMethod DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), typeof(object) });
				Method = DynamicMethod;
				NewWriteContentsMethods[Type] = DynamicMethod;

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

					Generator.Emit(OpCodes.Ldarg_0);

					Generator.Emit(OpCodes.Ldsfld, NamesField);
					Generator.Emit(OpCodes.Ldc_I4, Idx);
					Generator.Emit(OpCodes.Ldelem, typeof(Utf8String));

					Generator.Emit(OpCodes.Ldarg_1);
					Generator.EmitCall(OpCodes.Call, Property.GetMethod!, null);

					MethodInfo? WriteMethod;
					if (!WritePropertyMethods.TryGetValue(PropertyType, out WriteMethod))
					{
						if (PropertyType.IsClass)
						{
							WriteMethod = CreateObjectPropertyWriter(PropertyType, NewWritePropertyMethods, NewWriteContentsMethods);
						}
						else
						{
							throw new CbException($"Unable to serialize type {PropertyType.Name}");
						}
					}
					Generator.EmitCall(OpCodes.Call, WriteMethod, null);
				}
				Generator.Emit(OpCodes.Ret);

				// Add the method to the cache
				WriteObjectContentsMethods.TryAdd(Type, DynamicMethod);
				WriteObjectDelegates.TryAdd(Type, (WriteObjectDelegate)DynamicMethod.CreateDelegate(typeof(WriteObjectDelegate)));
			}
			return Method;
		}

		static MethodInfo CreateObjectReader(Type Type)
		{
			MethodInfo? Method;
			if(!ReadObjectContentsMethods.TryGetValue(Type, out Method))
			{
				Dictionary<Type, MethodInfo> NewReadContentsMethods = new Dictionary<Type, MethodInfo>();
				Method = CreateObjectReader(Type, NewReadContentsMethods);
			}
			return Method;
		}

		static MethodInfo CreateObjectReader(Type Type, Dictionary<Type, MethodInfo> NewReadMethods)
		{
			MethodInfo? Method;
			if (!ReadObjectContentsMethods.TryGetValue(Type, out Method) && !NewReadMethods.TryGetValue(Type, out Method))
			{
				// Create the method
				DynamicMethod DynamicMethod = new DynamicMethod("_", typeof(object), new Type[] { typeof(CbField) });
				Method = DynamicMethod;
				NewReadMethods[Type] = DynamicMethod;

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

					MethodInfo? ReadMethod;
					if (!ReadPropertyMethods.TryGetValue(Property.PropertyType, out ReadMethod))
					{
						if (Property.PropertyType.IsClass)
						{
							ReadMethod = CreateObjectReader(Property.PropertyType, NewReadMethods);
						}
						else
						{
							throw new CbException($"Unable to serialize type {Property.PropertyType.Name}");
						}
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

				// Add the new methods
				ReadObjectContentsMethods.TryAdd(Type, Method);
				ReadObjectDelegates.TryAdd(Type, (ReadObjectDelegate)DynamicMethod.CreateDelegate(typeof(ReadObjectDelegate)));
			}
			return Method;
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
			foreach (PropertyInfo Property in Type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
			{
				CbFieldAttribute? Attribute = Property.GetCustomAttribute<CbFieldAttribute>();
				if (Attribute != null)
				{
					Utf8String Name = Attribute.Name ?? Property.Name;
					PropertyList.Add((Name, Property));
				}
			}
			return PropertyList.ToArray();
		}

		static void WriteInt32(CbWriter Writer, Utf8String Name, int Value)
		{
			if (Value != 0)
			{
				Writer.WriteInteger(Name, Value);
			}
		}

		static void WriteString(CbWriter Writer, Utf8String Name, string? Value)
		{
			if (!String.IsNullOrEmpty(Value))
			{
				Writer.WriteString(Name, Value);
			}
		}
	}
}
