/**
 * @file read_type.cpp
 * @brief Source implementing a utility funciton to read in a type.
 * 
 * @section License
 * 
 * Copyright (C) 2011 Josh Ventura
 * This file is part of JustDefineIt.
 * 
 * JustDefineIt is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3 of the License, or (at your option) any later version.
 * 
 * JustDefineIt is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * JustDefineIt. If not, see <http://www.gnu.org/licenses/>.
**/

#include <Parser/bodies.h>
#include <API/context.h>
#include <API/compile_settings.h>
#include <API/AST.h>
#include <API/error_reporting.h>
#include <General/parse_basics.h>
#include <General/debug_macros.h>
#include <Parser/parse_context.h>
#include <System/builtins.h>
#include <cstdio>
using namespace jdip;
using namespace jdi;

full_type jdip::read_type(lexer *lex, token_t &token, definition_scope *scope, context_parser *cp, error_handler *herr)
{
  definition* inferred_type = NULL; // This is the type we will use if absolutely no other type is given
  definition* overridable_type = NULL;
  long int rflags = 0; // Return flags.
  long int swif = 0; // Swap-in flags: or'd in when a flag is determined not to be the main type.
  definition *rdef = NULL;
  ref_stack rrefs;
  
  if (token.type != TT_DECLARATOR) {
    if (token.type != TT_DECFLAG) {
      if (token.type == TT_CLASS or token.type == TT_STRUCT or token.type == TT_ENUM or token.type == TT_UNION) {
        if (cp)
          rdef = token.type == TT_ENUM? (definition*)cp->handle_enum(scope,token,0): 
            token.type == TT_UNION? (definition*)cp->handle_union(scope,token,0) : (definition*)cp->handle_class(scope,token,0);
        else {
          token = lex->get_token_in_scope(scope, herr);
          if (token.type != TT_DECLARATOR)
            token.report_error(herr, "Existing class name must follow class/struct token at this point");
        }
      }
      else if (token.type == TT_ELLIPSIS) {
        rdef = builtin_type__va_list;
        token = lex->get_token_in_scope(scope,herr);
      }
      else {
        if (token.type == TT_IDENTIFIER or token.type == TT_DEFINITION)
          token.report_error(herr,"Type name expected here; `" + string(token.content.toString()) + "' does not name a type");
        else
          token.report_errorf(herr,"Type name expected here before %s");
        return full_type();
      }
    }
    else {
      typeflag *const tf = ((typeflag*)token.def);
      if (tf->usage & UF_PRIMITIVE) {
        (tf->usage == UF_PRIMITIVE? rdef : overridable_type) = tf->def,
        swif = tf->flagbit;
      }
      else {
        if (tf->usage & UF_STANDALONE)
          inferred_type = tf->def;
        rflags = tf->flagbit;
      }
      token = lex->get_token_in_scope(scope,herr);
    }
  }
  else {
    rdef = token.def;
    token = lex->get_token_in_scope(scope,herr);
  }
  
  // Read any additional type info
  typeloop: while (token.type == TT_DECLARATOR or token.type == TT_DECFLAG)
  {
    if (token.type == TT_DECLARATOR) {
      if (rdef) {
        if (token.def->flags & (DEF_CLASS | DEF_ENUM | DEF_UNION | DEF_TYPED))
          break;
        token.report_error(herr,"Two types named in expression");
        FATAL_RETURN(1);
      }
      rdef = token.def;
      rflags |= swif;
    }
    else {
      typeflag *const tf = ((typeflag*)token.def);
      if (tf->usage & UF_PRIMITIVE) {
        if (tf->usage == UF_PRIMITIVE) {
          if (rdef)
            token.report_error(herr,"Two types named in expression");
          rdef = tf->def;
          rflags |= swif;
        } else {
          overridable_type = tf->def,
          rflags |= swif,
          swif = tf->flagbit;
        }
      }
      else if (tf->usage & UF_STANDALONE_FLAG)
        inferred_type = tf->def,
        rflags |= tf->flagbit;
    }
    token = lex->get_token_in_scope(scope, herr);
  }
  if (rdef == NULL and (token.type == TT_CLASS or token.type == TT_STRUCT or token.type == TT_UNION or token.type == TT_ENUM))
  {
    if (cp)
      // FIXME: I don't have any inherited flags to pass here. Is that OK?
      rdef = (token.type == TT_UNION? (definition*)cp->handle_union(scope,token,0) :
              token.type == TT_ENUM?  (definition*)cp->handle_enum(scope,token,0)  :  (definition*)cp->handle_class(scope,token,0));
    else {
      token = lex->get_token_in_scope(scope, herr);
      goto typeloop;
    }
  }
  
  if (rdef == NULL)
    rdef = overridable_type;
  if (rdef == NULL)
    rdef = inferred_type;
  if (rdef)
    jdip::read_referencers(rrefs, lex, token, scope, cp, herr);
  return full_type(rdef, rrefs, rflags);
}

#include <General/debug_macros.h>
int jdip::read_referencers(ref_stack &refs, lexer *lex, token_t &token, definition_scope *scope, context_parser *cp, error_handler *herr)
{
  ref_stack append;
  ref_stack postfix;
  bool rhs = false;
  for (;;)
  {
    switch (token.type)
    {
      case TT_LEFTBRACKET: { // Array bound indicator
        AST ast;
        rhs = true;
        token = lex->get_token_in_scope(scope, herr);
        if (token.type != TT_RIGHTBRACKET) {
          if (ast.parse_expression(token,lex,herr))
            return 1; // This error has already been reported, just return empty.
          if (token.type != TT_RIGHTBRACKET) {
            token.report_errorf(herr,"Expected closing square bracket here before %s");
            return 1;
          }
          render_ast(ast, "ArrayBounds");
          value as = ast.eval();
          size_t boundsize = (as.type == VT_INTEGER)? as.val.i : ref_stack::node_array::nbound;
          postfix.push_array(boundsize);
        }
        else
          postfix.push_array(ref_stack::node_array::nbound);
      } break;
      
      case TT_LEFTPARENTH: { // Either a function or a grouping
        token = lex->get_token_in_scope(scope, herr);
        if (!rhs) // If we're still on the left-hand side
        {
          rhs = true;
          if (token.type == TT_DECLARATOR || token.type == TT_DECFLAG || token.type == TT_RIGHTPARENTH || token.type == TT_DECLTYPE)
            goto handle_params;
          read_referencers(append, lex, token, scope, cp, herr);
          if (token.type != TT_RIGHTPARENTH) {
            token.report_errorf(herr, "Expected right parenthesis before %s to close nested referencers");
            FATAL_RETURN(1);
          }
        }
        else // Otherwise, we're on the right-hand side of the identifier, and we assume we are at function parameters.
        {
          handle_params:
          ref_stack::parameter_ct params;
          
          // Navigate to the end of the function parametr list
          while (token.type != TT_RIGHTPARENTH)
          {
            full_type a = read_type(lex,token,scope,cp,herr);
            ref_stack::parameter param; param.swap_in(a);
            param.variadic = cp? cp->variadics.find(param.def) != cp->variadics.end() : false;
            params.throw_on(param);
            if (token.type != TT_COMMA) {
              if (token.type == TT_RIGHTPARENTH) break;
              token.report_error(herr,"Expected comma or closing parenthesis to function parameters");
              FATAL_RETURN(1);
            }
            token = lex->get_token_in_scope(scope, herr);
          }
          
          // Push our function information onto the reference stack
          postfix.push_func(params);
          if (token.type != TT_RIGHTPARENTH) {
            token.report_error(herr,"Expected closing parenthesis to function parameters");
            FATAL_RETURN(1);
          }
          
          // If there's no other special garbage being tacked onto this, then we are not a pointer-to function,
          // and we are not an array of functions, and we aren't a function returning a function.
          // Ergo, the function can be implemented here. FIXME: Make sure parser allows implementing function returning function pointer.
          if (append.empty())
          {
            token = lex->get_token_in_scope(scope, herr); // Read in our next token to see if it's a brace or extra info
            while (token.type == TT_DECFLAG) { // It is legal to put the flags throw and const here.
              token = lex->get_token_in_scope(scope, herr);
            }
            continue;
          }
        }
      } break;
      
      case TT_IDENTIFIER: case TT_DEFINITION: // The name associated with this type, be it the name of a parameter or of a declaration or what have you.
        refs.name = string(token.content.toString());
        rhs = true; // Officially on the right hand side
      break;
      
      case TT_OPERATORKW:
          token = lex->get_token_in_scope(scope, herr);
          if (token.type == TT_OPERATOR) {
            refs.name = "operator" + string(token.content.toString());
          }
          else if (token.type == TT_LEFTBRACKET) {
            refs.name = "operator[]";
            token = lex->get_token_in_scope(scope, herr);
            if (token.type != TT_RIGHTBRACKET) {
              token.report_error(herr, "Expected closing bracket for `operator[]' definition");
              FATAL_RETURN(1);
            }
          }
          else if (token.type == TT_LEFTPARENTH) {
            refs.name = "operator()";
            token = lex->get_token_in_scope(scope, herr);
            if (token.type != TT_RIGHTPARENTH) {
              token.report_error(herr, "Expected closing parenthesis for `operator()' definition");
              FATAL_RETURN(1);
            }
          }
          else {
            token.report_errorf(herr, "Unexpected %s following `operator' keyword; does not form a valid operator");
            FATAL_RETURN(1);
          }
          rhs = true;
        break;
      
      
      case TT_OPERATOR: // Could be an asterisk or ampersand
        if ((token.content.str[0] == '&' or token.content.str[0] == '*') and token.content.len == 1) {
          refs.push(token.content.str[0] == '&'? ref_stack::RT_REFERENCE : ref_stack::RT_POINTERTO);
          break;
        } goto default_; // Else overflow
      
      case TT_DECFLAG: {
          typeflag* a = ((typeflag*)token.def);
          if (a->flagbit == builtin_flag__const || a->flagbit == builtin_flag__volatile) {
            // TODO: Give RT_POINTERTO node a bool/volatile flag; to denote that the pointer is const or volatile; set it here.
            token = lex->get_token_in_scope(scope, herr);
            continue;
          }
        } goto default_;
      
      case TT_ELLIPSIS:
          token.report_error(herr, "`...' not allowed as general modifier");
      
      case TT_DECLARATOR:
        if (!rhs and refs.name.empty() and (token.def->flags & (DEF_CLASS | DEF_UNION | DEF_ENUM | DEF_TYPED))) { //
          refs.name = token.def->name;
          rhs = true;
          break;
        }
      
      case TT_CLASS: case TT_STRUCT: case TT_ENUM: case TT_EXTERN: case TT_UNION: 
      case TT_NAMESPACE: case TT_TEMPLATE: case TT_TYPENAME: case TT_TYPEDEF: case TT_USING: case TT_PUBLIC:
      case TT_PRIVATE: case TT_PROTECTED: case TT_COLON: case TT_SCOPE: case TT_RIGHTPARENTH: case TT_RIGHTBRACKET:
      case TT_LEFTBRACE: case TT_RIGHTBRACE: case TT_LESSTHAN:case TT_GREATERTHAN: case TT_TILDE: case TT_ASM: case TT_SIZEOF: case TT_DECLTYPE:
      case TT_COMMA: case TT_SEMICOLON: case TT_STRINGLITERAL: case TT_CHARLITERAL: case TT_DECLITERAL: case TT_HEXLITERAL:
      case TT_OCTLITERAL: case TT_ENDOFCODE: case TTM_CONCAT: case TTM_TOSTRING: case TT_INVALID: default: default_:
          refs.append(postfix);
          refs.append_nest(append);
        return 0;
    }
    token = lex->get_token_in_scope(scope, herr);
  }
}
