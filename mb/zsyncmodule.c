/* "rsum" checksumming function from zsync's librcksum/rsum.c, version 0.6.1,
 * wrapped into a Python extension
 *
 * Copyright 2010,2012 Peter Poeml <poeml@mirrorbrain.org> 
 *
 * The checksumming function itself is available under the Artistic License;
 * the boilerplate was a nice exercise.
 *
 * This is something that will be a whole lot slower when programmed in a
 * scripting language, thus I wanted this Python extension. */

#include <stdlib.h>
#include <arpa/inet.h>
#include "Python.h"

static PyObject *zsync_rsum06(PyObject *self, PyObject *args) {
    char *data;
    int len;
    unsigned short a, b;
    unsigned char digest[4];
    memset(digest, 0, sizeof(digest));

    if (!PyArg_ParseTuple(args, "s#", &data, &len))
        return NULL;

    {
        register unsigned short aa = 0;
        register unsigned short bb = 0;
        while (len) {
            register unsigned char c = *data++;
            aa += c;
            bb += len * c;
            len--;
        }
        a = aa;
        b = bb;
    }

    a = htons(a);
    b = htons(b);
    memcpy((void *)&digest, &a, 2);
    memcpy((void *)&digest + 2, &b, 2);

    return PyBytes_FromStringAndSize((const char *)digest, sizeof(digest));
}

static PyMethodDef zsyncMethods[] = {
    {"rsum06",  zsync_rsum06, METH_VARARGS, "Calculate a zsync rsum value."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef zsyncModule = {
     PyModuleDef_HEAD_INIT,
    "zsync",
    "zsync rsum generation",
    -1,
    zsyncMethods
 };

PyMODINIT_FUNC
PyInit_zsync(void)
{
    return PyModule_Create(&zsyncModule);
}

/* vim: set ts=4 sw=4 expandtab smarttab: */
