/**
 * @file handle_declarators.cpp
 * @brief Source implementing the parser function to handle standard declarations.
 * 
 * This file's function will be referenced by every other function in the
 * parser. The efficiency of its implementation is of crucial importance.
 * If this file runs slow, so do the others in the parser.
 * 
 * @section License
 * 
 * Copyright (C) 2011-2012 Josh Ventura
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
#include <General/parse_basics.h>
#include <General/debug_macros.h>
#include <Parser/parse_context.h>
#include <System/builtins.h>
#include <API/compile_settings.h>
#include <API/AST.h>
#include <cstdio>
using namespace jdip;
using namespace jdi;

static unsigned anon_count = 0;
namespace jdip { definition *dangling_pointer = NULL; }
int jdip::context_parser::handle_declarators(definition_scope *scope, token_t& token, unsigned inherited_flags, definition* &res)
{
  // Skip destructor tildes; log if we are a destructor
  bool dtor = token.type == TT_TILDE;
  if (dtor) token = read_next_token(scope);
  
  // Outsource to read_fulltype, which will take care of the hard work for us.
  // When this function finishes, per its specification, our token will be set to the next relevant, non-referencer symbol.
  // This means an identifier if the syntax is correct.
  full_type tp = read_fulltype(lex, token, scope, this, herr);
  if (dtor) {
    if (tp.refs.name.empty() and tp.def == scope and !tp.flags and tp.refs.size() == 1 and tp.refs.top().type == ref_stack::RT_FUNCTION)
      tp.refs.name = "<destruct>";
    else {
      token.report_error(herr, "Junk destructor; remove tilde?");
      FATAL_RETURN(1);
    }
  }
  
  // Make sure we actually read a valid type.
  if (!tp.def) {
    token.report_error(herr, "Declaration does not give a valid type");
    return 1;
  }
  
  return handle_declarators(scope, token, tp, inherited_flags, res);
}

int jdip::context_parser::handle_declarators(definition_scope *scope, token_t& token, full_type &tp, unsigned inherited_flags, definition* &res)
{
  // Make sure we do indeed find ourselves at an identifier to declare.
  if (tp.refs.name.empty()) {
    const bool potentialc = tp.def == scope or (scope->flags & DEF_TEMPSCOPE and tp.def == scope->parent);
    // Handle constructors; this might need moved to a handle_constructors method.
    if (potentialc and !tp.flags and tp.refs.size() == 1 and tp.refs.top().type == ref_stack::RT_FUNCTION) {
      tp.refs.name = "<construct>";
      if (token.type == TT_COLON) {
        // TODO: When you have a place to store constructor data, 
        do {
          token = read_next_token(scope);
          if (token.type == TT_SEMICOLON) {
            token.report_error(herr, "Expected constructor body here after initializers.");
            FATAL_RETURN(1);
          }
        } while (token.type != TT_LEFTBRACE);
      }
    }
    else if (token.type == TT_COLON) {
      if (scope->flags & DEF_CLASS) {
        char anonname[32];
        sprintf(anonname,"<anonymousField%010d>",anon_count);
        tp.refs.name = anonname;
      }
      else
        token.report_warning(herr, "Declaration without name is meaningless outside of a class");
    }
    else if (token.type == TT_DEFINITION or token.type == TT_DECLARATOR) {
      definition *d = token.def;
      token = read_next_token(scope);
      rescope: {
        while (token.type == TT_SCOPE) {
          if (!(d->flags & DEF_SCOPE)) {
            token.report_error(herr, "Cannot access `" + d->name + "' as scope");
            FATAL_RETURN(1); break;
          }
          token = read_next_token((definition_scope*)d);
          if (token.type != TT_DEFINITION and token.type != TT_DECLARATOR) {
            if (token.type == TT_IDENTIFIER)
              token.report_errorf(herr, "Expected qualified-id before %s; `" + token.content.toString() + "' is not a member of `" + d->name + "'");
            else
              token.report_errorf(herr, "Expected qualified-id before %s");
            FATAL_RETURN(1); break;
          }
          d = token.def;
          token = read_next_token(scope);
        }
        if (token.type == TT_LESSTHAN and d->flags & DEF_TEMPLATE) {
          definition_template* temp = (definition_template*)d;
          arg_key k(temp->params.size());
          if (read_template_parameters(k, temp, lex, token, scope, this, herr))
            return 1;
          d = temp->instantiate(k);
          if (!d) return 1;
          token = read_next_token(scope);
          goto rescope;
        }
      }
      read_referencers_post(tp.refs, lex, token, scope, this, herr);
      res = d; goto extra_loop;
    }
    else
      return 0;
  }
  
  {
    // Add it to our definitions map, without overwriting the existing member.
    definition_scope::inspair ins = ((definition_scope*)scope)->members.insert(definition_scope::entry(tp.refs.name,NULL));
    if (ins.second) { // If we successfully inserted,
      insert_anyway:
      res = ins.first->second = (!tp.refs.empty() && tp.refs.top().type == ref_stack::RT_FUNCTION)?
        new definition_function(tp.refs.name,scope,tp.def,tp.refs,tp.flags,DEF_TYPED | inherited_flags):
        new definition_typed(tp.refs.name,scope,tp.def,tp.refs,tp.flags,DEF_TYPED | inherited_flags);
    }
    else // Well, uh-oh. We didn't insert anything. This is non-fatal, and will not leak, so no harm done.
    {
      if (ins.first->second->flags & (DEF_CLASS | DEF_UNION | DEF_ENUM)) {
        pair<map<string,definition*>::iterator,bool> cins
          = c_structs.insert(pair<string,definition*>(ins.first->first,ins.first->second));
        if (!cins.second and cins.first->second != ins.first->second) {
          token.report_error(herr, "Attempt to redeclare `" + tp.refs.name + "' failed due to conflicts");
          FATAL_RETURN(1);
        }
        else goto insert_anyway;
      }
      if (not(ins.first->second->flags & DEF_TYPED)) {
        token.report_error(herr, "Redeclaration of `" + tp.refs.name + "' as a different kind of symbol");
        return 3;
      }
      if (ins.first->second->flags & DEF_FUNCTION) { // Handle function overloading
        definition_function* func = (definition_function*)ins.first->second;
        arg_key k(func->referencers);
        res = func->overload(k, new definition_function(tp.refs.name,scope,tp.def,tp.refs,tp.flags,DEF_TYPED | inherited_flags), herr);
      }
      else
        res = ins.first->second;
    }
  }
  
  extra_loop:
  for (;;)
  {
    switch (token.type) {
      case TT_OPERATOR:
          if (token.content.len != 1 or *token.content.str != '=') { // If this operator isn't =, this is a fatal error. No idea where we are.
            case TT_GREATERTHAN: case TT_LESSTHAN:
            token.report_error(herr, "Unexpected operator `" + token.content.toString() + "' at this point");
            return 5;
          }
          else {
            AST ast;
            token = read_next_token(scope);
            ast.parse_expression(token, lex, scope, precedence::comma, herr);
            // TODO: Store AST
          }
        break;
      case TT_COMMA:
          // Move past this comma
          token = read_next_token(scope);
          
          // Read a new type
          read_referencers(tp.refs, tp, lex, token, scope, this, herr);
          
          // Just hop into the error checking above and pass through the definition addition again.
        return handle_declarators(scope, token, tp, inherited_flags, res);
        
      case TT_COLON: {
          if (tp.def != builtin_type__int) {
            token.report_error(herr,"Attempt to assign bit count in non-integer declaration");
            FATAL_RETURN(1);
          }
          AST bitcountexp;
          bitcountexp.parse_expression(token = read_next_token(scope), lex, scope, precedence::comma+1, herr);
          value bc = bitcountexp.eval();
          if (bc.type != VT_INTEGER) {
            token.report_error(herr,"Bit count is not an integer");
            FATAL_RETURN(1);
          }
          // TODO: Store the bit count somewhere
        } break;
      
      case TT_STRINGLITERAL: case TT_CHARLITERAL: case TT_DECLITERAL: case TT_HEXLITERAL: case TT_OCTLITERAL:
          token.report_error(herr, "Expected initializer `=' here before literal.");
        return 5;
      
      case TT_ELLIPSIS:
      case TT_SEMICOLON:
      
      case TT_DECLARATOR: case TT_DECFLAG: case TT_CLASS: case TT_STRUCT: case TT_ENUM: case TT_UNION: case TT_NAMESPACE: case TT_EXTERN: case TT_IDENTIFIER:
      case TT_DEFINITION: case TT_TEMPLATE: case TT_TYPENAME: case TT_TYPEDEF: case TT_USING: case TT_PUBLIC: case TT_PRIVATE: case TT_PROTECTED:
      case TT_SCOPE: case TT_LEFTPARENTH: case TT_RIGHTPARENTH: case TT_LEFTBRACKET: case TT_RIGHTBRACKET: case TT_LEFTBRACE: case TT_RIGHTBRACE:
      case TT_ASM: case TT_TILDE: case TTM_CONCAT: case TTM_TOSTRING: case TT_ENDOFCODE: case TT_SIZEOF: case TT_ISEMPTY: case TT_OPERATORKW: case TT_DECLTYPE: case TT_INVALID: default:
        return 0;
    }
  }
  
  return 0;
}

