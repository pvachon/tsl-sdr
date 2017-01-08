/*
 *  errors.c - Human-readable mappings for error codes from the TSL.
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <tsl/errors.h>

const char *tsl_result_to_string(aresult_t result)
{
    switch (result) {
    case A_OK:
        return "A_OK";

    case A_E_NOMEM:
        return "A_E_NOMEM";
    case A_E_BADARGS:
        return "A_E_BADARGS";
    case A_E_NOTFOUND:
        return "A_E_NOTFOUND";
    case A_E_BUSY:
        return "A_E_BUSY";
    case A_E_INVAL:
        return "A_E_INVAL";
    case A_E_NOTHREAD:
        return "A_E_NOTHREAD";
    case A_E_EMPTY:
        return "A_E_EMPTY";
    case A_E_NO_SOCKET:
        return "A_E_NO_SOCKET";
    case A_E_NOENT:
        return "A_E_NOENT";
    case A_E_INV_DATE:
        return "A_E_INV_DATE";
    case A_E_NOSPC:
        return "A_E_NOSPC";
    case A_E_EXIST:
        return "A_E_EXIST";
    case A_E_UNKNOWN:
        return "A_E_UNKNOWN";
    case A_E_DONE:
        return "A_E_DONE";
    case A_E_OVERFLOW:
        return "A_E_OVERFLOW";
    case A_E_FULL:
        return "A_E_FULL";
    case A_E_EOF:
        return "A_E_EOF";
    case A_E_REJECTED:
        return "A_E_REJECTED";
    case A_E_TIMEOUT:
        return "A_E_TIMEOUT";

    default:
        return "Unknown";
    }
}

const char *tsl_result_to_string_friendly(aresult_t result)
{
    switch (result) {
    case A_OK:
        return "OK";

    case A_E_NOMEM:
        return "Out of memory";
    case A_E_BADARGS:
        return "Bad arguments";
    case A_E_NOTFOUND:
        return "Not found";
    case A_E_BUSY:
        return "Busy / in use";
    case A_E_INVAL:
        return "Invalid reference";
    case A_E_NOTHREAD:
        return "Thread not found";
    case A_E_EMPTY:
        return "Empty";
    case A_E_NO_SOCKET:
        return "No socket";
    case A_E_NOENT:
        return "No entity";
    case A_E_INV_DATE:
        return "Invalid date";
    case A_E_NOSPC:
        return "No space";
    case A_E_EXIST:
        return "Item already exists";
    case A_E_UNKNOWN:
        return "Unknown error (A_E_UNKNOWN)";
    case A_E_DONE:
        return "Done";
    case A_E_OVERFLOW:
        return "Overflow";
    case A_E_FULL:
        return "Full";
    case A_E_EOF:
        return "EOF";
    case A_E_REJECTED:
        return "Rejected";
    case A_E_TIMEOUT:
        return "Timeout";

    default:
        return "Unknown";
    }
}

