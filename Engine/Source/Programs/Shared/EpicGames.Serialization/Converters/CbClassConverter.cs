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
	class CbClassConverterMethods
	{
		class CbReflectedTypeInfo<T> where T : class
		{
			public static Utf8String[]? Names = null;
			public static PropertyInfo[]? Properties = null;

			public static bool MatchName(CbField Field, int Idx)
			{
				return Field.Name == Names![Idx];
			}
		}

		static Utf8String DiscriminatorKey = "_t";

		public Type ClassType { get; }
		public bool IsPolymorphic { get; }

		public DynamicMethod ReadMethod { get; }
		public DynamicMethod WriteMethod { get; }
		public DynamicMethod WriteNamedMethod { get; }

		public DynamicMethod WriteContentsMethod { get; }

		public DynamicMethod ReadConcreteMethod { get; }
		public DynamicMethod WriteConcreteContentsMethod { get; }

		static Dictionary<Type, CbClassConverterMethods> TypeToMethods = new Dictionary<Type, CbClassConverterMethods>();

		public static CbClassConverterMethods Create(Type ClassType)
		{
			CbClassConverterMethods? Methods;
			if (!TypeToMethods.TryGetValue(ClassType, out Methods))
			{
				Methods = new CbClassConverterMethods(ClassType);
				TypeToMethods.Add(ClassType, Methods);
				Methods.GenerateBytecode();
			}
			return Methods;
		}

		private CbClassConverterMethods(Type ClassType)
		{
			this.ClassType = ClassType;
			this.IsPolymorphic = (ClassType.GetCustomAttribute<CbPolymorphicAttribute>(true) != null);

			ReadConcreteMethod = new DynamicMethod($"ReadConcrete_{ClassType.Name}", ClassType, new Type[] { typeof(CbField) });
			WriteConcreteContentsMethod = new DynamicMethod($"WriteConcreteContents_{ClassType.Name}", null, new Type[] { typeof(CbWriter), ClassType });

			WriteMethod = new DynamicMethod($"Write_{ClassType.Name}", null, new Type[] { typeof(CbWriter), ClassType });
			WriteNamedMethod = new DynamicMethod($"WriteNamed_{ClassType.Name}", null, new Type[] { typeof(CbWriter), typeof(Utf8String), ClassType });

			if (IsPolymorphic)
			{
				ReadMethod = new DynamicMethod($"Read_{ClassType.Name}", ClassType, new Type[] { typeof(CbField) });
				WriteContentsMethod = new DynamicMethod($"WriteContents_{ClassType.Name}", null, new Type[] { typeof(CbWriter), ClassType });
			}
			else
			{
				ReadMethod = ReadConcreteMethod;
				WriteContentsMethod = WriteConcreteContentsMethod;
			}
		}

		private void GenerateBytecode()
		{
			// Create the regular methods
			CreateConcreteObjectReader(ClassType, ReadConcreteMethod.GetILGenerator());
			CreateConcreteObjectContentsWriter(ClassType, WriteConcreteContentsMethod.GetILGenerator());

			CreateObjectWriter(ClassType, WriteMethod.GetILGenerator(), WriteContentsMethod);
			CreateNamedObjectWriter(ClassType, WriteNamedMethod.GetILGenerator(), WriteContentsMethod);

			// Create the extra polymorphic methods
			if (IsPolymorphic)
			{
				// Create the dispatch type
				Type DispatchType = typeof(CbPolymorphicDispatch<>).MakeGenericType(ClassType);

				// Create the read dispatch method
				{
					ILGenerator Generator = ReadMethod.GetILGenerator();
					Generator.Emit(OpCodes.Ldarg_0);
					Generator.Emit(OpCodes.Call, DispatchType.GetMethod(nameof(CbPolymorphicDispatch<object>.Read))!);
					Generator.Emit(OpCodes.Ret);
				}

				// Create the write dispatch method
				{
					ILGenerator Generator = WriteContentsMethod.GetILGenerator();
					Generator.Emit(OpCodes.Ldarg_0);
					Generator.Emit(OpCodes.Ldarg_1);
					Generator.Emit(OpCodes.Call, DispatchType.GetMethod(nameof(CbPolymorphicDispatch<object>.WriteContents))!);
					Generator.Emit(OpCodes.Ret);
				}

				// Finally, update the dispatch type with all the methods. We should be safe on the recursive path now.
				PopulateDispatchType(ClassType, DispatchType);
			}
		}

		static void PopulateDispatchType(Type ClassType, Type DispatchType)
		{
			Dictionary<Utf8String, Type> DiscriminatorToKnownType = new Dictionary<Utf8String, Type>();

			Type[] KnownTypes = ClassType.Assembly.GetTypes();
			foreach (Type KnownType in KnownTypes)
			{
				if (KnownType.IsClass && !KnownType.IsAbstract)
				{
					for (Type? BaseType = KnownType; BaseType != null; BaseType = BaseType.BaseType)
					{
						if (BaseType == ClassType)
						{
							CbDiscriminatorAttribute? Discriminator = KnownType.GetCustomAttribute<CbDiscriminatorAttribute>();
							if (Discriminator == null)
							{
								throw new NotSupportedException();
							}
							DiscriminatorToKnownType[Discriminator.Name] = KnownType;
						}
					}
				}
			}

			// Populate the dictionary
			Dictionary<Utf8String, Func<CbField, object>> NameToReadFunc = (Dictionary<Utf8String, Func<CbField, object>>)DispatchType.GetField(nameof(CbPolymorphicDispatch<object>.NameToReadFunc))!.GetValue(null)!;
			Dictionary<Type, Action<CbWriter, object>> TypeToWriteContentsFunc = (Dictionary<Type, Action<CbWriter, object>>)DispatchType.GetField(nameof(CbPolymorphicDispatch<object>.TypeToWriteContentsFunc))!.GetValue(null)!;

			foreach ((Utf8String Name, Type KnownType) in DiscriminatorToKnownType)
			{
				CbClassConverterMethods Methods = Create(KnownType);

				{
					DynamicMethod DynamicMethod = new DynamicMethod("_", typeof(object), new Type[] { typeof(CbField) });
					ILGenerator Generator = DynamicMethod.GetILGenerator();
					Generator.Emit(OpCodes.Ldarg_0);
					Generator.Emit(OpCodes.Call, Methods.ReadConcreteMethod);
					Generator.Emit(OpCodes.Castclass, typeof(object));
					Generator.Emit(OpCodes.Ret);
					NameToReadFunc[Name] = CreateDelegate<Func<CbField, object>>(DynamicMethod);
				}

				{
					DynamicMethod DynamicMethod = new DynamicMethod("_", null, new Type[] { typeof(CbWriter), typeof(object) });
					ILGenerator Generator = DynamicMethod.GetILGenerator();
					Generator.Emit(OpCodes.Ldarg_0);
					Generator.Emit(OpCodes.Ldarg_1);
					Generator.Emit(OpCodes.Castclass, KnownType);
					Generator.Emit(OpCodes.Call, Methods.WriteConcreteContentsMethod);
					Generator.Emit(OpCodes.Ret);
					TypeToWriteContentsFunc[KnownType] = CreateDelegate<Action<CbWriter, object>>(DynamicMethod);
				}
			}
		}

		static void CreateObjectWriter(Type Type, ILGenerator Generator, DynamicMethod ContentsWriter)
		{
			Generator.Emit(OpCodes.Ldarg_1);

			Label SkipLabel = Generator.DefineLabel();
			Generator.Emit(OpCodes.Brfalse, SkipLabel);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.BeginObject()), null);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_1);
			Generator.EmitCall(OpCodes.Call, ContentsWriter, null);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.EndObject()), null);

			Generator.MarkLabel(SkipLabel);
			Generator.Emit(OpCodes.Ret);
		}

		static void CreateNamedObjectWriter(Type Type, ILGenerator Generator, DynamicMethod ContentsWriter)
		{
			Generator.Emit(OpCodes.Ldarg_2);

			Label SkipLabel = Generator.DefineLabel();
			Generator.Emit(OpCodes.Brfalse, SkipLabel);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_1);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.BeginObject(null!)), null);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_2);
			Generator.EmitCall(OpCodes.Call, ContentsWriter, null);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.EndObject()), null);

			Generator.MarkLabel(SkipLabel);
			Generator.Emit(OpCodes.Ret);
		}

		static void CreateConcreteObjectContentsWriter(Type Type, ILGenerator Generator)
		{
			// Find the reflected properties from this type
			(Utf8String Name, PropertyInfo Property)[] Properties = GetProperties(Type);

			// Create a static type with the required reflection data
			Type ReflectedType = typeof(CbReflectedTypeInfo<>).MakeGenericType(Type);
			FieldInfo NamesField = ReflectedType.GetField(nameof(CbReflectedTypeInfo<object>.Names))!;
			NamesField.SetValue(null, Properties.Select(x => x.Name).ToArray());

			// Write the discriminator
			CbDiscriminatorAttribute? Discriminator = Type.GetCustomAttribute<CbDiscriminatorAttribute>();
			if (Discriminator != null)
			{
				FieldInfo DiscriminatorKeyField = GetFieldInfo(() => DiscriminatorKey);
				Generator.Emit(OpCodes.Ldarg_0);
				Generator.Emit(OpCodes.Ldsfld, DiscriminatorKeyField);
				Generator.Emit(OpCodes.Ldstr, Discriminator.Name);
				Generator.Emit(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.WriteString(default, null!)));
			}

			// Write all the remaining properties
			for (int Idx = 0; Idx < Properties.Length; Idx++)
			{
				PropertyInfo Property = Properties[Idx].Property;
				Type PropertyType = Property.PropertyType;

				// Get the field value
				Generator.Emit(OpCodes.Ldarg_1);
				Generator.EmitCall(OpCodes.Call, Property.GetMethod!, null);

				Label SkipLabel = Generator.DefineLabel();

				MethodInfo WriteMethod;
				if (TypeToMethods.TryGetValue(PropertyType, out CbClassConverterMethods? DynamicMethods))
				{
					Generator.Emit(OpCodes.Dup);
					Generator.Emit(OpCodes.Brfalse, SkipLabel);
					WriteMethod = DynamicMethods.WriteNamedMethod;
				}
				else
				{
					ICbConverterMethods Methods = CbConverterMethods.Get(PropertyType);
					WriteMethod = Methods.WriteNamedMethod;
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

		class CbPolymorphicDispatch<T>
		{
			public static Dictionary<Utf8String, Func<CbField, object>> NameToReadFunc = new Dictionary<Utf8String, Func<CbField, object>>();
			public static Dictionary<Type, Action<CbWriter, object>> TypeToWriteContentsFunc = new Dictionary<Type, Action<CbWriter, object>>();

			public static object Read(CbField Field)
			{
				Utf8String Name = Field.AsObject().Find(DiscriminatorKey).AsUtf8String();
				return NameToReadFunc[Name](Field);
			}

			public static void WriteContents(CbWriter Writer, object Value)
			{
				Type Type = Value!.GetType();
				TypeToWriteContentsFunc[Type](Writer, Value);
			}
		}

		static T CreateDelegate<T>(DynamicMethod Method) where T : Delegate
		{
			return (T)Method.CreateDelegate(typeof(T));
		}

		static void CreateConcreteObjectReader(Type Type, ILGenerator Generator)
		{
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
				if (TypeToMethods.TryGetValue(Property.PropertyType, out CbClassConverterMethods? DynamicMethods))
				{
					ReadMethod = DynamicMethods.ReadMethod;
				}
				else
				{
					ReadMethod = CbConverterMethods.Get(Property.PropertyType).ReadMethod;
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
			return PropertyList.ToArray();
		}

		static FieldInfo GetFieldInfo<T>(Expression<Func<T>> Expr)
		{
			return (FieldInfo)((MemberExpression)Expr.Body).Member;
		}

		static MethodInfo GetMethodInfo(Expression<Action> Expr)
		{
			return ((MethodCallExpression)Expr.Body).Method;
		}

		static MethodInfo GetMethodInfo<T>(Expression<Action<T>> Expr)
		{
			return ((MethodCallExpression)Expr.Body).Method;
		}
	}

	class CbClassConverter<T> : CbConverterBase<T>, ICbConverterMethods where T : class
	{
		CbClassConverterMethods Methods;

		Func<CbField, T> ReadFunc;
		Action<CbWriter, T> WriteFunc;
		Action<CbWriter, Utf8String, T> WriteNamedFunc;

		public CbClassConverter()
		{
			Methods = CbClassConverterMethods.Create(typeof(T));

			ReadFunc = CreateDelegate<Func<CbField, T>>(Methods.ReadMethod);
			WriteFunc = CreateDelegate<Action<CbWriter, T>>(Methods.WriteMethod);
			WriteNamedFunc = CreateDelegate<Action<CbWriter, Utf8String, T>>(Methods.WriteNamedMethod);
		}

		public MethodInfo ReadMethod => Methods.ReadMethod;

		public MethodInfo WriteMethod => Methods.WriteMethod;

		public MethodInfo WriteNamedMethod => Methods.WriteNamedMethod;

		static TDelegate CreateDelegate<TDelegate>(DynamicMethod Method) where TDelegate : Delegate => (TDelegate)Method.CreateDelegate(typeof(TDelegate));

		public override T Read(CbField Field) => ReadFunc(Field);

		public override void Write(CbWriter Writer, T Value) => WriteFunc(Writer, Value);

		public override void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => WriteNamedFunc(Writer, Name, Value);
	}

	class CbClassConverterFactory : CbConverterFactory
	{
		public override ICbConverter? CreateConverter(Type Type)
		{
			ICbConverter? Converter = null;
			if (Type.IsClass)
			{
				Type ConverterType = typeof(CbClassConverter<>).MakeGenericType(Type);
				Converter = (ICbConverter?)Activator.CreateInstance(ConverterType);
			}
			return Converter;
		}
	}
}
