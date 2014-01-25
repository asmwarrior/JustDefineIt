/**
 * @file handle_hypothetical.cpp
 * @brief Source implementing context_parser::handle_hypothetical.
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
#include <API/compile_settings.h>

namespace jdip {
  inline definition_hypothetical* handle_hypothetical_ast(AST *a, definition_scope *scope, token_t& token, unsigned flags, error_handler *herr) {
    definition_scope *temps;
    for (temps = scope; temps && !(temps->flags & DEF_TEMPLATE); temps = temps->parent);
    if (!temps || !(temps->flags & DEF_TEMPLATE)) {
      token.report_error(herr, "Cannot infer type outside of template");
      delete a;
      return NULL;
    }
    definition_template *temp = (definition_template*)temps;
    
    definition_hypothetical* h = new definition_hypothetical("(?=" + a->toString() + ")", scope, flags, a);
    temp->dependents.push_back(h);
    scope->dec_order.push_back(new definition_template::dec_order_hypothetical(h));
    return h;
  }
  
  definition* handle_dependent_tempinst(definition_scope *scope, token_t& token, definition_template *temp, const arg_key &key, unsigned flags, error_handler *herr) {
    if (scope->flags & DEF_TEMPLATE) {
      definition_template::specialization *spec = temp->find_specialization(key);
      if (spec && spec->spec_temp && spec->spec_temp->def) return spec->spec_temp->def;
      return temp->def;
    }
    AST *a = AST::create_from_instantiation(temp, key);
    if (temp->def && (temp->def->flags & (DEF_CLASS | DEF_TYPENAME)))
      flags |= DEF_TYPENAME;
    return handle_hypothetical_ast(a, scope, token, flags, herr);
  }
  
  definition_hypothetical* handle_hypothetical_access(definition_hypothetical *scope, string id) {
    AST *a = AST::create_from_access(scope, id, "::");
    token_t dummy_token;
    return handle_hypothetical_ast(a, scope->parent, dummy_token, scope->flags, def_error_handler); // XXX: scope->flags, that & DEF_PRIVATE/whaever, or 0?
  }
  
  definition_hypothetical* handle_hypothetical(lexer *lex, definition_scope *scope, token_t& token, unsigned flags, error_handler *herr) {
    AST *a = new AST();
    if (a->parse_expression(token, lex, scope, precedence::scope, herr))
      { FATAL_RETURN(1); }
    
    return handle_hypothetical_ast(a, scope, token, flags, herr);
  }
}
