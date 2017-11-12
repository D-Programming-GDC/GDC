
// Compiler implementation of the D programming language
// Copyright: Copyright (c) 2014 by Digital Mars, All Rights Reserved
// Authors: Walter Bright, http://www.digitalmars.com
// License: http://boost.org/LICENSE_1_0.txt
// Source: https://github.com/D-Programming-Language/dmd/blob/master/src/nspace.c


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "nspace.h"
#include "identifier.h"
#include "scope.h"

void semantic(Dsymbol *dsym, Scope *sc);

/* This implements namespaces.
 */

Nspace::Nspace(Loc loc, Identifier *ident, Dsymbols *members)
    : ScopeDsymbol(ident)
{
    //printf("Nspace::Nspace(ident = %s)\n", ident->toChars());
    this->loc = loc;
    this->members = members;
}

Dsymbol *Nspace::syntaxCopy(Dsymbol *s)
{
    Nspace *ns = new Nspace(loc, ident, NULL);
    return ScopeDsymbol::syntaxCopy(ns);
}

void Nspace::semantic3(Scope *sc)
{
    if (semanticRun >= PASSsemantic3)
        return;
    semanticRun = PASSsemantic3;
#if LOG
    printf("Nspace::semantic3('%s')\n", toChars());
#endif
    if (members)
    {
        sc = sc->push(this);
        sc->linkage = LINKcpp;
        for (size_t i = 0; i < members->dim; i++)
        {
            Dsymbol *s = (*members)[i];
            s->semantic3(sc);
        }
        sc->pop();
    }
}

const char *Nspace::kind()
{
    return "namespace";
}

bool Nspace::oneMember(Dsymbol **ps, Identifier *ident)
{
    return Dsymbol::oneMember(ps, ident);
}

Dsymbol *Nspace::search(Loc loc, Identifier *ident, int flags)
{
    //printf("%s::Nspace::search('%s')\n", toChars(), ident->toChars());
    if (_scope && !symtab)
        semantic(this, _scope);

    if (!members || !symtab) // opaque or semantic() is not yet called
    {
        error("is forward referenced when looking for '%s'", ident->toChars());
        return NULL;
    }

    return ScopeDsymbol::search(loc, ident, flags);
}

int Nspace::apply(Dsymbol_apply_ft_t fp, void *param)
{
    if (members)
    {
        for (size_t i = 0; i < members->dim; i++)
        {
            Dsymbol *s = (*members)[i];
            if (s)
            {
                if (s->apply(fp, param))
                    return 1;
            }
        }
    }
    return 0;
}

bool Nspace::hasPointers()
{
    //printf("Nspace::hasPointers() %s\n", toChars());

    if (members)
    {
        for (size_t i = 0; i < members->dim; i++)
        {
            Dsymbol *s = (*members)[i];
            //printf(" s = %s %s\n", s->kind(), s->toChars());
            if (s->hasPointers())
            {
                return true;
            }
        }
    }
    return false;
}

void Nspace::setFieldOffset(AggregateDeclaration *ad, unsigned *poffset, bool isunion)
{
    //printf("Nspace::setFieldOffset() %s\n", toChars());
    if (_scope)                  // if fwd reference
        semantic(this, NULL);         // try to resolve it
    if (members)
    {
        for (size_t i = 0; i < members->dim; i++)
        {
            Dsymbol *s = (*members)[i];
            //printf("\t%s\n", s->toChars());
            s->setFieldOffset(ad, poffset, isunion);
        }
    }
}
