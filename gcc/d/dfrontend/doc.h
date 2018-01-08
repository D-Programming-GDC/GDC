
/* Compiler implementation of the D programming language
 * Copyright (C) 1999-2018 by The D Language Foundation, All Rights Reserved
 * All Rights Reserved
 * written by Walter Bright
 * http://www.digitalmars.com
 * Distributed under the Boost Software License, Version 1.0.
 * http://www.boost.org/LICENSE_1_0.txt
 * https://github.com/D-Programming-Language/dmd/blob/master/src/doc.h
 */

#ifndef DMD_DOC_H
#define DMD_DOC_H

#ifdef __DMC__
#pragma once
#endif /* __DMC__ */

void escapeDdocString(OutBuffer *buf, size_t start);
void gendocfile(Module *m);

#endif
