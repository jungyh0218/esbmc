/*******************************************************************\

Module: C++ Language Type Checking

Author:

\*******************************************************************/

#include <config.h>
#include <bitvector.h>
#include <arith_tools.h>
#include <expr_util.h>
#include <simplify_expr_class.h>
#include <c_types.h>

#include <ansi-c/c_qualifiers.h>

#include "cpp_typecheck.h"

/*******************************************************************\

Function: standard_conversion_lvalue_to_rvalue

  Inputs: A typechecked  lvalue expression

  Outputs: True iff the lvalue-to-rvalue conversion is possible.
           'new_type' contains the result of the conversion.

  Purpose: Lvalue-to-rvalue conversion

  An lvalue (3.10) of a non-function, non-array type T can be
  converted to an rvalue. If T is an incomplete type, a program
  that necessitates this conversion is ill-formed. If the object
  to which the lvalue refers is not an object of type T and is
  not an object of a type derived from T, or if the object is
  uninitialized, a program that necessitates this conversion has
  undefined behavior. If T is a non-class type, the type of the
  rvalue is the cv-unqualified version of T. Otherwise, the type of
  the rvalue is T.

  The value contained in the object indicated by the lvalue
  is the rvalue result. When an lvalue-to-rvalue conversion
  occurs within the operand of sizeof (5.3.3) the value contained
  in the referenced object is not accessed, since that operator
  does not evaluate its operand.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_lvalue_to_rvalue(
  const exprt &expr,
  exprt &new_expr) const
{
  assert(expr.cmt_lvalue());

  if(expr.type().id()== "code" ||
     expr.type().id()== "incomplete_array" ||
     expr.type().id()== "incomplete_class" ||
     expr.type().id()== "incomplete_struct" ||
     expr.type().id()== "incomplete_union")
    return false;

  new_expr=expr;
  new_expr.remove("#lvalue");

  return true;
}

/*******************************************************************\

Function: standard_conversion_array_to_pointer

  Inputs: An array expression

  Outputs: True iff the array-to-pointer conversion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Array-to-pointer conversion

  An lvalue or rvalue of type "array of N T" or "array of unknown
  bound of T" can be converted to an rvalue of type "pointer to T."
  The result is a pointer to the first element of the array.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_array_to_pointer(
  const exprt &expr,
  exprt &new_expr) const
{
  assert(expr.type().id()=="array" ||
         expr.type().id()=="incomplete_array");

  exprt index("index", expr.type().subtype());
  index.copy_to_operands(expr, from_integer(0, int_type()));
  index.set("#lvalue",true);

  pointer_typet pointer;
  pointer.subtype()=expr.type().subtype();

  new_expr=exprt("address_of",pointer);
  new_expr.move_to_operands(index);

  return true;
}

/*******************************************************************\

Function: standard_conversion_function_to_pointer

  Inputs: A function expression

  Outputs: True iff the array-to-pointer conversion is possible.
           The result of the conversion is stored in 'new_expr'.


  Purpose:  Function-to-pointer conversion

  An lvalue of function type T can be converted to an rvalue of type
  "pointer to T." The result is a pointer to the function.50)

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_function_to_pointer(
  const exprt &expr, exprt &new_expr) const
{
  const code_typet &func_type=to_code_type(expr.type());

  if(!expr.cmt_lvalue())
    return false;

  pointer_typet pointer;
  pointer.subtype() = func_type;

  new_expr = exprt("address_of");
  new_expr.copy_to_operands(expr);
  new_expr.type() = pointer;

  return true;
}

/*******************************************************************\

Function: standard_conversion_qualification

  Inputs: A typechecked expression 'expr', a destination
          type 'type'

 Outputs: True iff the qualification conversion is possible.
          The result of the conversion is stored in 'new_expr'.

 Purpose: Qualification conversion

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_qualification(
  const exprt &expr,
  const typet &type,
  exprt &new_expr) const
{
  if(expr.type().id()!="pointer" ||
     is_reference(expr.type()))
    return false;

  if(expr.cmt_lvalue())
    return false;

  if(expr.type()!=type)
    return false;

  typet sub_from=expr.type().subtype();
  typet sub_to=type.subtype();
  bool const_to=true;

  while(sub_from.id()=="pointer")
  {
    c_qualifierst qual_from;
    qual_from.read(sub_from);

    c_qualifierst qual_to;
    qual_to.read(sub_to);

    if(!qual_to.is_constant)
      const_to=false;

    if(qual_from.is_constant && !qual_to.is_constant)
      return false;

    if(qual_from!=qual_to && !const_to)
      return false;

    typet tmp1=sub_from.subtype();
    sub_from.swap(tmp1);

    typet tmp2=sub_to.subtype();
    sub_to.swap(tmp2);
  }

  c_qualifierst qual_from(sub_from);
  c_qualifierst qual_to(sub_to);

  if(qual_from.is_subset_of(qual_to))
  {
    new_expr=expr;
    new_expr.type()=type;
    return true;
  }

  return false;
}

/*******************************************************************\

Function: standard_conversion_integral_promotion

  Inputs: A typechecked expression 'expr'

  Outputs: True iff the integral pormotion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Integral-promotion conversion

  An rvalue of type char, signed char, unsigned char, short int,
  or unsigned short int can be converted to an rvalue of type int
  if int can represent all the values of the source type; otherwise,
  the source rvalue can be converted to an rvalue of type unsigned int.

  An rvalue of type wchar_t (3.9.1) or an enumeration type (7.2) can
  be converted to an rvalue of the first of the following types that
  can represent all the values of its underlying type: int, unsigned int,
  long, or unsigned long.

  An rvalue for an integral bit-field (9.6) can be converted
  to an rvalue of type int if int can represent all the values of the
  bit-field; otherwise, it can be converted to unsigned int if
  unsigned int can represent all the values of the bit-field.
  If the bit-field is larger yet, no integral promotion applies to
  it. If the bit-field has an enumerated type, it is treated as
  any other value of that type for promotion purposes.

  An rvalue of type bool can be converted to an rvalue of type int,
  with false becoming zero and true becoming one.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_integral_promotion(
  const exprt &expr,
  exprt &new_expr) const
{
  if(expr.cmt_lvalue())
    return false;

  c_qualifierst qual_from;
  qual_from.read(expr.type());

  typet int_type("signedbv");
  int_type.width(config.ansi_c.int_width);
  qual_from.write(int_type);

  if(expr.type().id()=="signedbv")
  {
    unsigned width=to_signedbv_type(expr.type()).get_width();
    if(width >= config.ansi_c.int_width)
      return false;
    new_expr = expr;
    new_expr.make_typecast(int_type);
    return true;
  }

  if(expr.type().id()=="unsignedbv")
  {
    unsigned width=to_unsignedbv_type(expr.type()).get_width();
    if(width >= config.ansi_c.int_width)
      return false;
    new_expr = expr;
    if(width==config.ansi_c.int_width)
      int_type.id("unsignedbv");
    new_expr.make_typecast(int_type);
    return true;
  }

  if(follow(expr.type()).id()=="c_enum")
  {
    new_expr=expr;
    new_expr.make_typecast(int_type);
    return true;
  }

  return false;
}

/*******************************************************************\

Function: standard_conversion_floating_point_promotion

  Inputs: A typechecked expression 'expr'

  Outputs: True iff the integral promotion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Floating-point-promotion conversion

  An rvalue of type float can be converted to an rvalue of type
  double. The value is unchanged.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_floating_point_promotion(
  const exprt &expr,
  exprt &new_expr) const
{
  if(expr.cmt_lvalue())
    return false;

  // we only do that with 'float',
  // not with 'double' or 'long double'
  if(expr.type()!=float_type())
    return false;

  unsigned width=bv_width(expr.type());

  if(width!=config.ansi_c.single_width)
    return false;

  c_qualifierst qual_from;
  qual_from.read(expr.type());

  new_expr=expr;
  new_expr.make_typecast(double_type());
  qual_from.write(new_expr.type());

  return true;
}

/*******************************************************************\

Function: standard_conversion_integral_conversion

  Inputs: A typechecked expression 'expr', a destination
          type 'type'

  Outputs: True iff the integral pormotion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Integral conversion

  An rvalue of type char, signed char, unsigned char, short int,
  An rvalue of an integer type can be converted to an rvalue of
  another integer type. An rvalue of an enumeration type can be
  converted to an rvalue of an integer type.

  If the destination type is unsigned, the resulting value is the
  least unsigned integer congruent to the source integer (modulo
  2n where n is the number of bits used to represent the unsigned
  type). [Note: In a two's complement representation, this
  conversion is conceptual and there is no change in the bit
  pattern (if there is no truncation). ]

  If the destination type is signed, the value is unchanged if it
  can be represented in the destination type (and bit-field width);
  otherwise, the value is implementation-defined.

  If the destination type is bool, see 4.12. If the source type is
  bool, the value false is converted to zero and the value true is
  converted to one.

  The conversions allowed as integral promotions are excluded from
  the set of integral conversions.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_integral_conversion(
  const exprt &expr,
  const typet &type,
  exprt &new_expr) const
{
  if(type.id()!="signedbv" &&
     type.id()!="unsignedbv")
      return false;

  if(expr.type().id()!="signedbv" &&
     expr.type().id()!="unsignedbv" &&
     expr.type().id()!="bool" &&
     follow(expr.type()).id() != "c_enum")
    return false;

  if(expr.cmt_lvalue())
    return false;

  c_qualifierst qual_from;
  qual_from.read(expr.type());
  new_expr = expr;
  new_expr.make_typecast(type);
  qual_from.write(new_expr.type());

  return true;
}

/*******************************************************************\

Function: standard_conversion_floating_integral_conversion

  Inputs: A typechecked expression 'expr'

  Outputs: True iff the conversion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Floating-integral conversion

  An rvalue of a floating point type can be converted to an rvalue
  of an integer type. The conversion truncates; that is, the
  fractional part is discarded. The behavior is undefined if the
  truncated value cannot be represented in the destination type.
  [Note: If the destination type is bool, see 4.12. ]

  An rvalue of an integer type or of an enumeration type can be
  converted to an rvalue of a floating point type. The result is
  exact if possible. Otherwise, it is an implementation-defined
  choice of either the next lower or higher representable value.
  [Note: loss of precision occurs if the integral value cannot be
  represented exactly as a value of the floating type. ] If the
  source type is bool, the value false is converted to zero and the
  value true is converted to one.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_floating_integral_conversion(
  const exprt &expr,
  const typet &type,
  exprt &new_expr) const
{
  if(expr.cmt_lvalue())
    return false;

  if(expr.type().id()=="floatbv" ||
     expr.type().id()=="fixedbv" )
  {
    if(type.id()!="signedbv" &&
       type.id()!="unsignedbv")
      return false;
  }
  else if(expr.type().id()=="signedbv" ||
          expr.type().id()=="unsignedbv" ||
          follow(expr.type()).id()=="c_enum")
  {
    if(type.id()!="fixedbv" &&
       type.id()!="floatbv")
      return false;
  }
  else
    return false;

  c_qualifierst qual_from;
  qual_from.read(expr.type());
  new_expr = expr;
  new_expr.make_typecast(type);
  qual_from.write(new_expr.type());

  return true;
}


/*******************************************************************\

Function: standard_conversion_floating_point_conversion

  Inputs: A typechecked expression 'expr', a destination
          type 'type'

  Outputs: True iff the floating-point conversion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Floating-point conversion

  An rvalue of floating point type can be converted to an rvalue
  of another floating point type. If the source value can be exactly
  represented in the destination type, the result of the conversion
  is that exact representation. If the source value is between two
  adjacent destination values, the result of the conversion is an
  implementation-defined choice of either of those values. Otherwise,
  the behavior is undefined.

  The conversions allowed as floating point promotions are excluded
  from the set of floating point conversions.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_floating_point_conversion(
  const exprt &expr,
  const typet &type,
  exprt &new_expr) const
{
  if(expr.type().id()!="floatbv" &&
     expr.type().id()!="fixedbv")
    return false;

  if(type.id()!="floatbv" &&
     type.id()!="fixedbv")
      return false;

  if(expr.cmt_lvalue())
    return false;

  c_qualifierst qual_from;

  qual_from.read(expr.type());
  new_expr = expr;
  new_expr.make_typecast(type);
  qual_from.write(new_expr.type());

  return true;
}

/*******************************************************************\

Function: standard_conversion_pointer

  Inputs: A typechecked expression 'expr', a destination
          type 'type'

  Outputs: True iff the pointer conversion is possible.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  Pointer conversion

  A null pointer constant is an integral constant expression
  (5.19) rvalue of integer type that evaluates to zero. A null
  pointer constant can be converted to a pointer type; the result
  is the null pointer value of that type and is distinguishable
  from every other value of pointer to object or pointer to
  function type. Two null pointer values of the same type shall
  compare equal. The conversion of a null pointer constant to a
  pointer to cv-qualified type is a single conversion, and not the
  sequence of a pointer conversion followed by a qualification
  conversion (4.4).

  An rvalue of type "pointer to cv T," where T is an object type,
  can be converted to an rvalue of type "pointer to cv void." The
  result of converting a "pointer to cv T" to a "pointer to cv
  void" points to the start of the storage location where the
  object of type T resides, as if the object is a most derived
  object (1.8) of type T (that is, not a base class subobject).

  An rvalue of type "pointer to cv D," where D is a class type,
  can be converted to an rvalue of type "pointer to cv B," where
  B is a base class (clause 10) of D. If B is an inaccessible
  (clause 11) or ambiguous (10.2) base class of D, a program that
  necessitates this conversion is ill-formed. The result of the
  conversion is a pointer to the base class sub-object of the
  derived class object. The null pointer value is converted to
  the null pointer value of the destination type.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_pointer(
  const exprt &expr,
  const typet &type,
  exprt &new_expr)
{
  if(type.id()!="pointer" ||
     is_reference(type) ||
     type.find("to-member").is_not_nil())
    return false;

  if(expr.cmt_lvalue())
    return false;

  if(expr.is_zero())
  {
    new_expr = expr;
    new_expr.value("NULL");
    new_expr.type() = type;
    return true;
  }

  if(expr.type().id() != "pointer" ||
     expr.type().find("to-member").is_not_nil())
    return false;

  typet sub_from = follow(expr.type().subtype());
  typet sub_to = follow(type.subtype());

  // anything but function pointer to void *
  if(sub_from.id()!="code" && sub_to.id()=="empty")
  {
    c_qualifierst qual_from;
    qual_from.read(expr.type().subtype());
    new_expr = expr;
    new_expr.make_typecast(type);
    qual_from.write(new_expr.type().subtype());
    return true;
  }

  // struct * to struct *
  if(sub_from.id()=="struct" && sub_to.id()=="struct")
  {
    const struct_typet& from_struct = to_struct_type(sub_from);
    const struct_typet& to_struct = to_struct_type(sub_to);
    if(subtype_typecast(from_struct, to_struct))
    {
      c_qualifierst qual_from;
      qual_from.read(expr.type().subtype());
      new_expr = expr;
      make_ptr_typecast(new_expr, type);
      qual_from.write(new_expr.type().subtype());
      return true;
    }
  }

  return false;
}

/*******************************************************************\

Function: standard_conversion_pointer

  Inputs: A typechecked expression 'expr', a destination
          type 'type'

  Outputs: True iff the pointer-to-member conversion is possible.
           The result of the conversion is stored in 'new_expr'.


  Purpose:  Pointer-to-member conversion

  A null pointer constant (4.10) can be converted to a pointer to
  member type; the result is the null member pointer value of that
  type and is distinguishable from any pointer to member not created
  from a null pointer constant. Two null member pointer values of
  the same type shall compare equal. The conversion of a null pointer
  constant to a pointer to member of cv-qualified type is a single
  conversion, and not the sequence of a pointer to member conversion
  followed by a qualification conversion (4.4).

  An rvalue of type "pointer to member of B of type cv T," where B
  is a class type, can be converted to an rvalue of type "pointer
  to member of D of type cv T," where D is a derived class
  (clause 10) of B. If B is an inaccessible (clause 11), ambiguous
  (10.2) or virtual (10.1) base class of D, a program that
  necessitates this conversion is ill-formed. The result of the
  conversion refers to the same member as the pointer to member
  before the conversion took place, but it refers to the base class
  member as if it were a member of the derived class. The result
  refers to the member in D"s instance of B. Since the result has
  type "pointer to member of D of type cv T," it can be dereferenced
  with a D object. The result is the same as if the pointer to
  member of B were dereferenced with the B sub-object of D. The null
  member pointer value is converted to the null member pointer value
  of the destination type.52)

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_pointer_to_member(
  const exprt &expr,
  const typet &type,
  exprt &new_expr)
{
  if(type.id()!="pointer" ||
     is_reference(type) ||
     type.find("to-member").is_nil())
    return false;

  if(expr.type().id() != "pointer" ||
     expr.type().find("to-member").is_nil())
    return false;

  if(type.subtype()!=expr.type().subtype())
  {
    // subtypes different
    if(type.subtype().id()=="code"  &&
       expr.type().subtype().id()=="code")
    {
      code_typet code1 = to_code_type(expr.type().subtype());
      assert(code1.arguments().size()>0);
      code_typet::argumentt this1 = code1.arguments()[0];
      assert(this1.cmt_base_name()=="this");
      code1.arguments().erase(code1.arguments().begin());

      code_typet code2 = to_code_type(type.subtype());
      assert(code2.arguments().size()>0);
      code_typet::argumentt this2 = code2.arguments()[0];
      assert(this2.cmt_base_name()=="this");
      code2.arguments().erase(code2.arguments().begin());

      if(this2.type().subtype().cmt_constant() &&
         !this1.type().subtype().cmt_constant())
        return false;

      // give a second chance ignoring `this'
      if(code1!=code2)
        return false;
    }
    else
      return false;
  }

  if(expr.cmt_lvalue())
    return false;

  if(expr.id()=="constant" &&
     expr.value()=="NULL")
  {
    new_expr = expr;
    new_expr.make_typecast(type);
    return true;
  }

  struct_typet from_struct =
    to_struct_type(follow(static_cast<const typet &>
      (expr.type().find("to-member"))));

  struct_typet to_struct =
    to_struct_type(follow(static_cast<const typet &>
      (type.find("to-member"))));

  if(subtype_typecast(to_struct, from_struct))
  {
    new_expr = expr;
    new_expr.make_typecast(type);
    return true;
  }

  return false;
}

/*******************************************************************\

Function: standard_conversion_boolean

  Inputs: A typechecked expression 'expr'

  Outputs: True iff the boolean conversion is possible.
           The result of the conversion is stored in 'new_expr'.


  Purpose:  Boolean conversion

  An rvalue of arithmetic, enumeration, pointer, or pointer to
  member type can be converted to an rvalue of type bool.
  A zero value, null pointer value, or null member pointer value is
  converted to false; any other value is converted to true.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_boolean(
  const exprt &expr, exprt &new_expr) const
{
  if(expr.cmt_lvalue())
    return false;

  if(expr.type().id()!="signedbv" &&
     expr.type().id()!="unsignedbv" &&
     expr.type().id()!="pointer" &&
     follow(expr.type()).id()!="c_enum")
    return false;

  c_qualifierst qual_from;
  qual_from.read(expr.type());

  bool_typet Bool;
  qual_from.write(Bool);

  new_expr=expr;
  new_expr.make_typecast(Bool);
  return true;
}

/*******************************************************************\

Function: standard_conversion_sequence

  Inputs: A typechecked expression 'expr', a destination
          type 'type'.

  Outputs: True iff a standard conversion sequence exists.
           The result of the conversion is stored in 'new_expr'.
           The reference 'rank' is incremented.


  Purpose:  Standard Conversion Sequence

  A standard conversion sequence is a sequence of standard conversions
  in the following order:

  * Zero or one conversion from the following set: lvalue-to-rvalue
    conversion, array-to-pointer conversion, and function-to-pointer
    conversion.

  * Zero or one conversion from the following set: integral
    promotions, floating point promotion, integral conversions,
    floating point conversions, floating-integral conversions,
    pointer conversions, pointer to member conversions, and boolean
    conversions.

  * Zero or one qualification conversion.

\*******************************************************************/

bool cpp_typecheckt::standard_conversion_sequence(
  const exprt &expr,
  const typet &type,
  exprt &new_expr,
  cpp_typecast_rank &rank)
{
  assert(!is_reference(expr.type()) && !is_reference(type));

  exprt curr_expr=expr;

  if(curr_expr.type().id()=="array" ||
     curr_expr.type().id()=="incomplete_array")
  {
    if(type.id()=="pointer")
    {
      if(!standard_conversion_array_to_pointer(curr_expr, new_expr))
        return false;
    }
  }
  else if(curr_expr.type().id()=="code" &&
          type.id()=="pointer")
  {
    if(!standard_conversion_function_to_pointer(curr_expr, new_expr))
      return false;
  }
  else if(curr_expr.cmt_lvalue())
  {
    if(!standard_conversion_lvalue_to_rvalue(curr_expr, new_expr))
      return false;
  }
  else
    new_expr=curr_expr;

  curr_expr.swap(new_expr);

  if(curr_expr.type()!=type)
  {
    if(type.id()=="signedbv" ||
       type.id()=="unsignedbv" ||
       follow(type).id()=="c_enum")
    {
      if(!standard_conversion_integral_promotion(curr_expr, new_expr) ||
         new_expr.type() != type)
      {
        if(!standard_conversion_integral_conversion(curr_expr, type, new_expr))
        {
          if(!standard_conversion_floating_integral_conversion(curr_expr, type, new_expr))
            return false;
        }
        rank.rank+=3;
      }
      else
        rank.rank+=2;
    }
    else if(type.id()=="floatbv" || type.id()=="fixedbv")
    {
      if(!standard_conversion_floating_point_promotion(curr_expr, new_expr) ||
         new_expr.type() != type)
      {
        if(!standard_conversion_floating_point_conversion(curr_expr, type, new_expr) &&
           !standard_conversion_floating_integral_conversion(curr_expr,type, new_expr))
          return false;

        rank.rank += 3;
      }
      else
        rank.rank += 2;
    }
    else if(type.id()=="pointer")
    {
      if(!standard_conversion_pointer(curr_expr, type, new_expr))
      {
        if(!standard_conversion_pointer_to_member(curr_expr, type, new_expr))
          return false;
      }
      rank.rank += 3;

      // Did we just cast to a void pointer?
      if (type.subtype().id() == "empty" && curr_expr.type().id() == "pointer"
          && curr_expr.type().subtype().id() != "empty")
        rank.has_ptr_to_voidptr = true;
    }
    else if(type.id()=="bool")
    {
      if(!standard_conversion_boolean(curr_expr,new_expr))
        return false;
      rank.rank += 3;

      // Pointer to bool conversion might lead to special disambiguation later
      if (curr_expr.type().id() == "pointer")
        rank.has_ptr_to_bool = true;
    }
    else
      return false;
  }
  else
    new_expr = curr_expr;

  curr_expr.swap(new_expr);

  if(curr_expr.type().id()=="pointer")
  {
    typet sub_from=curr_expr.type();
    typet sub_to=type;

    do
    {
      typet tmp_from = sub_from.subtype();
      sub_from.swap(tmp_from);
      typet tmp_to = sub_to.subtype();
      sub_to.swap(tmp_to);

      c_qualifierst qual_from;
      qual_from.read(sub_from);

      c_qualifierst qual_to;
      qual_to.read(sub_to);

      if(qual_from!=qual_to)
      {
        rank.rank+=1;
        break;
      }

    }
    while(sub_from.id()=="pointer");

    if(!standard_conversion_qualification(curr_expr, type, new_expr))
      return false;
  }
  else
  {
    new_expr = curr_expr;
    new_expr.type() = type;
  }

  return true;
}

/*******************************************************************\

Function: user_defined_conversion_sequence

  Inputs: A typechecked expression 'expr', a destination
          type 'type'.

  Outputs: True iff a user-defined conversion sequence exists.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  User-defined conversion sequence

\*******************************************************************/

bool cpp_typecheckt::user_defined_conversion_sequence(
  const exprt &expr,
  const typet &type,
  exprt &new_expr,
  cpp_typecast_rank &rank)
{
  static bool recursion_guard = false;
  assert(!is_reference(expr.type()));
  assert(!is_reference(type));

  const typet &from = follow(expr.type());
  const typet &to = follow(type);

  new_expr.make_nil();

  rank.rank +=4;

  if(to.id()=="struct")
  {
    std::string err_msg;

    if(cpp_is_pod(to))
    {
      if(from.id()=="struct")
      {
        const struct_typet &from_struct=to_struct_type(from);
        const struct_typet &to_struct=to_struct_type(to);

        if(subtype_typecast(from_struct,to_struct) &&
            expr.cmt_lvalue() )
        {
          exprt address("address_of", pointer_typet());
          address.copy_to_operands(expr);
          address.type().subtype() = expr.type();

          // simplify address
          if(expr.id()=="dereference")
            address=expr.op0();

          pointer_typet ptr_sub;
          ptr_sub.subtype() = type;
          c_qualifierst qual_from;
          qual_from.read(expr.type());
          qual_from.write(ptr_sub.subtype());
          make_ptr_typecast(address, ptr_sub);

          exprt deref("dereference");
          deref.copy_to_operands(address);
          deref.type() = address.type().subtype();

          // create temporary object
          exprt tmp_object_expr=exprt("sideeffect", type);
          tmp_object_expr.statement("temporary_object");
          tmp_object_expr.location()=expr.location();
          tmp_object_expr.copy_to_operands(deref);
          tmp_object_expr.set("#lvalue", true);

          new_expr.swap(tmp_object_expr);
          return true;
        }
      }
    }
    else if (!recursion_guard)
    {
      struct_typet from_struct;
      from_struct.make_nil();

      if(from.id()=="struct")
        from_struct=to_struct_type(from);

      struct_typet to_struct=to_struct_type(to);

      // Look up a constructor that will build us a temporary of the correct
      // type, and takes an argument of the relevant type. Do this by asking
      // the resolve code to look it up for us; this avoids duplication, and
      // nets us template constructors too.

      // Move to the struct scope
      cpp_scopet &scope = cpp_scopes.get_scope(to_struct.name());
      cpp_save_scopet cpp_saved_scope(cpp_scopes);
      cpp_scopes.go_to(scope);

      // Just look up the plain name from this scope.
      // XXX this is super dodgy.
      irept thename = to_struct.add("tag").get_sub()[0];
      cpp_namet name_record;
      name_record.get_sub().push_back(thename);

      // Make a fake temporary.
      symbol_exprt fake_temp("fake_temporary", type);
      fake_temp.type().remove("#constant");
      fake_temp.cmt_lvalue(true);

      cpp_typecheck_fargst fargs;
      fargs.in_use = true;
      fargs.add_object(fake_temp);
      fargs.operands.push_back(expr);

      // Disallow more than one level of implicit construction.
      recursion_guard = true;
      exprt result = resolve(name_record, cpp_typecheck_resolvet::VAR, fargs,
                             false);
      recursion_guard = false;

      if (result.is_nil())
        goto out;

      // XXX explicit?
      if (result.type().get_bool("is_explicit"))
        goto out;

      if (result.type().id() !="code")
        goto out;

      if(result.type().return_type().id() !="constructor")
        goto out;

      result.location() = expr.location();

      {
        exprt tmp("already_typechecked");
        tmp.copy_to_operands(result);
        result.swap(result);
      }

      // create temporary object
      side_effect_expr_function_callt ctor_expr;
      ctor_expr.location() = expr.location();
      ctor_expr.function().swap(result);
      ctor_expr.arguments().push_back(expr);
      typecheck_side_effect_function_call(ctor_expr);

      new_expr.swap(ctor_expr);
      assert(new_expr.statement()=="temporary_object");

      if(to.cmt_constant())
        new_expr.type().cmt_constant(true);
    }
  }
  else if(to.id() == "bool")
  {
    std::string name = expr.type().identifier().as_string();
    if(name == "cpp::std::tag.istream"
      || name == "cpp::std::tag.ostream"
      || name == "cpp::std::tag.iostream"
      || name == "cpp::std::tag.ifstream")
    {
      exprt nondet_expr("nondet_symbol", bool_typet());
      new_expr.swap(nondet_expr);
    }
    else if(expr.id() == "symbol")
    {
      // Can't blindly cast aggregate / composite types. NB: Nothing here
      // actually appears to look up any custom convertors.
      if (expr.type().id() == "array" || expr.type().id() == "struct" ||
          expr.type().id() == "union")
        return false;

      exprt tmp_expr = expr;
      tmp_expr.make_typecast(bool_typet());
      new_expr.swap(tmp_expr);
    }
  }

out:

  // conversion operators
  if(from.id()=="struct")
  {
    struct_typet from_struct = to_struct_type(from);

    bool found = false;
    for(struct_typet::componentst::const_iterator
        it = from_struct.components().begin();
        it != from_struct.components().end(); it++)
    {
      const irept& component = *it;
      const typet comp_type = static_cast<const typet&>(component.type());

      if(component.get_bool("from_base"))
        continue;

      if(!component.get_bool("is_cast_operator"))
        continue;

      assert(component.get("type") == "code" &&
             component.type().arguments().get_sub().size() == 1);

      typet this_type =
        static_cast<const typet&>(comp_type.arguments()
                                           .get_sub()
                                           .front()
                                           .type());
      this_type.set("#reference", true);

      exprt this_expr(expr);
      this_type.set("#this", true);

      cpp_typecast_rank tmp_rank;
      exprt tmp_expr;

      if(implicit_conversion_sequence(
        this_expr, this_type, tmp_expr, tmp_rank))
      {
        // To take care of the possible virtual case,
        // we build the function as a member expression.
        irept func_name("name");
        func_name.identifier(component.base_name());
        cpp_namet cpp_func_name;
        cpp_func_name.get_sub().push_back(func_name);

        exprt member_func("member");
        member_func.add("component_cpp_name") = cpp_func_name;
        exprt ac("already_typechecked");
        ac.copy_to_operands(expr);
        member_func.copy_to_operands(ac);

        side_effect_expr_function_callt func_expr;
        func_expr.location() = expr.location();
        func_expr.function().swap(member_func);
        typecheck_side_effect_function_call(func_expr);

        exprt tmp_expr;
        if(standard_conversion_sequence(func_expr,type, tmp_expr, tmp_rank))
        {
          // check if it's ambiguous
          if(found)
            return false;
          found = true;

          rank+=tmp_rank;
          new_expr.swap(tmp_expr);
        }
      }
    }
    if(found)
      return true;
  }

  return new_expr.is_not_nil();
}

/*******************************************************************\

Function: reference_related

  Inputs: A typechecked expression 'expr',
          a reference 'type'.

  Outputs: True iff an the reference 'type' is reference-related
           to 'expr'.

  Purpose:  Reference-related

\*******************************************************************/

bool cpp_typecheckt::reference_related(
  const exprt &expr,
  const typet &type) const
{
  assert(is_reference(type));
  assert(!is_reference(expr.type()));
  if(expr.type() == type.subtype())
    return true;

  typet from = follow(expr.type());
  typet to = follow(type.subtype());

  if(from.id()=="struct" &&
     to.id()=="struct")
    return subtype_typecast(to_struct_type(from),
                            to_struct_type(to));

  if(from.id()=="struct" &&
     type.get_bool("#this") &&
     type.subtype().id()=="empty")
  {
    // virtual-call case
    return true;
  }

  return false;
}

/*******************************************************************\

Function: reference_compatible

  Inputs: A typechecked expression 'expr', a
          reference 'type'.

  Outputs: True iff an the reference 'type' is reference-compatible
           to 'expr'.

  Purpose:  Reference-compatible

\*******************************************************************/

bool cpp_typecheckt::reference_compatible(
  const exprt &expr,
  const typet &type,
  cpp_typecast_rank &rank) const
{
  assert(is_reference(type));
  assert(!is_reference(expr.type()));

  if(!reference_related(expr, type))
    return false;

  if(expr.type()!=type.subtype())
    rank.rank+=3;

  c_qualifierst qual_from;
    qual_from.read(expr.type());

  c_qualifierst qual_to;
    qual_to.read(type.subtype());

  if(qual_from!=qual_to)
    rank.rank+=1;

  if(qual_from.is_subset_of(qual_to))
    return true;

  return false;
}

/*******************************************************************\

Function: reference_binding

  Inputs: A typechecked expression 'expr', a
          reference 'type'.

  Outputs: True iff an the reference can be bound to the expression.
           The result of the conversion is stored in 'new_expr'.


  Purpose:  Reference binding

  When a parameter of reference type binds directly (8.5.3) to an
  argument expression, the implicit conversion sequence is the
  identity conversion, unless the argument expression has a type
  that is a derived class of the parameter type, in which case the
  implicit conversion sequence is a derived-to-base Conversion
  (13.3.3.1).

  If the parameter binds directly to the result of applying a
  conversion function to the argument expression, the implicit
  conversion sequence is a user-defined conversion sequence
  (13.3.3.1.2), with the second standard conversion sequence
  either an identity conversion or, if the conversion function
  returns an entity of a type that is a derived class of the
  parameter type, a derived-to-base Conversion.

  When a parameter of reference type is not bound directly to
  an argument expression, the conversion sequence is the one
  required to convert the argument expression to the underlying
  type of the reference according to 13.3.3.1. Conceptually, this
  conversion sequence corresponds to copy-initializing a temporary
  of the underlying type with the argument expression. Any
  difference in top-level cv-qualification is subsumed by the
  initialization itself and does not constitute a conversion.

  A standard conversion sequence cannot be formed if it requires
  binding a reference to non-const to an rvalue (except when
  binding an implicit object parameter; see the special rules
  for that case in 13.3.1).

\*******************************************************************/

bool cpp_typecheckt::reference_binding(
  exprt expr,
  const typet &type,
  exprt &new_expr,
  cpp_typecast_rank &rank)
{
  assert(is_reference(type));
  assert(!is_reference(expr.type()));

  cpp_typecast_rank backup_rank = rank;

  if(type.get_bool("#this") &&
     !expr.cmt_lvalue())
  {
    // `this' has to be an lvalue
    if(expr.statement()=="temporary_object")
      expr.set("#lvalue", true);
    else if(expr.statement()=="function_call")
      expr.set("#lvalue", true);
    else if(expr.get_bool("#temporary_avoided"))
    {
      expr.remove("#temporary_avoided");
      exprt temporary;
      new_temporary(expr.location(),expr.type(), expr, temporary);
      expr.swap(temporary);
      expr.set("#lvalue",true);
    }
    else
      return false;
  }

  if(expr.cmt_lvalue())
  {
    // TODO: Check this
    if(reference_compatible(expr, type, rank))
    {
      if(expr.id() == "dereference")
      {
        new_expr = expr.op0();
        new_expr.type().set("#reference",true);
      }
      else
      {
        address_of_exprt tmp;
        tmp.location()=expr.location();
        tmp.object()=expr;
        tmp.type()=pointer_typet();
        tmp.type().set("#reference", true);
        tmp.type().subtype()=tmp.op0().type();
        new_expr.swap(tmp);
      }

      if(expr.type()!=type.subtype())
      {
        c_qualifierst qual_from;
        qual_from.read(expr.type());
        new_expr.make_typecast(type);
        qual_from.write(new_expr.type().subtype());
      }

      return true;
    }

    rank = backup_rank;
  }

  // conversion operators
  typet from_type = follow(expr.type());
  if(from_type.id()=="struct")
  {
    struct_typet from_struct = to_struct_type(from_type);

    for(struct_typet::componentst::const_iterator
        it = from_struct.components().begin();
        it != from_struct.components().end(); it++)
    {
      const irept& component = *it;

      if(component.get_bool("from_base"))
        continue;

      if(!component.get_bool("is_cast_operator"))
        continue;

      const code_typet& component_type =
        to_code_type(static_cast<const typet&>(component.type()));

      // otherwise it cannot bind directly (not an lvalue)
      if(!is_reference(component_type.return_type()))
        continue;

      assert(component_type.arguments().size()==1);

      typet this_type =
        component_type.arguments().front().type();
      this_type.set("#reference", true);

      exprt this_expr(expr);

      this_type.set("#this", true);

      cpp_typecast_rank tmp_rank;

      exprt tmp_expr;
      if(implicit_conversion_sequence(
        this_expr, this_type, tmp_expr, tmp_rank))
      {
        // To take care of the possible virtual case,
        // we build the function as a member expression.
        irept func_name("name");
        func_name.identifier(component.base_name());
        cpp_namet cpp_func_name;
        cpp_func_name.get_sub().push_back(func_name);

        exprt member_func("member");
        member_func.add("component_cpp_name") = cpp_func_name;
        exprt ac("already_typechecked");
        ac.copy_to_operands(expr);
        member_func.copy_to_operands(ac);

        side_effect_expr_function_callt func_expr;
        func_expr.location() = expr.location();
        func_expr.function().swap(member_func);
        typecheck_side_effect_function_call(func_expr);

        // let's check if the returned value binds directly
        exprt returned_value = func_expr;
        add_implicit_dereference(returned_value);

        if(returned_value.cmt_lvalue()
           && reference_compatible(returned_value,type, rank))
        {
          // returned values are lvalues in case of references only
          assert(returned_value.id()=="dereference" &&
                 is_reference(returned_value.op0().type()));

          new_expr = returned_value.op0();

          if(returned_value.type() != type.subtype())
          {
            c_qualifierst qual_from;
            qual_from.read(returned_value.type());
            make_ptr_typecast(new_expr,type);
            qual_from.write(new_expr.type().subtype());
          }
          rank += tmp_rank;
          rank.rank += 4;
          return true;
        }
      }
    }
  }

  // No temporary allowed for `this'
  if(type.get_bool("#this"))
    return false;

  if(!type.subtype().cmt_constant() ||
     type.subtype().cmt_volatile())
    return false;

  // TODO: hanlde the case for implicit parameters
  if(!type.subtype().cmt_constant() &&
     !expr.cmt_lvalue())
    return false;

  exprt arg_expr = expr;

  if(follow(arg_expr.type()).id()=="struct")
  {
    // required to initialize the temporary
    arg_expr.set("#lvalue", true);
  }

  if(user_defined_conversion_sequence(arg_expr,type.subtype(), new_expr, rank))
  {
    address_of_exprt tmp;
    tmp.type()=pointer_typet();
    tmp.object()=new_expr;
    tmp.type().set("#reference", true);
    tmp.type().subtype()= new_expr.type();
    tmp.location()=new_expr.location();
    new_expr.swap(tmp);
    return true;
  }

  rank = backup_rank;
  if(standard_conversion_sequence(expr,type.subtype(),new_expr,rank))
  {
    {
      // create temporary object
      exprt tmp=exprt("sideeffect", type.subtype());
      tmp.statement("temporary_object");
      tmp.location()=expr.location();
      //tmp.set("#lvalue", true);
      tmp.move_to_operands(new_expr);
      new_expr.swap(tmp);
    }

    exprt tmp("address_of", pointer_typet());
    tmp.copy_to_operands(new_expr);
    tmp.type().set("#reference",true);
    tmp.type().subtype()= new_expr.type();
    tmp.location()=new_expr.location();
    new_expr.swap(tmp);
    return true;
  }

  return false;
}

/*******************************************************************\

Function: implicit_conversion_sequence

  Inputs: A typechecked expression 'expr', a destination
          type 'type'.

  Outputs: True iff an implicit conversion sequence exists.
           The result of the conversion is stored in 'new_expr'.
           The rank of the sequence is stored in 'rank'

  Purpose:  implicit conversion sequence

\*******************************************************************/

bool cpp_typecheckt::implicit_conversion_sequence(
  const exprt &expr,
  const typet &type,
  exprt &new_expr,
  cpp_typecast_rank &rank)
{
  cpp_typecast_rank backup_rank = rank;

  exprt e=expr;
  add_implicit_dereference(e);

  if(is_reference(type))
  {
    if(!reference_binding(e, type, new_expr, rank))
      return false;

    simplify_exprt simplify;
    simplify.simplify(new_expr);
    new_expr.type().set("#reference", true);
  }
  else if(!standard_conversion_sequence(e, type, new_expr, rank))
  {
    rank = backup_rank;
    if(!user_defined_conversion_sequence(e, type, new_expr, rank))
      return false;

    simplify_exprt simplify;
    simplify.simplify(new_expr);
  }

  return true;
}

/*******************************************************************\

Function: implicit_conversion_sequence

  Inputs: A typechecked expression 'expr', a destination
          type 'type'.

  Outputs: True iff an implicit conversion sequence exists.
           The result of the conversion is stored in 'new_expr'.

  Purpose:  implicit conversion sequence

\*******************************************************************/

bool cpp_typecheckt::implicit_conversion_sequence(
  const exprt &expr,
  const typet &type,
  exprt &new_expr)
{
  cpp_typecast_rank rank;
  return implicit_conversion_sequence(expr, type, new_expr, rank);
}

/*******************************************************************\

Function: implicit_conversion_sequence

  Inputs: A typechecked expression 'expr', a destination
          type 'type'.

  Outputs: True iff an implicit conversion sequence exists.
           The rank of the sequence is stored in 'rank'


  Purpose:  implicit conversion sequence


\*******************************************************************/

bool cpp_typecheckt::implicit_conversion_sequence(
  const exprt &expr,
  const typet &type,
  cpp_typecast_rank &rank)
{
  exprt new_expr;
  return implicit_conversion_sequence(expr, type, new_expr, rank);
}

/*******************************************************************\

Function: cpp_typecheck_baset::implicit_typecast

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void cpp_typecheckt::implicit_typecast(exprt &expr, const typet &type)
{
  exprt e = expr;

  if(!implicit_conversion_sequence(e, type, expr))
  {
    show_instantiation_stack(str);
    err_location(e);
    str << "invalid implicit conversion from `"
        << to_string(e.type()) << "' to `"
        << to_string(type) << "' ";
    throw 0;
  }
}

/*******************************************************************\

Function: cpp_typecheck_baset::reference_initializer

  Inputs:

 Outputs:

 Purpose:

 A reference to type "cv1 T1" is initialized by an expression of
 type "cv2 T2" as follows:

 - If the initializer expression
   - is an lvalue (but is not a bit-field), and "cv1 T1" is
     reference-compatible with "cv2 T2," or
   - has a class type (i.e., T2 is a class type) and can be
     implicitly converted to an lvalue of type "cv3 T3," where
     "cv1 T1" is reference-compatible with "cv3 T3" 92) (this
     conversion is selected by enumerating the applicable conversion
     functions (13.3.1.6) and choosing the best one through overload
     resolution (13.3)),

   then the reference is bound directly to the initializer
   expression lvalue in the first case, and the reference is
   bound to the lvalue result of the conversion in the second
   case. In these cases the reference is said to bind directly
   to the initializer expression.

 - Otherwise, the reference shall be to a non-volatile const type
   - If the initializer expression is an rvalue, with T2 a class
     type, and "cv1 T1" is reference-compatible with "cv2 T2," the
     reference is bound in one of the following ways (the choice is
     implementation-defined):

     - The reference is bound to the object represented by the
       rvalue (see 3.10) or to a sub-object within that object.

     - A temporary of type "cv1 T2" [sic] is created, and a
       constructor is called to copy the entire rvalue object into
       the temporary. The reference is bound to the temporary or
       to a sub-object within the temporary.

     The constructor that would be used to make the copy shall be
     callable whether or not the copy is actually done.

     Otherwise, a temporary of type "cv1 T1" is created and
     initialized from the initializer expression using the rules for
     a non-reference copy initialization (8.5). The reference is then
     bound to the temporary. If T1 is reference-related to T2, cv1
     must be the same cv-qualification as, or greater cvqualification
     than, cv2; otherwise, the program is ill-formed.

\*******************************************************************/

void cpp_typecheckt::reference_initializer(
  exprt &expr,
  const typet &type)
{
  assert(is_reference(type));
  add_implicit_dereference(expr);

  cpp_typecast_rank rank;
  exprt new_expr;
  if(reference_binding(expr,type,new_expr,rank))
  {
    expr.swap(new_expr);
    return;
  }

  err_location(expr);
  str << "bad reference initializer";
  throw 0;
}

/*******************************************************************\

Function: cpp_typecheckt::cast_away_constness

  Inputs:

 Outputs:

 Purpose:


\*******************************************************************/

bool cpp_typecheckt::cast_away_constness(
  const typet &t1,
  const typet &t2) const
{
  assert(t1.id()=="pointer" && t2.id()=="pointer");
  typet nt1 = t1;
  typet nt2 = t2;

  if(is_reference(nt1))
    nt1.remove("#reference");

  if(is_reference(nt2))
    nt2.remove("#reference");

  // substitute final subtypes
  std::vector<typet> snt1;
  snt1.push_back(nt1);

  while(snt1.back().find("subtype").is_not_nil())
  {
    snt1.reserve(snt1.size()+1);
    snt1.push_back(snt1.back().subtype());
  }

  c_qualifierst q1;
  q1.read(snt1.back());

  bool_typet newnt1;
  q1.write(newnt1);
  snt1.back() = newnt1;

  std::vector<typet> snt2;
  snt2.push_back(nt2);
  while(snt2.back().find("subtype").is_not_nil())
  {
    snt2.reserve(snt2.size()+1);
    snt2.push_back(snt2.back().subtype());
  }

  c_qualifierst q2;
  q2.read(snt2.back());

  bool_typet newnt2;
  q2.write(newnt2);
  snt2.back() = newnt2;

  const int k = snt1.size() < snt2.size() ? snt1.size() : snt2.size();

  for(int i = k; i > 1; i--)
  {
    snt1[snt1.size()-2].subtype() = snt1[snt1.size()-1];
    snt1.pop_back();

    snt2[snt2.size()-2].subtype() = snt2[snt2.size()-1];
    snt2.pop_back();
  }

  exprt e1("Dummy", snt1.back());
  exprt e2;

  return !standard_conversion_qualification(e1, snt2.back(), e2);
}

/*******************************************************************\

Function: cpp_typecheckt::const_typecast

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cpp_typecheckt::const_typecast(
  const exprt &expr,
  const typet &type,
  exprt &new_expr)
{
  assert(is_reference(expr.type())==false);

  exprt curr_expr = expr;

  if(curr_expr.type().id()== "array" ||
     curr_expr.type().id()=="incomplete_array")
  {
    if(type.id()=="pointer")
    {
      if(!standard_conversion_array_to_pointer(curr_expr, new_expr))
        return false;
    }
  }
  else if(curr_expr.type().id()=="code" &&
          type.id()=="pointer")
  {
    if(!standard_conversion_function_to_pointer(curr_expr, new_expr))
        return false;
  }
  else if(curr_expr.cmt_lvalue())
  {
    if(!standard_conversion_lvalue_to_rvalue(curr_expr, new_expr))
      return false;
  }
  else
    new_expr = curr_expr;

  if(is_reference(type))
  {
    if(!expr.cmt_lvalue())
      return false;

    if(new_expr.type()!=type.subtype())
      return false;

    exprt address_of("address_of", type);
    address_of.copy_to_operands(expr);
    add_implicit_dereference(address_of);
    new_expr.swap(address_of);
    return true;
  }
  else if(type.id()=="pointer")
  {
    if(type!=new_expr.type())
      return false;

    // add proper typecast
    typecast_exprt typecast_expr(expr, type);
    new_expr.swap(typecast_expr);
    return true;
  }

  return false;
}

/*******************************************************************\

Function: cpp_typecheckt::dynamic_typecast

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cpp_typecheckt::dynamic_typecast(
  const exprt &expr,
  const typet &type,
  exprt &new_expr)
{
  exprt e(expr);

  if(type.id()=="pointer")
  {
    if(e.id()=="dereference" && e.implicit())
      e = expr.op0();

    if(e.type().id()=="pointer" &&
       cast_away_constness(e.type(), type))
      return false;
  }
  add_implicit_dereference(e);

  if(is_reference(type))
  {
    exprt typeid_function = new_expr;
    exprt function = typeid_function;
    irep_idt badcast_identifier = "cpp::std::tag.bad_cast";

    // We must check if the user included typeinfo
    const symbolt *bad_cast_symbol;
    bool is_included = lookup(badcast_identifier, bad_cast_symbol);

    if (is_included)
      throw "Error: must #include <typeinfo>. Bad_cast throw";

    // Ok! Let's create the temp object badcast
    exprt badcast;
    badcast.identifier(badcast_identifier);
    badcast.operands().push_back(exprt("sideeffect"));
    badcast.op0().type() = typet("symbol");
    badcast.op0().type().identifier(badcast_identifier);

    // Check throw
    typecheck_expr_throw(badcast);

    // Save on the expression for handling on goto-program
    function.set("exception_list", badcast.find("exception_list"));
    e.make_typecast(type);
    new_expr.swap(e);
    new_expr.op0().operands().push_back(badcast);
    return true;

    if (follow(type.subtype()).id() != "struct")
    {
      return false;
    }
  }
  else if(type.id()=="pointer")
  {
    if(type.find("to-member").is_not_nil())
      return false;

    if(type.subtype().id()=="empty")
    {
      if(!e.cmt_lvalue())
        return false;
    }
    else if(follow(type.subtype()).id()=="struct")
    {
      if(e.cmt_lvalue())
      {
        exprt tmp(e);

        if(!standard_conversion_lvalue_to_rvalue(tmp,e))
          return false;
      }
    }
    else return false;
  }
  else
    return false;

  bool res = static_typecast(e,type, new_expr);

  if (res)
  {
    if (type.id() == "pointer" && e.type().id() == "pointer")
    {
      if (type.find("to-member").is_nil()
        && e.type().find("to-member").is_nil())
      {
        typet to = follow(type.subtype());
        symbolt t;
        if (e.identifier() != "")
        {
          t = lookup(e.identifier());
        }
        else if (e.op0().identifier() != "") // Array
        {
          t = lookup(e.op0().identifier());
        }
        else
          return false;

        typet from;
        if (t.type.id() == "array")
        {
          if (type.id() == new_expr.type().id())
          {
            from = follow(t.value.op0().type());
            e.make_typecast(type);
            new_expr.op0().op0().operands() = t.value.operands();
            return true;
          }
        }

        from = follow(t.value.type());

        // Could not dynamic_cast from void type
        if (t.type.subtype().id() == "empty")
        {
          return false;
        }

        // Are we doing a dynamic typecast between objects of the same class type?
        if ((type.id() == new_expr.type().id())
          && (type.subtype().id() == new_expr.type().subtype().id()))
        {
          return true;
        }

        if (from.id() == "empty")
        {
          e.make_typecast(type);
          new_expr.swap(e);
          return true;
        }

        if (to.id() == "struct" && from.id() == "struct")
        {
          if (e.cmt_lvalue())
          {
            exprt tmp(e);
            if (!standard_conversion_lvalue_to_rvalue(tmp, e))
              return false;
          }

          struct_typet from_struct = to_struct_type(from);
          struct_typet to_struct = to_struct_type(to);
          if (subtype_typecast(from_struct, to_struct))
          {
            make_ptr_typecast(e, type);
            new_expr.op0().swap(t.value);
            return true;
          }
        }

        // Cannot make typecast
        constant_exprt null_expr;
        null_expr.type() = new_expr.type();
        null_expr.set_value("NULL");

        new_expr.swap(null_expr);
        return true;
      }
      else if (type.find("to-member").is_not_nil()
        && e.type().find("to-member").is_not_nil())
      {
        if (type.subtype() != e.type().subtype())
          return false;

        struct_typet from_struct = to_struct_type(
          follow(static_cast<const typet&>(e.type().find("to-member"))));

        struct_typet to_struct = to_struct_type(
          follow(static_cast<const typet&>(type.find("to-member"))));

        if (subtype_typecast(from_struct, to_struct))
        {
          new_expr = e;
          new_expr.make_typecast(type);
          return true;
        }
      }
      else
        return false;
    }

    return false;
  } else {
    err_location(expr.location());
    str << "expr: " << e << std::endl;
    str << "totype: " << type << std::endl;
    throw "Could not cast types in dynamic cast";
  }

  return false;
}

/*******************************************************************\

Function: cpp_typecheckt::reinterpret_typecastcast

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cpp_typecheckt::reinterpret_typecast(
  const exprt &expr,
  const typet &type,
  exprt &new_expr,
  bool check_constantness)
{
  exprt e=expr;

  if(check_constantness && type.id()=="pointer")
  {
    if(e.id()=="dereference" && e.implicit())
      e = expr.op0();

    if(e.type().id()=="pointer" &&
       cast_away_constness(e.type(), type))
      return false;
  }

  add_implicit_dereference(e);

  if(!is_reference(type))
  {
    exprt tmp;

    if(e.id()=="code")
    {
      if(standard_conversion_function_to_pointer(e,tmp))
         e.swap(tmp);
      else
        return false;
    }

    if(e.type().id() == "array" ||
       e.type().id() == "incomplete_array" )
    {
      if(standard_conversion_array_to_pointer(e,tmp))
        e.swap(tmp);
      else
        return false;
    }

    if(e.cmt_lvalue())
    {
      if(standard_conversion_lvalue_to_rvalue(e,tmp))
        e.swap(tmp);
      else
        return false;
    }
  }

  if(e.type().id()=="pointer" &&
     (type.id()=="unsignedbv" || type.id() == "signedbv"))
  {
    // pointer to integer, always ok
    new_expr=e;
    new_expr.make_typecast(type);
    return true;
  }

  if((e.type().id()=="unsignedbv" ||
      e.type().id()=="signedbv" ||
      e.type().id()=="bool")
     && type.id()=="pointer"
     && !is_reference(type))
  {
    // integer to pointer
    if(e.is_zero())
    {
      // NULL
      new_expr = e;
      new_expr.value("NULL");
      new_expr.type() = type;
    }
    else
    {
      new_expr = e;
      new_expr.make_typecast(type);
    }
    return true;
  }

  if(e.type().id()=="pointer" &&
     type.id()=="pointer" &&
     !is_reference(type))
  {
    if(e.type().subtype().id()=="code"
       && type.subtype().id()!="code" )
      return false;
    else if (e.type().subtype().id()!="code"
       && type.subtype().id()=="code" )
      return false;

    // this is more generous than the standard
    new_expr = expr;
    new_expr.make_typecast(type);
    return true;
  }

  if(is_reference(type) && e.cmt_lvalue())
  {
    exprt tmp("address_of", pointer_typet());
    tmp.type().subtype() = e.type();
    tmp.copy_to_operands(e);
    tmp.make_typecast(type);
    new_expr.swap(tmp);
    return true;
  }

  return false;
}

/*******************************************************************\

Function: cpp_typecheckt::static_typecast

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool cpp_typecheckt::static_typecast(
  const exprt &expr,
  const typet &type,
  exprt &new_expr,
  bool check_constantness)
{
  exprt e=expr;

  if(check_constantness && type.id()=="pointer")
  {
    if(e.id()=="dereference" && e.implicit())
      e = expr.op0();

    if(e.type().id()=="pointer" &&
       cast_away_constness(e.type(), type))
      return false;
  }

  add_implicit_dereference(e);

  if(type.reference())
  {
    cpp_typecast_rank rank;
    if(reference_binding(e,type,new_expr,rank))
      return true;

    typet subto = follow(type.subtype());
    typet from = follow(e.type());

    if(subto.id()=="struct" && from.id()=="struct")
    {
      if(!expr.cmt_lvalue())
        return false;

      c_qualifierst qual_from;
      qual_from.read(e.type());

      c_qualifierst qual_to;
      qual_to.read(type.subtype());

      if(!qual_to.is_subset_of(qual_from))
        return false;

      struct_typet from_struct = to_struct_type(from);
      struct_typet subto_struct = to_struct_type(subto);

      if(subtype_typecast(subto_struct, from_struct))
      {
        if(e.id()=="dereference")
        {
          make_ptr_typecast(e.op0(),type);
          new_expr.swap(e.op0());
          return true;
        }

        exprt address_of("address_of", pointer_typet());
        address_of.type().subtype()=e.type();
        address_of.copy_to_operands(e);
        make_ptr_typecast(address_of ,type);
        new_expr.swap(address_of);
        return true;
      }
    }
    return false;
  }

  if(type.id()=="empty")
  {
    new_expr = e;
    new_expr.make_typecast(type);
    return true;
  }

  if(follow(type).id() == "c_enum"
    && (e.type().id() == "signedbv"
       || e.type().id() == "unsignedbv"
       || follow(e.type()).id() == "c_enum"))
  {
     new_expr = e;
     new_expr.make_typecast(type);
     if(new_expr.cmt_lvalue())
       new_expr.remove("#lvalue");
     return true;
  }

  if(implicit_conversion_sequence(e, type, new_expr))
  {
    if(!cpp_is_pod(type))
    {
      exprt tc("already_typechecked");
      tc.copy_to_operands(new_expr);
      exprt temporary;
      new_temporary(e.location(), type, tc, temporary);
      new_expr.swap(temporary);
    }
    else
    {
      // try to avoid temporary
      new_expr.set("#temporary_avoided",true);
      if(new_expr.cmt_lvalue())
        new_expr.remove("#lvalue");
    }

    return true;
  }

  if(type.id()=="pointer" && e.type().id()=="pointer")
  {
    if(type.find("to-member").is_nil()
       && e.type().find("to-member").is_nil())
    {
      typet to = follow(type.subtype());
      typet from = follow(e.type().subtype());

      if(from.id()=="empty")
      {
          e.make_typecast(type);
          new_expr.swap(e);
          return true;
      }

      if(to.id()=="struct" && from.id()=="struct")
      {

        if(e.cmt_lvalue())
        {
          exprt tmp(e);
          if(!standard_conversion_lvalue_to_rvalue(tmp,e))
            return false;
        }

        struct_typet from_struct = to_struct_type(from);
        struct_typet to_struct = to_struct_type(to);
        if(subtype_typecast(to_struct, from_struct))
        {
          make_ptr_typecast(e,type);
          new_expr.swap(e);
          return true;
        }
      }

      return false;
    }
    else if (type.find("to-member").is_not_nil()
       && e.type().find("to-member").is_not_nil())
    {
      if(type.subtype() != e.type().subtype())
        return false;

      struct_typet from_struct =
        to_struct_type(follow(static_cast<const typet&>(e.type().find("to-member"))));

      struct_typet to_struct =
        to_struct_type(follow(static_cast<const typet&>(type.find("to-member"))));

      if(subtype_typecast(from_struct, to_struct))
      {
        new_expr = e;
        new_expr.make_typecast(type);
        return true;
      }
    }
    else
      return false;
  }

  return false;
}
