/**
 * @file lex_cpp.cpp
 * @brief Source implementing the C++ \c lexer class extensions.
 * 
 * This file's function will be referenced, directly or otherwise, by every
 * other function in the parser. The efficiency of its implementation is of
 * crucial importance. If this file runs slow, so do the others.
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

#include "lex_cpp.h"
#include <General/debug_macros.h>
#include <General/parse_basics.h>
#include <General/debug_macros.h>
#include <Parser/parse_context.h>
#include <System/builtins.h>
#include <API/context.h>
#include <API/AST.h>

using namespace jdi;
using namespace jdip;

#define cfile data //I'm sorry, but I can't spend the whole function calling the file buffer "data."

/// Returns whether s1 begins with s2, followed by whitespace.
static inline bool strbw(const char* s1, const char (&s2)[3]) { return *s1 == *s2 and s1[1] == s2[1] and (s1[2] == ' ' or s1[2] == '\t' or s1[2] == '\n' or s1[2] == '\r'); }
static inline bool strbw(const char* s1, const char (&s2)[4]) { return *s1 == *s2 and s1[1] == s2[1] and s1[2] == s2[2] and (s1[3] == ' ' or s1[3] == '\t' or s1[3] == '\n' or s1[3] == '\r'); }
static inline bool strbw(const char* s1, const char (&s2)[5]) { return *s1 == *s2 and s1[1] == s2[1] and s1[2] == s2[2] and s1[3] == s2[3] and (s1[4] == ' ' or s1[4] == '\t' or s1[4] == '\n' or s1[4] == '\r'); }
static inline bool strbw(const char* s1, const char (&s2)[6]) { return *s1 == *s2 and s1[1] == s2[1] and s1[2] == s2[2] and s1[3] == s2[3] and s1[4] == s2[4] and (s1[5] == ' ' or s1[5] == '\t' or s1[5] == '\n' or s1[5] == '\r'); }
static inline bool strbw(const char* s1, const char (&s2)[7]) { return *s1 == *s2 and s1[1] == s2[1] and s1[2] == s2[2] and s1[3] == s2[3] and s1[4] == s2[4] and s1[5] == s2[5] and (s1[6] == ' ' or s1[6] == '\t' or s1[6] == '\n' or s1[6] == '\r'); }
static inline bool strbw(char s) { return s == ' ' or s == '\t' or s == '\n' or s == '\r'; }

void lexer_cpp::skip_comment()
{
  #ifdef ALLOW_MULTILINE_COMMENT
  while (++pos < length and cfile[pos] != '\n' and cfile[pos] != '\r') if (cfile[pos] == '\\') {
    if (cfile[++pos] == '\n') ++line, lpos = pos;
    else if (cfile[pos] == '\r') pos += cfile[pos+1] == '\n', ++line, lpos = pos;
  }
  #else
  while (++pos < length and cfile[pos] != '\n' and cfile[pos] != '\r');
  #endif
}

/**
  @section implementation
  Outsources to two other skip functions in a loop.
**/
void lexer_cpp::skip_to_macro(error_handler *herr)
{
  while (pos < length) {
    while (pos < length and cfile[pos] != '\n' and cfile[pos] != '\r') {
      if (cfile[pos] == '/') {
        if (cfile[pos+1] == '*') {
          pos += 3; while (pos < length and (cfile[pos] != '/' or cfile[pos-1] != '*')) ++pos;
          ++pos; continue;
        }
        if (cfile[pos+1] == '/') {
          skip_comment();
          continue;
        }
      }
      if (cfile[pos] == '"' or cfile[pos] == '\'')
      {
        register const char endc = cfile[pos];
        while (++pos < length and cfile[pos] != endc)
        {
          if (cfile[pos] == '\\') {
            if (cfile[++pos] == '\n') ++line, lpos = pos;
            else if (cfile[pos] == '\r') {
              if (cfile[++pos] != '\n') --pos;
              ++line, lpos = pos; 
            }
          }
          else if (cfile[pos] == '\n' or cfile[pos] == '\r') {
            herr->error("Unterminated string literal");
            break;
          }
        }
      }
      ++pos;
    }
    if (pos >= length)
      break;
    while (is_useless(cfile[pos])) {
      if (cfile[pos] == '\n') lpos = pos, ++line;
      else if (cfile[pos] == '\r' and (cfile[++pos] == '\n' or --pos)) lpos = pos++, ++line;
      ++pos;
    }
    if (cfile[pos] == '#')
      return ++pos, handle_preprocessor(herr);
  }
  herr->error("Expected closing preprocessors before end of code",filename,line,pos-lpos);
}

/**
  @section Implementation
  
  For the preprocessor's implementation, we switch gears a bit. The preprocessor needs
  to be the fastest aspect of the entire parser, as directives are everywhere. As such,
  it operates on a hacked-up switch statement using a whole heap of gotos, designed to
  support a specific set of macros. Inserting a new directive into this switch is still
  simple enough, but unlike other aspects of the parser, is not as trivial as a single
  function call. Don't gripe; originally I was going to use a perfect hash.
**/
void lexer_cpp::handle_preprocessor(error_handler *herr)
{
  while (cfile[pos] == ' ' or cfile[pos] == '\t') ++pos;
  const size_t pspos = pos;
  switch (cfile[pos++])
  {
    case 'd':
      if (strbw(cfile+pos, "efine")) { pos += 5; goto case_define; }
      goto failout;
	  case 'e':
      if (cfile[pos] == 'n') { if (strbw(cfile+pos+1, "dif")) { pos += 4; goto case_endif; } goto failout; }
      if (cfile[pos] == 'l')
      { 
        if (cfile[++pos] == 's') { if (cfile[++pos] == 'e') { ++pos; goto case_else; } goto failout; }
        if (cfile[pos] == 'i' and cfile[++pos] == 'f')
        {
          if (strbw(cfile[++pos])) goto case_elif;
          if (cfile[pos] == 'd') { if (strbw(cfile+pos+1, "ef"))  { pos += 3; goto case_elifdef;  } goto failout; }
          if (cfile[pos] == 'n') { if (strbw(cfile+pos+1, "def")) { pos += 4; goto case_elifndef; } goto failout; }
        }
        goto failout;
      }
      if (strbw(cfile+pos, "rror")) { pos += 4; goto case_error; }
      goto failout;
	  case 'i':
      if (cfile[pos] == 'f')
      {
        if (strbw(cfile[++pos])) goto case_if;
        if (cfile[pos] == 'd') { if (strbw(cfile+pos+1, "ef"))  { pos += 3; goto case_ifdef;  } goto failout; }
        if (cfile[pos] == 'n') { if (strbw(cfile+pos+1, "def")) { pos += 4; goto case_ifndef; } goto failout; }
        goto failout;
      }
      if (cfile[pos] == 'n') { if (strbw(cfile+pos+1, "clude")) { pos += 6; goto case_include; } goto failout; }
      if (cfile[pos] == 'm') { if (strbw(cfile+pos+1, "port"))  { pos += 5; goto case_import;  } goto failout; }
      goto failout;
	  case 'l':
      if (strbw(cfile+pos, "ine")) { pos += 3; goto case_line; }
      goto failout;
	  case 'p':
      if (strbw(cfile+pos, "ragma")) { pos += 5; goto case_pragma; }
      goto failout;
	  case 'u':
      if (strbw(cfile+pos, "ndef")) { pos += 4; goto case_undef; }
      if (strbw(cfile+pos, "sing")) { pos += 4; goto case_using; }
      goto failout;
    case 'w':
      if (strbw(cfile+pos, "arning")) { pos += 6; goto case_warning; }
      goto failout;
    default: goto failout;
  }
  
  for (;;)
  {
    break;
    case_define:
      break;
    case_error: {
          while (pos < length and (cfile[pos] == ' ' or cfile[pos] == '\t')) ++pos;
          const size_t espos = pos; while (pos < length and cfile[pos] != '\n' and cfile[pos] != '\r') ++pos;
          if (conditionals.empty() or conditionals.top().is_true)
            herr->error(token_basics("#error " + string(cfile + espos, pos - espos),filename,line,pos-lpos));
        }
      break;
    case_elif:
        if (conditionals.empty())
          herr->error(token_basics("Unexpected #elif directive; no matching #if",filename,line,pos-lpos));
        else {
          if (conditionals.top().is_true)
            conditionals.top().is_true = conditionals.top().can_be_true = false;
          else {
            if (conditionals.top().can_be_true) {
              conditionals.pop();
              goto case_if;
            }
          }
        }
      break;
    case_elifdef:
      break;
    case_elifndef:
      break;
    case_else:
        if (conditionals.empty())
          herr->error(token_basics("Unexpected #else directive; no matching #if",filename,line,pos-lpos));
        else {
          if (conditionals.top().is_true)
            conditionals.top().is_true = conditionals.top().can_be_true = false;
          else
            conditionals.top().is_true = conditionals.top().can_be_true;
        }
      break;
    case_endif:
        if (conditionals.empty())
          return herr->error(token_basics("Unexpected #endif directive: no open conditionals.",filename,line,pos-lpos));
        conditionals.pop();
      break;
    case_if: 
        if (conditionals.empty() or conditionals.top().is_true) {
          AST a;
          mlex->update();
          if (a.parse_expression(mlex, herr) or !a.eval()) {
            token_t res;
            conditionals.push(condition(0,1));
            break;
          }
          conditionals.push(condition(1,0));
        }
        else
          conditionals.push(condition(0,0));
      break;
    case_ifdef:
      break;
    case_ifndef:
      break;
    case_import:
      break;
    case_include:
      break;
    case_line:
      break;
    case_pragma:
      break;
    case_undef:
      break;
    case_using:
      break;
    case_warning: {
          while (pos < length and (cfile[pos] == ' ' or cfile[pos] == '\t')) ++pos;
          const size_t espos = pos; while (pos < length and cfile[pos] != '\n' and cfile[pos] != '\r') ++pos;
          if (conditionals.empty() or conditionals.top().is_true)
            herr->warning(token_basics("#warning " + string(cfile + espos, pos - espos),filename,line,pos-lpos));
        }
      break;
  }
  if (conditionals.empty() or conditionals.top().is_true)
    return;
  return skip_to_macro(herr);
  
  failout:
    while (is_letterd(cfile[pos])) ++pos;
    string ppname(cfile + pspos, pos - pspos);
    herr->error(token_basics("Invalid preprocessor directive `" + ppname + "'",filename,line,pos-lpos));
    while (pos < length and cfile[pos] != '\n' and cfile[pos] != '\r') ++pos;
}

token_t lexer_cpp::get_token(error_handler *herr)
{
  if (pos >= length) goto POP_FILE;
  for (;;) // Loop until we find something or hit world's end
  {
    // Skip all whitespace
    while (is_useless(cfile[pos])) {
      if (cfile[pos] == '\n') ++line, lpos = pos;
      if (cfile[pos] == '\r') { ++line; lpos = pos; if (cfile[++pos] != '\n') --pos; }
      if (++pos >= length) goto POP_FILE;
    }
    
    //============================================================================================
    //====: Check for and handle comments. :======================================================
    //============================================================================================
    
    if (cfile[pos] == '/') {
      if (++pos < length) {
        if (cfile[pos] == '/') {
          // Find the end of the line
          while (cfile[pos] != '\n') if (++pos >= length) goto POP_FILE;
          continue;
        }
        if (cfile[pos] == '*') {
          ++pos; // Skip one more char so we don't break on /*/
          do if (pos >= length) goto POP_FILE; 
            else if (cfile[pos] == '\n' or (cfile[pos] == '\r' and cfile[pos+1] != '\n')) ++line, lpos = pos;
            while (cfile[pos++] != '*' or cfile[pos] != '/');
          ++pos; continue;
        }
        if (cfile[pos] == '=')
          return token_t(token_basics(TT_OPERATOR,filename,line,pos-lpos), cfile+pos-1, 2);
      }
      return token_t(token_basics(TT_OPERATOR,filename,line,pos-lpos), cfile+pos-1,1);
    }
    
    //============================================================================================
    //====: Not at a comment. See if we're at an identifier. :====================================
    //============================================================================================
    
    if (is_letter(cfile[pos])) // Check if we're at an identifier or keyword.
    {
      const size_t spos = pos; // Record where we are
      while (++pos < length and is_letterd(cfile[pos]));
      
      string fn(cfile + spos, cfile + pos); // We'll need a copy of this thing for lookup purposes
      
      macro_iter mi = macros.find(fn);
      if (mi != macros.end()) {
        openfile of(filename, line, lpos, *this);
        files.enswap(of);
        filename = fn.c_str();
        if (mi->second->argc < 0) {
          macro_scalar *ms = (macro_scalar*)mi->second;
          this->encapsulate(ms->value);
          filename = mi->first.c_str();
          line = lpos = pos = 0;
          return get_token(herr);
        }
        else {
          cout << "UNSUPPORTED: MACRO FUNCTION" << endl;
        }
      }
      
      keyword_map::iterator kwit = keywords.find(fn);
      if (kwit != keywords.end())
        return token_t(token_basics(kwit->second,filename,line,spos-lpos));
      
      tf_iter tfit = builtin_declarators.find(fn);
      if (tfit != builtin_declarators.end()) {
        if ((tfit->second->usage & UF_STANDALONE_FLAG) == UF_PRIMITIVE)
          return token_t(token_basics(TT_DECLARATOR,filename,line,spos-lpos), tfit->second->def);
        return token_t(token_basics(TT_DECFLAG,filename,line,spos-lpos), (definition*)tfit->second);
      }
      
      return token_t(token_basics(TT_IDENTIFIER,filename,line,spos-lpos), cfile + spos, fn.length());
    }
    
    //============================================================================================
    //====: Not at an identifier. Maybe at a number? :============================================
    //============================================================================================
    
    if (is_digit(cfile[pos])) {
      if (cfile[pos++] == '0') { // Check if the number is hexadecimal or octal.
        if (cfile[pos] == 'x') { // Check if the number is hexadecimal.
          // Yes, it is hexadecimal.
          const size_t sp = pos;
          while (++pos < length and is_hexdigit(cfile[pos]));
          while (pos < length and is_letter(cfile[pos])) pos++; // Include the flags, like ull
          return token_t(token_basics(TT_HEXLITERAL,filename,line,pos-lpos), cfile+sp, pos-sp);  
        }
        // Turns out, it's octal.
        const size_t sp = pos;
        while (pos < length and is_hexdigit(cfile[pos])) pos++;
        while (pos < length and is_letter(cfile[pos])) pos++; // Include the flags, like ull
        return token_t(token_basics(TT_OCTLITERAL,filename,line,pos-lpos), cfile+sp, pos-sp);
      }
      // Turns out, it's decimal.
      const size_t sp = pos - 1;
      while (pos < length and is_digit(cfile[pos])) pos++;
      while (pos < length and is_letter(cfile[pos])) pos++; // Include the flags, like ull
      return token_t(token_basics(TT_DECLITERAL,filename,line,pos-lpos), cfile+sp, pos-sp);
    }
    
    //============================================================================================
    //====: Not at a number. Find out where we are. :=============================================
    //============================================================================================
    
    const size_t spos = pos;
    switch (cfile[pos++])
    {
      case ';':
        return token_t(token_basics(TT_SEMICOLON,filename,line,spos-lpos));
      case ',':
        return token_t(token_basics(TT_COMMA,filename,line,spos-lpos));
      case '+': case '-':
        pos += cfile[pos] == cfile[spos] or cfile[pos] == '=';
        return token_t(token_basics(TT_OPERATOR,filename,line,spos-lpos), cfile+spos, pos-spos);
      case '=': pos += cfile[pos] == cfile[spos]; case '*': case '/': case '^':
        return token_t(token_basics(TT_OPERATOR,filename,line,spos-lpos), cfile+spos, pos-spos);
      case '&': case '|':  case '!': case '~': 
        pos += cfile[pos] == cfile[spos] || cfile[pos] == '=';
        return token_t(token_basics(TT_OPERATOR,filename,line,spos-lpos), cfile+spos, pos-spos);
      case '>': case '<':
        pos += cfile[pos] == cfile[spos]; pos += cfile[pos] == '=';
        return token_t(token_basics(TT_OPERATOR,filename,line,spos-lpos), cfile+spos, pos-spos);
      case ':':
        pos += cfile[pos] == cfile[spos];
        return token_t(token_basics(pos - spos == 1 ? TT_COLON : TT_SCOPE,filename,line,spos-lpos), cfile+spos, pos-spos);
        
      case '(': return token_t(token_basics(TT_LEFTPARENTH,filename,line,spos-lpos));
      case '[': return token_t(token_basics(TT_LEFTBRACKET,filename,line,spos-lpos));
      case '{': return token_t(token_basics(TT_LEFTBRACE,  filename,line,spos-lpos));
      case '}': return token_t(token_basics(TT_RIGHTBRACE,  filename,line,spos-lpos));
      case ']': return token_t(token_basics(TT_RIGHTBRACKET,filename,line,spos-lpos));
      case ')': return token_t(token_basics(TT_RIGHTPARENTH,filename,line,spos-lpos));
      
      case '#':
        return handle_preprocessor(herr), get_token(herr);
      
      default:
        return token_t(token_basics(TT_INVALID,filename,line,pos-lpos++));
    }
  }
  
  return token_t();
  
  POP_FILE: // This block was created instead of a helper function to piss Rusky off.
  if (files.empty())
    return token_t(token_basics(TT_ENDOFCODE,filename,line,pos-lpos));
  
  // Fetch data from top item
  openfile& of = files.top();
  line = of.line, lpos = of.lpos;
  filename = of.filename;
  consume(of.file);
  
  // Pop file stack and return next token in the containing file.
  files.pop();
  return get_token(herr);
}
  
lexer_cpp::lexer_cpp(llreader &input, macro_map &pmacros): macros(pmacros), filename("stdcall/file.cpp"), line(1), lpos(0), mlex(new lexer_macro(this))
{
  consume(input); // We are also an llreader. Consume the given one using the inherited method.
  keywords["class"] = TT_CLASS;
  keywords["enum"] = TT_ENUM;
  keywords["namespace"] = TT_NAMESPACE;
  keywords["private"] = TT_PRIVATE;
  keywords["protected"] = TT_PROTECTED;
  keywords["public"] = TT_PUBLIC;
  keywords["struct"] = TT_STRUCT;
  keywords["template"] = TT_TEMPLATE;
  keywords["typedef"] = TT_TYPEDEF;
  keywords["typename"] = TT_TYPENAME;
  keywords["union"] = TT_UNION;
  keywords["using"] = TT_USING;
}
lexer_cpp::~lexer_cpp() {
  delete mlex;
}

openfile::openfile() {}
openfile::openfile(const char* fname): filename(fname), line(0), lpos(0) {}
openfile::openfile(const char* fname, size_t line_num, size_t line_pos, llreader &consume): filename(fname), line(line_num), lpos(line_pos) { file.consume(consume); }
void openfile::swap(openfile &f) {
  { register const char* tmpl = filename;
  filename = f.filename, f.filename = tmpl; }
  register size_t tmpl = line;
  line = f.line, f.line = tmpl;
  tmpl = lpos, lpos = f.lpos, f.lpos = tmpl;
  llreader tmpr;
  tmpr.consume(file);
  file.consume(f.file);
  f.file.consume(tmpr);
}

#undef cfile
lexer_macro::lexer_macro(lexer_cpp *enc): pos(enc->pos), lcpp(enc) { }
void lexer_macro::update() { cfile = lcpp->data; length = lcpp->length; }
token_t lexer_macro::get_token(error_handler *herr)
{
  if (pos >= length) return token_t(token_basics(TT_ENDOFCODE,lcpp->filename,lcpp->line,pos-lcpp->lpos));
  for (;;) // Loop until we find something or hit world's end
  {
    // Skip all whitespace
    while (cfile[pos] == ' ' or cfile[pos] == '\t')
      if (++pos >= length) return token_t(token_basics(TT_ENDOFCODE,lcpp->filename,lcpp->line,pos-lcpp->lpos));
    if (cfile[pos] == '\n' or cfile[pos] == '\r') return token_t(token_basics(TT_ENDOFCODE,lcpp->filename,lcpp->line,pos-lcpp->lpos));
    
    //============================================================================================
    //====: Check for and handle comments. :======================================================
    //============================================================================================
    
    if (cfile[pos] == '/') {
      if (++pos < length) {
        if (cfile[pos] == '/') {
          // Find the end of the line
          while (cfile[pos] != '\n') if (++pos >= length) return token_t(token_basics(TT_ENDOFCODE,lcpp->filename,lcpp->line,pos-lcpp->lpos));
          continue;
        }
        if (cfile[pos] == '*') {
          ++pos; // Skip one more char so we don't break on /*/
          do if (pos >= length) return token_t(token_basics(TT_ENDOFCODE,lcpp->filename,lcpp->line,pos-lcpp->lpos));
            else if (cfile[pos] == '\n' or (cfile[pos] == '\r' and cfile[pos+1] != '\n')) ++lcpp->line, lcpp->lpos = pos;
            while (cfile[pos++] != '*' or cfile[pos] != '/');
          ++pos; continue;
        }
        if (cfile[pos] == '=')
          return token_t(token_basics(TT_OPERATOR,lcpp->filename,lcpp->line,pos-lcpp->lpos), cfile+pos-1, 2);
      }
      return token_t(token_basics(TT_OPERATOR,lcpp->filename,lcpp->line,pos-lcpp->lpos), cfile+pos-1,1);
    }
    
    //============================================================================================
    //====: Not at a comment. See if we're at an identifier. :====================================
    //============================================================================================
    
    if (is_letter(cfile[pos])) // Check if we're at an identifier or keyword.
    {
      const char* sp = cfile + pos; // Record where we are
      while (++pos < length and is_letterd(cfile[pos]));
      
      string fn(sp, cfile + pos); // We'll need a copy of this thing for lookup purposes
      
      macro_iter mi = lcpp->macros.find(fn);
      if (mi != lcpp->macros.end()) {
        cout << "FIX ME" << endl;
      }
      
      static const char zero[] = "0";
      return token_t(token_basics(TT_DECLITERAL,"some file",0,0), zero, 1);
    }
    
    //============================================================================================
    //====: Not at an identifier. Maybe at a number? :============================================
    //============================================================================================
    
    if (is_digit(cfile[pos])) {
      if (cfile[pos++] == '0') { // Check if the number is hexadecimal or octal.
        if (cfile[pos] == 'x') { // Check if the number is hexadecimal.
          // Yes, it is hexadecimal.
          const size_t sp = pos;
          while (++pos < length and is_hexdigit(cfile[pos]));
          while (pos < length and is_letter(cfile[pos])) pos++; // Include the flags, like ull
          return token_t(token_basics(TT_HEXLITERAL,lcpp->filename,lcpp->line,pos-lcpp->lpos), cfile+sp, pos-sp);  
        }
        // Turns out, it's octal.
        const size_t sp = pos;
        while (pos < length and is_hexdigit(cfile[pos])) pos++;
        while (pos < length and is_letter(cfile[pos])) pos++; // Include the flags, like ull
        return token_t(token_basics(TT_OCTLITERAL,lcpp->filename,lcpp->line,pos-lcpp->lpos), cfile+sp, pos-sp);
      }
      // Turns out, it's decimal.
      const size_t sp = pos - 1;
      while (pos < length and is_digit(cfile[pos])) pos++;
      while (pos < length and is_letter(cfile[pos])) pos++; // Include the flags, like ull
      return token_t(token_basics(TT_DECLITERAL,lcpp->filename,lcpp->line,pos-lcpp->lpos), cfile+sp, pos-sp);
    }
    
    
    //============================================================================================
    //====: Not at a number. Find out where we are. :=============================================
    //============================================================================================
    
    const size_t spos = pos;
    switch (cfile[pos++])
    {
      case ';':
        return token_t(token_basics(TT_SEMICOLON,lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case ',':
        return token_t(token_basics(TT_COMMA,lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case '+': case '-':
        pos += cfile[pos] == cfile[spos] or cfile[pos] == '=';
        return token_t(token_basics(TT_OPERATOR,lcpp->filename,lcpp->line,spos-lcpp->lpos), cfile+spos, pos-spos);
      case '=': pos += cfile[pos] == cfile[spos]; case '*': case '/': case '^':
        return token_t(token_basics(TT_OPERATOR,lcpp->filename,lcpp->line,spos-lcpp->lpos), cfile+spos, pos-spos);
      case '&': case '|':  case '!': case '~': 
        pos += cfile[pos] == cfile[spos] || cfile[pos] == '=';
        return token_t(token_basics(TT_OPERATOR,lcpp->filename,lcpp->line,spos-lcpp->lpos), cfile+spos, pos-spos);
      case '>': case '<':
        pos += cfile[pos] == cfile[spos]; pos += cfile[pos] == '=';
        return token_t(token_basics(TT_OPERATOR,lcpp->filename,lcpp->line,spos-lcpp->lpos), cfile+spos, pos-spos);
      case ':':
        pos += cfile[pos] == cfile[spos];
        return token_t(token_basics(pos - spos == 1 ? TT_COLON : TT_SCOPE,lcpp->filename,lcpp->line,spos-lcpp->lpos), cfile+spos, pos-spos);
        
      case '(': return token_t(token_basics(TT_LEFTPARENTH,lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case '[': return token_t(token_basics(TT_LEFTBRACKET,lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case '{': return token_t(token_basics(TT_LEFTBRACE,  lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case '}': return token_t(token_basics(TT_RIGHTBRACE,  lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case ']': return token_t(token_basics(TT_RIGHTBRACKET,lcpp->filename,lcpp->line,spos-lcpp->lpos));
      case ')': return token_t(token_basics(TT_RIGHTPARENTH,lcpp->filename,lcpp->line,spos-lcpp->lpos));
      
      case '#':
          if (cfile[pos] == '#') return token_t(token_basics(TTM_CONCAT,lcpp->filename,lcpp->line,spos-lcpp->lpos));
          return token_t(token_basics(TTM_TOSTRING,lcpp->filename,lcpp->line,spos-lcpp->lpos));
        break;
      
      case '\\':
          if (cfile[pos] == '\n' or (cfile[pos] == '\r' and (cfile[++pos] == '\n' or pos--))) lcpp->lpos = pos++, ++lcpp->line;
          return get_token(herr);
        break;
      
      default:
        return token_t(token_basics(TT_INVALID,lcpp->filename,lcpp->line,pos-lcpp->lpos++));
    }
  }
  
  return token_t();
}

lexer_cpp::condition::condition() {}
lexer_cpp::condition::condition(bool t, bool cbt): is_true(t), can_be_true(cbt) {}
