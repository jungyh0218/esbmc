/*******************************************************************\

Module: Program Transformation

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <rename.h>

#include "goto_convert_class.h"

void goto_convertt::new_name(symbolt &symbol)
{
  // rename it
  get_new_name(symbol, ns);

  // store in context
  context.add(symbol);
}

const symbolt &goto_convertt::lookup(const irep_idt &identifier) const
{
  const symbolt *symbol;
  if(ns.lookup(identifier, symbol))
    throw "failed to find symbol "+id2string(identifier);
  return *symbol;
}

void goto_convert(
  const codet &code,
  contextt &context,
  optionst &options,
  goto_programt &dest,
  message_handlert &message_handler)
{
  goto_convertt goto_convert(context, options, message_handler);

  try
  {
    goto_convert.goto_convert(code, dest);
  }

  catch(int)
  {
    goto_convert.error();
  }

  catch(const char *e)
  {
    goto_convert.error(e);
  }

  catch(const std::string &e)
  {
    goto_convert.error(e);
  }

  if(goto_convert.get_error_found())
    throw 0;
}

void goto_convert(
  contextt &context,
  optionst &options,
  goto_programt &dest,
  message_handlert &message_handler)
{
  // find main symbol
  const symbolt* s = context.find_symbol("main");
  if(s == nullptr)
    throw "failed to find main symbol";

  std::cout << "goto_convert : start converting symbol table to goto functions " << std::endl;
  ::goto_convert(to_code(s->value), context, options, dest, message_handler);
}
