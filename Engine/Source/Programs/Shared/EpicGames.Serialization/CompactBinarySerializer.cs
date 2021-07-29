// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;
using System.Text;

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
		delegate void WriteObjectDelegate(CbWriter Writer, object Value);

		class CbReflectedTypeInfo<T>
		{
			public static Utf8String[]? Names = null;
		}

		static Dictionary<Type, MethodInfo> WritePropertyMethods = new Dictionary<Type, MethodInfo>
		{
			[typeof(int)] = GetMethodInfo(() => WriteInt32(default!, default, default)),
			[typeof(string)] = GetMethodInfo(() => WriteString(default!, default, default)),
		};

		static ConcurrentDictionary<Type, MethodInfo> WriteObjectPropertyMethods = new ConcurrentDictionary<Type, MethodInfo>();
		static ConcurrentDictionary<Type, MethodInfo> WriteObjectContentsMethods = new ConcurrentDictionary<Type, MethodInfo>();
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
				CreateClassContentsWriter(typeof(T));
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
			return (T)DeserializeObject(typeof(T), Object);
		}

		/// <summary>
		/// Deserialize an individual class from a CbObject
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Object"></param>
		/// <returns></returns>
		static object DeserializeObject(Type Type, CbObject Object)
		{
			(Utf8String, PropertyInfo)[] Properties = GetProperties(Type);
			Dictionary<Utf8String, PropertyInfo> NameToPropertyInfo = Properties.ToDictionary(x => x.Item1, x => x.Item2);

			object NewObject = Activator.CreateInstance(Type)!;
			foreach (CbField Field in Object)
			{
				PropertyInfo? PropertyInfo;
				if (NameToPropertyInfo.TryGetValue(Field.Name, out PropertyInfo))
				{
					Type PropertyType = PropertyInfo.PropertyType;
					if (PropertyType == typeof(int))
					{
						PropertyInfo.SetValue(NewObject, Field.AsInt32());
					}
					else if (PropertyType == typeof(string))
					{
						PropertyInfo.SetValue(NewObject, Field.AsString());
					}
					else if (PropertyType == typeof(IoHash))
					{
						PropertyInfo.SetValue(NewObject, Field.AsHash());
					}
					else if (PropertyType.IsClass)
					{
						PropertyInfo.SetValue(NewObject, DeserializeObject(PropertyType, Field.AsObject()));
					}
					else
					{
						throw new NotImplementedException();
					}
				}
			}
			return NewObject;
		}

		static MethodInfo CreateClassPropertyWriter(Type Type, Dictionary<Type, MethodInfo> NewWritePropertyMethods, Dictionary<Type, MethodInfo> NewWriteContentsMethods)
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
				Generator.EmitCall(OpCodes.Call, CreateClassContentsWriter(Type, NewWritePropertyMethods, NewWriteContentsMethods), null);

				Generator.Emit(OpCodes.Ldarg_0);
				Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.EndObject()), null);

				Generator.MarkLabel(SkipLabel);
				Generator.Emit(OpCodes.Ret);

				// Add it into the map
				WriteObjectPropertyMethods.TryAdd(Type, Method);
			}
			return Method;
		}

		static MethodInfo CreateClassContentsWriter(Type Type)
		{
			MethodInfo? Method;
			if (!WriteObjectContentsMethods.TryGetValue(Type, out Method))
			{
				Dictionary<Type, MethodInfo> NewWritePropertyMethods = new Dictionary<Type, MethodInfo>();
				Dictionary<Type, MethodInfo> NewWriteContentsMethods = new Dictionary<Type, MethodInfo>();
				Method = CreateClassContentsWriter(Type, NewWritePropertyMethods, NewWriteContentsMethods);
			}
			return Method;
		}

		static MethodInfo CreateClassContentsWriter(Type Type, Dictionary<Type, MethodInfo> NewWritePropertyMethods, Dictionary<Type, MethodInfo> NewWriteContentsMethods)
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
					if (PropertyType.IsClass)
					{
						WriteMethod = CreateClassPropertyWriter(PropertyType, NewWritePropertyMethods, NewWriteContentsMethods);
					}
					else if(!WritePropertyMethods.TryGetValue(PropertyType, out WriteMethod))
					{
						throw new CbException($"Unable to serialize type {PropertyType.Name}");
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
