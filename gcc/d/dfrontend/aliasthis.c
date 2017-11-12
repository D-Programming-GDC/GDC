
/* Compiler implementation of the D programming language
 * Copyright (c) 2009-2014 by Digital Mars
 * All Rights Reserved
 * written by Walter Bright
 * http://www.digitalmars.com
 * Distributed under the Boost Software License, Version 1.0.
 * http://www.boost.org/LICENSE_1_0.txt
 * https://github.com/D-Programming-Language/dmd/blob/master/src/aliasthis.c
 */

#include <stdio.h>
#include <assert.h>

#include "aliasthis.h"
#include "scope.h"
#include "aggregate.h"

Expression *semantic(Expression *e, Scope *sc);

Expression *resolveAliasThis(Scope *sc, Expression *e, bool gag)
{
    AggregateDeclaration *ad = isAggregate(e->type);

    if (ad && ad->aliasthis)
    {
        unsigned olderrors = gag ? global.startGagging() : 0;

        Loc loc = e->loc;
        Type *tthis = (e->op == TOKtype ? e->type : NULL);
        e = new DotIdExp(loc, e, ad->aliasthis->ident);
        e = semantic(e, sc);
        if (tthis && ad->aliasthis->needThis())
        {
            if (e->op == TOKvar)
            {
                if (FuncDeclaration *fd = ((VarExp *)e)->var->isFuncDeclaration())
                {
                    // Bugzilla 13009: Support better match for the overloaded alias this.
                    bool hasOverloads = false;
                    if (FuncDeclaration *f = fd->overloadModMatch(loc, tthis, hasOverloads))
                    {
                        if (!hasOverloads)
                            fd = f;     // use exact match
                        e = new VarExp(loc, fd, hasOverloads);
                        e->type = f->type;
                        e = new CallExp(loc, e);
                        goto L1;
                    }
                }
            }
            /* non-@property function is not called inside typeof(),
             * so resolve it ahead.
             */
            {
            int save = sc->intypeof;
            sc->intypeof = 1;   // bypass "need this" error check
            e = resolveProperties(sc, e);
            sc->intypeof = save;
            }

        L1:
            e = new TypeExp(loc, new TypeTypeof(loc, e));
            e = semantic(e, sc);
        }
        e = resolveProperties(sc, e);

        if (gag && global.endGagging(olderrors))
            e = NULL;
    }

    return e;
}

AliasThis::AliasThis(Loc loc, Identifier *ident)
    : Dsymbol(NULL)             // it's anonymous (no identifier)
{
    this->loc = loc;
    this->ident = ident;
}

Dsymbol *AliasThis::syntaxCopy(Dsymbol *s)
{
    assert(!s);
    /* Since there is no semantic information stored here,
     * we don't need to copy it.
     */
    return this;
}

const char *AliasThis::kind()
{
    return "alias this";
}
