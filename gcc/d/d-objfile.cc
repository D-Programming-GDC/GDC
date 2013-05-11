// d-objfile.cc -- D frontend for GCC.
// Copyright (C) 2011, 2012 Free Software Foundation, Inc.

// GCC is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3, or (at your option) any later
// version.

// GCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include "d-system.h"
#include "d-lang.h"
#include "d-codegen.h"

#include "module.h"
#include "template.h"
#include "init.h"
#include "attrib.h"
#include "enum.h"
#include "id.h"


ModuleInfo *ObjectFile::moduleInfo;
Modules ObjectFile::modules;
unsigned  ObjectFile::moduleSearchIndex;
DeferredThunks ObjectFile::deferredThunks;
FuncDeclarations ObjectFile::staticCtorList;
FuncDeclarations ObjectFile::staticDtorList;

// Construct a new Symbol.

Symbol::Symbol (void)
{
  this->Sident = NULL;
  this->prettyIdent = NULL;

  this->Sdt = NULL_TREE;
  this->Salignment = 0;
  this->Sreadonly = false;

  this->Stree = NULL_TREE;
  this->ScontextDecl = NULL_TREE;
  this->SframeField = NULL_TREE;

  this->outputStage = NotStarted;
  this->frameInfo = NULL;
}


void
Dsymbol::toObjFile (int)
{
  TupleDeclaration *td = this->isTupleDeclaration();

  if (!td)
    return;

  // Emit local variables for tuples.
  for (size_t i = 0; i < td->objects->dim; i++)
    {
      Object *o = (*td->objects)[i];
      if ((o->dyncast() == DYNCAST_EXPRESSION) && ((Expression *) o)->op == TOKdsymbol)
	{
	  Declaration *d = ((DsymbolExp *) o)->s->isDeclaration();
	  if (d)
	    d->toObjFile (0);
	}
    }
}

void
AttribDeclaration::toObjFile (int)
{
  Dsymbols *d = include (NULL, NULL);

  if (!d)
    return;

  for (size_t i = 0; i < d->dim; i++)
    {
      Dsymbol *s = (*d)[i];
      s->toObjFile (0);
    }
}

void
PragmaDeclaration::toObjFile (int)
{
  if (!global.params.ignoreUnsupportedPragmas)
    {
      if (ident == Id::lib)
	warning (loc, "pragma(lib) not implemented");
       else if (ident == Id::startaddress)
	 warning (loc, "pragma(startaddress) not implemented");
    }

  AttribDeclaration::toObjFile (0);
}

void
StructDeclaration::toObjFile (int)
{
  if (type->ty == Terror)
    {
      error ("had semantic errors when compiling");
      return;
    }

  // Anonymous structs/unions only exist as part of others,
  // do not output forward referenced structs's
  if (isAnonymous() || !members)
    return;

  if (global.params.symdebug)
    toDebug();

  // Generate TypeInfo
  type->getTypeInfo (NULL);

  // Generate static initialiser
  toInitializer();
  toDt (&sinit->Sdt);

  sinit->Sreadonly = true;
  d_finish_symbol (sinit);

  // Put out the members
  for (size_t i = 0; i < members->dim; i++)
    {
      Dsymbol *member = (*members)[i];
      // There might be static ctors in the members, and they cannot
      // be put in separate object files.
      member->toObjFile (0);
    }
}

void
ClassDeclaration::toObjFile (int)
{
  if (type->ty == Terror)
    {
      error ("had semantic errors when compiling");
      return;
    }

  if (!members)
    return;

  if (global.params.symdebug)
    toDebug();

  // Put out the members
  for (size_t i = 0; i < members->dim; i++)
    {
      Dsymbol *member = (*members)[i];
      member->toObjFile (0);
    }

  // Generate C symbols
  toSymbol();
  toVtblSymbol();
  sinit = toInitializer();

  // Generate static initialiser
  toDt (&sinit->Sdt);
  sinit->Sreadonly = true;
  d_finish_symbol (sinit);

  // Put out the TypeInfo
  type->getTypeInfo (NULL);

  // must be ClassInfo.size
  size_t classinfo_size = global.params.is64bit ? CLASSINFO_SIZE_64 : CLASSINFO_SIZE;
  size_t offset = classinfo_size;
  if (classinfo->structsize != classinfo_size)
    {
      error ("mismatch between compiler and object.d or object.di found. Check installation and import paths.");
      gcc_unreachable();
    }

  /* Put out the ClassInfo. 
   * The layout is:
   *  void **vptr;
   *  monitor_t monitor;
   *  byte[] initializer;         // static initialisation data
   *  char[] name;                // class name
   *  void *[] vtbl;
   *  Interface[] interfaces;
   *  Object *base;               // base class
   *  void *destructor;
   *  void *invariant;            // class invariant
   *  uint flags;
   *  void *deallocator;
   *  OffsetTypeInfo[] offTi;
   *  void *defaultConstructor;
   *  void* xgetRTInfo;
   */
  tree dt = NULL_TREE;

  build_vptr_monitor (&dt, classinfo);

  // initializer[]
  gcc_assert (structsize >= 8);
  dt_cons (&dt, d_array_value (Type::tint8->arrayOf()->toCtype(),
			       size_int (structsize),
			       build_address (sinit->Stree)));
  // name[]
  const char *name = ident->toChars();
  if (!(strlen (name) > 9 && memcmp (name, "TypeInfo_", 9) == 0))
    name = toPrettyChars();
  dt_cons (&dt, d_array_string (name));

  // vtbl[]
  dt_cons (&dt, d_array_value (Type::tvoidptr->arrayOf()->toCtype(),
			       size_int (vtbl.dim),
			       build_address (vtblsym->Stree)));
  // (*vtblInterfaces)[]
  dt_cons (&dt, size_int (vtblInterfaces->dim));

  if (vtblInterfaces->dim)
    {
      // Put out offset to where (*vtblInterfaces)[] is put out
      // after the other data members.
      dt_cons (&dt, build_offset (build_address (csym->Stree),
				  size_int (offset)));
    }
  else
    dt_cons (&dt, d_null_pointer);

  // base*
  if (baseClass)
    dt_cons (&dt, build_address (baseClass->toSymbol()->Stree));
  else
    dt_cons (&dt, d_null_pointer);

  // dtor*
  if (dtor)
    dt_cons (&dt, build_address (dtor->toSymbol()->Stree));
  else
    dt_cons (&dt, d_null_pointer);

  // invariant*
  if (inv)
    dt_cons (&dt, build_address (inv->toSymbol()->Stree));
  else
    dt_cons (&dt, d_null_pointer);

  // flags
  size_t flags = (4 | isCOMclass() | 16 | 32);
  if (ctor)
    flags |= 8;
  if (isabstract)
    flags |= 64;
  for (ClassDeclaration *cd = this; cd; cd = cd->baseClass)
    {
      if (cd->members)
	{
	  for (size_t i = 0; i < cd->members->dim; i++)
	    {
	      Dsymbol *sm = (*cd->members)[i];
	      if (sm->hasPointers())
		goto Lhaspointers;
	    }
	}
    }
  flags |= 2;

Lhaspointers:
  dt_cons (&dt, size_int (flags));

  // deallocator*
  if (aggDelete)
    dt_cons (&dt, build_address (aggDelete->toSymbol()->Stree));
  else
    dt_cons (&dt, d_null_pointer);

  // offTi[]
  dt_cons (&dt, d_array_value (Type::tuns8->arrayOf()->toCtype(),
			       size_int (0), d_null_pointer));

  // defaultConstructor*
  if (defaultCtor)
    dt_cons (&dt, build_address (defaultCtor->toSymbol()->Stree));
  else
    dt_cons (&dt, d_null_pointer);

  // xgetRTInfo*
  if (getRTInfo)
    getRTInfo->toDt (&dt);
  else
    {
      // If class has no pointers.
      if (flags & 2)
    	dt_cons (&dt, size_int (0));
      else
	dt_cons (&dt, size_int (1));
    }

  /* Put out (*vtblInterfaces)[]. Must immediately follow csym.
   * The layout is:
   *  TypeInfo_Class typeinfo;
   *  void*[] vtbl;
   *  ptrdiff_t offset;
   */
  offset += vtblInterfaces->dim * (4 * PTRSIZE);
  for (size_t i = 0; i < vtblInterfaces->dim; i++)
    {
      BaseClass *b = (*vtblInterfaces)[i];
      ClassDeclaration *id = b->base;

      // Fill in vtbl[]
      b->fillVtbl(this, &b->vtbl, 1);

      // ClassInfo
      dt_cons (&dt, build_address (id->toSymbol()->Stree));

      // vtbl[]
      dt_cons (&dt, size_int (id->vtbl.dim));
      dt_cons (&dt, build_offset (build_address (csym->Stree),
				     size_int (offset)));
      // 'this' offset.
      dt_cons (&dt, size_int (b->offset));

      offset += id->vtbl.dim * PTRSIZE;
    }

  // Put out the (*vtblInterfaces)[].vtbl[]
  // This must be mirrored with ClassDeclaration::baseVtblOffset()
  for (size_t i = 0; i < vtblInterfaces->dim; i++)
    {
      BaseClass *b = (*vtblInterfaces)[i];
      ClassDeclaration *id = b->base;

      if (id->vtblOffset())
	{
	  tree size = size_int (classinfo_size + i * (4 * PTRSIZE));
	  dt_cons (&dt, build_offset (build_address (csym->Stree), size));
	}

      gcc_assert (id->vtbl.dim == b->vtbl.dim);
      for (size_t j = id->vtblOffset() ? 1 : 0; j < id->vtbl.dim; j++)
	{
	  gcc_assert (j < b->vtbl.dim);
	  FuncDeclaration *fd = b->vtbl[j];
	  if (fd)
	    dt_cons (&dt, build_address (fd->toThunkSymbol (b->offset)->Stree));
	  else
	    dt_cons (&dt, d_null_pointer);
	}
    }

  // Put out the overriding interface vtbl[]s.
  // This must be mirrored with ClassDeclaration::baseVtblOffset()
  ClassDeclaration *cd;
  FuncDeclarations bvtbl;

  for (cd = this->baseClass; cd; cd = cd->baseClass)
    {
      for (size_t i = 0; i < cd->vtblInterfaces->dim; i++)
	{
	  BaseClass *bs = (*cd->vtblInterfaces)[i];

	  if (bs->fillVtbl (this, &bvtbl, 0))
	    {
	      ClassDeclaration *id = bs->base;

	      if (id->vtblOffset())
		{
		  tree size = size_int (classinfo_size + i * (4 * PTRSIZE));
		  dt_cons (&dt, build_offset (build_address (cd->toSymbol()->Stree), size));
		}

	      for (size_t j = id->vtblOffset() ? 1 : 0; j < id->vtbl.dim; j++)
		{
		  gcc_assert (j < bvtbl.dim);
		  FuncDeclaration *fd = bvtbl[j];
		  if (fd)
		    dt_cons (&dt, build_address (fd->toThunkSymbol (bs->offset)->Stree));
		  else
		    dt_cons (&dt, d_null_pointer);
		}
	    }
	}
    }

  csym->Sdt = dt;
  d_finish_symbol (csym);

  // Put out the vtbl[]
  dt = NULL_TREE;

  // first entry is ClassInfo reference
  dt_cons (&dt, build_address (csym->Stree));

  for (size_t i = 1; i < vtbl.dim; i++)
    {
      FuncDeclaration *fd = vtbl[i]->isFuncDeclaration();

      if (fd && (fd->fbody || !isAbstract()))
	{
	  fd->functionSemantic();
	  Symbol *s = fd->toSymbol();

	  if (!isFuncHidden (fd))
	    goto Lcontinue;

	  // If fd overlaps with any function in the vtbl[], then 
	  // issue 'hidden' error.
	  for (size_t j = 1; j < vtbl.dim; j++)
	    {
	      if (j == i)
		continue;

	      FuncDeclaration *fd2 = vtbl[j]->isFuncDeclaration();
	      if (!fd2->ident->equals (fd->ident))
		continue;

	      if (fd->leastAsSpecialized (fd2) || fd2->leastAsSpecialized (fd))
		{
		  TypeFunction *tf = (TypeFunction *) fd->type;
		  if (tf->ty == Tfunction)
		    {
		      deprecation ("use of %s%s hidden by %s is deprecated. "
				   "Use 'alias %s.%s %s;' to introduce base class overload set.",
				   fd->toPrettyChars(), Parameter::argsTypesToChars(tf->parameters, tf->varargs), toChars(),
				   fd->parent->toChars(), fd->toChars(), fd->toChars());
		    }
		  else
		    deprecation ("use of %s hidden by %s is deprecated", fd->toPrettyChars(), toChars());

		  s = get_libcall (LIBCALL_HIDDEN_FUNC)->toSymbol();
		  break;
		}
	    }

	Lcontinue:
	  dt_cons (&dt, build_address (s->Stree));
	}
      else
	dt_cons (&dt, d_null_pointer);
    }

  vtblsym->Sdt = dt;
  vtblsym->Sreadonly = true;
  d_finish_symbol (vtblsym);
}

// Get offset of base class's vtbl[] initialiser from start of csym.

unsigned
ClassDeclaration::baseVtblOffset (BaseClass *bc)
{
  unsigned csymoffset = global.params.is64bit ? CLASSINFO_SIZE_64 : CLASSINFO_SIZE;
  csymoffset += vtblInterfaces->dim * (4 * PTRSIZE);

  for (size_t i = 0; i < vtblInterfaces->dim; i++)
    {
      BaseClass *b = (*vtblInterfaces)[i];
      if (b == bc)
	return csymoffset;
      csymoffset += b->base->vtbl.dim * PTRSIZE;
    }

  // Put out the overriding interface vtbl[]s.
  for (ClassDeclaration *cd = this->baseClass; cd; cd = cd->baseClass)
    {
      for (size_t k = 0; k < cd->vtblInterfaces->dim; k++)
	{
	  BaseClass *bs = (*cd->vtblInterfaces)[k];
	  if (bs->fillVtbl(this, NULL, 0))
	    {
	      if (bc == bs)
		return csymoffset;
	      csymoffset += bs->base->vtbl.dim * PTRSIZE;
	    }
	}
    }

  return ~0;
}

void
InterfaceDeclaration::toObjFile (int)
{
  if (type->ty == Terror)
    {
      error ("had semantic errors when compiling");
      return;
    }

  if (!members)
    return;

  if (global.params.symdebug)
    toDebug();

  // Put out the members
  for (size_t i = 0; i < members->dim; i++)
    {
      Dsymbol *member = (*members)[i];
      member->toObjFile (0);
    }

  // Generate C symbols
  toSymbol();

  // Put out the TypeInfo
  type->getTypeInfo (NULL);
  type->vtinfo->toObjFile (0);

  /* Put out the ClassInfo. 
   * The layout is:
   *  void **vptr;
   *  monitor_t monitor;
   *  byte[] initializer;         // static initialisation data
   *  char[] name;                // class name
   *  void *[] vtbl;
   *  Interface[] interfaces;
   *  Object *base;               // base class
   *  void *destructor;
   *  void *invariant;            // class invariant
   *  uint flags;
   *  void *deallocator;
   *  OffsetTypeInfo[] offTi;
   *  void *defaultConstructor;
   *  void* xgetRTInfo;
   */
  tree dt = NULL_TREE;

  build_vptr_monitor (&dt, classinfo);

  // initializer[]
  dt_cons (&dt, d_array_value (Type::tint8->arrayOf()->toCtype(),
			       size_int (0), d_null_pointer));
  // name[]
  dt_cons (&dt, d_array_string (toPrettyChars()));

  // vtbl[]
  dt_cons (&dt, d_array_value (Type::tvoidptr->arrayOf()->toCtype(),
			       size_int (0), d_null_pointer));
  // (*vtblInterfaces)[]
  dt_cons (&dt, size_int (vtblInterfaces->dim));

  if (vtblInterfaces->dim)
    {
      // must be ClassInfo.size
      size_t offset = global.params.is64bit ? CLASSINFO_SIZE_64 : CLASSINFO_SIZE;
      if (classinfo->structsize != offset)
	{
	  error ("mismatch between compiler and object.d or object.di found. Check installation and import paths.");
	  gcc_unreachable();
	}
      // Put out offset to where (*vtblInterfaces)[] is put out
      // after the other data members.
      dt_cons (&dt, build_offset (build_address (csym->Stree),
				  size_int (offset)));
    }
  else
    dt_cons (&dt, d_null_pointer);

  // base*, dtor*, invariant*
  gcc_assert (!baseClass);
  dt_cons (&dt, d_null_pointer);
  dt_cons (&dt, d_null_pointer);
  dt_cons (&dt, d_null_pointer);

  // flags
  dt_cons (&dt, size_int (4 | isCOMinterface() | 32));

  // deallocator*
  dt_cons (&dt, d_null_pointer);

  // offTi[]
  dt_cons (&dt, d_array_value (Type::tuns8->arrayOf()->toCtype(),
			       size_int (0), d_null_pointer));

  // defaultConstructor*, xgetRTInfo*
  dt_cons (&dt, d_null_pointer);
  dt_cons (&dt, size_int (0x12345678));

  /* Put out (*vtblInterfaces)[]. Must immediately follow csym.
   * The layout is:
   *  TypeInfo_Class typeinfo;
   *  void*[] vtbl;
   *  ptrdiff_t offset;
   */
  for (size_t i = 0; i < vtblInterfaces->dim; i++)
    {
      BaseClass *b = (*vtblInterfaces)[i];
      ClassDeclaration *id = b->base;

      // ClassInfo
      dt_cons (&dt, build_address (id->toSymbol()->Stree));

      // vtbl[]
      dt_cons (&dt, d_array_value (Type::tvoidptr->arrayOf()->toCtype(),
				   size_int (0), d_null_pointer));
      // 'this' offset.
      dt_cons (&dt, size_int (b->offset));
    }

  csym->Sdt = dt;
  csym->Sreadonly = true;
  d_finish_symbol (csym);
}

void
EnumDeclaration::toObjFile (int)
{
  if (objFileDone)
    return;

  if (type->ty == Terror)
    {
      error ("had semantic errors when compiling");
      return;
    }

  if (isAnonymous())
    return;

  if (global.params.symdebug)
    toDebug();

  // Generate TypeInfo
  type->getTypeInfo(NULL);

  TypeEnum *tc = (TypeEnum *) type;
  if (tc->sym->defaultval && !type->isZeroInit())
    {
      // Generate static initialiser
      toInitializer();
      tc->sym->defaultval->toDt (&sinit->Sdt);
      d_finish_symbol (sinit);
    }

  objFileDone = true;
}

void
VarDeclaration::toObjFile (int)
{
  if (type->ty == Terror)
    {
      error ("had semantic errors when compiling");
      return;
    }

  if (aliassym)
    {
      toAlias()->toObjFile (0);
      return;
    }

  // Do not store variables we cannot take the address of
  if (!canTakeAddressOf())
    return;

  if (isDataseg() && !(storage_class & STCextern))
    {
      Symbol *s = toSymbol();
      size_t sz = type->size();

      if (init)
	{
	  // Look for static array that is block initialised.
	  Type *tb = type->toBasetype();
	  ExpInitializer *ie = init->isExpInitializer();

	  if (ie && tb->ty == Tsarray
	      && !tb->nextOf()->equals(ie->exp->type->toBasetype()->nextOf())
	      && ie->exp->implicitConvTo(tb->nextOf()))
	    {
	      TypeSArray *tsa = (TypeSArray *) tb;
	      tsa->toDtElem (&s->Sdt, ie->exp);
	    }
	  else
	    s->Sdt = init->toDt();
	}
      else
	type->toDt(&s->Sdt);

      // Frontend should have already caught this.
      gcc_assert (sz || type->toBasetype()->ty == Tsarray);
      d_finish_symbol (s);
    }
  else
    {
      // This is needed for VarDeclarations in mixins that are to be
      // local variables of a function.  Otherwise, it would be
      // enough to make a check for isVarDeclaration() in
      // DeclarationExp::toElem.
      current_irs->emitLocalVar (this);
    }
}

void
TypedefDeclaration::toObjFile (int)
{
  if (type->ty == Terror)
    {
      error ("had semantic errors when compiling");
      return;
    }

  if (global.params.symdebug)
    toDebug();

  // Generate TypeInfo
  type->getTypeInfo(NULL);

  TypeTypedef *tc = (TypeTypedef *) type;
  if (tc->sym->init && !type->isZeroInit())
    {
      // Generate static initialiser
      toInitializer();
      sinit->Sdt = tc->sym->init->toDt();
      sinit->Sreadonly = true;
      d_finish_symbol (sinit);
    }
}

void
TemplateInstance::toObjFile (int)
{
  if (errors || !members)
    return;

  for (size_t i = 0; i < members->dim; i++)
    {
      Dsymbol *s = (*members)[i];
      s->toObjFile (0);
    }
}

void
TemplateMixin::toObjFile (int)
{
  TemplateInstance::toObjFile (0);
}

void
TypeInfoDeclaration::toObjFile (int)
{
  Symbol *s = toSymbol();
  toDt (&s->Sdt);
  d_finish_symbol (s);
}


// Put out instance of ModuleInfo for this Module

void
Module::genmoduleinfo()
{
  Symbol *msym = toSymbol();
  tree dt = NULL_TREE;
  ClassDeclarations aclasses;
  FuncDeclaration *sgetmembers;

  for (size_t i = 0; i < members->dim; i++)
    {
      Dsymbol *member = (*members)[i];
      member->addLocalClass (&aclasses);
    }

  size_t aimports_dim = aimports.dim;
  for (size_t i = 0; i < aimports.dim; i++)
    {
      Module *m = aimports[i];
      if (!m->needmoduleinfo)
	aimports_dim--;
    }

  sgetmembers = findGetMembers();

  size_t flags = MInew;
  if (sctor)
    flags |= MItlsctor;
  if (sdtor)
    flags |= MItlsdtor;
  if (ssharedctor)
    flags |= MIctor;
  if (sshareddtor)
    flags |= MIdtor;
  if (sgetmembers)
    flags |= MIxgetMembers;
  if (sictor)
    flags |= MIictor;
  if (stest)
    flags |= MIunitTest;
  if (aimports_dim)
    flags |= MIimportedModules;
  if (aclasses.dim)
    flags |= MIlocalClasses;

  if (!needmoduleinfo)
    flags |= MIstandalone;

  /* Put out:
   *  uint flags;
   *  uint index;
   */
  dt_cons (&dt, build_integer_cst (flags, Type::tuns32->toCtype()));
  dt_cons (&dt, build_integer_cst (0, Type::tuns32->toCtype()));

  /* Order of appearance, depending on flags
   *  void function() tlsctor;
   *  void function() tlsdtor;
   *  void* function() xgetMembers;
   *  void function() ctor;
   *  void function() dtor;
   *  void function() ictor;
   *  void function() unitTest;
   *  ModuleInfo*[] importedModules;
   *  TypeInfo_Class[] localClasses;
   *  string name;
   */
  if (flags & MItlsctor)
    dt_cons (&dt, build_address (sctor->Stree));

  if (flags & MItlsdtor)
    dt_cons (&dt, build_address (sdtor->Stree));

  if (flags & MIctor)
    dt_cons (&dt, build_address (ssharedctor->Stree));

  if (flags & MIdtor)
    dt_cons (&dt, build_address (sshareddtor->Stree));

  if (flags & MIxgetMembers)
    dt_cons (&dt, build_address (sgetmembers->toSymbol()->Stree));

  if (flags & MIictor)
    dt_cons (&dt, build_address (sictor->Stree));

  if (flags & MIunitTest)
    dt_cons (&dt, build_address (stest->Stree));

  if (flags & MIimportedModules)
    {
      dt_cons (&dt, size_int (aimports_dim));
      for (size_t i = 0; i < aimports.dim; i++)
	{
	  Module *m = aimports[i];
	  if (m->needmoduleinfo)
	    dt_cons (&dt, build_address (m->toSymbol()->Stree));
	}
    }

  if (flags & MIlocalClasses)
    {
      dt_cons (&dt, size_int (aclasses.dim));
      for (size_t i = 0; i < aclasses.dim; i++)
	{
	  ClassDeclaration *cd = aclasses[i];
	  dt_cons (&dt, build_address (cd->toSymbol()->Stree));
	}
    }

  // Put out module name as a 0-terminated string, to save bytes
  dt_cons (&dt, d_array_string (toPrettyChars()));

  csym->Sdt = dt;
  d_finish_symbol (csym);

  build_moduleinfo (msym);
}


void
FuncDeclaration::toObjFile (int)
{
  if (!global.params.useUnitTests && isUnitTestDeclaration())
    return;

  if (!object_file->shouldEmit (this))
    return;

  Symbol *sym = toSymbol();
  if (sym->outputStage)
    return;

  sym->outputStage = InProgress;

  tree fndecl = sym->Stree;

  if (!fbody)
    {
      if (!isNested())
	{
	  // %% Should set this earlier...
	  DECL_EXTERNAL (fndecl) = 1;
	  TREE_PUBLIC (fndecl) = 1;
	}
      rest_of_decl_compilation (fndecl, 1, 0);
      return;
    }

  if (global.params.verbose)
    fprintf (stdmsg, "function  %s\n", this->toPrettyChars());

  IRState *irs = current_irs->startFunction (this);

  irs->useChain (NULL, NULL_TREE);

  tree old_current_function_decl = current_function_decl;
  function *old_cfun = cfun;
  current_function_decl = fndecl;

  tree return_type = TREE_TYPE (TREE_TYPE (fndecl));
  tree result_decl = build_decl (UNKNOWN_LOCATION, RESULT_DECL,
				 NULL_TREE, return_type);
  object_file->setDeclLoc (result_decl, this);
  DECL_RESULT (fndecl) = result_decl;
  DECL_CONTEXT (result_decl) = fndecl;
  DECL_ARTIFICIAL (result_decl) = 1;
  DECL_IGNORED_P (result_decl) = 1;

  allocate_struct_function (fndecl, false);
  object_file->setCfunEndLoc (endloc);

  // Add method to record for debug information.
  if (isThis())
    {
      AggregateDeclaration *ad = isThis();
      tree rec = ad->type->toCtype();

      if (ad->isClassDeclaration())
	rec = TREE_TYPE (rec);

      object_file->addAggMethod (rec, this);
    }

  tree parm_decl = NULL_TREE;
  tree param_list = NULL_TREE;

  // Special arguments...

  // 'this' parameter
  if (vthis)
    {
      parm_decl = vthis->toSymbol()->Stree;
      if (!isThis() && isNested())
	{
	  // D still generates a vthis, but it should not be
	  // referenced in any expression.
	  FuncDeclaration *fd = toParent2()->isFuncDeclaration();
	  DECL_ARTIFICIAL (parm_decl) = 1;
	  irs->useChain (fd, parm_decl);
	}
      object_file->setDeclLoc (parm_decl, vthis);
      param_list = chainon (param_list, parm_decl);
    }

  // _arguments parameter.
  if (v_arguments)
    {
      parm_decl = v_arguments->toSymbol()->Stree;
      object_file->setDeclLoc (parm_decl, v_arguments);
      param_list = chainon (param_list, parm_decl);
    }

  // formal function parameters.
  size_t n_parameters = parameters ? parameters->dim : 0;

  for (size_t i = 0; i < n_parameters; i++)
    {
      VarDeclaration *param = (*parameters)[i];
      parm_decl = param->toSymbol()->Stree;
      object_file->setDeclLoc (parm_decl, (Dsymbol *) param);
      // chain them in the correct order
      param_list = chainon (param_list, parm_decl);
    }
  DECL_ARGUMENTS (fndecl) = param_list;
  for (tree t = param_list; t; t = DECL_CHAIN (t))
    DECL_CONTEXT (t) = fndecl;

  rest_of_decl_compilation (fndecl, 1, 0);
  DECL_INITIAL (fndecl) = error_mark_node;
  pushlevel (0);

  irs->pushStatementList();
  irs->startScope();
  irs->doLineNote (loc);

  // If this is a member function that nested (possibly indirectly) in another
  // function, construct an expession for this member function's static chain
  // by going through parent link of nested classes.
  if (isThis())
    {
      AggregateDeclaration *ad = isThis();
      tree this_tree = vthis->toSymbol()->Stree;
      while (ad->isNested())
	{
	  Dsymbol *d = ad->toParent2();
	  tree vthis_field = ad->vthis->toSymbol()->Stree;
	  this_tree = component_ref (build_deref (this_tree), vthis_field);

	  FuncDeclaration *fd = d->isFuncDeclaration();
	  ad = d->isAggregateDeclaration();
	  if (ad == NULL)
	    {
	      gcc_assert (fd != NULL);
	      irs->useChain (fd, this_tree);
	      break;
	    }
	}
    }

  // May chain irs->chainLink and irs->chainFunc.
  irs->buildChain (this);
  DECL_LANG_SPECIFIC (fndecl) = build_d_decl_lang_specific (this);

  if (vresult)
    irs->emitLocalVar (vresult, true);

  if (v_argptr)
    irs->pushStatementList();
  if (v_arguments_var)
    irs->emitLocalVar (v_arguments_var);

  fbody->toIR (irs);

  if (v_argptr)
    {
      tree body = irs->popStatementList();
      tree var = irs->var (v_argptr);
      var = build_address (var);

      tree init_exp = d_build_call_nary (builtin_decl_explicit (BUILT_IN_VA_START), 2, var, parm_decl);
      v_argptr->init = NULL; // VoidInitializer?
      irs->emitLocalVar (v_argptr, true);
      irs->addExp (init_exp);

      tree cleanup = d_build_call_nary (builtin_decl_explicit (BUILT_IN_VA_END), 1, var);
      irs->addExp (build2 (TRY_FINALLY_EXPR, void_type_node, body, cleanup));
    }

  irs->endScope();

  DECL_SAVED_TREE (fndecl) = irs->popStatementList();

  /* In tree-nested.c, init_tmp_var expects a statement list to come
     from somewhere.  popStatementList returns expressions when
     there is a single statement.  This code creates a statemnt list
     unconditionally because the DECL_SAVED_TREE will always be a
     BIND_EXPR. */
    {
      tree body = DECL_SAVED_TREE (fndecl);
      tree t;

      gcc_assert (TREE_CODE (body) == BIND_EXPR);

      t = TREE_OPERAND (body, 1);
      if (TREE_CODE (t) != STATEMENT_LIST)
	{
	  tree sl = alloc_stmt_list();
	  append_to_statement_list_force (t, &sl);
	  TREE_OPERAND (body, 1) = sl;
	}
      else if (!STATEMENT_LIST_HEAD (t))
	{
	  /* For empty functions: Without this, there is a
	     segfault when inlined.  Seen on build=ppc-linux but
	     not others (why?). */
	  tree ret = build1 (RETURN_EXPR, void_type_node, NULL_TREE);
	  append_to_statement_list_force (ret, &t);
	}
    }

  tree block = poplevel (1, 0, 1);
  DECL_INITIAL (fndecl) = block;
  BLOCK_SUPERCONTEXT (DECL_INITIAL (fndecl)) = fndecl;

  if (!errorcount && !global.errors)
    {
      FILE *dump_file;
      int local_dump_flags;

      // Build cgraph for function.
      cgraph_get_create_node (fndecl);

      // Set original decl context back to true context
      if (D_DECL_STATIC_CHAIN (fndecl))
	{
	  struct lang_decl *d = DECL_LANG_SPECIFIC (fndecl);
	  DECL_CONTEXT (fndecl) = d->d_decl->toSymbol()->ScontextDecl;
	}

      /* Dump the D-specific tree IR.  */
      dump_file = dump_begin (TDI_original, &local_dump_flags);
      if (dump_file)
	{
	  fprintf (dump_file, "\n;; Function %s",
		   lang_hooks.decl_printable_name (fndecl, 2));
	  fprintf (dump_file, " (%s)\n",
		   (!DECL_ASSEMBLER_NAME_SET_P (fndecl) ? "null"
		    : IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (fndecl))));
	  fprintf (dump_file, ";; enabled by -%s\n", dump_flag_name (TDI_original));
	  fprintf (dump_file, "\n");

	  if (local_dump_flags & TDF_RAW)
	    dump_node (DECL_SAVED_TREE (fndecl),
		       TDF_SLIM | local_dump_flags, dump_file);
	  else
	    print_generic_expr (dump_file, DECL_SAVED_TREE (fndecl), local_dump_flags);
	  fprintf (dump_file, "\n");

	  dump_end (TDI_original, dump_file);
	}
    }

  sym->outputStage = Finished;

  if (!errorcount && !global.errors)
    object_file->outputFunction (this);

  // Process all deferred nested functions.
  for (size_t i = 0; i < this->deferred.dim; ++i)
    {
      FuncDeclaration *fd = this->deferred[i];
      fd->toObjFile (0);
    }

  current_function_decl = old_current_function_decl;
  set_cfun (old_cfun);

  irs->endFunction();
}

void
FuncDeclaration::buildClosure (IRState *irs)
{
  FuncFrameInfo *ffi = irs->getFrameInfo (this);
  gcc_assert (ffi->is_closure);

  if (!ffi->creates_frame)
    {
      if (ffi->static_chain)
	{
	  tree link = irs->chainLink();
	  irs->useChain (this, link);
	}
      return;
    }

  tree closure_rec_type = irs->buildFrameForFunction (this);
  gcc_assert(COMPLETE_TYPE_P (closure_rec_type));

  tree closure_ptr = irs->localVar (build_pointer_type (closure_rec_type));
  DECL_NAME (closure_ptr) = get_identifier ("__closptr");
  DECL_IGNORED_P (closure_ptr) = 0;

  tree arg = convert (Type::tsize_t->toCtype(),
		      TYPE_SIZE_UNIT (closure_rec_type));

  DECL_INITIAL (closure_ptr) =
    build_nop (TREE_TYPE (closure_ptr),
	       build_libcall (LIBCALL_ALLOCMEMORY, 1, &arg));
  irs->expandDecl (closure_ptr);

  // set the first entry to the parent closure, if any
  tree chain_link = irs->chainLink();
  tree chain_field = component_ref (build_deref (closure_ptr),
				    TYPE_FIELDS (closure_rec_type));
  tree chain_expr = vmodify_expr (chain_field,
				  chain_link ? chain_link : d_null_pointer);
  irs->addExp (chain_expr);

  // copy parameters that are referenced nonlocally
  for (size_t i = 0; i < closureVars.dim; i++)
    {
      VarDeclaration *v = closureVars[i];
      if (!v->isParameter())
	continue;

      Symbol *vsym = v->toSymbol();

      tree closure_field = component_ref (build_deref (closure_ptr), vsym->SframeField);
      tree closure_expr = vmodify_expr (closure_field, vsym->Stree);
      irs->addExp (closure_expr);
    }

  irs->useChain (this, closure_ptr);
}

void
Module::genobjfile (int)
{
  /* Normally would create an ObjFile here, but gcc is limited to one obj file
     per pass and there may be more than one module per obj file. */
  gcc_assert (object_file);

  object_file->beginModule (this);
  object_file->setupStaticStorage (this, toSymbol()->Stree);

  if (members)
    {
      for (size_t i = 0; i < members->dim; i++)
	{
	  Dsymbol *dsym = (*members)[i];
	  dsym->toObjFile (0);
	}
    }

  // Default behaviour is to always generate module info because of templates.
  // Can be switched off for not compiling against runtime library.
  if (!global.params.betterC)
    {
      ModuleInfo& mi = *object_file->moduleInfo;
      if (mi.ctors.dim || mi.ctorgates.dim)
	sctor = object_file->doCtorFunction ("*__modctor", &mi.ctors, &mi.ctorgates)->toSymbol();
      if (mi.dtors.dim)
	sdtor = object_file->doDtorFunction ("*__moddtor", &mi.dtors)->toSymbol();
      if (mi.sharedctors.dim || mi.sharedctorgates.dim)
	ssharedctor = object_file->doCtorFunction ("*__modsharedctor",
						   &mi.sharedctors, &mi.sharedctorgates)->toSymbol();
      if (mi.shareddtors.dim)
	sshareddtor = object_file->doDtorFunction ("*__modshareddtor", &mi.shareddtors)->toSymbol();
      if (mi.unitTests.dim)
	stest = object_file->doUnittestFunction ("*__modtest", &mi.unitTests)->toSymbol();

      genmoduleinfo();
    }

  object_file->endModule();
}


void
ObjectFile::beginModule (Module *m)
{
  moduleInfo = new ModuleInfo;
  current_module = m;
}

void
ObjectFile::endModule (void)
{
  for (size_t i = 0; i < deferredThunks.dim; i++)
    {
      DeferredThunk *t = deferredThunks[i];
      outputThunk (t->decl, t->target, t->offset);
    }
  deferredThunks.setDim (0);
  moduleInfo = NULL;
  current_module = NULL;
}

bool
ObjectFile::hasModule (Module *m)
{
  if (!m || !modules.dim)
    return false;

  if (modules[moduleSearchIndex] == m)
    return true;
  for (size_t i = 0; i < modules.dim; i++)
    {
      if (modules[i] == m)
	{
	  moduleSearchIndex = i;
	  return true;
	}
    }
  return false;
}

void
ObjectFile::finish (void)
{
  /* If the target does not directly support static constructors,
     staticCtorList contains a list of all static constructors defined
     so far.  This routine will create a function to call all of those
     and is picked up by collect2. */
  if (staticCtorList.dim)
    {
      doFunctionToCallFunctions (IDENTIFIER_POINTER (get_file_function_name ("I")),
				 &staticCtorList, true);
    }
  if (staticDtorList.dim)
    {
      doFunctionToCallFunctions (IDENTIFIER_POINTER (get_file_function_name ("D")),
				 &staticDtorList, true);
    }
}

void
ObjectFile::doLineNote (const Loc& loc)
{
  if (loc.filename)
    setLoc (loc);
  // else do nothing
}

static location_t
cvtLocToloc_t (const Loc loc)
{
  location_t gcc_location;

  linemap_add (line_table, LC_ENTER, 0, loc.filename, loc.linnum);
  linemap_line_start (line_table, loc.linnum, 0);
  gcc_location = linemap_position_for_column (line_table, 0);
  linemap_add (line_table, LC_LEAVE, 0, NULL, 0);

  return gcc_location;
}

void
ObjectFile::setLoc (const Loc& loc)
{
  if (loc.filename)
    {
      input_location = cvtLocToloc_t (loc);
    }
  // else do nothing
}

void
ObjectFile::setDeclLoc (tree t, const Loc& loc)
{
  // DWARF2 will often crash if the DECL_SOURCE_FILE is not set.  It's
  // easier the error here.
  gcc_assert (loc.filename);
  DECL_SOURCE_LOCATION (t) = cvtLocToloc_t (loc);
}

void
ObjectFile::setDeclLoc (tree t, Dsymbol *decl)
{
  Dsymbol *dsym = decl;
  while (dsym)
    {
      if (dsym->loc.filename)
	{
	  setDeclLoc (t, dsym->loc);
	  return;
	}
      dsym = dsym->toParent();
    }

  // fallback; backend sometimes crashes if not set

  Loc l;

  Module *m = decl->getModule();
  if (m && m->srcfile && m->srcfile->name)
    l.filename = m->srcfile->name->str;
  else
    l.filename = "<no_file>"; // Emptry string can mess up debug info

  l.linnum = 1;
  setDeclLoc (t, l);
}

void
ObjectFile::setCfunEndLoc (const Loc& loc)
{
  tree fn_decl = cfun->decl;
  if (loc.filename)
    cfun->function_end_locus = cvtLocToloc_t (loc);
  else
    cfun->function_end_locus = DECL_SOURCE_LOCATION (fn_decl);
}

void
ObjectFile::giveDeclUniqueName (tree decl, const char *prefix)
{
  /* It would be nice to be able to use TRANSLATION_UNIT_DECL
     so lhd_set_decl_assembler_name would do this automatically.
     Unforntuately, the non-NULL decl context confuses dwarf2out.

     Maybe this is fixed in later versions of GCC.
     */
  const char *name;
  if (prefix)
    name = prefix;
  else if (DECL_NAME (decl))
    name = IDENTIFIER_POINTER (DECL_NAME (decl));
  else
    name = "___s";

  char *label;
  ASM_FORMAT_PRIVATE_NAME (label, name, DECL_UID (decl));
  SET_DECL_ASSEMBLER_NAME (decl, get_identifier (label));

  if (!DECL_NAME (decl))
    DECL_NAME (decl) = DECL_ASSEMBLER_NAME (decl);
}

/* Return the COMDAT group into which DECL should be placed. */

static tree
d_comdat_group (tree decl)
{
  // If already part of a comdat group, use that.
  if (DECL_COMDAT_GROUP (decl))
    return DECL_COMDAT_GROUP (decl);

  return DECL_ASSEMBLER_NAME (decl);
}


void
ObjectFile::makeDeclOneOnly (tree decl_tree)
{
  if (!D_DECL_IS_TEMPLATE (decl_tree) || gen.emitTemplates != TEprivate)
    {
      // Weak definitions have to be public.
      if (!TREE_PUBLIC (decl_tree))
	return;
    }

  /* First method: Use one-only.  If user has specified -femit-templates,
     honor that even if the target supports one-only. */
  if (!D_DECL_IS_TEMPLATE (decl_tree) || gen.emitTemplates != TEprivate)
    {
      // Necessary to allow DECL_ONE_ONLY or DECL_WEAK functions to be inlined
      if (TREE_CODE (decl_tree) == FUNCTION_DECL)
	DECL_DECLARED_INLINE_P (decl_tree) = 1;

      // The following makes assumptions about the behavior of make_decl_one_only.
      if (SUPPORTS_ONE_ONLY)
	{
	  make_decl_one_only (decl_tree, d_comdat_group (decl_tree));
	  return;
	}
      else if (SUPPORTS_WEAK)
	{
	  tree decl_init = DECL_INITIAL (decl_tree);
	  DECL_INITIAL (decl_tree) = integer_zero_node;
	  make_decl_one_only (decl_tree, d_comdat_group (decl_tree));
	  DECL_INITIAL (decl_tree) = decl_init;
	  return;
	}
    }
  /* Second method: Make a private copy.  For RTTI, we can always make
     a private copy.  For templates, only do this if the user specified
     -femit-templates. */
  else if (gen.emitTemplates == TEprivate)
    {
      TREE_PRIVATE (decl_tree) = 1;
      TREE_PUBLIC (decl_tree) = 0;
    }

  /* Fallback, cannot have multiple copies. */
  if (DECL_INITIAL (decl_tree) == NULL_TREE
      || DECL_INITIAL (decl_tree) == error_mark_node)
    DECL_COMMON (decl_tree) = 1;
}

void
ObjectFile::setupSymbolStorage (Dsymbol *dsym, tree decl_tree, bool force_static_public)
{
  Declaration *real_decl = dsym->isDeclaration();
  FuncDeclaration *func_decl = real_decl ? real_decl->isFuncDeclaration() : 0;

  if (force_static_public
      || (TREE_CODE (decl_tree) == VAR_DECL && (real_decl && real_decl->isDataseg()))
      || (TREE_CODE (decl_tree) == FUNCTION_DECL))
    {
      bool has_module = false;
      bool is_template = false;
      Dsymbol *sym = dsym->toParent();
      Module *ti_obj_file_mod;

      while (sym)
	{
	  TemplateInstance *ti = sym->isTemplateInstance();
	  if (ti)
	    {
	      ti_obj_file_mod = ti->objFileModule;
	      is_template = true;
	      break;
	    }
	  sym = sym->toParent();
	}

      if (is_template)
	{
	  D_DECL_ONE_ONLY (decl_tree) = 1;
	  D_DECL_IS_TEMPLATE (decl_tree) = 1;
	  has_module = hasModule (ti_obj_file_mod) && gen.emitTemplates != TEnone;
	}
      else
	has_module = hasModule (dsym->getModule());

      if (real_decl)
	{
	  if (real_decl->isVarDeclaration()
	      && real_decl->storage_class & STCextern)
	    has_module = false;
	}

      if (has_module)
	{
	  DECL_EXTERNAL (decl_tree) = 0;
	  TREE_STATIC (decl_tree) = 1;

	  if (real_decl && real_decl->storage_class & STCcomdat)
	    D_DECL_ONE_ONLY (decl_tree) = 1;
	}
      else
	{
	  DECL_EXTERNAL (decl_tree) = 1;
	  TREE_STATIC (decl_tree) = 0;
	}

      // Do this by default, but allow private templates to override
      if (!func_decl || !func_decl->isNested() || force_static_public)
	TREE_PUBLIC (decl_tree) = 1;

      if (D_DECL_ONE_ONLY (decl_tree))
	makeDeclOneOnly (decl_tree);
    }
  else
    {
      TREE_STATIC (decl_tree) = 0;
      DECL_EXTERNAL (decl_tree) = 0;
      TREE_PUBLIC (decl_tree) = 0;
    }

  if (real_decl && real_decl->userAttributes)
    decl_attributes (&decl_tree, build_attributes (real_decl->userAttributes), 0);
  else if (DECL_ATTRIBUTES (decl_tree) != NULL)
    decl_attributes (&decl_tree, DECL_ATTRIBUTES (decl_tree), 0);
}

void
ObjectFile::setupStaticStorage (Dsymbol *dsym, tree decl_tree)
{
  setupSymbolStorage (dsym, decl_tree, true);
}


void
ObjectFile::outputStaticSymbol (Symbol *s)
{
  tree t = s->Stree;
  gcc_assert (t);

  if (s->prettyIdent)
    DECL_NAME (t) = get_identifier (s->prettyIdent);
  if (s->Salignment > 0)
    {
      DECL_ALIGN (t) = s->Salignment * BITS_PER_UNIT;
      DECL_USER_ALIGN (t) = 1;
    }

  d_add_global_declaration (t);

  // %% Hack
  // Defer output of tls symbols to ensure that
  // _tlsstart gets emitted first.
  if (!DECL_THREAD_LOCAL_P (t))
    rest_of_decl_compilation (t, 1, 0);
  else
    {
      tree sinit = DECL_INITIAL (t);
      DECL_INITIAL (t) = NULL_TREE;

      DECL_DEFER_OUTPUT (t) = 1;
      rest_of_decl_compilation (t, 1, 0);
      DECL_INITIAL (t) = sinit;
    }
}

void
ObjectFile::outputFunction (FuncDeclaration *f)
{
  Symbol *s = f->toSymbol();
  tree t = s->Stree;

  gcc_assert (TREE_CODE (t) == FUNCTION_DECL);

  // Write out _tlsstart/_tlsend.
  if (f->isMain() || f->isWinMain() || f->isDllMain())
    build_tlssections();

  if (s->prettyIdent)
    DECL_NAME (t) = get_identifier (s->prettyIdent);

  d_add_global_declaration (t);

  if (!targetm.have_ctors_dtors)
    {
      if (DECL_STATIC_CONSTRUCTOR (t))
	staticCtorList.push (f);
      if (DECL_STATIC_DESTRUCTOR (t))
	staticDtorList.push (f);
    }

  if (!gen.functionNeedsChain (f))
    {
      bool context = decl_function_context (t) != NULL;
      cgraph_finalize_function (t, context);
    }
}

/* Multiple copies of the same template instantiations can
   be passed to the backend from the frontend leaving
   assembler errors left in their wrath.

   One such example:
   class c (int i = -1) {}
   c!() aa = new c!()();

   So put these symbols - as generated by toSymbol,
   toInitializer, toVtblSymbol - on COMDAT.
   */
static StringTable *symtab = NULL;

bool
ObjectFile::shouldEmit (Declaration *d_sym)
{
  // If errors occurred compiling it.
  if (d_sym->type->ty == Tfunction && ((TypeFunction *) d_sym->type)->next->ty == Terror)
    return false;

  FuncDeclaration *fd = d_sym->isFuncDeclaration();
  if (fd && fd->isNested())
    {
      // Typically, an error occurred whilst compiling
      if (fd->fbody && !fd->vthis)
	{
	  gcc_assert (global.errors);
	  return false;
	}
    }

  // Defer emitting nested functions whose parent isn't started yet.
  // If the parent never gets emitted, then neither will fd.
  Dsymbol *outer = fd->toParent2();
  if (outer && outer->isFuncDeclaration())
    {
      Symbol *osym = outer->toSymbol();
      if (osym->outputStage != Finished)
	{
	  ((FuncDeclaration *) outer)->deferred.push (fd);
	  return false;
	}
    }

  return shouldEmit (d_sym->toSymbol());
}

bool
ObjectFile::shouldEmit (Symbol *sym)
{
  gcc_assert (sym);

  // If have already started emitting, continue doing so.
  if (sym->outputStage)
    return true;

  if (D_DECL_ONE_ONLY (sym->Stree))
    {
      tree id = DECL_ASSEMBLER_NAME (sym->Stree);
      const char *ident = IDENTIFIER_POINTER (id);
      size_t len = IDENTIFIER_LENGTH (id);

      gcc_assert (ident != NULL);

      if (!symtab)
	{
	  symtab = new StringTable;
	  symtab->init();
	}

      if (!symtab->insert (ident, len))
	/* Don't emit, assembler name already in symtab. */
	return false;
    }

  // Not emitting templates, so return true all others.
  if (gen.emitTemplates == TEnone)
    return !D_DECL_IS_TEMPLATE (sym->Stree);

  return true;
}

void
ObjectFile::addAggMethod (tree rec_type, FuncDeclaration *fd)
{
  if (write_symbols != NO_DEBUG)
    {
      tree methods = TYPE_METHODS (rec_type);
      tree t = fd->toSymbol()->Stree;
      TYPE_METHODS (rec_type) = chainon (methods, t);
    }
}

void
ObjectFile::initTypeDecl (tree t, Dsymbol *d_sym)
{
  gcc_assert (!POINTER_TYPE_P (t));
  if (!TYPE_STUB_DECL (t))
    {
      const char *name = d_sym->ident ? d_sym->ident->string : "fix";
      tree decl = build_decl (UNKNOWN_LOCATION, TYPE_DECL, get_identifier (name), t);
      DECL_CONTEXT (decl) = gen.declContext (d_sym);
      setDeclLoc (decl, d_sym);
      initTypeDecl (t, decl);
    }
}


void
ObjectFile::declareType (tree t, Type *d_type)
{
  // Note: It is not safe to call d_type->toCtype().
  Loc l;
  tree decl = build_decl (UNKNOWN_LOCATION, TYPE_DECL, get_identifier (d_type->toChars()), t);
  l.filename = "<internal>";
  l.linnum = 1;
  setDeclLoc (decl, l);

  initTypeDecl (t, decl);
  declareType (decl);
}

void
ObjectFile::declareType (tree t, Dsymbol *d_sym)
{
  initTypeDecl (t, d_sym);
  declareType (TYPE_NAME (t));
}


void
ObjectFile::initTypeDecl (tree t, tree decl)
{
  if (TYPE_STUB_DECL (t))
    return;

  gcc_assert (!POINTER_TYPE_P (t));

  TYPE_CONTEXT (t) = DECL_CONTEXT (decl);
  TYPE_NAME (t) = decl;

  if (TREE_CODE (t) == ENUMERAL_TYPE
      || TREE_CODE (t) == RECORD_TYPE
      || TREE_CODE (t) == UNION_TYPE)
    {
      /* Not sure if there is a need for separate TYPE_DECLs in
	 TYPE_NAME and TYPE_STUB_DECL. */
      TYPE_STUB_DECL (t) = decl;
      DECL_ARTIFICIAL (decl) = 1;
    }
}

void
ObjectFile::declareType (tree decl)
{
  bool top_level = !DECL_CONTEXT (decl);
  // okay to do this?
  rest_of_decl_compilation (decl, top_level, 0);
}

tree
ObjectFile::stripVarDecl (tree value)
{
  if (TREE_CODE (value) != VAR_DECL)
    return value;

  if (DECL_INITIAL (value))
    return DECL_INITIAL (value);

  gcc_unreachable ();
}

void
ObjectFile::doThunk (tree thunk_decl, tree target_decl, int offset)
{
  if (current_function_decl)
    {
      DeferredThunk *t = new DeferredThunk;
      t->decl = thunk_decl;
      t->target = target_decl;
      t->offset = offset;
      deferredThunks.push (t);
    }
  else
    outputThunk (thunk_decl, target_decl, offset);
}

/* Thunk code is based on g++ */

static int thunk_labelno;

/* Create a static alias to function.  */

static tree
make_alias_for_thunk (tree function)
{
  tree alias;
  char buf[256];

  // For gdc: Thunks may reference extern functions which cannot be aliased.
  if (DECL_EXTERNAL (function))
    return function;

  targetm.asm_out.generate_internal_label (buf, "LTHUNK", thunk_labelno);
  thunk_labelno++;

  alias = build_decl (DECL_SOURCE_LOCATION (function), FUNCTION_DECL,
		      get_identifier (buf), TREE_TYPE (function));
  DECL_LANG_SPECIFIC (alias) = DECL_LANG_SPECIFIC (function);
  DECL_CONTEXT (alias) = NULL;
  TREE_READONLY (alias) = TREE_READONLY (function);
  TREE_THIS_VOLATILE (alias) = TREE_THIS_VOLATILE (function);
  TREE_PUBLIC (alias) = 0;

  DECL_EXTERNAL (alias) = 0;
  DECL_ARTIFICIAL (alias) = 1;

  DECL_DECLARED_INLINE_P (alias) = 0;
  DECL_INITIAL (alias) = error_mark_node;
  DECL_ARGUMENTS (alias) = copy_list (DECL_ARGUMENTS (function));

  TREE_ADDRESSABLE (alias) = 1;
  TREE_USED (alias) = 1;
  SET_DECL_ASSEMBLER_NAME (alias, DECL_NAME (alias));
  TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (alias)) = 1;

  if (!flag_syntax_only)
    {
      struct cgraph_node *aliasn;
      aliasn = cgraph_same_body_alias (cgraph_get_create_node (function),
				       alias, function);
      DECL_ASSEMBLER_NAME (function);
      gcc_assert (aliasn != NULL);
    }
  return alias;
}

void
ObjectFile::outputThunk (tree thunk_decl, tree target_decl, int offset)
{
  /* Settings used to output D thunks.  */
  int fixed_offset = -offset;
  bool this_adjusting = true;
  int virtual_value = 0;
  tree alias;

  if (TARGET_USE_LOCAL_THUNK_ALIAS_P (target_decl))
    alias = make_alias_for_thunk (target_decl);
  else
    alias = target_decl;

  TREE_ADDRESSABLE (target_decl) = 1;
  TREE_USED (target_decl) = 1;

  if (flag_syntax_only)
    {
      TREE_ASM_WRITTEN (thunk_decl) = 1;
      return;
    }

  if (TARGET_USE_LOCAL_THUNK_ALIAS_P (target_decl)
      && targetm_common.have_named_sections)
    {
      resolve_unique_section (target_decl, 0, flag_function_sections);

      if (DECL_SECTION_NAME (target_decl) != NULL && DECL_ONE_ONLY (target_decl))
	{
	  resolve_unique_section (thunk_decl, 0, flag_function_sections);
	  /* Output the thunk into the same section as function.  */
	  DECL_SECTION_NAME (thunk_decl) = DECL_SECTION_NAME (target_decl);
	}
    }

  /* Set up cloned argument trees for the thunk.  */
  tree t = NULL_TREE;
  for (tree a = DECL_ARGUMENTS (target_decl); a; a = DECL_CHAIN (a))
    {
      tree x = copy_node (a);
      DECL_CHAIN (x) = t;
      DECL_CONTEXT (x) = thunk_decl;
      SET_DECL_RTL (x, NULL);
      DECL_HAS_VALUE_EXPR_P (x) = 0;
      TREE_ADDRESSABLE (x) = 0;
      t = x;
    }
  DECL_ARGUMENTS (thunk_decl) = nreverse (t);
  TREE_ASM_WRITTEN (thunk_decl) = 1;

  if (!DECL_EXTERNAL (target_decl))
    {
      struct cgraph_node *funcn, *thunk_node;

      funcn = cgraph_get_create_node (target_decl);
      gcc_assert (funcn);
      thunk_node = cgraph_add_thunk (funcn, thunk_decl, thunk_decl,
				     this_adjusting, fixed_offset,
				     virtual_value, 0, alias);

      if (DECL_ONE_ONLY (target_decl))
	symtab_add_to_same_comdat_group ((symtab_node) thunk_node,
					 (symtab_node) funcn);
    }
  else
    {
      /* Backend will not emit thunks to external symbols unless the function is
	 being emitted in this compilation unit.  So make generated thunks weakref
	 symbols for the methods they interface with.  */
      tree id = DECL_ASSEMBLER_NAME (target_decl);
      tree attrs;

      id = build_string (IDENTIFIER_LENGTH (id), IDENTIFIER_POINTER (id));
      id = tree_cons (NULL_TREE, id, NULL_TREE);

      attrs = tree_cons (get_identifier ("alias"), id, NULL_TREE);
      attrs = tree_cons (get_identifier ("weakref"), NULL_TREE, attrs);

      DECL_INITIAL (thunk_decl) = NULL_TREE;
      DECL_EXTERNAL (thunk_decl) = 1;
      TREE_ASM_WRITTEN (thunk_decl) = 0;
      TREE_PRIVATE (thunk_decl) = 1;
      TREE_PUBLIC (thunk_decl) = 0;

      decl_attributes (&thunk_decl, attrs, 0);
      rest_of_decl_compilation (thunk_decl, 1, 0);
    }

  if (!targetm.asm_out.can_output_mi_thunk (thunk_decl, fixed_offset,
					    virtual_value, alias))
    {
      /* if varargs... */
      sorry ("backend for this target machine does not support thunks");
    }
}

FuncDeclaration *
ObjectFile::doSimpleFunction (const char *name, tree expr, bool static_ctor, bool public_fn)
{
  if (!current_module)
    current_module = d_gcc_get_output_module();

  if (name[0] == '*')
    {
      Symbol *s = current_module->toSymbolX (name + 1, 0, 0, "FZv");
      name = s->Sident;
    }

  TypeFunction *func_type = new TypeFunction (0, Type::tvoid, 0, LINKc);
  FuncDeclaration *func = new FuncDeclaration (current_module->loc, current_module->loc,
					       Lexer::idPool (name), STCstatic, func_type);
  func->loc = Loc (current_module, 1);
  func->linkage = func_type->linkage;
  func->parent = current_module;
  func->protection = public_fn ? PROTpublic : PROTprivate;

  tree func_decl = func->toSymbol()->Stree;
  if (static_ctor)
    DECL_STATIC_CONSTRUCTOR (func_decl) = 1; // apparently, the back end doesn't do anything with this

  // D static ctors, dtors, unittests, and the ModuleInfo chain function
  // are always private (see ObjectFile::setupSymbolStorage, default case)
  TREE_PUBLIC (func_decl) = public_fn;
  TREE_USED (func_decl) = 1;

  // %% maybe remove the identifier
  WrappedExp *body = new WrappedExp (current_module->loc, TOKcomma, expr, Type::tvoid);
  func->fbody = new ExpStatement (current_module->loc, body);
  func->toObjFile (0);

  return func;
}

/* force: If true, create a new function even there is only one function in the
   list.
   */
FuncDeclaration *
ObjectFile::doFunctionToCallFunctions (const char *name, FuncDeclarations *functions, bool force_and_public)
{
  tree expr_list = NULL_TREE;

  // If there is only one function, just return that
  if (functions->dim == 1 && !force_and_public)
    {
      return (*functions)[0];
    }
  else
    {
      // %% shouldn't front end build these?
      for (size_t i = 0; i < functions->dim; i++)
	{
	  tree fndecl = ((*functions)[i])->toSymbol()->Stree;
	  tree call_expr = d_build_call (void_type_node, build_address (fndecl), NULL_TREE);
	  expr_list = maybe_vcompound_expr (expr_list, call_expr);
	}
    }
  if (expr_list)
    return doSimpleFunction (name, expr_list, false, false);

  return NULL;
}


/* Same as doFunctionToCallFunctions, but includes a gate to
   protect static ctors in templates getting called multiple times.
   */
FuncDeclaration *
ObjectFile::doCtorFunction (const char *name, FuncDeclarations *functions, VarDeclarations *gates)
{
  tree expr_list = NULL_TREE;

  // If there is only one function, just return that
  if (functions->dim == 1 && !gates->dim)
    {
      return (*functions)[0];
    }
  else
    {
      // Increment gates first.
      for (size_t i = 0; i < gates->dim; i++)
	{
	  VarDeclaration *var = (*gates)[i];
	  tree var_decl = var->toSymbol()->Stree;
	  tree var_expr = build2 (MODIFY_EXPR, void_type_node, var_decl,
				  build2 (PLUS_EXPR, TREE_TYPE (var_decl), var_decl, integer_one_node));
	  expr_list = maybe_vcompound_expr (expr_list, var_expr);
	}
      // Call Ctor Functions
      for (size_t i = 0; i < functions->dim; i++)
	{
	  tree fndecl = ((*functions)[i])->toSymbol()->Stree;
	  tree call_expr = d_build_call (void_type_node, build_address (fndecl), NULL_TREE);
	  expr_list = maybe_vcompound_expr (expr_list, call_expr);
	}
    }
  if (expr_list)
    return doSimpleFunction (name, expr_list, false, false);

  return NULL;
}

/* Same as doFunctionToCallFunctions, but calls all functions in
   the reverse order that the constructors were called in.
   */
FuncDeclaration *
ObjectFile::doDtorFunction (const char *name, FuncDeclarations *functions)
{
  tree expr_list = NULL_TREE;

  // If there is only one function, just return that
  if (functions->dim == 1)
    {
      return (*functions)[0];
    }
  else
    {
      for (int i = functions->dim - 1; i >= 0; i--)
	{
	  tree fndecl = ((*functions)[i])->toSymbol()->Stree;
	  tree call_expr = d_build_call (void_type_node, build_address (fndecl), NULL_TREE);
	  expr_list = maybe_vcompound_expr (expr_list, call_expr);
	}
    }
  if (expr_list)
    return doSimpleFunction (name, expr_list, false, false);

  return NULL;
}

/* Currently just calls doFunctionToCallFunctions
*/
FuncDeclaration *
ObjectFile::doUnittestFunction (const char *name, FuncDeclarations *functions)
{
  return doFunctionToCallFunctions (name, functions);
}

// Finish up a symbol declaration and compile it all the way to
// the assembler language output.

void
d_finish_symbol (Symbol *sym)
{
  if (!sym->Stree)
    {
      gcc_assert (!sym->Sident);

      tree init = dtlist_to_tree (sym->Sdt);
      tree var = build_decl (UNKNOWN_LOCATION, VAR_DECL, NULL_TREE, TREE_TYPE (init));
      object_file->giveDeclUniqueName (var);

      DECL_INITIAL (var) = init;
      TREE_STATIC (var) = 1;
      TREE_USED (var) = 1;
      TREE_PRIVATE (var) = 1;
      DECL_IGNORED_P (var) = 1;
      DECL_ARTIFICIAL (var) = 1;

      sym->Stree = var;
    }

  tree t = sym->Stree;

  if (sym->Sdt)
    {
      if (DECL_INITIAL (t) == NULL_TREE)
	{
	  tree sinit = dtlist_to_tree (sym->Sdt);
	  if (TREE_TYPE (t) == d_unknown_type_node)
	    {
	      TREE_TYPE (t) = TREE_TYPE (sinit);
	      TYPE_NAME (TREE_TYPE (t)) = get_identifier (sym->Sident);
	    }

	  DECL_INITIAL (t) = sinit;
	}
      gcc_assert (COMPLETE_TYPE_P (TREE_TYPE (t)));
    }

  gcc_assert (!error_mark_p (t));

  if (DECL_INITIAL (t) != NULL_TREE)
    {
      TREE_STATIC (t) = 1;
      DECL_EXTERNAL (t) = 0;
    }

  /* If the symbol was marked as readonly in the frontend, set TREE_READONLY.  */
  if (sym->Sreadonly)
    TREE_READONLY (t) = 1;

  DECL_CONTEXT (t) = decl_function_context (t);

  if (!object_file->shouldEmit (sym))
    return;

  // This was for typeinfo decls ... shouldn't happen now.
  // %% Oops, this was supposed to be static.
  gcc_assert (!DECL_EXTERNAL (t));
  relayout_decl (t);

#ifdef ENABLE_TREE_CHECKING
  if (DECL_INITIAL (t) != NULL_TREE) 
    {
      // Initialiser must never be bigger than symbol size.
      dinteger_t tsize = int_size_in_bytes (TREE_TYPE (t));
      dinteger_t dtsize = int_size_in_bytes (TREE_TYPE (DECL_INITIAL (t)));
      if (tsize != dtsize)
	internal_error ("Mismatch between declaration '%s' size (%wd) and it's initializer size (%wd).",
			sym->prettyIdent ? sym->prettyIdent : sym->Sident, tsize, dtsize);
    }
#endif

  object_file->outputStaticSymbol (sym);
}

// Generate our module reference and append to _Dmodule_ref.

void
build_moduleinfo (Symbol *sym)
{
  // Generate:
  //   struct ModuleReference
  //   {
  //     void *next;
  //     ModuleReference m;
  //   }

  // struct ModuleReference in moduleinit.d
  Type *obj_type = build_object_type();
  tree modref_type_node = build_two_field_type (ptr_type_node, obj_type->toCtype(),
						NULL, "next", "mod");
  tree fld_next = TYPE_FIELDS (modref_type_node);
  tree fld_mod = TREE_CHAIN (fld_next);

  // extern (C) ModuleReference *_Dmodule_ref;
  tree module_ref = build_decl (BUILTINS_LOCATION, VAR_DECL,
				get_identifier ("_Dmodule_ref"),
				build_pointer_type (modref_type_node));
  d_keep (module_ref);
  DECL_EXTERNAL (module_ref) = 1;
  TREE_PUBLIC (module_ref) = 1;

  // private ModuleReference our_mod_ref = { next: null, mod: _ModuleInfo_xxx };
  tree our_mod_ref = build_decl (UNKNOWN_LOCATION, VAR_DECL, NULL_TREE, modref_type_node);
  d_keep (our_mod_ref);
  object_file->giveDeclUniqueName (our_mod_ref, "__mod_ref");
  object_file->setDeclLoc (our_mod_ref, current_module);

  DECL_ARTIFICIAL (our_mod_ref) = 1;
  DECL_IGNORED_P (our_mod_ref) = 1;
  TREE_PRIVATE (our_mod_ref) = 1;
  TREE_STATIC (our_mod_ref) = 1;

  CtorEltMaker ce;
  ce.cons (fld_next, d_null_pointer);
  ce.cons (fld_mod, build_address (sym->Stree));

  DECL_INITIAL (our_mod_ref) = build_constructor (modref_type_node, ce.head);
  TREE_STATIC (DECL_INITIAL (our_mod_ref)) = 1;
  rest_of_decl_compilation (our_mod_ref, 1, 0);

  // void ___modinit()  // a static constructor
  // {
  //   our_mod_ref.next = _Dmodule_ref;
  //   _Dmodule_ref = &our_mod_ref;
  // }
  tree m1 = vmodify_expr (component_ref (our_mod_ref, fld_next), module_ref);
  tree m2 = vmodify_expr (module_ref, build_address (our_mod_ref));

  object_file->doSimpleFunction ("*__modinit", vcompound_expr (m1, m2), true);
}

// Put out symbols that define the beginning and end
// of the thread local storage sections.

void
build_tlssections (void)
{
  /* Generate:
	__thread int _tlsstart = 3;
	__thread int _tlsend;
   */
  tree tlsstart, tlsend;

  tlsstart = build_decl (UNKNOWN_LOCATION, VAR_DECL,
			 get_identifier ("_tlsstart"), integer_type_node);
  TREE_PUBLIC (tlsstart) = 1;
  TREE_STATIC (tlsstart) = 1;
  DECL_ARTIFICIAL (tlsstart) = 1;
  // DECL_INITIAL so the symbol goes in .tdata
  DECL_INITIAL (tlsstart) = build_int_cst (integer_type_node, 3);
  DECL_TLS_MODEL (tlsstart) = decl_default_tls_model (tlsstart);
  object_file->setDeclLoc (tlsstart, current_module);
  rest_of_decl_compilation (tlsstart, 1, 0);

  tlsend = build_decl (UNKNOWN_LOCATION, VAR_DECL,
		       get_identifier ("_tlsend"), integer_type_node);
  TREE_PUBLIC (tlsend) = 1;
  TREE_STATIC (tlsend) = 1;
  DECL_ARTIFICIAL (tlsend) = 1;
  // DECL_COMMON so the symbol goes in .tcommon
  DECL_COMMON (tlsend) = 1;
  DECL_TLS_MODEL (tlsend) = decl_default_tls_model (tlsend);
  object_file->setDeclLoc (tlsend, current_module);
  rest_of_decl_compilation (tlsend, 1, 0);
}

