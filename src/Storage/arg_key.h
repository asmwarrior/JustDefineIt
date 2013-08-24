/**
 * @file  arg_key.h
 * @brief System header declaring a structure for storing parameter types
 *        or template argument types/values.
 * 
 * @section License
 * 
 * Copyright (C) 2011-2013 Josh Ventura
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

#ifndef _ARG_KEY__H
#define _ARG_KEY__H

namespace jdi { class arg_key; }

#include <Storage/definition_forward.h>
#include <Storage/full_type.h>
#include <Storage/value.h>

namespace jdi {
  /** Structure containing template arguments; can be used as the key in an std::map. **/
  class arg_key {
  public:
    enum ak_type { AKT_NONE, AKT_FULLTYPE, AKT_VALUE };
    /** Improvised C++ Union of full_type and value. */
    struct node {
      struct antialias {
        char data[
          (((sizeof(full_type) > sizeof(value))? sizeof(full_type) : sizeof(value)) + sizeof(char) - 1)
          /sizeof(char)
        ];
      } data;
      ak_type type;
      
      bool is_abstract() const;
      inline const full_type& ft() const { return *(full_type*)&data; }
      inline const value& val() const { return *(value*)&data; }
      inline full_type& ft() { return *(full_type*)&data; }
      inline value& val() { return *(value*)&data; }
      node &operator= (const node& other);
      bool operator!=(const node& x) const;
      
      inline node(): type(AKT_NONE) {}
      ~node();
    };
    
    private:
      /// An array of all our values
      node *values;
      /// A pointer past our value array
      node *endv;
      
    public:
      static definition *abstract; ///< A sentinel pointer marking that this parameter is still abstract.
      /// A comparator to allow storage in a map.
      bool operator<(const arg_key& other) const;
      /// A method to prepare this instance for storage of parameter values for the given template.
      void mirror(definition_template* temp);
      /// Allocate a new definition for the parameter at the given index; this will be either a definition_typed or definition_valued.
      definition *new_definition(size_t index, string name, definition_scope* parent) const;
      /// A fast function to assign to our list at a given index, consuming the given type.
      void swap_final_type(size_t argnum, full_type &type);
      /// A less fast function to assign to our list at a given index, copying the given type.
      void put_final_type(size_t argnum, const full_type &type);
      /// A slower function to put the most basic type representation down, consuming the given type
      void swap_type(size_t argnum, full_type &type);
      /// An even slower function to put the most basic type representation down, copying the given starting type
      void put_type(size_t argnum, const full_type &type);
      /// Function to copy over a full node
      void put_node(size_t argnum, const node &n);
      /// A quick function to put a value at a given index
      void put_value(size_t argnum, const value& val);
      /// A quick function to grab the type at a position
      inline node &operator[](size_t x) { return values[x]; }
      inline const node &operator[](size_t x) const { return values[x]; }
      /// A quick function to return an immutable pointer to the first parameter
      inline node* begin() { return values; }
      /// A quick function to return a pointer past the end of our list
      inline node* end() { return endv; }
      /// Const begin() equivalent.
      inline const node* begin() const { return values; }
      /// Const end() equivalent.
      inline const node* end() const { return endv; }
      
      /// Return a string version of this key's argument list. You'll need to wrap in () or <> yourself.
      string toString() const;
      
      /// Default constructor; mark values NULL.
      arg_key();
      /// Construct with a size, reserving sufficient memory.
       arg_key(size_t n);
      /// Construct a copy.
      arg_key(const arg_key& other);
      /// Construct from a ref_stack.
      arg_key(const ref_stack& refs);
      /// Destruct, freeing items.
      ~arg_key();
  };
}

#endif